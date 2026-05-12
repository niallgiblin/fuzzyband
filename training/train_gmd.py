#!/usr/bin/env python3
"""Train PatternNet on merged GMD+Lakh tensors and export pattern_trained.onnx (PMODEL-02 / Phase 26)."""

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
    blob = torch.load(path, map_location="cpu", weights_only=True)
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


def _embed_onnx_inplace(path: Path) -> None:
    """Fold external-data tensors into a single .onnx file (CI-friendly)."""

    raw = onnx.load(str(path), load_external_data=True)
    onnx.save_model(raw, str(path), save_as_external_data=False)


def main() -> int:
    p = argparse.ArgumentParser(description="GMD pattern training + ONNX export")
    p.add_argument(
        "--data-dir",
        type=Path,
        default=_TRAINING_DIR / "data/processed",
        help="Directory with train.pt, val.pt or val_gmd.pt (default: training/data/processed)",
    )
    p.add_argument(
        "--val-file",
        type=str,
        default=None,
        help="Validation .pt filename (default: val_gmd.pt if present, else val.pt)",
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
        help="Output dir (default: training/artifacts/pattern-merged-<UTC>/ )",
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
    norm_stats = args.norm_stats or (data_dir / "norm_stats.json")
    train_path = data_dir / "train_merged.pt" if (data_dir / "train_merged.pt").is_file() else data_dir / "train.pt"

    # Auto-detect validation file: prefer val_gmd.pt (merged norm) over val.pt
    if args.val_file is not None:
        val_path = data_dir / args.val_file
    elif (data_dir / "val_gmd.pt").is_file():
        val_path = data_dir / "val_gmd.pt"
    else:
        val_path = data_dir / "val.pt"

    if not train_path.is_file():
        print(f"error: missing {train_path}", file=sys.stderr)
        return 1
    if not val_path.is_file():
        print(f"error: missing {val_path}", file=sys.stderr)
        return 1
    if not norm_stats.is_file():
        print(f"error: missing {norm_stats}", file=sys.stderr)
        return 1

    print(f"train: {train_path.name}  val: {val_path.name}  norm: {norm_stats.name}", flush=True)

    if args.out_dir is None:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = _TRAINING_DIR / "artifacts" / f"pattern-merged-{ts}"
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

    print(f"\nBest val_macro_f1: {best_f1:.4f}", flush=True)

    if best_f1 < 0.55:
        print(
            f"error: PMODEL-04 gate failed — best_macro_f1 {best_f1:.4f} < 0.55 (val_gmd)",
            file=sys.stderr,
        )
        return 1

    pattern_cpu = PatternNet()
    pattern_cpu.load_state_dict(best_state)
    pattern_cpu.eval()

    # ── Final validation pass with class-wise metrics ────────────────────
    final_preds: list[int] = []
    final_labels: list[int] = []
    with torch.no_grad():
        for bx, by in val_loader:
            bx = bx.to(dtype=torch.float32)
            logits = pattern_cpu(bx)
            final_preds.extend(torch.argmax(logits, dim=-1).cpu().numpy().tolist())
            final_labels.extend(by.cpu().numpy().tolist())

    class_f1 = f1_score(final_labels, final_preds, average=None, zero_division=0)
    from sklearn.metrics import confusion_matrix as sk_confusion_matrix
    cm = sk_confusion_matrix(final_labels, final_preds, labels=list(range(7)))

    print("\nCLASS-WISE F1 (best model on val set):", flush=True)
    for c in range(7):
        count = sum(1 for lb in final_labels if lb == c)
        f1_val = float(class_f1[c]) if c < len(class_f1) else 0.0
        print(f"  class {c}: F1={f1_val:.4f}  (n={count})", flush=True)

    print("\nCONFUSION MATRIX (rows=true, cols=pred):", flush=True)
    header = "      " + " ".join(f"pred{c:>4d}" for c in range(7))
    print(header, flush=True)
    for r in range(7):
        row_str = " ".join(f"{cm[r][c]:>8d}" for c in range(7))
        print(f"true{r}: {row_str}", flush=True)

    val_report = {
        "best_macro_f1": float(best_f1),
        "class_f1": [float(x) for x in class_f1],
        "confusion_matrix": cm.tolist(),
    }
    (out_dir / "validation.json").write_text(
        json.dumps(val_report, indent=2) + "\n", encoding="utf-8"
    )

    # ── Save best model weights (PyTorch state dict) ────────────────────
    torch.save({"state_dict": best_state, "best_macro_f1": best_f1}, out_dir / "best_model.pt")

    # ── ONNX export ──────────────────────────────────────────────────────
    export = PatternOnnxExport.from_norm_stats(norm_stats, pattern_cpu)
    export.eval()

    onnx_path = out_dir / "pattern_trained.onnx"
    dummy_x = torch.zeros((1, 7), dtype=torch.float32)
    torch.onnx.export(
        export,
        dummy_x,
        str(onnx_path),
        input_names=["X"],
        output_names=["Y"],
        opset_version=17,
    )
    onnx.checker.check_model(onnx.load(str(onnx_path)))
    _embed_onnx_inplace(onnx_path)
    print(f"\nWrote {onnx_path.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
