> **Canonical.** Read-only audit of the test suite at v1.0.3 (`5d5f410`), 2026-09-14. Both
> binaries were executed: 269 unit + 79 integration cases, 89,235 assertions, 0 failures,
> 0 `[!mayfail]`. This file is the authority on what the suite does and does not cover.
> See also [`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md) and [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md).

# Test-Suite Audit — fuzzyband (MetalAccompaniment)

Read-only audit of the automated test suite in `/Users/ng/projects/fuzzyband`.
Both built binaries were executed. No source or test file was modified; this
document is the only artefact created.

- Repository: `/Users/ng/projects/fuzzyband`
- Framework: Catch2 v3.5.2 (`CMakeLists.txt:64-70`)
- Binaries: `build/MetalAccompanimentTests` (unit), `build/MetalAccompanimentIntegrationTests`
- Test sources: 35 `.cpp` files in `tests/` (plus `tests/fixtures/MidiProbe.h`)
- Date of run: binaries built 2026-09-14; audit run against those artefacts.

---

## 1. Execution results (actually run)

Both binaries were run to completion from the repo root.

### 1.1 Unit — `./build/MetalAccompanimentTests`

```
Randomness seeded to: 1364720531
metal_groove latency ms: min=0.183 mean=0.220 p99=0.549 max=4.694
===============================================================================
All tests passed (87984 assertions in 269 test cases)
```

- Exit code: **0**
- Assertions: **87,984**
- Test cases: **269 passed, 0 failed**
- Failures: **none**
- Skipped: none

Note the output line `metal_groove latency ms: ... max=4.694` — this is a
*passing* run of the latency benchmark (`tests/test_onnx_latency_benchmark.cpp:115`
requires p99 < 5.0 ms). Re-running that single case in isolation twice gave
`p99=3.060 max=42.886` and `p99=0.232 max=1.770`; the threshold is therefore
flake-prone under machine load (see §6.4).

### 1.2 Integration — `./build/MetalAccompanimentIntegrationTests`

```
Randomness seeded to: 1910252526
[GROOVE] section 1..12: patIdx=22 / 4 ...
[STABILITY] Starting 300+ second stability test...
[STABILITY] Complete — 62 sections, 30 non-silent, 240s music, ~312s total
[PERF] 10000 blocks @ 256 samples
[PERF]   mean: 0.949825 ms, min: 0.841959 ms, max: 2.34 ms, p99: 1.448 ms
===============================================================================
All tests passed (1251 assertions in 79 test cases)
```

- Exit code: **0**
- Assertions: **1,251**
- Test cases: **79 passed, 0 failed**
- Failures: **none**
- Skipped: none

### 1.3 Combined

| Metric | Value |
|---|---|
| Test cases | **348** (269 unit + 79 integration) |
| Assertions | **89,235** |
| Failures | **0** |
| `[!mayfail]` tests | **0** |
| `[!shouldfail]` tests | **0** |
| Hidden `[.]` tests | **0** |

### 1.4 Tag grep results

```
$ grep -rn "mayfail"   tests/   -> no matches (exit 1)
$ grep -rn "shouldfail" tests/  -> no matches (exit 1)
$ grep -rn '\[\.\]'    tests/   -> no matches (exit 1)
```

There are **no** `[!mayfail]`, `[!shouldfail]`, hidden `[.]`, `SKIP()`, `FAIL()`
or `WARN()` markers anywhere in the suite, and no Catch2 `SECTION`,
`DYNAMIC_SECTION` or `GENERATE` (no conditionally-entered paths inside a case).
A clean green run therefore means exactly what it says: nothing is being
suppressed. The weaknesses documented below are **structural** (missing or
weakened assertions), not suppressed failures.

Within-file build-flag guards that make tests *silently disappear* rather than
skip at runtime:

- `tests/test_metal_groove_inference.cpp:3,12,268` — all 16 cases vanish if
  `MA_ENABLE_ONNX=OFF`, yet the file stays unconditionally listed
  (`CMakeLists.txt:288`) and is not in `MA_CONDITIONAL_TEST_FILES`
  (`CMakeLists.txt:427-430`), so the orphan-test guard stays green.
- `tests/test_mel_spectrogram.cpp:102-116` — the Style-CNN case vanishes with ONNX off.
- `tests/test_groove_renderer_onnx.cpp` — dropped entirely when
  `MA_BUNDLE_GROOVE_RENDERER=OFF` (`CMakeLists.txt:361-364`).
- `tests/test_processor_pipeline.cpp:109-120` — the ONNX-backend case vanishes with ONNX off.

---

## 2. Per-file inventory (all 35 files)

Counts are the test cases actually present in the built binaries (verified via
`--list-tests --reporter xml`), which equals the `TEST_CASE` macro count in
every file. "Asserts on" is the primary observable.

### 2.1 Unit binary — 269 cases in 24 files

| File | # | Subsystem | Asserts on |
|---|---|---|---|
| `test_energy_analyser.cpp` | 10 | `analysis/EnergyAnalyser` | Audio buffers (synthetic sines/impulse) → RMS/centroid/flux |
| `test_feature_capture.cpp` | 3 | `capture/FeatureCapture` | Text: JSONL substrings on disk |
| `test_golden_signal.cpp` | 4 | `PhraseLearner` + `PitchEstimator` | Real WAV fixtures → lock time/density; local DSP reimpl. |
| `test_groove_renderer.cpp` | 5 | `inference/GrooveRenderer`, `PatternPlayer` | Generated MIDI velocities + pure logic |
| `test_groove_renderer_onnx.cpp` | 1 | `inference/GrooveRenderer` (ONNX) | Inference arrays (`velocity`, `offset`) |
| `test_groove_template.cpp` | 8 | `midi/GrooveTemplate` | Pure logic / static constants |
| `test_mel_spectrogram.cpp` | 3 | `analysis/MelSpectrogramExtractor` | Audio buffers → mel array |
| `test_metal_groove_inference.cpp` | 16 | `inference/MetalGrooveInference` | ONNX inference indices / error counts |
| `test_midi_pattern_library.cpp` | 12 | `midi/MidiPatternLibrary` | Static authored pattern data |
| `test_midi_probe.cpp` | 2 | `PatternPlayer` / `MidiProbe` harness | Generated MIDI, absolute samples + fingerprint |
| `test_onnx_latency_benchmark.cpp` | 1 | `assets/metal_groove.onnx` | Wall-clock latency |
| `test_pattern_player.cpp` | 40 | `midi/PatternPlayer` | Generated MIDI; pure ornament logic |
| `test_pattern_rules.cpp` | 60 | `inference/PatternRules` | Pure logic only |
| `test_phase1_rendering.cpp` | 12 | `PatternPlayer` note-offs/gates | Generated MIDI (on+off) |
| `test_phase3_dynamics.cpp` | 8 | `PatternPlayer` dynamics/humanise | Generated MIDI + pure constants |
| `test_phrase_learner.cpp` | 29 | `analysis/PhraseLearner` | Pure logic on scalar (rms, pitch, conf) inputs |
| `test_pitch_estimator.cpp` | 4 | `analysis/PitchEstimator` | Audio buffers (synthetic sines) |
| `test_playback_gate.cpp` | 6 | `analysis/PlaybackGate` | Pure logic |
| `test_rule_based_inference.cpp` | 3 | `inference/RuleBasedInference` | Pure logic |
| `test_stable_pitch_tracker.cpp` | 12 | `analysis/StablePitchTracker` | Pure logic |
| `test_structure_sequencer.cpp` | 5 | `analysis/StructureSequencer` | Pure logic |
| `test_structure_tagger.cpp` | 2 | `analysis/StructureTagger` | Pure logic |
| `test_structure_tagger_extended.cpp` | 13 | `analysis/StructureTagger` | Pure logic |
| `test_sub_bass_energy.cpp` | 10 | `EnergyAnalyser` + `StructureTagger` | Audio buffers + pure logic |

### 2.2 Integration binary — 79 cases in 11 files

| File | # | Subsystem | Asserts on | In CI? |
|---|---|---|---|---|
| `test_processor_pipeline.cpp` | 56 | Full `AccompanimentProcessor` | Generated MIDI, accessors, UI layout (3 cases) | 53/56 (3 editor cases excluded) |
| `test_e2e_structure_transitions.cpp` | 6 | Full processor | Accessors only (MIDI buffer never read) | yes |
| `test_phase9_acceptance.cpp` | 6 | `PatternPlayer` (5) + processor (1) | Generated MIDI; accessors | yes |
| `test_baseline_capture.cpp` | 2 | Full processor | MIDI (case 202); case 164 = capture-only | **case 164 never runs** |
| `test_e2e_bpm_tracking.cpp` | 2 | Full processor | `getDisplayBpm()` only | yes |
| `test_phase2_clock.cpp` | 2 | Full processor + playhead | Generated MIDI across loop wrap / seek | yes |
| `test_e2e_groove_variety.cpp` | 1 | Full processor | Pattern-index set | yes |
| `test_e2e_silent_section.cpp` | 1 | Full processor | Generated MIDI + accessors | yes |
| `test_long_duration_stability.cpp` | 1 | Full processor soak | Range invariants only | **no** |
| `test_performance_benchmark.cpp` | 1 | Full processor perf | Wall-clock timing | **no** |
| `test_structure_shadow_integration.cpp` | 1 | Full processor golden | 2-line pattern-transition golden | yes |

The integration binary is **not** auto-discovered; its 79 cases are reachable
only through ten tag-filtered `add_test` entries (`CMakeLists.txt:475-545`).
Any case whose tags do not match one of those filters never runs under CTest.

---

## 3. Coverage mapping of source subsystems

`src/` files and whether they have *direct* tests (a test that includes the
header and exercises the class):

| Source | Direct test(s) | Verdict |
|---|---|---|
| `src/AccompanimentProcessor.cpp` (2483 lines) | 12 integration files; 50/56 pipeline cases call `prepareToPlay`+`processBlock` | **Heavily tested**, but always with the inference thread paused (§4f) |
| `src/AccompanimentEditor.cpp` (1043 lines) | `test_processor_pipeline.cpp:1663,1681,1733` | **Effectively untested in CI** — 3 cases exist but no CTest filter matches `[editor][integration]` |
| `src/midi/PatternPlayer.cpp` | `test_pattern_player`, `test_phase1_rendering`, `test_phase3_dynamics`, `test_phase9_acceptance`, `test_midi_probe`, `test_groove_renderer` | **Strongly tested** (MIDI level) |
| `src/midi/MidiPatternLibrary.cpp` | `test_midi_pattern_library` (+7 files indirectly) | Tested (static data / clamps) |
| `src/analysis/PhraseLearner.cpp` | `test_phrase_learner`, `test_golden_signal` | **Tested, but with a test-local re-implementation of the RMS front end** (§4d) |
| `src/analysis/EnergyAnalyser.cpp` | `test_energy_analyser`, `test_sub_bass_energy` | Tested for RMS/centroid/flux; **`getOnsetRmsEnergy()` has zero direct call sites** |
| `src/analysis/PitchEstimator.cpp` | `test_pitch_estimator`, `test_golden_signal` | Tested (synthetic sines, 48 kHz only) |
| `src/analysis/StablePitchTracker.cpp` | `test_stable_pitch_tracker` | Tested |
| `src/analysis/StructureSequencer.cpp` | `test_structure_sequencer` | Tested |
| `src/analysis/StructureTagger.cpp` | `test_structure_tagger`, `test_structure_tagger_extended`, `test_sub_bass_energy` | Tested |
| `src/analysis/PlaybackGate.cpp` | `test_playback_gate` | Tested |
| `src/analysis/MelSpectrogramExtractor.cpp` | `test_mel_spectrogram` | Tested (2 cases always) |
| `src/analysis/AudioRingBuffer.cpp` | **none** | **No direct test.** Exercised only indirectly via `MelSpectrogramExtractor` |
| `src/inference/RuleBasedInference.cpp` | `test_rule_based_inference` | Tested |
| `src/inference/GrooveRenderer.cpp` | `test_groove_renderer`, `test_groove_renderer_onnx` | Tested (grid build + ONNX path) |
| `src/inference/MetalGrooveInference.cpp` | `test_metal_groove_inference`, `test_mel_spectrogram` | Tested; **secondary `style_cnn.onnx` failure is silently swallowed and untested** |
| `src/capture/FeatureCapture.cpp` | `test_feature_capture` | Tested weakly (field-name substrings only) |

Subsystems with **no direct unit coverage**:

1. **`AccompanimentEditor` / UI layout** — tests exist but are excluded from CI (§6.2).
2. **`AudioRingBuffer`** — never included by any test.
3. **The inference-thread concurrency surface** of `AccompanimentProcessor`
   (`featureQueue`, `grooveCommitQueue`, `pendingSongForm`, the `RiffUiRead`
   triple buffer) — compiled and linked, never raced (§4f).
4. **`getOnsetRmsEnergy()`** (the fast onset RMS the live bass mirror depends
   on) — never called by any test.

---

## 4. GAPS — the six historical regression classes

This is the core of the audit. Each subsection states what the suite *does*
verify and what it does **not**.

### (a) Live bass "mirror" of the guitarist's riff

**What is verified**

- `tests/test_processor_pipeline.cpp:2478-2591` ("play-mode bass mirrors the
  guitarist, harmony only fills gaps", block **512 only**):
  - both contour pitches occur somewhere: `bassNotes.count(36) > 0` (:2572) and
    `count(43) > 0` (:2573);
  - ≥90 % of the **nominal** eighth-note attacks have *some* bass note-on
    within ±30 ms: `REQUIRE(mirrored >= (numAttacks * 9) / 10)` (:2585);
  - density ceiling: `REQUIRE(bassAbs.size() <= numAttacks + numAttacks/3)` (:2591).
- `tests/test_processor_pipeline.cpp:2597-2660` — after picking stops, ≥1 bass
  onset appears later than 0.9 beat after the gap opens (`late >= 1`, :2659).
- `tests/test_phrase_learner.cpp:831-877` (newest commit) — a synthetic C2/G2
  contour yields both pitch classes (`distinct.count(36)` / `count(43)`, :873-874)
  and a count in `[12, 32]` for 24 attacks (:876-877).
- `tests/test_pattern_player.cpp:1309-1368` (T5.3) — one learned note "owns" the
  monophonic voice for its gate: `downbeatOns == 0` (:1366), grid resumes at
  `nextGridOns >= 1` (:1367).
- `tests/test_golden_signal.cpp` — real recorded guitar locks within 5-8 s and,
  for two fixtures, stays dense (`notesPerSec >= 6.0`, :209/:217).

**What is NOT verified**

- **No test asserts that bass pitch *corresponds to the guitar* note-for-note
  at each onset.** The mirror tests check set membership (36 and 43 each appear
  somewhere) plus proximity counts — never that attack *k*'s bass note equals
  the guitar's note for *k*. A bass line that plays 36 and 43 in the wrong
  order, or at the wrong onsets, passes.
- **No mirror latency tighter than 30 ms** (≈1440 samples, ~¼ of an eighth at
  120 BPM) and no per-attack alignment check.
- **No mirror note length / note-off** assertion (the `BassNote` type carries no
  duration — `src/analysis/PhraseLearner.h:21-26`).
- **Only monophonic alternation is tested** (:2544); no chords, no polyphony.
- **Only at block 512** (and the phrase-learner variant at 512).
- `tests/test_phase2_clock.cpp:78-97` computes a live pre-lock mirror count
  (`bassDuring`) and **both callers discard the return value** (:137, :244).
- The new `tests/test_phrase_learner.cpp:831` mirror test bypasses
  `PitchEstimator` entirely (it passes `36.0f`/`43.0f` as the pitch argument,
  :876) and uses the test-local `RmsWindow`, so `PitchEstimator`/`EnergyAnalyser`
  regressions cannot fail it.
- `tests/test_golden_signal.cpp`'s own `tests/fixtures/README.md:26-28` states
  it "only asserts lock time and note density ... not specific notes."

### (b) Buffer-size dependence of rendered MIDI

**What is verified**

- PatternPlayer-level full fingerprints at 128/512/2048:
  - `tests/test_midi_probe.cpp:69-110` — pattern 4, fixed seed, compares
    `MidiProbe::fingerprint` (:87-88) and crash holds (:106-110).
  - `tests/test_phase9_acceptance.cpp:138-155` — pattern 4, compares full
    fingerprints with count equality (:77-78, :153-154).
- Full-processor fingerprint at 128/512/2048:
  - `tests/test_baseline_capture.cpp:202-258` — real `AccompanimentProcessor`,
    4.8-bar count-in + capture span, `fingerprint(e128)==fingerprint(e512)==fingerprint(e2048)` (:256-257).
- Partial:
  - `tests/test_phase1_rendering.cpp:127-164` — 128 vs 2048, **crash holds only** (:160).
  - `tests/test_phase3_dynamics.cpp:225-256` — 128 vs 2048, **only a sorted
    (channel,note,velocity) bag**; sample positions are deliberately excluded.
  - `tests/test_pattern_player.cpp:241-281` — 512 vs 256, **one** tom onset with
    ±2400-sample slack (:279-281).
  - `tests/test_processor_pipeline.cpp:1997-2092` (T7.2) uses blocks 1024/2048
    but **compares the two sizes to each other not at all** — only per-cell
    property assertions.

**What is NOT verified**

- `test_processor_pipeline.cpp` uses block **512 in 55 of 56 cases**; no
  cross-block event comparison exists in it at all. The only full-processor
  invariant test (`test_baseline_capture.cpp:202`) is tagged
  `[baseline][golden][t9.2][phase9]` — **not** `[integration][pipeline]` — so it
  is not part of the `integration_processor_pipeline` job, though it does run via
  the `[t9.2]`/`[phase9]` jobs.
- The processor-level invariant test is **narrow and self-admittedly incomplete**
  (`test_baseline_capture.cpp:209-213`): *"the lock's own onset is still
  block-quantised (the capture is detected finishing on a block boundary), as is
  the transition start."* Only the count-in + capture is covered, and
  `within(span)` (:242-248) discards the tail.
- **The lock/transition/return-A path is never compared across block sizes** —
  only 512 (e.g. `test_processor_pipeline.cpp:2745-2874` grid-equality snapshot).
- **The live bass mirror path is only tested at 512.**
- `test_phase3_dynamics.cpp:238-241` **documents that note sample positions can
  differ across block sizes** and therefore compares only the (note,velocity)
  bag — the test titled "humanisation is block-size invariant" does not verify
  timing.
- The commit that fixed buffer invariance (`db31dd7`, "riff phase, loop-wrap
  re-phase, onset capture") records the residual
  deviations in its own message: *"fill arming differs at 128 (T7.2 follow-up),
  3 of ~55 bass notes between 512 and 2048 (capture edge), click note-off and
  lock onset land on block boundaries."* None of these are asserted as
  invariants; several are asserted as acceptable.
- No **128- or 256-sample** block appears anywhere in `test_processor_pipeline.cpp`;
  no test changes block size mid-session (so `lastClockBlockSamples` jump
  detection, `src/AccompanimentProcessor.h:311`, is untested).

### (c) Drum note-off lengths

**What is verified**

- `tests/test_phase1_rendering.cpp:64-90` — a per-(channel,note) on/off balance
  ledger, all channels, pattern 4, block 128. Offs not emitted during the render
  are flushed at `span` (:74-75), so this proves *accounting*, not in-block
  emission or length.
- `tests/test_phase1_rendering.cpp:92-125` — at block 128, a crash note-off
  lands in a later block than its note-on (`offBlock > onBlock`, :117).
- `tests/test_phase1_rendering.cpp:127-164` — crash holds equal at 128 vs 2048
  and `>= 1 beat` (:160-163).
- `tests/test_phase1_rendering.cpp:166-203` — open hat (46) `durationBeats >= 1.0`
  and crash (49) `>= 1.5` in pattern 4's *authored data* (:173-175); rendered
  `minHold(49) >= spb` and `minHold(46) >= spb*9/10` (:201-202).
- `tests/test_midi_pattern_library.cpp:143-158` — open-hat `durationBeats >= 1.0`
  across all patterns (:153).
- `tests/test_phase9_acceptance.cpp:159-175` — crash hold `>= 1 beat` at
  128/512/2048 (:173). Only the first on/off pair is measured (:119-132).
- `tests/test_pattern_player.cpp:519-569, 617-677` — bass note-on/off **count**
  equality (no length).

**What is NOT verified**

- **`tests/test_processor_pipeline.cpp` contains zero note-off references**
  (`isNoteOff`/`NoteOff`/`durationBeats` → no matches). It includes 56 integration
  cases over the full processor and never inspects a note-off.
- **No length assertion exists for kick (36), snare (38), closed hat (42), toms
  (41/43/45/47/48) or side-stick (37)** anywhere in the suite. Only crash and
  open hat have duration assertions.
- `durationBeats` defaults to `0.25f` (`src/midi/MidiPatternLibrary.h:17`), so
  the entire percussive core is authored at a 16th — and none of it is
  length-checked.
- The machinery is wholly uncovered: `PatternPlayer::scheduleDrumNoteOff`
  (`src/midi/PatternPlayer.cpp:580-612`), `flushDueDrumNoteOffs` (:560-578), the
  128-slot `drumNoteOffSample` table (`src/midi/PatternPlayer.h:466-467`), the
  open-hat ≥1-beat clamp (`src/midi/PatternPlayer.cpp:729-730`), crash gate
  (:654-655) and retrigger/close ordering.
- The only test that observes a drum note-off in a deferred path
  (`test_pattern_player.cpp:149-188`) uses a single giant 8-bar block, so the
  cross-block flush path is never asserted.
- `tests/fixtures/MidiProbe.h:115-124` provides `noteOffs()` for exactly this;
  `test_processor_pipeline.cpp` does not include the header at all.

### (d) Onset / attack detection

**What is verified**

- `tests/test_phrase_learner.cpp` is the only real attack-detector surface, and
  it asserts **rates/counts**, not detection quality:
  - 16th pulse train at 200 BPM → `2.5 <= attacksPerBeat <= 6.0` (:733-734),
    constant tone → `getAttackCount() == 0` (:741); a ±50 % tolerance.
  - `getAttackCount() >= 4` / `>= 8` (:155, :477); pre-lock mirror `>= 4` (:180).
  - "sustained tone does NOT lock" (:284-310) — asserts `isLocked()` only, not
    the attack count.
- Indirect processor-level counts: `test_processor_pipeline.cpp:3358-3370`
  (`maxGate >= 4`, `onsetCount <= 16`) and `:3439-3447` (`onsetSlots >= 8`).
- Golden signal (`tests/test_golden_signal.cpp`) locks real recorded guitar,
  which exercises the detector end-to-end but asserts only lock time/density.

**What is NOT verified**

- **`EnergyAnalyser::getOnsetRmsEnergy()` has zero direct call sites** in the
  whole suite (its only test mention is a comment at `test_phrase_learner.cpp:48`).
- Every test feeds the learner through a **test-local `RmsWindow`** class
  (`tests/test_phrase_learner.cpp:52-80`, `tests/test_golden_signal.cpp:105-133`)
  that duplicates the analyser. The newest commit (`5d5f410`) changed both
  copies from a 0.1 s to a 0.02 s window to track a source change — evidence the
  duplication must be manually kept in sync.
- Worse, `test_phrase_learner.cpp:73` **omits the production 1.0 clamp**
  (`EnergyAnalyser.cpp:155` uses `juce::jlimit(0.0f, 1.0f, ...)`) — the chug
  input `0.4*env` can exceed 1.0, so `PhraseLearner` is exercised with values
  production never emits, and nothing asserts the test double equals the real
  analyser. (`test_golden_signal.cpp:126` *does* clamp.)
- **No test measures a detected onset sample against a known injection time**,
  latency, jitter, or false-positive rate. `makeClickBuffer`
  (`test_processor_pipeline.cpp:51-63`) is never used to probe the detector.
- Detector thresholds are never probed from below: the `rms > 0.01` floor
  (`src/analysis/PhraseLearner.cpp:93`), `kMinAttackIntervalSamples=2000`
  (`PhraseLearner.h:384`), `kFallWindowBlocks=20` (:385) and the `rms < 0.002`
  fall boundary (:78) are untested. The "slow ramp rejected" branch is never
  reached (the swell test is monotonic, so `fallCounter_` is never set), so the
  "attack detector ignores slow ramps" title overstates what is asserted.
- `tests/test_phrase_learner.cpp:460-461` documents and works around a real
  detector artefact: *"the first rise may not count until a fall has armed the
  detector, so we over-feed"*. The first post-reset attack is never detected
  (`src/analysis/PhraseLearner.cpp:37-39` resets `prevRms_`/`fallCounter_`;
  `:93` needs `fallCounter_ > 0`) — this is encoded as expected, not asserted
  as a bug.
- `EnergyAnalyser.cpp:159-165` applies the peak-envelope release **once per
  `process()` call**, so the decay is block-size dependent; the test
  `test_energy_analyser.cpp:146-166` codifies per-call behaviour and even allows
  an increase (`<= peak + 0.01f`, :166).

### (e) Transport loop-wrap handling

**What is verified**

- `tests/test_phase2_clock.cpp:121-227` (T2.1) is the only genuine loop-wrap
  test: `TransportPlayHead::getPosition` wraps `t = samples % loopLength`
  (:33-39), `ph.loopLength = 4 bars` is set at :152, ~5 wraps are driven (:157).
  It asserts bass note-ons `>= onsets` after the wrap (:217), within the first
  riff cycle (:218), and on the 16th grid (:219, 20 ms tolerance), plus lock
  expiry and transition (:221-223).
- `tests/test_phase2_clock.cpp:229-307` (T2.2) — a **seek** (:262-263), not a
  loop: remaining duration preserved, kick-on-grid `>= 1` (:302-303).
- `tests/test_baseline_capture.cpp:40-60, 164-200` wraps a 4-bar DAW loop.

**What is NOT verified**

- **The loop-wrap capture asserts nothing about the wrap.**
  `test_baseline_capture.cpp:190-195` renders the looped session at 128/512/2048
  but the only assertion is `REQUIRE(writeCapture(...))` (:194), i.e. that
  non-empty TSV+MID files were written. There is no loop-vs-free-run comparison,
  no re-phase check, no fingerprint, and the looped captures lack the extra
  `noteOns` non-empty check that the non-looped ones have (:187).
- T2.1 does not assert re-phasing to the loop boundary and never compares MIDI
  before vs after the wrap — only aggregate counts/grid.
- Its assertions are weakened by
  `if (onsets < 1) onsets = occupied;  // back-compat if gates were not stamped`
  (`test_phase2_clock.cpp:146-147`), which can make every subsequent
  `>= onsets` bound trivially satisfiable.
- `tests/test_processor_pipeline.cpp` has **no loop coverage at all**: its two
  `FakePlayHead`s only advance a monotonic sample counter (:2010-2012,
  :3083-3085). The `loop` APVTS parameter is never set; `reanchorLockClockOnJump`
  (`src/AccompanimentProcessor.h:246-247`), `lastSeenBarsElapsed`
  ("loop/restart edge detection", :452) and `lastLoopValue` (:483) are untested
  there. (T2.1/T2.2 live in `test_phase2_clock.cpp` but do run under the
  `[integration][pipeline]` label.)
- The phrase-learner's internal wrap branch
  (`src/analysis/PhraseLearner.cpp:738-742` and crossed-wrap `:754-755`) is
  never asserted; `test_phrase_learner.cpp:268-281` only checks
  `len % 4 == 0` and `0 <= phase < len`.

### (f) Thread races between audio thread and UI/analysis threads

**This is the single largest hole.** The processor starts a real background
inference thread in its **constructor**:

- `src/AccompanimentProcessor.cpp:139-140` —
  `inferenceRunning.store(true); inferenceThread = std::thread([this]{ inferenceLoop(); });`
- It is joined only in the destructor (`:143-148`).
- `prepareToPlay` sets `inferencePaused = true` (`:159`), so the thread is parked
  by default and spins on a 5 ms sleep (`:664-678`).

**What is verified**

- `tests/test_processor_pipeline.cpp:271-305` ("pause/flush/resume cycle does
  not corrupt state") is the only test that calls
  `resumeBackgroundInferenceForTests()` (:292). It then reads
  `getDisplayPatternIndex()` (:295) and `getDisplayBpm()` (:300) while the thread
  runs — but **it never processes audio during that window**, so the
  producer/consumer queues are idle.
- 50/56 pipeline cases call `pauseBackgroundInferenceForTests()`, then drive the
  whole pipeline through `flushBackgroundInferenceForTests()` (60 call sites),
  which takes the same `inferenceDrainMutex` the loop takes
  (`src/AccompanimentProcessor.cpp:688` vs `:673`) and drains the feature queue
  **synchronously on the test thread**. This exercises the lock protocol
  *uncontended*, never concurrently.
- The repo-wide only other concurrency-adjacent construct is a test-local
  `std::atomic<int>` parameter listener (`test_processor_pipeline.cpp:1747`),
  which is single-threaded.

**What is NOT verified**

- **No test runs `processBlock` concurrently with a live inference thread.** The
  declarations say so themselves (`test_processor_pipeline.cpp:3-5`: "exercise
  the processor deterministically without real-time threading").
- Consequently the real shared state is never raced: `featureQueue`
  (`src/AccompanimentProcessor.h:279`), `grooveCommitQueue` (:280),
  `pendingSongForm` (atomic shared_ptr, :478-480, `AccompanimentProcessor.cpp:842,2418`),
  the display atomics (:295-304), and the `RiffUiRead` triple-buffer UI snapshot
  protocol (:328-351).
- **The TSAN job therefore races nothing.** `.github/workflows/ci.yml:122-127`
  runs `MetalAccompanimentTests "[midi][T8.2]"` (a `PatternPlayer`-only test —
  no processor, no thread) and `MetalAccompanimentIntegrationTests "[integration][pipeline]"`
  — and every `[integration][pipeline]` test pauses the inference thread. With
  the producer and consumer serialised on one thread, ThreadSanitizer has no
  concurrent access to observe.
- `releaseResources()` does not join the thread (`AccompanimentProcessor.cpp:423-426`).

---

## 5. Tests that encode a bug / were changed to make a fix pass

No test contains a literal "known-wrong golden". The pattern is **weakened
bounds, unasserted computed oracle values, and assertions rewritten to match
changed behaviour**. The most significant:

### 5.1 The newest commit reversed a behavioural contract and rewrote the tests

Commit `5d5f410` "Mirroring choices fix" (2026-09-14, HEAD) changed four test
files and made these semantic reversals:

- `tests/test_pattern_player.cpp:1309` (T5.3). **Before**: the test required the
  authored bass grid hit at the downbeat (`REQUIRE(beat1Ons >= 1)`). **After**:
  it requires the grid to be *absent* there — `REQUIRE(downbeatOns == 0)`
  (:1366), with the comment at :1311-1316 explaining it "Supersedes the
  pre-mirror-primary rule". A correct-looking assertion was replaced by its
  opposite. This is a deliberate design change, but it is exactly the pattern
  "test updated so the fix passes".
- `tests/test_processor_pipeline.cpp:2478` (play-mode bass). **Before**: required
  a pick within 150 ms plus authored beat-1/beat-3 harmony hits (`beat1Hits >= 3`,
  `beat3Hits >= 2`). **After**: pitch-set membership + 90 % within 30 ms + a
  33 % density allowance. The harmony assertions were dropped; a new test
  (:2597) covers the fallback.
- `tests/test_phrase_learner.cpp:52-73` and `tests/test_golden_signal.cpp:105-133`
  — both test-local `RmsWindow` classes were changed from a 0.1 s to a 0.02 s
  window to follow the production change. The tests duplicate the implementation
  they verify.
- `tests/test_phrase_learner.cpp:831` — a **new** mirror test was added in the
  same commit (see §4a).

### 5.2 A test weakened in the "Responsiveness testing" commit

Commit `e463847` relaxed the editor smoke assertion:
`REQUIRE(ed->getHeight() >= 760)` → `REQUIRE(ed->getHeight() >= 460)`
(`tests/test_processor_pipeline.cpp:1671-1673`), justified by the comment "The
default height is clamped to the display ... so only the resize floor is
guaranteed here." The new "Editor panel fits a laptop-height window" test added
in the same commit is tagged `[integration][editor]` and **never runs in CI**
(§6.2).

### 5.3 A test weakened in the loop-wrap/onset fix

Commit `db31dd7` changed `tests/test_phase2_clock.cpp:189-218` and
`tests/test_processor_pipeline.cpp:603-611, 2804-2814` to measure from
`getRiffAPlayOriginSample()` instead of the detecting block boundary. The test
changes are legitimate (the old oracle was wrong), but they were made in the
same commit as the fix, and the commit message additionally records admitted
residual non-invariance (see §4b).

### 5.4 Assertions that pin contract violations or contradictions

- `tests/test_pattern_rules.cpp:243-250` — asserts
  `isPatternCompatibleWithState(7, LOUD) == false` (:247) **and**
  `diversifyPattern(4, f, 0) == 7` (:249). Two public predicates contradict;
  the test pins the contradiction as intended.
- `tests/test_pattern_rules.cpp:828-835` — "constrainToPool falls back globally
  when the pool mismatches state": asserts
  `REQUIRE_FALSE(poolContains(verse, snapped))` (:834), i.e. a function named
  *constrainToPool* returning an index outside the pool is asserted as correct.
- `tests/test_pattern_player.cpp:761-789` — "T8.2 pending learned overrun drops
  the newest note": asserts `notes.count(50) == 0` (:786) while older notes
  survive. Drop-newest overflow is frozen as the contract.
- `tests/test_pattern_player.cpp:1226-1274` — asserts `rideSwitch == 0` (:1274),
  and `:1084-1090, :1111, :1127` all assert `!o.rideSwitch`: the ride-switch
  ornament is asserted to **never fire** ("stated rate 0") even though the test
  is named "ornaments fire near their stated rate".
- `tests/test_rule_based_inference.cpp:61-62` — explicitly exempts
  `(SILENT, exclude == 0)` from "exclusion never returns excluded index".
- `tests/test_metal_groove_inference.cpp:228-238` — deliberately never loads the
  model and pins the degraded contract (`== 0`, `== 4 // silence default`, `== 0`).
- `tests/test_groove_renderer.cpp:78-81` — asserts `cond[1..11] == 0.0f`,
  encoding that most advertised model inputs are discarded.

### 5.5 Tautological or unfalsifiable assertions

- `tests/test_pattern_player.cpp:1097` — `REQUIRE(!(o.rideSwitch && (patternHasNote(lib,22,51) || patternHasNote(lib,22,53))))`.
  The same test already asserted `!patternHasNote(lib,22,51)` and
  `!patternHasNote(lib,22,53)` at :1073-1074, so the disjunction is always false
  and the assertion cannot fail.
- `tests/test_pattern_player.cpp:1413` — `REQUIRE(pos < bar)` where the loop
  bound (`while (pos < beat*2)`, :1404) guarantees it. The "next beat, not next
  bar" claim rests only on the weaker `applied` flag (:1402-1412).
- `tests/test_pattern_player.cpp:1519-1527` — "fill 18 replaces groove kicks at
  3.75" asserts only `near375 <= 1` (:1527); emitting **zero** kicks passes.
- `tests/test_long_duration_stability.cpp:198-199, :206-207` — the counts can
  never fail (the loop executes a fixed 30 non-silent sections), and
  `minBpmSeen`/`maxBpmSeen`/`minPatSeen`/`maxPatSeen` are computed (:150-154)
  but only printed (:190-191), never asserted.
- `tests/test_performance_benchmark.cpp:109` — `CHECK(maxVal < 150.0)` with its
  own comment admitting observed spikes to ~90 ms while the mean is ~0.4 ms; the
  header's promised leak/throughput checks (:6-7, :111-112) are comments only.
- `tests/test_playback_gate.cpp:34,60,86` — `REQUIRE_FALSE(armCrash)` on a
  SILENT decision is structurally always false (`armCrash` is only set in the
  non-SILENT branch, `src/analysis/PlaybackGate.cpp:35-42`).
- `tests/test_groove_template.cpp:22-26` — all non-strict `>=`; a flat template
  passes the "velocity hierarchy" test.
- `tests/test_metal_groove_inference.cpp:120-135` — named "different inputs",
  asserts only `>= 0` for both (the comment admits they "may or may not differ").
- `tests/test_metal_groove_inference.cpp:178-187` — `style in [0,4]` includes
  the not-loaded/silence default `4`, so a dead classifier passes.
- `tests/test_groove_renderer_onnx.cpp:32-35` — bounds are exactly the source
  `std::clamp` limits (`src/inference/GrooveRenderer.cpp:203-206`).
- `tests/test_e2e_structure_transitions.cpp:200` — `REQUIRE(rms < rmsAfterSignal)`
  accepts any decrease; `:307` allows up to 4 state changes in 8 alternating bars.
- `tests/test_structure_shadow_integration.cpp` golden
  (`tests/expected_structure_shadow_pattern_transitions.txt`) has **two lines**
  (`512 0`, `1024 4`) for a 2.0 s fixture; `lastPat = -999999` (:47) guarantees a
  first record even if nothing transitions.

### 5.6 Self-referential "expected value" arithmetic

`tests/test_phase3_dynamics.cpp:116-126` and its duplicate
`tests/test_phase9_acceptance.cpp:193-200` recompute the verse/chorus velocity
delta from the **same production constants** they are supposed to check
(`Groove::presetFor(0)`, `Groove::rock().velocityMul[4]`,
`PatternPlayer::kVelocityTrim`) with hard-coded literals — these cases render no
MIDI and cannot catch an engine regression. Similarly
`tests/test_rule_based_inference.cpp:60` validates routing with the same
`PatternRules::isPatternCompatibleWithState` table the selector consults.

### 5.7 Test-count inflation through duplication

The 348 figure overstates distinct coverage. Near-duplicate or repeating cases
include:

- `test_pattern_player.cpp:325` vs `:375` (near-identical ~50-line bodies);
  `:431` vs `:461`; `:791` vs `:821`; `:1012` vs `:1197`; `:1038` vs `:1277`;
  `:519` vs `:617`.
- `test_pattern_rules.cpp:316` vs `:368`; `:171` vs `:187`; `:200` vs `:305`;
  `:122` vs `:331`; `:420` vs `:786`; three `selectFillPattern` seed loops.
- `test_phrase_learner.cpp:348` vs `:805` (byte-identical deviant-attack
  lambdas); `:312` vs `:328`; `:494` vs `:678` vs `:578`.
- `test_stable_pitch_tracker.cpp:52` vs `:144`; `:79` vs `:170`; five copies of
  `runToStable`+compare.
- `test_midi_pattern_library.cpp:6-11` duplicated by `test_groove_template.cpp:129-133`.
- Integration: `test_baseline_capture.cpp:202` runs **twice** in CI, matching
  both `[t9.2]` and `[phase9]`; `test_phase9_acceptance.cpp:138` likewise.

### 5.8 Stale / misleading test documentation

- `tests/test_structure_tagger_extended.cpp:6-10` cites `kLoudRms(0.60)` and
  `kSilentRms` bands; the real values are `kLoudRms = 0.45f`
  (`src/analysis/StructureTagger.h:67`) with an adaptive floor. Inputs are
  0.040/0.650, so no test pins the actual 0.45 boundary.
- `tests/test_pattern_rules.cpp:316` title says `kPatternCount=22`; the constant
  is 28 (`src/midi/MidiPatternLibrary.h:40`), asserted at :393.
- `tests/test_baseline_capture.cpp:5-6` says "Not registered in CTest"; its
  second case is registered twice.
- `tests/test_performance_benchmark.cpp:35` title says "<1ms" while the
  assertion is `< 150 ms` (:109).
- `tests/test_e2e_structure_transitions.cpp:139` title says pattern `[4,5]`;
  the body asserts only `isPatternCompatibleWithState(pat, LOUD)` (:167).
- `tests/test_sub_bass_energy.cpp` cites a non-existent `kAmbientCeil` (:94) and
  `kLoudRms=0.60` (:119,:154,:166).
- `tests/test_structure_shadow_integration.cpp:51` reuses one uncleared
  `juce::MidiBuffer` across all blocks (harmless only because that test never
  reads it).

---

## 6. Sanitizers, CI, and how tests are invoked

### 6.1 CMake / CTest invocation

`CMakeLists.txt`:

- `option(MA_BUILD_TESTS "Build unit tests" ON)` (:14).
- Unit target `MetalAccompanimentTests` (:282-376) compiles 22 test files plus
  two ONNX-gated ones and links the plugin sources directly.
- Integration target `MetalAccompanimentIntegrationTests` (:378-420) links the
  full `MetalAccompaniment` plugin target.
- **Orphan-test guard** (:422-463): configuration fails if any `tests/*.cpp` is
  not compiled into a target, exempting the two flag-gated files
  (`test_onnx_latency_benchmark.cpp`, `test_groove_renderer_onnx.cpp`, :427-430).
  This does **not** catch onnx-gated cases *inside* `test_metal_groove_inference.cpp`.
- `catch_discover_tests(MetalAccompanimentTests PROPERTIES LABELS "unit")`
  (:470-472) registers **every** unit case, including the latency benchmark, as
  an individual CTest with label `unit`.
- The integration binary gets **ten tag-filtered `add_test` entries**:
  `[structure][shadow]`, `[integration][pipeline]`, `[e2e][silent]`, `[e2e][bpm]`,
  `[e2e][transitions]`, `[e2e][groove]`, `[stability][long]`, `[perf]`, `[t9.2]`,
  `[phase9]` (:475-545). No `ctest` entry runs the integration binary unfiltered.

### 6.2 CI coverage holes (verified against `build/CTestTestfile.cmake`)

`.github/workflows/ci.yml` macos job runs only:

```
ctest --test-dir build --output-on-failure -L unit
ctest --test-dir build --output-on-failure -L integration
ctest --test-dir build --output-on-failure -L e2e
ctest --test-dir build --output-on-failure -L onnx
```

(`ci.yml:57-67`). The generated labels are `e2e` (×4), `integration` (×2),
`integration;phase9` (×2), `stability` (×1), `perf;phase9` (×1), `onnx;benchmark`
(×1), plus `unit` per discovered case. Therefore:

1. **`stability_long_duration` (`LABELS "stability"`) never runs in CI** — the
   300+ second soak test is excluded by all four `-L` filters.
2. **`perf_block_processing` (`LABELS "perf;phase9"`) never runs in CI** — the
   performance benchmark is excluded by all four filters.
3. **Four integration test cases are never selected by any filter**:
   - `test_processor_pipeline.cpp:1663` "Editor construction smoke test"
     (`[editor][integration]`)
   - `test_processor_pipeline.cpp:1681` "Editor panel fits a laptop-height
     window without clipping" (`[editor][integration]`)
   - `test_processor_pipeline.cpp:1733` "T8.3 genre change notifies host of
     swing" (`[editor][integration][swing]`)
   - `test_baseline_capture.cpp:164` "Baseline: capture current MIDI at
     128/512/2048, loop wrap, and Play form" (`[baseline][capture]`)

   The three editor cases contain **all nine `CHECK(...)` in
   `test_processor_pipeline.cpp`** — the entire UI-layout assertion surface never
   executes in CI. `catch_discover_tests` is applied only to the unit target, so
   these are unreachable.
4. **`test_baseline_capture.cpp:202` runs twice per CI run** — it matches both
   `[t9.2]` (`t9_2_buffer_invariance`) and `[phase9]` (`t9_acceptance`).
5. There are **no `TIMEOUT` properties** on any test; the ~312 s stability soak
   would block a CI job if it were ever enabled.

`release.yml` and `pages.yml` contain **no test steps at all** — release
binaries are built and published without running the suite.

### 6.3 Sanitizers

- `option(MA_ENABLE_TSAN "Build with ThreadSanitizer (Clang/GCC)" OFF)`
  (`CMakeLists.txt:18-32`): adds `-fsanitize=thread -fno-omit-frame-pointer -g -O1`
  before `FetchContent` so JUCE is instrumented, and defines `MA_ENABLE_TSAN=1`.
  This is the **only** sanitizer option.
- **No ASan, MSan or UBSan option or job exists** anywhere in `CMakeLists.txt` or
  `.github/` (grep for `asan|ubsan|address.?sanit|undefined` finds only font file
  names).
- CI `tsan` job (`.github/workflows/ci.yml:106-127`): configures
  `-DMA_ENABLE_TSAN=ON -DMA_ENABLE_ONNX=OFF -DMA_BUNDLE_GROOVE_RENDERER=OFF
  -DMA_BUILD_STANDALONE=OFF`, builds both test targets, then runs
  `TSAN_OPTIONS=halt_on_error=1 second_deadlock_stack=1`:
  - `./build-tsan/MetalAccompanimentTests "[midi][T8.2]"`
  - `./build-tsan/MetalAccompanimentIntegrationTests "[integration][pipeline]"`
- **The TSan job cannot detect the audio/inference race** because
  `[midi][T8.2]` is a `PatternPlayer`-only unit test (no processor, no thread) and
  every `[integration][pipeline]` processor test pauses the inference thread
  before `processBlock` and drains synchronously on the test thread (§4f).
  ThreadSanitizer therefore has no concurrent access to observe.
- The JUCE plugin editor and `juce_add_binary_data` assets also mean TSan builds
  skip ONNX and the standalone, so the ONNX inference thread path is not
  instrumented either.

### 6.4 Flakiness / environment dependence

- `tests/test_onnx_latency_benchmark.cpp:115` asserts `p99Ms < 5.0`. The full
  unit run reported `p99=0.549 max=4.694`; re-running that one case in isolation
  gave `p99=3.060 max=42.886` once and `p99=0.232 max=1.770` once. The spread
  (max up to 8.6× the limit) shows the assertion can fail on a loaded CI runner.
- Because `catch_discover_tests` labels it `unit`, this benchmark **gates the
  ordinary unit suite and runs twice** (`-L unit` at `ci.yml:58` and `-L onnx`
  at `:67`).
- `tests/test_onnx_latency_benchmark.cpp:74` computes the p99 index as
  `size_t(samples.size() * 0.99)` = 9900 for 10000 samples, dropping the worst
  99 samples; a `REQUIRE` also runs inside the timed region (:109).
- `tests/test_feature_capture.cpp` writes a JSONL capture into the user's
  Documents/Application-Data directory (`src/capture/FeatureCapture.cpp:96-105,144`)
  and deletes it only on the success path (`:38, :89, :105`); a failed assertion
  leaks the file.
- `tests/test_golden_signal.cpp:39-97` returns a default (unlocked) result if a
  fixture is missing, so `REQUIRE(r.locked)` fails — loud, not silent — but the
  fixture paths depend on the compile-time `MA_REPO_ROOT`.

---

## 7. Trustworthiness verdict

**What the suite is good at**

- MIDI-level determinism and block-size invariance of `PatternPlayer` output
  (full fingerprints, 128/512/2048).
- Lock/transition/return state machine over many scenarios against the real
  processor with rendered MIDI.
- Structural/pure-logic coverage of `PatternRules` (60 cases), `StructureTagger`
  (15), `StablePitchTracker` (12), `StructureSequencer`, `PlaybackGate`.
- Real-audio golden fixtures for the analysis layer (lock time/density), which
  do catch the historical attack-detector/density regressions.
- A clean run with zero suppressed failures: no `[!mayfail]`, no hidden tests,
  no runtime skips; every `TEST_CASE` in every file is compiled.

**Why a green run is not sufficient evidence**

1. The full-processor path is only ever exercised with the inference thread
   parked, and the TSan job re-runs exactly those tests — so thread races (f) are
   effectively untested despite a TSan target existing.
2. Buffer-size invariance (b) is proven only for `PatternPlayer` and for a
   narrow count-in+capture span; the lock onset, transition start, live mirror
   and 128-sample fill arming are known non-invariant and untested as such.
3. The live bass mirror (a) is asserted only as pitch-set membership + 90 %
   within 30 ms + a 33 % density allowance, at one block size, with no
   per-attack pitch identity.
4. Drum note-off lengths (c) are asserted only for crash and open hat; the
   percussive core (kick/snare/closed-hat/toms/side-stick) and the deferred
   note-off scheduler are unasserted, and `test_processor_pipeline.cpp` contains
   no note-off reference at all.
5. Onset detection (d) is never directly tested: `getOnsetRmsEnergy()` has no
   call sites, and the tests feed a hand-copied `RmsWindow` that omits the
   production clamp.
6. Large parts of the suite are unreachable in CI: the stability soak, the perf
   benchmark, the entire editor/UI-layout surface, and the baseline capture case.
7. Test-count inflation: near-duplicate cases and tautological assertions
   (documented in §5.5, §5.7) mean 348 cases is an upper bound on distinct
   behaviours, not a lower bound on confidence.
8. Several "known limitation" comments record exactly the residual defects a
   future regression would reintroduce, yet assert them as acceptable
   (`db31dd7` message; `test_phase3_dynamics.cpp:238-241`;
   `test_baseline_capture.cpp:209-213`; `test_processor_pipeline.cpp:1485-1512`).

**Highest-value additions, in priority order**

1. A concurrency test that runs `processBlock` on a dedicated thread while the
   inference thread is live (no pause), under TSan, with the feature queue under
   sustained load.
2. A full-processor cross-block-size fingerprint test that includes lock onset,
   transition start, live mirror and at least one 128-sample run.
3. Per-attack guitar-to-bass pitch correspondence in the live mirror, plus a
   note-length assertion.
4. Note-off length assertions for kick, snare, closed hat, toms and side-stick
   in the deferred cross-block path (via `MidiProbe::noteOffs`).
5. Direct `EnergyAnalyser::getOnsetRmsEnergy()` tests (and delete the test-local
   `RmsWindow` copies), including onset-time accuracy against `makeClickBuffer`.
6. Register the editor, stability and perf tests under labels CI actually runs,
   and remove the latency benchmark from the `unit` label.

---

## Appendix A — Evidence commands and outputs

```
$ ./build/MetalAccompanimentTests
All tests passed (87984 assertions in 269 test cases)

$ ./build/MetalAccompanimentIntegrationTests
All tests passed (1251 assertions in 79 test cases)

$ grep -rn "mayfail" tests/ ; grep -rn "shouldfail" tests/ ; grep -rn '\[\.\]' tests/
(no matches)

$ ./build/MetalAccompanimentIntegrationTests --list-tests --reporter xml   # 79 TestCase nodes
$ ./build/MetalAccompanimentTests             --list-tests --reporter xml   # 269 TestCase nodes
```

Integration CI selection, computed by intersecting each `add_test` tag filter
with the XML test list: `[structure][shadow]`→1, `[integration][pipeline]`→55,
`[e2e][silent]`→1, `[e2e][bpm]`→2, `[e2e][transitions]`→6, `[e2e][groove]`→1,
`[stability][long]`→1, `[perf]`→1, `[t9.2]`→2, `[phase9]`→7; union → 75 of 79,
leaving the 3 editor cases and the baseline-capture case unselected.

## Appendix B — Files read for this audit

All 35 `tests/*.cpp`, `tests/fixtures/MidiProbe.h`, `tests/fixtures/README.md`,
`tests/expected_structure_shadow_pattern_transitions.txt`, `CMakeLists.txt`,
`.github/workflows/ci.yml`, `.github/workflows/release.yml`,
`.github/workflows/pages.yml`, `scripts/check_onnx_audio_thread.sh`, the
generated `build/CTestTestfile.cmake` and per-case registration files, and the
relevant `src/` headers/sources for `AccompanimentProcessor`, `PhraseLearner`,
`EnergyAnalyser`, `PlaybackGate`, `PatternPlayer`, `GrooveRenderer`,
`MetalGrooveInference`, `StructureTagger`, and both CI fix commits `5d5f410`,
`e463847`, `db31dd7`.

*No file under `src/` or `tests/` was modified.*
