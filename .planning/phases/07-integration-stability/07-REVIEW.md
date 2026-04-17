---
phase: 07-integration-stability
reviewed: 2026-04-17T00:00:00Z
depth: standard
files_reviewed: 9
files_reviewed_list:
  - src/AccompanimentEditor.cpp
  - src/AccompanimentEditor.h
  - src/AccompanimentProcessor.cpp
  - src/AccompanimentProcessor.h
  - src/analysis/StructureTagger.cpp
  - src/analysis/StructureTagger.h
  - src/midi/MidiPatternLibrary.cpp
  - tests/test_pattern_player.cpp
  - tests/test_structure_tagger.cpp
findings:
  critical: 1
  warning: 6
  info: 6
  total: 13
status: addressed
addressed: 2026-04-17
---

# Phase 7: Code Review Report

**Reviewed:** 2026-04-17T00:00:00Z
**Depth:** standard
**Files Reviewed:** 9
**Status:** addressed (CR-01 … WR-06 fixed in code; see Resolution)

## Resolution

**2026-04-17** — All **critical** and **warning** items were implemented:

| ID | Action |
|----|--------|
| CR-01 | Scalar `std::isfinite` scrub before SIMD clip in `processBlock` |
| WR-01 | `cachedSampleRate` is `std::atomic<double>` with release/acquire |
| WR-02 | `inferencePaused` defaults true; cleared at end of `prepareToPlay` |
| WR-03 | `processBlockBypassed` copies mono → stereo for dry pass-through |
| WR-04 | `PatternPlayer::reset()` documented as real-time safe |
| WR-05 | Acquire/release on `latestPatternIndex` and `debugPreviewSamplesRemaining` where coordinated |
| WR-06 | Positive sample rate enforced in `StructureTagger`, `OnsetDetector`, `EnergyAnalyser` (and `prepareToPlay` wiring) |

Project `ARCHITECTURE.md` threading and data-flow sections were updated to match. Info-level items (IN-01 … IN-06) were not required for closure.

## Summary

Phase 7 (Integration & Stability) is in good shape overall. Real-time audio safety
discipline is consistent: no locks, no heap allocation on the audio thread, atomics
use explicit memory ordering where inference and UI coordinate with the audio thread, and the SPSC queue is sized up-front. The recent D-03
(pause inference during reprepare), D-04 (flush MIDI on bypass), and D-07
(immediate exit from SILENT) fixes look correct.

The former NaN/Inf concern in `processBlock` is addressed by clearing non-finite samples before `FloatVectorOperations::clip`. Threading items from the review (sample rate for UI, inference lifecycle, bypass audio, memory ordering, `prepare(0)` guards) are implemented as in the Resolution table.

No heap allocation or locking was detected on the audio thread. No hardcoded
secrets, dangerous functions, or command-injection surfaces were found.

## Critical Issues

### CR-01: NaN/Inf "clip guard" does not reliably remove NaN

**File:** `src/AccompanimentProcessor.cpp:117`
**Issue:** The comment advertises this line as a "NaN/inf clip guard":

```cpp
juce::FloatVectorOperations::clip(in, in, -2.0f, 2.0f, numSamples);
```

`clip` is implemented with SIMD `min`/`max` (SSE `minps`/`maxps`, NEON `vminq`/`vmaxq`).
IEEE 754 min/max behaviour with a NaN operand is platform- and instruction-dependent:
on x86, `minps(NaN, x)` returns the second operand, so `clip(NaN, -2, 2)` becomes
`min(max(NaN, -2), 2) = min(-2, 2) = -2` — NaN is scrubbed. But `max(NaN, -2)` on
some implementations returns NaN, in which case the whole expression remains NaN.
Inf is handled correctly because `min/max(+Inf, 2) = 2` on all conforming hardware,
but NaN is not portable.

Downstream, `onsetDetector.process(in, …)` feeds the FFT, and `energyAnalyser`
computes `sqrt(mean(in^2))`. A single NaN sample contaminates the running RMS and
the spectral centroid for the lifetime of the session (NaN is sticky through
accumulators).

**Fix:** Replace clip with an explicit NaN/Inf sanitiser before clipping. Example:

```cpp
// Scrub NaN/Inf first, then clip to sane range.
for (int i = 0; i < numSamples; ++i)
{
    const float s = in[i];
    if (! std::isfinite(s))
        in[i] = 0.0f;
}
juce::FloatVectorOperations::clip(in, in, -2.0f, 2.0f, numSamples);
```

A SIMD-friendly branchless alternative: `in[i] = std::isfinite(s) ? s : 0.0f;`.
Cost is a few ns per block at 256 samples — negligible versus the FFT that follows.

## Warnings

### WR-01: `cachedSampleRate` is read on the UI thread without synchronisation

**File:** `src/AccompanimentProcessor.cpp:188` (read) and `src/AccompanimentProcessor.cpp:48` (write)
**Issue:** `bumpDebugPattern()` is called from the editor button's `onClick`
(message thread) and computes `std::round(5.0 * cachedSampleRate)`. `cachedSampleRate`
is a plain `double` written from `prepareToPlay` on the audio thread. A torn read
is unlikely on x86_64 (aligned 8-byte doubles are atomic in practice), but this is
UB by the C++ memory model and will be a real data-race flag under TSAN.

**Fix:** Make `cachedSampleRate` a `std::atomic<double>`, or snapshot the rate into
an atomic dedicated for UI use:

```cpp
// header
std::atomic<double> cachedSampleRate{ 44100.0 };

// prepareToPlay
cachedSampleRate.store(sampleRate, std::memory_order_release);

// bumpDebugPattern
const double sr = cachedSampleRate.load(std::memory_order_acquire);
const int dur = juce::jmax(1, static_cast<int>(std::round(5.0 * sr)));
```

### WR-02: Inference thread starts before `prepareToPlay`

**File:** `src/AccompanimentProcessor.cpp:25-27`
**Issue:** The constructor starts `inferenceThread` with `inferencePaused = false`.
If the host calls `createEditor` or any hot-path before `prepareToPlay`, the
inference loop will spin and call `inference->selectPattern(latest)` on a
`RuleBasedInference` whose `prepare(sampleRate)` has not yet run. Today
`RuleBasedInference::prepare` is presumably a no-op, so this is latent rather than
active — but it violates the contract documented in `CLAUDE.md` ("prepare()
methods verify sample rate …") and will bite Phase 2 when `OnnxInference::prepare`
loads the model.

**Fix:** Default `inferencePaused{ true }` in the header, and only set it to
`false` at the end of `prepareToPlay`. This also removes the ordering subtlety
around the release store in the constructor.

```cpp
// header
std::atomic<bool> inferencePaused{ true };
```

### WR-03: `processBlockBypassed` drops dry signal

**File:** `src/AccompanimentProcessor.cpp:205-213`
**Issue:** The bypass handler clears MIDI and resets the pattern player (good for
D-04), but it does nothing with the audio buffer. Most hosts expect bypass to pass
input to output so the guitar signal keeps flowing. As written, toggling bypass
will cause the guitarist's own signal to disappear on the plug-in's output bus.

```cpp
void AccompanimentProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& /*buffer*/, juce::MidiBuffer& midi)
```

**Fix:** Copy mono input to stereo output before clearing MIDI, mirroring the
non-bypass path but without analysis or gain:

```cpp
void AccompanimentProcessor::processBlockBypassed(
    juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    for (int ch = 1; ch <= 16; ++ch)
        midi.addEvent(juce::MidiMessage::allNotesOff(ch), 0);
    patternPlayer.reset();

    // Pass guitar through so host bypass behaves as expected.
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() >= 2 && n > 0)
    {
        auto* l = buffer.getWritePointer(0);
        auto* r = buffer.getWritePointer(1);
        juce::FloatVectorOperations::copy(r, l, n);
    }
}
```

Confirm expected bypass behaviour in Reaper and Logic before shipping — some
hosts route around the plug-in on hard bypass and this method is only called on
soft bypass.

### WR-04: `PatternPlayer::reset()` called from bypass / prepareToPlay may not be real-time safe on some paths

**File:** `src/AccompanimentProcessor.cpp:53, 212`
**Issue:** `patternPlayer.reset()` is called from both `prepareToPlay` (not
real-time critical) and `processBlockBypassed` (real-time critical). The header
only shows the declaration; if `reset()` clears a `std::vector` or otherwise
touches heap-allocated state that `process()` also touches, a future maintainer
could introduce an RT violation. Not a bug today, but an invariant worth asserting.

**Fix:** Add a comment on `PatternPlayer::reset` stating "must be real-time safe"
and cover it with a TSAN-backed test that calls `reset()` and `process()` from
different threads. Also consider moving the `reset()` call in `processBlockBypassed`
behind a latch check so repeated bypass blocks do not re-reset every call.

### WR-05: Inference loop uses relaxed ordering for coordinated state

**File:** `src/AccompanimentProcessor.cpp:92-96`
**Issue:**

```cpp
if (got && inference && debugPreviewSamplesRemaining.load(std::memory_order_relaxed) <= 0)
{
    const int idx = inference->selectPattern(latest);
    latestPatternIndex.store(idx, std::memory_order_relaxed);
}
```

`debugPreviewSamplesRemaining` is written from the audio thread (decrement) and
the UI thread (start preview); `latestPatternIndex` is written from both the
inference thread and the UI thread (`bumpDebugPattern`). Using `relaxed` means a
preview started on the UI thread can race the inference thread's next store and
be overwritten even while `debugPreviewSamplesRemaining > 0` from the inference
thread's point of view.

**Fix:** Use `acquire` on the preview-remaining load and `release` on both stores
to `latestPatternIndex`, so the preview-count write on the UI thread happens-before
the pattern-index read on the inference thread:

```cpp
// UI thread
debugPreviewSamplesRemaining.store(dur, std::memory_order_release);
latestPatternIndex.store(next, std::memory_order_release);

// inference thread
if (debugPreviewSamplesRemaining.load(std::memory_order_acquire) <= 0)
    latestPatternIndex.store(idx, std::memory_order_release);

// audio thread read
const int patternIdx = latestPatternIndex.load(std::memory_order_acquire);
```

### WR-06: `StructureTagger::prepare` does not defend against `sampleRate == 0`

**File:** `src/analysis/StructureTagger.cpp:3-8`
**Issue:** Hosts occasionally call `prepareToPlay(0, 0)` during scan or layout
negotiation. If `sampleRate` is stored as 0, `update()` computes
`blockSec = numSamples / 0.0 = +inf`, and `timeInStateSec` becomes Inf forever.
Every hold check then passes immediately and state flickers on every block.

```cpp
void StructureTagger::prepare(double newSampleRate)
{
    sampleRate = newSampleRate;  // no validation
    ...
}
```

**Fix:** Clamp to a safe minimum:

```cpp
sampleRate = (newSampleRate > 0.0) ? newSampleRate : 44100.0;
```

Same defensive check belongs in `OnsetDetector::prepare` and
`EnergyAnalyser::prepare` if they do not already have it (not in scope of this
review but worth verifying in a follow-up).

## Info

### IN-01: `in` and `outL` alias — in-place loop is correct but confusing

**File:** `src/AccompanimentProcessor.cpp:112, 119-120, 165-171`
**Issue:** `in`, `outL` are both taken from `getWritePointer(0)`. The pass-through
loop writes `outL[i] = in[i] * gain` where `in[i]` and `outL[i]` are the same
memory location. This is correct for a mono-in/stereo-out plug-in, but a reader
coming back in six months will wonder why the original `in` (which was fed into
`onsetDetector.process` and `energyAnalyser.process`) is being mutated.

**Fix:** Either comment the aliasing explicitly or split the variable:

```cpp
// in and outL alias channel 0; this is a deliberate in-place gain pass-through.
```

### IN-02: `highFreqFlux` parameter is unused in `StructureTagger::update`

**File:** `src/analysis/StructureTagger.cpp:24`
**Issue:** The parameter is kept in the signature (commented out in the impl)
but `FeatureVector` and the processor still compute and pass it. Either document
that it is reserved for Phase 2 ML, or drop it to simplify the interface.

**Fix:** Add a `/** Currently unused; reserved for Phase 2 ML classifier. */`
comment to the header declaration.

### IN-03: Test coverage for `StructureTagger` is thin

**File:** `tests/test_structure_tagger.cpp`
**Issue:** Only one transition direction is tested (BREAKDOWN → SILENT via
immediate exit). Missing coverage:
- VERSE → CHORUS hysteresis (must hold 2.0s)
- CHORUS → VERSE hysteresis (must hold 2.5s)
- SILENT → VERSE (must be immediate, per D-07)
- BREAKDOWN flicker suppression with oscillating input

**Fix:** Add parameterised cases covering each `(from, to)` pair and assert both
that the state does not change before the hold and that it does change after.

### IN-04: `MidiPatternLibrary` bass note naming

**File:** `src/midi/MidiPatternLibrary.cpp:11`
**Issue:** `kBassRoot = 40` is labelled "E2 placeholder". In the standard
MIDI / scientific pitch convention where middle C = MIDI 60 = C4, note 40 is in
fact E2, so the comment is correct. Just flagging because Phase 2 key tracking
will want a named-constant table rather than raw MIDI numbers.

**Fix:** Introduce a `midi::Note` enum or at least `constexpr uint8_t kE2 = 40;`
when key inference lands.

### IN-05: Preview countdown loses fractional samples at block boundaries

**File:** `src/AccompanimentProcessor.cpp:146`
**Issue:** `debugPreviewSamplesRemaining` counts down by `numSamples` per block.
If the final block underflows past zero, `juce::jmax(0, …)` clamps, so the
preview can end up slightly shorter than 5 s (by up to one buffer, ≈5-10 ms at
256-512 samples). Below perceptual threshold, but worth noting.

**Fix:** None required; document the tolerance if users ever report it.

### IN-06: `timeInStateSec` keeps accumulating while desired == current

**File:** `src/analysis/StructureTagger.cpp:27, 31-32`
**Issue:** `timeInStateSec += blockSec` runs before the early return at line 32.
When desired matches current for a long time and then changes, the hold check at
line 42 passes instantly because the counter already dwarfs the hold threshold.
This appears to be the intended semantics (hold = "minimum time before we are
allowed to leave this state"), but it means the hold is only enforced right after
a previous transition. Worth a one-line comment to prevent a future contributor
from "fixing" it.

**Fix:** Add a comment:

```cpp
// timeInStateSec accumulates whenever desired == current; the hold threshold
// only gates transitions that would fire within `holdRequired` of the prior
// transition. Sustained matching states simply stay.
```

---

_Reviewed: 2026-04-17T00:00:00Z_
_Reviewer: Claude (gsd-code-reviewer)_
_Depth: standard_
