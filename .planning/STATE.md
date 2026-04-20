---
gsd_state_version: 1.0
milestone: pending
milestone_name: Next milestone (undefined)
status: between_milestones
last_updated: "2026-04-19T23:59:00.000Z"
last_activity: 2026-04-19
progress:
  total_phases: 0
  completed_phases: 0
  total_plans: 0
  completed_plans: 0
  percent: 0
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-19)

**Version string in the plugin UI** comes from **`CMakeLists.txt`** `project(MetalAccompaniment VERSION …)` (JUCE `ProjectInfo::versionString`). It should move with milestone checkpoints; **GSD phase/milestone authority** is this file plus **`.planning/ROADMAP.md`** (not `ROADMAP_PHASE_1.md`, which is the historical Phase 1 doc only).

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** Define the next milestone (`/gsd-new-milestone`). **Last shipped:** v0.3.0 — Real ML Training Pipeline (Phases 17–20), 2026-04-19.

---

## Current Position

**Phase:** — (milestone closed; phase history under `.planning/phases/` and milestone archives)  
**Milestone:** Pending — run `/gsd-new-milestone`  
**Last shipped:** v0.3.0 — Real ML Training Pipeline (Phases 17–20), 2026-04-19  
**Status:** Between milestones — requirements reset; roadmap lists next milestone as TBD  
**Last activity:** 2026-04-19 — `/gsd-complete-milestone` (v0.3.0)

```
Progress: (no active milestone — start with /gsd-new-milestone)
```

---

## Shipped milestones (summary)

| Milestone | Phases | Archive |
|-----------|--------|---------|
| v0.1.0 | 1–8 | `.planning/milestones/v0.1.0-ROADMAP.md` |
| v0.2.0 | 9–16 | `.planning/milestones/v0.2.0-ROADMAP.md` |
| v0.3.0 | 17–20 | `.planning/milestones/v0.3.0-ROADMAP.md` |

---

## v0.3.0 (archived reference)

| Phase | Name | Requirements | Status |
|-------|------|--------------|--------|
| 17 | Data Pipeline | DATA-04, DATA-05, DATA-06 | Complete |
| 18 | Pattern Model | PMODEL-01, PMODEL-02, PMODEL-03 | Complete |
| 19 | Bass Model | BMODEL-01, BMODEL-02 | Complete |
| 20 | Export & Promotion | EXP-01, EXP-02 | EXP-01 complete; EXP-02 human checklist in `20-VERIFICATION.md` (optional pass before release tag) |

**Parallelization note:** Phase 19 could run concurrently with Phase 18 — both depend only on Phase 17 tensors.

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

- **2026-04-19 — Phase 20 discuss-phase:** `install-model-local.sh` with `--pattern-dir` / `--bass-dir`, validate-then-copy to `assets/accompaniment_model.onnx` + `assets/bass_model.onnx`, optional `--copy-stats`; Reaper smoke via three `intensity` settings and **Pattern:** readout (≥2 distinct indices, non-stuck); milestone close local-only (no mandatory CI/S3). See `20-CONTEXT.md`.
- **2026-04-19 — Phase 19 execute-phase:** Synthetic bass tensors (E2/A2/B1 balance), MSE + val early stop, `bass_trained.onnx` opset 17 with `X_bass`/`Y_bass`; contract check passes. See `19-CONTEXT.md`, `19-VERIFICATION.md`.
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
| release | Push `v0.3.0` tag to `origin` (after EXP-02 human pass if gating release) | Pending |
| release | Create Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md` | Pending |
| verification | EXP-02: Reaper smoke per `.planning/phases/20-export-promotion/20-VERIFICATION.md` | Pending (human) |
| verification | Optional Reaper routing smoke-test (OUT-05) | Optional |

---

## Session Continuity

Last session: 2026-04-19 — v0.3.0 milestone archived (`/gsd-complete-milestone`)  
Resume: `/gsd-new-milestone` — define next version, requirements, and roadmap

---

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260420-421 | Implement tempo and bass stability fixes | 2026-04-20 | b2d0f5d | [260420-421-tempo-bass-stability-fixes](./quick/260420-421-tempo-bass-stability-fixes/) |

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
