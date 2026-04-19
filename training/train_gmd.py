#!/usr/bin/env python3
"""Train PatternNet on Phase 17 GMD tensors and export pattern_trained.onnx (PMODEL-02)."""

from __future__ import annotations

import argparse
import json
import math
import random
import sys
import warnings
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import onnx
import torch
import torch.nn as nn
from sklearn.metrics import f1_score
from torch.utils.data import DataLoader, TensorDataset

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from models.pattern_model import PatternNet, PatternOnnxExport  # noqa: E402


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _resolve_data_dir(data_dir: Path) -> Path:
    """Prefer paths under repo `training/`; warn if user points elsewhere."""
    resolved = data_dir.resolve()
    training_root = (_repo_root() / "training").resolve()
    try:
        resolved.relative_to(training_root)
    except ValueError:
        warnings.warn(
            "--data-dir resolves outside training/; only use trusted paths.",
            stacklevel=2,
        )
    return resolved


def _load_split(path: Path) -> tuple[torch.Tensor, torch.Tensor]:
    blob = torch.load(path, map_location="cpu", weights_only=False)
    if not isinstance(blob, dict) or "X" not in blob or "Y" not in blob:
        raise ValueError(f"{path}: expected dict with X, Y")
    x, y = blob["X"], blob["Y"]
    if y.dtype != torch.long:
        y = y.long()
    y_min = int(y.min().item())
    y_max = int(y.max().item())
    if y_min < 0 or y_max > 6:
        raise ValueError(f"{path}: Y must be in 0..6, got [{y_min}, {y_max}]")
    return x, y


def _class_weights(y: torch.Tensor) -> torch.Tensor:
    """Inverse-frequency weights; zero counts treated as 1; normalized to mean 1."""
    counts = torch.bincount(y, minlength=7).to(dtype=torch.float32)
    n = float(y.numel())
    w = torch.tensor(
        [n / (7.0 * max(float(counts[i].item()), 1.0)) for i in range(7)],
        dtype=torch.float32,
    )
    w = w / w.mean()
    return w


def main() -> int:
    p = argparse.ArgumentParser(description="GMD pattern training + ONNX export")
    p.add_argument(
        "--data-dir",
        type=Path,
        default=_TRAINING_DIR / "data/processed",
        help="Directory with train.pt, val.pt (default: training/data/processed)",
    )
    p.add_argument(
        "--norm-stats",
        type=Path,
        default=None,
        help="norm_stats.json (default: {data-dir}/norm_stats.json)",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output dir (default: training/artifacts/pattern-gmd-<UTC>/ )",
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

    data_dir = _resolve_data_dir(args.data_dir)
    norm_stats = args.norm_stats or (data_dir / "norm_stats.json")
    train_path = data_dir / "train.pt"
    val_path = data_dir / "val.pt"
    if not train_path.is_file() or not val_path.is_file():
        print(f"error: missing {train_path} or {val_path}", file=sys.stderr)
        return 1
    if not norm_stats.is_file():
        print(f"error: missing {norm_stats}", file=sys.stderr)
        return 1

    if args.out_dir is None:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = _TRAINING_DIR / "artifacts" / f"pattern-gmd-{ts}"
    else:
        out_dir = args.out_dir
    out_dir.mkdir(parents=True, exist_ok=True)
    print(f"Output directory: {out_dir}", flush=True)

    x_tr, y_tr = _load_split(train_path)
    x_va, y_va = _load_split(val_path)

    device = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    weights = _class_weights(y_tr).to(device)
    ce = nn.CrossEntropyLoss(weight=weights)

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

    model = PatternNet().to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)

    metrics_path = out_dir / "metrics.jsonl"
    best_state: dict[str, torch.Tensor] | None = None
    best_f1 = -1.0
    stale = 0

    for epoch in range(1, args.max_epochs + 1):
        model.train()
        train_losses: list[float] = []
        for bx, by in train_loader:
            bx = bx.to(device, dtype=torch.float32)
            by = by.to(device)
            opt.zero_grad(set_to_none=True)
            logits = model(bx)
            loss = ce(logits, by)
            if not torch.isfinite(loss):
                print("error: non-finite train loss — stopping", file=sys.stderr)
                return 1
            loss.backward()
            opt.step()
            train_losses.append(float(loss.detach()))

        model.eval()
        val_losses: list[float] = []
        preds: list[int] = []
        labels: list[int] = []
        with torch.no_grad():
            for bx, by in val_loader:
                bx = bx.to(device, dtype=torch.float32)
                by = by.to(device)
                logits = model(bx)
                vloss = nn.functional.cross_entropy(
                    logits, by, weight=weights, reduction="mean"
                )
                val_losses.append(float(vloss))
                preds.extend(torch.argmax(logits, dim=-1).cpu().numpy().tolist())
                labels.extend(by.cpu().numpy().tolist())

        train_loss = float(np.mean(train_losses)) if train_losses else float("nan")
        val_loss = float(np.mean(val_losses)) if val_losses else float("nan")
        if not math.isfinite(val_loss):
            print("error: non-finite val loss — stopping", file=sys.stderr)
            return 1

        v_macro = f1_score(labels, preds, average="macro", zero_division=0)

        rec = {
            "epoch": epoch,
            "train_loss": train_loss,
            "val_loss": val_loss,
            "val_macro_f1": float(v_macro),
        }
        with metrics_path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(rec) + "\n")
        print(json.dumps(rec), flush=True)

        if v_macro > best_f1:
            best_f1 = v_macro
            best_state = {k: v.detach().cpu().clone() for k, v in model.state_dict().items()}
            stale = 0
        else:
            stale += 1
            if stale >= args.patience:
                print(f"Early stop: val_macro_f1 did not improve for {args.patience} epochs.", flush=True)
                break

    if best_state is None:
        print("error: no model state captured", file=sys.stderr)
        return 1

    pattern_cpu = PatternNet()
    pattern_cpu.load_state_dict(best_state)
    pattern_cpu.eval()
    export = PatternOnnxExport.from_norm_stats(norm_stats, pattern_cpu)
    export.eval()

    onnx_path = out_dir / "pattern_trained.onnx"
    dummy_x = torch.zeros((1, 5), dtype=torch.float32)
    torch.onnx.export(
        export,
        dummy_x,
        str(onnx_path),
        input_names=["X"],
        output_names=["Y"],
        opset_version=17,
    )
    onnx.checker.check_model(onnx.load(str(onnx_path)))
    print(f"Wrote {onnx_path.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
