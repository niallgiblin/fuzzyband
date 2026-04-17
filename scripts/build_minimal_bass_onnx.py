#!/usr/bin/env python3
"""
Build a tiny ONNX model for Phase 13 generative bass smoke tests:
  input  X_bass: float32 [1, 7]  — see docs/BASS_ONNX_IO.md
  output Y_bass: float32 [1, 4]  — proposal_confidence, root_midi, duration_beats, margin

Run from repo root:
  training/.venv/bin/python scripts/build_minimal_bass_onnx.py
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import onnx
from onnx import TensorProto, helper


def build_graph() -> onnx.ModelProto:
    # [1,7] -> MatMul [1,4] + bias -> Split -> Sigmoid on col0 only -> Concat -> Y_bass
    x_info = helper.make_tensor_value_info("X_bass", TensorProto.FLOAT, [1, 7])
    y_info = helper.make_tensor_value_info("Y_bass", TensorProto.FLOAT, [1, 4])

    rng = np.random.default_rng(43)
    w = rng.standard_normal((7, 4)).astype(np.float32) * 0.1
    b = rng.standard_normal((4,)).astype(np.float32) * 0.05

    w_init = helper.make_tensor(
        "W", TensorProto.FLOAT, [7, 4], w.flatten().astype(np.float32)
    )
    b_init = helper.make_tensor(
        "B", TensorProto.FLOAT, [4], b.astype(np.float32)
    )
    init_w = helper.make_node("Constant", [], ["W_tensor"], value=w_init)
    init_b = helper.make_node("Constant", [], ["B_tensor"], value=b_init)

    matmul = helper.make_node("MatMul", ["X_bass", "W_tensor"], ["logits_pre"])
    add = helper.make_node("Add", ["logits_pre", "B_tensor"], ["logits"])
    split_sizes = helper.make_tensor(
        "split_sizes",
        TensorProto.INT64,
        [2],
        np.array([1, 3], dtype=np.int64),
    )
    init_split = helper.make_node("Constant", [], ["split_tensor"], value=split_sizes)
    split = helper.make_node(
        "Split",
        ["logits", "split_tensor"],
        ["col0", "col123"],
        axis=1,
    )
    sigmoid = helper.make_node("Sigmoid", ["col0"], ["col0_sig"])
    concat = helper.make_node(
        "Concat",
        ["col0_sig", "col123"],
        ["Y_bass"],
        axis=1,
    )

    graph = helper.make_graph(
        [init_w, init_b, matmul, add, init_split, split, sigmoid, concat],
        "bass_generative_stub",
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
        default=Path("assets/bass_model.onnx"),
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
