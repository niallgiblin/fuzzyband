# Fuzzyband — Implementation Plan: Drum & Bass Response

**Companion to:** [`docs/PLAYABILITY_REVIEW.md`](PLAYABILITY_REVIEW.md)
**Target:** v0.9.67 → v1.0.0-rc
**Scope:** every defect in the review's §2, plus the playability (§3) and musicality (§4) items.
**Working codebase:** `/Users/ng/projects/fuzzyband` (the review doc lives in the Desktop docs folder)

---

## How to use this plan

* **Phases are ordered by dependency and by "value per unit of risk".** Each phase ends green: full
  test suite passing, build installable, and a short listening check.
* **One commit per task**, referenced as `fix(<task-id>): <title>`. Task ids match the review's issue
  numbers so the two documents cross-reference.
* **Every fix lands with its test.** §5 of the review lists the nine tests that would have caught all
  of this; they are distributed to the tasks that need them, and T0.4 builds the harness they need.
* **Never weaken an assertion to make a fix pass.** Three existing tests currently encode the
  regressions (T0.3) — they are corrected in Phase 0 so that later phases have an honest signal.
* **Non-goals**: no new patterns, no model retraining, no UI redesign, no new genres. This plan fixes
  and reconnects what already exists. Data regeneration (T8.4) is explicitly optional.

### Effort key

| Size | Meaning |
| --- | --- |
| S | ≤ half a day, mechanical |
| M | 1–2 days, needs a test and a listen |
| L | 3–5 days, touches the state machine |
| XL | > 5 days, design work required first |

### Decisions required before Phase 1

| # | Decision | Recommendation |
| --- | --- | --- |
| **D1** | Is a riff lock a *duration* or a *song position*? | **Duration.** Keep a monotonic clock for all lock/transition schedules and lock the bar phase once at engage (T2.1). This is what the header comments already claim the fields hold. |
| **D2** | Keep transitions uninterruptible, or restore cut-short? | **Restore cut-short for the *same* riff only**, at the next bar line. The shipped user-facing contract (`END_USER_STRESS_TEST.md` §B4) promises it; keep "don't re-lock onto a different riff". |
| **D3** | In Play, is the model argmax or the pool rotation authoritative? | **Rotation is authoritative**; the argmax picks *within* the pool (it votes). This is what `pickPoolPattern` was written for. |
| **D4** | Ornaments: variants or gated humanize? | Phase 4 does the **quick gate** (probabilities behind one `humanize` parameter, `rideSwitch` default off), Phase 8 tracks the **variant** redesign as a follow-up. |
| **D5** | Ghost fix: re-derive data or re-author the library? | **Re-author the library** so the documented threshold (62) is true, and add a unit test asserting every authored ghost is below it. Cheaper and testable; regeneration is T8.4. |
| **D6** | Frozen-riff articulation: onset detection design | Approve the "re-attack = peak rise over the previous slot" heuristic in T5.2 before implementing. |

---

## Phase 0 — Preflight

Nothing else is trustworthy until this is done. **Do not skip T0.1**: the play-test that produced the
review was run against v0.9.62.

### T0.1 — Rebuild, install, and verify the version
**Size:** S · **Deps:** none

The repo's `build/MetalAccompaniment_artefacts/Release/VST3/fuzzyband.vst3` contains only
`Info.plist` — no binary — and the installed plugin is v0.9.62 while the source is v0.9.67.

```bash
cd /Users/ng/projects/fuzzyband
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
# install/copy the VST3 + AU to ~/Library/Audio/Plug-Ins/
strings -a ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
  | grep -E '^0\.9\.[0-9]+$'          # must print 0.9.67
```

**Acceptance:** installed binary reports 0.9.67; plugin loads; UI shows the matching version string.
**Risk:** if the ONNX Runtime dylib path is stale, `tryLoadModel()` silently falls back to
`RuleBasedInference` (T0.3 covers how to detect this).

### T0.2 — Baseline capture set
**Size:** S · **Deps:** T0.1

Record the *current* behaviour before changing anything, so "before/after" is objective:

1. A fixed 4-bar Drop-C palm-mute riff into a fresh session, LOCK=4, TRANSITION=4, Metal, 120 BPM —
   capture **the plugin's MIDI output** (not audio) at buffer sizes 128 / 512 / 2048.
2. The same riff with a **4-bar DAW loop** armed.
3. A Play-mode pass through the default form at 120 BPM.

Keep the MIDI files under `.artifacts/baseline/`. These become the regression corpus for Phase 9 and
make the buffer-size-dependence of T1.1 visible immediately.

### T0.3 — Restore honest test signals
**Size:** S · **Deps:** none

Three tests currently pin the regressions. Fix the *assertions* to their documented intent and let
them fail until the relevant fix lands (mark them `[!mayfail]` if the suite must stay green
mid-phase, and remove the tag in the task that fixes them).

| File | Current | Change to |
| --- | --- | --- |
| `tests/test_e2e_groove_variety.cpp:147` | `REQUIRE(seenNames.size() >= 2)` | `>= 3` per its own title (fixed by T4.1) |
| `tests/test_pattern_player.cpp:453-456` | asserts authored bass **must not** leak | assert it **does** leak, transposed (fixed by T5.1) |
| `tests/test_processor_pipeline.cpp:1138` | "riff replay does NOT cut it short" | assert same-riff cut-short at the next bar (fixed by T6.2) |

Also add an ONNX-availability guard: a test or a startup assertion that fails loudly when
`MetalGrooveInference::tryLoadModel()` returns false under `MA_ENABLE_ONNX`, so a silent rule-based
fallback can never masquerade as working ML again.

### T0.4 — MIDI test harness
**Size:** M · **Deps:** none

Every high-value test in this plan needs to assert on rendered MIDI independent of block size.

Add to the test fixtures:

```cpp
// tests/fixtures/MidiProbe.h
struct NoteOn { int note, channel, velocity; int64_t sample; };
struct MidiProbe {
    static std::vector<NoteOn> render(AccompanimentProcessor&, int blocks, int blockSize,
                                      int64_t startSample, int64_t* endSample = nullptr);
    static std::vector<std::pair<int,int64_t>> noteOffs(...);
    // buffer-size invariance: render at 128/512/2048 and diff absolute sample positions
    static std::vector<int64_t> absoluteEventSamples(AccompanimentProcessor&, int blocks);
};
```

`absoluteEventSamples()` is the key primitive: it renders the same musical span at two block sizes and
returns every event's absolute sample position, which must be identical.

**Acceptance:** a self-test renders a fixed pattern at 128 and 2048 and asserts equality.

---

## Phase 1 — Rendering correctness

Independent, low-risk, and immediately audible. This phase fixes the "the kit sounds wrong" half of
the complaint without touching the state machine.

### T1.1 — Deferred drum note-offs (review §2.1) — **blocker**
**Size:** M · **Deps:** T0.4

**Problem.** `PatternPlayer.cpp:583` clamps every drum note-off into the triggering block
(`jmin(numSamples - 1, off + durSamps)`), so note lengths are buffer-size dependent and every cymbal
is choked to 3–46 ms.

**Change.** Add a per-note deferred note-off table (mirroring the existing `bassNoteOffSample`
pattern), flushed at the top of `process()` next to the crash flush at `:1002-1008`.

```cpp
// PatternPlayer.h  (private)
static constexpr int kDrumVoices = 128;
std::array<int64_t, kDrumVoices> drumNoteOffSample{};   // init -1 in reset()/prepare()
```

```cpp
// PatternPlayer.cpp — new helper
void PatternPlayer::scheduleDrumNoteOff(juce::MidiBuffer& midi, int numSamples,
                                        int64_t blockStart, int note, int off, int durSamps)
{
    const int64_t abs = blockStart + off + durSamps;
    if (abs < blockStart + numSamples)
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, note), off + durSamps);
    else
        drumNoteOffSample[(size_t) note] = abs;   // last-write-wins on re-trigger
}
```

Then replace the inline note-off emission in `emitDrumEventsForRange` (`:580-584`),
`emitBarFill` (`:439-442`), `emitMicroFill` (`:466-470`) and `emitGhostNotes` (`:603,625-626`) with
calls to it, and flush pending offs at the top of `process()`:

```cpp
for (int n = 0; n < kDrumVoices; ++n)
    if (drumNoteOffSample[n] >= 0 && drumNoteOffSample[n] < sampleCounter + numSamples) {
        midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, n),
                      jlimit(0, numSamples - 1, (int)(drumNoteOffSample[n] - sampleCounter)));
        drumNoteOffSample[n] = -1;
    }
```

**Also required:**
* Give open hats and cymbals a musical gate. Add an explicit `durationBeats` to open-hat library
  events (≥ 1.0) instead of inheriting the closed-hat 0.25 default; leave crashes at 2.5.
* Unify with the existing crash path — either migrate `crashNoteOffSample` onto the table or leave it
  and document why (it needs its own note number / armed semantics).
* `reset()` and `prepare()` must clear the table to -1.

**Tests (review §5.2, §5.3):**
* `Note-off ledger`: every note-on has a matching note-off in the rendered span.
* For every event authored `durationBeats >= 1`, the note-off lands in a **later** block than the
  note-on at block size 128.
* `Dual-block-size golden`: a pattern with a 2.5-beat crash renders identical absolute event samples
  at 128 and 2048.

**Acceptance:** crash/china/open-hat note-offs are ≥ 1 beat after their note-on at every block size;
the baseline MIDI from T0.2 now differs from pre-fix in exactly the long-note durations.

**Risk:** medium — this is the change most likely to expose the seek bug in T1.2, which is why T1.2
must land in the same phase.

---

### T1.2 — Seek/loop-safe note-off release (review §2.14)
**Size:** S · **Deps:** T1.1

**Problem.** The transport-jump branch (`PatternPlayer.cpp:949-965`) clears `bassNoteOffSample`,
`crashNoteOffSample`, `clickNoteOffSample` and `pendingLearned_` **without emitting the note-offs**.
With T1.1 in place the drum table must be handled too, or the fix makes stuck notes worse.

**Change.** Before clearing, emit each pending off at offset 0:

```cpp
if (delta < -slack || delta > slack) {
    if (bassNoteOffSample  >= 0) midi.addEvent(juce::MidiMessage::noteOff(kBassChannel, bassNoteOffMidi), 0);
    if (crashNoteOffSample >= 0) midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, kCrashNote), 0);
    for (int n = 0; n < kDrumVoices; ++n)
        if (drumNoteOffSample[n] >= 0)
            midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, n), 0);
    // ... then the existing clears
}
```

Extract this into `flushAllPendingNoteOffs(midi)` and call it from the seek branch, the silence branch
(`:970-983`, which currently relies on `allNotesOff`), and `processBlockBypassed`.

**Tests (review §5.8):** seek/loop while a crash and a bass note are ringing; assert a matching
note-off for each.

---

### T1.3 — Split-block time base (review §2.12)
**Size:** S · **Deps:** T0.4

**Problem.** `emitGroove(changeBeat, beatEnd, …)` (`PatternPlayer.cpp:1074`) passes `changeBeat` as
the callee's `beatStart`, but events are written at `sampleOffsetBase + off` with
`sampleOffsetBase` hard-wired to 0 (`:1048`). The post-change half is emitted early by
`(changeBeat − blockStartBeat)` and overlaps the pre-change half.

**Change.** Add an explicit block base and use it:

```cpp
auto emitGroove = [&](double from, double to, int patIdx) noexcept {
    if (patIdx == 0 || to <= from + 1.0e-12) return;
    ...
    const int base = (int) std::llround((from - beatStart) * samplesPerBeat);
    emitDrumEventsForRange(midi, numSamples, from, to, library->getPattern(patIdx), orn, base);
};
```

Apply the same to both `emitHarmonicBass` calls (`:1161-1167`), which have the identical bug.

**Tests (review §5.2):** force a pattern change to land mid-block (odd block size or non-zero start
position) and assert the absolute event samples are identical to a render where the change lands on a
block boundary.

---

### T1.4 — Learned-bass octave fold (review §2.15)
**Size:** S · **Deps:** none

**Problem.** `triggerLearnedBassNote` clamps to `[28, 55]` **after** the caller has applied
`bassTranspose` (`PatternPlayer.cpp:131`; callers `AccompanimentProcessor.cpp:1770,1526`), so with the
−12 octave setting pitch classes C/C♯/D/D♯ all collapse to E1.

**Change.**

```cpp
// PatternPlayer.cpp:131
int n = midiNote;
while (n < 28) n += 12;
while (n > 55) n -= 12;
note.midi = juce::jlimit(28, 55, n);
```

(Identical to the existing correct folds at `:738-739` and `:787-788`.)

**Tests:** for each pitch class 0–11 and each of `bassTranspose ∈ {−12, 0, +12}`, assert the resulting
note has the expected pitch class and lies in `[28, 55]`.

---

### T1.5 — Ghost threshold vs the library (review §2.10, second half)
**Size:** S · **Deps:** D5 approved

**Problem.** `GrooveTemplateData.h` sets `ghostThreshold = 42` while
`MidiPatternLibrary.cpp:755-757` documents `<= 62` and authors pattern 20's ghosts at 68–72. Those
ghosts therefore lose their quiet clamp and early timing.

**Change (per D5, re-author the library):**
* Raise the authored ghost velocities in `buildVerseGhost()` (and any other documented ghost) to
  ≤ 58 so they sit under the documented threshold.
* Set `ghostThreshold` back to 62 in `GrooveTemplateData.h` **and** in
  `training/build_groove_template.py` so regeneration does not undo it.
* Widen the ghost band to the documented 30–55 unless a listening test says otherwise
  (`ghostVelocityLo/Hi` are currently 15/42 — likely too quiet).

**Tests:** a unit test that walks `MidiPatternLibrary` and asserts every event carrying a `// ghost`
marker is `<= ghostThreshold`, so library and template can never disagree again. Plus: pattern 20
renders its off-16th snares in the ghost band and with `ghostTimingMs` applied.

---

### T1.6 — Crash placement and state clearing (review §2.13)
**Size:** M · **Deps:** T1.1

**Problem.** `armCrashPending` fires at `sampleOffset = 0` (block start, not a beat); it doubles a
pattern's own downbeat crash; the gate is 1 beat; and it is not cleared by the silent branch.

**Change:**
1. `emitCrashHit` takes an explicit grid offset: next beat boundary, or the section bar line when
   armed for a section change. Reuse the `changeBeat` arithmetic at `:1022`.
2. Before emitting, check whether the active pattern already has note 49 within ±20 ms of the target
   and skip if so.
3. Lengthen the crash gate to ≥ 2 beats (matching the authored 2.5).
4. Clear `armCrashPending`, `pendingBarFillIndex_`, `barFillStartBeat_` and `bassLeadInArmed` in the
   silent branch and in `reset()`.

**Tests:** arm a crash into a section whose pattern crashes on beat 1 → exactly one note-49 at the bar
line; arm while silent → it does not fire later.

---

## Phase 2 — Clock and schedule correctness

The highest-risk, highest-value phase. It is isolated so it can be reverted alone.

### T2.1 — Monotonic, grid-locked musical clock (review §2.2) — **blocker**
**Size:** L · **Deps:** D1 approved, T0.4

**Problem.** `grooveLockStartSample` / `grooveLockEndSample` / `transitionStartSample` /
`transitionEndSample` / `riffAPlayOriginSample` / `riffBPlayOriginSample` are declared as
**`hostSampleTime`-frame** (`AccompanimentProcessor.h:346-347,383-384`) but assigned from
`clockSample`, which is the **host transport position** (`previewResolvedHostSample`, `:620`). On a DAW
loop wrap the transport jumps backwards, so:

* `emitFrozenRiff` computes `beat0 < 0`, forces `k = 0`, and emits nothing (`:1748-1767`);
* the lock can never reach `grooveLockEndSample` if the loop is shorter than `lockBars`.

Simulation with the real arithmetic (4-bar loop, LOCK=16, engaged 2 bars in) loses half the bass notes
and never expires the lock.

**Change.** Introduce one monotonic, bar-phase-locked clock and route every schedule through it.

```cpp
// AccompanimentProcessor.h
int64_t  lockOriginMono   = -1;   // hostSampleTime when the current A/B cycle began
double   lockBarPhaseBeats = 0.0; // fmod(transport beats at engage, 4.0) — keeps bar alignment

// At lock / transition engage:
lockBarPhaseBeats = std::fmod(static_cast<double>(clockSample) / samplesPerBeat, 4.0);
if (lockBarPhaseBeats < 0.0) lockBarPhaseBeats += 4.0;
lockOriginMono    = hostSampleTime;

// Monotonic musical beat position (never wraps, still on the host bar grid):
const double monoBeats = static_cast<double>(hostSampleTime - lockOriginMono) / samplesPerBeat
                       + lockBarPhaseBeats;
```

Then:
* `grooveLockStartSample/EndSample`, `transitionStartSample/EndSample` are computed from
  `hostSampleTime` (they already claim this frame).
* `emitFrozenRiff(riffA, originMono, numSamples, bpm, sr, hostSampleTime, bassTranspose)` where
  `originMono = lockOriginMono - (int64_t)(lockBarPhaseBeats * samplesPerBeat)`; the function itself
  is unchanged because `beat0` can no longer go negative.
* The UI countdowns (`:1317-1322`, `:1602-1621`) keep working — they are simple `elapsed / samplesPerBar`.

**Why the bar-phase term matters:** it preserves the reason the original author used the transport
position (the bass re-entry lands on the audible drum downbeat) while removing the wrap dependence.
Because every loop length that matters is a whole number of bars, the beat phase is preserved across
wraps and the two clocks agree modulo the loop length.

**Tests (review §5.1):**
* Wrap the host playhead at a 4-bar loop point mid-lock; assert ≥1 ch.2 note-on per occupied 16th of
  the `riffA` snapshot, and that the transition engages within `lockBars` bars.
* Assert the first bass note after a wrap falls on a drum downbeat (compare against the drum note-on
  positions in the same render).

**Risk:** high. `emitFrozenRiff` is also fed `riffBPlayOriginSample` in `RiffBLocked` (`:1518`) — both
origins must move to the mono frame together. RiffA/B return-to-A equality (`test_processor_pipeline.cpp:2386`)
is the canary; run it first.

### T2.2 — Transport-jump detection and re-anchor
**Size:** M · **Deps:** T2.1

`PatternPlayer` already detects the jump (`:949-965`) but never tells the processor. Expose it:

```cpp
// PatternPlayer.h
bool consumeTransportJumped() noexcept { const bool j = transportJumped_; transportJumped_ = false; return j; }
```

Set `transportJumped_ = true` in the seek branch. In the processor, on a reported jump while in
`RiffA`/`RiffBLocked`/`TransitionHold`, re-latch `lockBarPhaseBeats` and `lockOriginMono` from the new
position (a seek is a musical restart) while preserving the *remaining* lock duration so a non-bar-aligned
seek cannot strand the state machine.

**Tests:** seek to a non-bar-aligned position mid-lock; assert the lock still expires and the drums
stay on the host grid.

### T2.3 — Retirement of the old clock fields
**Size:** S · **Deps:** T2.1, T2.2

Rename the six schedule members to make the frame explicit in the type system, e.g.
`grooveLockStartMono`, so the class of bug cannot recur silently. Update the comments at
`AccompanimentProcessor.cpp:612-619` to describe the mono clock.

---

## Phase 3 — Dynamics and humanisation

### T3.1 — Velocity headroom (review §2.3) — **blocker (musicality)**
**Size:** M · **Deps:** none

**Problem.** `velocityMul × sectionVel × velocityScale × guitarEnergy` peaks at ≈1.39, so authored
92–125 all render as 127.

**Change.** Add an explicit trim so the nominal product peaks near ~118, and keep the clamp only as a
safety net:

```cpp
// PatternPlayer.h
static constexpr float kVelocityTrim = 0.80f;   // leaves ~2 dB of headroom
```

```cpp
// PatternPlayer.cpp:~562
const float mul = grooveTemplate.velocityMul[grid16] * sectionVelMul * kVelocityTrim;
```

Consider a soft knee instead of a hard clamp for the residual peaks:

```cpp
if (vel > 118) vel = 118 + (int) std::lround((vel - 118) * 0.4f);   // gentle compression
vel = juce::jlimit(1, 127, vel);
```

**Tests (review §5.4):** render a Metal chorus bar at `guitarEnergy = 1.28`; assert
< 25 % of note-ons are at 127 and the velocity standard deviation is > 12. Also assert the verse/chorus
backbeat delta from `MUSICALITY_ROCK_PIVOT_PLAN.md` §7 (≥ 15) now holds.

**Risk:** every rendered velocity changes; re-baseline T0.2 after this task.

### T3.2 — `guitarEnergy` becomes bidirectional (review §2.3, §2.11)
**Size:** S · **Deps:** T3.1

**Problem.** `jlimit(0.85, 1.28, 1.0 + rms*1.5)` can never go below 1.0.

**Change.**

```cpp
// AccompanimentProcessor.cpp:874
const float guitarEnergyTarget = juce::jlimit(0.85f, 1.20f, 0.94f + smoothedRms * 0.55f);
```

Map over a longer RMS window than the raw per-block value so it is a *swell*, not a compressor, and
document the resulting dB range.

**Tests:** assert a quiet passage renders measurably softer than a loud one for the same pattern and
section; assert the range spans both sides of 1.0.

### T3.3 — Microtiming recentring (review §2.10, first half)
**Size:** M · **Deps:** none

**Problem.** `kRockTimingMs` has a negative mean on every downbeat cell (−2.7 to −6.8 ms) and pushes
off-16ths **+8.5 ms late** with a 6 ms gaussian (±15 ms at 2.5σ).

**Change:**
* Subtract each template's **mean** offset so the grid is centred, then apply the residual as jitter.
  Do this in the generator (`training/build_groove_template.py`) *and* add a runtime assertion that
  `|mean(timingMs)| < 1.5 ms`.
* Reduce `timingJitterMs` to ≈2.5–3 ms for rock and ≈2 ms for metal, or scale it by the new
  `humanize` parameter (T4.4).
* Add a helper script `tools/check_groove_template.py` that prints per-cell stats so this is reviewable.

**Tests:** A/B a straight pattern against a click-track render; assert the mean onset error vs the
grid is < 2 ms and the standard deviation is in a musical range (5–12 ms).

### T3.4 — Deterministic humanisation (review §3, P2.11)
**Size:** S · **Deps:** none

**Problem.** `prepare()` calls `rng.setSeedRandomly()` (`PatternPlayer.cpp:60`), so the same take
renders differently every bounce and the drum feel changes with block size and with how many bass
draws happened first.

**Change.** Replace the single `rng` with a deterministic per-event draw keyed by
`(barNumber, grid16, voice, salt)` using the existing `barHash` (`:41-46`), and keep `setRandomSeed`
for tests. This gives reproducible bounces *and* per-bar variation.

**Tests:** render the same 8 bars twice → identical event samples and velocities; render across two
block sizes → identical.

---

## Phase 4 — Selection, variety and ornamentation

### T4.1 — Wire the Play-mode pool rotation (review §2.4)
**Size:** L · **Deps:** D3 approved, T0.3

**Problem.** `diversifyPatternForGenre/Style` runs only when `playSectionIndex < 0`
(`AccompanimentProcessor.cpp:383-392`) — never in an audible phase — and the Play branch snaps to a
fixed pool member via `constrainToPool` (`:822-823`).

**Change.** Make the rotation the source of truth in Play:

```cpp
// AccompanimentProcessor.cpp — Play branch (replaces :822-823)
const auto orderedPool = PatternRules::orderedSectionPatternPoolForGenre(secName, genreId);
const int barsPerGroove = PatternRules::barsPerGrooveForSection(secName);
const int grooveSlot   = barsElapsedNow / juce::jmax(1, barsPerGroove);
const unsigned seed    = static_cast<unsigned>(sectionEntryBar);
int picked = PatternRules::pickPoolPattern(orderedPool, seed, grooveSlot, lastPlayedPoolPattern);
if (picked < 0)
    picked = PatternRules::constrainToPool(patternIdx, orderedPool, st);
lastPlayedPoolPattern = picked;
effectivePatternIdx = picked;
```

* `sectionEntryBar` and `lastPlayedPoolPattern` are new members (reset on section change).
* Keep `constrainToPool` only as the "is this pick legal" fallback.
* In parallel, feed the inference argmax in as a *vote*: if the argmax is a pool member, prefer it —
  this keeps the ML meaningful without letting it stall the rotation.
* Fix the `constrainToPool` fallback (`pattern_rules.h:712`) from `pool[0]` to a state-compatible
  global pattern so a loud verse does not collapse.
* Note the existing 2-bar drum hold (T6.1) already limits how often this rotates; the two must be
  reconciled so the rotation slot is not fought by the hold.

**Tests (review §5.5):** restore `test_e2e_groove_variety.cpp` to `>= 3`; add a Play-form test
asserting each section shows at least `min(2, pool.count)` distinct members across two passes, and
that consecutive phrases never repeat.

### T4.2 — Re-enable the mel variety draw (review §2.4)
**Size:** S · **Deps:** T4.1

Production calls `selectPatternFromMel(..., seed = -1)` (`AccompanimentProcessor.cpp:303,336`), which
forces deterministic argmax and disables the top-K weighted draw at `MetalGrooveInference.cpp:324-341`.

**Change.** Seed from the section instance rather than the bar (so a phrase is stable):

```cpp
const int melSeed = static_cast<int>(sectionEntryBar & 0x7fffffff);
idx = groove->selectPatternFromMel(latestMel.data.data(), excludeParam, melSeed);
```

Keep `-1` (deterministic) for the B-lock one-shot pick at `:303`, where reproducibility matters.

**Tests:** assert two different sections with identical features can yield different picks; assert a
single section instance is stable across the phrase.

### T4.3 — Ornaments behind one `humanize` control (review §2.11)
**Size:** M · **Deps:** D4 approved

**Problem.** Per-bar hashed mutations (`openHat` 18 %, `rideSwitch` 12–28 %, `dropKick` 12 %,
`extraGhost` 15 %, `microFill` 12 %) rewrite the groove's identity mid-phrase, and there is no way to
turn them off.

**Change:**
* Add an APVTS parameter `humanize` (0–1, default ≈0.35) and scale every probability in
  `computeOrnamentation` by it; at 0 the function returns an empty `BarOrnamentation`.
* Set `kRideSwitchPct*` to 0 by default — changing hat → ride + bell is a *pattern* change, not an
  ornament. Track the variant approach (T8.6) for reintroducing it properly.
* Bound `openHatCell`/`extraGhostCell` selection to cells the pattern actually uses *and* that are not
  the same cell as another ornament (the current `extraGhost` can collide with the injected ghost on
  cell 9 — see review §2.11 and the drum analysis).
* Persist the parameter with the session and expose it in the editor next to Swing.

**Tests:** `humanize = 0` produces byte-identical output to a build without ornaments; each ornament
fires at approximately its stated rate across 400 bars; no two ornaments target the same cell.

### T4.4 — Idle pattern readout (review §2.20)
**Size:** S · **Deps:** none

**Change.** In the audio thread's display update (`AccompanimentProcessor.cpp:1583`) publish `0` when
`!armActive`, or stop the inference thread from writing `displayPatternIndex` while idle (`:401`).
Prefer the latter — one writer.

**Tests:** idle with audio playing → `getDisplayPatternIndex() == 0`; armed → tracks the active index.

---

## Phase 5 — Bass musicality

### T5.1 — Reconnect the authored bass lines (review §2.5)
**Size:** M · **Deps:** T0.3

**Problem.** `emitBassRange` has no call site, so 23 patterns' authored bass lines never play.

**Change.** In `PatternPlayer::process` (`:1159-1167`) replace the direct `emitHarmonicBass` calls with
`emitBassRange`, which already falls back to the harmonic engine when `bassEvents` is empty:

```cpp
if (beatGridBassEnabled_) {
    if (changeBeat < 0.0) emitBassRange(midi, numSamples, beatStart, beatEnd, lib->getPattern(activePatternIndex), 0);
    else { emitBassRange(... beatStart, changeBeat ...); emitBassRange(... changeBeat, beatEnd ...); }
}
```

* `emitBassRange` already prefers `emitPatternBass` (transposed authored intervals relative to
  `kPatternBassRoot = 36`) and falls back to `emitHarmonicBass`.
* Pass the correct `sampleOffsetBase` per T1.3.
* Keep the live-mirror suppression semantics, but see T5.3.
* Undo the assertion in `tests/test_pattern_player.cpp:453-456` (T0.3).

**Tests:** render a Play verse with a root of E; assert the bass notes include the pattern's authored
intervals transposed (e.g. pattern 22's `kBassRoot + 5` becomes E+5), not just the root. Assert a
pattern with empty `bassEvents` still gets the harmonic fallback.

### T5.2 — Frozen-riff articulation: note lengths from onset detection (review §2.6)
**Size:** L · **Deps:** D6 approved, D1

**Problem.** The capture grid stores occupancy only, so a held chord becomes eight 16th retriggers; the
snapshot has no notion of a note's duration.

**Change.**

1. Extend the snapshot:
   ```cpp
   // PhraseLearner.h — LearnedRiff and GridSlot
   struct GridSlot { bool occupied = false; int midiNote = 36; uint8_t gate16 = 1; bool onset = true; };
   struct LearnedRiff { bool valid; double lenBeats; std::array<bool,64> occupied; std::array<int,64> midi;
                        std::array<uint8_t,64> gate16; };   // 16th counts
   ```
   Keep `occupied`/`midi` so the existing UI getters (`AccompanimentProcessor.h:130-152`) and tests
   keep compiling.
2. Onset detection at capture: track `prevSlotPeak_` in `stampWindow`
   (`AccompanimentProcessor.cpp:1693-1714`). A slot is an **onset** when
   `slotPeak > prevSlotPeak_ * 1.15f + 0.01f` or it is the first occupied slot after a rest;
   otherwise it is a sustain continuation. This distinguishes a held/decaying chord from a re-picked
   same-pitch chug. (Threshold tuning is the acceptance risk — see D6.)
3. On `exportPattern`, coalesce: gate = number of consecutive 16ths from an onset up to the next onset.
4. In `emitFrozenRiff` (`:1757-1772`), trigger only on onset slots, with

   ```cpp
   const int duration = juce::jmax(1, (int) std::lround(gate16[s] * 0.25 * spb * 0.9));  // 90% gate
   ```

**Tests (review §5.7):** record a sustained 2-bar chord → playback emits O(1) notes per chord; record a
16th-note chug → playback emits one note per 16th. Round-trip `exportPattern`/`loadPattern` preserves
gates. The A2 == A1 equality test at `test_processor_pipeline.cpp:2386` must still pass with gates
compared too.

### T5.3 — Mirror vs grid retrigger (review §2.16)
**Size:** S · **Deps:** T5.1

**Problem.** `PatternPlayer.cpp:754-755,808-810` skips a grid hit that falls inside a ringing mirror
note, so a pickup on the "and of 4" steals the next downbeat's root.

**Change.** Replace `continue` with a retrigger: close the ringing note with its own note-off at the
grid hit's offset, then emit the grid note. Add a `forceRetrigger` flag to `emitBassNote`.

Alternative (if the retrigger clicks): bound the mirror duration to the distance to the next grid hit at
trigger time (`AccompanimentProcessor.cpp:1505`).

**Tests:** pick on the "and of 4" and assert the beat-1 grid root is still present in the render.

---

## Phase 6 — Reactivity

### T6.1 — Bounded fast path and hold reduction (review §2.7)
**Size:** M · **Deps:** T4.1

**Problem.** Up to ~6.5 s between changing what you play and hearing the kit change.

**Change:**

```cpp
// AccompanimentProcessor.cpp — around :367-400
const int64_t holdSamples = static_cast<int64_t>(4.0 * 60.0 / bpmNowDrum * sr);   // 1 bar (was 8 beats=2 bars)
const bool holdExpired = (lastDrumPatternChangeSample < 0)
    || (latest.sampleTimestamp - lastDrumPatternChangeSample >= holdSamples);
const bool gestureChange = std::abs(latest.rmsDelta) > 0.6f;   // already computed at :566
if (holdExpired || excludeParam >= 0 || gestureChange) { ...commit... }
```

* A `gestureChange` commit must still be bar-quantised by the player; additionally allow beat-quantised
  application for this case only (a new `GrooveCommit::alignToBeat` flag honoured at
  `PatternPlayer.cpp:1022` by using a 1.0-beat boundary).
* Reset the hold timer on *every* accepted commit so a flurry of gestures does not thrash.
* Re-tune the mel window: 512 ms is the dominant fixed cost. Consider a 256 ms window with a 128 ms hop
  for the pattern path (keep 512 ms for style), or at minimum document why not.

**Tests (review §5.6):** a large RMS step produces a committed pattern change within 250 ms of the step.

### T6.2 — Same-riff cut-short (review §2.8)
**Size:** M · **Deps:** D2 approved

**Change.** In the transition hold (`AccompanimentProcessor.cpp:1421-1429`), re-enable an edge-triggered
exit **for the same riff**:

```cpp
const bool sameRiffReturned = phraseLearner.justMatchedRiff()
                           && lastRiffMatchSample > transitionStartSample;
if (clockSample >= transitionEndSample || sameRiffReturned) { ...exit to A... }
```

Keep "a *different* riff does not cut the transition" (that rule is legitimate). Update the pinned test
at `tests/test_processor_pipeline.cpp:1138` and the doc.

**Tests:** replay the locked riff mid-contrast → re-locks at the next bar; play a different riff → the
contrast still plays its full `transitionBars`.

### T6.3 — Unfreeze B-listen and make the lock reachable (review §2.8)
**Size:** M · **Deps:** T4.1

**Change (three parts):**
1. In `RiffBListen`, follow inference constrained to `transitionPool` (via `pickPoolPattern`) instead of
   pinning `drumB0` (`:838-846`).
2. Lower the B lock threshold: allow `RiffBListen` to lock on `attackCount_ >= 4` (or the first
   2-bar repeat) rather than `>= 8` (`PhraseLearner.cpp:555-567`). Note `patternsMatch` already
   requires `attackCount_ >= len * 2`.
3. Allow drift-unlock while held after N bars of non-matching attacks, so a stuck lock can escape
   without Forget (today `PhraseLearner.cpp:612-613` blocks it via `!holdActive_`).

**Tests:** feed a non-repeating solo through B → drums still vary and the phase exits on the clock;
feed a repeating 2-bar figure → locks within 2 phrases.

### T6.4 — Scope the guitar-stop gate (review §2.9)
**Size:** S · **Deps:** none

**Change.** Do not route `guitarStopped` into `RiffA`/`RiffBLocked`
(`AccompanimentProcessor.cpp:1501-1502`). The lock exists to accompany independently of the player.
Keep the gate for `Play` (where the song form should continue anyway — so in practice this becomes
"remove the gate from the lock phases") and, if a stop is genuinely desirable, require a transport-level
stop or a much longer window (≥ 4 bars) and emit pending note-offs via T1.2 before cutting.

**Tests:** stop playing for 6 s mid-lock → drums and frozen bass continue; resume → no glitch.

### T6.5 — Attack detector relaxation (review §2.17)
**Size:** S · **Deps:** none

**Change.** In `PhraseLearner::detectAttack` (`:70-71`), use the fall counter only to reject a
sustained constant level:

```cpp
const bool sustained = (fallCounter_ == 0) && (std::abs(rms - rmsSmooth_) < 0.02f * rmsSmooth_);
if (sustained) return false;
return sharpRise && rms > 0.01f;
```

**Tests:** a synthetic 16th-note pulse train at 200 BPM produces ~4 attacks/beat; a constant tone
produces none.

---

## Phase 7 — Fills

### T7.1 — Fill seed in Record (review §2.18)
**Size:** S · **Deps:** none

`updateOutgoingFill` is called with `seed = 0u` for `riffAFillArm` and `riffBFillArm`
(`AccompanimentProcessor.cpp:1276,1455`), so `selectFillPattern(0, rms, 0)` can never return 19.

**Change.** Pass a varying seed (e.g. the lock bar count or `transitionSectionNumberLocal`) for the
Record paths, and add a dedicated `selectFillPatternForEnergy(rms, seed)` that guarantees 19 for
`rms >= 0.45` on alternate phrases rather than relying on seed parity.

**Tests:** at `rms = 0.5` over 8 section ends, all three of 17/18/19 appear.

### T7.2 — Fill arming from the real section boundary (review §2.18)
**Size:** M · **Deps:** T2.1

**Problem.** `fromNext = (beatInBar >= 0.05)` (`:1659`) is a ~25 ms magic guard, so any longer buffer
places 17/18 in the *next* section's first bar.

**Change.** Derive the section's last-bar start as an explicit host sample from the sequencer
(`StructureSequencer::getBarsElapsed/…` plus the block's start position) and arm the fill against that
sample, with a containment test rather than a phase threshold:

```cpp
const bool fillLandsThisBar = (sectionEndSample >= blockStart && sectionEndSample < blockEnd);
```

**Tests (review §5.9):** at 1024 and 2048-sample buffers, 120 and 240 BPM, log `fillOrigin` and assert
the fill lands inside the section's last bar in every case.

### T7.3 — Fills replace the groove, and inherit feel (review §2.18)
**Size:** M · **Deps:** T1.3

**Change:**
1. Suppress the groove from the fill's window start for 17 and 18, exactly as 19 already does
   (`PatternPlayer.cpp:1034,1040-1047`) — not just for 19.
2. Route fill events through the same microtiming and velocity path as groove events (share the
   `timeMs`/velocity computation rather than the plain `round` at `:434-435`), including swing and
   `sectionVelMul`/`guitarEnergy`.
3. Suppress the fill pattern's terminal crash when the incoming bar already crashes (reuse the T1.6
   duplicate check).

**Tests:** pattern 21 + fill 18 no longer produces two kicks within 5 ms at beat 3.75; a swung groove
produces a swung fill; fill note velocities follow the section multiplier.

---

## Phase 8 — Cleanup, data and docs

### T8.1 — Delete dead code (review §2.23)
**Size:** M · **Deps:** each item's owning phase

Remove, or wire and document, each entry in the review's §2.23 table:
`snapBassToSectionHarmony`, `lastRiffMatchSample` (or implement "returning to the riff extends the hold"
if D2 says so), `GrooveCommit::fillKind`/`TransitionFillKind`, `setMirrorWhileHeld`/`releaseForTransition`,
`barsPerGrooveForSection` if T4.1 does not use it, and `rmsDelta` if T6.1 does not.
`ARCHITECTURE.md`'s `OnnxInference` section must be rewritten around `MetalGrooveInference`.

**Acceptance:** `grep` for each symbol finds only its definition, or a caller.

### T8.2 — Fix the races (review §2.23)
**Size:** M · **Deps:** none

* `static bool lastLoopValue` (`AccompanimentProcessor.cpp:636`) → a member; it is currently shared
  across plugin instances and mutated on the audio thread.
* The riff snapshot getters (`AccompanimentProcessor.h:130-179`) read `riffA`/`riffB`/`enginePhase`
  while the audio thread writes them → torn 64-slot reads. Publish an immutable snapshot behind an
  atomic pointer with a version counter, or copy under a `SpinLock` that the audio thread never takes
  (double-buffer + atomic index).
* The `pendingLearned_` overrun at `PatternPlayer.cpp:143` should drop the newest note and assert,
  not silently overwrite an existing one.

**Tests:** a TSan build (add a CI target) runs the pipeline test without data-race reports.

### T8.3 — Swing parameter notification (review §2.19)
**Size:** S · **Deps:** none

```cpp
// AccompanimentEditor.cpp:339-341
if (auto* p = audioProcessorRef.getApvts().getParameter("swing"))
    p->setValueNotifyingHost(p->convertTo0to1(Groove::presetFor(g).defaultSwing));
```

**Tests:** change genre → the slider's value changes and the host receives a parameter change.

### T8.4 — Groove template regeneration (review §2.21) — **optional, data-dependent**
**Size:** L · **Deps:** data availability

`kRockVelocityMul == kPunkVelocityMul` byte-for-byte and `kPunkTimingMs == kRockTimingMs / 4`; all three
blocks report the same 204 files / 115 534 hits. Either filter the corpus per genre in
`training/build_groove_template.py` and regenerate, or delete the punk/metal blocks and document that
one template covers the rock family with per-preset jitter/swing.

**Not on the critical path** — T1.5 and T3.3 deliver the audible improvement; this is a data-honesty fix.

### T8.5 — Documentation reconciliation
**Size:** M · **Deps:** Phases 1–7

* `docs/END_USER_STRESS_TEST.md` — update every contract the plan changes: cut-short (T6.2), fills
  (T7.x), idle pattern readout (T4.4), and remove the duplicate paragraph at lines 485-487.
* `CHANGELOG.md` — remove the 0.9.59 `setMirrorWhileHeld` claim (the API is a stub) and correct the
  "0.9 beat legato" claim to state which paths have it.
* `ARCHITECTURE.md` — replace the `OnnxInference` section; document the two-clock model (transport
  clock for the drum grid, monotonic clock for lock schedules) so T2.1's frame bug cannot recur.
* `PatternPlayer.h:228-229` — correct the "authored bass lines play when present" claim to match T5.1.

### T8.6 — Ornaments as pattern variants (follow-up)
**Size:** XL · **Deps:** T4.3 shipped and heard

Replace render-time hash mutations with explicit library variants selected by the rotation, so an
ornamented bar is a *committed* pattern change. Design first; do not start until T4.3 has been played.

---

## Phase 9 — Verification and acceptance

### T9.1 — Full test suite green
Run both binaries; every test from the review's §5 exists and passes.

```bash
./build/MetalAccompanimentTests
./build/MetalAccompanimentIntegrationTests
```

### T9.2 — Buffer-size invariance
Render the T0.2 riff at 128 / 512 / 2048 and diff the absolute event samples. **Must be identical.**
This is the single strongest regression guard for T1.1, T1.3 and T3.4.

### T9.3 — Listening matrix
For each row, A/B against the T0.2 baseline and record a verdict:

| Check | Target |
| --- | --- |
| Cymbals ring | Crash/china decay ≥ 1 beat at all buffer sizes |
| Dynamics | Verse vs chorus backbeat delta ≥ 15; no wall of 127 |
| Grid | Straight pattern vs click: mean error < 2 ms, no systematic rush/lag |
| Play variety | ≥ 3 distinct grooves across the default form; no section stuck on one pattern |
| Lock under a DAW loop | Bass plays every occupied 16th; transition fires within `lockBars` |
| Lock under a breath | 6 s without picking → drums and bass continue |
| Bass content | Play verse shows the pattern's authored intervals, not just the root |
| Frozen riff | A sustained chord plays one note, not eight 16th blips |
| Reactivity | A hard dynamic change moves the groove within 250 ms |
| Cut-short | Replaying the riff mid-contrast re-locks at the next bar |

### T9.4 — Re-run the end-user stress test
Re-run `docs/END_USER_STRESS_TEST.md` Station A–H on the **newly built** plugin (verify the version
string first — T0.1). Fill the miss/bug log honestly and diff against the original results.

### T9.5 — Performance budget
The integration suite reports mean 0.52 ms / p99 0.83 ms per 256-sample block. Re-check after
Phases 2 and 6 and fail the release if p99 exceeds 1.5 ms or the ONNX latency benchmark regresses
past its 5 ms budget.

---

## Risk register

| Risk | Where | Mitigation |
| --- | --- | --- |
| The clock refactor (T2.1) breaks A/B/A return-to-A equality | Phase 2 | Run `test_processor_pipeline.cpp:2386` first; revert T2.1 alone if red |
| Deferred drum note-offs (T1.1) introduce stuck cymbals | Phase 1 | Land T1.2 in the same phase; add the seek test before the feature |
| Velocity retune (T3.1) invalidates every prior listening judgement | Phase 3 | Re-baseline T0.2 immediately after T3.1 |
| Play rotation (T4.1) fights the drum hold (T6.1) | Phases 4 & 6 | Implement T6.1's hold change before T4.1, or land them in one release |
| Onset detection (T5.2) misclassifies fast chugs as one long note | Phase 5 | The threshold is D6; gate on the chug-vs-chord test pair |
| Relaxing the cut-short rule (T6.2) reintroduces "the transition never plays" | Phase 6 | Keep the guaranteed exit on `transitionEndSample`; only add the same-riff edge |
| Removing the guitar-stop gate (T6.4) leaves drums running when the guitarist walks away | Phase 6 | Acceptable inside a lock; `Forget` and transport stop still cut |

## Rollback strategy

Every phase is independently revertable: each task is its own commit and no task changes a persisted
format except T5.2, which **adds** fields to `LearnedRiff` without removing any. Phase 2 is the only
one where a partial revert is unsafe — revert T2.1 and T2.2 together.

## Suggested release slicing

| Release | Contents | Playable? |
| --- | --- | --- |
| v0.9.68 | Phase 0 + 1 | Yes — cymbals ring, MIDI is buffer-independent |
| v0.9.69 | Phase 2 | Yes — lock survives DAW loops (the big user-visible fix) |
| v0.9.70 | Phase 3 | Yes — the kit has dynamics again |
| v0.9.71 | Phases 4 + 6 | Yes — Play varies, the kit reacts |
| v0.9.72 | Phase 5 + 7 | Yes — bass is a musical part, fills land |
| v1.0.0-rc | Phase 8 + 9 | Release candidate after a full stress-test pass |

---

## Traceability matrix

| Review issue | Task | Test |
| --- | --- | --- |
| §2.1 drum note-offs | T1.1 | §5.3 ledger + §5.2 golden |
| §2.2 DAW loop wedges lock | T2.1, T2.2, T2.3 | §5.1 transport-loop |
| §2.3 velocity saturation | T3.1, T3.2 | §5.4 histogram |
| §2.4 no Play rotation | T4.1, T4.2 | §5.5 coverage |
| §2.5 authored bass dead | T5.1 | T5.1 authored-interval test |
| §2.6 16th retrigger | T5.2 | §5.7 articulation |
| §2.7 latency | T6.1 | §5.6 latency budget |
| §2.8 uninterruptible | T6.2, T6.3 | cut-short + B-listen tests |
| §2.9 guitar-stop gate | T6.4 | 6 s breath test |
| §2.10 humanisation/ghosts | T1.5, T3.3 | grid-error + ghost-marker tests |
| §2.11 ornaments/energy | T3.2, T4.3 | ornament-rate test |
| §2.12 split-block base | T1.3 | §5.2 golden |
| §2.13 crash | T1.6 | duplicate-crash test |
| §2.14 seek note-offs | T1.2 | §5.8 seek test |
| §2.15 octave fold | T1.4 | pitch-class matrix |
| §2.16 downbeat suppression | T5.3 | "and of 4" test |
| §2.17 attack detector | T6.5 | 200 BPM pulse train |
| §2.18 fills | T7.1, T7.2, T7.3 | §5.9 fill log |
| §2.19 swing notify | T8.3 | parameter-change test |
| §2.20 idle readout | T4.4 | idle display test |
| §2.21 template data | T8.4 (optional) | template stats script |
| §2.22 tests enshrine regressions | T0.3 | — |
| §2.23 dead code / races / docs | T8.1, T8.2, T8.5 | TSan target |
| §0.1 stale binary | T0.1 | version string check |
