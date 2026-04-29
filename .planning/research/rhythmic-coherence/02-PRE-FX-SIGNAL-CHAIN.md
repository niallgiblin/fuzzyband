# Pre-FX signal chain — proposal

> Status: proposal. Discussion-grade. No code yet.
> Decision needed before plans 03–05 are scoped, since this changes the
> input distribution all three plans assume.

---

## The decision

**Place the plugin BEFORE the fuzz/distortion in the signal chain.**

```
Recommended:    Guitar ─▶ MetalAccompaniment ─▶ Fuzz/Amp ─▶ Output
Today's mental model: Guitar ─▶ Fuzz/Amp ─▶ MetalAccompaniment ─▶ Output  ❌
```

This matches industry practice for guitar-following systems (Roland GK,
BIAS, every commercial guitar-to-MIDI converter). The plugin only analyses
audio — it doesn't pass distorted tone — so placing it pre-fuzz costs nothing
sonically. The user still hears their full fuzz tone downstream.

## Why this dramatically simplifies plans 03 and 05

| Sub-system | On clean signal | On post-fuzz signal |
|------------|----------------|---------------------|
| Onset attack | Clear transient peak | Compressed, sustains look identical |
| Pitch (YIN) | Reliable for monophonic playing | Odd-order harmonics confuse F0 |
| RMS dynamics | 30–40 dB swing | 4–6 dB swing (fuzz limits) |
| Spectral centroid | Tracks string position / pickup choice | Locked into harmonic cloud |
| HF flux | Tracks pick attack | Constant noise floor |

Every input feature in `FeatureVector` becomes more informative. PatternNet
and BassNet don't need to be retrained for this — they were trained on
relatively clean MIDI-derived features anyway. The shift is on the input
side: the audio analysers feed them better numbers.

## What this requires (docs-only, decision 1.2 skipped UI work)

### Documentation
- README + plugin help text update: "Place before distortion in your chain"
- DAW screenshots showing recommended routing in Reaper / Logic / Ableton
- Brief explainer of why (frame it as the standard pro approach, not as a
  workaround for fuzz incompatibility)

### Code
- No functional changes. The plugin already works on whatever audio it's
  fed; the change is operational guidance.
- Signal-health UI indicator is **deferred** (decision 1.2) — can be revisited
  in a later polish phase if users actually need it.

## Live workflow note

For users playing live with hardware fuzz pedals, the workflow is:
- DI box with split output → one path to plugin (clean), one to fuzz pedal → amp
- Or: amp's effects loop SEND tap before the fuzz block

This is a standard guitar synth setup. Worth one paragraph in the docs.

## What if someone runs it post-fuzz anyway?

The plugin won't crash. Tempo lock will be unreliable, pitch will drift,
dynamics will be flat. The behaviour is the same as today — we're not
making it worse for that user, we're making the recommended path much better.

The "Signal health" indicator is the user feedback mechanism for "you're
doing it wrong." Better than silent degradation.

## Decisions

- ✓ 1.1 Pre-fuzz is the recommended workflow
- ✗ 1.2 Signal-health indicator deferred (not in this phase)
- ✓ 1.3 README + help text update in scope

→ Plan 02 is now a docs-only plan (~half day). Becomes Phase 27 in the milestone.
