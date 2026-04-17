# Jam Session Notes (STAB-01)

**Date:** 16-04-26
**Duration:** 10 minutes
**DAW:** Reaper
**Buffer size:** 256 samples, 48 kHz

## Observed Feature Ranges


| Playing Style   | RMS     | Centroid (Hz) | HF Flux  | Reported State                                                                                     |
| --------------- | ------- | ------------- | -------- | -------------------------------------------------------------------------------------------------- |
| Silent          | 0.0033  | 2600-2900     | 0.4-0.6  | the fluctuating centroid and flux are due to fuzz still being on. Silent seems to work well so far |
| Clean picking   | 0.04    | 500-900       | 0.05-0.1 | Seems to always be in BREAKDOWN state while picking                                                |
| Breakdown chug  | 0.4-0.6 | 1000-1400     | 8.0-20.0 | state swaps between VERSE and BREAKDOWN unintelligibly                                             |
| Distorted leads | 0.2-0.3 | 2000-2400     | 4.0-24.0 | state swaps between VERSE and CHORUS frequently and unintelligibly                                 |


## How to assess thresholds (quick reference)

The tagger picks a **desired** state each block in this order (see `StructureTagger::computeDesiredState`):

1. **RMS < kSilentRms** → SILENT (nothing else matters until you’re “loud enough”).
2. Else **centroid < kBreakdownCentroidHz** → BREAKDOWN (“dark / muddy” band).
3. Else **centroid < kVerseCentroidHz** → VERSE (mid band).
4. Else → CHORUS (“bright” band).

**Holds** (`kHold*Sec`) only slow *leaving* a state you’re already in; they don’t change which state is *desired*. Flicker = desired state is jumping **or** holds are short relative to how long you stay in one sonic zone.


| Constant                               | What it does                                           | “Raise” tends to…                                   | “Lower” tends to…                                 |
| -------------------------------------- | ------------------------------------------------------ | --------------------------------------------------- | ------------------------------------------------- |
| **kSilentRms**                         | Quieter than this → SILENT                             | Need more level before you exit SILENT              | Exit SILENT with quieter playing (more sensitive) |
| **kBreakdownCentroidHz**               | Centroid below this → BREAKDOWN                        | Widen BREAKDOWN (more riffs read as chug/mud)       | Narrow BREAKDOWN (only very dark centroids count) |
| **kVerseCentroidHz**                   | Between breakdown line and this → VERSE                | Widen VERSE, shrink CHORUS                          | Widen CHORUS (bright leads easier)                |
| **kHoldVerseSec / Chorus / Breakdown** | Min seconds in that state before a *change* is allowed | States stick longer (less flicker, slower reaction) | React faster (more flicker if desired jumps)      |


**Practical fill-in:** For each line, write **keep** if behavior is acceptable for Phase 1, or **change to X** only when your **Observed Feature Ranges** table shows a **clear mismatch** (e.g. clean picking centroid 500–900 always wants BREAKDOWN because 900 < 1200 — that’s a **boundary** issue, not random noise).

---

## Threshold Assessment

- kSilentRms = 0.05 → keep
- kBreakdownCentroidHz = 1200 → Lower → **applied 1000** in `StructureTagger.h`
- kVerseCentroidHz = 2400 → Lower → **applied 2200** in `StructureTagger.h`
- kHoldVerseSec = 1.5 → Raise → **applied 2.0** in `StructureTagger.h`
- kHoldChorusSec = 2.0 → Raise → **applied 2.5** in `StructureTagger.h`
- kHoldBreakdownSec = 3.0 → Lower → **applied 2.5** in `StructureTagger.h`

## Issues Observed

- <any flicker, misclassification, stuck state, glitch>
- The way it pick up from silence to playing feels pretty solid for this phase. The states flicker often between different ones throughout jamming the same riff. Changing often within the same riff.
- Not a whole lot of musicality in the drums but it is generally responding

---

*Status: **complete** — jam complete signalled; thresholds applied in `StructureTagger.h` (Phase 7 Plan 05 Task 3). Next: CPU profile (resume with `cpu pass` / `cpu fail`), TSan, edge cases, 20-minute stability per `07-05-PLAN.md`.*