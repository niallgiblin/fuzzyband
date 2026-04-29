---
status: complete
quick_id: 260427-bass-pitch-class-tracking
date: 2026-04-27
---

# Quick Task: Bass pitch-class tracking — Summary

**One-liner:** Library-pattern bass offset now uses pitch-class stability (octave-fold tolerant) and ±6 semitone register mapping from E2 — matching `PLAN.md` tasks 1–2 in `AccompanimentProcessor.cpp`.

## Implementation

Verified in `src/AccompanimentProcessor.cpp`: pitch stability uses `currentPc` / `lastPc` modulo-12 comparison; bass offset uses `bassRootPc` + clamped `delta` to ±6 semitones before `setBassSemitoneOffset`.

## Note

`PLAN.md` remained marked in-progress until this audit close; execution predates v0.4 generative/piano-roll bass work and remains the basis for static-pattern bass routing.
