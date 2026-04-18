---
status: passed
phase: 17-data-pipeline
updated: 2026-04-18
---

# Phase 17 — Verification

## Must-haves (from plans)

| Requirement | Evidence |
|-------------|----------|
| DATA-04 | `training/download_gmd.py`, `training/data/` gitignored, `training/requirements.txt` pins, manifest + SHA-256 gate |
| DATA-05 | `training/FEATURE_PROXY.md` — ONNX column order, pattern indices 0–6, domain gap, hybrid oracle |
| DATA-06 | `training/build_dataset.py` — TFDS load, `_events_from_file`, `GroupShuffleSplit`, `.pt` + `norm_stats.json` + manifests, HISTOGRAM + `sys.exit(3)` |

## Automated checks run

- `python3 -m py_compile training/download_gmd.py` — pass
- `python3 -m py_compile training/build_dataset.py` — pass
- Plan acceptance greps for `17-01`–`17-03` — pass

## Human / follow-up

- Run **`python3 training/download_gmd.py`** then **`python3 training/build_dataset.py`** on the **full** train split (no `--max-examples`) and confirm stdout ends with **`PASS: HISTOGRAM`** (all classes ≥ 50). If the gate fails, adjust oracle constants per `FEATURE_PROXY.md` (no automatic oversampling in code).

## Verdict

**status:** `passed` — implementation artifacts match PLAN.md; full-corpus histogram remains a **local validation** step.
