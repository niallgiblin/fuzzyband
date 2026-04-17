---
gsd_state_version: 1.0
milestone: v0.2.0
milestone_name: — Phase 2 ML + Generative
status: Milestone complete
last_updated: "2026-04-17T23:59:00.000Z"
last_activity: 2026-04-17
progress:
  total_phases: 8
  completed_phases: 8
  total_plans: 18
  completed_plans: 18
  percent: 100
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-17)

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** v0.2.0 milestone closed — Phases 9–16 complete. Optional: release tagging, Phase 2 GitHub issues, dataset scale-up beyond stubs.

---

## Current Position

Phase: — (milestone complete)  
Plan: Phase 15 — `15-01` + `15-02` executed; see `.planning/phases/15-python-training-pipeline/`  
**Milestone:** v0.2.0 — Phase 2: ML + Generative — **complete**  
**Last completed:** Phase 15 — Python training pipeline (PYTR-01–03), 2026-04-17  
**Next:** Release / backlog ops (see ROADMAP **Next**); no queued GSD phase in v0.2.0.  
**Last activity:** 2026-04-17

---

## Current Status

| Phase | Name | Status |
|-------|------|--------|
| 1–8 | Phase 1 (v0.1.0) | ✓ Shipped |
| 9 | Data & training strategy | ✓ Complete 2026-04-17 |
| 10 | ONNX runtime & IInference | ✓ Complete 2026-04-17 |
| 11 | Pitch & harmony | ✓ Complete 2026-04-17 |
| 12 | ML structure | ✓ Complete 2026-04-17 |
| 13 | Generative bass | ✓ Complete 2026-04-17 |
| 14 | Plugin UI | ✓ Complete 2026-04-17 |
| 15 | Python training pipeline | ✓ Complete 2026-04-17 |
| 16 | Terraform model storage | ✓ Complete 2026-04-17 |

---

## Deferred Items (from v0.1.0)

| Category | Item | Status |
|----------|------|--------|
| release | Push `v0.1.0-phase1` to `origin` | Pending |
| release | Create Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md` | Pending |
| verification | Optional Reaper routing smoke-test (OUT-05) | Optional |

---

## Decisions Log

- **2026-04-17 — Phase 15 executed (Python training pipeline):** Pinned `training/requirements.txt` (torch **2.6.0** for Python 3.13+ wheels); `training/train_pytr_stub.py` + `scripts/validate_onnx_contract.py`; docs in `training/README.md` / `CONTRIBUTING.md`; `.planning/phases/15-python-training-pipeline/15-VERIFICATION.md`. `gsd-sdk` unavailable — inline execution.
- **2026-04-17 — Phase 15 planned:** `15-RESEARCH.md`, `15-PATTERNS.md`, two plans — **15-01** (PYTR-01 reproducibility) wave 1, **15-02** (PYTR-02/03 train + ONNX export + validate) wave 2. `gsd-sdk` unavailable — plan-phase executed inline; `plan_check` false in config (checker skipped).
- **2026-04-17 — Phase 15 discuss (Python training pipeline):** **PYTR-01** — **pip** + pinned **`requirements.txt`**. **Layout / dataset scale / metal filter / export paths / Phase 13 consolidation** — **Claude's discretion** with README clarity and single export-contract source of truth. **Training** — **PyTorch**; **first run** — **tiny/stub both** pattern (`ONNX_IO`) and bass (`BASS_ONNX_IO`) in one session for wiring + metrics. **ONNX validation** — **local/dev + docs**; **default CI light**. Context: `.planning/phases/15-python-training-pipeline/15-CONTEXT.md`.
- **2026-04-17 — Planning clarity:** **Onset robustness** sidecar lives under `.planning/phases/15-onset-robustness/` (distinct from roadmap Phase 15 **PYTR**). Roadmap Phase 15 is **`.planning/phases/15-python-training-pipeline/`** only.
- **2026-04-17 — Phase 16 (Terraform model storage) executed:** `infra/` Terraform (versioned S3, GitHub OIDC IAM), `scripts/bootstrap-tfstate.sh`, `scripts/promote-model.sh`, `scripts/download-model.sh`, optional CI OIDC + model download; verification `.planning/phases/16-terraform-model-storage/16-VERIFICATION.md`. `gsd-sdk` unavailable — execution inline.
- **2026-04-17 — Phase 14 (Plugin UI) executed:** APVTS policy + `PolicyPatternMapper` + structure blend drain + editor attachments + README/CONTRIBUTING; verification `.planning/phases/14-plugin-ui/14-VERIFICATION.md`.
- **2026-04-17 — Phase 14 (Plugin UI) planned:** `14-RESEARCH.md` + three plans (`14-01`–`14-03`); status **Ready to execute** — `/gsd-execute-phase 14`.
- **2026-04-17 — Phase 14 (Plugin UI) discuss:** **Outcome-first** PUI knobs (planner implements); **genre** = discrete choice, **intensity/variation** = continuous mid-defaults; **all diagnostics stay visible** + **PUI-03** docs; **short help + link**; **generative bass** + **structure trust** exposed in APVTS/editor this phase. Context: `.planning/phases/14-plugin-ui/14-CONTEXT.md`.
- **2026-04-17 — Phase 13 (Generative bass) discuss:** Hybrid **rank/select** for bass; **separate ONNX** from pattern `X`/`Y`; **transpose** generated bass like static patterns; **degradation** aligned with load + confidence gates; **training depth** and **merge point** at Claude discretion within GBASS-01–03. Context: `.planning/phases/13-generative-bass/13-CONTEXT.md`.
- **2026-04-17 — Phase 10 (ONNX runtime & IInference) 10-01 executed:** Froze ONNX I/O contract in `docs/ONNX_IO.md` (input `X` float32 `[1,5]`, output `Y` int64 clamped 0–6); linked from README + ARCHITECTURE; added `scripts/check_onnx_audio_thread.sh` + CI step guarding against ORT API leaking into `AccompanimentProcessor`. Default CI stays `MA_ENABLE_ONNX=OFF` (D-10-01). Closes ONNX-01/02/03.
- **2026-04-17 — Phase 9 (Data & training strategy) executed:** Dataset audit (`docs/DATASET_AUDIT.md`), tokenization (`docs/TOKENIZATION.md`), `training/prep_midi.py` + `mido`, CI prep smoke on `training/fixtures/minimal.mid`. Context: `.planning/phases/09-data-training-strategy/09-CONTEXT.md`.
- **2026-04-17 — Phase 11 (Pitch & harmony) discuss:** Hybrid **YIN** + optional **ONNX** pitch model; **root-only** (+ confidence) per window; **transpose bass at playback** vs nominal E2; **hold last good root** when noisy, E2 on silence/cold start; **unit tests + macOS CI**. Context: `.planning/phases/11-pitch-harmony/11-CONTEXT.md`.
- **2026-04-17 — Phase 12 (ML structure) complete:** ML-based structure path with safe fallback; milestone advanced — next focus Phase 13 (Generative bass). See `.planning/phases/12-ml-structure/` for plans and summaries.
- **2026-04-17 — Phase 2 ML data strategy (backlog 999.1):** Pretrain **symbolic drum** behavior from **Groove MIDI (GMD)** for pure MIDI; **E-GMD** only if audio+aligned MIDI is needed. Use **Lakh** (drum-filtered) for scale. **Bridge** MIDI pretrain to the plugin via a **policy** on `FeatureVector` (+ time history for anticipation), or generative MIDI with constraints — see `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`. **Hybrid** (offline MIDI pool + runtime rank/select) preferred over pure replace for safety.

---

## Session Continuity

Last session: 2026-04-17T23:59:00.000Z  
Resume: Milestone v0.2.0 complete — optional `/gsd-milestone-summary` or release checklist; see `docs/PHASE2_GITHUB_ISSUES.md`.

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
