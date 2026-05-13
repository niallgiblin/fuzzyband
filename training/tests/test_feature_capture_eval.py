from __future__ import annotations

import json
from pathlib import Path

import pytest

import evaluate_feature_capture as efc


def _feature_row(elapsed: float, rule: int, onnx: int | None, *, state: str = "SOFT") -> dict:
    return {
        "type": "feature",
        "schema_version": "feature_capture.v1",
        "sample_timestamp": int(elapsed * 48000),
        "elapsed_seconds": elapsed,
        "bpm": 140.0 + elapsed,
        "rms_energy": 0.2,
        "spectral_centroid": 2200.0,
        "high_freq_flux": 0.35,
        "state": 1,
        "state_name": state,
        "pitch_root_midi": 40.0,
        "pitch_confidence": 0.8,
        "policy_intensity": 0.5,
        "rms_delta": 0.1,
        "rule_pattern_index": rule,
        "rule_pattern_name": efc.PATTERN_NAMES[rule],
        "active_pattern_index": onnx if onnx is not None else rule,
        "active_pattern_name": efc.PATTERN_NAMES[onnx if onnx is not None else rule],
        "onnx_pattern_index": onnx,
        "onnx_pattern_name": efc.PATTERN_NAMES[onnx] if onnx is not None else None,
        "onnx_available": onnx is not None,
        "model_mode": "OnnxInference" if onnx is not None else "RuleBasedInference",
    }


def _write_capture(path: Path, rows: list[dict]) -> None:
    header = {
        "type": "header",
        "schema_version": "feature_capture.v1",
        "plugin_version": "0.5.2",
        "sample_rate": 48000.0,
        "block_size": 256,
        "capture_start_utc": "2026-05-13T10:00:00Z",
        "model_mode": "OnnxInference",
    }
    path.write_text("\n".join(json.dumps(row) for row in [header, *rows]) + "\n", encoding="utf-8")


def _write_annotations(path: Path, lines: list[str] | None = None) -> None:
    body = lines or [
        "0.0,1.0,2,Verse mid",
        "1.0,2.0,4,Chorus mid",
    ]
    path.write_text("start_seconds,end_seconds,label_index,label_name\n" + "\n".join(body) + "\n", encoding="utf-8")


def test_valid_fixture_reports_metrics_and_confusion_matrix(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    capture = tmp_path / "capture.jsonl"
    annotations = tmp_path / "labels.csv"
    json_out = tmp_path / "report.json"
    _write_capture(capture, [_feature_row(0.25, 2, 2), _feature_row(1.25, 5, 4)])
    _write_annotations(annotations)

    rc = efc.main(["--capture", str(capture), "--annotations", str(annotations), "--json-out", str(json_out)])

    assert rc == 0
    stdout = capsys.readouterr().out
    assert "Rule accuracy" in stdout
    assert "ONNX accuracy" in stdout
    assert "Rule/ONNX agreement" in stdout
    assert "Annotation coverage" in stdout
    assert "Confusion matrix" in stdout
    assert "Top disagreements" in stdout
    report = json.loads(json_out.read_text())
    assert report["rule_accuracy"] == pytest.approx(0.5)
    assert report["onnx_accuracy"] == pytest.approx(1.0)
    assert "Verse mid" in report["rule_per_class_accuracy"]


def test_rows_outside_annotations_are_ignored_and_coverage_reported(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    annotations = tmp_path / "labels.csv"
    _write_capture(capture, [_feature_row(0.25, 2, 2), _feature_row(3.0, 2, 2)])
    _write_annotations(annotations, ["0.0,1.0,2,Verse mid"])

    report, rc = efc.evaluate(capture, annotations, onnx=None, min_onnx_accuracy=0.65, top_disagreements=20)

    assert rc == 0
    assert report["annotated_rows"] == 1
    assert report["annotation_coverage"] == pytest.approx(0.5)


def test_annotation_name_mismatch_fails(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    annotations = tmp_path / "labels.csv"
    _write_capture(capture, [_feature_row(0.25, 2, 2)])
    _write_annotations(annotations, ["0.0,1.0,2,Chorus mid"])

    assert efc.main(["--capture", str(capture), "--annotations", str(annotations)]) == 1


def test_missing_required_capture_field_fails(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    annotations = tmp_path / "labels.csv"
    row = _feature_row(0.25, 2, 2)
    row.pop("sample_timestamp")
    _write_capture(capture, [row])
    _write_annotations(annotations, ["0.0,1.0,2,Verse mid"])

    assert efc.main(["--capture", str(capture), "--annotations", str(annotations)]) == 1


def test_onnx_threshold_failure_exits_nonzero(tmp_path: Path) -> None:
    capture = tmp_path / "capture.jsonl"
    annotations = tmp_path / "labels.csv"
    _write_capture(capture, [_feature_row(0.25, 2, 1), _feature_row(1.25, 4, 1)])
    _write_annotations(annotations)

    rc = efc.main(["--capture", str(capture), "--annotations", str(annotations), "--min-onnx-accuracy", "0.65"])

    assert rc == 2


def test_top_disagreement_output_includes_elapsed_seconds_and_feature_context(tmp_path: Path, capsys: pytest.CaptureFixture[str]) -> None:
    capture = tmp_path / "capture.jsonl"
    annotations = tmp_path / "labels.csv"
    _write_capture(capture, [_feature_row(0.25, 1, None)])
    _write_annotations(annotations, ["0.0,1.0,2,Verse mid"])

    assert efc.main(["--capture", str(capture), "--annotations", str(annotations)]) == 0

    stdout = capsys.readouterr().out
    assert "0.250s" in stdout
    assert "bpm=" in stdout
    assert "centroid=" in stdout
    assert "hf_flux=" in stdout
