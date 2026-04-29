# Rhythmic Coherence — research and proposals

> Status: pre-milestone discussion docs. Not yet phases.
> Goal: align on approach before committing to v0.5.0 milestone scope.

This folder captures the state-of-the-plugin analysis and the proposed
direction for the next round of improvements. Read in order:

1. **[01-CURRENT-BEHAVIOR.md](01-CURRENT-BEHAVIOR.md)** — How the plugin
   actually decides things today, end-to-end. Architectural and empirical
   context for everything below. No code changes implied.

2. **[02-PRE-FX-SIGNAL-CHAIN.md](02-PRE-FX-SIGNAL-CHAIN.md)** — Workflow
   decision: place plugin BEFORE distortion in the chain. Simplifies plans
   03 and 05 dramatically.

3. **[03-BEAT-TRACKER.md](03-BEAT-TRACKER.md)** — Replace IOI median tempo
   estimation with autocorrelation + DP phase tracking. Drops time-to-lock
   from ~6s to ~2 bars. Provides musical bar alignment.

4. **[04-BASS-SEQUENCER-FIX.md](04-BASS-SEQUENCER-FIX.md)** — Fix the
   block-relative bug where only step 0 of the 16-step bass roll plays.
   Independent of the others; could ship first.

5. **[05-COORDINATION-AND-CONTEXT.md](05-COORDINATION-AND-CONTEXT.md)** —
   Unified bar-aligned drum/bass update cycle, FeatureVector enrichment
   (beat phase, history), pattern transitions with fills.

## Locked milestone shape

```
v0.5.0 — Rhythmic Coherence
  Phase 27: Pre-FX signal chain (docs only)                          [plan 02]
  Phase 28: Beat tracker + bar-aligned bass sequencer (bundled)      [plans 03 + 04]
  Phase 29: Coordinated drum/bass + transitions (C++ side)           [plan 05a]
  Phase 30: Retrain models on enriched FeatureVector                 [plan 05b]
```

Plans 03 and 04 merged into Phase 28 (decision 3.1). Plan 05 split into a
C++ phase and a retraining phase (decision 4.3).

## Resolved decisions

**Pre-FX (plan 02):**
- ✓ 1.1 Pre-fuzz is the recommended workflow
- ✗ 1.2 Signal-health indicator skipped for now (revisit later)
- ✓ 1.3 README + help text update is in scope

**Beat tracker (plan 03):**
- ✓ 2.1 Autocorrelation + dynamic programming approach (not neural)
- ✓ 2.2 Target latency: 1 bar to first commit, 2 bars to confident
- ✓ 2.3 Replace count-in onset gate with tracker confidence (single source of truth)

**Bass sequencer (plan 04):**
- ✓ 3.1 Bundle with beat tracker — ships in same phase as plan 03

**Coordination (plan 05):**
- ✓ 4.1 Unified bar-aligned update cycle (drums + bass tick together)
- ✓ 4.2 All five FeatureVector additions: beatPhaseSin/Cos, tempoConfidence,
       prevPatternIndex, prevBassDensity
- ✓ 4.3 Model retraining is a separate follow-up phase (Phase 30)
- ✓ 4.4 Transition fills YES; first-bar velocity scaling NO
- ✓ 4.5 4 fills, picked by transition direction (any→any, soft→loud,
       loud→soft, dramatic any→any)

## Next step

Folder is ready to be converted into a v0.5.0 milestone. The conversion would
spawn 4 phase scaffolds in `.planning/phases/`. The proposal docs here become
historical context — actual PLAN.md files get generated per-phase via
`/gsd-plan-phase` once the milestone is started.
