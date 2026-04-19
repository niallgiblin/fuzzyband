---
phase: 20-export-promotion
plan: 01
subsystem: infra
tags: [bash, onnx, assets, exp-01]

requires:
  - phase: 18-pattern-model
    provides: pattern_trained.onnx contract and artifact layout
  - phase: 19-bass-model
    provides: bass_trained.onnx contract and artifact layout
provides:
  - Local validate-then-copy path from training artifacts to CMake BinaryData ONNX names
  - Documented Phase 20 install workflow in training README
affects:
  - 20-export-promotion

tech-stack:
  added: []
  patterns:
    - "Shell out to validate_onnx_contract.py before any cp into assets/"

key-files:
  created:
    - scripts/install-model-local.sh
  modified:
    - training/README.md

key-decisions:
  - "Followed CONTEXT: long-option CLI, --copy-stats optional off by default, no structure_model touch"

patterns-established:
  - "REPO_ROOT from BASH_SOURCE; set -euo pipefail matching download-model.sh"

requirements-completed: [EXP-01]

duration: 15min
completed: 2026-04-19
---

# Phase 20 — Export & Promotion (plan 01) Summary

**Bash installer validates pattern and bass ONNX then copies to `assets/accompaniment_model.onnx` and `assets/bass_model.onnx`, with README cross-links to ops scripts.**

## Performance

- **Duration:** ~15 min
- **Started:** 2026-04-19
- **Completed:** 2026-04-19
- **Tasks:** 2
- **Files modified:** 2

## Accomplishments

- Added `scripts/install-model-local.sh` with `--pattern-dir` / `--bass-dir`, optional explicit ONNX paths, `--copy-stats`, and mandatory `validate_onnx_contract.py` gate
- Documented Phase 20 local install in `training/README.md` with example command and ops references

## Task Commits

1. **Task 1: Add scripts/install-model-local.sh** — `30f49fc` (feat)
2. **Task 2: Document Phase 20 in training/README.md** — `f820e0e` (docs)

## Files Created/Modified

- `scripts/install-model-local.sh` — validate-then-copy local install for EXP-01
- `training/README.md` — Phase 20 section before References

## Decisions Made

None beyond plan and `20-CONTEXT.md` — implemented as specified.

## Deviations from Plan

None — plan executed exactly as written.

## Issues Encountered

None

## User Setup Required

None — run script with paths to your `training/artifacts/...` trees after training.

## Next Phase Readiness

Plan 20-02 can author `20-VERIFICATION.md` for Reaper smoke (depends on EXP-01 artifacts in `assets/`).

## Self-Check: PASSED

- `bash -n scripts/install-model-local.sh` — OK
- Acceptance greps from plan — OK
- `test -x scripts/install-model-local.sh` — OK

---
*Phase: 20-export-promotion*
*Completed: 2026-04-19*
