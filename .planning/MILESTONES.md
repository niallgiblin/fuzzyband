# Milestones — Metal Accompaniment

## v0.6.0 — ML Correctness & Evaluation (Shipped: 2026-06-02)

**Phases:** 32–36 (5 phases) | **Plans:** 16 | **Requirements:** 16+ (all satisfied — see `.planning/milestones/v0.6.0-REQUIREMENTS.md`)

### Summary

Shipped ONNX default enablement with `MA_ENABLE_ONNX=ON`, retrained pattern model on corrected oracle labels and centroid-aligned proxy features (macro-F1 0.6391), baked-normalization structure model with dual quality gates, live-capture domain gap analysis, inference path consistency (shared adjustedBpm + compatible exclusion), CI readiness fixture, latency benchmark (all p99 < 5ms), and `ONNX_READINESS.md` with all six criteria PASS.

### Key accomplishments

1. **Label correction + quality gates** — Phases 32–33: rule-oracle labels replacing quantile bins; bass MSE gate + structure dual F1 gate; structure ONNX with baked normalization.
2. **Domain gap analysis** — Phase 34: real-guitar FeatureCapture JSONL; offline evaluator; proxy distribution shift documented.
3. **Inference path consistency** — Phase 35: shared `PatternRules::adjustedBpm`; compatibility-aware exclusion cycling; intensity-variant training.
4. **Centroid fix + retrain** — Phase 36: proxy formula updated to match captured guitar stats; full GMD+Lakh retrain; model promoted.
5. **Default ONNX flip** — `MA_ENABLE_ONNX` default → ON; six readiness criteria all PASS; CI fixture evaluation gate; latency benchmark p99 < 5ms.

### Pre-flight

- `.planning/v0.6.0-MILESTONE-AUDIT.md` — verdict: `passed` (remediated 2026-06-02)
- Phases 33 and 36 verified on 2026-06-02 during milestone close.

### Accepted flag

Class 0 (SILENT) permanently excluded from training data/DATA-06 gate — MIDI corpora lack SILENT examples. Rule-based passthrough handles it at runtime. Documented in `merge_datasets.py`.

---

## v0.5.0 — Rhythmic Coherence (Partial — superseded)

**Phases:** 27, 28, 29, 31 shipped (with caveats); 30 superseded | **Status:** Phase 31 (Architecture Deepening) complete. Phase 28 implemented but UAT-failed — folded into v0.7.0 S01 Tempo Stability (BeatTracker refocused on learn-mode/gate). Phase 29 complete with review fixes resolved, UAT passed 8/8, and security verified with 0 open threats. Phase 30 superseded by v0.7.0 feature semantics freeze.

---

## v0.4.0 — ML Playability & Simplification (Shipped: 2026-04-29)

**Phases:** 21–26 (6 phases) | **Plans:** 13 | **Requirements:** 15 (all satisfied — see `.planning/milestones/v0.4.0-REQUIREMENTS.md`)

### Summary

Shipped three-class energy structure (SILENT/SOFT/LOUD), ONNX contracts + stubs for 3-class structure and `[1,32]` piano-roll bass, C++ `OnnxInference` with load-time shape assertions and ML pattern rejection (`excludeIndex` / `patternRejectionCount`), genre-free UI with Next Pattern driving rejection, GMD+Lakh merged training tensors with domain compatibility gate, retrained Pattern/Bass/Structure models, and human Reaper jam (PVAL-01).

### Key accomplishments

1. **Types + contract** — Phase 21–22: enum + `FeatureVector` cleanup; `docs/ONNX_IO.md`, stubs, `validate_onnx_contract.py` gates.
2. **Inference + UI** — Phase 23–24: piano-roll bass decode, rejection path + tests; atomic genre removal + session XML compat.
3. **Data** — Phase 25: Lakh download/filter/build + `merge_datasets.py` with `val_gmd.pt` preserved.
4. **Retrain + ship** — Phase 26: merged-data training, promoted `assets/*.onnx`, Reaper verification (`26-03-SUMMARY.md`).

### Pre-flight

- No `.planning/v0.4.0-MILESTONE-AUDIT.md` — optional `/gsd-audit-milestone` for future milestones.
- `audit-open` clear at close after resolving quick-task `SUMMARY.md` filenames and `20-VERIFICATION.md` status.

### Deferred at milestone boundary

Operational git pushes / GitHub issues remain in `PROJECT.md` **Active (release / ops)** where applicable.

---

## v0.3.0 — Real ML Training Pipeline (Shipped: 2026-04-19)

**Phases:** 17–20 (4 phases) | **Plans with summaries:** 10 | **Tasks (from summaries):** 13

### Summary

Shipped Groove MIDI Dataset ingestion and tensor pipeline with class histogram gate, trained `PatternNet` and `BassNet` with ONNX export at opset 17, contract validation for both graphs, local `install-model-local.sh` into `assets/*.onnx`, and a Reaper-first human verification doc for EXP-02.

### Key accomplishments

1. **Data pipeline** — `download_gmd.py`, `FEATURE_PROXY.md`, `build_dataset.py`, train/val shards + `norm_stats.json` (Phase 17).
2. **Pattern model** — `train_gmd.py`, macro-F1 early stopping, `pattern_trained.onnx` (Phase 18).
3. **Bass model** — Synthetic metal-key training, `bass_trained.onnx` (Phase 19).
4. **Export** — Validate-then-copy installer + training README; `20-VERIFICATION.md` checklist (Phase 20).

### Known gaps at close

- **EXP-02** (non-degenerate pattern selection in a DAW): procedure documented; human pass pending. See `STATE.md` → **Deferred Items**.

---

## v0.2.0 — Phase 2 ML + Generative (Shipped: 2026-04-17)

**Phases:** 9–16 (8 phases) | **Plans with summaries:** 18

### Summary

Shipped optional ONNX inference with a frozen I/O contract and CI guardrails, YIN-based pitch and adaptive bass routing, ML structure with rule fallback, generative bass with validation and rank/select, APVTS-backed plugin UI, a reproducible Python training stub with ONNX export validation, and Terraform + scripts for versioned model storage and promotion.

### Key accomplishments

1. **Data & ONNX foundation** — Dataset audit and tokenization docs; `training/prep_midi.py`; `docs/ONNX_IO.md` and audio-thread checks (Phase 9–10).
2. **Realtime intelligence** — Pitch/harmony, ML structure, generative bass integration without blocking the audio thread (Phase 11–13).
3. **Product surface** — Policy parameters and editor layout with documentation (Phase 14).
4. **Train → ship loop** — Pinned Python deps, stub training, dual ONNX export, contract validation script (Phase 15).
5. **Ops** — S3 + GitHub OIDC Terraform, promote/download scripts, optional CI model fetch (Phase 16).

---

## v0.1.0 — Phase 1 rule-based MVP (Shipped: 2026-04-17)

**Phases:** 1–8 (8 development phases) | **Plans with summaries:** 6

### Summary

Shipped a JUCE 8 VST3/AU plugin that listens to guitar audio and drives drum and bass MIDI in real time via rule-based onset/tempo/structure analysis and `IInference` → `PatternPlayer`, with lock-free threading to the audio path, CI on macOS, Doxygen/CONTRIBUTING/CHANGELOG handoff, and Phase 2 issue checklist.

### Key accomplishments

1. **Scaffold & analysis** — CMake VST3/AU, CI, onset/tempo tracker with unit test, energy/structure classification with debug UI.
2. **MIDI core** — Pattern library, offline export, `PatternPlayer` with humanisation and channel routing.
3. **Inference boundary** — `RuleBasedInference` + background thread; `IInference` ready for Phase 2 ONNX drop-in.
4. **Integration & stability** — Phase 7 plans: tagger tuning, BPM-adaptive durations, processor hardening, debug metrics, release/TSan/stability/CPU evidence.
5. **Documentation handoff** — Doxygen target, CONTRIBUTING, CHANGELOG tagging instructions, `docs/PHASE2_GITHUB_ISSUES.md` checklist.

### Known deferred items at close

| Item | Notes |
|------|--------|
| Push tag | `git push origin v0.1.0-phase1` may still be pending |
| GitHub issues | Create issues from `docs/PHASE2_GITHUB_ISSUES.md` |
| Optional DAW smoke | Reaper session confirmation for OUT-05 if desired |

---

*Milestone entries created at each milestone close (`/gsd-complete-milestone`).*
