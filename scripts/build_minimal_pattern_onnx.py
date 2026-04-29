#!/usr/bin/env python3
"""
Build a tiny ONNX model for Phase 10 smoke tests:
  input  X: float32 [1, 5]  — FeatureVector fields (bpm, rms, centroid, flux, state)
  output Y: int64   [1]     — pattern index via sum(features) cast to int64 mod 7

Run from repo root (uses training/.venv if onnx not global):
  training/.venv/bin/python scripts/build_minimal_pattern_onnx.py
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper


def build_graph() -> onnx.ModelProto:
    # X [1,7] -> ReduceSum axes=1 -> [1] float
    # Mod 7 -> [1] float
    # Floor -> Cast INT64 -> Y [1]
    x_info = helper.make_tensor_value_info("X", TensorProto.FLOAT, [1, 7])
    y_info = helper.make_tensor_value_info("Y", TensorProto.INT64, [1])

    seven_i = helper.make_tensor(
        "seven_i", TensorProto.INT64, [1], np.array([7], dtype=np.int64)
    )
    init_seven_i = helper.make_node("Constant", [], ["S7i"], value=seven_i)

    axes = helper.make_tensor(
        "axes", TensorProto.INT64, [1], np.array([1], dtype=np.int64)
    )
    init_axes = helper.make_node("Constant", [], ["axes1"], value=axes)
    reduce = helper.make_node(
        "ReduceSum", ["X", "axes1"], ["rsum"], keepdims=0
    )
    cast_sum = helper.make_node(
        "Cast", ["rsum"], ["rsum_i"], to=TensorProto.INT64
    )
    mod = helper.make_node("Mod", ["rsum_i", "S7i"], ["Y"], fmod=0)

    graph = helper.make_graph(
        [init_seven_i, init_axes, reduce, cast_sum, mod],
        "pattern_index_stub",
        [x_info],
        [y_info],
    )
    model = helper.make_model(
        graph,
        producer_name="MetalAccompaniment",
        opset_imports=[helper.make_opsetid("", 17)],
    )
    onnx.checker.check_model(model)
    return model


def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("assets/accompaniment_model.onnx"),
        help="Output .onnx path",
    )
    args = p.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    model = build_graph()
    onnx.save(model, str(args.output))
    print(f"Wrote {args.output.resolve()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
