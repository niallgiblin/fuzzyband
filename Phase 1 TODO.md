# Phase 1 — TODO (cross off as completed)

Derived from `ROADMAP_PHASE_1.md`. Update this file as work completes.

## Success criteria (Phase 1 prototype)

- [ ] Plugin loads in Reaper as VST3 (macOS)
- [ ] Guitar input at 256-sample buffer without audible glitching
- [ ] Tempo within ±5 BPM of actual tempo
- [ ] Drum MIDI on correct beat positions
- [ ] Bass root within ±1 semitone of fundamental (basic tracking / fixed root where noted)
- [ ] Pattern switching within ~200ms of energy change
- [ ] End-to-end audio→MIDI latency under 30ms
- [ ] 20-minute session stable (no crashes / xruns)

---

## Milestone 1 — Project scaffold

- [x] JUCE 8 + CMakeLists with VST3 and AU targets *(JUCE 8 required for macOS 15+ / juceaide build)*
- [ ] Confirm plugin loads in Reaper and appears in scanner *(user verification)*
- [x] `AudioProcessorValueTreeState` skeleton for future parameter UI
- [x] `.gitignore` for JUCE/CMake artifacts
- [x] `README.md` stub + roadmap reference
- [x] CI: build-only GitHub Actions (macOS)

**Exit:** Plugin loads, blank UI, processes silence without crashing *(build verified locally)*

---

## Milestone 2 — Onset + tempo

- [x] `OnsetDetector` (spectral flux, FFT, peaks, IOI median, BPM clamp 80–220)
- [x] `getCurrentBpm()` thread-safe; onset flag per block
- [x] Unit test: synthetic click @ 160 BPM → converges within 4 beats

---

## Milestone 3 — Energy + structure

- [x] `EnergyAnalyser` (RMS 100ms, spectral centroid, high-frequency flux)
- [x] `StructureTagger` (SILENT / VERSE / CHORUS / BREAKDOWN, 2s hysteresis)
- [x] Debug UI: state label + BPM

---

## Milestone 4 — MIDI pattern library

- [x] `MidiPattern` / `MidiEvent` design; 7 pattern indices (0–6 per rules)
- [x] Drum + bass patterns authored as static data (no file I/O in plugin)
- [x] Offline MIDI export utility to audition patterns in a DAW
- [x] Debug control to exercise patterns from UI

---

## Milestone 5 — MIDI output + Reaper routing

- [x] `PatternPlayer` (beat clock, `MidiBuffer`, loop, humanisation, ch 10 / ch 2)
- [ ] Reaper session: Superior Drummer / EZdrummer on MIDI track *(user setup — see README)*

**Exit:** Plugin drives drum VSTi in Reaper in time with transport *(user verification)*

---

## Milestone 6 — Feature → pattern inference

- [x] `IInference` + `RuleBasedInference` (same interface as future ONNX)
- [x] Background inference ~50Hz; `std::atomic<int>` pattern index; `ReaderWriterQueue` features

---

## Milestone 7 — Integration + stability

- [x] 10-minute jam profile / tune thresholds *(see `07-05-JAM-NOTES.md` + `StructureTagger.h`)*
- [ ] CPU &lt; 15% M-series @ 256 samples *(STAB-02 **fail** 2026-04-16 — see `07-05-CPU-PROFILE.md`; optimize + re-profile)*
- [ ] TSan / edge cases: bypass, SR change, buffer change, transport *(partial — prepare resets)*
- [ ] Demo video *(optional)*

---

## Milestone 8 — Docs + Phase 2 handoff

- [x] `ARCHITECTURE.md` present (threading + boundaries)
- [ ] Doxygen on all public interfaces *(incremental)*
- [ ] `CONTRIBUTING.md` *(optional this sprint)*
- [ ] Tag `v0.1.0-phase1` + Phase 2 GitHub issues *(when ready)*

---

## Notes for Reaper

1. Insert **Metal Accompaniment** on the guitar track (audio FX).
2. Create a **new track** with your drum VSTi; set **MIDI input** from the track that receives the plugin’s MIDI (routing varies: use **Record: output (MIDI)** or JS: MIDI routing as needed).
3. In Reaper, enable **MIDI output** from the plugin track to the instrument track (Options → Preferences → MIDI if needed).
