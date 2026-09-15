# Step 2 — Play-mode per-section riff learning and recall

**Status:** not started. This document is the complete brief for the agent that
implements it.
**Written at:** v1.0.13 (`6463cdc`), after a long debugging session on the bass
mirror. Read `docs/CONTEXT_HANDOFF.md` and `docs/PITFALLS_AND_INVARIANTS.md`
first — this document assumes them and only adds what changed.

---

## 1. The feature (what the user asked for, verbatim)

> "It should be mirroring me asap for each section and on a return to verse or
> chorus etc should play back a locked section from learning from my playing."

Concretely, in **Play mode**:

1. **First time through a section** (e.g. the first `VERSE`): mirror the
   guitarist live, immediately, and **in the background learn/capture the riff
   they are playing for that section**.
2. **When that section comes round again** (the second `VERSE`): play back the
   **locked** riff learned the first time, instead of re-learning from scratch.
3. Different sections keep different riffs: a `CHORUS` riff is not a `VERSE`
   riff. The song form (e.g. `"VERSE:8,CHORUS:8,VERSE:8"`) is the key.

This is the last real gap between the plugin and the project's core value
statement ("play into the plugin and hear a musically reactive groove in time").

---

## 2. Why it does not happen today

- **Play mode deliberately never captures or locks anything.**
  `AccompanimentProcessor` sets `phraseLearner.setAutoLockEnabled(false)` on
  Play and only mirrors live. (`autoLockEnabled` on Play is set around
  `AccompanimentProcessor.cpp:946`, `:991`, `:1343`, `:1387`, `:1418`.)
- **Only Record-riff mode learns**, and it learns a *global* `riffA`/`riffB`,
  with no notion of *which section* a riff belongs to. `emitFrozenRiff()` and the
  `EnginePhase::RiffA` / `RiffBLocked` states replay those global snapshots.
- `StructureSequencer` **does** track section identity
  (`getCurrentSectionName()`, `getCurrentSectionIndex()`), and the processor
  already detects section entry (see §5.1), but nothing binds a learned riff to
  a section name.

So the plumbing to know *when* a section starts exists; the plumbing to
*remember a riff per section* does not.

---

## 3. What the previous session did (context you must not undo)

Nine commits, `a32543b..6463cdc`. All unit (287) and integration (85) tests pass
at v1.0.13. Each fix has a guard test. Read the tests before touching the areas.

| Commit | What it fixed | Guard test |
|---|---|---|
| `a32543b` | Extracted **`BassVoice`** (`src/midi/BassVoice.{h,cpp}`): the one monophonic bass voice, producer arbitration + per-note provenance. | `tests/test_bass_voice.cpp` |
| `da59833` | **Mirror is host-buffer-size invariant.** The attack detector ran once per host block, so at a large buffer it saw ~1 value/second. Now `EnergyAnalyser` records the onset envelope at a fixed ~10.7 ms hop and the processor drives `PhraseLearner` per hop. | `bass mirror: the emitted mirror is host-buffer-size invariant` (sweeps 64→4096) |
| `10cc4e3` | **Learner starvation latch.** The learner was fed only when the slow structure tagger was not SILENT; a quiet passage made it SILENT, which starved the learner, which kept it SILENT — and the harmony line leaked under the player. Now fed whenever armed. | `bass mirror: the transition mirrors a different live riff` |
| `e034ee7` | Added the **offline DI audit** (`MA_DI_WAV`, see §7) — the instrument that compares emitted MIDI to what was played. | — |
| `2586d05` | **Recovered the dropped picks.** `clearsFloor` in `AttackDetector` rejected ~68 % of a real DI's rise edges. Relaxed to relative-only + added a broadband-transient gate (flux > absolute floor). | `test_attack_detector`, `test_bass_mirror_play_realaudio` |
| `6177d27` | **Transition mirrors live.** The contrast-section B-lock was freezing the bass onto a sparse snapshot. Now the drums commit to the contrast section and the bass mirrors (`PhraseLearner::setLiveMirrorWhenLocked`). | `bass mirror: the transition mirrors a different live riff` |
| `991ce5a` | **Removed the T6.2 same-riff transition cut-short** (user decision). The transition always runs its selected bars. | — |
| `6933466` | **Stopped sustain over-firing.** `kRiseVsPrev` 1.08 → 1.20 (a real pick rises >20 % in one hop; a sustain ripple does not). | `test_attack_detector` freeze test |
| `6463cdc` | **Sustain release on decay** (`BassVoice::setInputLevel`, release at 25 % of attack level) + **widened Rock section pools** 4 → 5-7 patterns. | buffer sweep + `distinct drum patterns` (audit) |

### Measured outcomes (for regression baselines)

- Mirror note rate now **matches the player's pick rate** on real DIs
  (e.g. 412 emitted vs 439 picked, 5.74/s vs 6.11/s).
- Emitted mirror is **flat across host block sizes** (210 notes at 64 → 4096).
- Transition bass went 0.5 → 18.7 notes/s.
- Drum variety in a verse-only form: **5 → 7 distinct patterns** (was capped by a
  pool of 4).

---

## 4. Known remaining problems (do not assume these are solved)

1. **This feature (per-section learning) — the big one.**
2. **Mirror tuning is residual, not perfect.** The detector's thresholds
   (`AttackDetector.h`) are hand-tuned against a handful of DIs. Expect ±10-20 %
   on a player whose touch differs. It is now a *single dial* (`kRiseVsPrev`,
   `kFloorRise`, `kFluxAbs`), not a structural problem.
3. **Drum musicality** is improved but still shallow: the rotation cycles a
   fixed pool per section with no phrase-level development. Fills exist
   (`armBarFill`, patterns 17/18/19) but are not part of the Play rotation.
4. The bass is **pitch-class only** (`36 + pitchClass`), folded to C2-B2. It does
   not track octave or a moving bassline contour.

---

## 5. Design for Step 2

### 5.1 Where to hook in

The processor already detects section entry in the PlaySection branch, around
`AccompanimentProcessor.cpp:1030-1042`:

```cpp
enginePhase = EnginePhase::PlaySection;
const auto* secName = structureSequencer.getCurrentSectionName();
const int secIndex = structureSequencer.getCurrentSectionIndex();
...
if (secIndex != lastSectionIndex || !wasPlayOn || barsElapsedNow < lastSeenBarsElapsed)
{
    lastSectionIndex = secIndex;
    sectionEntryBar.store(structureSequencer.getGlobalBarCount(), ...);
    ...
}
```

`secName` (a `const char*`: `"VERSE"`, `"CHORUS"`, `"BREAKDOWN"`, `"INTRO"`,
`"SOLO"`, `"OUTRO"`) is the natural cache key. **Key the cache by the name
string, not by `secIndex`** — the same section name recurs at different indices
in the form, and those are exactly the recurrences you want to hit.

### 5.2 Data structure

Add to `AccompanimentProcessor` (private):

```cpp
// Per-section riff memory (Play mode). Keyed by section NAME, so a return to
// the same section name replays what was learned the first time through.
static constexpr int kSectionRiffSlots = 8;
struct SectionRiffMemory
{
    bool valid = false;
    char name[16] {};                        // "VERSE", "CHORUS", ...
    PhraseLearner::LearnedRiff riff {};
    int64_t learnedAtMono = -1;              // diagnostics
};
std::array<SectionRiffMemory, kSectionRiffSlots> sectionRiffs {};
int sectionRiffWrite = 0;                    // ring for overflow

// Play-mode section take state (audio thread)
bool playTakeActive = false;                 // currently capturing this section
bool playTakeReplaying = false;              // currently replaying a stored riff
char playTakeSectionName[16] {};
int64_t playTakeOriginMono = -1;             // monotonic origin of the replay loop
```

Reuse `PhraseLearner::LearnedRiff` (`kGridBars=4`, 64 × 16th slots, `gate16`).

### 5.3 Reused APIs (do not reinvent)

- `PhraseLearner::beginLiveGridListen()` / `isGridListening()` — passive capture
  that does **not** disable the live mirror (`beginGridCapture()` does disable
  it; do not use that here).
- `PhraseLearner::stampGridRange(beat0, beat1, peak, bassMidi, onset)` — stamps
  16th slots. The processor's wrapper is
  `AccompanimentProcessor::stampLearnerGridSlots(in, numSamples, beatStart,
  beatEnd, samplesPerBeat, originBeat, bassMidi, wrapLoop)` — currently called
  only for `RiffBListen`; see `AccompanimentProcessor.cpp` (search
  `stampLearnerGridSlots`).
- `PhraseLearner::exportPattern(LearnedRiff&)` — snapshot the current capture.
- `PhraseLearner::loadPattern(const LearnedRiff&)` — load a snapshot and rewind
  to phase 0. **Check whether it sets `locked_`; you may need a
  `replayPattern()`-style entry point that loads AND locks.**
- `AccompanimentProcessor::emitFrozenRiff(riff, originSample, numSamples, bpm,
  sr, clockSample, bassTranspose)` — emits a snapshot's bass at absolute sample
  positions (buffer-invariant; see `emitFrozenRiff` body for why it uses integer
  arithmetic).
- `PatternPlayer::setBeatGridBassEnabled(bool)`, `setGuitarLevel(float)`,
  `setGuitarAudible(bool)`, `triggerLearnedBassNote(...)`.
- `BassVoice` provenance: `getLastBassProducer()`, `getRecentBassNoteOns()` —
  use these in tests to assert the bass came from `Producer::Frozen` on replay.

### 5.4 Algorithm (Play mode, per block)

```
on section entry (secName changed, or Play just started, or loop wrap):
    playTakeSectionName = secName
    if memory exists for secName (valid):
        phraseLearner.loadPattern(memory.riff)      // + lock, see 5.3
        phraseLearner.beginLiveGridListen()          // keep listening for edits
        playTakeReplaying = true
        playTakeActive = false
        playTakeOriginMono = <bar-aligned origin in hostSampleTime frame>
    else:
        phraseLearner.beginLiveGridListen()
        playTakeActive = true
        playTakeReplaying = false

each block while PlaySection:
    if playTakeActive and phraseLearner.isGridListening():
        stampLearnerGridSlots(...)                   // capture the section's riff
    if playTakeReplaying:
        emitFrozenRiff(memory.riff, playTakeOriginMono, numSamples, bpm, sr,
                       clockSample, bassTranspose)
    // (the live mirror path already runs when not replaying / when
    //  setLiveMirrorWhenLocked is set — decide which wins, see 5.5)

on section exit (secName about to change, or Play ends):
    if playTakeActive and phraseLearner.getGridOccupiedCount() >= 2:
        snapshot = phraseLearner.exportPattern()
        store(sectionRiffs, playTakeSectionName, snapshot)
    reset playTake* flags
```

Notes:

- **"Mirror me asap"** = the first pass must be the live mirror; capture runs in
  parallel and must not suppress it. That is why `beginLiveGridListen` (passive),
  not `beginGridCapture` (active).
- The replay origin must be **bar-aligned** so the loop sits on the grid. The
  monotonic frame is `hostSampleTime`; see `frozenRiffOriginMono()` and
  `latchLockClock()` for how the existing frozen-riff replay computes this.
  **Do not merge the transport clock and the monotonic clock** (invariant 9).
- Section exit is detectable inside the existing PlaySection advance path: the
  section changes when `structureSequencer.isComplete()` / a new `secName`
  appears. Watch loop wraps too (`lastSeenBarsElapsed` decreasing signals a
  wrap — already used for re-seed).

### 5.5 The design decision you must make explicitly

When a section **replays** its stored riff, does the bass also mirror live
attacks (the `setLiveMirrorWhenLocked` behaviour added for the transition), or is
the replay the authoritative bass?

Recommendation: **replay is authoritative while it is playing; a detected attack
does not interrupt it mid-phrase but a clearly different riff should unlock and
return to live mirroring** (re-use `PhraseLearner`'s drift-unlock:
`kDriftUnlockBars`, `justMatchedRiff`). Whatever you choose, **write the test
that pins it** — this is the same "who owns the voice" question that caused nine
mirror regressions.

---

## 6. Tests to add (required)

Put them in `tests/test_bass_mirror_play_realaudio.cpp` (real audio) and
`tests/test_processor_pipeline.cpp` (synthetic/deterministic).

1. **Learn then recall**: Play a form `VERSE:8,CHORUS:8,VERSE:8` over a real DI.
   Assert:
   - first VERSE: bass notes come from `Producer::Mirror`, note count ≈ attack
     count (live mirror);
   - CHORUS: different material, also mirrored;
   - second VERSE: bass notes come from `Producer::Frozen`, and the note set/pitch
     histogram matches the first VERSE's captured riff (not the CHORUS one).
2. **Sections do not cross-contaminate**: two different riffs play two different
   stored snapshots; assert the CHORUS replay does not contain the VERSE riff's
   grid slots.
3. **First pass is not silent**: during the capture pass the bass is the live
   mirror (this is the "asap" requirement) — guard against regressing to
   "learn first, play later".
4. **Buffer invariance**: the replay must be identical at 128 / 512 / 2048
   host block sizes (same absolute event samples). Pattern: see
   `tests/test_midi_probe.cpp` / `test_phase1_rendering.cpp`.
5. **No riff learned**: a section with fewer than 2 occupied slots must not
   create a memory entry, and the next visit must fall back to live mirroring.
6. **Loop wrap**: a looping song form re-enters VERSE and must replay the stored
   VERSE riff after the wrap.

Assert on **emitted MIDI** and on **`BassVoice` provenance**, not on
`PhraseLearner` internal counters — that was the mistake behind the nine prior
regressions (see `docs/BASS_MIRRORING.md` §4.4, §7).

---

## 7. How to build, test, and measure

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/MetalAccompanimentTests            # unit; must stay green (287 cases)
./build/MetalAccompanimentIntegrationTests # integration; must stay green
./scripts/install-plugin-to-user.sh --build build --config Release
```

**Offline DI audit — the key instrument.** Point it at a WAV of the guitarist's
dry DI and it runs the real processor and dumps every emitted bass note, the
detector breakdown, and the drum patterns used:

```bash
MA_DI_WAV="$HOME/Desktop/some_di.wav" \
  ./build/MetalAccompanimentIntegrationTests "bass mirror: offline audit of a supplied DI"
```

Use it to prove the replay matches what was played. The user's own clean DI
recordings are the ground truth; do not tune against synthetic sines (invariant/
pitfall: `docs/PITFALLS_AND_INVARIANTS.md` §2.5).

**Buffer sweep** (must stay flat):

```bash
./build/MetalAccompanimentIntegrationTests "bass mirror: the emitted mirror is host-buffer-size invariant"
```

---

## 8. Invariants and pitfalls you must respect

From `docs/PITFALLS_AND_INVARIANTS.md` — the ones that bite this feature:

- **Audio thread never blocks, never allocates.** The cache is a fixed
  `std::array`; no `std::map`, no `std::string` on the audio thread.
- **Any window describing musical time is derived from `sampleRate` in
  `prepare()`.** Do not count bars in blocks.
- **Do not merge the two clocks.** Transport (`clockSample`) places drums and the
  replay grid on the host timeline; the monotonic `hostSampleTime` times the
  lock/replay durations. `emitFrozenRiff` already depends on this.
- **Rendered MIDI must be buffer-size invariant.** Replay positions are absolute
  sample arithmetic (see `emitFrozenRiff`), never block-relative floats.
- **The mirror owns the monophonic bass voice while it rings**; the grid is a
  gap-filler (`BassVoice`). Do not add a second bass producer that bypasses
  `BassVoice`.
- **Every fix lands with its test; never weaken an assertion or add
  `[!mayfail]`.**
- **Bump `CMakeLists.txt:4` before every build** — it is how you confirm in the
  DAW that the new binary loaded (the UI shows `vX.Y.Z`).
- `.planning/` is the planning authority; **never read `.gsd/`** (stale, wrong).

---

## 9. Acceptance criteria

- First pass through a section mirrors live (provenance `Mirror`), captures in
  parallel, and does not go silent.
- Returning to the same section name replays the stored riff (provenance
  `Frozen`), and the replayed note set matches the first pass.
- Two sections with different riffs store and replay different snapshots.
- Replay is byte-identical at 128 / 512 / 2048.
- Loop wrap re-enters and replays correctly.
- All existing unit + integration tests still pass; no assertion weakened.
- New tests cover the six cases in §6.

---

## 10. Suggested order of work

1. Read `docs/BASS_MIRRORING.md` (whole), `docs/PITFALLS_AND_INVARIANTS.md`,
   `ARCHITECTURE.md`, and this session's commits (`git log a32543b..HEAD`).
2. Confirm `PhraseLearner::loadPattern` locks (or add a `replayPattern`).
3. Implement the cache + capture + replay behind a **default-off flag** first, so
   Play behaviour is unchanged until you opt in.
4. Wire section entry/exit at `AccompanimentProcessor.cpp:1030` and the
   PlaySection advance path.
5. Add the six tests, then flip the default on.
6. Run the DI audit over a real multi-section take (`MA_DI_WAV`) and confirm the
   second VERSE replays.
7. Bump the version, commit, hand back to the user for an ear test.

**Keep the diff small and the tests loud.** The mirror took nine regressions to
get here; the fastest way to lose it is to change the voice-ownership rules in
`BassVoice`/`PhraseLearner` without a test that pins them.
