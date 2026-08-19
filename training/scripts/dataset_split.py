#!/usr/bin/env python3
"""Grouped train/val/test split by source recording (DATA_STRATEGY.md §5.1).

The core validation bug this fixes: augmented variants of a single take must
never straddle the train/val boundary. If a time-stretched copy of take *A* lands
in train while another copy of *A* lands in val, the reported F1 is inflated by
leakage — the model has effectively seen the val example. This module assigns a
split label to every window such that:

  * **all windows sharing a source recording go to the same split** (no leakage);
  * **every class appears in the validation set** (§5.1 — no dead val classes);
  * assignment is **deterministic** for a fixed seed.

The output is a per-window list of "train" / "val" / "test" strings that the
dataset builders write into meta CSV and the trainers read back — so the split is
frozen at dataset-build time and identical across every training run.
"""

from __future__ import annotations

import csv
from collections import defaultdict
from pathlib import Path

import numpy as np


def assign_grouped_split(
    groups: list[str],
    labels: list[int],
    *,
    seed: int = 42,
    val_frac: float = 0.2,
    test_frac: float = 0.0,
) -> list[str]:
    """Assign a grouped, class-balanced split label to each sample.

    Args:
        groups: per-sample source-recording identifier (the grouping key).
        labels: per-sample class index (parallel to *groups*).
        seed: RNG seed for deterministic assignment.
        val_frac: target fraction of *groups* (per class) placed in val.
        test_frac: target fraction of *groups* (per class) placed in test.

    Returns:
        A list of "train"/"val"/"test" strings, one per input sample.

    Guarantees:
        * every sample of a given group receives the same split;
        * every class that has >= 2 groups contributes >= 1 group to val;
        * deterministic for a fixed seed.

    Notes:
        A class with only a single source recording cannot be split without
        leakage, so its lone group stays in *train* and the class will be absent
        from val — the trainer's quality gate reports this honestly rather than
        the builder faking a held-out example.
    """
    if len(groups) != len(labels):
        raise ValueError("groups and labels must have equal length")
    if not groups:
        return []
    if val_frac < 0.0 or test_frac < 0.0 or (val_frac + test_frac) > 1.0:
        raise ValueError("val_frac/test_frac must be >= 0 and sum to <= 1")

    rng = np.random.RandomState(seed)

    # Map each group -> the classes it contains (a group is normally single-class,
    # but we assign a group to the split of its majority class to stay robust).
    group_to_labels: dict[str, list[int]] = defaultdict(list)
    for g, y in zip(groups, labels):
        group_to_labels[g].append(int(y))

    group_majority: dict[str, int] = {
        g: int(np.bincount(ys).argmax()) for g, ys in group_to_labels.items()
    }

    # Bucket groups by their majority class so we can guarantee per-class coverage.
    class_to_groups: dict[int, list[str]] = defaultdict(list)
    for g, cls in group_majority.items():
        class_to_groups[cls].append(g)

    group_split: dict[str, str] = {}
    for cls in sorted(class_to_groups):
        cls_groups = sorted(class_to_groups[cls])  # deterministic order
        rng.shuffle(cls_groups)
        n = len(cls_groups)

        if n == 1:
            # Single source recording — cannot hold out without leakage.
            group_split[cls_groups[0]] = "train"
            continue

        n_val = max(1, int(round(n * val_frac))) if val_frac > 0.0 else 0
        n_test = int(round(n * test_frac)) if test_frac > 0.0 else 0
        # Never starve train: keep at least one group in train.
        while n_val + n_test >= n and (n_val + n_test) > 0:
            if n_test > 0:
                n_test -= 1
            elif n_val > 1:
                n_val -= 1
            else:
                break

        idx = 0
        for _ in range(n_val):
            group_split[cls_groups[idx]] = "val"
            idx += 1
        for _ in range(n_test):
            group_split[cls_groups[idx]] = "test"
            idx += 1
        for g in cls_groups[idx:]:
            group_split[g] = "train"

    return [group_split[g] for g in groups]


def load_split_indices(
    meta_path: Path, n_samples: int
) -> tuple[np.ndarray, np.ndarray, np.ndarray] | None:
    """Load frozen train/val/test row indices from a builder meta CSV.

    The meta CSV rows correspond 1:1 (in order) to the rows of X/y written by the
    dataset builder. Returns (train_idx, val_idx, test_idx) as int arrays, or None
    if the file is missing, has no ``split`` column, or its row count does not
    match *n_samples* (a stale meta → fall back to a computed split rather than a
    silently wrong one).
    """
    if not meta_path.is_file():
        return None
    with meta_path.open("r", encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames is None or "split" not in reader.fieldnames:
            return None
        splits = [row["split"] for row in reader]
    if len(splits) != n_samples:
        return None
    arr = np.array(splits)
    train_idx = np.where(arr == "train")[0]
    val_idx = np.where(arr == "val")[0]
    test_idx = np.where(arr == "test")[0]
    return train_idx, val_idx, test_idx


def summarize_split(groups: list[str], labels: list[int], splits: list[str]) -> str:
    """Return a human-readable per-split, per-class window/group count table."""
    lines = ["split      class  windows  groups"]
    by_split_class_windows: dict[tuple[str, int], int] = defaultdict(int)
    by_split_class_groups: dict[tuple[str, int], set[str]] = defaultdict(set)
    for g, y, s in zip(groups, labels, splits):
        by_split_class_windows[(s, int(y))] += 1
        by_split_class_groups[(s, int(y))].add(g)
    for s in ("train", "val", "test"):
        for cls in sorted({int(y) for y in labels}):
            w = by_split_class_windows.get((s, cls), 0)
            grp = len(by_split_class_groups.get((s, cls), set()))
            if w or grp:
                lines.append(f"{s:<9s} {cls:>5d}  {w:>7d}  {grp:>6d}")
    return "\n".join(lines)
