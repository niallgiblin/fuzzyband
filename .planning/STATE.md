---
gsd_state_version: 1.0
milestone: v0.6.0
milestone_name: — ML Correctness & Evaluation
status: ready_to_execute
last_updated: "2026-05-12T15:45:00.000Z"
progress:
  total_phases: 11
  completed_phases: 2
  total_plans: 11
  completed_plans: 9
  percent: 18
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-29)

**Version string in the plugin UI** comes from **`CMakeLists.txt`** `project(MetalAccompaniment VERSION …)` (JUCE `ProjectInfo::versionString`). **GSD phase/milestone authority** is this file plus **`.planning/ROADMAP.md`**.

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** Phase 33 — model-quality-gates

---

## Current position

Phase: 32 (training-label-correction) — EXECUTED; verification recorded in `.planning/phases/32-training-label-correction/32-VERIFICATION.md`
Next: Phase 33 (model-quality-gates)
Milestone: **v0.6.0 ML Correctness & Evaluation**  
Completed requirements: LABEL-01, LABEL-02, LABEL-03

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

Resume: `/gsd-execute-phase 28` — `.planning/phases/28-beat-tracker-bass-sequencer/`; Phase **27** summary `.planning/phases/27-rhythmic-coherence-documentation/27-SUMMARY.md`.

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
