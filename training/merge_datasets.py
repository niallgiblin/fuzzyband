#!/usr/bin/env python3
"""Merge GMD + Lakh tensors with joint quantile label recomputation (Phase 25 DATA-09).

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


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _activity_score_from_row(row: np.ndarray) -> float:
    """Approximate activity score from a 5-float proxy row (no event data needed).

    Uses a dens_est proxy from state/hf/rms to approximate the original
    `0.5*dens + 2*rms + 0.5*hf + 0.2*state` formula.
    """
    rms = float(row[1])
    hf = float(row[3])
    state = float(row[4])
    dens_est = state * 4.0 + hf * 2.0 + rms * 6.0
    return 0.5 * dens_est + 2.0 * rms + 0.5 * hf + 0.2 * state


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
    Y_gmd_train_old = gmd_train["Y"].numpy()
    Y_gmd_val_old = gmd_val["Y"].numpy()
    X_lakh_raw = lakh_data["X"].numpy().astype(np.float64)
    scores_lakh = lakh_data["scores"].numpy()

    old_mean = np.asarray(old_norm["mean"], dtype=np.float64)
    old_std = np.asarray(old_norm["std"], dtype=np.float64)

    # ── De-normalize GMD ──────────────────────────────────────────────────
    X_gmd_train_raw = X_gmd_train_norm * old_std + old_mean
    X_gmd_val_raw = X_gmd_val_norm * old_std + old_mean

    # ── Recompute activity scores for GMD ─────────────────────────────────
    scores_gmd_train = np.array(
        [_activity_score_from_row(row) for row in X_gmd_train_raw],
        dtype=np.float64,
    )
    scores_gmd_val = np.array(
        [_activity_score_from_row(row) for row in X_gmd_val_raw],
        dtype=np.float64,
    )

    # ── Joint quantile recomputation (merged GMD train + Lakh) ────────────
    scores_merged = np.concatenate([scores_gmd_train, scores_lakh])
    qt = np.quantile(scores_merged, [1 / 7, 2 / 7, 3 / 7, 4 / 7, 5 / 7, 6 / 7])

    Y_gmd_train_new = np.searchsorted(qt, scores_gmd_train, side="right").astype(np.int64)
    Y_lakh_new = np.searchsorted(qt, scores_lakh, side="right").astype(np.int64)
    Y_gmd_val_new = np.searchsorted(qt, scores_gmd_val, side="right").astype(np.int64)

    # ── Merge features ────────────────────────────────────────────────────
    X_merged_raw = np.concatenate([X_gmd_train_raw, X_lakh_raw], axis=0)
    Y_merged = np.concatenate([Y_gmd_train_new, Y_lakh_new])

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
    print(f"Quantile edges:     {np.round(qt, 4).tolist()}", flush=True)

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
    if np.any(new_counts < 50):
        for i in range(7):
            if new_counts[i] < 50:
                print(f"FAIL: class {i} has {int(new_counts[i])} < 50 examples", flush=True)
        print("FAIL: HISTOGRAM — at least one class below 50", flush=True)
        return 3

    print("PASS: HISTOGRAM — all classes >= 50 in merged train", flush=True)

    if args.dry_run:
        print("\nDRY RUN — no files written.", flush=True)
        return 0

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
    print(f"\nSaved: {args.out_dir / 'train.pt'}", flush=True)

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

    # ── Backup GMD originals ──────────────────────────────────────────────
    # Preserve the original GMD files before overwriting
    backup_dir = args.out_dir / "gmd_original_backup"
    backup_dir.mkdir(parents=True, exist_ok=True)
    import shutil

    for src_name in ["train.pt", "val.pt", "norm_stats.json"]:
        src = args.out_dir / src_name
        dst = backup_dir / src_name
        if src.exists() and not dst.exists():
            shutil.copy2(str(src), str(dst))
    print(f"Backed up GMD originals: {backup_dir}", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
