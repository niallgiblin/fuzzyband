# Single-Note Detection — Data & Retraining Plan

**Status:** Ready to execute (no code required to read this; recording is on the guitarist).
**Relates to:** `docs/DATA_STRATEGY.md` §5.2 (perception self-labels), `training/EVALUATION.md`.
**Model:** `PlayingStyleCNN` (`training/models/playing_style_cnn.py`) → `assets/style_cnn.onnx`.

## 0. Why single notes are weak (root cause, not vibes)

1. **Too little data, and it leaks.** The perception CNN was trained on **2 recordings
   per class** (`data/raw/single_note/` has 2 WAVs). Adjacent mel windows share
   **30 of 32 frames** (93.75% overlap), so the "100% test accuracy" in
   `training/EVALUATION.md` is window-level leakage — the model memorised ~5 takes,
   it did not learn to generalise to a live guitar.
2. **Runtime window mismatch.** Training windows stride every **23 ms**, but the
   plugin produces one **non-overlapping 512 ms** mel window. A lone ~150 ms single
   note lands in a window that is mostly silence/sustain, so it gets averaged away.

The C++ side is already fixed for *stability* (`AccompanimentProcessor::updateCommittedStyle`),
but a smoother of a weak classifier still can't *find* a note the model never labels.

## 1. Recording plan (the human step — highest leverage)

Record into `data/raw/<class>/` as **mono WAV at 44.1 kHz** (any length ≥ 1 s; aim for
4–12 s of continuous playing per take). The single source of truth for labels is
`training/perception_taxonomy.py` — do **not** add new label names.

| Class | Minimum new takes | What to play |
|---|---|---|
| `single_note` | **10+** (currently 2) | Isolated single notes: pick one note, let it ring, stop; repeat at varying frets/strings, tempos, pick strength. Also **lead runs** (scale fragments, 3–8 notes) so the model sees the *articulation*, not just the envelope. |
| `palm_mute` | 5 | Chugged muted riffs at several tempos/densities (slow 8ths, fast 16ths). |
| `open_chord` | 5 | Strummed open/barre chords, clean and distorted. |
| `sustain` | 5 | Long held notes/chords with vibrato; let them decay fully. |
| `silence` | 0 (synthetic exists) | A few seconds of *quiet room/amp hiss* (real floor) is still valuable. |

**Why 10+ for single_note:** with a grouped (source-level) holdout you need enough
distinct takes that the val/test set actually contains unseen *recordings*. With 2
takes, a grouped split cannot hold any single-note recording out at all.

**Diversity rules (prevents the next leakage-style false green):**
- Different guitar tones: neck vs bridge pickup, clean vs distortion, two gain levels.
- Different tempos: 60 / 90 / 120 / 160 BPM.
- Different strings/registers (low E vs high e).

## 2. Rebuild + retrain

```bash
# 2a. Rebuild the C++-aligned mel dataset (writes the grouped split into meta.csv)
python3 training/scripts/build_mel_dataset.py \
    --raw-dir data/raw --out-dir data/processed

# 2b. Train + export. train_style_cnn.py uses the frozen grouped split in meta.csv
#     (source-level holdout) and writes assets/style_cnn.onnx directly.
python3 training/train_style_cnn.py --epochs 50
```

`train_style_cnn.py` already exports a self-contained `assets/style_cnn.onnx`. Rebuild the
plugin (BinaryData re-bundles it) and bump the `CMakeLists.txt` `VERSION` patch per the
version-bumping rule.

## 3. Runtime window overlap (optional, second lever)

Even with a good model, the **512 ms non-overlapping** runtime window is the wrong
temporal resolution for transients. Two options, in increasing effort:

- **A. Higher-rate mel windows (recommended).** Slide `AudioRingBuffer` reads by
  ~11025 samples (256 ms) instead of 22050, so consecutive mel windows overlap and a
  short note spans several windows. `MelSpectrogramExtractor` already carries a rolling
  32-frame buffer, so this is a queue/window-hop change, not a model change. Cost: ~2×
  mel extraction FFT work on the audio thread (still small at 256-sample buffers).
- **B. Peak-hold the mel frames.** Feed the classifier a window centred on a detected
  onset so the transient is maximally represented. More invasive (needs onset → window
  alignment plumbing).

## 4. Success criteria

- **Grouped holdout** (source-level, never window-level): per-class F1 ≥ 0.70,
  single_note recall ≥ 0.70, no dead class. Ignore any window-level number.
- **Live check (the real gate, `training/EVALUATION.md` S05):** while playing a
  single-note line, the "Style: Single Note" readout holds (no flicker) and the drums
  steer into the single-note family (verse-fast/thrash/half-time for metal,
  verse-fast/rock-shuffle/d-beat for rock). Confirm the model isn't just calling
  everything "sustain" or "silence".
