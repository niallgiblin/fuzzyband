# ROADMAP — Phase 1: Rule-Based MVP

> **Project:** AI Metal Accompaniment Plugin  
> **Goal:** A playable JUCE VST3/AU plugin that listens to a guitarist and triggers
> rhythmically appropriate drum and bass MIDI in real time — no ML required yet.  
> **Target:** Working prototype in 8–10 weeks, solo dev, part-time.

---

## What Phase 1 Is (and Isn't)

**Is:** A rule-based system that detects tempo and energy from the guitar signal and
uses that to select and trigger pre-authored drum and bass MIDI patterns. Fully
playable, low-latency, and genuinely musical for metal.

**Is not:** ML-driven, generative, or capable of understanding melody or harmonic
structure beyond root note detection. That's Phase 2.

The architecture is deliberately designed so that the rule-based inference layer in
Phase 1 is a drop-in replacement for the ML model in Phase 2 — same interface,
same threading model.

---

## Success Criteria

A Phase 1 prototype is done when all of the following are true:

- Plugin loads in Reaper or Ableton as a VST3 (macOS target first)
- Guitar input is processed with no audible glitching at 256-sample buffer
- Tempo is tracked within ±5 BPM of the guitarist's actual tempo
- Drum MIDI fires at the correct beat positions relative to detected tempo
- Bass MIDI root note tracks the detected fundamental within ±1 semitone
- Pattern switching triggers within ~200ms of a detected energy change
- End-to-end audio-to-MIDI latency is under 30ms
- Plugin runs stably for a 20-minute session without crashes or xruns

---

## Milestones

### Milestone 1 — Project scaffold (Week 1–2)

Get a buildable, loadable JUCE plugin with correct CMake structure before writing
any audio logic.

**Tasks:**

- Install JUCE 7 and set up CMakeLists.txt with VST3 and AU targets
- Confirm plugin loads in Reaper and shows up in the plugin scanner
- Add JUCE `AudioProcessorValueTreeState` skeleton for future parameter UI
- Set up Git repo with `.gitignore` for JUCE/CMake artifacts
- Add `README.md` stub and this roadmap file
- Establish a basic CI check (build-only, GitHub Actions, macOS runner)

**Exit check:** Plugin loads, shows a blank UI, processes silence without crashing.

---

### Milestone 2 — Onset detection and tempo tracking (Week 2–3)

Detect when the guitarist is hitting notes and derive a stable tempo estimate.

**Tasks:**

- Implement spectral flux onset detector in `OnsetDetector.h/.cpp`
  - Compute magnitude spectrum per buffer via FFT (use JUCE's `dsp::FFT`)
  - Detect peaks in the flux signal above a configurable threshold
  - Output onset timestamps as sample positions
- Implement a simple inter-onset interval (IOI) tempo tracker
  - Median filter over the last 8 IOIs to smooth tempo estimate
  - Clamp to a metal-realistic range: 80–220 BPM
  - Expose `getCurrentBpm()` with thread-safe read
- Write a unit test that feeds a synthetic click track at 160 BPM and
  asserts the tracker converges within 4 beats

**Exit check:** Plugin prints a stable BPM estimate to the JUCE debug log while
you play a steady down-picked riff.

---

### Milestone 3 — Spectral energy and structure tagging (Week 3–4)

Classify the current guitar state so the pattern selector knows what section it's in.

**Tasks:**

- Implement `EnergyAnalyser.h/.cpp`
  - RMS energy over a 100ms rolling window
  - High-frequency spectral centroid (distinguishes palm mute from open chord)
  - Low-frequency flux (tracks kick-range content to avoid false triggers)
- Implement `StructureTagger.h/.cpp` with a simple threshold-based state machine
  - States: `SILENT`, `VERSE` (low energy), `CHORUS` (high energy), `BREAKDOWN`
  - Hysteresis on transitions to prevent rapid flickering (minimum 2s in a state)
- Add a debug overlay to the plugin UI showing current state label and BPM

**Exit check:** Play a riff that goes quiet → heavy → quiet. The state label in
the UI transitions correctly with no flickering.

---

### Milestone 4 — MIDI pattern library (Week 4–5)

Author the drum and bass patterns that will be triggered by the analysis layer.

**Tasks:**

- Design `MidiPattern` struct: vector of `{note, velocity, beatOffset, duration}`
- Author at minimum 3 drum patterns per structural state (9 patterns total):
  - `VERSE`: moderate groove, closed hi-hat, backbeat snare, single kick
  - `CHORUS`: driving, open hi-hat or ride, heavier snare velocity, double kick runs
  - `BREAKDOWN`: half-time feel, sparse kick, heavy ghost notes on snare
- Author corresponding bass patterns:
  - Root-note whole notes for VERSE
  - Root + fifth movement for CHORUS
  - Syncopated root for BREAKDOWN
- Store patterns as C++ `constexpr` arrays (no file I/O in the plugin)
- Write a MIDI file export utility (offline, not in plugin) to audition patterns
  in a DAW before integrating

**Exit check:** Patterns load correctly and play back at the right tempo when
triggered manually via a debug button in the UI.

---

### Milestone 5 — MIDI output and DAW routing (Week 5–6)

Wire the pattern player into the JUCE `MidiBuffer` and confirm DAW routing works.

**Tasks:**

- Implement `PatternPlayer.h/.cpp`
  - Maintains a beat clock derived from current BPM and sample position
  - Fills `MidiBuffer` with note-on/note-off events at correct sample offsets
  - Handles pattern looping and smooth transitions between patterns
  - Randomises note velocity ±10 and timing ±2ms for humanisation
- Route MIDI output to channel 10 (drums) and channel 2 (bass) separately
- Set up a Reaper session with Superior Drummer / EZdrummer on a MIDI track
  receiving from the plugin
- Confirm drum hits trigger at the correct rhythmic positions

**Exit check:** Plugin drives a drum VSTi in Reaper. You can hear a basic metal
groove fire in time with a click track.

---

### Milestone 6 — Feature-to-pattern inference (Week 6–7)

Connect the analysis outputs to the pattern selector so the system reacts to
the guitarist.

**Tasks:**

- Implement `RuleBasedInference.h/.cpp` with the same interface that
  a future `OnnxInference` class will satisfy:
  ```cpp
  class IInference {
  public:
      virtual int selectPattern(const FeatureVector& f) = 0;
  };
  ```
- Rule logic:
  - If `state == SILENT`: stop all MIDI output, send note-off flush
  - If `state == VERSE`: select drum pattern by BPM band (slow/mid/fast)
  - If `state == CHORUS`: select heavier pattern, enable double kick above 160 BPM
  - If `state == BREAKDOWN`: select half-time pattern
- Run inference on a background thread at 50Hz (see threading model below)
- Pass results to audio thread via `std::atomic<int>`
- Use `moodycamel::ReaderWriterQueue` for the feature queue

**Threading model (lock-free):**
```
Audio thread:   extract features → push to queue → read atomic pattern index
Background:     pop features → run inference → write atomic pattern index
```

**Exit check:** Play into the plugin. When you switch from quiet picking to a
heavy riff, the drum pattern changes within ~200ms.

---

### Milestone 7 — End-to-end integration and stability (Week 7–8)

Full system running together. Find and fix all the rough edges.

**Tasks:**

- Play a full 10-minute metal jam session and record the output
- Profile CPU usage — target under 15% on an M-series Mac at 256-sample buffer
- Fix any threading issues (use ThreadSanitizer if needed)
- Add graceful handling for:
  - Plugin bypassed mid-session
  - Sample rate changes (44.1k / 48k / 96k)
  - Buffer size changes
  - Host transport stop/start
- Tune hysteresis and threshold values by ear
- Record a short demo video of the plugin in use

**Exit check:** 20-minute session, no crashes, no xruns, drum output feels
musically reactive.

---

### Milestone 8 — Documentation and handoff to Phase 2 (Week 9–10)

Leave Phase 1 in a state that makes Phase 2 straightforward to begin.

**Tasks:**

- Write `ARCHITECTURE.md` describing the threading model and component boundaries
- Add Doxygen comments to all public class interfaces
- Document the `IInference` interface and how to swap in `OnnxInference`
- Write `CONTRIBUTING.md` covering build instructions for macOS and Linux
- Tag a `v0.1.0-phase1` release on GitHub
- Open GitHub Issues for all known Phase 2 work:
  - [ ] Replace `RuleBasedInference` with `OnnxInference`
  - [ ] Add pitch/chord detection for root note tracking
  - [ ] Build Python training pipeline
  - [ ] Add plugin UI with parameter controls

---

## Stack Summary

| Layer | Technology |
|---|---|
| Plugin framework | JUCE 7 (VST3 + AU) |
| Build system | CMake 3.22+ |
| Audio analysis | JUCE `dsp::FFT`, custom onset/energy code |
| Inference (Phase 1) | Rule-based C++ (`RuleBasedInference`) |
| Inference (Phase 2) | ONNX Runtime C++ (`OnnxInference`, same interface) |
| Thread safety | `std::atomic`, `moodycamel::ReaderWriterQueue` |
| MIDI output | JUCE `MidiBuffer`, channel 10 (drums), channel 2 (bass) |
| Target DAWs | Reaper (primary), Ableton Live, Logic Pro |
| Target OS | macOS first (Apple Silicon + Intel), Windows later |
| CI | GitHub Actions, build-only macOS runner |

---

## Risk Register

| Risk | Likelihood | Mitigation |
|---|---|---|
| Onset detector fires on noise/silence | Medium | Noise gate threshold + minimum energy floor |
| Tempo tracker unstable on slow riffs | Medium | Clamp IOI range, median filter, fallback to last stable BPM |
| Palm mute misclassified as SILENT | Medium | Spectral centroid distinguishes mute from silence |
| Pattern transitions sound jarring | Low-Medium | Quantise transitions to bar boundaries, not immediate |
| Plugin crashes on buffer size change | Low | Reset all state in `prepareToPlay()`, called on any change |
| Latency too high for musical feel | Low | Background thread inference means audio thread is never blocked |

---

## What Phase 2 Will Add

- Real-time pitch and chord detection (YIN algorithm or CREPE via ONNX)
- ML structure classifier replacing the threshold state machine
- Generative bass line model (small transformer, PyTorch → ONNX)
- Plugin UI with controls for genre, intensity, and pattern variation
- Python training pipeline with metal MIDI dataset
- Terraform-provisioned cloud storage for model versioning
