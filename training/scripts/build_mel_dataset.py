#!/usr/bin/env python3
"""Build mel-spectrogram dataset from raw guitar recordings (M009 — mel-aligned).

Matches C++ MelSpectrogramExtractor exactly:
  - n_fft=2048 (46ms), hop=512 (11.6ms), 64 mel bands, fmax=8000Hz
  - Hann window, magnitude FFT, triangular mel filterbank
  - Normalize by max magnitude, dB with -80 floor
  - 32-frame windows (~370ms context)

This MUST produce identical mel output to the C++ plugin for the same audio input.
"""

from __future__ import annotations

import argparse
import csv
import sys
from pathlib import Path

import numpy as np

# ─── Constants (MUST match src/analysis/MelSpectrogramExtractor.h) ───────────

SR = 44100
N_MELS = 64
N_FFT = 2048            # matches C++ kFftSize
HOP_LENGTH = 512        # matches C++ kHopSize
FMAX = 8000
FRAMES_PER_WINDOW = 32  # matches C++ kTimeFrames
NUM_BINS = N_FFT // 2 + 1  # 1025
STRIDE_FRAMES = 2

CLASSES = ["palm_mute", "open_chord", "single_note", "sustain", "silence"]
CLASS_TO_IDX = {name: i for i, name in enumerate(CLASSES)}

SUSTAIN_AUG_FACTOR = 0
SUSTAIN_TIME_STRETCH = (0.9, 1.1)
SUSTAIN_PITCH_SHIFT = (-1, 1)
SUSTAIN_GAIN_DB = (-6, 6)

TARGET_SILENCE_WINDOWS = 1200
MIN_WINDOWS_PER_CLASS = 500


# ─── Helpers ─────────────────────────────────────────────────────────────────

def _hz_to_mel(hz: float) -> float:
    return 2595.0 * np.log10(1.0 + hz / 700.0)


def _mel_to_hz(mel: float) -> float:
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def _build_mel_filterbank() -> np.ndarray:
    """Build triangular mel filterbank matching C++ buildMelFilterbank().

    Returns (N_MELS, NUM_BINS) float32 array.
    """
    nyquist = SR / 2.0
    mel_low = _hz_to_mel(0.0)
    mel_high = _hz_to_mel(float(FMAX))

    # Mel center frequencies (kMelBands + 2 points for left/right edges)
    mel_centers = np.linspace(mel_low, mel_high, N_MELS + 2)
    hz_centers = np.array([_mel_to_hz(m) for m in mel_centers])

    # Map to FFT bin indices
    bin_centers = np.floor(hz_centers / nyquist * (NUM_BINS - 1)).astype(np.int32)

    weights = np.zeros((N_MELS, NUM_BINS), dtype=np.float32)
    for m in range(N_MELS):
        left = bin_centers[m]
        mid = bin_centers[m + 1]
        right = bin_centers[m + 2]

        # Rising edge
        for k in range(left, mid + 1):
            weights[m, k] = 1.0 if mid == left else (k - left) / (mid - left)
        # Falling edge
        for k in range(mid + 1, right + 1):
            weights[m, k] = 1.0 if right == mid else (right - k) / (right - mid)

    return weights


# Pre-compute filterbank once
_MEL_FILTERBANK = _build_mel_filterbank()


def _hann_window(n: int) -> np.ndarray:
    return 0.5 * (1.0 - np.cos(2.0 * np.pi * np.arange(n) / (n - 1))).astype(np.float32)


_HANN_WINDOW = _hann_window(N_FFT)


def _training_dir() -> Path:
    return Path(__file__).resolve().parents[1]


def _repo_root() -> Path:
    return _training_dir().parent


def load_audio(path: Path) -> np.ndarray:
    """Load WAV as mono float32 at SR."""
    import soundfile as sf
    y, file_sr = sf.read(str(path), dtype="float32", always_2d=False)
    if file_sr != SR:
        import librosa
        y = librosa.resample(y, orig_sr=file_sr, target_sr=SR)
    if y.ndim > 1:
        y = y.mean(axis=1)
    return y.astype(np.float32)


def extract_mel_windows(y: np.ndarray) -> list[np.ndarray]:
    """Extract (64, 32) mel-dB windows matching C++ MelSpectrogramExtractor.

    Algorithm:
      1. Slide 2048-sample Hann window with hop=512 across audio
      2. Compute FFT → magnitude spectrum (1025 bins)
      3. Accumulate in 32-frame rolling buffer
      4. Apply mel filterbank to all 32 frames
      5. Normalize by max mel value, convert to dB with -80 floor
      6. Output one window every STRIDE_FRAMES hops
    """
    n_samples = len(y)
    total_hops = (n_samples - N_FFT) // HOP_LENGTH + 1
    if total_hops < FRAMES_PER_WINDOW:
        return []

    # Rolling buffer: (FRAMES_PER_WINDOW, NUM_BINS)
    spectra_buffer = np.zeros((FRAMES_PER_WINDOW, NUM_BINS), dtype=np.float32)
    write_idx = 0
    frames_collected = 0

    windows: list[np.ndarray] = []

    for hop in range(total_hops):
        start = hop * HOP_LENGTH
        segment = y[start : start + N_FFT]
        if len(segment) < N_FFT:
            break

        # Hann-windowed FFT
        windowed = segment * _HANN_WINDOW
        fft_result = np.fft.rfft(windowed, n=N_FFT)  # 1025 complex bins
        magnitudes = np.abs(fft_result).astype(np.float32)

        # Store in rolling buffer
        spectra_buffer[write_idx] = magnitudes
        write_idx = (write_idx + 1) % FRAMES_PER_WINDOW
        frames_collected += 1

        # Produce a window every STRIDE_FRAMES hops after buffer is full
        if frames_collected >= FRAMES_PER_WINDOW and hop % STRIDE_FRAMES == 0:
            # Apply mel filterbank to all 32 frames at once
            # spectra_buffer contains [oldest...newest] from write_idx
            mel_out = np.zeros((N_MELS, FRAMES_PER_WINDOW), dtype=np.float32)
            for t in range(FRAMES_PER_WINDOW):
                frame_idx = (write_idx + t) % FRAMES_PER_WINDOW
                spectrum = spectra_buffer[frame_idx]
                mel_out[:, t] = _MEL_FILTERBANK @ spectrum  # (64,) @ (64,1025) × (1025,)

            # Normalize and convert to dB (matching C++ process())
            max_mag = max(np.max(mel_out), 1e-8)
            mel_db = mel_out / max_mag
            mel_db = np.maximum(mel_db, 1e-8)
            mel_db = 10.0 * np.log10(mel_db)
            mel_db = np.maximum(mel_db, -80.0)

            windows.append(mel_db.astype(np.float32))

    return windows


def augment_sustain(y: np.ndarray) -> list[np.ndarray]:
    """Generate augmented copies of a sustain audio signal.

    Uses simple numpy operations (avoids librosa/numba dependency issues).
    """
    rng = np.random.RandomState(42)
    augmented: list[np.ndarray] = []

    for _ in range(SUSTAIN_AUG_FACTOR):
        rate = rng.uniform(*SUSTAIN_TIME_STRETCH)
        steps = rng.uniform(*SUSTAIN_PITCH_SHIFT)
        gain_db = rng.uniform(*SUSTAIN_GAIN_DB)

        # Time stretch: linear interpolation resample
        n_out = int(len(y) / rate)
        indices = np.linspace(0, len(y) - 1, n_out)
        y_aug = np.interp(indices, np.arange(len(y)), y).astype(np.float32)

        # Pitch shift: resample by 2^(steps/12) then interpolate back
        pitch_rate = 2.0 ** (-steps / 12.0)
        n_shifted = int(len(y) * pitch_rate)
        idx_shift = np.linspace(0, len(y) - 1, n_shifted)
        y_shifted = np.interp(idx_shift, np.arange(len(y)), y)
        # Resample back to original length
        idx_back = np.linspace(0, len(y_shifted) - 1, len(y))
        y_aug = np.interp(idx_back, np.arange(len(y_shifted)), y_shifted).astype(np.float32)

        # Gain
        gain_linear = 10.0 ** (gain_db / 20.0)
        y_aug = (y_aug * gain_linear).astype(np.float32)
        y_aug = np.clip(y_aug, -1.0, 1.0)
        augmented.append(y_aug)

    return augmented


def generate_synthetic_silence(n_windows: int, noise_floor_db: float = -93.7) -> list[np.ndarray]:
    """Generate synthetic silence mel windows matching C++ extractor output."""
    rng = np.random.RandomState(42)
    # Need enough audio to produce 32+ frames: (FRAMES_PER_WINDOW * HOP_LENGTH + N_FFT) samples
    min_samples = FRAMES_PER_WINDOW * HOP_LENGTH + N_FFT + STRIDE_FRAMES * HOP_LENGTH
    synthetic: list[np.ndarray] = []

    for _ in range(n_windows):
        nf = noise_floor_db + rng.uniform(-1.0, 1.0)
        noise = rng.randn(min_samples).astype(np.float32)
        noise *= 10.0 ** (nf / 20.0)
        windows = extract_mel_windows(noise)
        if windows:
            synthetic.append(windows[0])

    return synthetic


def build_dataset(raw_dir: Path, processed_dir: Path) -> tuple[np.ndarray, np.ndarray, list[dict]]:
    """Walk raw_dir, extract mel windows, return X, y, meta rows."""
    X_list: list[np.ndarray] = []
    y_list: list[int] = []
    meta_rows: list[dict] = []

    for class_name in CLASSES:
        class_dir = raw_dir / class_name
        if not class_dir.is_dir():
            print(f"WARNING: {class_dir} not found — skipping {class_name}", file=sys.stderr)
            continue

        class_idx = CLASS_TO_IDX[class_name]
        wav_files = sorted(class_dir.glob("*.wav")) + sorted(class_dir.glob("*.WAV"))

        if not wav_files:
            print(f"WARNING: no .wav files in {class_dir} — skipping", file=sys.stderr)
            continue

        class_windows: list[np.ndarray] = []

        for wav_path in wav_files:
            print(f"  Loading {wav_path} ...")
            y = load_audio(wav_path)

            windows = extract_mel_windows(y)
            class_windows.extend(windows)

            if class_name == "sustain":
                aug_signals = augment_sustain(y)
                for aug_y in aug_signals:
                    aug_windows = extract_mel_windows(aug_y)
                    class_windows.extend(aug_windows)

            print(f"    → {len(windows)} windows (aug total: {len(class_windows)} for class)")

        if class_name == "silence" and len(class_windows) < TARGET_SILENCE_WINDOWS:
            needed = TARGET_SILENCE_WINDOWS - len(class_windows)
            print(f"  Generating {needed} synthetic silence windows ...")
            synth = generate_synthetic_silence(needed)
            class_windows.extend(synth)

        for win in class_windows:
            X_list.append(win[np.newaxis, :, :])
            y_list.append(class_idx)
            meta_rows.append({
                "class_name": class_name,
                "class_idx": class_idx,
                "split": "train",
            })

    X = np.stack(X_list, axis=0).astype(np.float32)
    y_arr = np.array(y_list, dtype=np.int64)
    return X, y_arr, meta_rows


def main() -> int:
    parser = argparse.ArgumentParser(description="Build C++-aligned mel dataset (M009).")
    parser.add_argument("--raw-dir", type=Path, default=_repo_root() / "data" / "raw")
    parser.add_argument("--out-dir", type=Path, default=_repo_root() / "data" / "processed")
    args = parser.parse_args()

    raw_dir = args.raw_dir.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if not raw_dir.is_dir():
        print(f"ERROR: raw directory not found: {raw_dir}", file=sys.stderr)
        return 1

    print(f"Building C++-aligned dataset from {raw_dir}")
    print(f"  n_fft={N_FFT}, hop={HOP_LENGTH}, mels={N_MELS}, fmax={FMAX}")
    print(f"  Output: {out_dir}")
    print()

    X, y, meta_rows = build_dataset(raw_dir, out_dir)

    print()
    print(f"Total windows: {X.shape[0]}")
    print(f"X shape: {X.shape}  (expected: N, 1, 64, 32)")
    print(f"y shape: {y.shape}  (expected: N,)")
    print()
    print("Per-class window counts:")
    unique, counts = np.unique(y, return_counts=True)
    all_ok = True
    for cls_idx, count in zip(unique, counts):
        name = CLASSES[cls_idx]
        status = "✓" if count >= MIN_WINDOWS_PER_CLASS else "✗ BELOW MINIMUM"
        if count < MIN_WINDOWS_PER_CLASS:
            all_ok = False
        print(f"  {name:>12s} (class {cls_idx}): {count:>6d} windows  {status}")

    np.save(out_dir / "X.npy", X)
    np.save(out_dir / "y.npy", y)

    meta_path = out_dir / "meta.csv"
    with meta_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["class_name", "class_idx", "split"])
        writer.writeheader()
        writer.writerows(meta_rows)

    print(f"\nSaved: {out_dir / 'X.npy'}")
    print(f"Saved: {out_dir / 'y.npy'}")
    print(f"Saved: {meta_path}")

    if not all_ok:
        print(f"\nERROR: class(es) below {MIN_WINDOWS_PER_CLASS} minimum.", file=sys.stderr)
        return 1

    print("\n✓ Dataset ready for training.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
