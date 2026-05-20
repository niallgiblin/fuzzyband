---
gsd_state_version: 1.0
milestone: v0.6.0
milestone_name: — ML Correctness & Evaluation
status: ready_to_execute
last_updated: "2026-05-13T11:33:37.298Z"
progress:
  total_phases: 6
  completed_phases: 2
  total_plans: 9
  completed_plans: 6
  percent: 67
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-29)

**Version string in the plugin UI** comes from **`CMakeLists.txt`** `project(MetalAccompaniment VERSION …)` (JUCE `ProjectInfo::versionString`). **GSD phase/milestone authority** is this file plus **`.planning/ROADMAP.md`**.

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** Phase 34 — domain-gap-and-feature-capture

---

## Current position

Phase: 33 (model-quality-gates) — EXECUTED; summaries recorded in `.planning/phases/33-model-quality-gates/`
Phase: 34 (domain-gap-and-feature-capture) — PLANNED; plans recorded in `.planning/phases/34-domain-gap-and-feature-capture/`
Next: Execute Phase 34 (`/gsd-execute-phase 34`)
Milestone: **v0.6.0 ML Correctness & Evaluation**  
Completed requirements: LABEL-01, LABEL-02, LABEL-03, QGATE-01, QGATE-02, QGATE-03, QGATE-04

---

## Shipped milestones (summary)

| Milestone | Phases | Requirements archive |
|-----------|--------|------------------------|
| v0.1.0 | 1–8 | `.planning/milestones/v0.1.0-REQUIREMENTS.md` |
| v0.2.0 | 9–16 | `.planning/milestones/v0.2.0-REQUIREMENTS.md` |
| v0.3.0 | 17–20 | `.planning/milestones/v0.3.0-REQUIREMENTS.md` |
| v0.4.0 | 21–26 | `.planning/milestones/v0.4.0-REQUIREMENTS.md` |

---

## Accumulated context

### Key architectural constraints (v0.5.0 — preview)

- Phase **29** must freeze `FeatureVector` layout before Phase **30** ONNX/training changes — single validator for plugin ↔ Python parity (`RHY-ML-01`).
- Beat tracker + bass sequencer (**28**) precede unified bar logic (**29**) per roadmap dependency note.

### Decisions log (recent)

- **2026-04-29 — Phase 28 discuss-phase:** Beat-primary + IOI fallback with tests; tracker-based start gate; bar-aligned 16-step bass; synthetic-first ±5 BPM tests; debug getters OK, no `FeatureVector` expansion until Phase 29; module boundary planner discretion — see `28-CONTEXT.md`.
- **2026-04-29 — v0.4.0 milestone archived:** `.planning/milestones/v0.4.0-ROADMAP.md`, `v0.4.0-REQUIREMENTS.md`; living `REQUIREMENTS.md` reset to **v0.5.0** RHY-* scope; `STATE.md` milestone pointer → v0.5.0.
- **2026-05-12 — v0.6.0 ML Correctness & Evaluation phases added (32–36):** Phases derived from `ML_REPORT.md` analysis and `CHANGES_PLAN.md` (5 areas). Phase 32 (label correction), 33 (quality gates), 34 (domain gap + capture), 35 (inference path consistency — depends on 32), 36 (evaluation + default readiness — depends on 32–35). Phases 32–34 can execute in parallel after v0.5.0 ships.
- **2026-05-12 — Phase 32 Plan 03 verification gap closed:** Stale Lakh labels were repaired to rule-oracle labels in 0..6, `merge_datasets.py` now rejects invalid labels before gating, class 6 is deterministically oversampled to the 50-example DATA-06 minimum, and the pattern model was retrained/promoted from merged data with best macro-F1 0.8095. Class 0 remains an accepted structural absence handled by rule-based passthrough.
- **2026-05-13 — Phase 33 Model Quality Gates completed:** Bass and structure training now enforce hard quality gates before ONNX export, structure normalization is baked into the ONNX graph, structure contract tests validate `X_struct` shape/dtype and baked mean/std initializers, and `assets/structure_model.onnx` was promoted with version bump 0.5.0 → 0.5.1. See `33-01-SUMMARY.md`, `33-02-SUMMARY.md`, and `33-03-SUMMARY.md`.
- **2026-05-13 — Phase 34 planned:** Three executable plans cover runtime feature capture, offline human-label evaluation, and captured-vs-proxy domain-gap documentation. Decision coverage passed for all 21 Phase 34 context decisions.

*(Older entries remain in git history and phase directories.)*

---

## Deferred items (cross-cutting)

| Category | Item | Status |
|----------|------|--------|
| release | Push `v0.1.0-phase1` to `origin` | Pending |
| release | Push `v0.2.0`, `v0.3.0`, `v0.4.0` tags to `origin` | Pending |
| release | Create Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md` | Pending |
| verification | Optional legacy EXP-02 intensity sweep (`20-VERIFICATION.md`) — release gating superseded by **v0.4 PVAL-01** | Optional |

---

## Session continuity

Resume: `/gsd-execute-phase 34` — `.planning/phases/34-domain-gap-and-feature-capture/`; plans `34-01-PLAN.md` through `34-03-PLAN.md`.

---

### Quick tasks completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260420-421 | Implement tempo and bass stability fixes | 2026-04-20 | b2d0f5d | [260420-421-tempo-bass-stability-fixes](./quick/260420-421-tempo-bass-stability-fixes/) |
| 260420-422 | Rebrand UI to fuzzyband with Pacific NW mossy aesthetic | 2026-04-20 | 22ac48d | [260420-422-fuzzyband-ui-rebrand](./quick/260420-422-fuzzyband-ui-rebrand/) |
| 260427-t42 | Drum count-in gate (4 onsets) and 2-bar pattern hold | 2026-04-27 | 09a92c4 | [260427-t42-drum-timing-countin-gate-2bar-hold](./quick/260427-t42-drum-timing-countin-gate-2bar-hold/) |
| 260427-t43 | BPM lock-in, 5-BPM quantization, 80ms onset refractory | 2026-04-27 | 1c02d3e | [260427-t43-onset-tempo-stability](./quick/260427-t43-onset-tempo-stability/) |

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
