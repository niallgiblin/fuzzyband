"""Tests for scripts/validate_onnx_contract.py — ONNX I/O contract validation.

These tests are automatically skipped when no .onnx model files are present
(the default Phase 1 configuration). They validate that any committed ONNX
models conform to the shapes and types specified in docs/ONNX_IO.md and
docs/BASS_ONNX_IO.md.
"""

from __future__ import annotations

import sys
from pathlib import Path

import pytest

# Locate the scripts directory relative to this test file
_REPO_ROOT = Path(__file__).resolve().parent.parent.parent
_SCRIPTS_DIR = _REPO_ROOT / "scripts"
if str(_SCRIPTS_DIR) not in sys.path:
    sys.path.insert(0, str(_SCRIPTS_DIR))

_PATTERN_ONNX = _REPO_ROOT / "assets" / "accompaniment_model.onnx"
_STRUCTURE_ONNX = _REPO_ROOT / "assets" / "structure_model.onnx"
_BASS_ONNX = _REPO_ROOT / "assets" / "bass_model.onnx"

_HAS_ONNX_MODELS = _PATTERN_ONNX.exists() or _STRUCTURE_ONNX.exists() or _BASS_ONNX.exists()


def _import_onnx() -> bool:
    try:
        import onnx  # noqa: F401
        return True
    except ImportError:
        return False


# ─── Pattern model ────────────────────────────────────────────────────────────

@pytest.mark.skipif(not _PATTERN_ONNX.exists(), reason="assets/accompaniment_model.onnx not present")
@pytest.mark.skipif(not _import_onnx(), reason="onnx package not installed")
class TestPatternModelContract:
    def test_model_loads_without_error(self) -> None:
        import onnx
        model = onnx.load(str(_PATTERN_ONNX))
        onnx.checker.check_model(model)

    def test_input_X_is_float32(self) -> None:
        from validate_onnx_contract import check_pattern
        # Should not raise
        check_pattern(_PATTERN_ONNX)

    def test_input_feature_dim_is_7(self) -> None:
        import onnx
        from onnx import shape_inference
        raw = onnx.load(str(_PATTERN_ONNX))
        inferred = shape_inference.infer_shapes(raw)
        x = next(vi for vi in inferred.graph.input if vi.name == "X")
        dims = [d.dim_value for d in x.type.tensor_type.shape.dim]
        assert len(dims) >= 2
        assert dims[1] == 7 or dims[1] == 0  # v0.4.0 contract

    def test_output_Y_is_int64(self) -> None:
        import onnx
        from onnx import TensorProto
        raw = onnx.load(str(_PATTERN_ONNX))
        y = next(vi for vi in raw.graph.output if vi.name == "Y")
        assert y.type.tensor_type.elem_type == TensorProto.INT64


# ─── Structure model (v0.4.0: Y_struct [1,3]) ────────────────────────────────

@pytest.mark.skipif(not _STRUCTURE_ONNX.exists(), reason="assets/structure_model.onnx not present")
@pytest.mark.skipif(not _import_onnx(), reason="onnx package not installed")
class TestStructureModelContract:
    def test_model_loads_without_error(self) -> None:
        import onnx
        model = onnx.load(str(_STRUCTURE_ONNX))
        onnx.checker.check_model(model)

    def test_check_structure_passes(self) -> None:
        from validate_onnx_contract import check_structure
        check_structure(_STRUCTURE_ONNX)

    def test_output_Y_struct_is_float32(self) -> None:
        import onnx
        from onnx import TensorProto
        raw = onnx.load(str(_STRUCTURE_ONNX))
        y = next(vi for vi in raw.graph.output if vi.name == "Y_struct")
        assert y.type.tensor_type.elem_type == TensorProto.FLOAT

    def test_output_Y_struct_shape_is_1x3(self) -> None:
        import onnx
        from onnx import shape_inference
        raw = onnx.load(str(_STRUCTURE_ONNX))
        inferred = shape_inference.infer_shapes(raw)
        y = next(
            (vi for vi in list(inferred.graph.output) + list(inferred.graph.value_info)
             if vi.name == "Y_struct"),
            None,
        )
        assert y is not None, "Y_struct tensor not found in model"
        dims = [d.dim_value for d in y.type.tensor_type.shape.dim]
        assert len(dims) == 2
        assert dims[1] == 3 or dims[1] == 0

    def test_input_X_struct_shape_is_1x12x7(self) -> None:
        import onnx
        from onnx import shape_inference
        raw = onnx.load(str(_STRUCTURE_ONNX))
        inferred = shape_inference.infer_shapes(raw)
        x = next(vi for vi in inferred.graph.input if vi.name == "X_struct")
        dims = [d.dim_value for d in x.type.tensor_type.shape.dim]
        assert len(dims) == 3
        assert dims[1] == 12 or dims[1] == 0
        assert dims[2] == 7 or dims[2] == 0

    def test_input_X_struct_is_float32(self) -> None:
        import onnx
        from onnx import TensorProto
        raw = onnx.load(str(_STRUCTURE_ONNX))
        x = next(vi for vi in raw.graph.input if vi.name == "X_struct")
        assert x.type.tensor_type.elem_type == TensorProto.FLOAT

    def test_structure_normalization_is_baked(self) -> None:
        import numpy as np
        import onnx
        model = onnx.load(str(_STRUCTURE_ONNX))
        init_names = {init.name for init in model.graph.initializer}
        assert "mean" in init_names, "mean buffer not found in structure ONNX graph - normalization not baked"
        assert "std" in init_names, "std buffer not found in structure ONNX graph - normalization not baked"
        mean_init = next(i for i in model.graph.initializer if i.name == "mean")
        std_init = next(i for i in model.graph.initializer if i.name == "std")
        assert np.prod(list(mean_init.dims)) == 7
        assert np.prod(list(std_init.dims)) == 7


# ─── Bass model (v0.4.0: Y_bass [1,32]) ──────────────────────────────────────

@pytest.mark.skipif(not _BASS_ONNX.exists(), reason="assets/bass_model.onnx not present")
@pytest.mark.skipif(not _import_onnx(), reason="onnx package not installed")
class TestBassModelContract:
    def test_model_loads_without_error(self) -> None:
        import onnx
        model = onnx.load(str(_BASS_ONNX))
        onnx.checker.check_model(model)

    def test_check_bass_passes(self) -> None:
        from validate_onnx_contract import check_bass
        check_bass(_BASS_ONNX)

    def test_input_X_bass_feature_dim_is_7(self) -> None:
        import onnx
        from onnx import shape_inference
        raw = onnx.load(str(_BASS_ONNX))
        inferred = shape_inference.infer_shapes(raw)
        x = next(vi for vi in inferred.graph.input if vi.name == "X_bass")
        dims = [d.dim_value for d in x.type.tensor_type.shape.dim]
        assert len(dims) >= 2
        assert dims[1] == 7 or dims[1] == 0

    def test_output_Y_bass_shape_is_1x32(self) -> None:
        import onnx
        from onnx import shape_inference
        raw = onnx.load(str(_BASS_ONNX))
        inferred = shape_inference.infer_shapes(raw)
        y = next(
            (vi for vi in list(inferred.graph.output) + list(inferred.graph.value_info)
             if vi.name == "Y_bass"),
            None,
        )
        assert y is not None, "Y_bass tensor not found in model"
        dims = [d.dim_value for d in y.type.tensor_type.shape.dim]
        assert len(dims) == 2
        assert dims[1] == 32 or dims[1] == 0

    def test_output_Y_bass_is_float32(self) -> None:
        import onnx
        from onnx import TensorProto
        raw = onnx.load(str(_BASS_ONNX))
        y = next(vi for vi in raw.graph.output if vi.name == "Y_bass")
        assert y.type.tensor_type.elem_type == TensorProto.FLOAT


# ─── Negative test: legacy [1,4] bass contract ────────────────────────────────

@pytest.mark.skipif(not _import_onnx(), reason="onnx package not installed")
class TestLegacyBassRejection:
    """Prove that the v0.4.0 validator rejects legacy [1,4] bass artifacts."""

    def test_legacy_1x4_bass_is_rejected(self, tmp_path: Path) -> None:
        """Build a minimal [1,4] bass ONNX model and assert the validator fails."""
        import numpy as np
        import onnx
        from onnx import TensorProto, helper
        from validate_onnx_contract import check_bass

        # Build a minimal legacy [1,4] model matching the old contract
        x_info = helper.make_tensor_value_info("X_bass", TensorProto.FLOAT, [1, 7])
        y_info = helper.make_tensor_value_info("Y_bass", TensorProto.FLOAT, [1, 4])

        rng = np.random.default_rng(99)
        w = rng.standard_normal((7, 4)).astype(np.float32) * 0.1
        b = rng.standard_normal((4,)).astype(np.float32) * 0.05

        w_init = helper.make_tensor("W", TensorProto.FLOAT, [7, 4], w.flatten().astype(np.float32))
        b_init = helper.make_tensor("B", TensorProto.FLOAT, [4], b.astype(np.float32))
        init_w = helper.make_node("Constant", [], ["W_tensor"], value=w_init)
        init_b = helper.make_node("Constant", [], ["B_tensor"], value=b_init)

        matmul = helper.make_node("MatMul", ["X_bass", "W_tensor"], ["logits"])
        add = helper.make_node("Add", ["logits", "B_tensor"], ["Y_bass"])

        graph = helper.make_graph(
            [init_w, init_b, matmul, add],
            "legacy_bass_1x4",
            [x_info],
            [y_info],
        )
        model = helper.make_model(
            graph,
            producer_name="MetalAccompaniment",
            opset_imports=[helper.make_opsetid("", 17)],
        )
        onnx.checker.check_model(model)

        legacy_path = tmp_path / "legacy_bass_1x4.onnx"
        onnx.save(model, str(legacy_path))

        # Validator MUST reject this legacy shape
        with pytest.raises(SystemExit):
            check_bass(legacy_path)


# ─── Informational skip message when no models present ────────────────────────

@pytest.mark.skipif(_HAS_ONNX_MODELS, reason="ONNX models are present — skipping no-model placeholder")
def test_onnx_contract_skipped_when_no_models() -> None:
    """Passes trivially; serves as documentation that ONNX tests are gated."""
    pass
