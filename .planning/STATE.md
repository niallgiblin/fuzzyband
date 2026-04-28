---
gsd_state_version: 1.0
milestone: v0.4.0
milestone_name: ML Playability & Simplification
status: roadmapped
last_updated: "2026-04-28T00:00:00.000Z"
last_activity: 2026-04-28
progress:
  total_phases: 6
  completed_phases: 2
  total_plans: 3
  completed_plans: 3
  percent: 33
---

# STATE — Metal Accompaniment

> Project memory. Updated at phase transitions and milestone boundaries.

## Project Reference

See: `.planning/PROJECT.md` (updated 2026-04-28)

**Version string in the plugin UI** comes from **`CMakeLists.txt`** `project(MetalAccompaniment VERSION …)` (JUCE `ProjectInfo::versionString`). It should move with milestone checkpoints; **GSD phase/milestone authority** is this file plus **`.planning/ROADMAP.md`** (not `ROADMAP_PHASE_1.md`, which is the historical Phase 1 doc only).

**Core value:** A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.  
**Current focus:** Phase 23 — C++ Inference Layer. Run `/gsd-plan-phase 23` to begin.

---

## Current Position

Phase: 22 — ONNX Contract + Stubs (complete)
Plan: 22-01
Status: Complete — ONNX contract docs, stubs, validator, tests, CI gate all locked
Last activity: 2026-04-28 — Phase 22 execution complete

---

## Shipped milestones (summary)

| Milestone | Phases | Archive |
|-----------|--------|---------|
| v0.1.0 | 1–8 | `.planning/milestones/v0.1.0-ROADMAP.md` |
| v0.2.0 | 9–16 | `.planning/milestones/v0.2.0-ROADMAP.md` |
| v0.3.0 | 17–20 | `.planning/milestones/v0.3.0-ROADMAP.md` |

---

## v0.4.0 — Current Milestone

| Phase | Name | Requirements | Status |
|-------|------|--------------|--------|
| 21 | C++ Type Foundation | TYPE-01, TYPE-02 | Complete |
| 22 | ONNX Contract + Stubs | ONNX-04, ONNX-05 | Complete |
| 23 | C++ Inference Layer | INF-01, INF-02 | Not started |
| 24 | UI Simplification | UI-01, UI-02 | Not started |
| 25 | Training Data Pipeline | DATA-07, DATA-08, DATA-09 | Not started |
| 26 | Retrain + Validate | PMODEL-04, BMODEL-03, STRUC-04, PVAL-01 | Not started |

**Execution order:** 21 → 22 → 23 → 24 (sequential). Phase 25 depends on Phase 21 and can run in parallel with 22–24. Phase 26 depends on both Phase 23 and Phase 25.

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

### Key architectural constraints (v0.4.0)

- TYPE-01 and TYPE-02 (Phase 21) must complete before any model retrain — all 3 ONNX models silently fail on shape mismatch if state encoding is wrong; shape assertions in INF-01 prevent silent catch fallback
- Phase 21 + Phase 22 must be in the same execution window as model retrains (Phase 26) — running the plugin with ONNX enabled between them risks silent fallback
- Genre APVTS removal (UI-01) must touch all 4 files atomically — partial removal crashes on session load; dedicated Phase 24 enforces this
- `docs/ONNX_IO.md` (ONNX-04, ONNX-05) is the source of truth for all C++ inference changes — update contract docs before touching `OnnxInference` C++
- Lakh data pipeline (Phase 25) depends on Phase 21 3-class label encoding but is otherwise independent of Phases 22–24
- `validate_onnx_contract.py` (all three flags: `--pattern`, `--bass`, `--structure`) is a mandatory pass gate in Phase 26

### Key architectural constraints (v0.3.0, preserved)

- `training/prep_midi.py` must NOT be modified — Phase 17 imports `_events_from_file` from it
- Normalization must be baked INTO the `PatternOnnxExport` ONNX wrapper — plugin sends raw `FeatureVector` values
- Class histogram audit (≥50 examples per pattern class 0–6) is a Phase 17 gate before training
- `scripts/validate_onnx_contract.py` is a mandatory pass gate in Phases 18, 19, and 20
- Bass model uses synthetic metal-key pitch data (GMD is drum-only — no external bass corpus)
- All `src/` C++ is frozen — trained models slot into existing `assets/accompaniment_model.onnx` path
- Pin opset 17 in all ONNX exports; `onnxruntime==1.20.1` in training must match C++ ORT version

### Decisions Log

- **2026-04-28 — v0.4.0 roadmap created:** 6 phases (21–26), 15 requirements mapped, all coverage validated. StructureState→3-value and genre removal sequenced ahead of data/retrain work to eliminate silent ONNX shape failures.
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

Last session: 2026-04-28 — v0.4.0 roadmap created (phases 21–26, 15/15 requirements mapped)  
Resume: `/gsd-plan-phase 23` — C++ Inference Layer

---

### Quick Tasks Completed

| # | Description | Date | Commit | Directory |
|---|-------------|------|--------|-----------|
| 260420-421 | Implement tempo and bass stability fixes | 2026-04-20 | b2d0f5d | [260420-421-tempo-bass-stability-fixes](./quick/260420-421-tempo-bass-stability-fixes/) |
| 260420-422 | Rebrand UI to fuzzyband with Pacific NW mossy aesthetic | 2026-04-20 | 22ac48d | [260420-422-fuzzyband-ui-rebrand](./quick/260420-422-fuzzyband-ui-rebrand/) |
| 260427-t42 | Drum count-in gate (4 onsets) and 2-bar pattern hold | 2026-04-27 | 09a92c4 | [260427-t42-drum-timing-countin-gate-2bar-hold](./quick/260427-t42-drum-timing-countin-gate-2bar-hold/) |
| 260427-t43 | BPM lock-in, 5-BPM quantization, 80ms onset refractory | 2026-04-27 | 1c02d3e | [260427-t43-onset-tempo-stability](./quick/260427-t43-onset-tempo-stability/) |

---

## Initialized

2026-04-16 — GSD project initialized by importing existing Phase 1 work.
