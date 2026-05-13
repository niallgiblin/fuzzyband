#!/usr/bin/env python3
"""Train StructureNet on [12,7] windows and export structure_trained.onnx (STRUC-04 / Phase 26)."""

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
from sklearn.metrics import confusion_matrix as sk_confusion_matrix
from sklearn.metrics import f1_score
from torch.utils.data import DataLoader, TensorDataset

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from models.structure_model import StructureNet, StructureOnnxExport  # noqa: E402


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
    if y_min < 0 or y_max > 2:
        raise ValueError(f"{path}: Y must be in 0..2, got [{y_min}, {y_max}]")
    if len(x.shape) != 3 or x.shape[1] != 12 or x.shape[2] != 7:
        raise ValueError(f"{path}: X must be [N,12,7], got {tuple(x.shape)}")
    return x, y


def _class_weights(y: torch.Tensor) -> torch.Tensor:
    counts = torch.bincount(y, minlength=3).to(dtype=torch.float32)
    n = float(y.numel())
    w = torch.tensor([n / (3.0 * max(float(counts[i].item()), 1.0)) for i in range(3)], dtype=torch.float32)
    w = w / w.mean()
    return w


def main() -> int:
    p = argparse.ArgumentParser(description="Structure classifier training + ONNX export")
    p.add_argument(
        "--data-dir",
        type=Path,
        default=_TRAINING_DIR / "data/processed",
        help="Directory with structure_train.pt, structure_val.pt",
    )
    p.add_argument(
        "--out-dir",
        type=Path,
        default=None,
        help="Output dir (default: training/artifacts/structure-<UTC>/)",
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
    train_path = data_dir / "structure_train.pt"
    val_path = data_dir / "structure_val.pt"
    if not train_path.is_file():
        print(f"error: missing {train_path}", file=sys.stderr)
        return 1
    if not val_path.is_file():
        print(f"error: missing {val_path}", file=sys.stderr)
        return 1

    if args.out_dir is None:
        ts = datetime.now(timezone.utc).strftime("%Y%m%d-%H%M%S")
        out_dir = _TRAINING_DIR / "artifacts" / f"structure-{ts}"
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

    model = StructureNet().to(device)
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
                vloss = nn.functional.cross_entropy(logits, by, weight=weights, reduction="mean")
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

    # D-08: rule-path agreement — anchor row (index 11), column 4 = state_float
    y_va_np = y_va.numpy()
    anchor_state_norm = x_va[:, 11, 4].numpy()
    rule_preds = np.array([int(np.clip(round(float(v)), 0, 2)) for v in anchor_state_norm])
    rule_agreement_rate = float(f1_score(y_va_np, rule_preds, average="macro", zero_division=0))
    print(f"Rule-path agreement rate (macro-F1): {rule_agreement_rate:.4f}", flush=True)

    # D-07: dual gate — model must beat rule-path oracle AND clear fixed floor
    fixed_macro_f1_floor = 0.80
    f1_floor_passed = best_f1 >= fixed_macro_f1_floor
    rule_gate_passed = best_f1 >= rule_agreement_rate
    gate_passed = f1_floor_passed and rule_gate_passed

    # Write norm stats (Plan 03 consumer) — run on every execution including gate failures
    norm5_path = data_dir / "norm_stats.json"
    norm5 = json.loads(norm5_path.read_text(encoding="utf-8"))
    raw_mean5 = np.array(norm5["mean"], dtype=np.float32)
    raw_std5 = np.array(norm5["std"], dtype=np.float32)
    raw_mean7 = np.concatenate([raw_mean5, [60.0, 0.0]])
    raw_std7 = np.concatenate([raw_std5, [1e-8, 1e-8]])
    struct_norm_stats = {
        "feature_order": [
            "bpm",
            "rmsEnergy",
            "spectralCentroid",
            "highFreqFlux",
            "state_float",
            "pitchRootMidi",
            "pitchConfidence",
        ],
        "mean": raw_mean7.tolist(),
        "std": raw_std7.tolist(),
        "epsilon": 1e-8,
        "note": "Features 5 (pitchRootMidi) and 6 (pitchConfidence) are constant training placeholders (60.0 and 0.0 respectively); std clamped to epsilon to avoid division by zero.",
    }
    struct_norm_path = out_dir / "structure_norm_stats.json"
    struct_norm_path.write_text(json.dumps(struct_norm_stats, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {struct_norm_path.resolve()}", flush=True)

    net_cpu = StructureNet()
    net_cpu.load_state_dict(best_state)
    net_cpu.eval()

    final_preds: list[int] = []
    final_labels: list[int] = []
    with torch.no_grad():
        for bx, by in val_loader:
            bx = bx.to(dtype=torch.float32)
            logits = net_cpu(bx)
            final_preds.extend(torch.argmax(logits, dim=-1).cpu().numpy().tolist())
            final_labels.extend(by.cpu().numpy().tolist())

    class_f1 = f1_score(final_labels, final_preds, average=None, zero_division=0)
    cm = sk_confusion_matrix(final_labels, final_preds, labels=list(range(3)))

    print("\nCLASS-WISE F1 (best model on val):", flush=True)
    for c in range(3):
        count = sum(1 for lb in final_labels if lb == c)
        print(f"  class {c}: F1={class_f1[c]:.4f}  (n={count})", flush=True)

    print("\nCONFUSION MATRIX (rows=true, cols=pred):", flush=True)
    for r in range(3):
        row_str = " ".join(f"{cm[r][c]:>8d}" for c in range(3))
        print(f"true{r}: {row_str}", flush=True)

    val_report = {
        "best_macro_f1": float(best_f1),
        "rule_agreement_rate": rule_agreement_rate,
        "fixed_macro_f1_floor": fixed_macro_f1_floor,
        "gate_passed": gate_passed,
        "class_f1": [float(x) for x in class_f1],
        "confusion_matrix": cm.tolist(),
    }
    (out_dir / "validation.json").write_text(json.dumps(val_report, indent=2) + "\n", encoding="utf-8")

    # D-07: enforce gate before ONNX export
    if not gate_passed:
        print(
            f"error: QGATE-02 failed — best_macro_f1 {best_f1:.4f} "
            f"(fixed_floor={fixed_macro_f1_floor:.2f}, rule_agreement_rate={rule_agreement_rate:.4f})",
            file=sys.stderr,
        )
        return 1

    torch.save({"state_dict": best_state, "best_macro_f1": best_f1}, out_dir / "best_model.pt")

    export = StructureOnnxExport(net_cpu)
    export.eval()
    onnx_path = out_dir / "structure_trained.onnx"
    dummy_x = torch.zeros(1, 12, 7, dtype=torch.float32)
    torch.onnx.export(
        export,
        dummy_x,
        str(onnx_path),
        input_names=["X_struct"],
        output_names=["Y_struct"],
        opset_version=17,
    )
    onnx.checker.check_model(onnx.load(str(onnx_path)))
    _embed_onnx_inplace(onnx_path)
    print(f"\nWrote {onnx_path.resolve()}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
