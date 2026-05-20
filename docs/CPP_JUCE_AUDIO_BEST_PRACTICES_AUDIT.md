# C++ / JUCE / Audio Best Practices Audit

This is a source-grounded quality review of the current codebase. It is written as a learning document first, but it also calls out concrete risks.

## Rating Summary

| Area | Rating | Notes |
|---|---|---|
| Real-time audio thread discipline | Good, with one concurrency caveat | No obvious locks or file I/O in `processBlock()`. Most allocation is in `prepare()`. Generative bass handoff should be hardened. |
| Memory allocation hygiene | Good | DSP vectors and FFTs allocate in `prepareToPlay()` paths. ONNX allocation stays off audio thread. |
| Modern C++ ownership | Good | Uses `std::unique_ptr`, RAII destructors, atomics, by-value subsystem ownership. |
| JUCE plugin practices | Good | APVTS parameters, `ScopedNoDenormals`, bus layout check, bypass all-notes-off, state serialization. |
| Audio UX | Improving, still tunable | Bar-boundary pattern changes and playback gate are strong. Diagnostics and capture are useful. Some old docs are stale. |
| Security / robustness | Moderate-good | Input is sanitized, model contracts are checked. File capture is bounded. Main concern is unsynchronized plain data crossing threads. |
| Modularity | Moderate-good | Many focused modules exist, but processor and pattern player are still bulky. |

## What "Real-Time Safe" Means Here

The DAW calls `processBlock()` repeatedly with tight deadlines. In that function, avoid:

- heap allocation,
- locks and mutex waits,
- disk or network I/O,
- slow logging,
- waiting on other threads,
- unpredictable work proportional to user-controlled large data.

This project mostly follows that rule.

## Memory Allocation Review

### Allocations That Are Correctly Outside The Audio Loop

The analysis modules allocate buffers in `prepare()`:

- `OnsetDetector` creates its FFT and resizes vectors in `prepare()` (`src/analysis/OnsetDetector.cpp:9`).
- `EnergyAnalyser` creates its FFT, spectral buffers, RMS window, and FIFO in `prepare()` (`src/analysis/EnergyAnalyser.cpp:4`).
- `PitchEstimator` allocates YIN buffers in `prepare()` (`src/analysis/PitchEstimator.cpp`).
- `BeatTracker` allocates its flux ring and scratch window in `prepare()` (`src/analysis/BeatTracker.cpp`).

This is the expected JUCE pattern: allocate when the host tells you sample rate and block size, then reuse memory during `processBlock()`.

### Allocations That Are Safe Because They Are Off The Audio Thread

ONNX inference constructs tensor wrappers and output containers around each run (`src/inference/OnnxInference.cpp:129`, `src/inference/OnnxBassInference.cpp:125`). That is acceptable because `selectPattern()` and `propose()` are called by the inference thread, not the audio thread.

Feature capture opens and writes files in its writer thread (`src/capture/FeatureCapture.cpp:175`), not the audio thread.

### Watch Item: JUCE `MidiBuffer::addEvent`

The audio thread calls `midi.addEvent()` in `PatternPlayer`. JUCE hosts supply the `MidiBuffer`, and this is standard plugin code. Still, when generating many events, keep event counts bounded. This project does that through fixed pattern data and fixed 16-step bass output.

## Threading and Synchronization Review

### Good: Queue For Audio-To-Inference

`featureQueue` is a bounded SPSC queue (`src/AccompanimentProcessor.h:122`). The audio thread uses `try_enqueue()` and ignores failure (`src/AccompanimentProcessor.cpp:503`). This is the right failure mode for live audio.

### Good: Atomics For Simple Cross-Thread Values

The code uses atomics for:

- pattern index,
- display values,
- inference run/pause flags,
- capture status,
- debug preview/rejection signal.

This is appropriate for single scalar values.

### Watch Item: Mutex Exists, But Not On Audio Thread

`inferenceDrainMutex` protects test-time synchronous drains against the background inference thread (`src/AccompanimentProcessor.h:147`). It is locked in `inferenceLoop()` and `flushBackgroundInferenceForTests()` (`src/AccompanimentProcessor.cpp:378`, `src/AccompanimentProcessor.cpp:393`).

The audio thread does not lock it. That is okay.

### High-Priority Risk: Plain Arrays Cross Threads

Generative bass step data is written by the inference thread and read by the audio thread:

- fields: `genBassPitchOffsets`, `genBassVelocities`, `genBassRootMidi` (`src/AccompanimentProcessor.h:129`)
- ready flag: `genBassStepsReady` (`src/AccompanimentProcessor.h:132`)
- read path: `processBlock()` copies them into `PatternPlayer` (`src/AccompanimentProcessor.cpp:519`)

The release/acquire flag improves ordering, but the plain arrays and root are still non-atomic data accessed by two threads. Under strict C++ rules, this can be a data race.

Recommended fix:

- Replace the plain-array handoff with `moodycamel::ReaderWriterQueue<BassStepFrame>` where `BassStepFrame` contains `std::array<float, 16> pitchOffsets`, `std::array<float, 16> velocities`, and `float rootMidi`.
- Or use two fixed buffers plus an atomic published index, where the writer only writes the inactive buffer and the reader copies the published buffer.

This would keep the audio thread lock-free and make the ownership story easier to reason about.

## JUCE Practices Review

### Good: APVTS Parameters

Parameters are created in one layout (`src/AccompanimentProcessor.cpp:73`). The UI uses APVTS attachments (`src/AccompanimentEditor.cpp:73`). This is a good JUCE pattern because it lets the host automate and save parameters.

### Good: State Save / Restore

The processor serializes APVTS state with `copyState().writeToStream()` and restores with `ValueTree::readFromData()` (`src/AccompanimentProcessor.cpp:585`).

One oddity: `getStateInformation()` calls `setValueNotifyingHost()` on every parameter before serialization (`src/AccompanimentProcessor.cpp:590`). The comment explains the intent. It is worth keeping an eye on because host notification during state save can surprise some hosts. If tests or host behavior ever become strange, revisit this.

### Good: Bypass Handling

`processBlockBypassed()` clears MIDI, emits all-notes-off for channels 1-16, resets timing state, and passes dry audio through (`src/AccompanimentProcessor.cpp:604`). That protects users from stuck notes during bypass.

### Good: Bus Layout

The plugin supports mono input to stereo output (`src/AccompanimentProcessor.cpp:133`). This matches the guitar-in, stereo-through mental model.

## Audio Programming Review

### Good: Denormal Protection and Input Sanitization

`ScopedNoDenormals` is used at the top of `processBlock()` (`src/AccompanimentProcessor.cpp:404`). Non-finite samples are replaced with zero before clipping (`src/AccompanimentProcessor.cpp:416`). This prevents NaNs from poisoning downstream DSP and protects user experience.

### Good: Silence and Bypass Note Cleanup

When `PatternPlayer` enters silence, it sends all-notes-off once (`src/midi/PatternPlayer.cpp:379`). Bypass does the same at the processor level. This is exactly the sort of detail that prevents stuck MIDI notes.

### Good: Musical Scheduling

`PatternPlayer` defers pattern changes until a bar boundary (`src/midi/PatternPlayer.cpp:429`). This is musically better than immediately swapping grooves.

### Watch Item: Host Sample Position Is Ignored

`PatternPlayer::process()` receives `hostSamplePosition` but immediately casts it unused (`src/midi/PatternPlayer.cpp:368`). The plugin instead uses its own `sampleCounter` and `beatPosition`.

That is not necessarily wrong because the plugin tracks guitarist-derived tempo rather than host tempo. But the parameter name can mislead future readers. If host transport sync is added later, this is the place to start.

## ONNX Runtime Review

### Good: Optional Build

ONNX is behind `MA_ENABLE_ONNX`. Default builds use rules. This keeps the baseline plugin easier to build and debug.

### Good: Contract Checks

Pattern and bass ONNX classes validate input/output tensor shape and type after loading (`src/inference/OnnxInference.cpp:54`, `src/inference/OnnxBassInference.cpp:53`). That is a strong safety choice.

### Watch Item: Runtime Errors Use Silent Fallback

ONNX `Run()` exceptions fall back to rules or invalid proposals. That protects the live plugin from crashing, but it can hide model problems from users. A diagnostic counter or UI state for "ONNX run errors" would make failures easier to understand.

### Watch Item: `fprintf(stderr)` In Plugin Code

Some ONNX classes print load/run errors to stderr. This is off the audio thread, so it is not a real-time violation. In plugin hosts, stderr is often invisible, so user-facing diagnostics would be more useful.

## Security and Robustness Review

### Input Audio

The plugin does not trust input samples. It zeroes NaN/Inf and clips amplitude. Good.

### Filesystem Writes

Feature capture writes to user documents, app data, temp, or `/tmp` fallback (`src/capture/FeatureCapture.cpp:80`). It creates a bounded JSONL file:

- max rows: 30000 (`src/capture/FeatureCapture.h`)
- max seconds: 600
- bounded queue: 4096 rows
- writer thread flushes periodically

The code escapes JSON strings. Good.

Security/privacy note: feature capture does not record raw audio, but it does record derived musical features and pattern labels. The UI should continue making capture explicit and visible.

### Model Assets

Bundled ONNX models are read from JUCE BinaryData when ONNX is enabled. Runtime loading does not open arbitrary user paths. This is safer than loading user-specified model paths inside the plugin.

## User Experience Risks

### Stale Documentation

`docs/PLUGIN_GUIDE.md` contains older "current broken behavior" narrative about playback gate logic that has since been refactored into `PlaybackGate`. That can confuse you while learning and can confuse future contributors.

Recommendation: either update that section or mark it as historical design context.

### Feature Capture Status

The UI shows capture on/off, dropped rows, and file name. That is good. If capture start fails because no directory can be created, the toggle may not clearly explain why. Consider a visible failure state if this becomes an issue.

### Rule Path Cannot Select Breakdown

Pattern index 6 exists but the rule path only returns 0-5 (`src/inference/pattern_rules.h`). If users expect breakdowns without ONNX, they may never hear them except through preview. This is more product behavior than code safety, but it matters.

## Priority Fix List

1. Harden the generative bass step handoff into a queue or double-buffered frame.
2. Update stale `docs/PLUGIN_GUIDE.md` playback-gate sections or label them historical.
3. Consider adding ONNX run/load error counters visible in the editor.
4. Consider splitting `AccompanimentProcessor::drainFeatureQueueAndRunInference()` into an inference coordinator object.
5. Consider splitting `PatternPlayer` bass rendering into a small helper class once behavior stabilizes.

