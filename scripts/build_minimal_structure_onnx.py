#!/usr/bin/env python3
"""
Build a tiny ONNX model for Phase 12 structure smoke tests:
  input  X_struct: float32 [1, 12, 7]  — windowed FeatureVector history (see docs/ONNX_IO.md)
  output Y_struct: float32 [1, 3]     — logits over StructureState order (SILENT/SOFT/LOUD)

Run from repo root:
  training/.venv/bin/python scripts/build_minimal_structure_onnx.py
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper


def build_graph() -> onnx.ModelProto:
    # [1,12,7] -> ReduceMean axis=1 -> [1,7]
    # MatMul [1,7] x [7,3] + bias -> [1,3] logits
    x_info = helper.make_tensor_value_info("X_struct", TensorProto.FLOAT, [1, 12, 7])
    y_info = helper.make_tensor_value_info("Y_struct", TensorProto.FLOAT, [1, 3])

    # axes as attribute (ReduceMean single-input form for opset 13+)
    reduce_mean = helper.make_node(
        "ReduceMean",
        ["X_struct"],
        ["pooled"],
        axes=[1],
        keepdims=0,
    )

    rng = np.random.default_rng(42)
    w = rng.standard_normal((7, 3)).astype(np.float32) * 0.1
    b = rng.standard_normal((3,)).astype(np.float32) * 0.05

    w_init = helper.make_tensor(
        "W", TensorProto.FLOAT, [7, 3], w.flatten().astype(np.float32)
    )
    b_init = helper.make_tensor(
        "B", TensorProto.FLOAT, [3], b.astype(np.float32)
    )
    init_w = helper.make_node("Constant", [], ["W_tensor"], value=w_init)
    init_b = helper.make_node("Constant", [], ["B_tensor"], value=b_init)

    matmul = helper.make_node("MatMul", ["pooled", "W_tensor"], ["logits_pre"])
    add = helper.make_node("Add", ["logits_pre", "B_tensor"], ["Y_struct"])

    graph = helper.make_graph(
        [reduce_mean, init_w, init_b, matmul, add],
        "structure_logits_stub",
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
        default=Path("assets/structure_model.onnx"),
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
