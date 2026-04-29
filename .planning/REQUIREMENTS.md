# Requirements — v0.4.0 ML Playability & Simplification

> Milestone goal: Make the plugin musical and self-contained — better-trained ML drives all pattern decisions from a richer dataset, the UI gets out of the way, and the structure model is simplified to energy states.

## Active Requirements

### C++ Type Foundation

- [ ] **TYPE-01**: StructureState enum reduced to SILENT/SOFT/LOUD (3 values); all C++ use sites updated and compile cleanly
- [ ] **TYPE-02**: FeatureVector `policyGenreIndex` field removed; 3-state encoding propagated through all ONNX packing sites

### ONNX Contract

- [x] **ONNX-04**: `docs/ONNX_IO.md` updated for 3-class state input + `Y_struct [1,3]` output; stub generator (`build_minimal_structure_onnx.py`) regenerated; CI passes
- [x] **ONNX-05**: `Y_bass` contract expanded from `[1,4]` (single note) to `[1,32]` piano-roll (16-step bar slice: pitch offset + velocity per 16th note); `validate_onnx_contract.py --bass` updated

### C++ Inference + Plugin

- [ ] **INF-01**: `OnnxInference` updated for 3-class state packing + `[1,32]` bass output; explicit shape assertions at ONNX model load time (no silent `catch(...)` fallback on shape mismatch — fallback only on model-absent, not on shape error)
- [ ] **INF-02**: ML rejection signal implemented: `patternRejectionCount` atomic on processor written by message thread; `excludeIndex` parameter added to `IInference::selectPattern`; rejection signal bypasses 2-bar hold guard for one cycle
- [ ] **UI-01**: Genre APVTS param + editor widget + `PolicyPatternMapper` removed atomically across all 4 affected files; old session XML round-trip test passes (no crash on load of pre-v0.4.0 session)
- [ ] **UI-02**: Variation APVTS param + slider removed; `structureBlend` retained; "Next Pattern" button wired to rejection signal (`patternRejectionCount`); preview behaviour resolved in planning

### Training Data

- [ ] **DATA-07**: `download_lakh.py` script downloads `lmd_matched` subset (~2GB); `filter_lakh.py` filters by MIDI channel-10 drum track presence + BPM range [80, 220] (not genre metadata tags); filtered file count logged and checked against minimum threshold
- [ ] **DATA-08**: `build_lakh_dataset.py` extracts feature proxies in GMD-compatible tensor format; domain compatibility check comparing Lakh vs GMD feature distributions passes before merge is permitted
- [ ] **DATA-09**: `merge_datasets.py` merges GMD + Lakh tensors with joint quantile label recomputation; source-stratified train/val split produced (GMD-only validation fold preserved for cross-corpus comparison)

### Model Retraining

- [ ] **PMODEL-04**: PatternNet retrained on merged GMD+Lakh dataset with 3-class state encoding; macro-F1 early-stop gate satisfied; `validate_onnx_contract.py --pattern` passes
- [ ] **BMODEL-03**: BassNet retrained with `[1,32]` piano-roll output using metal bass interval vocabulary (P5, m3, b5/tritone, chromatic approach notes); `validate_onnx_contract.py --bass` passes
- [ ] **STRUC-04**: Structure classifier retrained with 3-class SILENT/SOFT/LOUD output; `validate_onnx_contract.py --structure` passes
- [ ] **PVAL-01**: Reaper jam verification: ≥3 distinct drum patterns audible in a 5-minute session; bass lines demonstrate within-bar variation; no silent ML fallback detectable (pattern display shows ML-chosen index, not rule-based default)

## Future Requirements (deferred)

- lmd_full (170K files) corpus expansion — deferred; lmd_matched sufficient for v0.4.0
- Autoregressive/LSTM bass model — deferred; ONNX dynamic-sequence export is unreliable; revisit if fixed-shape piano-roll proves limiting
- Cloud model promotion pipeline update for new ONNX shapes — deferred to v0.5.0
- Windows plugin build — deferred; macOS first

## Out of Scope

- ONNX Runtime version upgrade — current 1.20.1 supports opset 17 piano-roll shapes; no reason to change
- `structureBlend` slider removal — retained as ML/rule weighting knob for future use
- Windows build target — macOS first by project constraint
- Waveform audio generation — outside product contract

## Traceability

| REQ-ID | Phase | Status |
|--------|-------|--------|
| TYPE-01 | Phase 21 | Complete |
| TYPE-02 | Phase 21 | Complete |
| ONNX-04 | Phase 22 | Complete |
| ONNX-05 | Phase 22 | Complete |
| INF-01 | Phase 23 | Pending |
| INF-02 | Phase 23 | Pending |
| UI-01 | Phase 24 | Pending |
| UI-02 | Phase 24 | Pending |
| DATA-07 | Phase 25 | Pending |
| DATA-08 | Phase 25 | Pending |
| DATA-09 | Phase 25 | Pending |
| PMODEL-04 | Phase 26 | Verified 2026-04-29 |
| BMODEL-03 | Phase 26 | Verified 2026-04-29 |
| STRUC-04 | Phase 26 | Verified 2026-04-29 |
| PVAL-01 | Phase 26 | Verified 2026-04-29 |

---
*Created: 2026-04-28 — v0.4.0 milestone (`/gsd-new-milestone`)*
*Traceability updated: 2026-04-28 — roadmap phases 21–26 assigned (`/gsd-roadmap`)*
