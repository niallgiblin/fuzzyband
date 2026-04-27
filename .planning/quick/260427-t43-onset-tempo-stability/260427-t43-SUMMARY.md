---
status: complete
quick_id: 260427-t43
slug: onset-tempo-stability
date: 2026-04-27
commit: 1c02d3e
version: 0.3.8
---

# Quick Task 260427-t43: Onset Tempo Stability Fixes

## What changed

**Fix 1 — BPM lock-in (OnsetDetector.cpp / .h)**
After 8+ consecutive IOIs all fall within ±8% of their mean, `tempoLocked` is set to `true`.
While locked, `currentBpm.store()` is skipped entirely — the BPM freezes at the established value.
A new `resetTempoLock()` method (called from `AccompanimentProcessor::processBlock()` on every SILENT block) clears the lock so the next playing session re-establishes tempo from scratch.

**Fix 2 — 5-BPM grid quantization (OnsetDetector.cpp: medianIoiBpm)**
After the median → BPM conversion and jlimit clamp, BPM is rounded to nearest 5:
`bpm = std::round(bpm / 5.0f) * 5.0f` then re-clamped to [80, 220].
This eliminates 1–3 BPM jitter visible in the display and felt in PatternPlayer timing drift.

**Fix 3 — 80ms refractory period (OnsetDetector.cpp: prepare)**
`minOnsetIntervalSamples` now uses `0.08 * sampleRate` instead of `0.05`.
At 44100 Hz: 3528 samples (80ms) vs 2205 (50ms). Still fast enough to catch 16th notes at 187 BPM.
Rejects most fuzz harmonic ghost-triggers that arrive 30–60ms after a real note attack.

## Why

Fuzz/distortion guitar generates dense harmonic content that produces multiple spurious onsets per pick stroke. With the old system, every false onset updated the BPM median, causing constant re-estimation during playing. The lock-in + quantization combination means the displayed BPM is stable for the duration of an idea; the 80ms gate reduces the number of bad IOIs reaching the ring buffer in the first place.

## Gotchas

- `tempoLocked` is a plain `bool` — both write sites (`runFftFrame` and `resetTempoLock`) execute on the audio thread, so no atomic is required.
- The second `for` loop in the consistency check uses `j` (not `i`) to avoid a -Wshadow clangd diagnostic.
- `ioiMean` (not `mean`) used inside the lock-in check to avoid shadowing the `medianIoi` local in `medianIoiBpm()`.
