#!/usr/bin/env python3
"""
Phase 15 stub: synthetic train + ONNX export for pattern + bass heads.

Pattern graph: input **X** float32 **[1, 5]** → output **Y** int64 **[1]** per `docs/ONNX_IO.md`.
`OnnxInference.cpp` reads `Y[0]` as int64 (tensor rank 1, length 1).

Bass graph: **X_bass** float32 **[1, 7]** → **Y_bass** float32 **[1, 32]** per `docs/BASS_ONNX_IO.md`.
"""

from __future__ import annotations

import argparse
import json
import random
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import onnx
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset


def _positive_epochs(x: str) -> int:
    v = int(x)
    if v < 1:
        raise argparse.ArgumentTypeError("must be an integer >= 1")
    return v


class PatternStub(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.linear = nn.Linear(5, 7)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.linear(x)


class BassStub(nn.Module):
    def __init__(self) -> None:
        super().__init__()
        self.linear = nn.Linear(7, 4)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.linear(x)


class PatternOnnxExport(nn.Module):
    """X [1,5] -> Y [1] int64 — matches plugin expectation (single row index)."""

    def __init__(self, linear: nn.Linear) -> None:
        super().__init__()
        self.linear = linear

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        logits = self.linear(x)
        y = torch.argmax(logits, dim=-1, keepdim=True).to(torch.int64)
        return y.reshape(-1)


def synthetic_tensors(
    n: int, seed: int
) -> tuple[torch.Tensor, torch.Tensor, torch.Tensor, torch.Tensor]:
    rng = np.random.default_rng(seed)
    xp = rng.random((n, 5)).astype(np.float32)
    xp[:, 0] = np.clip(xp[:, 0] * 200.0 + 80.0, 80.0, 220.0)
    yp = rng.integers(0, 7, size=(n,), dtype=np.int64)

    xb = rng.random((n, 7)).astype(np.float32)
    xb[:, 0] = np.clip(xb[:, 0] * 200.0 + 80.0, 80.0, 220.0)
    yb = np.zeros((n, 4), dtype=np.float32)
    yb[:, 0] = rng.random(n).astype(np.float32)
    yb[:, 1] = rng.uniform(28.0, 64.0, size=(n,)).astype(np.float32)
    yb[:, 2] = rng.uniform(0.01, 4.0, size=(n,)).astype(np.float32)
    yb[:, 3] = rng.normal(0.0, 0.1, size=(n,)).astype(np.float32)

    return (
        torch.from_numpy(xp),
        torch.from_numpy(yp),
        torch.from_numpy(xb),
        torch.from_numpy(yb),
    )


def main() -> int:
    p = argparse.ArgumentParser(description="PYTR stub train + ONNX export (synthetic data)")
    p.add_argument("--epochs", type=_positive_epochs, default=3)
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output directory (default: training/artifacts/pytr-stub-<UTC timestamp>)",
    )
    p.add_argument("--seed", type=int, default=42)
    args = p.parse_args()

    torch.manual_seed(args.seed)
    random.seed(args.seed)
    np.random.seed(args.seed)

    repo_root = Path(__file__).resolve().parent.parent
    if args.out_dir is None:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = repo_root / "training" / "artifacts" / f"pytr-stub-{ts}"
    else:
        out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {out_dir}", flush=True)

    n_samples = 64
    batch_size = 8
    xp, yp, xb, yb = synthetic_tensors(n_samples, args.seed)

    loader = DataLoader(
        TensorDataset(xp, yp, xb, yb),
        batch_size=batch_size,
        shuffle=True,
    )

    pattern = PatternStub()
    bass = BassStub()
    ce = nn.CrossEntropyLoss()
    bass_loss_fn = nn.SmoothL1Loss()
    opt = torch.optim.Adam(
        list(pattern.parameters()) + list(bass.parameters()),
        lr=1e-2,
    )

    metrics_path = out_dir / "metrics.jsonl"

    for epoch in range(args.epochs):
        p_losses: list[float] = []
        b_losses: list[float] = []
        for bx, by_p, bx_b, by_b in loader:
            opt.zero_grad()
            plog = pattern(bx)
            bl = bass(bx_b)
            lp = ce(plog, by_p)
            lb = bass_loss_fn(bl, by_b)
            loss = lp + lb
            loss.backward()
            opt.step()
            p_losses.append(float(lp.detach()))
            b_losses.append(float(lb.detach()))
        rec = {
            "epoch": epoch + 1,
            "pattern_loss": float(np.mean(p_losses)),
            "bass_loss": float(np.mean(b_losses)),
        }
        line = json.dumps(rec) + "\n"
        with metrics_path.open("a", encoding="utf-8") as fh:
            fh.write(line)
        print(line.strip(), flush=True)

    pattern.eval()
    bass.eval()
    pat_export = PatternOnnxExport(pattern.linear)

    dummy_x = torch.zeros((1, 5), dtype=torch.float32)
    dummy_xb = torch.zeros((1, 7), dtype=torch.float32)

    pat_path = out_dir / "pattern_trained.onnx"
    bass_path = out_dir / "bass_trained.onnx"

    torch.onnx.export(
        pat_export,
        dummy_x,
        str(pat_path),
        input_names=["X"],
        output_names=["Y"],
        opset_version=17,
    )
    torch.onnx.export(
        bass,
        dummy_xb,
        str(bass_path),
        input_names=["X_bass"],
        output_names=["Y_bass"],
        opset_version=17,
    )

    onnx.checker.check_model(onnx.load(str(pat_path)))
    onnx.checker.check_model(onnx.load(str(bass_path)))

    print(f"Wrote {pat_path.resolve()}", flush=True)
    print(f"Wrote {bass_path.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
