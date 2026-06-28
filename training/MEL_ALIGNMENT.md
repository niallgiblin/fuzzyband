# Mel Parameter Alignment — Playing-Style Classifier (M009)

**Status:** BLOCKED — predictions are garbage until resolved
**Created:** 2026-06-24

---

## The Problem

The `PlayingStyleCNN` ONNX model was trained on mel spectrograms produced by **Python librosa** with one set of parameters. The C++ `MelSpectrogramExtractor` uses different parameters. The outputs are not compatible.

| Parameter | Python (librosa, trained) | C++ (MelSpectrogramExtractor) |
|-----------|---------------------------|-------------------------------|
| `n_fft` | **22,050** (512ms window) | **2,048** (46ms window) |
| `hop_length` | **11,290** (256ms hop) | **512** (11.6ms hop) |
| `n_mels` | 64 | 64 ✅ |
| `fmax` | 8,000 Hz | 8,000 Hz ✅ |
| Output shape | (64, 32) | (64, 32) ✅ |
| Temporal context | ~8.2 seconds | ~370 ms |
| Frames in buffer | 32 (from sliced mel) | 32 (rolling STFT buffer) |

The fundamental difference: Python uses a single very-long FFT window (22,050 samples) and extracts mel bands from that. C++ uses short FFT windows (2,048 samples) accumulated over time. These produce fundamentally different spectral representations of the same audio.

## Why Predictions Are Garbage

The ONNX model learned to map **librosa-style mel spectrograms** (long FFT, coarse time resolution, fine frequency resolution) to playing-style labels. The C++ extractor produces **short-FFT mel spectrograms** (short FFT, fine time resolution, coarse frequency resolution). Feeding incompatible mel data to the model produces random/meaningless predictions.

## Resolution Plan

### Option A: Align C++ extractor to match Python (Recommended)

Modify `MelSpectrogramExtractor` to use the same parameters as the Python training pipeline:

1. Change `kFftSize` from 2048 → 22050
2. Change `kHopSize` from 512 → 11290
3. Remove the rolling STFT buffer — compute mel directly from the full 22,050-sample window
4. Verify mel output matches Python librosa for the same input audio

**Pros:** No retraining needed. Model works immediately.
**Cons:** 22,050-point FFT is ~10× more expensive per frame. But inference runs at ~2Hz (every 22050 samples = 500ms), so compute is negligible.

### Option B: Retrain with C++ mel features

Keep the existing C++ extractor as-is, rebuild the dataset using it:

1. Write a Python script that calls the C++ extractor (or replicates its logic in Python)
2. Run it on the 5 recordings
3. Rebuild X.npy/y.npy
4. Retrain PlayingStyleCNN
5. Re-export classifier.onnx

**Pros:** Keeps the efficient short-FFT approach for real-time use.
**Cons:** Full retrain cycle (S01-S04). ~30 minutes of work.

### Option C: Adjust Python training to match C++ params

Change the Python `build_mel_dataset.py` to use `n_fft=2048`, `hop_length=512`:

1. Modify `build_mel_dataset.py` constants
2. Rebuild dataset
3. Retrain and re-export

**Pros:** Minimal C++ changes. Python params match C++.
**Cons:** Full retrain cycle. Shorter FFT windows lose low-frequency resolution (important for guitar fundamentals at 82-330 Hz).

## Recommendation

**Option A** is the fastest path to working predictions:
- Only 2 C++ constants to change
- No retraining
- The compute cost of a 22,050-point FFT every 500ms is negligible on M-series

The Python training pipeline used 22050-point FFT for good reason — guitar fundamentals are at 82-330 Hz, requiring fine frequency resolution that a 2048-point FFT cannot provide.

## Effort Estimate

| Option | C++ work | Python work | Total |
|--------|----------|-------------|-------|
| A: Fix C++ | ~15 min | none | **~15 min** |
| B: Retrain | none | ~30 min | ~30 min |
| C: Retrain | minimal | ~30 min | ~30 min |

## Related Files

- `src/analysis/MelSpectrogramExtractor.h` — C++ extractor constants
- `src/analysis/MelSpectrogramExtractor.cpp` — C++ mel implementation
- `training/scripts/build_mel_dataset.py` — Python dataset builder
- `REAL_TIME_AUDIO_CLASSIFIER.md` — Original spec (Python params)
- `assets/classifier.onnx` — Current trained model (librosa params)
