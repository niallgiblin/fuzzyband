#!/usr/bin/env python3
"""Generate synthetic WAV fixtures used by E2E tests.

Outputs (all 48 kHz, mono, float32):
  test/fixtures/silence_5s.wav            — 5 s of digital silence
  test/fixtures/click_120bpm_10s.wav      — click train at 120 BPM for 10 s
  test/fixtures/structure_transitions.wav — composite:
                                              5 s silence
                                             10 s verse-like (1500 Hz sine, amp 0.5)
                                             10 s chorus-like (3000 Hz sine, amp 0.5)
                                              5 s silence

Usage:
  python3 scripts/generate_test_fixtures.py [--output-dir test/fixtures]

Dependencies:
  numpy, soundfile  (pip install numpy soundfile)
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

try:
    import numpy as np
    import soundfile as sf
except ImportError as exc:
    print(f"error: {exc}\nInstall with: pip install numpy soundfile", file=sys.stderr)
    sys.exit(1)

SR = 48_000  # sample rate (Hz)


def silence(duration_sec: float) -> np.ndarray:
    return np.zeros(int(duration_sec * SR), dtype=np.float32)


def sine(duration_sec: float, freq_hz: float, amplitude: float = 0.5) -> np.ndarray:
    n = int(duration_sec * SR)
    t = np.arange(n, dtype=np.float64) / SR
    return (amplitude * np.sin(2.0 * np.pi * freq_hz * t)).astype(np.float32)


def click_train(duration_sec: float, bpm: float, amplitude: float = 1.0) -> np.ndarray:
    n = int(duration_sec * SR)
    period = int(round(SR * 60.0 / bpm))
    audio = np.zeros(n, dtype=np.float32)
    for i in range(0, n, period):
        audio[i] = amplitude
    return audio


def write(path: Path, audio: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    sf.write(str(path), audio, SR, subtype="FLOAT")
    print(f"  wrote {path}  ({len(audio) / SR:.1f} s, {len(audio)} samples)")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument(
        "--output-dir",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "test" / "fixtures",
        help="Directory for generated WAV files (default: test/fixtures/)",
    )
    args = ap.parse_args(argv)
    out = Path(args.output_dir)

    print(f"Writing fixtures to {out}/")

    # silence_5s.wav
    write(out / "silence_5s.wav", silence(5.0))

    # click_120bpm_10s.wav
    write(out / "click_120bpm_10s.wav", click_train(10.0, 120.0))

    # structure_transitions.wav
    # Sections: 5 s silence → 10 s verse (1500 Hz) → 10 s chorus (3000 Hz) → 5 s silence
    transitions = np.concatenate([
        silence(5.0),
        sine(10.0, 1500.0, amplitude=0.5),
        sine(10.0, 3000.0, amplitude=0.5),
        silence(5.0),
    ])
    write(out / "structure_transitions.wav", transitions)

    return 0


if __name__ == "__main__":
    sys.exit(main())
