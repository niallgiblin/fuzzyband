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

_HAS_ONNX_MODELS = _PATTERN_ONNX.exists() or _BASS_ONNX.exists()


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

    def test_input_feature_dim_is_5(self) -> None:
        import onnx
        from onnx import shape_inference
        raw = onnx.load(str(_PATTERN_ONNX))
        inferred = shape_inference.infer_shapes(raw)
        x = next(vi for vi in inferred.graph.input if vi.name == "X")
        dims = [d.dim_value for d in x.type.tensor_type.shape.dim]
        assert len(dims) >= 2
        assert dims[1] == 5 or dims[1] == 0  # 0 = dynamic

    def test_output_Y_is_int64(self) -> None:
        import onnx
        from onnx import TensorProto
        raw = onnx.load(str(_PATTERN_ONNX))
        y = next(vi for vi in raw.graph.output if vi.name == "Y")
        assert y.type.tensor_type.elem_type == TensorProto.INT64


# ─── Bass model ───────────────────────────────────────────────────────────────

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

    def test_output_Y_bass_shape_is_1x4(self) -> None:
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
        assert dims[1] == 4 or dims[1] == 0


# ─── Informational skip message when no models present ────────────────────────

@pytest.mark.skipif(_HAS_ONNX_MODELS, reason="ONNX models are present — skipping no-model placeholder")
def test_onnx_contract_skipped_when_no_models() -> None:
    """Passes trivially; serves as documentation that ONNX tests are gated."""
    pass
