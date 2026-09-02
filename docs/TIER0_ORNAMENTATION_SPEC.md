# Tier 0 — Ornamentation: per-bar score-level variation (no ML)

**Goal.** Add a deterministic, real-time-safe "drummer's hand" on top of the
existing humanization so a held groove stops sounding like a loop. This does
*not* change the 28 authored scores — it subtly mutates *which* hits land, once
per bar, in a way that is reproducible and testable.

**Scope.** `src/midi/PatternPlayer.{h,cpp}` only. No new dependencies, no heap
allocation on the audio thread, bounded O(1) work per bar.

**Why per-bar + deterministic.** A fine jitter RNG (`juce::Random rng`) is
already free-running (`prepare()` → `setSeedRandomly()`). Ornamentation decisions
must instead be a pure function of the **bar number** so that:

1. a bar that straddles two audio blocks makes the *same* decision in both blocks,
2. the same song position always plays the same ornaments (reproducible tests),
3. a DAW seek/loop back to bar N replays the same take (no "it changed" surprise).

---

## 1. Add a per-bar decision primitive

`src/midi/PatternPlayer.h` — add near the top (namespace-level helper in the
`.cpp` is fine; keep it header-visible only if tests need it):

```cpp
// SplitMix32-style avalanche (same as PatternRules::hashMix, kept local so
// PatternPlayer does not depend on the inference headers).
inline unsigned barHash(unsigned a, unsigned b) noexcept
{
    unsigned h = a * 0x9E3779B1u + b;
    h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return h;
}

// Deterministic per-bar probability gate: returns true `pct`% of bars.
inline bool barChance(int64_t barNumber, unsigned salt, int pct) noexcept
{
    return (barHash(static_cast<unsigned>(barNumber), salt) % 100u)
         < static_cast<unsigned>(pct);
}
```

## 2. Add an ornamentation struct + computation

`PatternPlayer.h` — private section, near the other Musicality-pivot state
(around line 276):

```cpp
struct BarOrnamentation
{
    bool openHat = false;       // replace ONE closed-hat cell with open hat
    int  openHatCell = -1;      // grid16 cell to open
    bool rideSwitch = false;    // closed hats -> ride (or bell) for the bar
    bool extraGhost = false;    // add one extra off-16th ghost snare
    int  extraGhostCell = -1;
    bool dropKick = false;      // omit one non-downbeat kick
    int  dropKickCell = -1;
    bool microFill = false;     // tiny tom pickup at phrase end
};

BarOrnamentation computeOrnamentation(int64_t barNumber, int patternIndex) const noexcept;
```

`PatternPlayer.cpp` — implement `computeOrnamentation`. It reads `sectionId` and
`patternIndex`, then gates each ornament by section + probability. Default
probabilities (tunable constants, put them in the anonymous namespace):

```cpp
// Per-bar probabilities (percent). Section-dependent where idiomatic.
static constexpr int kOpenHatPctChorus  = 18;   // chorus/solo loosen up
static constexpr int kOpenHatPctElse    = 8;
static constexpr int kRideSwitchPctSolo = 28;   // solo rides the cymbal
static constexpr int kRideSwitchPctChorus= 12;
static constexpr int kExtraGhostPct     = 15;   // verse/breakdown only
static constexpr int kDropKickPctBreak  = 12;   // breakdown/outro leave space
static constexpr int kMicroFillPct      = 12;   // phrase-end bars only
```

Candidate cells (off-16ths; never the downbeat/backbeat):

```cpp
// closed-hat cells that are idiomatic to open (the "and" of a beat):
static constexpr int kOpenHatCandidates[4] = { 2, 6, 10, 14 };
// non-essential kicks that may be dropped (8th-note kicks, never cell 0/8):
static constexpr int kDropKickCandidates[2] = { 2, 10 };
```

Rules (musical safety):

- **openHat** only when the pattern authors closed hats (note 42). Check
  `library->getPattern(patternIndex)` for any `drumEvents` with `note == 42`.
  Pick `kOpenHatCandidates[barHash % 4]`, but only if that cell actually has a
  closed hat in the pattern.
- **rideSwitch** only in `Chorus`/`Solo`, and only when the pattern uses closed
  hats (never ride already — patterns 16/26/27 stay untouched).
- **extraGhost** only in `Verse`/`Breakdown` (mirrors `sectionAllowsGhosts()`),
  cell from `{ 1, 9, 11, 15 }` not occupied by an authored snare.
- **dropKick** only in `Breakdown`/`Outro`, and only drop a kick on a
  `kDropKickCandidates` cell — never beat 1 or beat 3.
- **microFill** only when `(barNumber % 4) == 3` (last bar of a 4-bar phrase)
  and the active pattern is not already a fill (index not 17/18/19).

## 3. Wire it into `emitDrumEventsForRange` (lines 264–339)

Compute the ornament once for the block and apply note substitutions/drops
inside the existing event loop. Two small changes:

**(a) Open-hat / ride substitution** — inside the loop, after `outNote` is set
(line 323), before `midi.addEvent`:

```cpp
int outNote = juce::jlimit(0, 127, static_cast<int>(ev.note));
if (orn.openHat && ev.note == 42 && grid16 == orn.openHatCell)
    outNote = 46;                                   // closed -> open hat
else if (orn.rideSwitch && ev.note == 42)
    outNote = (grid16 == 0) ? 53 : 51;              // bell on 1, ride elsewhere
```

**(b) Kick drop** — skip the event entirely:

```cpp
if (orn.dropKick && ev.note == 36 && grid16 == orn.dropKickCell)
    continue;                                       // leave space
```

Both are *note replacement/drop* on already-scheduled events, so the loop
structure, microtiming, swing and jitter all apply unchanged. No new MIDI
scheduling, no allocation.

## 4. Extend `emitGhostNotes` (lines 341–387)

When `orn.extraGhost` is true, append `orn.extraGhostCell` to the candidate set
for this bar (or simply allow one additional cell). The occupancy check
(`occupied[cell]`) already prevents doubling an authored snare. Reuse the
existing velocity band (`ghostVelocityLo..Hi`) and `ghostTimingMs`.

```cpp
// after the existing occupied/continue check, before the emit:
if (!orn.extraGhost || cell != orn.extraGhostCell)
    { /* existing low/high candidate logic */ }
// else: emit the extra ghost at orn.extraGhostCell even if not in lowCells/highCells
```

## 5. Add a micro-fill pickup

New tiny method (declare in `PatternPlayer.h`, define in `.cpp`):

```cpp
void PatternPlayer::emitMicroFill(juce::MidiBuffer& midi, int numSamples,
                                  double beatStart, double beatEnd,
                                  int sampleOffsetBase) noexcept;
```

Emits, on the *last beat* of the phrase (`barNumber % 4 == 3`), a 2–3 note
pickup leading into the next downbeat, then a crash on the downbeat:

```
beat 3.75  tom-hi (48)  vel ~105
beat 3.875 tom-mid (45) vel ~108
next bar 0 crash (49)   vel ~118   (reuse emitCrashHit)
```

Keep it monophonic-per-voice, bounded, no allocation. Call it from `process()`
immediately after `emitDrumEventsForRange` when `orn.microFill && activePatternIndex != 0`.

## 6. Plumb the ornament through `process` (lines 626–849)

In `process()`, after `sectionVelMul` is set (line 649), compute once:

```cpp
const double samplesPerBar = 4.0 * samplesPerBeat;
const int64_t barNumber = static_cast<int64_t>(std::floor(
    static_cast<double>(hostSamplePosition) / samplesPerBar));
const BarOrnamentation orn = computeOrnamentation(barNumber, activePatternIndex);
```

Pass `orn` into `emitDrumEventsForRange` (new parameter, defaulted to an empty
struct so existing calls/tests still compile) and `emitGhostNotes`.

> Boundary note: a block that crosses a bar boundary applies the *block-start*
> bar's ornament for the whole block. At 256–4096-sample buffers this is a
> non-issue (a bar is 2 s at 120 BPM; a 4096-sample block is ~85 ms). Ornaments
> are subtle, so worst case one ornament lands a few ms early.

## 7. Constants table (single place to tune)

| Constant | Default | Meaning | Gate |
|---|---|---|---|
| `kOpenHatPctChorus` | 18 | open a hat cell | chorus/solo |
| `kOpenHatPctElse` | 8 | open a hat cell | verse/intro/outro |
| `kRideSwitchPctSolo` | 28 | hats → ride/bell | solo |
| `kRideSwitchPctChorus` | 12 | hats → ride/bell | chorus |
| `kExtraGhostPct` | 15 | one extra ghost snare | verse/breakdown |
| `kDropKickPctBreak` | 12 | drop a non-essential kick | breakdown/outro |
| `kMicroFillPct` | 12 | tom pickup into downbeat | phrase-end bar |

## 8. RT-safety + test checklist

- [ ] No `new`/`std::vector`/`std::string` in any added path.
- [ ] All decisions are O(1): one `barHash` + a few comparisons per bar.
- [ ] `computeOrnamentation` is `const noexcept` and reads only `sectionId`
      (value) and the pattern's `drumEvents` (const reference).
- [ ] Deterministic test: with a fixed `hostSamplePosition`, the same bar always
      produces the same ornament (assert via a small unit test in
      `tests/` calling `computeOrnamentation` directly, or by pinning the RNG
      with the existing `setRandomSeed` and checking emitted MIDI).
- [ ] No ornament ever mutates pattern 0 (Silent) or the click track.
- [ ] No ornament fires during `structureSilent` (the early-return at line 673
      already guards this — ornaments are computed after it).

## 9. Expected result

A locked groove (Record riff) and a held Play-mode phrase now vary per bar:
hats open up in the chorus, the solo switches to ride, a ghost sneaks into the
verse, the breakdown drops a kick, and every 4th bar ends with a tiny tom pickup
into a crash. Same song, same selection, but it *moves* like a drummer instead
of replaying a loop — with zero model inference and a few hours of work.
