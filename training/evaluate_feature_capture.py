#!/usr/bin/env python3
"""Evaluate Phase 34 feature_capture.v1 JSONL files against human annotations."""

from __future__ import annotations

import argparse
import csv
import json
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from sklearn.metrics import f1_score

from build_dataset import _rule_pattern_for_state

SCHEMA_VERSION = "feature_capture.v1"
ANNOTATION_HEADER = "start_seconds,end_seconds,label_index,label_name"

PATTERN_NAMES: dict[int, str] = {
    0: "Silent",
    1: "Verse slow",
    2: "Verse mid",
    3: "Verse fast",
    4: "Chorus mid",
    5: "Chorus fast",
    6: "Breakdown",
}

required_fields = {
    "type",
    "schema_version",
    "sample_timestamp",
    "elapsed_seconds",
    "bpm",
    "rms_energy",
    "spectral_centroid",
    "high_freq_flux",
    "state",
    "state_name",
    "pitch_root_midi",
    "pitch_confidence",
    "policy_intensity",
    "rms_delta",
    "rule_pattern_index",
    "rule_pattern_name",
    "active_pattern_index",
    "active_pattern_name",
    "onnx_pattern_index",
    "onnx_pattern_name",
    "onnx_available",
    "model_mode",
}


class EvaluationError(ValueError):
    """Raised for user-facing validation failures."""


@dataclass(frozen=True)
class Annotation:
    start_seconds: float
    end_seconds: float
    label_index: int
    label_name: str


@dataclass(frozen=True)
class JoinedRow:
    row: dict[str, Any]
    label_index: int
    label_name: str


def _pattern_name(index: int) -> str:
    if index not in PATTERN_NAMES:
        raise EvaluationError(f"label index out of range: {index}")
    return PATTERN_NAMES[index]


def load_capture(path: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    rows: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8") as fh:
        for line_no, line in enumerate(fh, start=1):
            stripped = line.strip()
            if not stripped:
                continue
            try:
                rows.append(json.loads(stripped))
            except json.JSONDecodeError as exc:
                raise EvaluationError(f"{path}:{line_no}: invalid JSON: {exc}") from exc

    if not rows:
        raise EvaluationError("capture file is empty")

    header = rows[0]
    if header.get("type") != "header":
        raise EvaluationError("first JSONL row must have type=header")
    if header.get("schema_version") != SCHEMA_VERSION:
        raise EvaluationError(f"header schema_version must be {SCHEMA_VERSION}")

    feature_rows = rows[1:]
    if not feature_rows:
        raise EvaluationError("capture contains no feature rows")

    for i, row in enumerate(feature_rows, start=2):
        missing = sorted(required_fields.difference(row))
        if missing:
            raise EvaluationError(f"capture row {i} missing required fields: {', '.join(missing)}")
        if row.get("type") != "feature":
            raise EvaluationError(f"capture row {i} must have type=feature")
        if row.get("schema_version") != SCHEMA_VERSION:
            raise EvaluationError(f"capture row {i} schema_version must be {SCHEMA_VERSION}")

    return header, feature_rows


def load_annotations(path: Path) -> list[Annotation]:
    with path.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.DictReader(fh)
        if reader.fieldnames != ANNOTATION_HEADER.split(","):
            raise EvaluationError(f"annotation CSV header must be {ANNOTATION_HEADER}")

        annotations: list[Annotation] = []
        for line_no, row in enumerate(reader, start=2):
            try:
                start = float(row["start_seconds"])
                end = float(row["end_seconds"])
                label_index = int(row["label_index"])
                label_name = row["label_name"]
            except (TypeError, ValueError) as exc:
                raise EvaluationError(f"annotation row {line_no} has invalid numeric values") from exc

            if start >= end:
                raise EvaluationError(f"annotation row {line_no} must have start_seconds < end_seconds")
            expected = _pattern_name(label_index)
            if label_name != expected:
                raise EvaluationError(
                    f"annotation row {line_no} label_index,label_name mismatch: "
                    f"{label_index} expects {expected}, got {label_name}"
                )
            annotations.append(Annotation(start, end, label_index, label_name))

    annotations.sort(key=lambda a: a.start_seconds)
    for prev, cur in zip(annotations, annotations[1:]):
        if cur.start_seconds < prev.end_seconds:
            raise EvaluationError("annotation ranges must not overlap")
    if not annotations:
        raise EvaluationError("annotation CSV contains no ranges")
    return annotations


def join_rows(feature_rows: list[dict[str, Any]], annotations: list[Annotation]) -> list[JoinedRow]:
    joined: list[JoinedRow] = []
    annotation_index = 0
    for row in sorted(feature_rows, key=lambda r: float(r["elapsed_seconds"])):
        elapsed = float(row["elapsed_seconds"])
        while annotation_index < len(annotations) and elapsed >= annotations[annotation_index].end_seconds:
            annotation_index += 1
        if annotation_index >= len(annotations):
            break
        ann = annotations[annotation_index]
        if ann.start_seconds <= elapsed < ann.end_seconds:
            joined.append(JoinedRow(row=row, label_index=ann.label_index, label_name=ann.label_name))
    return joined


def _empty_confusion() -> list[list[int]]:
    return [[0 for _ in range(7)] for _ in range(7)]


def _macro_f1(pairs: list[tuple[int, int]]) -> float | None:
    if not pairs:
        return None
    actuals = [actual for _, actual in pairs]
    preds = [pred for pred, _ in pairs]
    return float(f1_score(actuals, preds, average="macro", zero_division=0))


def _derive_rule_pattern(row: dict[str, Any]) -> int:
    state_float = float(row["state"])
    return _rule_pattern_for_state(float(row["bpm"]), state_float, float(row["policy_intensity"]))


def _accuracy(pairs: list[tuple[int, int]]) -> float | None:
    if not pairs:
        return None
    return sum(1 for pred, label in pairs if pred == label) / len(pairs)
    if not pairs:
        return None
    return sum(1 for pred, label in pairs if pred == label) / len(pairs)


def _per_class_accuracy(pairs: list[tuple[int, int]]) -> dict[str, float | None]:
    result: dict[str, float | None] = {}
    for label, name in PATTERN_NAMES.items():
        label_pairs = [(pred, actual) for pred, actual in pairs if actual == label]
        result[name] = _accuracy(label_pairs)
    return result


def _confusion_matrix(pairs: list[tuple[int, int]]) -> list[list[int]]:
    matrix = _empty_confusion()
    for pred, actual in pairs:
        if 0 <= pred <= 6 and 0 <= actual <= 6:
            matrix[actual][pred] += 1
    return matrix


def _captured_onnx_pairs(joined: list[JoinedRow]) -> list[tuple[int, int]]:
    pairs: list[tuple[int, int]] = []
    for item in joined:
        row = item.row
        if row.get("onnx_available") and row.get("onnx_pattern_index") is not None:
            pairs.append((int(row["onnx_pattern_index"]), item.label_index))
    return pairs


def _disagreements(joined: list[JoinedRow], limit: int) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for item in joined:
        row = item.row
        rule = int(row["rule_pattern_index"])
        onnx_raw = row.get("onnx_pattern_index")
        onnx = int(onnx_raw) if onnx_raw is not None else None
        if rule == item.label_index and (onnx is None or onnx == item.label_index):
            continue
        rows.append({
            "elapsed_seconds": float(row["elapsed_seconds"]),
            "human_label": item.label_name,
            "rule_label": _pattern_name(rule),
            "onnx_label": _pattern_name(onnx) if onnx is not None else "ONNX unavailable",
            "bpm": float(row["bpm"]),
            "rms_energy": float(row["rms_energy"]),
            "spectral_centroid": float(row["spectral_centroid"]),
            "high_freq_flux": float(row["high_freq_flux"]),
            "state": row["state_name"],
        })
    return rows[:limit]


def evaluate(
    capture: Path,
    annotations: Path,
    *,
    onnx: Path | None,
    min_onnx_accuracy: float,
    min_onnx_macro_f1: float,
    min_rule_onnx_agreement: float,
    top_disagreements: int,
) -> tuple[dict[str, Any], int]:
    _header, feature_rows = load_capture(capture)
    annotation_rows = load_annotations(annotations)
    joined = join_rows(feature_rows, annotation_rows)
    if not joined:
        raise EvaluationError("no captured feature rows fall inside annotation ranges")

    rule_pairs = [(int(item.row["rule_pattern_index"]), item.label_index) for item in joined]
    onnx_pairs = _captured_onnx_pairs(joined)
    onnx_available = bool(onnx_pairs)
    onnx_note = None
    if not onnx_available:
        onnx_note = "ONNX unavailable"
        if onnx is not None:
            onnx_note = "ONNX unavailable: offline ONNX execution is optional and no captured ONNX predictions exist"

    rule_onnx_pairs = [
        (int(item.row["rule_pattern_index"]), int(item.row["onnx_pattern_index"]))
        for item in joined
        if item.row.get("onnx_available") and item.row.get("onnx_pattern_index") is not None
    ]
    agreement = _accuracy(rule_onnx_pairs)

    onnx_accuracy = _accuracy(onnx_pairs)
    onnx_macro_f1 = _macro_f1(onnx_pairs)
    threshold_failed = False
    gate_failures: list[str] = []
    if onnx_accuracy is not None and onnx_accuracy < min_onnx_accuracy:
        threshold_failed = True
        gate_failures.append(
            f"ONNX accuracy {_fmt_accuracy(onnx_accuracy)} < {min_onnx_accuracy:.3f}"
        )
    if onnx_macro_f1 is not None and onnx_macro_f1 < min_onnx_macro_f1:
        threshold_failed = True
        gate_failures.append(
            f"ONNX macro-F1 {_fmt_accuracy(onnx_macro_f1)} < {min_onnx_macro_f1:.3f}"
        )
    if agreement is not None and agreement < min_rule_onnx_agreement:
        threshold_failed = True
        gate_failures.append(
            f"Rule/ONNX agreement {_fmt_accuracy(agreement)} < {min_rule_onnx_agreement:.3f}"
        )

    report = {
        "capture": str(capture),
        "annotations": str(annotations),
        "feature_rows": len(feature_rows),
        "annotated_rows": len(joined),
        "annotation_coverage": len(joined) / len(feature_rows),
        "rule_accuracy": _accuracy(rule_pairs),
        "onnx_accuracy": onnx_accuracy,
        "onnx_macro_f1": onnx_macro_f1,
        "onnx_note": onnx_note,
        "rule_onnx_agreement": agreement,
        "rule_per_class_accuracy": _per_class_accuracy(rule_pairs),
        "onnx_per_class_accuracy": _per_class_accuracy(onnx_pairs),
        "rule_confusion_matrix": _confusion_matrix(rule_pairs),
        "onnx_confusion_matrix": _confusion_matrix(onnx_pairs),
        "top_disagreements": _disagreements(joined, top_disagreements),
        "min_onnx_accuracy": min_onnx_accuracy,
        "min_onnx_macro_f1": min_onnx_macro_f1,
        "min_rule_onnx_agreement": min_rule_onnx_agreement,
        "gate_failures": gate_failures,
        "threshold_failed": threshold_failed,
    }
    return report, 2 if threshold_failed else 0


def replay_capture(
    capture: Path,
    *,
    min_rule_onnx_agreement: float | None = None,
) -> tuple[str, int]:
    _header, feature_rows = load_capture(capture)
    lines = ["Feature capture replay (rule re-derived vs captured ONNX)"]
    pairs: list[tuple[int, int]] = []
    for row in sorted(feature_rows, key=lambda r: float(r["elapsed_seconds"])):
        rule = _derive_rule_pattern(row)
        onnx_raw = row.get("onnx_pattern_index")
        onnx = int(onnx_raw) if row.get("onnx_available") and onnx_raw is not None else None
        if onnx is not None:
            pairs.append((rule, onnx))
        lines.append(
            f"{float(row['elapsed_seconds']):.3f}s rule={rule} onnx={onnx if onnx is not None else 'n/a'} "
            f"state={row['state_name']} bpm={float(row['bpm']):.1f}"
        )
    agreement = _accuracy(pairs)
    lines.append(f"Replay agreement (re-derived rule vs captured ONNX): {_fmt_accuracy(agreement)}")
    if min_rule_onnx_agreement is not None and agreement is not None and agreement < min_rule_onnx_agreement:
        lines.append(
            f"FAIL: agreement {_fmt_accuracy(agreement)} < {min_rule_onnx_agreement:.3f}"
        )
        return "\n".join(lines), 2
    return "\n".join(lines), 0


def _fmt_accuracy(value: float | None) -> str:
    return "n/a" if value is None else f"{value:.3f}"


def format_report(report: dict[str, Any]) -> str:
    lines = [
        "Feature capture evaluation",
        f"Capture rows: {report['feature_rows']}",
        f"Annotated rows: {report['annotated_rows']}",
        f"Annotation coverage: {report['annotation_coverage']:.1%}",
        f"Rule accuracy: {_fmt_accuracy(report['rule_accuracy'])}",
    ]
    if report["onnx_accuracy"] is None:
        lines.append("ONNX unavailable")
    else:
        lines.append(f"ONNX accuracy: {_fmt_accuracy(report['onnx_accuracy'])}")
        lines.append(f"ONNX macro-F1: {_fmt_accuracy(report.get('onnx_macro_f1'))}")
    lines.append(f"Rule/ONNX agreement: {_fmt_accuracy(report['rule_onnx_agreement'])}")
    lines.append("")
    lines.append("Gate status")
    lines.append(
        f"- ONNX accuracy gate ({report['min_onnx_accuracy']:.2f}): "
        f"{'FAIL' if report['onnx_accuracy'] is not None and report['onnx_accuracy'] < report['min_onnx_accuracy'] else 'PASS'}"
    )
    lines.append(
        f"- ONNX macro-F1 gate ({report['min_onnx_macro_f1']:.2f}): "
        f"{'FAIL' if report.get('onnx_macro_f1') is not None and report['onnx_macro_f1'] < report['min_onnx_macro_f1'] else 'PASS'}"
    )
    lines.append(
        f"- Rule/ONNX agreement gate ({report['min_rule_onnx_agreement']:.2f}): "
        f"{'FAIL' if report['rule_onnx_agreement'] is not None and report['rule_onnx_agreement'] < report['min_rule_onnx_agreement'] else 'PASS'}"
    )

    lines.append("")
    lines.append("Per-class accuracy")
    for name in PATTERN_NAMES.values():
        rule = _fmt_accuracy(report["rule_per_class_accuracy"][name])
        onnx = _fmt_accuracy(report["onnx_per_class_accuracy"][name])
        lines.append(f"- {name}: rule={rule} onnx={onnx}")

    lines.append("")
    lines.append("Confusion matrix (rows=human, columns=prediction)")
    lines.append("Rule confusion matrix")
    lines.extend(" ".join(str(v) for v in row) for row in report["rule_confusion_matrix"])
    lines.append("ONNX confusion matrix")
    lines.extend(" ".join(str(v) for v in row) for row in report["onnx_confusion_matrix"])

    lines.append("")
    lines.append("Top disagreements")
    if not report["top_disagreements"]:
        lines.append("- none")
    else:
        for row in report["top_disagreements"]:
            lines.append(
                f"- {row['elapsed_seconds']:.3f}s human={row['human_label']} "
                f"rule={row['rule_label']} onnx={row['onnx_label']} "
                f"bpm={row['bpm']:.1f} rms={row['rms_energy']:.4f} "
                f"centroid={row['spectral_centroid']:.1f} hf_flux={row['high_freq_flux']:.4f} "
                f"state={row['state']}"
            )
    return "\n".join(lines)


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", required=True, type=Path)
    parser.add_argument("--annotations", required=False, type=Path)
    parser.add_argument("--onnx", type=Path)
    parser.add_argument("--min-onnx-accuracy", type=float, default=0.65,
                        help="Minimum ONNX accuracy (legacy; macro-F1 is preferred)")
    parser.add_argument("--min-onnx-macro-f1", type=float, default=0.65)
    parser.add_argument("--min-rule-onnx-agreement", type=float, default=0.80)
    parser.add_argument("--replay", action="store_true",
                        help="Print side-by-side rule vs ONNX replay from capture rows")
    parser.add_argument("--top-disagreements", type=int, default=20)
    parser.add_argument("--json-out", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if args.replay:
        try:
            text, exit_code = replay_capture(
                args.capture,
                min_rule_onnx_agreement=args.min_rule_onnx_agreement
                if any(a.startswith("--min-rule-onnx-agreement") for a in (argv or sys.argv[1:]))
                else None,
            )
        except EvaluationError as exc:
            print(f"ERROR: {exc}", file=sys.stderr)
            return 1
        print(text)
        return exit_code

    if args.annotations is None:
        print("ERROR: --annotations is required unless --replay is set", file=sys.stderr)
        return 1

    try:
        report, exit_code = evaluate(
            args.capture,
            args.annotations,
            onnx=args.onnx,
            min_onnx_accuracy=args.min_onnx_accuracy,
            min_onnx_macro_f1=args.min_onnx_macro_f1,
            min_rule_onnx_agreement=args.min_rule_onnx_agreement,
            top_disagreements=args.top_disagreements,
        )
    except EvaluationError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(format_report(report))
    if args.json_out is not None:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if report["threshold_failed"]:
        for msg in report.get("gate_failures", []):
            print(f"ERROR: {msg}", file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
