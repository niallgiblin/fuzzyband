---
gsd_state_version: 1.0
milestone: v0.2.0
milestone_name: — Phase 2 ML + Generative
status: Ready to plan
last_updated: "2026-04-17T14:15:00.000Z"
last_activity: 2026-04-17
progress:
  total_phases: 8
  completed_phases: 2
  percent: 25
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-17)

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** Phase 10 — ONNX runtime & IInference

---

## Current Position

Phase: 09 (data-training-strategy) — complete (2026-04-17)  
Plan: 09-01 complete  
**Milestone:** v0.2.0 — Phase 2: ML + Generative  
**Phase:** 10 — next (ONNX runtime & IInference)  
**Next:** Phase 10 — ONNX runtime & IInference  
**Last activity:** 2026-04-17

---

## Current Status

| Phase | Name | Status |
|-------|------|--------|
| 1–8 | Phase 1 (v0.1.0) | ✓ Shipped |
| 9 | Data & training strategy | ✓ Complete 2026-04-17 |
| 10 | ONNX runtime & IInference | Not started |
| 11 | Pitch & harmony | ✓ Complete 2026-04-17 |
| 12 | ML structure | Not started |
| 13 | Generative bass | Not started |
| 14 | Plugin UI | Not started |
| 15 | Python training pipeline | Not started |
| 16 | Terraform model storage | Not started |

---

## Deferred Items (from v0.1.0)

| Category | Item | Status |
|----------|------|--------|
| release | Push `v0.1.0-phase1` to `origin` | Pending |
| release | Create Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md` | Pending |
| verification | Optional Reaper routing smoke-test (OUT-05) | Optional |

---

## Decisions Log

- **2026-04-17 — Phase 9 (Data & training strategy) executed:** Dataset audit (`docs/DATASET_AUDIT.md`), tokenization (`docs/TOKENIZATION.md`), `training/prep_midi.py` + `mido`, CI prep smoke on `training/fixtures/minimal.mid`. Context: `.planning/phases/09-data-training-strategy/09-CONTEXT.md`.
- **2026-04-17 — Phase 11 (Pitch & harmony) discuss:** Hybrid **YIN** + optional **ONNX** pitch model; **root-only** (+ confidence) per window; **transpose bass at playback** vs nominal E2; **hold last good root** when noisy, E2 on silence/cold start; **unit tests + macOS CI**. Context: `.planning/phases/11-pitch-harmony/11-CONTEXT.md`.
- **2026-04-17 — Phase 2 ML data strategy (backlog 999.1):** Pretrain **symbolic drum** behavior from **Groove MIDI (GMD)** for pure MIDI; **E-GMD** only if audio+aligned MIDI is needed. Use **Lakh** (drum-filtered) for scale. **Bridge** MIDI pretrain to the plugin via a **policy** on `FeatureVector` (+ time history for anticipation), or generative MIDI with constraints — see `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`. **Hybrid** (offline MIDI pool + runtime rank/select) preferred over pure replace for safety.

---

## Session Continuity

Last session: 2026-04-17T14:15:00.000Z
Resume: `/gsd-discuss-phase 10` or `/gsd-plan-phase 10` for v0.2.0 Phase 10

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
