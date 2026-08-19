#!/usr/bin/env python3
"""Perception layer label taxonomy — single source of truth (DATA_STRATEGY.md §5.2).

The **perception** layer learns guitar-audio → playing-style / intensity. These
labels are *physically grounded* in the guitarist's playing and are the only
labels the player self-annotates honestly (§5.2, "the circularity break").

The **arrangement** layer (section identity, groove feel, pattern selection) is
NOT self-labeled — it comes from external datasets (Phase 4) and the energy-based
`StructureTagger`. Do not add section/verse/chorus labels here.

Every offline tool that touches perception labels (dataset builders, the
annotation slicer, the capture evaluator) must import these constants instead of
re-declaring their own list, so the label set can never drift between tools.
"""

from __future__ import annotations

# ── Style classes (self-labeled, physically grounded) ────────────────────────
# Order is authoritative: it defines the integer class index used by the mel-CNN
# perception head and by data/processed/y.npy. Do NOT reorder — it would silently
# remap every previously built tensor.
STYLE_LABELS: tuple[str, ...] = (
    "palm_mute",
    "open_chord",
    "single_note",
    "sustain",
    "silence",
)

STYLE_TO_IDX: dict[str, int] = {name: i for i, name in enumerate(STYLE_LABELS)}
IDX_TO_STYLE: dict[int, str] = {i: name for i, name in enumerate(STYLE_LABELS)}

# ── Intensity (derivable from the energy analyser; a secondary self-label) ────
INTENSITY_LABELS: tuple[str, ...] = ("soft", "loud")
INTENSITY_TO_IDX: dict[str, int] = {name: i for i, name in enumerate(INTENSITY_LABELS)}

NUM_STYLE_CLASSES: int = len(STYLE_LABELS)


def is_valid_style(label: str) -> bool:
    """Return True if *label* is a known perception style class."""
    return label in STYLE_TO_IDX


def style_index(label: str) -> int:
    """Map a style label to its class index, raising on unknown labels."""
    try:
        return STYLE_TO_IDX[label]
    except KeyError as exc:
        known = ", ".join(STYLE_LABELS)
        raise ValueError(
            f"unknown perception style label {label!r}; expected one of: {known}"
        ) from exc


def style_name(index: int) -> str:
    """Map a class index back to its style label, raising on out-of-range."""
    try:
        return IDX_TO_STYLE[index]
    except KeyError as exc:
        raise ValueError(
            f"style index out of range: {index} (0..{NUM_STYLE_CLASSES - 1})"
        ) from exc
