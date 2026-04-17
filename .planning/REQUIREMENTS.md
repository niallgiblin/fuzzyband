# Requirements — Metal Accompaniment

> **Milestone:** v0.2.0 — Phase 2: ML + Generative  
> **Created:** 2026-04-17  
> Strategy context: `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`

---

## v0.2 Requirements (Phase 2 — ML + Generative)

### Data & training strategy (DATA)

- [x] **DATA-01**: Dataset/license audit memo with go/no-go for primary symbolic drum training path (e.g. Groove/GMD vs E-GMD vs Lakh), per strategy doc — `docs/DATASET_AUDIT.md` (Phase 9)
- [x] **DATA-02**: Tokenization decision recorded (beat-grid vs event) for symbolic models and training — `docs/TOKENIZATION.md` (Phase 9)
- [x] **DATA-03**: Reproducible data-prep stub: MIDI → model input format, aligned with chosen tokenization — `training/prep_midi.py` (Phase 9)

### ONNX & inference runtime (ONNX)

- [x] **ONNX-01**: ONNX Runtime integrated in CMake (`MA_ENABLE_ONNX` or equivalent; `ONNXRUNTIME_ROOT` documented)
- [x] **ONNX-02**: `OnnxInference` (or equivalent) implements `IInference`; falls back to `RuleBasedInference` when model missing or load fails
- [x] **ONNX-03**: Inference remains on background thread (~50 Hz target); audio thread never blocks on model I/O

### Pitch & harmony (PITCH)

- [x] **PITCH-01**: Real-time monophonic pitch estimate (YIN or CREPE-via-ONNX or agreed alternative) feeding the feature pipeline
- [x] **PITCH-02**: Chord/root or harmony hints sufficient for bass routing (minimal: stable root per window)
- [x] **PITCH-03**: Bass MIDI uses detected root where applicable (no longer fixed E2-only when pitch path is active)
- [x] **PITCH-04**: Tests on synthetic pitch signals (regression thresholds documented)

### ML structure (STRUC)

- [ ] **STRUC-01**: ML structure classifier or head replaces or shadows threshold `StructureTagger` with eval metrics vs. labels/gold
- [ ] **STRUC-02**: Fallback to rule-based structure when model unavailable or confidence low
- [ ] **STRUC-03**: Pattern transitions remain bar-quantized and consistent with `PatternPlayer` semantics

### Generative bass (GBASS)

- [ ] **GBASS-01**: Small transformer (or agreed architecture) trained; exports to ONNX or approved runtime consumed by plugin
- [ ] **GBASS-02**: Generated bass constrained to valid MIDI on bass channel; integrates with existing clock/boundary semantics
- [ ] **GBASS-03**: CPU/latency budget documented; safe degradation (e.g. revert to static patterns)

### Plugin UI (PUI)

- [ ] **PUI-01**: APVTS parameters for genre, intensity, and pattern variation (or agreed names) wired to inference/policy
- [ ] **PUI-02**: Editor UI exposes controls; state persists across sessions
- [ ] **PUI-03**: Documentation of debug vs. user-facing controls

### Python training (PYTR)

- [ ] **PYTR-01**: Python project with locked dependencies (`requirements.txt`, `uv.lock`, or equivalent) reproducible on macOS
- [ ] **PYTR-02**: Metal MIDI dataset pipeline: ingest + at least one training run with logged metrics
- [ ] **PYTR-03**: Export path from training to ONNX/checkpoint used by ONNX inference path

### Cloud & model storage (CLOUD)

- [ ] **CLOUD-01**: Terraform provisions cloud storage for versioned model artifacts
- [ ] **CLOUD-02**: Runbook for promoting a model version from training to plugin consumption (paths, checksums, or tags)

---

## Future (after v0.2)

- Real-time cloud inference service (latency, auth, rate limits) if not in this milestone
- Windows VST3/AU plugin builds
- Per-user style embeddings / LoRA persistence

---

## Out of scope (v0.2)

- Waveform audio generation (MusicGen-class) — not MIDI; incompatible with `IInference` contract
- Full replacement of drum pattern library without hybrid/rank-select safety path unless explicitly accepted

---

## Traceability

| Requirement | Phase | Notes |
|-------------|-------|--------|
| DATA-01–03 | 9 | Closes backlog 999.1 themes; pairs with PYTR |
| ONNX-01–03 | 10 | Foundation for ML models in plugin |
| PITCH-01–04 | 11 | Unlocks adaptive bass root |
| STRUC-01–03 | 12 | ML structure vs. thresholds |
| GBASS-01–03 | 13 | Generative bass |
| PUI-01–03 | 14 | User-facing controls |
| PYTR-01–03 | 15 | Training + export loop |
| CLOUD-01–02 | 16 | Model versioning infra |
