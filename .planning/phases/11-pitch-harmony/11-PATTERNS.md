# Phase 11 — Pattern map (Pitch & harmony)

Maps new work to existing codebase analogs.

---

## 1. New pitch module ← `OnsetDetector` / `EnergyAnalyser`

| New | Analog | Pattern |
|-----|--------|---------|
| `PitchEstimator` (YIN) | `OnsetDetector` | `prepare(sr, block)`, `process(buffer)`, internal state, no heap in `process` |

**Reference:** `src/analysis/OnsetDetector.h` — lifecycle called from `AccompanimentProcessor::prepareToPlay` / `processBlock`.

---

## 2. `FeatureVector` extension ← existing fields

| New fields | Analog | Pattern |
|------------|--------|---------|
| `pitchRootMidi`, `pitchConfidence` | `bpm`, `rmsEnergy`, `spectralCentroid` | Plain POD floats; set in `processBlock` before `try_enqueue` |

**Reference:** `src/analysis/FeatureVector.h` — single struct, default-initialized.

---

## 3. Bass transpose ← `setStructureSilent`

| Mechanism | Analog | Pattern |
|-----------|--------|---------|
| `setBassSemitoneOffset(int)` or internal offset | `PatternPlayer::setStructureSilent(bool)` | Small RT-safe setter; consumed in `emitEventsForRange` for bass channel only |

**Reference:** `src/midi/PatternPlayer.cpp` — `emitForList` lambda; add offset when `channel == kBassChannel`.

---

## 4. Optional ONNX ← `OnnxInference::tryLoadModel`

| New | Analog | Pattern |
|-----|--------|---------|
| Pitch ONNX loader (if any) | `AccompanimentProcessor` `makeInference()` | `#if MA_ENABLE_ONNX`, optional second model, fallback to YIN |

**Reference:** `src/AccompanimentProcessor.cpp` lines 11–19, `src/inference/OnnxInference.h`.

---

## PATTERN MAPPING COMPLETE
