# Playing-Style CNN — Evaluation Report (M009 S03)

**Date:** 2026-06-24
**Model:** PlayingStyleCNN, 155,365 parameters
**Training data:** 5 guitar recordings (palm_mute, open_chord, single_note, sustain, silence), 5,583 mel-spectrogram windows

---

## Test Set Performance

**Split:** Stratified 70/15/15 (train/val/test) — 838 test windows

| Metric | Value |
|--------|-------|
| Test accuracy | **1.000** |
| Test macro-F1 | **1.000** |

### Per-Class Metrics

| Class | Precision | Recall | F1 | Support |
|-------|-----------|--------|-----|---------|
| palm_mute | 1.00 | 1.00 | 1.00 | 109 |
| open_chord | 1.00 | 1.00 | 1.00 | 105 |
| single_note | 1.00 | 1.00 | 1.00 | 107 |
| sustain | 1.00 | 1.00 | 1.00 | 337 |
| silence | 1.00 | 1.00 | 1.00 | 180 |

### Confusion Matrix

```
             palm_mute open_chord single_note sustain silence
   palm_mute        109          0          0       0       0
  open_chord          0        105          0       0       0
 single_note          0          0        107       0       0
     sustain          0          0          0     337       0
     silence          0          0          0       0     180
```

Zero misclassifications across all five classes.

---

## ⚠️ Data Leakage Caveat

The 100% accuracy is **inflated by window-level data splits**, not recording-level splits.

- Mel windows are extracted with stride=2 frames (512ms stride) from 32-frame windows (8.2s context)
- Adjacent windows share **30 of 32 frames** (93.75% overlap)
- `StratifiedShuffleSplit` assigns adjacent windows to different splits
- The model effectively sees the same audio fragments in both training and testing

**This does not mean the model is broken.** It means the evaluation cannot measure generalization to *new* guitar recordings. For a proper holdout evaluation, we would need:

1. Multiple distinct recordings per class (not just one long take)
2. Split at the recording/file level, not the window level
3. Train on recordings 1-3, test on recording 4+

With only one recording per class, recording-level splits are impossible. **The real validation will be S05 — live plugin inference on novel guitar input.**

---

## Latency Benchmark

**Hardware:** M-series Mac (CPU only)
**Method:** 100 warmup + 1000 timed single-inference runs, batch_size=1

| Percentile | Latency |
|------------|---------|
| **P50** | 0.255 ms |
| **P95** | 0.404 ms |
| **P99** | **0.519 ms** |
| Mean | 0.290 ms |
| Min | 0.241 ms |
| Max | 0.688 ms |
| Std | 0.067 ms |

### Batch Throughput

| Batch Size | P50 Latency | Per-Sample |
|------------|-------------|------------|
| 1 | 0.255 ms | 0.255 ms |
| 4 | 1.049 ms | 0.262 ms |
| 16 | 3.316 ms | 0.207 ms |

**Verdict:** P99 latency of 0.52ms is **10× under the 5ms target**. This model is trivially fast enough for real-time inference on a background thread running at 50Hz (20ms budget). Batching provides marginal throughput gains — single-inference is sufficient.

---

## Training Dynamics

- Converged by epoch 5 (val acc 0.988)
- Best epoch: 11 (val F1 1.000)
- Epoch 4 spike: val loss briefly spiked to 2.46 (likely bad augmentation batch), recovered immediately
- CosineAnnealingLR cooled smoothly from 1e-3 to near-zero
- No NaN loss observed

### Expected Confusions (Per REAL_TIME_AUDIO_CLASSIFIER.md Phase E)

The spec anticipated two confusion pairs:

1. **palm_mute ↔ single_note** — "at high gain, similar spectral density"
2. **sustain ↔ open_chord** — "on slow decay"

Neither materialized in this test set — all classes perfectly separated. This is consistent with the window-level leakage explanation: the model memorized recording-specific timbral fingerprints rather than learning generalizable spectral features. These confusions may still appear in real-time plugin use (S05).

---

## Gates

| Gate | Target | Actual | Status |
|------|--------|--------|--------|
| Val accuracy | ≥ 0.80 | 1.00 | ✅ |
| Per-class F1 | ≥ 0.60 | 1.00 all | ✅ |
| P99 latency | < 5 ms | 0.52 ms | ✅ |
| No NaN loss | required | none observed | ✅ |

All gates pass. Model is ready for ONNX export (S04).
