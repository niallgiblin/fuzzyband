---
gsd_state_version: 1.0
milestone: v0.3.0
milestone_name: Real ML Training Pipeline
status: In progress
last_updated: "2026-04-18T12:00:00.000Z"
last_activity: 2026-04-18
progress:
  total_phases: 4
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-17)

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** v0.3.0 — Real ML Training Pipeline. Replace synthetic stub with GMD-trained models. C++ plugin unchanged; artifacts slot into `assets/*.onnx`.

---

## Current Position

**Phase:** 17 — Data Pipeline  
**Plan:** —  
**Milestone:** v0.3.0 — Real ML Training Pipeline  
**Last shipped:** v0.2.0 — Phase 2 ML + Generative (Phases 9–16), 2026-04-17  
**Status:** Context gathered (discuss-phase complete); planning next  
**Last activity:** 2026-04-18 — Phase 17 CONTEXT.md written

```
Progress: [                    ] 0% (0/4 phases)
```

---

## Shipped milestones (summary)

| Milestone | Phases | Archive |
|-----------|--------|---------|
| v0.1.0 | 1–8 | `.planning/milestones/v0.1.0-ROADMAP.md` |
| v0.2.0 | 9–16 | `.planning/milestones/v0.2.0-ROADMAP.md` |

---

## v0.3.0 Phase Map

| Phase | Name | Requirements | Status |
|-------|------|--------------|--------|
| 17 | Data Pipeline | DATA-04, DATA-05, DATA-06 | Not started |
| 18 | Pattern Model | PMODEL-01, PMODEL-02, PMODEL-03 | Not started |
| 19 | Bass Model | BMODEL-01, BMODEL-02 | Not started |
| 20 | Export & Promotion | EXP-01, EXP-02 | Not started |

**Parallelization note:** Phase 19 can run concurrently with Phase 18 — both depend only on Phase 17 tensors.

---

## Accumulated Context

### Key architectural constraints (v0.3.0)

- `training/prep_midi.py` must NOT be modified — Phase 17 imports `_events_from_file` from it
- Normalization must be baked INTO the `PatternOnnxExport` ONNX wrapper — plugin sends raw `FeatureVector` values
- Class histogram audit (≥50 examples per pattern class 0–6) is a Phase 17 gate before training
- `scripts/validate_onnx_contract.py` is a mandatory pass gate in Phases 18, 19, and 20
- Bass model uses synthetic metal-key pitch data (GMD is drum-only — no external bass corpus)
- All `src/` C++ is frozen — trained models slot into existing `assets/accompaniment_model.onnx` path
- Pin opset 17 in all ONNX exports; `onnxruntime==1.20.1` in training must match C++ ORT version

### Decisions Log

- **2026-04-18 — Phase 17 discuss-phase:** Full GMD via TFDS (pinned); hybrid MIDI heuristic + pattern-seed similarity for labels 0–6; proxies match `ONNX_IO.md` numerically with documented MIDI vs audio gap; manifest + `.pt` shards + norm stats JSON; train/val split by performance/session. See `17-CONTEXT.md`.
- **2026-04-18 — v0.3.0 roadmap created:** 4 phases (17–20), 10 requirements mapped, coarse granularity.
- **2026-04-17 — v0.2.0 milestone archived:** `.planning/milestones/v0.2.0-ROADMAP.md`, `v0.2.0-REQUIREMENTS.md`; `STATE.md` reset between milestones.

*(Older entries remain in git history and phase directories.)*

---

## Deferred Items (cross-cutting)

| Category | Item | Status |
|----------|------|--------|
| release | Push `v0.1.0-phase1` to `origin` | Pending |
| release | Push `v0.2.0` to `origin` | Pending |
| release | Create Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md` | Pending |
| verification | Optional Reaper routing smoke-test (OUT-05) | Optional |

---

## Session Continuity

Last session: 2026-04-18 — Phase 17 discuss-phase completed  
Resume: Run `/gsd-plan-phase 17` after `/clear` — context: `.planning/phases/17-data-pipeline/17-CONTEXT.md`

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
