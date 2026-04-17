#  Edge Case Report (STAB-03)

**Status:** COMPLETE — manual DAW checks after jam tuning.

**Date:** 17-04-26
**DAW:** Reaper
**Plugin:** Metal Accompaniment Release build (tuned constants, commit 46213ea)

## Results

| Check | Scenario | Pass / Fail | Notes |
|---|---|---|---|
| A | Bypass flush (toggle FX bypass mid-play; MIDI stops within one block; no stuck notes) | Pass | Seems very responsive and works well already|
| B | Sample rate change (e.g. 44.1 ↔ 48 kHz; no crash, resumes within 1s) | Pass | Very smooth |
| C | Buffer size change (256 → 128 → 512 → 256; no crash; xruns stop within 2s) | Pass | Very smooth also |
| D | Transport stop/start (play → stop → play; no stuck notes on restart) | pass | |
| E | Rapid buffer toggle (three toggles in 5s; no crash; < 100ms audio gap) | pass | |

## Stuck Notes Observed

none

## Crashes Observed

none

## STAB-03 Disposition

All four named cases (A–D) must pass for STAB-03 to be satisfied. Check E is bonus coverage of Pitfall 1 (rapid `prepareToPlay`).

- **Pass / Fail:** pass

---

_Fill per Plan 05 Task 6. Resume signal: `edge cases pass` when A–D all pass, or `edge case X failed` with repro._
