#!/usr/bin/env python3
"""Train the end-to-end PlayingStyleCNN (mel → 5 classes) and export it to ONNX.

This is the SHIPPED style classifier for MetalAccompaniment. It replaces the
weak Linear(128→5) style head that was trained on the groove-model's bottleneck
features: that head only sees whatever the 22-class groove classifier's
backbone happened to retain, and generalises poorly to live playing.

PlayingStyleCNN trains directly on the (64, 32) mel windows, so it uses the full
spectral/temporal information and is more robust to tonal variation.

  - data:     data/processed/X.npy / y.npy (perception set, 5 classes)
  - split:    the frozen grouped split in data/processed/meta.csv (held-out take)
  - output:   training/models/best_style_cnn.pt   (best-val state)
  - onnx:     assets/style_cnn.onnx                 (shipped, input mel → style_logits)

Usage:
  python3 training/train_style_cnn.py [--epochs 30] [--device mps|cpu|auto]
"""
from __future__ import annotations

import argparse
import csv
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

_REPO_ROOT = Path(__file__).resolve().parents[1]
_DATA_DIR = _REPO_ROOT / "data" / "processed"
_MODEL_DIR = Path(__file__).resolve().parent / "models"
_ASSETS_DIR = _REPO_ROOT / "assets"

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent / "models"))
from playing_style_cnn import PlayingStyleCNN  # noqa: E402
from perception_taxonomy import STYLE_LABELS  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--epochs", type=int, default=30)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--batch-size", type=int, default=64)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--device", type=str, default="auto")
    parser.add_argument("--pt-out", type=Path, default=_MODEL_DIR / "best_style_cnn.pt")
    parser.add_argument("--onnx-out", type=Path, default=_ASSETS_DIR / "style_cnn.onnx")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    device = torch.device("cuda" if args.device == "auto" and torch.cuda.is_available()
                          else "mps" if args.device == "auto" and torch.backends.mps.is_available()
                          else "cpu")
    print(f"device: {device}")

    # ── Load perception data + frozen grouped split ─────────────────────────
    X = np.load(_DATA_DIR / "X.npy")
    y = np.load(_DATA_DIR / "y.npy")
    rows = list(csv.DictReader(open(_DATA_DIR / "meta.csv")))
    train_idx = [i for i, r in enumerate(rows) if r["split"] == "train"]
    val_idx = [i for i, r in enumerate(rows) if r["split"] == "val"]
    print(f"perception data: {X.shape}, {len(STYLE_LABELS)} classes "
          f"(train={len(train_idx)}, val={len(val_idx)})")

    Xtr = torch.from_numpy(X[train_idx]); ytr = torch.from_numpy(y[train_idx]).long()
    Xva = torch.from_numpy(X[val_idx]);   yva = torch.from_numpy(y[val_idx]).long()

    model = PlayingStyleCNN(n_classes=5, dropout=0.3).to(device)
    counts = np.bincount(y[train_idx], minlength=5).astype(np.float32)
    counts = np.maximum(counts, 1.0)
    w = torch.tensor(counts.sum() / (5 * counts), dtype=torch.float32).to(device)
    crit = nn.CrossEntropyLoss(weight=w)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=args.weight_decay)

    from sklearn.metrics import accuracy_score, f1_score

    best_f1 = -1.0
    best_state = None
    t0 = time.time()
    for ep in range(1, args.epochs + 1):
        model.train()
        perm = torch.randperm(len(Xtr))
        total, correct = 0, 0
        for s in range(0, len(perm), args.batch_size):
            idx = perm[s:s + args.batch_size]
            xb = Xtr[idx].to(device); yb = ytr[idx].to(device)
            opt.zero_grad()
            loss = crit(model(xb), yb)
            loss.backward()
            opt.step()
            correct += (model(xb).argmax(1) == yb).sum().item()
            total += len(idx)
        model.eval()
        with torch.no_grad():
            vp = model(Xva.to(device)).argmax(1).cpu().numpy()
        vacc = float(accuracy_score(yva.numpy(), vp))
        vf1 = float(f1_score(yva.numpy(), vp, average="macro", zero_division=0))
        if vf1 > best_f1:
            best_f1 = vf1
            best_state = {k: v.clone() for k, v in model.state_dict().items()}
        if ep % 5 == 0 or ep == args.epochs:
            print(f"  ep {ep:3d}: train acc {correct/total:.3f}  val acc {vacc:.3f}  val F1 {vf1:.3f}")

    assert best_state is not None
    model.load_state_dict(best_state)
    model.eval()

    # ── Final honest val evaluation (held-out take per class) ────────────────
    with torch.no_grad():
        vp = model(Xva.to(device)).argmax(1).cpu().numpy()
    from sklearn.metrics import confusion_matrix
    acc = float(accuracy_score(yva.numpy(), vp))
    f1 = float(f1_score(yva.numpy(), vp, average="macro", zero_division=0))
    cm = confusion_matrix(yva.numpy(), vp, labels=list(range(5)))
    print("\n=== PlayingStyleCNN end-to-end VAL (held-out take) ===")
    print(f"  acc {acc:.4f}  macro-F1 {f1:.4f}  (linear bottleneck head was 0.769 / 0.796)")
    for i, name in enumerate(STYLE_LABELS):
        print(f"    {name:12s}: {cm[i].tolist()}")

    # ── Dead-class quality gate (§5.1): a perception class with ZERO recall on
    # the held-out take is unlearnable from the current corpus (likely only 2
    # takes of that style exist). Fail loudly so the fix is recording another
    # take, not shipping a model that silently never plays that articulation.
    dead = []
    for i, name in enumerate(STYLE_LABELS):
        row_total = int(cm[i].sum())
        if row_total > 0 and cm[i][i] == 0:
            dead.append(name)
    if dead:
        print(f"\n✗ DEAD (zero-recall) perception class(es) on the held-out set: "
              f"{', '.join(dead)}", file=sys.stderr)
        print("  The style model cannot recognize this articulation. Record more takes "
              "of it and re-run before shipping (DATA_STRATEGY.md §5.1).", file=sys.stderr)
        return 2

    args.pt_out.parent.mkdir(parents=True, exist_ok=True)
    torch.save({"state_dict": best_state, "val_acc": acc, "val_macro_f1": f1,
                "confusion": cm.tolist()}, args.pt_out)
    print(f"Saved best model: {args.pt_out}")

    # ── Export ONNX ─────────────────────────────────────────────────────────
    args.onnx_out.parent.mkdir(parents=True, exist_ok=True)
    dummy = torch.randn(1, 1, 64, 32, device=device)
    torch.onnx.export(
        model.cpu(),  # export on CPU for portability
        torch.randn(1, 1, 64, 32),
        str(args.onnx_out),
        input_names=["mel"],
        output_names=["style_logits"],
        dynamic_axes={"mel": {0: "batch"}, "style_logits": {0: "batch"}},
        opset_version=18,
    )

    # torch.onnx.export can split weights into an external .onnx.data. The C++
    # loader bundles a single file, so re-save the weights inline (self-contained
    # single-file ONNX) and drop the .data file.
    import onnx
    _m = onnx.load(str(args.onnx_out), load_external_data=True)
    for _t in _m.graph.initializer:
        if _t.data_location == onnx.TensorProto.EXTERNAL:
            _t.data_location = onnx.TensorProto.DEFAULT
            del _t.external_data[:]
    onnx.save(_m, str(args.onnx_out))
    _data_file = args.onnx_out.with_suffix(".onnx.data")
    if _data_file.exists():
        _data_file.unlink()

    print(f"Exported self-contained ONNX: {args.onnx_out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
