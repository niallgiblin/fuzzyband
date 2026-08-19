"""Tests for the C2 Lakh selection-priors builder (DATA_STRATEGY.md §6.2)."""

from __future__ import annotations

from pathlib import Path

import build_lakh_priors as blp


def test_track_id_from_path() -> None:
    p = Path("R/R/U/TRRRUTV12903CEA11B/2740bc2a1cd9bae5.mid")
    assert blp._track_id_from_path(p) == "TRRRUTV12903CEA11B"
    assert blp._track_id_from_path(Path("foo/bar/notatrack/x.mid")) is None


def test_load_genre_tags(tmp_path: Path) -> None:
    cls = tmp_path / "tags.cls"
    cls.write_text(
        "# comment line\n"
        "TRAAAAK128F9318786\tRock\n"
        "TRBBBBB128F9318787\tMetal\tPunk\n"
        "\n"
        "TRCCCCC128F9318788\tJazz\n",
        encoding="utf-8",
    )
    tags = blp._load_genre_tags(cls)
    assert tags["TRAAAAK128F9318786"] == "Rock"
    assert tags["TRBBBBB128F9318787"] == "Metal"  # majority genre only
    assert tags["TRCCCCC128F9318788"] == "Jazz"
    assert len(tags) == 3


def test_pattern_weights_normalized_and_mapped() -> None:
    # All mass on 'backbeat' -> its representative indices get the peak weight.
    w = blp._pattern_weights({"backbeat": 1.0})
    assert len(w) == blp._PATTERN_COUNT
    assert max(w) == 1.0
    for idx in blp._BUCKET_TO_PATTERNS["backbeat"]:
        assert w[idx] > 0.0
    # A pattern index in no bucket stays zero.
    untouched = set(range(blp._PATTERN_COUNT))
    for idxs in blp._BUCKET_TO_PATTERNS.values():
        untouched -= set(idxs)
    for idx in untouched:
        assert w[idx] == 0.0


def test_pattern_weights_within_unit_range() -> None:
    w = blp._pattern_weights({"backbeat": 0.5, "busy": 0.3, "blast": 0.2})
    assert all(0.0 <= x <= 1.0 for x in w)
    assert max(w) == 1.0


def test_emit_header_all_genres(tmp_path: Path) -> None:
    weights = {g: [0.0] * blp._PATTERN_COUNT for g in range(blp._NUM_PRESETS)}
    weights[0][1] = 1.0
    out = tmp_path / "PatternPriors.h"
    blp._emit_header(weights, out)
    text = out.read_text(encoding="utf-8")
    assert "namespace PatternPriors" in text
    assert f"kNumGenres = {blp._NUM_PRESETS}" in text
    assert f"kPatternCount = {blp._PATTERN_COUNT}" in text
    # One initializer row per genre, each with kPatternCount entries.
    rows = [ln for ln in text.splitlines() if ln.strip().startswith("{") and "f," in ln]
    assert len(rows) == blp._NUM_PRESETS
    for row in rows:
        assert row.count("f,") + row.count("f }") == blp._PATTERN_COUNT
