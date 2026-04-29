#!/usr/bin/env python3
"""Train BassNet on MIDI-extracted piano-roll tensors and export bass_trained.onnx (BMODEL-03 / Phase 26)."""

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


def _embed_onnx_inplace(path: Path) -> None:
    raw = onnx.load(str(path), load_external_data=True)
    onnx.save_model(raw, str(path), save_as_external_data=False)


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _resolve_data_dir(data_dir: Path) -> Path:
    resolved = data_dir.resolve()
    training_root = (_repo_root() / "training").resolve()
    try:
        resolved.relative_to(training_root)
    except ValueError:
        pass
    return resolved


def _load_split(path: Path) -> tuple[torch.Tensor, torch.Tensor]:
    blob = torch.load(path, map_location="cpu", weights_only=True)
    if not isinstance(blob, dict) or "X" not in blob or "Y" not in blob:
        raise ValueError(f"{path}: expected dict with X, Y")
    x, y = blob["X"], blob["Y"]
    if x.dtype != torch.float32:
        x = x.float()
    if y.dtype != torch.float32:
        y = y.float()
    if x.shape[1] != 7:
        raise ValueError(f"{path}: X must be [N,7], got {tuple(x.shape)}")
    if y.shape[1] != 32:
        raise ValueError(f"{path}: Y must be [N,32], got {tuple(y.shape)}")
    return x, y


def main() -> int:
    p = argparse.ArgumentParser(description="Bass piano-roll training + ONNX export")
    p.add_argument(
        "--data-dir",
        type=Path,
        default=_TRAINING_DIR / "data/processed",
        help="Directory with bass_train.pt, bass_val.pt",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output dir (default: training/artifacts/bass-<UTC>/)",
    )
    p.add_argument("--seed", type=int, default=42)
    p.add_argument("--batch-size", type=int, default=64)
    p.add_argument("--max-epochs", type=int, default=200)
    p.add_argument("--patience", type=int, default=12)
    p.add_argument("--lr", type=float, default=1e-3)
    args = p.parse_args()

    random.seed(args.seed)
    np.random.seed(args.seed)
    torch.manual_seed(args.seed)

    data_dir = _resolve_data_dir(args.data_dir)
    train_path = data_dir / "bass_train.pt"
    val_path = data_dir / "bass_val.pt"
    if not train_path.is_file():
        print(f"error: missing {train_path}", file=sys.stderr)
        return 1
    if not val_path.is_file():
        print(f"error: missing {val_path}", file=sys.stderr)
        return 1

    if args.out_dir is None:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = _TRAINING_DIR / "artifacts" / f"bass-{ts}"
    else:
        out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {out_dir}", flush=True)

    x_tr_raw, y_tr = _load_split(train_path)
    x_va_raw, y_va = _load_split(val_path)

    X_train = x_tr_raw.numpy()
    mean = X_train.mean(axis=0, keepdims=True)
    std = np.maximum(X_train.std(axis=0, keepdims=True), 1e-8)

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
        "mean": mean.reshape(-1).tolist(),
        "std": std.reshape(-1).tolist(),
        "epsilon": 1e-8,
    }
    norm_stats_path.write_text(json.dumps(stats, indent=2), encoding="utf-8")

    x_tr = torch.from_numpy(((X_train - mean) / std).astype(np.float32))
    x_va = torch.from_numpy(((x_va_raw.numpy() - mean) / std).astype(np.float32))

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
    best_val_mse = float("inf")
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

    val_report = {"best_val_mse": float(best_val_mse)}
    (out_dir / "validation.json").write_text(
        json.dumps(val_report, indent=2) + "\n", encoding="utf-8"
    )

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
    _embed_onnx_inplace(onnx_path)
    print(f"Wrote {onnx_path.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
