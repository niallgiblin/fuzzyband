#!/usr/bin/env python3
"""Train the DEPLOYED style head (style_logits) on the perception taxonomy.

The shipped assets/metal_groove.onnx has a style head that was randomly
initialised at export (export_centroids.py Phase C) — its "Style:" readout is
noise (measured 0.285 acc / 0.0 silence recall on perception WAVs). This script
trains the style head properly:

  - backbone: the groove-model backbone (frozen) from best_groove_model.pt
  - input:    bottleneck embeddings (128-dim) of the perception mel windows
              (data/processed/X.npy / y.npy, built by scripts/build_mel_dataset.py)
  - head:     nn.Linear(128, 5) — exactly the architecture of the exported
              GrooveModelForExport.style_head
  - split:    the frozen grouped train/val split from meta.csv (2 takes/class,
              so val = the held-out take; no test take exists for perception)

Saves the trained head to models/style_head.pt; export_centroids.py loads it so
the ONNX ships the trained head instead of a random one.

Usage:
  python3 training/train_style_head.py [--out training/models/style_head.pt]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn as nn

_REPO_ROOT = Path(__file__).resolve().parents[1]
_DATA_DIR = _REPO_ROOT / "data" / "processed"
_MODEL_DIR = Path(__file__).resolve().parent / "models"
_CKPT_PATH = _MODEL_DIR / "best_groove_model.pt"

sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))
from dataset_split import load_split_indices  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from models.groove_model import GrooveClassifier  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--epochs", type=int, default=60)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--batch-size", type=int, default=512)
    parser.add_argument("--out", type=Path, default=_MODEL_DIR / "style_head.pt")
    parser.add_argument("--device", type=str, default="cpu")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)

    # ── Load perception data + frozen split ────────────────────────────────
    X = np.load(_DATA_DIR / "X.npy")   # (N, 1, 64, 32) perception windows
    y = np.load(_DATA_DIR / "y.npy")   # 5 classes: palm_mute..silence
    n_classes = int(y.max()) + 1
    frozen = load_split_indices(_DATA_DIR / "meta.csv", len(X))
    assert frozen is not None, "perception meta.csv missing/stale"
    train_idx, val_idx, _ = frozen
    print(f"Perception data: {X.shape}, {n_classes} classes "
          f"(train={len(train_idx)}, val={len(val_idx)})")

    # ── Frozen groove backbone → bottleneck embeddings ─────────────────────
    model = GrooveClassifier(n_classes=22)
    sd = torch.load(_CKPT_PATH, weights_only=True, map_location="cpu")
    model.load_state_dict(sd)
    backbone = model.backbone
    backbone.eval()

    def embed(rows: np.ndarray) -> np.ndarray:
        out = []
        with torch.no_grad():
            for s in range(0, len(rows), 512):
                out.append(backbone(torch.from_numpy(X[rows[s:s + 512]])).numpy())
        return np.concatenate(out, axis=0)

    print("Extracting bottleneck embeddings (frozen groove backbone)...")
    t0 = time.time()
    Z = embed(np.arange(len(X)))
    print(f"  embeddings {Z.shape} ({time.time()-t0:.1f}s)")

    # ── Train linear style head on train split only ────────────────────────
    head = nn.Linear(Z.shape[1], n_classes)

    class_counts = np.bincount(y[train_idx], minlength=n_classes).astype(np.float32)
    class_counts = np.maximum(class_counts, 1.0)
    w = torch.tensor(class_counts.sum() / (n_classes * class_counts), dtype=torch.float32)
    criterion = nn.CrossEntropyLoss(weight=w)

    optimizer = torch.optim.Adam(head.parameters(), lr=args.lr, weight_decay=args.weight_decay)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    Xtr = torch.from_numpy(Z[train_idx])
    ytr = torch.from_numpy(y[train_idx]).long()
    Xva = torch.from_numpy(Z[val_idx])
    yva = torch.from_numpy(y[val_idx]).long()

    best_val_f1 = -1.0
    best_state = None
    from sklearn.metrics import f1_score

    print(f"\nTraining style head ({args.epochs} epochs, {len(train_idx)} samples)...")
    for epoch in range(1, args.epochs + 1):
        head.train()
        perm = torch.randperm(len(Xtr))
        total, correct = 0.0, 0.0
        for s in range(0, len(Xtr), args.batch_size):
            idx = perm[s:s + args.batch_size]
            logits = head(Xtr[idx])
            loss = criterion(logits, ytr[idx])
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            correct += (logits.argmax(1) == ytr[idx]).sum().item()
            total += len(idx)
        scheduler.step()

        head.eval()
        with torch.no_grad():
            vp = head(Xva).argmax(1).numpy()
        vf1 = float(f1_score(yva.numpy(), vp, average="macro", zero_division=0))
        vacc = float((vp == yva.numpy()).mean())

        if vf1 > best_val_f1:
            best_val_f1 = vf1
            best_state = {k: v.clone() for k, v in head.state_dict().items()}
        if epoch % 10 == 0 or epoch == args.epochs:
            print(f"  epoch {epoch:>3d}: train acc {correct/total:.4f}  "
                  f"val acc {vacc:.4f}  val F1 {vf1:.4f}")

    assert best_state is not None
    head.load_state_dict(best_state)

    # ── Final honest val evaluation (held-out take per class) ──────────────
    head.eval()
    with torch.no_grad():
        vp = head(Xva).argmax(1).numpy()
    from sklearn.metrics import accuracy_score, classification_report, confusion_matrix
    acc = float(accuracy_score(yva.numpy(), vp))
    f1 = float(f1_score(yva.numpy(), vp, average="macro", zero_division=0))
    cm = confusion_matrix(yva.numpy(), vp, labels=list(range(n_classes)))

    print("\n=== STYLE HEAD (frozen groove backbone, trained head) — VAL ===")
    print(f"  acc {acc:.4f}  macro-F1 {f1:.4f}  (chance 0.20)")
    print("Confusion (rows=true, cols=pred):")
    from perception_taxonomy import STYLE_LABELS
    for i, name in enumerate(STYLE_LABELS):
        print(f"    {name:>12s}: {cm[i].tolist()}")

    args.out.parent.mkdir(parents=True, exist_ok=True)
    torch.save({
        "state_dict": best_state,
        "val_acc": acc,
        "val_macro_f1": f1,
        "confusion": cm.tolist(),
        "n_classes": n_classes,
        "input_dim": Z.shape[1],
        "seed": args.seed,
    }, args.out)
    print(f"\nSaved trained style head: {args.out}")

    # ── Quality gate ───────────────────────────────────────────────────────
    if f1 < 0.40:
        print(f"\n✗ Style head val macro-F1 {f1:.3f} < 0.40 — the perception "
              f"signal is weak; reconsider wiring style into selection.",
              file=sys.stderr)
        return 1
    print(f"\n✓ Style head above 0.40 macro-F1 — deployable for display.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
