#!/usr/bin/env python3
"""Build mel-spectrogram dataset for 22-pattern groove model (SIMPLIFY.md Wave 1).

Matches C++ MelSpectrogramExtractor exactly:
  - n_fft=2048, hop=512, 64 mel bands, fmax=8000Hz
  - Hann window, magnitude FFT, triangular mel filterbank
  - Normalize by max magnitude, dB with -80 floor
  - 32-frame windows (~370ms context)

Discovers all pattern_XX_name/ subdirectories in data/raw/, treats each as a class.
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
N_FFT = 2048             # matches C++ kFftSize
HOP_LENGTH = 512         # matches C++ kHopSize
FMAX = 8000
FRAMES_PER_WINDOW = 32   # matches C++ kTimeFrames
NUM_BINS = N_FFT // 2 + 1  # 1025
STRIDE_FRAMES = 2

MIN_WINDOWS_PER_CLASS = 10  # permissive for small dataset; quality gates in training


# ─── Helpers ─────────────────────────────────────────────────────────────────

def _hz_to_mel(hz: float) -> float:
    return 2595.0 * np.log10(1.0 + hz / 700.0)


def _mel_to_hz(mel: float) -> float:
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def _build_mel_filterbank() -> np.ndarray:
    """Build triangular mel filterbank matching C++ buildMelFilterbank()."""
    nyquist = SR / 2.0
    mel_low = _hz_to_mel(0.0)
    mel_high = _hz_to_mel(float(FMAX))

    mel_centers = np.linspace(mel_low, mel_high, N_MELS + 2)
    hz_centers = np.array([_mel_to_hz(m) for m in mel_centers])
    bin_centers = np.floor(hz_centers / nyquist * (NUM_BINS - 1)).astype(np.int32)

    weights = np.zeros((N_MELS, NUM_BINS), dtype=np.float32)
    for m in range(N_MELS):
        left = bin_centers[m]
        mid = bin_centers[m + 1]
        right = bin_centers[m + 2]

        for k in range(left, mid + 1):
            weights[m, k] = 1.0 if mid == left else (k - left) / (mid - left)
        for k in range(mid + 1, right + 1):
            weights[m, k] = 1.0 if right == mid else (right - k) / (right - mid)

    return weights


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

    spectra_buffer = np.zeros((FRAMES_PER_WINDOW, NUM_BINS), dtype=np.float32)
    write_idx = 0
    frames_collected = 0
    windows: list[np.ndarray] = []

    for hop in range(total_hops):
        start = hop * HOP_LENGTH
        segment = y[start : start + N_FFT]
        if len(segment) < N_FFT:
            break

        windowed = segment * _HANN_WINDOW
        fft_result = np.fft.rfft(windowed, n=N_FFT)
        magnitudes = np.abs(fft_result).astype(np.float32)

        spectra_buffer[write_idx] = magnitudes
        write_idx = (write_idx + 1) % FRAMES_PER_WINDOW
        frames_collected += 1

        if frames_collected >= FRAMES_PER_WINDOW and hop % STRIDE_FRAMES == 0:
            mel_out = np.zeros((N_MELS, FRAMES_PER_WINDOW), dtype=np.float32)
            for t in range(FRAMES_PER_WINDOW):
                frame_idx = (write_idx + t) % FRAMES_PER_WINDOW
                spectrum = spectra_buffer[frame_idx]
                mel_out[:, t] = _MEL_FILTERBANK @ spectrum

            max_mag = max(np.max(mel_out), 1e-8)
            mel_db = mel_out / max_mag
            mel_db = np.maximum(mel_db, 1e-8)
            mel_db = 10.0 * np.log10(mel_db)
            mel_db = np.maximum(mel_db, -80.0)

            windows.append(mel_db.astype(np.float32))

    return windows


def discover_classes(raw_dir: Path) -> dict[str, int]:
    """Discover pattern_XX_name directories, return sorted {dir_name: class_idx}."""
    dirs = sorted(
        d for d in raw_dir.iterdir()
        if d.is_dir() and d.name.startswith("pattern_")
    )
    if not dirs:
        print("ERROR: no pattern_* directories found in raw_dir", file=sys.stderr)
        sys.exit(1)
    return {d.name: i for i, d in enumerate(dirs)}


def build_dataset(raw_dir: Path, processed_dir: Path) -> tuple[np.ndarray, np.ndarray, list[dict], dict[str, int]]:
    """Walk raw_dir, extract mel windows, return X, y, meta_rows, class_map."""
    class_map = discover_classes(raw_dir)
    print(f"Discovered {len(class_map)} classes:")
    for name, idx in class_map.items():
        print(f"  {idx:>2d}: {name}")

    X_list: list[np.ndarray] = []
    y_list: list[int] = []
    meta_rows: list[dict] = []

    for class_name, class_idx in sorted(class_map.items(), key=lambda x: x[1]):
        class_dir = raw_dir / class_name
        wav_files = sorted(class_dir.glob("*.wav")) + sorted(class_dir.glob("*.WAV"))

        if not wav_files:
            print(f"WARNING: no .wav files in {class_dir} — skipping", file=sys.stderr)
            continue

        class_windows: list[np.ndarray] = []

        for wav_path in wav_files:
            print(f"  {class_name}: {wav_path.name} ... ", end="", flush=True)
            y = load_audio(wav_path)
            windows = extract_mel_windows(y)
            class_windows.extend(windows)
            print(f"{len(windows)} windows")

        # Augment small classes with time-stretch variants
        if len(class_windows) < 30:
            needed = 30 - len(class_windows)
            print(f"  {class_name}: only {len(class_windows)} windows, augmenting with {needed} time-stretch variants")
            rng = np.random.RandomState(42)
            for wav_path in wav_files:
                if len(class_windows) >= 30:
                    break
                y = load_audio(wav_path)
                for _ in range(min(5, needed)):
                    rate = rng.uniform(0.85, 1.15)
                    n_out = int(len(y) / rate)
                    indices = np.linspace(0, len(y) - 1, n_out)
                    y_aug = np.interp(indices, np.arange(len(y)), y).astype(np.float32)
                    aug_windows = extract_mel_windows(y_aug)
                    class_windows.extend(aug_windows[:needed - len(class_windows) + len(aug_windows)])

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
    return X, y_arr, meta_rows, class_map


def main() -> int:
    parser = argparse.ArgumentParser(description="Build mel dataset for 22-pattern groove model.")
    parser.add_argument("--raw-dir", type=Path,
                        default=_repo_root() / "data" / "raw")
    parser.add_argument("--out-dir", type=Path,
                        default=_repo_root() / "data" / "processed")
    args = parser.parse_args()

    raw_dir = args.raw_dir.resolve()
    out_dir = args.out_dir.resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    if not raw_dir.is_dir():
        print(f"ERROR: raw directory not found: {raw_dir}", file=sys.stderr)
        return 1

    print(f"Building C++-aligned mel dataset from {raw_dir}")
    print(f"  n_fft={N_FFT}, hop={HOP_LENGTH}, mels={N_MELS}, fmax={FMAX}")
    print()

    X, y, meta_rows, class_map = build_dataset(raw_dir, out_dir)

    print(f"\nTotal windows: {X.shape[0]}")
    print(f"X shape: {X.shape}  (expected: N, 1, 64, 32)")
    print(f"y shape: {y.shape}")
    print()

    print("Per-class window counts:")
    unique, counts = np.unique(y, return_counts=True)
    all_ok = True
    for cls_idx, count in zip(unique, counts):
        # Find class name from map
        name = next((k for k, v in class_map.items() if v == cls_idx), f"class_{cls_idx}")
        status = "✓" if count >= MIN_WINDOWS_PER_CLASS else "✗ BELOW MINIMUM"
        if count < MIN_WINDOWS_PER_CLASS:
            all_ok = False
        print(f"  {name:>30s} (class {cls_idx:>2d}): {count:>5d} windows  {status}")

    np.save(out_dir / "X_groove.npy", X)
    np.save(out_dir / "y_groove.npy", y)

    meta_path = out_dir / "meta_groove.csv"
    with meta_path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=["class_name", "class_idx", "split"])
        writer.writeheader()
        writer.writerows(meta_rows)

    # Save class map
    import json
    (out_dir / "class_map.json").write_text(
        json.dumps(class_map, indent=2) + "\n", encoding="utf-8")

    print(f"\nSaved: {out_dir / 'X_groove.npy'}")
    print(f"Saved: {out_dir / 'y_groove.npy'}")
    print(f"Saved: {meta_path}")
    print(f"Saved: {out_dir / 'class_map.json'}")

    if not all_ok:
        print(f"\nWARNING: class(es) below {MIN_WINDOWS_PER_CLASS} minimum windows.")
        print("Training may still work with augmentation, but results may be poor.")
        # Don't fail — we'll let training quality gates decide

    print("\n✓ Groove dataset ready for training.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
