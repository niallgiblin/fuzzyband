from __future__ import annotations

import json
from pathlib import Path

import pytest
import torch

import analyze_feature_proxy_gap as gap


def _feature_row(bpm: float = 120.0, centroid: float = 2000.0) -> dict:
    return {
        "type": "feature",
        "schema_version": "feature_capture.v1",
        "bpm": bpm,
        "rms_energy": 0.5,
        "spectral_centroid": centroid,
        "high_freq_flux": 0.2,
        "state": 1,
        "pitch_root_midi": 40.0,
        "pitch_confidence": 0.9,
        "policy_intensity": 0.5,
        "rms_delta": 0.1,
    }


def _write_capture(path: Path, rows: list[dict]) -> None:
    header = {"type": "header", "schema_version": "feature_capture.v1"}
    path.write_text("\n".join(json.dumps(row) for row in [header, *rows]) + "\n", encoding="utf-8")


def _write_proxy(path: Path, rows: list[list[float]]) -> None:
    torch.save({"X": torch.tensor(rows, dtype=torch.float32)}, path)


def test_comparable_feature_stats_include_required_metrics(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    proxy = tmp_path / "proxy.pt"
    _write_capture(capture, [_feature_row(100.0), _feature_row(140.0)])
    _write_proxy(proxy, [[90.0, 0.4, 1900.0, 0.1, 1.0], [150.0, 0.6, 2100.0, 0.3, 2.0]])

    report = gap.analyze([capture], [proxy])
    bpm = next(f for f in report["features"] if f["feature"] == "bpm")

    assert "mean" in bpm["captured"]
    assert "std" in bpm["captured"]
    assert "p05" in bpm["captured"]
    assert "p50" in bpm["captured"]
    assert "p95" in bpm["captured"]
    assert "absolute_mean_delta" in bpm
    assert "standardized_mean_delta" in bpm


def test_verdict_thresholds() -> None:
    assert gap.verdict(0.49, 100, 100) == "acceptable"
    assert gap.verdict(0.5, 100, 100) == "risky"
    assert gap.verdict(1.49, 100, 100) == "risky"
    assert gap.verdict(1.5, 100, 100) == "needs proxy change"


def test_runtime_only_fields_are_not_modeled(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    proxy = tmp_path / "proxy.pt"
    _write_capture(capture, [_feature_row()])
    _write_proxy(proxy, [[120.0, 0.5, 2000.0, 0.2, 1.0]])

    report = gap.analyze([capture], [proxy])
    pitch = next(f for f in report["features"] if f["feature"] == "pitchRootMidi")

    assert pitch["verdict"] == "not modeled by pattern proxy"
    assert pitch["proxy"] is None


def test_markdown_mentions_spectral_centroid(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    proxy = tmp_path / "proxy.pt"
    _write_capture(capture, [_feature_row(centroid=3500.0)])
    _write_proxy(proxy, [[120.0, 0.5, 1000.0, 0.2, 1.0]])

    report = gap.analyze([capture], [proxy])
    text = gap.format_report(report)

    assert "spectralCentroid" in text
    assert "highFreqFlux" in text


def test_missing_required_capture_fields_fail(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    proxy = tmp_path / "proxy.pt"
    row = _feature_row()
    row.pop("spectral_centroid")
    _write_capture(capture, [row])
    _write_proxy(proxy, [[120.0, 0.5, 1000.0, 0.2, 1.0]])

    with pytest.raises(gap.GapError):
        gap.analyze([capture], [proxy])
