# Codebase Concerns

**Analysis Date:** 2026-04-16

## Phase Blockers (Critical Path Items)

**Missing Reaper Integration Verification:**
- Issue: Phase 1 TODO lists "Confirm plugin loads in Reaper and appears in scanner" as incomplete
- Files: `README.md` (lines 29-50), `/Phase 1 TODO.md` (line 11)
- Status: Build verified locally, but user verification of plugin loading in Reaper pending
- Impact: Cannot confirm Phase 1 success criteria without this
- Fix approach: Have user install VST3 to `~/Library/Audio/Plug-Ins/VST3/` and re-scan. Troubleshooting guide provided in README.

**20-Minute Stability Session Incomplete:**
- Issue: Phase 1 TODO Milestone 7 incomplete ("10-minute jam profile / tune thresholds" — ongoing)
- Files: `/Phase 1 TODO.md` (line 74)
- Impact: Cannot guarantee stability criteria; potential xruns or crashes under sustained load unknown
- Fix approach: Schedule 20-minute test session with profiling; use ThreadSanitizer to catch race conditions

**Threshold Tuning Not Complete:**
- Issue: Structure and onset thresholds set to default values; not verified against real guitar input
- Files: `src/analysis/StructureTagger.h` (lines 27-30), `src/analysis/OnsetDetector.h` (line 52)
- Values: `kSilentRms = 0.05f`, `kBreakdownCentroidHz = 1200.0f`, `kVerseCentroidHz = 2400.0f`, `fluxThreshold = 0.35f`
- Risk: May misclassify guitar states; onset detector may fire on noise
- Fix approach: Jam session with user to tune by ear; capture reference audio for benchmarking

---

## Tech Debt

**OnnxInference Stub — Phase 2 Dependency:**
- Issue: `OnnxInference::selectPattern()` returns hardcoded `0` (silent pattern) and does nothing
- Files: `src/inference/OnnxInference.cpp` (lines 8-11)
- Current state: Marked with TODO(Phase 2) comment; compiles but unused
- Impact: Phase 2 cannot begin until ML model loading is implemented
- Blocking: Cannot swap inference implementation without completing this
- Fix approach: In Phase 2, implement ONNX Runtime session loading from BinaryData; match `IInference` interface exactly

**Fixed Bass Root — Hardcoded E2:**
- Issue: Bass patterns use fixed MIDI note 40 (E2) placeholder
- Files: `src/midi/MidiPatternLibrary.cpp` (line 11 comment: `kBassRoot = 40; // E2 placeholder`)
- Impact: Cannot detect actual guitar root note; all songs sound like E2 bass
- Dependency: Requires pitch tracking (Phase 2 feature) to enable dynamic root detection
- Fix approach: Add `rootNote` field to `FeatureVector`; Phase 2 adds pitch detector; patch patterns dynamically in `PatternPlayer::emitEventsForRange()`

**Program Names Not Implemented:**
- Issue: `getProgramName()` returns empty string; `changeProgramName()` is stub
- Files: `src/AccompanimentProcessor.h` (lines 41-42)
- Impact: Host preset system unavailable; user cannot save/recall settings
- Fix approach: Implement using `AudioProcessorValueTreeState::copyState()` and `replaceState()`

---

## Threading Concerns

**Inference Queue Overflow Risk:**
- Issue: Feature queue is fixed-size (4096 elements); if producer outpaces consumer, features are silently dropped
- Files: `src/AccompanimentProcessor.h` (line 70), `src/AccompanimentProcessor.cpp` (line 131)
- Queue behavior: `try_enqueue()` returns bool; code ignores result with `(void)` cast
- Risk: If background thread stalls (e.g., ONNX Runtime inference slow), audio thread may keep dropping features silently
- Detection: No logging; user won't know features are lost
- Fix approach: Log dropped features in debug builds; add metrics to UI; consider dynamic queue resizing or monitoring

**Background Thread Sleep Timing Not Adaptive:**
- Issue: Inference thread sleeps for fixed 20ms between iterations, regardless of inference cost
- Files: `src/AccompanimentProcessor.cpp` (line 95)
- Impact: At ~50Hz poll rate, may miss rapid energy changes if pattern selection is slow; wastes CPU if inference finishes early
- Fix approach: Measure inference duration; adaptive sleep = max(20ms - measured_time, 0ms); cap at 50ms worst case

**Memory Order Relaxed Throughout:**
- Issue: All `std::atomic` reads/writes use `memory_order_relaxed` (no happens-before guarantees)
- Files: `src/AccompanimentProcessor.h` (lines 71-75), `src/AccompanimentProcessor.cpp` (multiple lines)
- Rationale stated in ARCHITECTURE.md: single-producer single-consumer + lock-free queue sufficient
- Risk: May create subtle timing bugs if code grows; not future-safe for adding more synchronization
- Fix approach: Document this constraint clearly; consider acquiring `memory_order_acquire/release` for safety margin if Phase 2 adds complexity

---

## Known Fragile Areas

**StructureTagger Hysteresis Logic:**
- Issue: State transition blocked for 2 seconds even if transitioning OUT OF SILENT is desired
- Files: `src/analysis/StructureTagger.cpp` (line 34)
- Bug: Condition is `timeInStateSec < kMinStateHoldSec && currentState != StructureState::SILENT`
- Symptom: If user goes quiet for 0.5s, then plays heavy—plugin waits 2s to acknowledge (stuck in previous state)
- Fix approach: Separate hysteresis; use stricter threshold (1s) only when entering SILENT to avoid flickering on natural silence

**Pattern Transition at Bar Boundaries:**
- Issue: `PatternPlayer` quantises transitions to bar boundaries; implementation may cause brief silence if pattern index changes mid-bar
- Files: `src/midi/PatternPlayer.cpp` (lines 95-145)
- Risk: If inference selects different pattern mid-bar, MIDI output pauses until next bar (audible pop/gap)
- Rationale in ARCHITECTURE.md: prevents mid-bar glitches; trade-off is acceptable
- Concern: Not tested under rapid pattern changes; may need additional note-off flush logic

**Spectral Centroid Threshold Boundaries:**
- Issue: Centroid thresholds (1200 Hz and 2400 Hz) may not generalize across guitar types
- Files: `src/analysis/StructureTagger.h` (lines 28-29)
- Risk: Drop-tuned guitars, 7-strings, or clean tones may trigger wrong state classification
- Fix approach: After tuning session, allow user-configurable centroid thresholds; document typical ranges

---

## Performance Unknowns

**CPU Usage Under 256-Sample Buffer:**
- Issue: Phase 1 TODO lists "CPU < 15% M-series @ 256 samples" as deferred ("profile later")
- Files: `/Phase 1 TODO.md` (line 75)
- Unknown: FFT cost at 256-sample buffer; onset/energy analysis overhead
- Risk: May exceed target on older hardware; glitching if CPU saturated
- Fix approach: Run `Instruments.app` CPU profiler (macOS); measure FFT + feature extraction time; profile ONNX Runtime when Phase 2 lands

**Memory Allocation Patterns:**
- Issue: OnsetDetector, EnergyAnalyser, StructureTagger allocate vectors in `prepare()`, but never deallocate
- Files: `src/analysis/OnsetDetector.cpp` (lines 17-37), `src/analysis/EnergyAnalyser.cpp` (lines 22-30)
- Detail: Uses `std::vector::assign()` which may reallocate
- Risk: If `prepareToPlay()` called multiple times (buffer size changes, sample rate changes), vectors grow; could hit memory limits on embedded hardware
- Fix approach: Reserve exact size in constructor; call `reserve()` in `prepare()` only; test with sample rate/buffer changes

---

## Missing Critical Features

**No Audio Input Validation:**
- Issue: `processBlock()` assumes input pointer is non-null after checking but doesn't validate buffer integrity
- Files: `src/AccompanimentProcessor.cpp` (lines 109-111)
- Risk: Malformed input (NaN, inf) propagates to FFT and spectral analysis; FFT behavior undefined with inf
- Fix approach: Add `juce::FloatVectorOperations::clip()` to input; check for denormals

**No MIDI Channel Validation:**
- Issue: Drum/bass channel numbers (10, 2) are hardcoded; no check that host supports MIDI output
- Files: `src/midi/PatternPlayer.h` (lines 48-49)
- Risk: If host doesn't produce MIDI output, plugin fails silently (no error message)
- Fix approach: Add `producesMidi()` return true check; validate channel count in `prepareToPlay()`

**No Graceful Bypass Handling:**
- Issue: Plugin does not reset state when bypassed; MIDI queue and inference may continue running
- Files: `src/AccompanimentProcessor.cpp` (lacks `setBypass()` override)
- Risk: User bypasses plugin expecting silence; MIDI notes still fire
- Fix approach: Override `isBusesLayoutSupported()` to disallow bypass, or add `setBypass()` to flush MIDI + reset inference state

**No Buffer Size / Sample Rate Persistence:**
- Issue: `prepareToPlay()` kills and restarts inference thread; long resume latency possible
- Files: `src/AccompanimentProcessor.cpp` (lines 56-63)
- Impact: Plugin pause/resume during playback triggers restart overhead
- Fix approach: Pause inference thread instead of killing it; reduce lock/unlock overhead

---

## Test Coverage Gaps

**Integration Testing Missing:**
- What's not tested: Full plugin→Reaper routing with actual drum instrument
- Files affected: All components (`OnsetDetector` through `PatternPlayer`)
- Risk: Plugin may compile and pass unit tests but fail in DAW (MIDI routing issue, buffer size mismatch, sample rate edge cases)
- Priority: High — blocks Phase 1 completion criteria
- Fix approach: Manual testing checklist in README; consider Reaper scripting for automated DAW integration test

**Edge Cases for Sample Rate / Buffer Size:**
- What's not tested: 44.1kHz, 48kHz, 96kHz, 192kHz sample rates; buffer sizes 32, 64, 128, 256, 512, 1024 samples
- Files affected: `OnsetDetector` (FFT size assumptions), `EnergyAnalyser` (RMS window), `PatternPlayer` (beat clock calculation)
- Risk: FFT may allocate wrong size or cause artifacts at unusual rates; beat clock arithmetic may overflow or lose precision
- Priority: Medium — covers real-world DAW configurations
- Fix approach: Parameterize FFT order and RMS window in `prepare()`; add test matrix for common rates/sizes

**Threading Edge Cases:**
- What's not tested: ThreadSanitizer (TSan) run; rapid `prepareToPlay()` calls; plugin unload during inference
- Files affected: `src/AccompanimentProcessor.cpp` (thread lifecycle)
- Risk: Data races on shutdown; use-after-free if thread accesses deallocated members
- Priority: High — can cause crashes
- Fix approach: Run with ThreadSanitizer in CI; add explicit thread join in destructor; use RAII for inference thread

**Pattern Player Humanization:**
- What's not tested: Humanization values (velocity ±10, timing ±2ms) produce musically acceptable variation
- Files: `src/midi/PatternPlayer.cpp` (lines 36-46)
- Risk: Variation may be imperceptible or too extreme
- Fix approach: Unit test + listen test; measure distribution of velocities/timings

---

## Security Considerations

**No Input Bounds Checking on APVTS Parameters:**
- Risk: If audio processor receives malformed parameter state from plugin host, may crash
- Files: `src/AccompanimentProcessor.cpp` (line 191, `setStateInformation()`)
- Mitigation: JUCE `ValueTree::readFromData()` validates structure, but no semantic bounds check
- Fix approach: Add validation in `setStateInformation()` to reject out-of-range APVTS values

**No Denial-of-Service Mitigation on Feature Queue:**
- Risk: If inference thread blocked, feature queue fills; audio thread stalls trying to enqueue
- Files: `src/AccompanimentProcessor.cpp` (line 131)
- Current: `try_enqueue()` is non-blocking, so this is low risk, but queue size is fixed
- Recommendation: Monitor queue fill ratio in UI; warn user if inference is backlogged

---

## Documentation Gaps

**Missing API Documentation for IInference:**
- Issue: `IInference` interface lacks Doxygen comments; Phase 2 developer will need to reverse-engineer expectations
- Files: `src/inference/IInference.h`
- Impact: Phase 2 ONNX implementation may not conform to implicit contract (threading, blocking, exceptions)
- Fix approach: Add Doxygen comments documenting thread safety, latency budget, error handling

**FeatureVector Semantics Unclear:**
- Issue: Field ranges not documented; e.g., is `rmsEnergy` [0, 1] normalized or raw RMS value?
- Files: `src/analysis/FeatureVector.h` (lines 146-153)
- Impact: Phase 2 inference model trained on unclear scale; may get garbage input
- Fix approach: Document each field: range, units, normalization, zero-meaning

**MIDI Channel Assignment Not Negotiable:**
- Issue: Channels 10 (drums) and 2 (bass) are hardcoded; no way for user to route bass to channel 1 or drums elsewhere
- Files: `src/midi/PatternPlayer.h` (lines 48-49), README.md (line 63)
- Impact: User must manually configure Reaper routing
- Fix approach: Add APVTS parameters for drum/bass channels; surface in UI or expose as host-controllable

---

## Dependencies at Risk

**moodycamel ReaderWriterQueue — Informal Maintenance:**
- Package: `readerwriterqueue` (cameron314 GitHub, master branch)
- Risk: Master branch used; no version tag; updates may break API
- Current: `GIT_TAG master` in CMakeLists.txt (line 37)
- Impact: Build may break if upstream changes lock-free API
- Mitigation: Code only uses `try_enqueue()` and `try_dequeue()`, stable public interface
- Fix approach: Pin to specific commit hash (e.g., `2efc8e2`) instead of master

**JUCE 8 macOS 15+ Dependency:**
- Issue: JUCE 8 required for macOS 15+ (Sonoma) support; Phase 1 TODO notes this (line 20)
- Files: `/Phase 1 TODO.md` (line 20 checkmark), `CMakeLists.txt` (line 25, `GIT_TAG 8.0.10`)
- Risk: Upgrading to newer macOS drops JUCE 7 support; no migration path for older macOS users
- Mitigation: Document macOS version support; consider CI matrix (12.0, 13.0, 14.0, 15.0)
- Fix approach: Test build on macOS 12 (Intel) to confirm JUCE 8 backward compatible

**ONNX Runtime Phase 2 Dependency:**
- Issue: ONNX Runtime not integrated yet; only stub in place
- Files: `CMakeLists.txt` (lines 62-75, `MA_ENABLE_ONNX` option), `src/inference/OnnxInference.cpp` (TODO comment)
- Risk: Will need to bundle large `.onnxruntime.dylib` with plugin; affects VST3 size and load time
- Fix approach: Phase 2 must address binary size; consider runtime model caching to avoid reload on DAW restart

---

## Platform-Specific Issues

**macOS Architecture-Specific Builds Required:**
- Issue: Plugin must be built separately for Apple Silicon (arm64) and Intel (x86_64); no universal binary in CMakeLists
- Files: `CMakeLists.txt` (no `CMAKE_OSX_ARCHITECTURES` set)
- Risk: User builds on M1 Mac but uses Intel Reaper; plugin won't load
- README mentions this (lines 50) but workaround is confusing
- Fix approach: Build fat binary with `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` in CI and docs

**Windows Support Deferred:**
- Issue: Phase 1 targets macOS only; Windows testing not done
- Files: `CMakeLists.txt` (ONNX Runtime uses dylib; would need .dll for Windows)
- Risk: First Windows user hits unknown compilation or runtime issues
- Fix approach: Phase 2 planning should include Windows CI step; document known gaps in Phase 1

---

## Build and CI Concerns

**GitHub Actions CI Build-Only:**
- Issue: CI runs build but no tests; `ctest` not invoked in CI
- Files: `.github/workflows/` (assumed to exist but not found; verify)
- Risk: Test failures don't block merge; regression undetected
- Fix approach: Add `cmake --build build --target RUN_TESTS` or `ctest --test-dir build` to CI

**Catch2 3.5.2 Test Helpers Not Provided:**
- Issue: Unit tests exist but no test fixtures or factories for common test data
- Files: `tests/test_*.cpp` (4 test files)
- Risk: Tests duplicate setup code; hard to add new tests
- Fix approach: Create `tests/fixtures/` with helper functions (e.g., `synthClickTrain()`, `silenceBuffer()`)

---

## Known Unknowns

**Onset Detector Convergence Speed on Real Guitar:**
- Hypothesis: Converges to accurate BPM within 4 beats on synthetic 160 BPM click (unit test passes)
- Unknown: How fast converges on real distorted metal guitar? Down-picked single notes vs. chord strumming?
- Risk: May take 10+ seconds on noisy or expressive playing; user perceives lag
- Testing needed: Record 10-minute jam; plot BPM estimate over time; measure convergence time on attack transients

**Energy Thresholds for Genre Generalization:**
- Hypothesis: Verse/chorus/breakdown classification works for typical metal (tight, percussive)
- Unknown: Applies to prog metal, black metal, doom metal, acoustic fingerpicking?
- Risk: Thresholds tuned only for one player's tone; fail for studio musicians
- Testing needed: Have 3-5 guitarists jam with plugin; retune thresholds; document assumptions (tone type, pickup type, string gauge)

**MIDI Note Duration Realism:**
- Current: Drum notes hardcoded to 0.2s duration; bass notes to 0.9s
- Unknown: Do these durations feel tight and punchy at all tempos? 80 BPM vs. 220 BPM?
- Risk: Drums may sound sloppy (overlapping notes) or choppy (too short) at tempo extremes
- Fix approach: Make note duration BPM-adaptive; e.g., drum duration = quarter-note / 2

---

## Recommended Phase 1 → Phase 2 Handoff Checklist

Before releasing v0.1.0, confirm:

- [ ] 20-minute jam session stable (no crashes, no xruns)
- [ ] Plugin loads and appears in Reaper plugin scanner
- [ ] MIDI routes to drum track; notes fire in time
- [ ] Tempo estimate within ±5 BPM for at least 5 different tempos (80, 120, 160, 200, 220 BPM)
- [ ] State transitions smooth; no rapid flickering between states
- [ ] CPU usage < 15% M-series @ 256 samples (measured with Instruments)
- [ ] All unit tests pass; CI green
- [ ] ThreadSanitizer run; no data races reported
- [ ] Doxygen generated and validated
- [ ] ARCHITECTURE.md and README up to date

Once complete, tag `v0.1.0-phase1` and create Phase 2 GitHub issues for:
1. OnnxInference implementation (priority 0)
2. Pitch detection + dynamic root note (priority 1)
3. UI parameter controls (priority 2)
4. MIDI channel configuration (priority 3)
5. Windows + Linux builds (priority 4)

---

*Concerns audit: 2026-04-16*
