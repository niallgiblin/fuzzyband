# Requirements — v0.5.0 Rhythmic Coherence

> Milestone goal (see `.planning/ROADMAP.md`): Make the plugin reliably join the player in time — beat tracking that survives clean guitar; unified drum/bass clock on bar boundaries; enriched `FeatureVector` (+5 fields); directional fills; separate Phase 30 ONNX + training alignment when semantics freeze.

## Active requirements (IDs match roadmap Phases 27–30)

### Documentation — Phase 27

- [x] **RHY-DOC-01**: `plugin/README.md` documents insert order (guitar → plugin → FX), failure modes when post-FX, and links rhythmic-coherence research  
- [x] **RHY-DOC-02**: Root or `docs/` entry points link the same workflow for contributors  

### Beat tracker & bass sequencer — Phase 28

- [ ] **RHY-TEMPO-01**: Beat-tracking path primary BPM/phase source (IOI-median retired or explicit fallback + tests)  
- [ ] **RHY-TEMPO-02**: BPM stability within ±5 BPM vs project expectation on controlled tests or golden snippets  
- [ ] **RHY-BASSSEQ-01**: Bar-aligned bass path — regression proves multiple 16th steps fire note-ons per bar  

### Runtime coordination — Phase 29

- [ ] **RHY-SYNC-01**: Drum pattern changes and bass updates align to same bar-boundary policy (documented + tests)  
- [ ] **RHY-FEAT-01**: `FeatureVector` + lock-free payload carry five new fields without audio-thread allocation  
- [ ] **RHY-FILL-01**: At least four fill patterns mapped by section-direction per research  

### ML retrain — Phase 30

- [ ] **RHY-ML-01**: `docs/ONNX_IO.md`, `validate_onnx_contract.py`, exporters match expanded inputs after Phase 29 semantics freeze  
- [ ] **RHY-ML-02**: Models promoted to `assets/`; `MA_ENABLE_ONNX=ON` build; jam checklist vs v0.4 behavior baseline  

### Architecture Deepening — Phase 31

- [ ] **ARCH-01**: `PlaybackGate` extracted to `src/analysis/`; `AccompanimentProcessor` loses 8 gate private fields; unit tests cover phrase-breath/beat-snap directly  
- [ ] **ARCH-02**: `StablePitchTracker` extracted to `src/analysis/`; `AccompanimentProcessor` loses 5 pitch-stability fields (`heldPitchRootMidi`, `heldPitchConfidence`, `pitchHoldValid`, `pitchStableCounterSamples`, `lastStablePitchMidi`; `lastBassUpdateSample` is retained — it is inference-thread bass hold guard state, not pitch stability); unit tests verify pitch-class window and semitone mapping  
- [ ] **ARCH-03**: Shared `pattern_rules.h` in `src/inference/`; duplicated rule logic eliminated from both `RuleBasedInference.cpp` and `OnnxInference.cpp`  
- [ ] **ARCH-04**: `TempoStabiliser` extracted to `src/analysis/`; `AccompanimentProcessor` loses 3 BPM-hysteresis fields; unit tests drive stabilizer with synthetic candidates  

## Deferred / backlog

See roadmap Future/out-of-scope for Windows-only builds and waveform generation — unchanged global constraints.

## Traceability

| REQ-ID       | Phase | Notes                         |
|--------------|-------|-------------------------------|
| RHY-DOC-*    | 27    | `27-01-PLAN.md`, `27-02-PLAN.md` |
| RHY-TEMPO-*  | 28    | `28-01-PLAN.md` |
| RHY-BASSSEQ-01 | 28  | `28-02-PLAN.md` |
| RHY-SYNC-01  | 29    |                               |
| RHY-FEAT-01  | 29    |                               |
| RHY-FILL-01  | 29    |                               |
| RHY-ML-*     | 30    |                               |
| ARCH-01      | 31    |                               |
| ARCH-02      | 31    |                               |
| ARCH-03      | 31    |                               |
| ARCH-04      | 31    |                               |

---
*Created from roadmap at v0.4.0 milestone close — 2026-04-29*
