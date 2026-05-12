#!/usr/bin/env python3
"""Merge GMD + Lakh tensors with oracle label passthrough (Phase 32, updated from Phase 25 DATA-09).

Preserves a GMD-only validation fold (val_gmd.pt) alongside merged train.pt.
Recalibrates norm_stats.json on the merged distribution.
Enforces >= 50 examples per class histogram gate.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

_TRAINING_DIR = Path(__file__).resolve().parent
_PROCESSED_DIR = _TRAINING_DIR / "data/processed"
_LAKH_TENSORS = _TRAINING_DIR / "data/lakh/lakh_tensors.pt"
_EPS = 1e-8
_FEATURE_ORDER = [
    "bpm",
    "rmsEnergy",
    "spectralCentroid",
    "highFreqFlux",
    "state_float",
]
_MIN_EXAMPLES_PER_CLASS = 50
_GATE_CLASSES = [1, 2, 3, 4, 5, 6]


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _oversample_minority_class(
    X: np.ndarray,
    Y: np.ndarray,
    class_index: int,
    min_examples: int,
) -> tuple[np.ndarray, np.ndarray, int]:
    count = int(np.sum(Y == class_index))
    if count == 0 or count >= min_examples:
        return X, Y, 0

    needed = min_examples - count
    class_rows = np.flatnonzero(Y == class_index)
    repeats = np.resize(class_rows, needed)
    return (
        np.concatenate([X, X[repeats]], axis=0),
        np.concatenate([Y, Y[repeats]], axis=0),
        needed,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Merge GMD + Lakh tensors (Phase 25 DATA-09)."
    )
    parser.add_argument(
        "--gmd-train",
        type=Path,
        default=_PROCESSED_DIR / "train.pt",
        help="GMD train tensors (default: training/data/processed/train.pt)",
    )
    parser.add_argument(
        "--gmd-val",
        type=Path,
        default=_PROCESSED_DIR / "val.pt",
        help="GMD validation tensors (default: training/data/processed/val.pt)",
    )
    parser.add_argument(
        "--gmd-norm",
        type=Path,
        default=_PROCESSED_DIR / "norm_stats.json",
        help="GMD norm stats (default: training/data/processed/norm_stats.json)",
    )
    parser.add_argument(
        "--lakh",
        type=Path,
        default=_LAKH_TENSORS,
        help="Lakh tensors (default: training/data/lakh/lakh_tensors.pt)",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=_PROCESSED_DIR,
        help="Output directory (default: training/data/processed)",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print merge summary without writing output files.",
    )
    args = parser.parse_args()

    backup_dir = args.out_dir / "gmd_original_backup"
    default_gmd_train = _PROCESSED_DIR / "train.pt"
    if args.gmd_train == default_gmd_train and args.gmd_train.exists():
        try:
            current_train_meta = torch.load(
                str(args.gmd_train),
                map_location="cpu",
                weights_only=True,
            ).get("meta", {})
        except Exception:
            current_train_meta = {}
        backup_train = backup_dir / "train.pt"
        backup_val = backup_dir / "val.pt"
        backup_norm = backup_dir / "norm_stats.json"
        if (
            isinstance(current_train_meta, dict)
            and current_train_meta.get("source") == "gmd+lakh_merged"
            and backup_train.exists()
            and backup_val.exists()
            and backup_norm.exists()
        ):
            args.gmd_train = backup_train
            args.gmd_val = backup_val
            args.gmd_norm = backup_norm

    # ── Load all inputs ───────────────────────────────────────────────────
    missing = []
    for p, name in [
        (args.gmd_train, "GMD train"),
        (args.gmd_val, "GMD val"),
        (args.gmd_norm, "GMD norm stats"),
        (args.lakh, "Lakh tensors"),
    ]:
        if not p.exists():
            missing.append(f"  {name}: {p}")
    if missing:
        print("error: missing input files:", file=sys.stderr)
        for m in missing:
            print(m, file=sys.stderr)
        return 1

    gmd_train = torch.load(str(args.gmd_train), map_location="cpu", weights_only=True)
    gmd_val = torch.load(str(args.gmd_val), map_location="cpu", weights_only=True)
    lakh_data = torch.load(str(args.lakh), map_location="cpu", weights_only=True)

    with args.gmd_norm.open("r", encoding="utf-8") as f:
        old_norm = json.load(f)

    X_gmd_train_norm = gmd_train["X"].numpy().astype(np.float64)
    X_gmd_val_norm = gmd_val["X"].numpy().astype(np.float64)
    Y_gmd_train_old = gmd_train["Y"].numpy()   # oracle labels from build_dataset.py train split
    Y_gmd_val_old = gmd_val["Y"].numpy()       # oracle labels from build_dataset.py val split
    X_lakh_raw = lakh_data["X"].numpy().astype(np.float64)
    Y_lakh = lakh_data["Y"].numpy()   # oracle labels from build_lakh_dataset.py

    for name, labels in [
        ("GMD train", Y_gmd_train_old),
        ("GMD val", Y_gmd_val_old),
        ("Lakh", Y_lakh),
    ]:
        invalid = (labels < 0) | (labels > 6)
        if np.any(invalid):
            bad = labels[invalid]
            print(
                f"FAIL: {name} labels must be in 0..6; "
                f"found min={int(np.min(bad))}, max={int(np.max(bad))}, "
                f"invalid_count={int(np.sum(invalid))}",
                flush=True,
            )
            return 3

    old_mean = np.asarray(old_norm["mean"], dtype=np.float64)
    old_std = np.asarray(old_norm["std"], dtype=np.float64)

    # ── De-normalize GMD ──────────────────────────────────────────────────
    X_gmd_train_raw = X_gmd_train_norm * old_std + old_mean
    X_gmd_val_raw = X_gmd_val_norm * old_std + old_mean

    # ── Oracle label passthrough (Phase 32) ───────────────────────────────────────
    # Oracle labels are computed in build_dataset.py and build_lakh_dataset.py.
    # merge_datasets.py concatenates them directly — no quantile recomputation.
    Y_gmd_train_new = Y_gmd_train_old   # oracle labels from build_dataset.py train split
    Y_lakh_new      = Y_lakh            # oracle labels from build_lakh_dataset.py
    Y_gmd_val_new   = Y_gmd_val_old    # oracle labels from build_dataset.py val split

    # ── Merge features ────────────────────────────────────────────────────
    X_merged_raw = np.concatenate([X_gmd_train_raw, X_lakh_raw], axis=0)
    Y_merged = np.concatenate([Y_gmd_train_new, Y_lakh_new])

    X_merged_raw, Y_merged, class6_added = _oversample_minority_class(
        X_merged_raw,
        Y_merged,
        class_index=6,
        min_examples=_MIN_EXAMPLES_PER_CLASS,
    )

    # ── Joint normalization ───────────────────────────────────────────────
    merged_mean = np.mean(X_merged_raw, axis=0)
    merged_std = np.std(X_merged_raw, axis=0, ddof=0)
    merged_std = np.maximum(merged_std, _EPS)

    X_merged_norm = ((X_merged_raw - merged_mean) / merged_std).astype(np.float32)
    X_gmd_val_norm_new = ((X_gmd_val_raw - merged_mean) / merged_std).astype(np.float32)

    # ── Log summary ───────────────────────────────────────────────────────
    print(f"GMD train:          {len(X_gmd_train_raw):>6} examples", flush=True)
    print(f"GMD val:            {len(X_gmd_val_raw):>6} examples", flush=True)
    print(f"Lakh:               {len(X_lakh_raw):>6} examples", flush=True)
    print(f"MERGED train:       {len(X_merged_raw):>6} examples", flush=True)
    if class6_added:
        print(
            f"Oversampled class 6: +{class6_added} examples "
            f"(minimum {_MIN_EXAMPLES_PER_CLASS})",
            flush=True,
        )

    # ── Class counts (old vs new) ─────────────────────────────────────────
    old_counts = np.bincount(Y_gmd_train_old, minlength=7)
    new_counts = np.bincount(Y_merged, minlength=7)
    gmd_new_counts = np.bincount(Y_gmd_train_new, minlength=7)
    lakh_counts = np.bincount(Y_lakh_new, minlength=7)
    val_counts = np.bincount(Y_gmd_val_new, minlength=7)

    print(f"\nClass distribution (merged train):", flush=True)
    for i in range(7):
        print(
            f"  class {i}: {int(new_counts[i]):>6}  "
            f"(GMD: {int(gmd_new_counts[i]):>5}, Lakh: {int(lakh_counts[i]):>5})",
            flush=True,
        )
    print(f"val_gmd:          {np.sum(val_counts):>6} examples", flush=True)

    # ── Histogram gate ────────────────────────────────────────────────────
    # Class 0 (SILENT) is structurally absent from the MIDI corpora and is handled
    # by rule-based passthrough. DATA-06 gates learnable non-silent classes 1-6.
    gate_failed = [i for i in _GATE_CLASSES if new_counts[i] < 50]
    if gate_failed:
        for i in gate_failed:
            print(f"FAIL: class {i} has {int(new_counts[i])} < 50 examples", flush=True)
        print("FAIL: HISTOGRAM — at least one gated class below 50", flush=True)
        return 3

    print("PASS: HISTOGRAM — all classes >= 50 in merged train", flush=True)

    if args.dry_run:
        print("\nDRY RUN — no files written.", flush=True)
        return 0

    # ── Backup GMD originals FIRST (before overwriting) ───────────────────
    import shutil

    backup_dir.mkdir(parents=True, exist_ok=True)
    for src_name in ["train.pt", "val.pt", "norm_stats.json"]:
        src = args.out_dir / src_name
        dst = backup_dir / src_name
        if src.exists():
            shutil.copy2(str(src), str(dst))
    print(f"Backed up GMD originals: {backup_dir}", flush=True)

    # ── Save outputs ──────────────────────────────────────────────────────
    args.out_dir.mkdir(parents=True, exist_ok=True)

    # Merged train
    torch.save(
        {
            "X": torch.from_numpy(X_merged_norm),
            "Y": torch.from_numpy(Y_merged),
            "meta": {"source": "gmd+lakh_merged", "norm": "merged_fit"},
        },
        str(args.out_dir / "train.pt"),
    )
    print(f"Saved: {args.out_dir / 'train.pt'}", flush=True)

    # GMD-only validation fold
    torch.save(
        {
            "X": torch.from_numpy(X_gmd_val_norm_new),
            "Y": torch.from_numpy(Y_gmd_val_new),
            "meta": {"source": "gmd_only_validation", "norm": "merged_fit"},
        },
        str(args.out_dir / "val_gmd.pt"),
    )
    print(f"Saved: {args.out_dir / 'val_gmd.pt'}", flush=True)

    # Updated norm stats
    new_norm_stats = {
        "feature_order": _FEATURE_ORDER,
        "mean": merged_mean.astype(float).tolist(),
        "std": merged_std.astype(float).tolist(),
        "epsilon": _EPS,
    }
    norm_path = args.out_dir / "norm_stats.json"
    norm_path.write_text(
        json.dumps(new_norm_stats, indent=2) + "\n",
        encoding="utf-8",
    )
    print(f"Saved: {norm_path}", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
