# Milestones — Metal Accompaniment

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

### Pre-flight

- No `.planning/v0.3.0-MILESTONE-AUDIT.md` on file — optional `/gsd-audit-milestone` next time.
- `audit-open` at close: **1** open item — EXP-02 human Reaper verification (`20-VERIFICATION.md`). Acknowledged; requirement **EXP-02** left unchecked in archived requirements until an operator completes the checklist.

### Known gaps at close

- **EXP-02** (non-degenerate pattern selection in a DAW): procedure documented; human pass pending. See `STATE.md` → **Deferred Items**.

---

## v0.2.0 — Phase 2 ML + Generative

**Shipped:** 2026-04-17  
**Phases:** 9–16 (8 phases)  
**Plans with summaries:** 18

### Summary

Shipped optional ONNX inference with a frozen I/O contract and CI guardrails, YIN-based pitch and adaptive bass routing, ML structure with rule fallback, generative bass with validation and rank/select, APVTS-backed plugin UI, a reproducible Python training stub with ONNX export validation, and Terraform + scripts for versioned model storage and promotion.

### Key accomplishments

1. **Data & ONNX foundation** — Dataset audit and tokenization docs; `training/prep_midi.py`; `docs/ONNX_IO.md` and audio-thread checks (Phase 9–10).
2. **Realtime intelligence** — Pitch/harmony, ML structure, generative bass integration without blocking the audio thread (Phase 11–13).
3. **Product surface** — Policy parameters and editor layout with documentation (Phase 14).
4. **Train → ship loop** — Pinned Python deps, stub training, dual ONNX export, contract validation script (Phase 15).
5. **Ops** — S3 + GitHub OIDC Terraform, promote/download scripts, optional CI model fetch (Phase 16).

### Stats (approximate)

- **Window:** 2026-04-16 — 2026-04-17 (GSD-recorded execution)
- **Plans:** 18 with summaries (phases 9–16)

### Pre-flight note

No `.planning/v0.2.0-MILESTONE-AUDIT.md` on file — optional `/gsd-audit-milestone` next time for a formal audit trail. Open-artifact audit (`audit-open`) was clear at close.

### Known deferred items at close

Same operational follow-ups as v0.1.0 where still pending: push legacy tag, GitHub issues checklist, optional DAW smoke. See `STATE.md` → **Deferred Items**.

---

## v0.1.0 — Phase 1 rule-based MVP

**Shipped:** 2026-04-17  
**Phases:** 1–8 (8 development phases)  
**Plans with summaries:** 6 (Phase 7: five plans; Phase 8: one plan)

### Summary

Shipped a JUCE 8 VST3/AU plugin that listens to guitar audio and drives drum and bass MIDI in real time via rule-based onset/tempo/structure analysis and `IInference` → `PatternPlayer`, with lock-free threading to the audio path, CI on macOS, Doxygen/CONTRIBUTING/CHANGELOG handoff, and Phase 2 issue checklist.

### Key accomplishments

1. **Scaffold & analysis** — CMake VST3/AU, CI, onset/tempo tracker with unit test, energy/structure classification with debug UI.
2. **MIDI core** — Pattern library, offline export, `PatternPlayer` with humanisation and channel routing.
3. **Inference boundary** — `RuleBasedInference` + background thread; `IInference` ready for Phase 2 ONNX drop-in.
4. **Integration & stability** — Phase 7 plans: tagger tuning, BPM-adaptive durations, processor hardening, debug metrics, release/TSan/stability/CPU evidence (see `.planning/phases/07-integration-stability/`).
5. **Documentation handoff** — Doxygen target, CONTRIBUTING, CHANGELOG tagging instructions, `docs/PHASE2_GITHUB_ISSUES.md` checklist.

### Stats (approximate)

- **Git range:** `0c306ca` → `HEAD` (first import through milestone close)
- **Commits on `main`:** 21+ (milestone window)
- **Diff (first commit..HEAD):** ~104 files, +12k LOC (per `git diff --stat`)

### Known deferred items at close

Operational follow-ups (not blocking code archive):

| Item | Notes |
|------|--------|
| Push tag | `git push origin v0.1.0-phase1` may still be pending |
| GitHub issues | Create issues from `docs/PHASE2_GITHUB_ISSUES.md` |
| Optional DAW smoke | Reaper session confirmation for OUT-05 if desired |

See `STATE.md` → **Deferred Items** for the canonical list.

---

*Milestone entry created at v0.1.0 close (`/gsd-complete-milestone`).*
