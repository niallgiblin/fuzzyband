#!/usr/bin/env python3
"""Analyze captured-vs-proxy feature distribution gaps for Phase 34."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

SCHEMA_VERSION = "feature_capture.v1"
COMPARABLE_FEATURES = [
    ("bpm", "bpm", 0),
    ("rmsEnergy", "rms_energy", 1),
    ("spectralCentroid", "spectral_centroid", 2),
    ("highFreqFlux", "high_freq_flux", 3),
    ("state_float", "state", 4),
]
RUNTIME_ONLY_FEATURES = [
    ("pitchRootMidi", "pitch_root_midi"),
    ("pitchConfidence", "pitch_confidence"),
    ("policyIntensity", "policy_intensity"),
    ("rmsDelta", "rms_delta"),
]
REQUIRED_CAPTURE_FIELDS = {
    "type",
    "schema_version",
    "bpm",
    "rms_energy",
    "spectral_centroid",
    "high_freq_flux",
    "state",
    "pitch_root_midi",
    "pitch_confidence",
    "policy_intensity",
    "rms_delta",
}


class GapError(ValueError):
    """Raised for validation failures."""


def _percentile(values: list[float], pct: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * pct
    lo = int(pos)
    hi = min(lo + 1, len(ordered) - 1)
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def stats(values: list[float]) -> dict[str, float | int]:
    if not values:
        return {"count": 0, "mean": 0.0, "std": 0.0, "min": 0.0, "p05": 0.0, "p50": 0.0, "p95": 0.0, "max": 0.0}
    n = len(values)
    mean = sum(values) / n
    var = sum((v - mean) ** 2 for v in values) / n
    return {
        "count": n,
        "mean": mean,
        "std": var ** 0.5,
        "min": min(values),
        "p05": _percentile(values, 0.05),
        "p50": _percentile(values, 0.50),
        "p95": _percentile(values, 0.95),
        "max": max(values),
    }


def verdict(standardized_mean_delta: float, captured_count: int, proxy_count: int) -> str:
    if standardized_mean_delta < 0.5:
        base = "acceptable"
    elif standardized_mean_delta < 1.5:
        base = "risky"
    else:
        base = "needs proxy change"
    if captured_count < 50 or proxy_count < 50:
        return f"{base} (low sample count warning)"
    return base


def load_captures(paths: list[Path]) -> list[dict[str, Any]]:
    rows: list[dict[str, Any]] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as fh:
            file_rows = [json.loads(line) for line in fh if line.strip()]
        if not file_rows or file_rows[0].get("type") != "header":
            raise GapError(f"{path}: first row must be a feature_capture.v1 header")
        if file_rows[0].get("schema_version") != SCHEMA_VERSION:
            raise GapError(f"{path}: schema_version must be {SCHEMA_VERSION}")
        for line_no, row in enumerate(file_rows[1:], start=2):
            missing = sorted(REQUIRED_CAPTURE_FIELDS.difference(row))
            if missing:
                raise GapError(f"{path}:{line_no}: missing required capture fields: {', '.join(missing)}")
            if row.get("type") != "feature" or row.get("schema_version") != SCHEMA_VERSION:
                raise GapError(f"{path}:{line_no}: expected feature row with schema_version={SCHEMA_VERSION}")
            rows.append(row)
    if not rows:
        raise GapError("no captured feature rows loaded")
    return rows


def _torch_load(path: Path) -> Any:
    try:
        import torch
    except ImportError as exc:
        raise GapError("torch is required to read proxy .pt tensors") from exc
    return torch.load(str(path), map_location="cpu", weights_only=True)


def _load_norm_stats(tensor_path: Path) -> dict[str, Any] | None:
    candidates = [
        tensor_path.parent / "norm_stats.json",
        tensor_path.parent.parent / "processed" / "norm_stats.json",
    ]
    for path in candidates:
        if path.exists():
            return json.loads(path.read_text(encoding="utf-8"))
    return None


def _denormalize_rows(rows: list[list[float]], stats: dict[str, Any]) -> list[list[float]]:
    mean = stats.get("mean")
    std = stats.get("std")
    if not mean or not std or len(mean) < 5 or len(std) < 5:
        return rows
    out: list[list[float]] = []
    for row in rows:
        restored = [
            float(row[i]) * float(std[i]) + float(mean[i])
            for i in range(min(5, len(row)))
        ]
        out.append(restored)
    return out


def load_proxy_tensors(paths: list[Path]) -> tuple[list[list[float]], list[str]]:
    matrices: list[list[list[float]]] = []
    used: list[str] = []
    for path in paths:
        if not path.exists():
            continue
        obj = _torch_load(path)
        tensor = obj["X"] if isinstance(obj, dict) and "X" in obj else obj
        if hasattr(tensor, "detach"):
            tensor = tensor.detach().cpu()
        rows = tensor.tolist()
        if rows and isinstance(rows[0], (int, float)):
            rows = [rows]
        raw_rows = [[float(v) for v in row[:5]] for row in rows]
        meta = obj.get("meta") if isinstance(obj, dict) else None
        looks_normalized = (
            isinstance(meta, dict)
            and "norm" in str(meta.get("norm", "")).lower()
        ) or (
            raw_rows
            and abs(raw_rows[0][0]) < 50.0
            and float(max(v for row in raw_rows for v in row)) < 100.0
            and float(min(v for row in raw_rows for v in row)) < 0.0
        )
        if looks_normalized:
            stats = _load_norm_stats(path)
            if stats is not None:
                raw_rows = _denormalize_rows(raw_rows, stats)
        matrices.append(raw_rows)
        used.append(str(path))
    values = [row for matrix in matrices for row in matrix]
    if not values:
        raise GapError("no proxy tensor rows loaded")
    return values, used


def default_proxy_paths() -> list[Path]:
    root = Path(__file__).resolve().parent
    return [
        root / "data/processed/train_merged.pt",
        root / "data/processed/train.pt",
        root / "data/lakh/lakh_tensors.pt",
    ]


def analyze(captures: list[Path], proxy_tensors: list[Path] | None) -> dict[str, Any]:
    capture_rows = load_captures(captures)
    proxy_paths = proxy_tensors if proxy_tensors else default_proxy_paths()
    proxy_rows, used_proxy_paths = load_proxy_tensors(proxy_paths)

    features: list[dict[str, Any]] = []
    for name, capture_key, proxy_index in COMPARABLE_FEATURES:
        captured_values = [float(row[capture_key]) for row in capture_rows]
        proxy_values = [float(row[proxy_index]) for row in proxy_rows]
        captured_stats = stats(captured_values)
        proxy_stats = stats(proxy_values)
        if name == "spectralCentroid":
            # Phase 36: p50 is the authoritative guitar capture statistic (see FEATURE_PROXY.md)
            abs_delta = abs(float(captured_stats["p50"]) - float(proxy_stats["p50"]))
        else:
            abs_delta = abs(float(captured_stats["mean"]) - float(proxy_stats["mean"]))
        standardized_mean_delta = abs_delta / max(float(proxy_stats["std"]), 1.0e-8)
        features.append({
            "feature": name,
            "captured": captured_stats,
            "proxy": proxy_stats,
            "absolute_mean_delta": abs_delta,
            "standardized_mean_delta": standardized_mean_delta,
            "verdict": verdict(standardized_mean_delta, int(captured_stats["count"]), int(proxy_stats["count"])),
        })

    for name, capture_key in RUNTIME_ONLY_FEATURES:
        captured_values = [float(row[capture_key]) for row in capture_rows]
        features.append({
            "feature": name,
            "captured": stats(captured_values),
            "proxy": None,
            "absolute_mean_delta": None,
            "standardized_mean_delta": None,
            "verdict": "not modeled by pattern proxy",
        })

    return {
        "captures": [str(path) for path in captures],
        "proxy_tensors": used_proxy_paths,
        "captured_rows": len(capture_rows),
        "proxy_rows": len(proxy_rows),
        "features": features,
    }


def markdown_table(report: dict[str, Any]) -> str:
    lines = [
        "| Feature | Captured n | Captured mean | Captured p50 | Captured p95 | Proxy n | Proxy mean | Proxy p50 | Proxy p95 | standardized mean delta | Verdict |",
        "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|",
    ]
    for feature in report["features"]:
        cap = feature["captured"]
        proxy = feature["proxy"]
        if proxy is None:
            lines.append(
                f"| {feature['feature']} | {cap['count']} | {cap['mean']:.4f} | {cap['p50']:.4f} | {cap['p95']:.4f} | - | - | - | - | - | {feature['verdict']} |"
            )
            continue
        lines.append(
            f"| {feature['feature']} | {cap['count']} | {cap['mean']:.4f} | {cap['p50']:.4f} | {cap['p95']:.4f} | "
            f"{proxy['count']} | {proxy['mean']:.4f} | {proxy['p50']:.4f} | {proxy['p95']:.4f} | "
            f"{feature['standardized_mean_delta']:.4f} | {feature['verdict']} |"
        )
    return "\n".join(lines)


def format_report(report: dict[str, Any]) -> str:
    return "\n".join([
        "# Phase 34 captured-vs-proxy gap",
        "",
        f"Captured rows: {report['captured_rows']}",
        f"Proxy rows: {report['proxy_rows']}",
        f"Capture files: {', '.join(report['captures'])}",
        f"Proxy tensors: {', '.join(report['proxy_tensors'])}",
        "",
        markdown_table(report),
    ])


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture", action="append", type=Path, default=[])
    parser.add_argument("--proxy-tensor", action="append", type=Path, default=[])
    parser.add_argument("--json-out", type=Path)
    parser.add_argument("--markdown-out", type=Path)
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if not args.capture:
        print("ERROR: at least one --capture PATH is required", file=sys.stderr)
        return 1
    try:
        report = analyze(args.capture, args.proxy_tensor or None)
    except GapError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    text = format_report(report)
    print(text)
    if args.json_out:
        args.json_out.parent.mkdir(parents=True, exist_ok=True)
        args.json_out.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.markdown_out:
        args.markdown_out.parent.mkdir(parents=True, exist_ok=True)
        args.markdown_out.write_text(text + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
