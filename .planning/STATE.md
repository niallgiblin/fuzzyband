---
gsd_state_version: 1.0
milestone: v0.2.0
milestone_name: — Phase 2 ML + Generative
status: Roadmap defined — use `/gsd-discuss-phase 9` or `/gsd-plan-phase 9`
last_updated: "2026-04-17T13:23:38.941Z"
last_activity: 2026-04-17 — Milestone v0.2.0 started
progress:
  total_phases: 5
  completed_phases: 2
  total_plans: 7
  completed_plans: 6
  percent: 86
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-17)

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** Milestone **v0.2.0** — Phase **9** next (data & training strategy)

---

## Current Position

**Milestone:** v0.2.0 — Phase 2: ML + Generative  
**Phase:** Not started (next: 9 — Data & training strategy)  
**Plan:** —  
**Status:** Roadmap defined — use `/gsd-discuss-phase 9` or `/gsd-plan-phase 9`  
**Last activity:** 2026-04-17 — Milestone v0.2.0 started

---

## Current Status

| Phase | Name | Status |
|-------|------|--------|
| 1–8 | Phase 1 (v0.1.0) | ✓ Shipped |
| 9 | Data & training strategy | Not started |
| 10 | ONNX runtime & IInference | Not started |
| 11 | Pitch & harmony | Not started |
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

- **2026-04-17 — Phase 2 ML data strategy (backlog 999.1):** Pretrain **symbolic drum** behavior from **Groove MIDI (GMD)** for pure MIDI; **E-GMD** only if audio+aligned MIDI is needed. Use **Lakh** (drum-filtered) for scale. **Bridge** MIDI pretrain to the plugin via a **policy** on `FeatureVector` (+ time history for anticipation), or generative MIDI with constraints — see `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`. **Hybrid** (offline MIDI pool + runtime rank/select) preferred over pure replace for safety.

---

## Session Continuity

Last session: 2026-04-17T13:23:38.933Z
Resume: `/gsd-discuss-phase 9` or `/gsd-plan-phase 9`

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
