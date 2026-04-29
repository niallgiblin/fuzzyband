#!/usr/bin/env python3
"""Behavioral gate for BassNet: interval vocabulary coverage on ONNX sweep (D-26-04)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import onnxruntime as ort
import torch


def _pitch_class_interval(off: int) -> int:
    """Absolute pitch-class interval 0..11 from signed semitone offset."""

    a = abs(int(off)) % 12
    return int(a)


def _gate_score(off: int) -> dict[str, bool]:
    """Bucket rounded offset into the four Plan 26-02 gate categories (pitch-class)."""
    pc = _pitch_class_interval(off)
    return {
        # Perfect fifth class (+7 / −5); fourth (+5) is a common inversion — include as P5 gate
        "P5": pc in (5, 7),
        "m3": pc in (3, 9),
        "tritone": pc == 6,
        "chromatic": pc in (1, 11),
    }


def _decode_offsets(y: np.ndarray) -> list[int]:
    """Collect rounded pitch offsets from interleaved [pitch, vel] piano-roll."""
    row = y.reshape(-1)
    out: list[int] = []
    for step in range(16):
        po = float(row[step * 2])
        vel = float(row[step * 2 + 1])
        if vel <= 1e-6:
            continue
        out.append(int(round(po)))
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description="Bass ONNX interval vocabulary gate")
    ap.add_argument("--model", type=Path, required=True, help="bass_trained.onnx path")
    ap.add_argument(
        "--val-pt",
        type=Path,
        default=None,
        help="Optional bass_val.pt — merge random rows into evaluation (default: training/data/processed/bass_val.pt if present)",
    )
    ap.add_argument("--output", type=Path, default=None, help="Optional report path")
    args = ap.parse_args()

    if not args.model.is_file():
        print(f"error: missing model {args.model}", file=sys.stderr)
        return 1

    sess = ort.InferenceSession(str(args.model), providers=["CPUExecutionProvider"])
    xi = sess.get_inputs()[0].name

    bpms = (120.0, 160.0, 200.0)
    roots = (40.0, 45.0, 35.0)  # E2, A2, B1
    state_vals = (0.0, 1.0, 2.0)

    all_offsets: list[int] = []
    sweeps = 0

    for bpm in bpms:
        for root in roots:
            for st in state_vals:
                if st == 0.0:
                    continue
                for rms in (0.05, 0.3, 0.85):
                    for cent in (1500.0, 3500.0):
                        for flux in (0.05, 0.25):
                            x = np.array(
                                [[bpm, rms, cent, flux, st, root, 0.95]],
                                dtype=np.float32,
                            )
                            y = sess.run(None, {xi: x})[0]
                            all_offsets.extend(_decode_offsets(np.asarray(y)))
                            sweeps += 1

    val_path = args.val_pt
    if val_path is None:
        cand = Path(__file__).resolve().parent.parent / "training/data/processed/bass_val.pt"
        val_path = cand if cand.is_file() else None

    sampled = 0
    if val_path is not None and val_path.is_file():
        d = torch.load(val_path, map_location="cpu", weights_only=True)
        xv = d["X"].numpy()
        rng = np.random.default_rng(42)
        take = min(6000, len(xv))
        ix = rng.choice(len(xv), size=take, replace=False)
        for i in ix:
            y = sess.run(None, {xi: xv[i : i + 1].astype(np.float32)})[0]
            all_offsets.extend(_decode_offsets(np.asarray(y)))
            sampled += 1

    total = len(all_offsets)
    if total == 0:
        print("error: no active bass steps — gate failed", file=sys.stderr)
        return 1

    counts: dict[str, int] = {k: 0 for k in ("P5", "m3", "tritone", "chromatic")}
    for off in all_offsets:
        g = _gate_score(off)
        for k in counts:
            if g[k]:
                counts[k] += 1

    pct = {k: 100.0 * counts[k] / total for k in counts}

    lines = [
        f"Bass interval gate — grid sweeps: {sweeps}, val samples: {sampled}, active offsets: {total}",
        f"P5: {pct['P5']:.2f}%  (n={counts['P5']})",
        f"m3: {pct['m3']:.2f}%  (n={counts['m3']})",
        f"tritone: {pct['tritone']:.2f}%  (n={counts['tritone']})",
        f"chromatic: {pct['chromatic']:.2f}%  (n={counts['chromatic']})",
    ]
    report = "\n".join(lines)
    print(report, flush=True)
    if args.output is not None:
        args.output.write_text(report + "\n", encoding="utf-8")

    gate = 1.0
    ok = all(pct[k] >= gate for k in counts)
    if not ok:
        print(
            "error: behavioral gate failed — need P5, m3, tritone, chromatic each ≥ 1%",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
