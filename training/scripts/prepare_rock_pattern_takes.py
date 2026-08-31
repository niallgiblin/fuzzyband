#!/usr/bin/env python3
"""Assemble rendered rock-pattern reference takes into data/raw for the 28-class retrain.

The 6 rock patterns (22-27) are authored in src/midi/MidiPatternLibrary.cpp but have
NO reference audio (data/raw only has pattern_00..21). To unblock the G1 retrain you
must render them. This script:

  1. Reads staged WAVs for each rock pattern (one folder per pattern).
  2. Validates the format MATCHES the existing reference takes (mono, 44.1 kHz, 24-bit)
     so the 6 extra classes stay in-mel-distribution with classes 0-21.
  3. Copies/renames them into data/raw/pattern_<idx>_<slug>/ as pattern_<idx>_<slug>_N.wav.
  4. Prints the exact next pipeline commands (rebuild dataset -> retrain -> export).

Render recipe (see docs/ROCK_PATTERN_RENDER.md): import the MIDI onto a GM drum kit
(channel 10) + bass (channel 2), LOOP the pattern for ~10-15s, export ONE mono 44.1 kHz
24-bit WAV per take, at least 3 takes per pattern (so the grouped split has a real
held-out class; every class with only 1-2 takes is flagged by the dataset builder).

Usage:
  python3 training/scripts/prepare_rock_pattern_takes.py --staging /path/to/rendered [--dry-run]

Staging layout expected:
  --staging/pattern_22/*.wav        (Rock Backbeat)
  --staging/pattern_23/*.wav        (Rock Half-Time)
  --staging/pattern_24/*.wav        (Rock Shuffle)
  --staging/pattern_25/*.wav        (Punk D-Beat)
  --staging/pattern_26/*.wav        (Rock Ballad)
  --staging/pattern_27/*.wav        (Rock 6/8 Feel)
"""

from __future__ import annotations

import argparse
import shutil
import sys
import wave
from pathlib import Path

_REPO_ROOT = Path(__file__).resolve().parents[2]
_RAW_DIR = _REPO_ROOT / "data" / "raw"

# pattern index -> class slug (matches tools/export_patterns.cpp slugify)
ROCK_PATTERNS = {
    22: "rock_backbeat",
    23: "rock_half_time",
    24: "rock_shuffle",
    25: "punk_d_beat",
    26: "rock_ballad",
    27: "rock_6_8_feel",
}

TARGET_SR = 44100
TARGET_CHANNELS = 1
TARGET_SAMPWIDTH = 3      # 24-bit
MIN_TAKES = 3             # grouped split wants >=3 source takes for a true held-out class
MIN_SECONDS = 8.0         # ~several bars so each take yields enough mel windows


def validate_wav(path: Path) -> tuple[bool, str]:
    try:
        with wave.open(str(path), "rb") as w:
            ch = w.getnchannels()
            sr = w.getframerate()
            sw = w.getsampwidth()
            dur = w.getnframes() / sr if sr else 0.0
    except Exception as e:  # noqa: BLE001
        return False, f"cannot read: {e}"
    if sr != TARGET_SR:
        return False, f"sample rate {sr} Hz (want {TARGET_SR})"
    if ch != TARGET_CHANNELS:
        return False, f"{ch} channel(s) (want mono {TARGET_CHANNELS})"
    if sw != TARGET_SAMPWIDTH:
        return False, f"{sw * 8}-bit (want 24-bit)"
    if dur < MIN_SECONDS:
        return False, f"only {dur:.1f}s (want >= {MIN_SECONDS:.0f}s so each take yields enough mel windows)"
    return True, f"ok ({dur:.1f}s, mono {sr} Hz / {sw * 8}-bit)"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--staging", type=Path, required=True, help="dir with pattern_22..27 WAVs")
    parser.add_argument("--dry-run", action="store_true", help="validate only, do not copy")
    args = parser.parse_args()

    staging = args.staging.resolve()
    if not staging.is_dir():
        print(f"ERROR: staging dir not found: {staging}", file=sys.stderr)
        return 1

    all_ok = True
    for idx, slug in ROCK_PATTERNS.items():
        src_dir = staging / f"pattern_{idx:02d}"
        if not src_dir.is_dir():
            print(f"  [{idx:02d}] {slug}: MISSING folder {src_dir}")
            all_ok = False
            continue
        wavs = sorted(list(src_dir.glob("*.wav")) + list(src_dir.glob("*.WAV")))
        if not wavs:
            print(f"  [{idx:02d}] {slug}: no .wav files in {src_dir}")
            all_ok = False
            continue
        print(f"  [{idx:02d}] {slug}: {len(wavs)} take(s)")
        for w in wavs:
            ok, msg = validate_wav(w)
            print(f"        {w.name}: {msg}")
            if not ok:
                all_ok = False

        if len(wavs) < MIN_TAKES:
            print(f"        WARNING: {len(wavs)} take(s) < {MIN_TAKES} -> grouped split will have no "
                  f"held-out class ({slug}); record at least {MIN_TAKES} takes.")

        if args.dry_run:
            continue

        dest_dir = _RAW_DIR / f"pattern_{idx:02d}_{slug}"
        dest_dir.mkdir(parents=True, exist_ok=True)
        for n, w in enumerate(wavs, start=1):
            dest = dest_dir / f"pattern_{idx:02d}_{slug}_{n}.wav"
            shutil.copy2(w, dest)
            print(f"        -> {dest.relative_to(_REPO_ROOT)}")

    print()
    if not all_ok:
        print(f"NOTE: fix the flagged issues above (format matches the existing 0-21 reference "
              f"takes: mono {TARGET_SR} Hz / {TARGET_SAMPWIDTH * 8}-bit, >= {MIN_SECONDS:.0f}s).")
    print("Next steps (see docs/ROCK_PATTERN_RENDER.md):")
    print("  1. python3 training/scripts/build_mel_groove_dataset.py")
    print("  2. python3 training/train_groove_model.py --device mps --epochs 80")
    print("  3. python3 training/export_centroids.py --embedding-dim 128 \\")
    print("        --checkpoint training/models/best_groove_model.pt")
    print("  4. rebuild the plugin (regenerates assets/metal_groove.onnx + pattern_embeddings.h)")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
