# Real-Time Audio Classifier for FuzzyBand
## Phase 2 ML Inference — Playing Style Detection

> **Goal:** A metal-specific playing-style classifier that slots into FuzzyBand's existing ONNX Runtime
> infrastructure, making the plugin genuinely playable with real ML inference running in real time.

---

## Overview

The rule-based engine (Phase 1) is too brittle to feel musical. This classifier replaces it with a small
CNN trained on mel spectrograms of your own playing, exported to ONNX, and loaded into the existing
inference slot. The standalone training pipeline also serves as a portfolio ML project in its own right.

**Five target classes:**

| Class | Description | Triggered behaviour |
|---|---|---|
| `PALM_MUTE` | Percussive, tight chugging | Dense, driving drum pattern |
| `OPEN_CHORD` | Open power chords, sustained | Bigger, more spacious pattern |
| `SINGLE_NOTE` | Single-note riff / lead line | Lighter, more reactive pattern |
| `SUSTAIN` | Held note, feedback, swell | Ambient/textural response |
| `SILENCE` | No signal / stop playing | Drum fill → stop |

---

## Phase A — Data Collection

### What you need
- Your guitar (the one you built at 15 is fine)
- A DAW session or simple recording script
- ~30–45 minutes of playing time total

### Recording strategy

Record each class in **separate, labelled sessions**. Aim for 6–8 minutes per class.

```
data/
  raw/
    palm_mute/      ← session_01.wav, session_02.wav ...
    open_chord/
    single_note/
    sustain/
    silence/        ← can be generated synthetically (noise floor)
```

**Tips per class:**

- `PALM_MUTE`: vary the tempo, string selection (low E vs. all strings), pickup position
- `OPEN_CHORD`: vary chord shapes (power chord, dyads), pick attack intensity
- `SINGLE_NOTE`: mix fast runs, slow melodic lines, bend-heavy playing
- `SUSTAIN`: held open chords, pinch harmonics, controlled feedback at low volume
- `SILENCE`: leave the guitar in the room unplugged / cable hum / amp buzz

**Mic vs. DI:** DI (direct input) is cleaner and more reproducible. If you're recording through
a DAW, a DI track with amp simulation is ideal. Avoid stereo — mono only.

**Sample rate:** 44100 Hz. Bit depth: 24-bit or 16-bit, doesn't matter.

### Why your own playing is an advantage
Off-the-shelf datasets (IDMT-SMT-Guitar, GuitarSet) are clean, academic, and genre-neutral.
Your playing captures the specific attack, gain staging, and articulation that FuzzyBand will
actually see in production. This is a genuine data advantage, not a workaround.

---

## Phase B — Feature Extraction Pipeline

### Window parameters

| Parameter | Value | Rationale |
|---|---|---|
| Window size | 512 ms | Long enough to capture rhythmic context |
| Hop size | 256 ms | 50% overlap, smooth transitions |
| Sample rate | 44100 Hz | Standard DAW rate |
| Samples per window | 22050 | |

### Feature: Mel Spectrogram

```python
import librosa
import numpy as np

def extract_mel(audio_path, sr=44100, n_mels=64, win_ms=512, hop_ms=256):
    y, _ = librosa.load(audio_path, sr=sr, mono=True)
    win_samples = int(sr * win_ms / 1000)
    hop_samples = int(sr * hop_ms / 1000)
    mel = librosa.feature.melspectrogram(
        y=y,
        sr=sr,
        n_mels=n_mels,
        n_fft=win_samples,
        hop_length=hop_samples,
        fmax=8000       # guitar fundamentals + harmonics well within this
    )
    return librosa.power_to_db(mel, ref=np.max)
```

**Output shape per window:** `(64, T)` where T ≈ time frames.  
For fixed-length input, slice into `(64, 32)` frames → 512ms at 256ms hop.

### Slice and label

```python
# scripts/build_dataset.py
# Walks raw/ directories, slices audio into windows, saves .npy + label CSV
```

**Final dataset structure:**

```
data/
  processed/
    X.npy         # shape: (N, 1, 64, 32) — N samples, 1 channel, H x W
    y.npy         # shape: (N,) — integer class labels 0–4
    meta.csv      # filename, class, split (train/val/test)
```

**Expected volume:** 6 min × 5 classes × ~4 windows/sec ≈ **7,200 samples**. Enough for a small CNN.

---

## Phase C — Model Architecture

A deliberately small CNN. Designed to be fast enough for real-time inference on the audio thread
background worker. Total parameter count target: **< 200k**.

```python
import torch
import torch.nn as nn

class PlayingStyleCNN(nn.Module):
    def __init__(self, n_classes=5):
        super().__init__()
        self.features = nn.Sequential(
            # Block 1
            nn.Conv2d(1, 16, kernel_size=3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(),
            nn.MaxPool2d(2, 2),          # → (16, 32, 16)

            # Block 2
            nn.Conv2d(16, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(),
            nn.MaxPool2d(2, 2),          # → (32, 16, 8)

            # Block 3
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(),
            nn.AdaptiveAvgPool2d((4, 4)) # → (64, 4, 4)
        )
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(64 * 4 * 4, 128),
            nn.ReLU(),
            nn.Dropout(0.3),
            nn.Linear(128, n_classes)
        )

    def forward(self, x):
        return self.classifier(self.features(x))
```

**Why this architecture:**
- BatchNorm makes training stable with a small dataset
- AdaptiveAvgPool handles slight input size variation gracefully
- Dropout at 0.3 prevents overfitting on ~7k samples
- No attention, no transformer — fast and auditable

---

## Phase D — Training Pipeline

### Directory structure

```
fuzzyband-classifier/
  data/
    raw/              ← your recordings
    processed/        ← X.npy, y.npy, meta.csv
  scripts/
    build_dataset.py  ← slice + extract features
    train.py          ← training loop
    evaluate.py       ← confusion matrix, per-class metrics
    export_onnx.py    ← PyTorch → ONNX
  models/
    best_model.pt
    classifier.onnx
  notebooks/
    exploration.ipynb ← quick EDA, spectrogram sanity checks
  requirements.txt
```

### Training config

```python
# train.py
BATCH_SIZE = 32
EPOCHS = 50
LR = 1e-3
WEIGHT_DECAY = 1e-4
SCHEDULER = "CosineAnnealingLR"

# 70/15/15 train/val/test split
# Stratified by class to handle any class imbalance
```

### Data augmentation (apply on-the-fly in training only)

```python
# These simulate real-world variation without collecting more data
augmentations = [
    AddGaussianNoise(std=0.01),       # amp noise / hum
    TimeStretch(rate_range=(0.9, 1.1)),
    PitchShift(semitones_range=(-1, 1)),
    RandomGain(db_range=(-6, 6)),
]
```

### Expected training time
- CPU (MacBook): ~10–15 minutes for 50 epochs
- No GPU required for this model size

---

## Phase E — Evaluation

This is your interview talking point. Don't skip it.

### Metrics to report

```
Overall accuracy
Per-class precision / recall / F1
Confusion matrix (most interesting — which classes get confused?)
```

### What to expect and explain

The most likely confusions:
- `PALM_MUTE` ↔ `SINGLE_NOTE` at high gain (similar spectral density)
- `SUSTAIN` ↔ `OPEN_CHORD` on slow decay

**The evaluation story:** "PALM_MUTE and SINGLE_NOTE confused each other at ~12% of the time.
I looked at the spectrograms, saw that high-gain single notes have nearly identical low-freq energy
to palm mutes. I added pitch centroid as a secondary feature and confusion dropped to 6%."
That's a better interview answer than "I got 94% accuracy."

### Latency benchmark

Run this before ONNX export and after, report both:

```python
import time
import numpy as np

def benchmark_inference(model, n_runs=1000):
    dummy = np.random.randn(1, 1, 64, 32).astype(np.float32)
    times = []
    for _ in range(n_runs):
        t0 = time.perf_counter()
        model.run(None, {"input": dummy})
        times.append((time.perf_counter() - t0) * 1000)
    print(f"P50: {np.percentile(times, 50):.2f}ms")
    print(f"P95: {np.percentile(times, 95):.2f}ms")
    print(f"P99: {np.percentile(times, 99):.2f}ms")

# Target: P99 < 5ms on CPU. Well within real-time budget for background thread.
```

---

## Phase F — ONNX Export

```python
# scripts/export_onnx.py
import torch

model = PlayingStyleCNN(n_classes=5)
model.load_state_dict(torch.load("models/best_model.pt"))
model.eval()

dummy_input = torch.randn(1, 1, 64, 32)

torch.onnx.export(
    model,
    dummy_input,
    "models/classifier.onnx",
    input_names=["input"],
    output_names=["logits"],
    dynamic_axes={"input": {0: "batch_size"}},
    opset_version=17
)

print("Exported to models/classifier.onnx")
```

**Verify the export:**

```python
import onnxruntime as ort
import numpy as np

session = ort.InferenceSession("models/classifier.onnx")
dummy = np.random.randn(1, 1, 64, 32).astype(np.float32)
out = session.run(None, {"input": dummy})
print("Output shape:", out[0].shape)  # expect (1, 5)
```

---

## Phase G — FuzzyBand Integration

### Threading model

```
Audio Thread:
  ├── Receives audio buffer (e.g. 512 samples at 44100Hz)
  ├── Accumulates into ring buffer
  ├── When 22050 samples accumulated → signals inference thread
  └── Reads latest class prediction (atomic read)

Inference Thread (background):
  ├── Receives 22050-sample window from ring buffer
  ├── Extracts mel spectrogram (C++ implementation or pre-baked lookup)
  ├── Runs ONNX Runtime session
  ├── Writes prediction to shared atomic variable
  └── Loops
```

**Critical rule:** ONNX Runtime **never** runs on the audio thread. Only the ring buffer write and
atomic read happen there.

### C++ integration sketch

```cpp
// In PluginProcessor.h
class FuzzyBandProcessor : public juce::AudioProcessor {
    // ...
    std::atomic<int> currentClass { 0 };  // 0-4, read by audio thread
    RingBuffer<float> inputBuffer;
    InferenceThread inferenceThread;
    OnnxClassifier classifier;           // wraps onnxruntime::Session
};

// InferenceThread::run()
void InferenceThread::run() {
    while (!threadShouldExit()) {
        if (inputBuffer.hasEnoughSamples(22050)) {
            auto window = inputBuffer.read(22050);
            auto mel = extractMelSpectrogram(window);
            int predicted = classifier.predict(mel);
            processor.currentClass.store(predicted);
        }
        wait(50); // check every 50ms
    }
}
```

### Mapping classes to patterns

```cpp
// PatternSelector.cpp
DrumPattern selectPattern(int classId, float confidence) {
    if (confidence < 0.6f) return currentPattern; // hold last pattern if uncertain

    switch (classId) {
        case PALM_MUTE:   return patterns["metal_drive"];
        case OPEN_CHORD:  return patterns["metal_open"];
        case SINGLE_NOTE: return patterns["metal_light"];
        case SUSTAIN:     return patterns["ambient"];
        case SILENCE:     return patterns["fill_then_stop"];
    }
}
```

**Confidence thresholding** (the `0.6f` above) is important — it prevents jittery pattern switching
when the model is uncertain at class boundaries.

---

## Milestones

| # | Milestone | Deliverable | Est. time |
|---|---|---|---|
| 1 | Data collection complete | 5 labelled audio folders, ~7k windows | 2–3 hours |
| 2 | Feature pipeline working | `X.npy`, `y.npy` generated cleanly | 1 day |
| 3 | Model trains and converges | Loss curve, val accuracy > 80% | 1 day |
| 4 | Evaluation complete | Confusion matrix, P50/P95/P99 latency | 0.5 days |
| 5 | ONNX export + Python verify | `classifier.onnx` runs correctly | 2 hours |
| 6 | FuzzyBand integration | Plugin loads model, background thread running | 2–3 days |
| 7 | Playable demo | Plugin responds to playing style in real time | — |

**Total estimated time:** 2–3 focused weeks, working part-time alongside job search.

---

## What this demonstrates (interview framing)

- **Domain expertise as training data** — recorded and labelled your own dataset using professional
  playing knowledge; no off-the-shelf dataset could replicate this
- **End-to-end ML pipeline** — data collection → feature engineering → CNN training → ONNX export →
  real-time C++ inference
- **Production constraints** — explicit latency budgeting, audio thread safety, confidence thresholding
- **Evaluation rigour** — per-class metrics, confusion matrix analysis, latency benchmarking
- **Cross-stack** — Python ML pipeline feeds directly into a C++/JUCE production system

This is not a notebook demo. It runs in a DAW.

---

## Dependencies

```txt
# Python (training pipeline)
torch>=2.0
torchaudio
librosa
onnx
onnxruntime
scikit-learn
numpy
pandas
matplotlib
seaborn

# C++ (FuzzyBand)
# onnxruntime v1.16+ (already in architecture)
# JUCE 7+ (already in project)
```
