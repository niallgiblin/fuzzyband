#!/usr/bin/env python3
"""
Build a tiny ONNX model for Phase 13 generative bass smoke tests:
  input  X_bass: float32 [1, 7]  — see docs/BASS_ONNX_IO.md
  output Y_bass: float32 [1, 32] — 16-step interleaved piano-roll
         [pitch_offset_0, velocity_0, ..., pitch_offset_15, velocity_15]

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
    # [1,7] -> MatMul [1,32] + bias -> Sigmoid on velocity columns -> Y_bass
    x_info = helper.make_tensor_value_info("X_bass", TensorProto.FLOAT, [1, 7])
    y_info = helper.make_tensor_value_info("Y_bass", TensorProto.FLOAT, [1, 32])

    rng = np.random.default_rng(43)
    w = rng.standard_normal((7, 32)).astype(np.float32) * 0.1
    b = rng.standard_normal((32,)).astype(np.float32) * 0.05

    w_init = helper.make_tensor(
        "W", TensorProto.FLOAT, [7, 32], w.flatten().astype(np.float32)
    )
    b_init = helper.make_tensor(
        "B", TensorProto.FLOAT, [32], b.astype(np.float32)
    )
    init_w = helper.make_node("Constant", [], ["W_tensor"], value=w_init)
    init_b = helper.make_node("Constant", [], ["B_tensor"], value=b_init)

    matmul = helper.make_node("MatMul", ["X_bass", "W_tensor"], ["logits_pre"])
    add = helper.make_node("Add", ["logits_pre", "B_tensor"], ["logits"])

    # Split even/odd columns: pitch offsets (even) and velocities (odd)
    # Use Gather to extract even and odd indices
    even_indices = np.arange(0, 32, 2, dtype=np.int64)  # 0,2,4,...,30
    odd_indices = np.arange(1, 32, 2, dtype=np.int64)   # 1,3,5,...,31

    even_init = helper.make_tensor(
        "even_idx", TensorProto.INT64, [16], even_indices
    )
    odd_init = helper.make_tensor(
        "odd_idx", TensorProto.INT64, [16], odd_indices
    )
    init_even = helper.make_node("Constant", [], ["even_idx_tensor"], value=even_init)
    init_odd = helper.make_node("Constant", [], ["odd_idx_tensor"], value=odd_init)

    gather_even = helper.make_node(
        "Gather", ["logits", "even_idx_tensor"], ["pitch_offsets"], axis=1
    )
    gather_odd = helper.make_node(
        "Gather", ["logits", "odd_idx_tensor"], ["velocities"], axis=1
    )

    # Sigmoid velocities to [0,1] range
    sigmoid = helper.make_node("Sigmoid", ["velocities"], ["velocities_sig"])

    # Interleave back: axis=1 concat alternates — we concatenate with a reshape
    # Strategy: stack [pitch_offsets, velocities_sig] shape [2,1,16],
    # transpose to [1,16,2], reshape to [1,32]
    # Opset 17: Unsqueeze axes is an input, not an attribute
    axes1_init = helper.make_tensor(
        "axes_1", TensorProto.INT64, [1], np.array([1], dtype=np.int64)
    )
    init_axes1 = helper.make_node("Constant", [], ["axes_1_tensor"], value=axes1_init)
    unsqueeze_p = helper.make_node(
        "Unsqueeze", ["pitch_offsets", "axes_1_tensor"], ["pitch_3d"]
    )  # [1,16] -> [1,1,16]
    unsqueeze_v = helper.make_node(
        "Unsqueeze", ["velocities_sig", "axes_1_tensor"], ["vel_3d"]
    )  # [1,16] -> [1,1,16]
    concat_3d = helper.make_node(
        "Concat", ["pitch_3d", "vel_3d"], ["stacked_3d"], axis=1
    )  # [1,2,16]
    transpose = helper.make_node(
        "Transpose", ["stacked_3d"], ["transposed"], perm=[0, 2, 1]
    )  # [1,16,2]

    # Reshape constant: [-1, 32]
    shape_init = helper.make_tensor(
        "target_shape", TensorProto.INT64, [2], np.array([1, 32], dtype=np.int64)
    )
    init_shape = helper.make_node("Constant", [], ["target_shape_tensor"], value=shape_init)
    reshape = helper.make_node(
        "Reshape", ["transposed", "target_shape_tensor"], ["Y_bass"]
    )

    graph = helper.make_graph(
        [
            init_w, init_b, matmul, add,
            init_even, init_odd, gather_even, gather_odd,
            sigmoid, init_axes1, unsqueeze_p, unsqueeze_v, concat_3d, transpose,
            init_shape, reshape,
        ],
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
