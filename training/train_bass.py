#!/usr/bin/env python3
"""Train BassNet on synthetic metal-key pitch data and export bass_trained.onnx (BMODEL-01)."""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import onnx
import torch
import torch.nn as nn
from torch.utils.data import DataLoader, TensorDataset

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from models.bass_model import BassNet, BassOnnxExport  # noqa: E402


def _generate_synthetic(
    n: int = 3000, seed: int = 42
) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    """Generate synthetic metal-key bass training data.

    Returns (X_train, Y_train, X_val, Y_val) as float32 numpy arrays.
    X columns (7): bpm, rmsEnergy, spectralCentroid, highFreqFlux, state, pitchRootMidi, pitchConfidence
    Y columns (4): proposal_confidence, root_midi, duration_beats, margin
    """
    rng = np.random.default_rng(seed)

    X = np.zeros((n, 7), dtype=np.float32)
    X[:, 0] = rng.uniform(80, 220, n).astype(np.float32)  # bpm (D-19-05)
    X[:, 1] = rng.uniform(0.01, 0.9, n).astype(np.float32)  # rmsEnergy
    X[:, 2] = rng.uniform(1000, 6000, n).astype(np.float32)  # spectralCentroid Hz
    X[:, 3] = rng.uniform(0, 0.5, n).astype(np.float32)  # highFreqFlux
    X[:, 4] = rng.integers(0, 4, n).astype(np.float32)  # state SILENT=0..BREAKDOWN=3
    # pitchRootMidi: cycles E2=40, A2=45, B1=35 in equal thirds (guaranteed balance — D-19-06)
    roots = np.array([40, 45, 35], dtype=np.float32)
    root_col = np.tile(roots, n // 3 + 1)[:n]
    rng.shuffle(root_col)
    X[:, 5] = root_col
    X[:, 6] = rng.uniform(0.3, 1.0, n).astype(np.float32)  # pitchConfidence

    # Labels (D-19-01/02/03/04)
    Y = np.zeros((n, 4), dtype=np.float32)
    Y[:, 0] = X[:, 6]  # proposal_confidence = pitchConfidence (D-19-03)
    Y[:, 1] = X[:, 5]  # root_midi = pitchRootMidi echo (D-19-01)
    Y[:, 2] = 1.0  # duration_beats = fixed 1.0 (D-19-02)
    Y[:, 3] = 0.0  # margin = 0.0 reserved (D-19-04)

    n_tr = int(n * 0.8)  # 80/20 split (D-19-07)
    return X[:n_tr], Y[:n_tr], X[n_tr:], Y[n_tr:]


def main() -> int:
    p = argparse.ArgumentParser(description="Synthetic bass model training + ONNX export")
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output dir (default: training/artifacts/bass-synth-<UTC>/)",
    )
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--batch-size", type=int, default=64)
    p.add_argument("--max-epochs", type=int, default=200)
    p.add_argument("--patience", type=int, default=8)
    p.add_argument("--lr", type=float, default=1e-3)
    args = p.parse_args()

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    if args.out_dir is None:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = _TRAINING_DIR / "artifacts" / f"bass-synth-{ts}"
    else:
        out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {out_dir}", flush=True)

    X_train, Y_train, X_val, Y_val = _generate_synthetic(n=3000, seed=args.seed)

    # Compute norm stats from training split only (D-19-08)
    mean = X_train.mean(axis=0)
    std = X_train.std(axis=0)
    std = np.maximum(std, 1e-8)  # floor (consistent with BassOnnxExport._EPS)

    norm_stats_path = out_dir / "bass_norm_stats.json"
    stats = {
        "feature_order": [
            "bpm",
            "rmsEnergy",
            "spectralCentroid",
            "highFreqFlux",
            "state_float",
            "pitchRootMidi",
            "pitchConfidence",
        ],
        "mean": mean.tolist(),
        "std": std.tolist(),
        "epsilon": 1e-8,
    }
    norm_stats_path.write_text(json.dumps(stats, indent=2), encoding="utf-8")

    # Normalize X for training (D-19-08: train on normalized; bake same norm in ONNX export)
    X_train_norm = ((X_train - mean) / std).astype(np.float32)
    X_val_norm = ((X_val - mean) / std).astype(np.float32)

    x_tr = torch.from_numpy(X_train_norm)
    y_tr = torch.from_numpy(Y_train)  # float32 regression targets
    x_va = torch.from_numpy(X_val_norm)
    y_va = torch.from_numpy(Y_val)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

    train_loader = DataLoader(
        TensorDataset(x_tr, y_tr),
        batch_size=args.batch_size,
        shuffle=True,
        drop_last=False,
    )
    val_loader = DataLoader(
        TensorDataset(x_va, y_va),
        batch_size=args.batch_size,
        shuffle=False,
    )

    model = BassNet().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    mse = nn.MSELoss()

    metrics_path = out_dir / "metrics.jsonl"
    best_state: dict[str, torch.Tensor] | None = None
    best_val_mse = float("inf")  # lower is better (D-19-11)
    stale = 0

    for epoch in range(1, args.max_epochs + 1):
        model.train()
        train_losses: list[float] = []
        for bx, by in train_loader:
            bx = bx.to(device, dtype=torch.float32)
            by = by.to(device, dtype=torch.float32)
            opt.zero_grad(set_to_none=True)
            y_hat = model(bx)
            loss = mse(y_hat, by)
            if not torch.isfinite(loss):
                print("error: non-finite train loss — stopping", file=sys.stderr)
                return 1
            loss.backward()
            opt.step()
            train_losses.append(float(loss.detach()))

        model.eval()
        val_losses: list[float] = []
        with torch.no_grad():
            for bx, by in val_loader:
                bx = bx.to(device, dtype=torch.float32)
                by = by.to(device, dtype=torch.float32)
                y_hat = model(bx)
                vloss = nn.functional.mse_loss(y_hat, by, reduction="mean")
                val_losses.append(float(vloss))

        train_loss = float(np.mean(train_losses)) if train_losses else float("nan")
        val_loss = float(np.mean(val_losses)) if val_losses else float("nan")
        if not math.isfinite(val_loss):
            print("error: non-finite val loss — stopping", file=sys.stderr)
            return 1

        rec = {
            "epoch": epoch,
            "train_loss": train_loss,
            "val_loss": val_loss,
        }
        with metrics_path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(rec) + "\n")
        print(json.dumps(rec), flush=True)

        # Early stopping: lower val MSE is better (D-19-11)
        if val_loss < best_val_mse:
            best_val_mse = val_loss
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}
            stale = 0
        else:
            stale += 1
            if stale >= args.patience:
                print(
                    f"Early stop: val_loss did not improve for {args.patience} epochs.",
                    flush=True,
                )
                break

    if best_state is None:
        print("error: no model state captured", file=sys.stderr)
        return 1

    bass_cpu = BassNet()
    bass_cpu.load_state_dict(best_state)
    bass_cpu.eval()
    export = BassOnnxExport.from_norm_stats(norm_stats_path, bass_cpu)
    export.eval()

    onnx_path = out_dir / "bass_trained.onnx"
    dummy_xb = torch.zeros((1, 7), dtype=torch.float32)
    torch.onnx.export(
        export,
        dummy_xb,
        str(onnx_path),
        input_names=["X_bass"],
        output_names=["Y_bass"],
        opset_version=17,
    )
    onnx.checker.check_model(onnx.load(str(onnx_path)))
    print(f"Wrote {onnx_path.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
