# Drum/bass coordination, feature enrichment, pattern transitions

> Status: proposal. The biggest of the four — combines three related improvements.
> Depends on plans 03 and 04. Pattern transition handling folded in here.

---

## Three problems, one underlying cause

| Symptom | Cause |
|---------|-------|
| Drums and bass drift apart musically | Independent 2-bar hold guards; not bar-aligned to each other |
| Models pick rhythmically incoherent things | FeatureVector has no bar position, no history |
| Pattern switches feel abrupt | Hard cut at next bar boundary, no transition |

All three trace back to: **the ML models have no awareness of where they are in
the bar, and the playback layer treats drums and bass as independent streams.**

This plan addresses all three.

---

## Problem 1 — Unified update cycle

### Today
```cpp
// In drainFeatureQueueAndRunInference()
const bool drumHoldExpired = (latest.sampleTimestamp - lastDrumPatternChangeSample) >= twoBarsInSamplesDrum;
const bool bassHoldExpired = (latest.sampleTimestamp - lastBassUpdateSample)        >= twoBarsInSamples;
```

`lastDrumPatternChangeSample` and `lastBassUpdateSample` are independent. They
got set at different moments, so the two 2-bar windows are offset from each
other by some arbitrary amount. A drum change happens at sample T₁, the next
bass change at sample T₂ ≠ T₁, so by bar 4 the drum cycle and bass cycle are
~half a bar out of phase.

### Proposed
A single shared "next-update-sample" anchored to actual bar boundaries:

```cpp
// Pseudocode
int64_t nextUpdateAtBar = 0;  // sample index of next bar 2k
if (currentSampleTs >= nextUpdateAtBar) {
    runPatternNet(...);
    runBassNet(...);
    nextUpdateAtBar = currentSampleTs + twoBars;
}
```

Both models run in the same inference tick, at the same bar boundary, every 2 bars.
This requires knowing where bar boundaries are — which is what the beat tracker
(plan 03) provides.

If we want to be even more conservative, update on every 1 bar boundary (faster
musical responsiveness) but only **commit changes at 2-bar boundaries** for
stability. Two-tier: think every bar, act every other bar.

---

## Problem 2 — Feature vector enrichment

### Today
`FeatureVector` is 7 numbers + a state enum. No temporal context.

### Proposed additions

```cpp
struct FeatureVector
{
    // existing 7 fields ...

    // NEW: rhythmic context
    float beatPhaseSin = 0.0f;    // sin(2π × beatInBar / 4)
    float beatPhaseCos = 0.0f;    // cos(2π × beatInBar / 4)
    float tempoConfidence = 0.0f; // [0,1] from BeatTracker

    // NEW: history (most recent prior choices)
    int   prevPatternIndex = 0;   // last pattern PatternNet selected
    float prevBassDensity  = 0.0f; // % of nonzero velocities in last bar
};
```

**Why sin/cos instead of raw beat phase?** Avoids the 0/4 wrap-around
discontinuity. Standard trick for cyclical features in ML.

**Why prior pattern/bass?** Lets the model learn smooth transitions: "if last
pattern was 3, prefer 2/3/4 over 5/6 unless the audio strongly demands it."
This is what makes selections feel musical instead of arbitrary.

**Why tempo confidence?** Rules path can refuse to commit pattern changes when
tempo is uncertain. ML path can use it as a feature.

### Retraining implications

This is the biggest cost of plan 05. The three ONNX models would need:
- Updated input contracts (`X` shape changes from `[1,7]` to `[1,12]` or similar)
- Retraining datasets that include the new features
- All three models retrained
- Contract validators updated
- C++ side updated to pack the new fields

We did exactly this work in Phase 26 (3-class state encoding). The pattern
exists; it's well-understood plumbing. But it's not free — probably a 1-week
sub-task on its own.

**Alternative:** ship the C++-side changes (compute and store the new fields)
without retraining, and have the rules path use them while ML path keeps using
the old 7-field projection. Then retrain in a follow-up phase. This decouples
the architectural work from the model work.

---

## Problem 3 — Pattern transitions

### Today
Pattern A plays for ≥2 bars. PatternNet wants to switch to B.
At next bar boundary, `activePatternIndex = B` and the new pattern starts cold
on beat 1. Hard cut.

### Proposed: transition fills + crossfade

**Decision: fills only (option A), no velocity scaling (option B rejected).**

When a pattern change is committed:
- The current bar continues playing pattern A
- The last beat (or last 2 beats) of that bar plays a fill from a small library
- The next bar starts pattern B at full velocity (no fade)

This is how a human drummer signals a transition. In metal specifically,
drummers don't ease into the new pattern — they hit harder if anything,
so velocity-scaled "fade-in" would sound wrong.

**Fill library: 4 fills picked by transition direction (decision 4.5).**

| Direction | Character | Use case |
|-----------|-----------|----------|
| any → any (generic) | crash-and-go | most transitions |
| soft → loud | build-up | section escalation |
| loud → soft | hat tick / quiet | section drop |
| any → any (dramatic) | tom roll | major structural change |

Picked by direction (current state vs. target state), not by lookup table.
Half a day to compose, easy to expand later.

For bass: when `genBassVelocity[]` changes (new bar), the velocity envelope
naturally provides some smoothing. No special handling needed if plan 04 fixed
the timing bug.

---

## What stays the same

- The IInference interface
- The 7-pattern library (we add fills, don't replace patterns)
- The lock-free queue / atomic handoff
- The threading model

## What changes

- `FeatureVector` gains 5 new fields
- `AccompanimentProcessor` populates the new fields, runs both models in one
  bar-aligned tick
- `PatternPlayer` learns about transition fills and first-bar velocity scaling
- `MidiPatternLibrary` gains a small fill library
- (deferred to follow-up phase) Models retrained on the enriched 12-feature input

## Tests

- Drums and bass change at the same bar boundary, every 2 bars. Verify with
  instrumented test.
- A→B transition fires a fill in the last beat of A. Verify with MIDI inspection.
- First bar of B has scaled-down velocity. Verify max velocity over first bar
  vs. second bar.
- New FeatureVector fields populated correctly: `beatPhaseSin² + beatPhaseCos² ≈ 1`.

## Scope estimate

- **Modified code:** `FeatureVector`, `AccompanimentProcessor` inference loop,
  `PatternPlayer` (fill emission + velocity scaling)
- **New code:** small `TransitionPlanner` helper, ~150 LOC
- **New data:** 4 fill MIDI patterns
- **Tests:** 4–5 new unit/integration tests
- **Estimated effort:** 1 week if model retraining is deferred, 2 weeks if
  bundled with retraining
- **Recommendation:** ship C++ side first, retrain in a separate phase

## Decisions

- ✓ 4.1 Unified bar-aligned update cycle (drums + bass tick together)
- ✓ 4.2 All 5 enriched FeatureVector fields: beatPhaseSin/Cos,
       tempoConfidence, prevPatternIndex, prevBassDensity
- ✓ 4.3 Model retraining = separate follow-up phase (Phase 30)
- ✓ 4.4 Transition fills YES, first-bar velocity scaling NO
- ✓ 4.5 4 fills picked by transition direction

→ Plan 05 splits into Phase 29 (C++ side: coordination + transitions +
  C++ FeatureVector wiring) and Phase 30 (retrain three ONNX heads on
  the enriched 12-feature input).
