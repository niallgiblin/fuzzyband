# Changelog

All notable changes to this project are documented here. For architecture and threading, see [`ARCHITECTURE.md`](ARCHITECTURE.md). Milestone/phase status: [`.planning/STATE.md`](.planning/STATE.md), [`.planning/ROADMAP.md`](.planning/ROADMAP.md).

## [0.5.5] — v0.6.0 ML Correctness & Evaluation (in progress)

**Current milestone:** v0.6.0 (Phases 32–36). Plugin version 0.5.6 in `CMakeLists.txt`. `MA_ENABLE_ONNX` default is **ON**.

### v0.6.0 — ML Correctness & Evaluation
- **Phase 32 — Training Label Correction:** Rule-oracle labels replace quantile bins in `build_dataset.py` and `build_lakh_dataset.py`; `merge_datasets.py` rejects invalid labels; deterministic class-6 oversampling; retrained `accompaniment_model.onnx` promoted (LABEL-01–03, DATA-06).
- **Phase 33 — Model Quality Gates:** Bass training gate (MSE vs rule baseline, per-step metrics, `validation.json`); structure dual gate (macro-F1 vs rule agreement, norm stats JSON); `StructureOnnxExport` bakes normalization into ONNX graph; contract tests for structure model (QGATE-01–04).
- **Phase 34 — Domain Gap & Feature Capture:** Runtime `feature_capture.v1` JSONL capture with debug toggle; offline annotation evaluator (rule vs ONNX accuracy, confusion matrices, disagreement); captured-vs-proxy gap analyzer with quantitative `FEATURE_PROXY.md` verdicts (DOMAIN-01–03).
- **Phase 35 — Inference Path Consistency:** `OnnxInference` packs adjusted BPM into tensor column 0; compatible exclusion modulo walks to valid pattern instead of blind `(last+1)%7`; 3× intensity dataset expansion + retrain; `structureBlend` and adjusted-BPM docs (INFC-01–03).
- **Phase 36 — ONNX Evaluation & Default Readiness** (in progress): Committed readiness fixture, centroid proxy fix + retrain, CI accuracy gates, ORT latency CTest, `docs/ONNX_READINESS.md`, `MA_ENABLE_ONNX` default flip criteria (ONNXEVAL-01–03).

### v0.5.0 — Rhythmic Coherence (partial)
- **Phase 27 — Documentation:** `plugin/README.md` Pre-FX placement story; root/docs entry points (RHY-DOC-01–02).
- **Phase 31 — Architecture Deepening:** `PlaybackGate`, `StablePitchTracker`, `TempoStabiliser` extracted to `src/analysis/`; `PatternRules` shared header in `src/inference/`; `AccompanimentProcessor` thinned (ARCH-01–04).
- Phases 28–30 (beat tracker, runtime coordination, ML retrain) parked for v0.7.0.

## [v0.4.0] — ML Playability & Simplification (2026-04-29)

**Milestone:** v0.4.0 (Phases 21–26). Better-trained ML on GMD+Lakh merged data; 3-class structure; `[1,32]` bass piano-roll; simplified UI; ML rejection for Next Pattern.

- **Phase 21 — C++ Type Foundation:** `StructureState` → 3-class enum; `FeatureVector` and `IInference` contract updates.
- **Phase 22 — ONNX Contract + Stubs:** Updated tensor shapes; forward-compatible stubs.
- **Phase 23 — C++ Inference Layer:** `OnnxInference` updated to new contract; `OnnxStructureInference` reads normalization from graph.
- **Phase 24 — UI Simplification:** Genre APVTS removed; editor simplified.
- **Phase 25 — Training Data Pipeline:** Lakh MIDI dataset integration; `build_lakh_dataset.py`; `merge_datasets.py` producing merged train/val tensors.
- **Phase 26 — Retrain + Validate:** Pattern and structure models retrained on merged data; promoted to `assets/`; PVAL-01 DAW verification.

## [v0.3.0] — Real ML Training Pipeline (2026-04-19)

- **Phase 17 — Data Pipeline:** `training/download_gmd.py` (TFDS-pinned GMD + SHA-256 manifest); `training/FEATURE_PROXY.md`; `training/build_dataset.py` (train/val `.pt`, histogram gate); `training/tfds_compat.py`; `training/data/` gitignored (DATA-04–06).
- **Phase 18 — Pattern Model:** `PatternNet` (5→32→16→7 MLP); `PatternOnnxExport` with baked normalization; `train_gmd.py` → `pattern_trained.onnx`; contract validation (PMODEL-01–03).
- **Phase 19 — Bass Model:** `BassNet` (7→32→16→4); synthetic E2/A2/B1 training; `train_bass.py` → `bass_trained.onnx`; contract validation (BMODEL-01–02).
- **Phase 20 — Export & Promotion:** `scripts/install-model-local.sh`; `20-VERIFICATION.md` DAW smoke checklist (EXP-01–02).

## [v0.2.0] — ML + Generative (2026-04-17)

- Phase 9 (data & training strategy): `docs/DATASET_AUDIT.md`, `docs/TOKENIZATION.md`, `training/prep_midi.py` stub (DATA-01–03).
- Phase 10 (ONNX): `OnnxInference` loads `assets/accompaniment_model.onnx` when `MA_ENABLE_ONNX=ON`; `RuleBasedInference` fallback; `scripts/build_minimal_pattern_onnx.py`.
- Phase 11 (pitch & harmony): YIN pitch detection; bass root routing (PITCH-01–04).
- Phase 12 (ML structure): Structure ONNX path + rule fallback; integration tests (STRUC-01–03).
- Phase 13 (generative bass): Train/export; rank/select; degradation to static patterns (GBASS-01–03).
- Phase 14 (plugin UI): APVTS policy + editor (PUI-01–03).
- Phase 15 (Python training pipeline): `training/requirements.txt`; `train_pytr_stub.py`; `validate_onnx_contract.py`; `training/README.md` (PYTR-01–03).
- Phase 16 (Terraform model storage): `infra/` S3 + OIDC; `promote-model.sh` / `download-model.sh` (CLOUD-01–02).

## [v0.1.0-phase1] — Phase 1 rule-based MVP

**Summary:** Real-time guitar onset/tempo tracking, energy/structure classification, rule-based `IInference` → drum/bass MIDI via `PatternPlayer`. Phase 2 can replace inference with ONNX or other backends behind the same `IInference` interface.

**Highlights**

- JUCE 8 VST3 + AU; lock-free handoff from audio thread to background inference (~50 Hz)
- `RuleBasedInference` selects patterns from structure state and BPM; `OnnxInference` stub for Phase 2
- Unit tests for onset, structure tagger, pattern player, and rule inference

**Known limitations (Phase 1)**

- Bass pitch fixed (no real-time pitch tracking yet)
- Structure is threshold/rule-based, not ML-classified
- Plugin UI is debug-oriented; full parameter UI is Phase 2

### Tagging this release

After documentation and CI are green on `main`:

```bash
git tag -a v0.1.0-phase1 -m "Phase 1 rule-based MVP; see CHANGELOG.md"
git push origin v0.1.0-phase1
```

Then open GitHub Issues for Phase 2 work using `docs/PHASE2_GITHUB_ISSUES.md` as a checklist.
