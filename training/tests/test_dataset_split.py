"""Tests for the grouped train/val/test split (DATA_STRATEGY.md §5.1).

The invariants under test are the whole point of the fix: no source recording may
straddle the train/val boundary (no leakage), and every class with >= 2 source
recordings must be represented in val.
"""

from __future__ import annotations

import csv
from pathlib import Path

import pytest

pytest.importorskip("numpy")

from scripts.dataset_split import (
    assign_grouped_split,
    load_split_indices,
    summarize_split,
)


def _make_case():
    """3 classes; class 0 has 4 sources, class 1 has 3, class 2 has 1 (singleton)."""
    groups: list[str] = []
    labels: list[int] = []
    plan = {0: 4, 1: 3, 2: 1}
    for cls, n_sources in plan.items():
        for s in range(n_sources):
            src = f"c{cls}_take{s}"
            for _win in range(5):  # 5 windows per source (simulate augmentation)
                groups.append(src)
                labels.append(cls)
    return groups, labels


def test_no_source_straddles_splits() -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=1, val_frac=0.25)

    source_to_splits: dict[str, set[str]] = {}
    for g, s in zip(groups, splits):
        source_to_splits.setdefault(g, set()).add(s)

    for src, sset in source_to_splits.items():
        assert len(sset) == 1, f"source {src} leaked across splits: {sset}"


def test_every_multi_source_class_in_val() -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=1, val_frac=0.25)

    val_classes = {y for y, s in zip(labels, splits) if s == "val"}
    # Classes 0 and 1 have multiple sources → must appear in val.
    assert 0 in val_classes
    assert 1 in val_classes


def test_singleton_class_stays_in_train() -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=1, val_frac=0.25)

    # Class 2 has a single source recording — it cannot be held out without
    # leakage, so all its windows stay in train (absent from val).
    class2_splits = {s for y, s in zip(labels, splits) if y == 2}
    assert class2_splits == {"train"}


def test_deterministic_for_fixed_seed() -> None:
    groups, labels = _make_case()
    a = assign_grouped_split(groups, labels, seed=7, val_frac=0.25)
    b = assign_grouped_split(groups, labels, seed=7, val_frac=0.25)
    assert a == b


def test_test_fraction_allocates_test_groups() -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=3, val_frac=0.25, test_frac=0.25)
    # Class 0 has 4 sources → room for train + val + test.
    class0 = {s for y, s in zip(labels, splits) if y == 0}
    assert "test" in class0


def test_train_never_starved() -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=2, val_frac=0.9)
    # Even with an aggressive val fraction, every class keeps >= 1 train group.
    for cls in (0, 1):
        train_srcs = {g for g, y, s in zip(groups, labels, splits) if y == cls and s == "train"}
        assert train_srcs, f"class {cls} has no train group"


def test_load_split_indices_roundtrip(tmp_path: Path) -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=1, val_frac=0.25)

    meta = tmp_path / "meta.csv"
    with meta.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["class_name", "class_idx", "source", "split"])
        w.writeheader()
        for g, y, s in zip(groups, labels, splits):
            w.writerow({"class_name": f"c{y}", "class_idx": y, "source": g, "split": s})

    result = load_split_indices(meta, len(groups))
    assert result is not None
    train_idx, val_idx, test_idx = result
    n = len(train_idx) + len(val_idx) + len(test_idx)
    assert n == len(groups)
    # Indices point back at the right split rows.
    for i in val_idx:
        assert splits[i] == "val"


def test_load_split_indices_rejects_stale_meta(tmp_path: Path) -> None:
    meta = tmp_path / "meta.csv"
    with meta.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=["class_name", "class_idx", "source", "split"])
        w.writeheader()
        w.writerow({"class_name": "c0", "class_idx": 0, "source": "s", "split": "train"})
    # n_samples mismatch → refuse to return a wrong split.
    assert load_split_indices(meta, 99) is None


def test_summarize_split_is_stringy() -> None:
    groups, labels = _make_case()
    splits = assign_grouped_split(groups, labels, seed=1)
    text = summarize_split(groups, labels, splits)
    assert "train" in text and "val" in text
