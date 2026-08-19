"""Tests for the perception taxonomy SSOT (DATA_STRATEGY.md §5.2)."""

from __future__ import annotations

import pytest

import perception_taxonomy as tax


def test_five_style_classes() -> None:
    assert tax.NUM_STYLE_CLASSES == 5
    assert tax.STYLE_LABELS == (
        "palm_mute", "open_chord", "single_note", "sustain", "silence",
    )


def test_index_roundtrip() -> None:
    for i, label in enumerate(tax.STYLE_LABELS):
        assert tax.style_index(label) == i
        assert tax.style_name(i) == label


def test_is_valid_style() -> None:
    assert tax.is_valid_style("palm_mute")
    assert not tax.is_valid_style("chorus")
    assert not tax.is_valid_style("")


def test_unknown_label_raises() -> None:
    with pytest.raises(ValueError):
        tax.style_index("verse")


def test_out_of_range_index_raises() -> None:
    with pytest.raises(ValueError):
        tax.style_name(99)


def test_intensity_labels() -> None:
    assert tax.INTENSITY_LABELS == ("soft", "loud")
