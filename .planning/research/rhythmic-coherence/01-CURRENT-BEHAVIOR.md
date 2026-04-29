# How the plugin decides things today

> Status: research doc, not a plan. No code changes implied.
> Purpose: document the fundamental decision flow so we can argue about
> what to change. Refer to this when reviewing the four plans that follow.

---

## End-to-end data flow

```
Guitar audio ──▶ [Audio thread, ~5ms blocks] ──┐
                                                ├─▶ OnsetDetector ─▶ BPM (atomic)
                                                ├─▶ EnergyAnalyser ─▶ RMS / centroid / HFflux
                                                ├─▶ StructureTagger ─▶ {SILENT, SOFT, LOUD}
                                                ├─▶ PitchEstimator ─▶ MIDI root + confidence
                                                │
                                                ▼
                                       FeatureVector (7 floats + state)
                                                │
                                                ▼
                                  [Lock-free queue, 4096 capacity]
                                                │
                                                ▼
              [Inference thread, ~50 Hz] ──▶ drainFeatureQueueAndRunInference()
                                                │
                       ┌────────────────────────┼────────────────────────┐
                       ▼                        ▼                        ▼
                StructureNet              PatternNet                 BassNet
                X[1,12,7]→Y[1,3]          X[1,7]→Y[1] int64          X[1,7]→Y[1,32]
                (3-class section)         (pattern index 0-6)        (16 pitch + 16 vel)
                       │                        │                        │
                       ▼                        ▼                        ▼
                effective state           latestPatternIndex       genBassPitchOffsets
                (rule/ML blend)           (atomic, 2-bar hold)     genBassVelocities
                                                                   (2-bar hold)
                                                │                        │
                                                ▼                        ▼
                          [Audio thread] PatternPlayer.process(midi, samples)
                                                │
                                                ▼
                                  Drum MIDI (ch 10) + Bass MIDI (ch 2)
```

## The three independent timing systems

This is the core problem. Three clocks run at three different rates and never coordinate:

| System | Clock | Rate | Source |
|--------|-------|------|--------|
| Audio thread | `hostSampleTime` (sample counter) | ~256 samples (~5ms) | DAW callback |
| Inference thread | wall clock (`sleep_for(20ms)`) | ~50 Hz | OS scheduler |
| Beat clock | `PatternPlayer::beatPosition` | continuous (advances by `samples × bpm / (60 × sr)` per block) | derived from `OnsetDetector::getCurrentBpm()` |

The beat clock derives its rate from `currentBpm`, which is itself updated asynchronously by the inference path's IOI tracker. If BPM changes mid-block, the beat clock starts advancing at the new rate — there's no smooth transition, no phase correction.

`PatternPlayer::beatPosition` is the only thing that knows where the bar boundary is. **The ML models never see this.**

## What each ONNX model actually sees

### PatternNet (drum pattern selector)
- **Input:** `[1, 7]` — `[bpm, rms, centroid, hfFlux, state, pitchRoot, pitchConfidence]`
- **Output:** integer 0–6 (pattern index)
- **Trained on:** GMD + Lakh derived feature/label pairs (per `26-01-PLAN.md`)
- **What it doesn't see:** beat position, what pattern was just playing, what the guitar's rhythm is, history beyond the current 20ms snapshot
- **Decision is point-in-time:** every 20ms, given a 7-number snapshot, pick a pattern. The 2-bar hold guard prevents thrashing but doesn't give the model temporal context.

### BassNet (bass piano-roll generator)
- **Input:** `[1, 7]` — same 7 features as PatternNet
- **Output:** `[1, 32]` — 16 pitch offsets (semitones, relative to root) + 16 velocities
- **What it produces:** a one-bar piano-roll at sixteenth-note resolution
- **Critical assumption in training:** these 16 steps map to one bar of 4/4 starting at beat 1
- **What it doesn't see:** drum pattern, beat position, prior bar
- **The model is doing its job correctly.** The bug is in how the steps are *played back* (see `04-BASS-SEQUENCER-FIX.md`).

### StructureNet (section classifier)
- **Input:** `[1, 12, 7]` — last 12 feature snapshots (~240ms of context)
- **Output:** `[1, 3]` — logits for {SILENT, SOFT, LOUD}
- **Used as:** input to PatternNet selection (blended with rule-based state)
- **Reasonable scope.** The 12-snapshot window gives some temporal context for section detection.

## How the bug expresses itself

The user's jam observations map directly to the architecture:

| Jam observation | Architectural cause |
|----------------|---------------------|
| "Drums groove changes but not musically, doesn't change seamlessly" | Pattern selection is a hard cut at the next bar boundary; no transition logic, no fill |
| "Bass kind of random, not in sync with drums" | `emitGenerativeBassSteps` fires step 0 every audio block; steps 1–15 never play (see plan 04) |
| "Right pitch though" | Pitch estimator + heldPitchRootMidi flows correctly into `genBassStepRootMidi`; that part works |
| "Stable" | Threading model is sound; lock-free queue + atomics behave |
| "System needs to listen better for the tempo set by player" | OnsetDetector requires 8 consistent IOIs to lock; with fuzz, it rarely does |

## Empirical evidence we don't have

We have model contract validators but no record of **what features the model actually sees during a real jam**. Things we can't currently answer:

- During the jam, what was the actual BPM curve over time?
- Did `tempoLocked` ever become true?
- What pattern indices did PatternNet choose, and how often did it want to change vs. how often the 2-bar guard let it?
- What did the BassNet 16-step roll look like for each bar?
- Did pitch confidence stay high enough for the bass to use it?

A small instrumented build that logs `FeatureVector` and model outputs to a CSV during a jam would let us evaluate model decisions empirically rather than by inference.

This becomes part of plan 03 (beat tracker) or a tiny standalone task.

## Reading order for the rest of this folder

1. `02-PRE-FX-SIGNAL-CHAIN.md` — workflow change that simplifies everything below
2. `03-BEAT-TRACKER.md` — replace IOI median with autocorrelation + phase alignment
3. `04-BASS-SEQUENCER-FIX.md` — fix the block-relative emission bug
4. `05-COORDINATION-AND-CONTEXT.md` — unify drum/bass cycle, enrich features, pattern transitions
