# Beat tracker — replace IOI median with autocorrelation + phase alignment

> Status: proposal. Outline of approach, scope, and risks.
> Depends on `02-PRE-FX-SIGNAL-CHAIN.md` (clean signal assumption).

---

## What we have today

`OnsetDetector` does two jobs:
1. **Onset detection** — spectral flux > adaptive threshold → mark an onset
2. **Tempo estimation** — collect inter-onset intervals (IOIs), median-filter, fold octaves

Tempo is "locked" only when 8 consecutive IOIs agree within ±8%. Until lock,
BPM hovers near 120. Phase alignment ("where is beat 1?") is set by
`snapToBarStart()` firing at onset #4 of the count-in.

### Why this fails

| Problem | Symptom |
|---------|---------|
| 8-onset consistency check is brittle | Tempo never locks during normal expressive playing |
| IOI median assumes notes ARE the beats | Sixteenth-note runs report tempo as 4× actual |
| Octave fold is heuristic | Half-time / double-time confusions on syncopated input |
| Phase = "snap to onset #4" | Plugin's beat 1 lands at an arbitrary moment in your phrase |
| 5-BPM quantisation | Drift accumulates if actual tempo isn't a multiple of 5 |

## What to build

A standard MIR beat tracker, in two layers:

### Layer 1: Onset Strength Function (OSF)
Reuse the spectral flux work that `OnsetDetector` already does. Don't pick
discrete onsets — keep the continuous flux signal as a 1-D function over time
(decimated to ~100 Hz). This is the OSF — `flux(t)` not `onset(t_i)`.

### Layer 2: Tempo via autocorrelation
Compute autocorrelation of OSF over a window (e.g. 4–8 seconds). The
peak in the lag range corresponding to 80–220 BPM is the tempo period.

Variants worth considering:
- **Comb-filter bank** at candidate tempos (60..240 BPM step 1) — pick the comb
  that maximally aligns with OSF. Robust, classic approach (Scheirer 1998,
  Klapuri 2006).
- **Tempogram** — log-spaced autocorrelation; better at handling tempo changes
  but more expensive.
- **DP beat tracker** (Ellis 2007) — use OSF + estimated tempo to find the
  optimal beat sequence via dynamic programming. Gives phase alignment for free.

For our use case (real-time, monophonic guitar input, 80–220 BPM), the comb-filter
bank + DP phase tracking gives a good balance: locks fast, handles small tempo
drift, exposes phase explicitly.

### Layer 3: Phase alignment
Once tempo period `T` is estimated, find the beat phase by:
- Convolving OSF with a comb of period `T` over the recent window
- The peak position (mod T) is the beat phase
- This gives "beat 1 happens at sample index X" — phase-aligned to the player's
  actual rhythmic intent

Key advantage: this works even when the count-in onset happens at beat 3 of a
phrase. The DP tracker finds the underlying beat grid regardless of where the
player happens to start.

## Time-to-lock target (decision 2.2: aggressive)

Currently: ~6 seconds before plugin joins (4-onset count-in + 8-onset lock).

Target:
- 0–1 bar: gather OSF, drums silent
- 1 bar: emit first BPM estimate; drums join with low-confidence flag
- 2 bars: tempo committed, drums fully confident

DP layer can retroactively refine if the first guess turns out wrong, since
the beat clock is a derived quantity.

## What stays the same

- The audio thread / inference thread split
- Lock-free queue, atomic BPM handoff
- `PatternPlayer::beatPosition` stays as the master beat clock
- `OnsetDetector` keeps producing OSF; discrete onset events still useful
  for general signal analysis

## What changes (decision 2.3: replace count-in)

- `OnsetDetector::medianIoiBpm()` → replaced with `BeatTracker::estimateTempo()`
- Add `BeatTracker::beatPhase()` returning the sample offset of the next beat
- Add `BeatTracker::confidence()` returning [0,1]
- **Count-in semantics replaced.** No more "4 onset" gate. Drums join when
  `BeatTracker::confidence() >= threshold` (single source of truth — by
  definition this means we know both tempo and phase)
- `snapToBarStart()` callers use `beatPhase()` instead of "snap when count-in done"
- BPM quantisation removed (or relaxed to nearest 0.5 BPM for display only)

## Risks / open questions

- **CPU budget.** Comb-filter bank @ 100 Hz OSF over 4-second window with 80–220
  BPM resolution is roughly 140 × 400 = 56k mults per frame at 100 Hz. About
  5.6M mults/sec — fine on M-series. Verify before committing.
- **Tempo changes.** Autocorrelation lags. If the player suddenly halves tempo,
  the tracker will follow but takes a window's worth to converge. Acceptable for
  Phase 1, can revisit.
- **What if the player plays straight quarters with no ornament?** OSF is sparse.
  Need to ensure the comb-filter still finds the period. Probably fine — clean
  quarter-note attacks give strong OSF peaks.
- **Empirical validation.** Need a test corpus. Could synth-generate clean
  guitar at known BPMs, or capture real DI playing with a click track ground truth.

## Scope estimate

- **New code:** `src/analysis/BeatTracker.h/.cpp` (~400 LOC)
- **Modified code:** `OnsetDetector` (decimated OSF output), `AccompanimentProcessor`
  (use BeatTracker for BPM + phase), `PatternPlayer::snapToBarStart` semantics
- **Tests:** unit tests for tempo accuracy on synthetic OSF, integration test
  for "play 8 quarters at 110 BPM, expect lock within 2 bars"
- **Estimated effort:** 1–2 weeks of focused work

## Decisions

- ✓ 2.1 Autocorrelation + dynamic programming (not neural)
- ✓ 2.2 1 bar to first commit, refined over next bar (aggressive target)
- ✓ 2.3 Replace count-in onset gate entirely — gating uses tracker confidence

→ Plan 03 + plan 04 (bass sequencer) bundled into Phase 28 per decision 3.1.
