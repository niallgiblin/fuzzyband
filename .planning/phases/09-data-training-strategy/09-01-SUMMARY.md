---
phase: 09-data-training-strategy
plan: 01
subsystem: ml
tags: [midi, mido, tokenization, datasets]

requires: []
provides:
  - Dataset audit memo and tokenization contract for Phase 2 prep
  - Runnable Python prep CLI and CI smoke on a checked-in MIDI fixture
affects:
  - Phase 15 (Python training pipeline)
  - Phase 10+ (ONNX / inference)

tech-stack:
  added: [mido 1.3.2]
  patterns: [JSON event stream from Standard MIDI Files]

key-files:
  created:
    - docs/DATASET_AUDIT.md
    - docs/TOKENIZATION.md
    - training/prep_midi.py
    - training/requirements.txt
    - training/README.md
    - training/fixtures/minimal.mid
  modified:
    - README.md
    - .github/workflows/ci.yml

key-decisions:
  - Primary data path documented as GMD + Lakh (drum-filtered); E-GMD not co-primary
  - Event JSON uses absolute time_sec with channel 1–16 and note_on/note_off types

patterns-established:
  - "Prep stub: mido merge_tracks + tempo-aware seconds for TOKENIZATION.md records"

requirements-completed: [DATA-01, DATA-02, DATA-03]

duration: 25min
completed: 2026-04-17
---

# Phase 9: Data & training strategy — Plan 01 Summary

**Dataset audit memo, tokenization spec, mido-based prep CLI, README discovery links, and macOS CI prep smoke on `training/fixtures/minimal.mid`.**

## Performance

- **Duration:** ~25 min
- **Tasks:** 5
- **Files modified:** 8 paths

## Accomplishments

- Locked **go/no-go** and ranked shortlist in `docs/DATASET_AUDIT.md` per 999.1 context
- Defined **event JSON** contract in `docs/TOKENIZATION.md` with `FeatureVector` / strategy bridge pointer
- Shipped **`training/prep_midi.py`** with pinned `mido` and fixture-backed **CI** step

## Task Commits

1. **Task 1: DATA-01 — docs/DATASET_AUDIT.md** — `cba74d3`
2. **Task 2: DATA-02 — docs/TOKENIZATION.md** — `f04594f`
3. **Task 3: Fixture MIDI** — `dfd9b79`
4. **Task 4: DATA-03 — training stub** — `32a47ef`
5. **Task 5: D-09-06 — README + CI** — `ff25b4c`

## Files Created/Modified

- `docs/DATASET_AUDIT.md` — License checklist, exclusions, primary path
- `docs/TOKENIZATION.md` — Event schema and JSON example
- `training/prep_midi.py` — CLI: MIDI → JSON array or JSONL
- `training/requirements.txt` — `mido==1.3.2`
- `training/README.md` — venv and usage
- `training/fixtures/minimal.mid` — SMF smoke input
- `README.md` — Phase 2 prep discovery section
- `.github/workflows/ci.yml` — Python prep after C++ tests

## Decisions Made

- **Time base:** `time_sec` from tempo-aware tick accumulation (handles `set_tempo` mid-file)
- **Channel:** JSON uses MIDI channels **1–16** for readability

## Deviations from Plan

None — plan executed as written.

## Issues Encountered

None.

## Next Phase Readiness

- DATA-01–03 satisfied at documentation + prep stub level; tensor pipelines remain Phase 15

---
*Phase: 09-data-training-strategy*
*Completed: 2026-04-17*
