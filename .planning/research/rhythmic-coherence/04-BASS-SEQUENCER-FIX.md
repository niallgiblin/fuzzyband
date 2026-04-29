# Bar-aligned bass step sequencer

> Status: proposal. Concrete bug fix + small redesign of step emission.
> Independent of plans 02 and 03 — could ship first.

---

## The bug, in code

`src/midi/PatternPlayer.cpp` `emitGenerativeBassSteps()`:

```cpp
const int64_t blockStart = sampleCounter;       // current audio block start
const int64_t blockEnd   = blockStart + numSamples;

for (int step = 0; step < 16; ++step)
{
    const int64_t stepOnSample = blockStart + step * stepLenSamples;  // ← bug
    if (stepOnSample < blockStart || stepOnSample >= blockEnd)
        continue;
    // ... emit note
}
```

`stepOnSample = blockStart + step × stepLenSamples`. At 120 BPM and a 256-sample
block, `stepLenSamples ≈ 5500` (one sixteenth note). For `stepOnSample` to fall
inside `[blockStart, blockEnd)` we need `step × 5500 < 256`. Only `step = 0`
satisfies that.

**Result:** every audio block, step 0 fires. Steps 1–15 never play. The bass
becomes "play step 0 every 5–6 ms," which sounds like a buzzy clicking on the
root note. That's why the user heard "right pitch, kind of random, not in sync."

## How the drum path does it correctly (for comparison)

`emitForList()` in the same file:

```cpp
double t = ev.beatOffset;                 // event's position in beats
while (t < beatStart) t += patternLenBeats;

while (t < beatEnd) {
    const double rel = t - beatStart;
    int off = std::round(rel * samplesPerBeat);
    midi.addEvent(... noteOn at off ...);
    t += patternLenBeats;
}
```

This is the right idea: walk the event list, place events at their **beat
position relative to the current `beatStart..beatEnd` window**. The window
moves with `beatPosition`, so each event fires once per loop at the right time.

The bass step sequencer should use the same pattern.

## The fix

Treat the 16 bass steps as a one-bar pattern at sixteenth-note resolution.
Emit them using beat-position math, not block-position math.

```cpp
void emitGenerativeBassSteps(midi, numSamples, beatStart, beatEnd, sampleOffsetBase)
{
    const double samplesPerBeat = 60.0 / bpm * sampleRate;
    const double stepBeats = 0.25;          // sixteenth note in beats
    const double barBeats  = 4.0;           // one bar of 4/4

    // Walk the bar grid: step i fires at beat (i * 0.25) within each bar
    for (int step = 0; step < 16; ++step)
    {
        const float vel = genBassVelocity[step];
        if (vel <= 0.0f) continue;

        // First occurrence of this step at or after beatStart
        double stepBeatPos = step * stepBeats;
        while (stepBeatPos < beatStart) stepBeatPos += barBeats;

        // Emit every occurrence of this step in [beatStart, beatEnd)
        while (stepBeatPos < beatEnd)
        {
            const double rel = stepBeatPos - beatStart;
            int off = std::round(rel * samplesPerBeat);
            off = juce::jlimit(0, numSamples - 1, off);

            // ... emit noteOn / noteOff ...
            stepBeatPos += barBeats;
        }
    }
}
```

This places step 0 at beat 0/4/8/..., step 1 at beat 0.25/4.25/8.25/..., etc.
The 16 steps tile the bar exactly, and the emission window naturally walks the
grid as `beatPosition` advances.

## Two design choices

### Bar-anchor: which beat is "step 0"?
The model was trained on bars of 4/4 starting at beat 1. So step 0 = beat 0
(modulo 4) in the plugin's beat clock. This depends on `PatternPlayer::beatPosition`
being correctly aligned with musical bar 1 — which is exactly what the
beat-tracker plan (03) provides.

If beat-tracker doesn't ship first: step 0 will fire at whatever moment
`snapToBarStart()` happened to set `beatPosition = 0`. That's better than today
(steps 1–15 actually play), but still phase-misaligned with the guitar.
**This plan's quality is bounded by the beat tracker's quality.**

### Note-off handling
Drums use a humanised note-off offset relative to the note-on. The current
bass step code clamps note-off to block end if it overflows, which loses
sustain. The fix should defer the note-off to a future block via
`genBassAbsNoteOffSample` (already a member, currently underused).

## What stays the same

- BassNet inputs and outputs unchanged
- `setGenerativeBassSteps()` signature unchanged
- The piano-roll buffer (`genBassPitchOffsets[16]`, `genBassVelocities[16]`) unchanged
- The `genBassStepsReady` atomic handshake unchanged
- 2-bar update guard in `drainFeatureQueueAndRunInference` unchanged

## What changes

- `emitGenerativeBassSteps()` — rewrite as above
- Note-off deferral: handle multi-step note-offs that span blocks (use a small
  fixed-size queue of pending note-offs, since up to 16 notes per bar)

## Tests

- Unit test: feed a known 16-step pattern (e.g. step 0/4/8/12 = quarters at
  velocity 100), simulate `process()` over one bar, assert exactly 4 note-ons
  at beat positions {0, 1, 2, 3} (with humanisation tolerance ±2ms).
- Unit test: every step velocity > 0 → 16 note-ons per bar at sixteenth
  positions.
- Regression: replay the existing bass tests, confirm no breakage.

## Scope estimate

- **Modified code:** `PatternPlayer::emitGenerativeBassSteps` (rewrite, ~50 LOC)
- **New code:** small note-off queue (~30 LOC)
- **Tests:** 2–3 new unit tests
- **Estimated effort:** 2–3 days

## Decision

- ✓ 3.1 Bundle with beat tracker — ships in same phase as plan 03 (Phase 28)

→ Bass sequencer fix lands when the beat clock is already correct, giving
  cleanest musical result on first listen.
