"""Tests for the C1 groove-template extractor (DATA_STRATEGY.md §6.1).

Exercises the pure reduction functions on synthetic drum hits (no GMD download
needed) plus the generated-header emitter, so CI proves the extractor end-to-end
against known inputs.
"""

from __future__ import annotations

from pathlib import Path

import pytest

import build_groove_template as bgt


def test_accumulate_grid_cell_and_deviation_wrap() -> None:
    acc = bgt._new_acc()
    # 120 BPM -> 500 ms/beat, 125 ms per 16th.
    bpm = 120.0
    # Exactly on beat-2 backbeat (cell 4), on-grid -> dev 0.
    # Just before the next bar's downbeat (beat 3.98) -> rounds up to cell 0 and
    # must read as slightly EARLY (negative ms), not a whole bar late.
    hits = [
        (1.0, 100, 38),    # cell 4, on grid
        (3.98, 90, 36),    # ~cell 0 of next bar, early
    ]
    bgt._accumulate(hits, bpm, acc)
    assert acc["vel"][4] == [100]
    assert acc["ms"][4] == [pytest.approx(0.0, abs=1e-6)]
    assert acc["vel"][0] == [90]
    # 0.02 beat early * 500 ms/beat = -10 ms.
    assert acc["ms"][0][0] == pytest.approx(-10.0, abs=0.5)
    assert acc["ms"][0][0] < 0.0


def test_reduce_template_builds_velocity_hierarchy() -> None:
    acc = bgt._new_acc()
    bpm = 120.0
    # Loud accents on the four beats, quiet on the off-16ths, enough hits/cell.
    hits = []
    for _ in range(bgt._MIN_HITS_PER_CELL + 5):
        for beat in (0.0, 1.0, 2.0, 3.0):      # cells 0,4,8,12
            hits.append((beat, 120, 36))
        for beat in (0.25, 0.75, 1.25, 1.75):  # off-16ths cells 1,3,5,7
            hits.append((beat, 40, 42))
    bgt._accumulate(hits, bpm, acc)
    t = bgt._reduce_template(acc)

    assert t["ghostVelocityLo"] == 30.0
    assert t["ghostVelocityHi"] == 55.0
    assert t["ghostThreshold"] == 62

    assert len(t["velocityMul"]) == 16
    assert len(t["timingMs"]) == 16
    assert t["velocityMul"][4] > t["velocityMul"][1]   # backbeat louder than off-16th
    assert t["velocityMul"][0] > 1.0                    # accents above the mean
    assert t["velocityMul"][1] < 1.0                    # ghosts below the mean
    # Jitter knobs are clamped to the musical band.
    assert bgt._TIMING_JITTER_MIN <= t["timingJitterMs"] <= bgt._TIMING_JITTER_MAX
    assert bgt._VEL_JITTER_MIN <= t["velocityJitter"] <= bgt._VEL_JITTER_MAX


def test_derive_metal_and_punk_tighten_timing() -> None:
    rock = {
        "velocityMul": [1.0] * 16,
        "timingMs": [(-6.0 if i == 0 else 8.0 if i == 4 else 0.0) for i in range(16)],
        "timingJitterMs": 6.0,
        "velocityJitter": 8.0,
        "ghostVelocityLo": 15.0,
        "ghostVelocityHi": 42.0,
        "ghostThreshold": 42,
    }
    metal = bgt._derive_metal(rock)
    punk = bgt._derive_punk(rock)

    for i in range(16):
        assert abs(metal["timingMs"][i]) <= abs(rock["timingMs"][i]) + 1e-9
        assert abs(punk["timingMs"][i]) <= abs(metal["timingMs"][i]) + 1e-9
    assert metal["timingJitterMs"] < rock["timingJitterMs"]
    assert punk["timingJitterMs"] < metal["timingJitterMs"]
    assert metal["_derived_from"] == "rock"
    assert punk["_derived_from"] == "rock"


def test_emit_header_shape(tmp_path: Path) -> None:
    templates = {
        name: {
            "velocityMul": [1.0] * 16,
            "timingMs": [0.0] * 16,
            "timingJitterMs": 6.0,
            "velocityJitter": 8.0,
            "ghostVelocityLo": 15.0,
            "ghostVelocityHi": 42.0,
            "ghostThreshold": 42,
            "files": 200,
            "hits": 100000,
        }
        for name in ("rock", "metal", "punk")
    }
    templates["metal"]["_derived_from"] = "rock"
    templates["punk"]["_derived_from"] = "rock"

    out = tmp_path / "GrooveTemplateData.h"
    bgt._emit_header(templates, out)
    text = out.read_text(encoding="utf-8")

    assert "#pragma once" in text
    assert "namespace Groove::data" in text
    for name in ("Rock", "Metal", "Punk"):
        assert f"k{name}VelocityMul[16]" in text
        assert f"k{name}TimingMs[16]" in text
        assert f"k{name}GhostThreshold" in text
    # Each 16-element array literal has exactly 16 float entries.
    for line in text.splitlines():
        if "VelocityMul[16]" in line or "TimingMs[16]" in line:
            assert line.count("f,") + line.count("f }") == 16
