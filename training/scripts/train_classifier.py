#!/usr/bin/env python3
"""Train PlayingStyleCNN on mel-spectrogram dataset (M009 S02).

Phase D of REAL_TIME_AUDIO_CLASSIFIER.md:
  - Stratified 70/15/15 train/val/test split
  - On-the-fly augmentation: Gaussian noise, random gain
  - Adam, CosineAnnealingLR, 50 epochs, batch_size=32
  - Track best model by validation macro-F1
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
from sklearn.metrics import (accuracy_score, classification_report,
                             confusion_matrix, f1_score)
from sklearn.model_selection import StratifiedShuffleSplit
from torch.utils.data import DataLoader, Dataset, Subset

# ─── Paths ───────────────────────────────────────────────────────────────────

_REPO_ROOT = Path(__file__).resolve().parents[2]
_DATA_DIR = _REPO_ROOT / "data" / "processed"
_MODEL_DIR = Path(__file__).resolve().parents[1] / "models"


# ─── Dataset ─────────────────────────────────────────────────────────────────

class MelDataset(Dataset):
    """Thin wrapper around X.npy / y.npy."""

    def __init__(self, X: np.ndarray, y: np.ndarray):
        self.X = torch.from_numpy(X)
        self.y = torch.from_numpy(y).long()

    def __len__(self) -> int:
        return len(self.X)

    def __getitem__(self, idx: int):
        return self.X[idx], self.y[idx]


# ─── Augmentation (mel-dB domain) ────────────────────────────────────────────

def augment_batch(x: torch.Tensor, rng: torch.Generator) -> torch.Tensor:
    """Apply on-the-fly augmentation to a batch of mel-dB spectrograms.

    x: (B, 1, 64, 32) — mel-dB values, typically [-80, 0].
    """
    B = x.shape[0]

    # Gaussian noise — increased to 1.0 dB to encourage generalization
    noise = torch.randn(B, 1, 64, 32, generator=rng, device=x.device) * 1.0
    x = x + noise

    # Random gain: ±12 dB (wider range for dynamic variation)
    gain_db = (torch.rand(B, 1, 1, 1, generator=rng, device=x.device) * 24.0) - 12.0
    x = x + gain_db

    # Time masking: zero 1–4 random time frames to prevent memorizing temporal position
    n_time_mask = int(torch.randint(1, 5, (1,), generator=rng).item())
    t_start = int(torch.randint(0, 32 - n_time_mask, (1,), generator=rng).item())
    x[:, :, :, t_start:t_start + n_time_mask] = -80.0

    # Frequency masking: zero 2–6 random mel bands
    n_freq_mask = int(torch.randint(2, 7, (1,), generator=rng).item())
    f_start = int(torch.randint(0, 64 - n_freq_mask, (1,), generator=rng).item())
    x[:, :, f_start:f_start + n_freq_mask, :] = -80.0

    return x


# ─── Model import ────────────────────────────────────────────────────────────

def _import_model():
    sys.path.insert(0, str(_MODEL_DIR.parent))
    from models.playing_style_cnn import PlayingStyleCNN  # noqa: PLC0415
    return PlayingStyleCNN


# ─── Training ────────────────────────────────────────────────────────────────

CLASS_NAMES = ["palm_mute", "open_chord", "single_note", "sustain", "silence"]


def train_epoch(
    model: nn.Module,
    loader: DataLoader,
    optimizer: torch.optim.Optimizer,
    criterion: nn.Module,
    device: torch.device,
    rng: torch.Generator,
    augment: bool = True,
) -> tuple[float, float]:
    model.train()
    total_loss = 0.0
    all_preds: list[int] = []
    all_labels: list[int] = []

    for x_batch, y_batch in loader:
        x_batch = x_batch.to(device)
        y_batch = y_batch.to(device)

        if augment:
            x_batch = augment_batch(x_batch, rng)

        optimizer.zero_grad()
        logits = model(x_batch)
        loss = criterion(logits, y_batch)
        loss.backward()
        optimizer.step()

        total_loss += loss.item() * x_batch.size(0)
        preds = logits.argmax(dim=1).cpu().tolist()
        all_preds.extend(preds)
        all_labels.extend(y_batch.cpu().tolist())

    avg_loss = total_loss / len(loader.dataset)
    acc = accuracy_score(all_labels, all_preds)
    return avg_loss, acc


@torch.no_grad()
def evaluate(
    model: nn.Module,
    loader: DataLoader,
    criterion: nn.Module,
    device: torch.device,
) -> tuple[float, float, float, np.ndarray, np.ndarray]:
    model.eval()
    total_loss = 0.0
    all_preds: list[int] = []
    all_labels: list[int] = []

    for x_batch, y_batch in loader:
        x_batch = x_batch.to(device)
        y_batch = y_batch.to(device)

        logits = model(x_batch)
        loss = criterion(logits, y_batch)

        total_loss += loss.item() * x_batch.size(0)
        preds = logits.argmax(dim=1).cpu().tolist()
        all_preds.extend(preds)
        all_labels.extend(y_batch.cpu().tolist())

    avg_loss = total_loss / len(loader.dataset)
    acc = accuracy_score(all_labels, all_preds)
    f1_macro = float(f1_score(all_labels, all_preds, average="macro", zero_division=0))
    conf = confusion_matrix(all_labels, all_preds, labels=list(range(5)))
    return avg_loss, acc, f1_macro, np.array(all_preds), np.array(all_labels)


def main() -> int:
    parser = argparse.ArgumentParser(description="Train PlayingStyleCNN (M009 S02).")
    parser.add_argument("--epochs", type=int, default=50)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--weight-decay", type=float, default=1e-4)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--no-augment", action="store_true")
    parser.add_argument("--data-dir", type=Path, default=_DATA_DIR)
    parser.add_argument("--model-dir", type=Path, default=_MODEL_DIR)
    parser.add_argument("--device", type=str, default="cpu")
    args = parser.parse_args()

    # ── Reproducibility ──────────────────────────────────────────────────
    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    rng = torch.Generator()
    rng.manual_seed(args.seed)

    device = torch.device(args.device)
    print(f"Device: {device}")
    print(f"Data dir: {args.data_dir}")
    print(f"Model dir: {args.model_dir}")

    # ── Load data ────────────────────────────────────────────────────────
    X = np.load(args.data_dir / "X.npy")
    y = np.load(args.data_dir / "y.npy")
    print(f"Loaded {X.shape[0]} samples, X shape: {X.shape}, y shape: {y.shape}")

    # ── Stratified split: 70/15/15 ───────────────────────────────────────
    # First split: 70 train, 30 temp
    sss1 = StratifiedShuffleSplit(n_splits=1, test_size=0.30, random_state=args.seed)
    train_idx, temp_idx = next(sss1.split(X, y))

    # Second split: temp → 15 val, 15 test (50/50 of temp)
    sss2 = StratifiedShuffleSplit(n_splits=1, test_size=0.50, random_state=args.seed)
    val_idx, test_idx = next(sss2.split(X[temp_idx], y[temp_idx]))
    val_idx = temp_idx[val_idx]
    test_idx = temp_idx[test_idx]

    dataset = MelDataset(X, y)
    train_loader = DataLoader(
        Subset(dataset, train_idx), batch_size=args.batch_size, shuffle=True,
    )
    val_loader = DataLoader(
        Subset(dataset, val_idx), batch_size=args.batch_size, shuffle=False,
    )
    test_loader = DataLoader(
        Subset(dataset, test_idx), batch_size=args.batch_size, shuffle=False,
    )

    print(f"Train: {len(train_idx)}, Val: {len(val_idx)}, Test: {len(test_idx)}")

    # ── Model ────────────────────────────────────────────────────────────
    PlayingStyleCNN = _import_model()
    model = PlayingStyleCNN(n_classes=5).to(device)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model params: {n_params:,}")

    class_counts = np.bincount(y[train_idx], minlength=5).astype(np.float32)
    class_weights = torch.tensor(class_counts.sum() / (5.0 * class_counts), dtype=torch.float32).to(device)
    criterion = nn.CrossEntropyLoss(weight=class_weights)
    optimizer = torch.optim.Adam(
        model.parameters(), lr=args.lr, weight_decay=args.weight_decay,
    )
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(
        optimizer, T_max=args.epochs,
    )

    # ── Training loop ────────────────────────────────────────────────────
    best_val_f1 = -1.0
    best_epoch = 0
    history: list[dict] = []
    start_time = time.time()

    print()
    print(f"{'Epoch':>6s} {'Train Loss':>10s} {'Train Acc':>10s} "
          f"{'Val Loss':>10s} {'Val Acc':>8s} {'Val F1':>8s} {'LR':>10s}")
    print("-" * 70)

    for epoch in range(1, args.epochs + 1):
        train_loss, train_acc = train_epoch(
            model, train_loader, optimizer, criterion, device, rng,
            augment=not args.no_augment,
        )
        val_loss, val_acc, val_f1, _, _ = evaluate(
            model, val_loader, criterion, device,
        )
        scheduler.step()
        current_lr = scheduler.get_last_lr()[0]

        history.append({
            "epoch": epoch,
            "train_loss": train_loss,
            "train_acc": train_acc,
            "val_loss": val_loss,
            "val_acc": val_acc,
            "val_f1": val_f1,
            "lr": current_lr,
        })

        marker = ""
        if val_f1 > best_val_f1:
            best_val_f1 = val_f1
            best_epoch = epoch
            args.model_dir.mkdir(parents=True, exist_ok=True)
            torch.save(model.state_dict(), args.model_dir / "best_model.pt")
            marker = " ← best"

        print(f"{epoch:>6d} {train_loss:>10.4f} {train_acc:>9.3f} "
              f"{val_loss:>10.4f} {val_acc:>7.3f} {val_f1:>7.3f} "
              f"{current_lr:>10.6f}{marker}")

    elapsed = time.time() - start_time
    print(f"\nTraining complete in {elapsed:.1f}s")
    print(f"Best val F1: {best_val_f1:.4f} at epoch {best_epoch}")

    # ── Final test evaluation ────────────────────────────────────────────
    print("\n--- Test Set Evaluation ---")
    model.load_state_dict(torch.load(args.model_dir / "best_model.pt", weights_only=True))
    test_loss, test_acc, test_f1, test_preds, test_labels = evaluate(
        model, test_loader, criterion, device,
    )
    print(f"Test loss: {test_loss:.4f}")
    print(f"Test accuracy: {test_acc:.4f}")
    print(f"Test macro-F1: {test_f1:.4f}")
    print()

    report = classification_report(
        test_labels, test_preds,
        target_names=CLASS_NAMES,
        zero_division=0,
    )
    print(report)

    cm = confusion_matrix(test_labels, test_preds, labels=list(range(5)))
    print("Confusion matrix (rows=true, cols=predicted):")
    header = "            " + " ".join(f"{n:>10s}" for n in CLASS_NAMES)
    print(header)
    for i, name in enumerate(CLASS_NAMES):
        row = " ".join(f"{v:>10d}" for v in cm[i])
        print(f"{name:>12s} {row}")

    # ── Save history ─────────────────────────────────────────────────────
    history_path = args.model_dir / "training_history.json"
    history_path.write_text(json.dumps(history, indent=2), encoding="utf-8")
    print(f"\nHistory saved to {history_path}")

    # ── Gate check ───────────────────────────────────────────────────────
    per_class_f1 = f1_score(test_labels, test_preds, average=None, zero_division=0)
    low_f1_classes = [
        CLASS_NAMES[i] for i, f in enumerate(per_class_f1) if f < 0.60
    ]
    gate_ok = test_acc >= 0.80 and len(low_f1_classes) == 0

    if test_acc < 0.80:
        print(f"\n⚠  Test accuracy {test_acc:.3f} < 0.80 target", file=sys.stderr)
    if low_f1_classes:
        print(f"\n⚠  Low F1 classes: {', '.join(low_f1_classes)}", file=sys.stderr)

    if gate_ok:
        print("\n✓ All gates passed: acc ≥ 0.80, all per-class F1 ≥ 0.60")
    else:
        print("\n✗ Gate check failed — see warnings above", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
