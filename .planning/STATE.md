---
gsd_state_version: 1.0
milestone: v0.3.0
milestone_name: Real ML Training Pipeline
status: In progress
last_updated: "2026-04-19T18:00:00.000Z"
last_activity: 2026-04-19
progress:
  total_phases: 4
  completed_phases: 2
  total_plans: 3
  completed_plans: 3
  percent: 50
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-19)

**Version string in the plugin UI** comes from **`CMakeLists.txt`** `project(MetalAccompaniment VERSION …)` (JUCE `ProjectInfo::versionString`). It should move with milestone checkpoints; **GSD phase/milestone authority** is this file plus **`.planning/ROADMAP.md`** (not `ROADMAP_PHASE_1.md`, which is the historical Phase 1 doc only).

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** v0.3.0 — Real ML Training Pipeline. Replace synthetic stub with GMD-trained models. C++ plugin unchanged; artifacts slot into `assets/*.onnx`.

---

## Current Position

**Phase:** 19 — Bass Model  
**Plan:** TBD (plan next phase)  
**Milestone:** v0.3.0 — Real ML Training Pipeline  
**Last shipped:** v0.2.0 — Phase 2 ML + Generative (Phases 9–16), 2026-04-17  
**Status:** Phase 18 complete — Pattern Model (PMODEL-01–03); ONNX contract validated on trained artifact  
**Last activity:** 2026-04-19 — Phase 18 executed (`train_gmd.py`, `pattern_model.py`, verification)

```
Progress: [████████            ] 50% (2/4 phases)
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
| 17 | Data Pipeline | DATA-04, DATA-05, DATA-06 | Complete |
| 18 | Pattern Model | PMODEL-01, PMODEL-02, PMODEL-03 | Complete |
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

- **2026-04-19 — Phase 18 discuss-phase:** Macro-F1 logging + early stopping on macro-F1; inverse-frequency class weights; bake `norm_stats.json` affine in `PatternOnnxExport` while keeping BatchNorm/Dropout in `PatternNet`; timestamped `training/artifacts/` runs with `pattern_trained.onnx`. See `18-CONTEXT.md`.
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

Last session: 2026-04-19 — Phase 18 execute-phase (training + ONNX + contract check)  
Resume: `/gsd-discuss-phase 19` or `/gsd-plan-phase 19` — Bass Model (BMODEL-01–02); Phase 18 artifacts under `training/artifacts/pattern-gmd-*/` (gitignored)

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
