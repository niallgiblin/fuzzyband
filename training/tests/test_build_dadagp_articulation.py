"""Tests for the C3 DadaGP articulation extractor (DATA_STRATEGY.md §6.3).

Uses a synthetic DadaGP-style token fixture so CI proves the extractor without
the access-gated DadaGP download.
"""

from __future__ import annotations

from pathlib import Path

import build_dadagp_articulation as bda
from perception_taxonomy import STYLE_LABELS


def test_classify_group_maps_to_perception_labels() -> None:
    assert bda.classify_group(1, {"palm_mute"}, 0) == "palm_mute"
    assert bda.classify_group(3, {"palm_mute"}, 0) == "palm_mute"   # palm mute wins
    assert bda.classify_group(3, set(), 0) == "open_chord"
    assert bda.classify_group(1, set(), 0) == "single_note"
    assert bda.classify_group(1, {"let_ring"}, 0) == "sustain"
    assert bda.classify_group(1, set(), bda._SUSTAIN_WAIT_TICKS) == "sustain"
    assert bda.classify_group(1, {"dead_note"}, 0) == "palm_mute"
    assert bda.classify_group(0, set(), 0) is None


def test_iter_articulations_over_token_stream() -> None:
    tokens = [
        "new_measure",
        "distorted0:note:s5:f3", "distorted0:note:s4:f3", "wait:480",   # 2 notes -> open_chord
        "distorted0:note:s6:f0", "distorted0:nfx:palm_mute", "wait:240", # palm_mute
        "distorted0:note:s3:f5", "wait:240",                            # single_note
        "distorted0:note:s2:f7", "distorted0:nfx:let_ring", "wait:1920", # sustain
        "wait:960",                                                      # rest -> silence
        "bass0:note:s1:f1", "wait:480",                                 # bass ignored (no guitar)
    ]
    labels = list(bda.iter_articulations(tokens))
    assert labels[0] == "open_chord"
    assert labels[1] == "palm_mute"
    assert labels[2] == "single_note"
    assert labels[3] == "sustain"
    assert "silence" in labels
    # Every emitted label is a valid perception style.
    for lab in labels:
        assert lab in STYLE_LABELS


def test_extract_effect_tolerates_both_separators() -> None:
    assert bda._extract_effect("distorted0:nfx:palm_mute") == "palm_mute"
    assert bda._extract_effect("distorted0:effect:harmonic") == "harmonic"
    assert bda._extract_effect("distorted0:note:s5:f3") is None


def test_main_smoke_on_fixture(tmp_path: Path) -> None:
    tokens_dir = tmp_path / "tokens"
    tokens_dir.mkdir()
    (tokens_dir / "song1.txt").write_text(
        "new_measure\n"
        "distorted0:note:s6:f0 distorted0:nfx:palm_mute wait:240\n"
        "distorted0:note:s5:f3 distorted0:note:s4:f3 wait:480\n"
        "distorted0:note:s3:f5 wait:240\n",
        encoding="utf-8",
    )
    out = tmp_path / "dadagp_articulation.json"
    rc = _run_main(tokens_dir, out)
    assert rc == 0
    import json
    payload = json.loads(out.read_text(encoding="utf-8"))
    assert payload["files"] == 1
    assert payload["events"] >= 3
    assert set(payload["counts"].keys()) == set(STYLE_LABELS)
    assert payload["counts"]["palm_mute"] >= 1
    assert payload["counts"]["open_chord"] >= 1


def _run_main(tokens_dir: Path, out: Path) -> int:
    import sys
    argv = sys.argv
    sys.argv = ["build_dadagp_articulation.py",
                "--tokens-dir", str(tokens_dir), "--json-out", str(out)]
    try:
        return bda.main()
    finally:
        sys.argv = argv


def test_main_missing_dir_errors(tmp_path: Path) -> None:
    rc = _run_main(tmp_path / "does_not_exist", tmp_path / "out.json")
    assert rc == 1
