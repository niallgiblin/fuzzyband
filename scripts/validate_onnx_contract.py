#!/usr/bin/env python3
"""Validate exported ONNX graphs against docs/ONNX_IO.md and docs/BASS_ONNX_IO.md."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import onnx
from onnx import TensorProto, shape_inference

FLOAT = TensorProto.FLOAT
INT64 = TensorProto.INT64


def _dims(vi) -> list:
    t = vi.type.tensor_type
    out: list = []
    for d in t.shape.dim:
        # Dimension uses oneof { dim_value, dim_param }; do not use truthiness on dim_value (0 is valid).
        which = d.WhichOneof("value")
        if which == "dim_value":
            out.append(int(d.dim_value))
        else:
            out.append(None)
    return out


def _find_tensor(inferred: onnx.ModelProto, name: str):
    for coll in (inferred.graph.input, inferred.graph.output, inferred.graph.value_info):
        for vi in coll:
            if vi.name == name:
                return vi
    raise ValueError(f"tensor '{name}' not found in model")


# ─── Pattern (frozen, unchanged from v0.2.0) ─────────────────────────────────

def check_pattern(path: Path) -> None:
    raw = onnx.load(path)
    onnx.checker.check_model(raw)
    inferred = shape_inference.infer_shapes(raw)
    vx = _find_tensor(inferred, "X")
    vy = _find_tensor(inferred, "Y")
    if vx.type.tensor_type.elem_type != FLOAT:
        print(
            f"pattern: X must be float32, got elem_type {vx.type.tensor_type.elem_type}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    if vy.type.tensor_type.elem_type != INT64:
        print(
            f"pattern: Y must be int64, got elem_type {vy.type.tensor_type.elem_type}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    dx = _dims(vx)
    dy = _dims(vy)
    if len(dx) != 2 or (dx[1] is not None and dx[1] != 5):
        print(f"pattern: X must have feature dim 5, got shape {dx}", file=sys.stderr)
        raise SystemExit(1)
    ok_y = False
    if dy == [1]:
        ok_y = True
    elif dy == []:
        ok_y = True
    elif len(dy) == 1 and dy[0] is None:
        ok_y = True
    if not ok_y:
        print(
            f"pattern: Y must be shape [1] or scalar; got {dy}",
            file=sys.stderr,
        )
        raise SystemExit(1)


# ─── Structure (v0.4.0: 3-class SILENT/SOFT/LOUD) ────────────────────────────

def check_structure(path: Path) -> None:
    """Validate structure classifier model: Y_struct [1,3] float32 logits."""
    raw = onnx.load(path)
    onnx.checker.check_model(raw)
    inferred = shape_inference.infer_shapes(raw)
    vx = _find_tensor(inferred, "X_struct")
    vy = _find_tensor(inferred, "Y_struct")
    if vx.type.tensor_type.elem_type != FLOAT:
        print(
            f"structure: X_struct must be float32, got elem_type {vx.type.tensor_type.elem_type}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    if vy.type.tensor_type.elem_type != FLOAT:
        print(
            f"structure: Y_struct must be float32, got elem_type {vy.type.tensor_type.elem_type}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    dx = _dims(vx)
    dy = _dims(vy)
    if len(dx) != 3 or (dx[1] is not None and dx[1] != 12) or (dx[2] is not None and dx[2] != 7):
        print(f"structure: X_struct must be [1, 12, 7], got shape {dx}", file=sys.stderr)
        raise SystemExit(1)
    if len(dy) != 2 or (dy[1] is not None and dy[1] != 3):
        print(f"structure: Y_struct must be [1, 3], got shape {dy}", file=sys.stderr)
        raise SystemExit(1)


# ─── Bass (v0.4.0: 16-step piano-roll [1,32]) ────────────────────────────────

def check_bass(path: Path) -> None:
    """Validate bass model: Y_bass [1,32] float32 interleaved piano-roll."""
    raw = onnx.load(path)
    onnx.checker.check_model(raw)
    inferred = shape_inference.infer_shapes(raw)
    vx = _find_tensor(inferred, "X_bass")
    vy = _find_tensor(inferred, "Y_bass")
    if vx.type.tensor_type.elem_type != FLOAT:
        print(
            f"bass: X_bass must be float32, got elem_type {vx.type.tensor_type.elem_type}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    if vy.type.tensor_type.elem_type != FLOAT:
        print(
            f"bass: Y_bass must be float32, got elem_type {vy.type.tensor_type.elem_type}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    dx = _dims(vx)
    dy = _dims(vy)
    if len(dx) != 2 or (dx[1] is not None and dx[1] != 7):
        print(f"bass: X_bass must have feature dim 7, got shape {dx}", file=sys.stderr)
        raise SystemExit(1)
    if len(dy) != 2 or (dy[1] is not None and dy[1] != 32):
        print(f"bass: Y_bass must be [1, 32], got shape {dy}", file=sys.stderr)
        raise SystemExit(1)


# ─── CLI ──────────────────────────────────────────────────────────────────────

def main() -> int:
    ap = argparse.ArgumentParser(description="Validate ONNX I/O vs frozen docs")
    ap.add_argument("--pattern", type=Path, default=None, help="Pattern .onnx path")
    ap.add_argument("--structure", type=Path, default=None, help="Structure .onnx path")
    ap.add_argument("--bass", type=Path, default=None, help="Bass .onnx path")
    args = ap.parse_args()
    if args.pattern is None and args.structure is None and args.bass is None:
        print("error: pass at least one of --pattern, --structure, or --bass", file=sys.stderr)
        return 1
    if args.pattern is not None:
        check_pattern(args.pattern)
    if args.structure is not None:
        check_structure(args.structure)
    if args.bass is not None:
        check_bass(args.bass)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
