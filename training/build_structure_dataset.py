#!/usr/bin/env python3
"""Synthesize structure classifier training sequences from per-row data (D-26-02).

Loads merged [N,5] tensors, extends to [N,7] with pitch placeholders,
groups by `group_id` (performer/session), samples [12,7] sliding windows,
and produces [N_valid,12,7] X_struct tensors with [N_valid] Y_struct labels.

Train/val split via GroupShuffleSplit by group_id.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

_TRAINING_DIR = Path(__file__).resolve().parent


def _extend_features(X: torch.Tensor) -> torch.Tensor:
    """Extend 5-float rows to 7-float: append pitchRootMidi=60.0, pitchConfidence=0.0.

    Args:
        X: Float tensor [N,5] where columns are [bpm, rmsEnergy, spectralCentroid, highFreqFlux, stateFloat].

    Returns:
        Float tensor [N,7] with pitch placeholders at columns 5 and 6.
    """
    N = X.shape[0]
    if X.shape[1] != 5:
        raise ValueError(f"Expected X with 5 features, got {X.shape[1]}")
    pitch_root = torch.full((N, 1), 60.0, dtype=X.dtype, device=X.device)
    pitch_conf = torch.zeros(N, 1, dtype=X.dtype, device=X.device)
    return torch.cat([X, pitch_root, pitch_conf], dim=1)


def _sample_sequence_window(
    X: torch.Tensor,
    meta: list[dict] | None = None,
    window_size: int = 12,
) -> tuple[torch.Tensor, torch.Tensor, list[dict]]:
    """Sample [window_size, 7] windows from per-row data grouped by group_id.

    For each row at position i within a group, if there are >= (window_size - 1)
    preceding rows in the same group, create a sample:
      - X_struct[0:window_size-1, :] = preceding rows (oldest first)
      - X_struct[window_size-1, :] = current row i (newest, anchor)
      - Y_struct = current row's state class (derived from X[i, 4])

    If meta is None, each row is treated as its own group (no temporal grouping).

    Args:
        X: Float tensor [N, 7] with features.
        meta: Optional list of dicts with 'group_id' key per row.
        window_size: Number of rows per window (default 12).

    Returns:
        tuple of (X_struct [M, window_size, 7], Y_struct [M] int64, window_meta [M]).
    """
    N = X.shape[0]
    if X.shape[1] != 7:
        raise ValueError(f"Expected X with 7 features, got {X.shape[1]}")

    if N < window_size:
        return (
            torch.empty(0, window_size, X.shape[1], dtype=X.dtype),
            torch.empty(0, dtype=torch.int64),
            [],
        )

    # Build group assignments
    if meta is not None and len(meta) == N:
        groups: dict[str, list[int]] = {}
        for i, m in enumerate(meta):
            gid = str(m.get("group_id", f"_row_{i}"))
            groups.setdefault(gid, []).append(i)
    else:
        # Each row is its own group — no temporal grouping possible
        # Fallback: use consecutive blocks as synthetic groups
        block_size = max(window_size, 50)
        groups = {}
        for i in range(N):
            gid = f"_block_{i // block_size}"
            groups.setdefault(gid, []).append(i)

    windows: list[torch.Tensor] = []
    labels: list[int] = []
    window_meta: list[dict] = []

    for gid, indices in groups.items():
        indices_sorted = sorted(indices)

        # For each position with enough preceding rows in the same group
        for pos in range(window_size - 1, len(indices_sorted)):
            # window_size preceding rows + current row
            window_indices = indices_sorted[pos - (window_size - 1): pos + 1]
            if len(window_indices) != window_size:
                continue

            window = X[window_indices, :]  # [window_size, 7]
            anchor_row = window[-1, :]  # newest row

            # Label from stateFloat (column 4)
            state_float = float(anchor_row[4].item())
            if np.isnan(state_float) or state_float < 0.0 or state_float > 2.0:
                continue
            label = int(np.clip(int(state_float), 0, 2))

            windows.append(window)
            labels.append(label)
            window_meta.append({
                "group_id": gid,
                "anchor_index": int(indices_sorted[pos]),
                "label": label,
            })

    if not windows:
        return (
            torch.empty(0, window_size, X.shape[1], dtype=X.dtype),
            torch.empty(0, dtype=torch.int64),
            [],
        )

    X_struct = torch.stack(windows, dim=0)  # [M, window_size, 7]
    Y_struct = torch.tensor(labels, dtype=torch.int64)  # [M]
    return X_struct, Y_struct, window_meta


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Synthesize structure classifier training sequences (D-26-02)."
    )
    parser.add_argument(
        "--input",
        type=Path,
        required=True,
        help="Merged tensor .pt file (e.g. train_merged.pt).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output .pt file path (e.g. structure_train.pt).",
    )
    parser.add_argument(
        "--window-size",
        type=int,
        default=12,
        help="Number of consecutive rows per sample window (default: 12).",
    )
    parser.add_argument(
        "--max-samples",
        type=int,
        default=None,
        help="Cap on number of output samples (optional).",
    )
    parser.add_argument(
        "--val-fraction",
        type=float,
        default=0.2,
        help="Validation fraction for GroupShuffleSplit (default: 0.2).",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=42,
        help="Random seed for split (default: 42).",
    )
    args = parser.parse_args()

    if not args.input.is_file():
        print(f"error: --input not found: {args.input}", file=sys.stderr)
        return 1

    # Load merged tensor
    try:
        blob = torch.load(str(args.input), map_location="cpu", weights_only=True)
    except Exception as e:
        print(f"error loading {args.input}: {e}", file=sys.stderr)
        return 1

    if not isinstance(blob, dict) or "X" not in blob:
        print(f"error: {args.input} must be a dict with key 'X'", file=sys.stderr)
        return 1

    X_in = blob["X"]  # [N, 5]
    if X_in.ndim != 2 or X_in.shape[1] != 5:
        print(f"error: expected X [N,5], got {list(X_in.shape)}", file=sys.stderr)
        return 1

    # Build per-row metadata (try to get group_ids from manifests)
    meta_list: list[dict] | None = None
    input_stem = args.input.stem  # e.g. "train_merged"

    # Try to find corresponding manifest
    manifest_candidates = [
        args.input.parent / f"manifest_{input_stem}.jsonl",
        args.input.parent / "manifest_train.jsonl",
        args.input.parent / "manifest_train_merged.jsonl",
    ]
    manifest_path = None
    for cand in manifest_candidates:
        if cand.exists():
            manifest_path = cand
            break

    if manifest_path is not None:
        manifest_entries: list[dict] = []
        with manifest_path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line:
                    manifest_entries.append(json.loads(line))

        if len(manifest_entries) > 0:
            # Each manifest entry may correspond to a session (multiple rows)
            # For the merged tensor (GMD+Lakh), expand per-row
            # If manifest is per-session, we expand to per-row using row ordering
            if len(manifest_entries) == X_in.shape[0]:
                meta_list = manifest_entries
            else:
                # Manifest entries are per-session, not per-row; assign synthetic group IDs
                print(f"  manifest has {len(manifest_entries)} entries for {X_in.shape[0]} rows (per-session, expanding...)")

    print(f"Loaded: {args.input}  X shape: {list(X_in.shape)}", flush=True)

    # Extend features from 5 → 7
    X7 = _extend_features(X_in)
    print(f"Extended: [N,5] → {list(X7.shape)}", flush=True)

    # Sample sequence windows
    X_struct, Y_struct, window_meta = _sample_sequence_window(
        X7, meta=meta_list, window_size=args.window_size,
    )

    if X_struct.shape[0] == 0:
        print("error: no valid windows sampled (input too small or no groups with >= window_size rows)", file=sys.stderr)
        return 1

    print(f"Sampled: {X_struct.shape[0]} windows of shape {list(X_struct.shape[1:])}", flush=True)

    # Cap samples if requested
    if args.max_samples is not None and X_struct.shape[0] > args.max_samples:
        indices = torch.randperm(X_struct.shape[0])[:args.max_samples].sort().values
        X_struct = X_struct[indices]
        Y_struct = Y_struct[indices]
        window_meta = [window_meta[i] for i in indices.tolist()]
        print(f"Capped to {args.max_samples} samples", flush=True)

    # ── Train/val split via GroupShuffleSplit ──────────────────────────────
    # Extract group IDs from window_meta
    group_ids = np.array([m.get("group_id", f"_row_{i}") for i, m in enumerate(window_meta)], dtype=object)

    try:
        from sklearn.model_selection import GroupShuffleSplit  # noqa: PLC0415
    except ImportError:
        print("warning: sklearn not available; saving all as train (no val split)", file=sys.stderr)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        torch.save(
            {
                "X": X_struct,
                "Y": Y_struct,
                "meta": window_meta,
            },
            str(args.output),
        )
        print(f"Saved: {args.output}", flush=True)
        return 0

    gss = GroupShuffleSplit(n_splits=1, test_size=args.val_fraction, random_state=args.seed)
    y_placeholder = np.zeros(len(Y_struct), dtype=np.int64)
    tr_idx, va_idx = next(gss.split(np.arange(len(Y_struct)), y_placeholder, groups=group_ids))

    X_train = X_struct[tr_idx]
    Y_train = Y_struct[tr_idx]
    X_val = X_struct[va_idx]
    Y_val = Y_struct[va_idx]

    # Save train
    train_path = args.output.parent / (args.output.stem + "_train.pt")
    if str(args.output) == str(train_path):
        train_path = args.output  # exact match
    train_path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {"X": X_train, "Y": Y_train, "meta": [window_meta[i] for i in tr_idx]},
        str(train_path),
    )
    print(f"Saved: {train_path}  X shape: {list(X_train.shape)}  Y shape: {list(Y_train.shape)}", flush=True)

    # Save val
    val_path = args.output.parent / (args.output.stem + "_val.pt")
    if str(args.output) == str(val_path):
        val_path = args.output
    val_path.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {"X": X_val, "Y": Y_val, "meta": [window_meta[i] for i in va_idx]},
        str(val_path),
    )
    print(f"Saved: {val_path}  X shape: {list(X_val.shape)}  Y shape: {list(Y_val.shape)}", flush=True)

    # ── Manifest ───────────────────────────────────────────────────────────
    manifest_out = args.output.parent / (args.output.stem + "_manifest.jsonl")
    with manifest_out.open("w", encoding="utf-8") as f:
        for i in tr_idx:
            rec = dict(window_meta[i])
            rec["split"] = "train"
            f.write(json.dumps(rec, sort_keys=True) + "\n")
        for i in va_idx:
            rec = dict(window_meta[i])
            rec["split"] = "val"
            f.write(json.dumps(rec, sort_keys=True) + "\n")
    print(f"Manifest: {manifest_out}", flush=True)

    # ── Class distribution ─────────────────────────────────────────────────
    tr_counts = np.bincount(Y_train.numpy(), minlength=3)
    va_counts = np.bincount(Y_val.numpy(), minlength=3)
    print(f"\nClass distribution:", flush=True)
    for c in range(3):
        print(f"  class {c}: train={int(tr_counts[c])}  val={int(va_counts[c])}", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
