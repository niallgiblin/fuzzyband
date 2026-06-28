#!/usr/bin/env python3
"""Export PlayingStyleCNN to ONNX and verify (M009 S04).

Phase F of REAL_TIME_AUDIO_CLASSIFIER.md:
  - PyTorch → ONNX, opset 17
  - Input: (1, 1, 64, 32) float32
  - Output: (1, 5) float32 logits
  - Dynamic batch axis
  - Verify: checker, onnxruntime load, numerical match within 1e-5
"""

from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import torch

_REPO_ROOT = Path(__file__).resolve().parents[1]
_MODEL_DIR = _REPO_ROOT / "training" / "models"
_ASSETS_DIR = _REPO_ROOT / "assets"

sys.path.insert(0, str(_MODEL_DIR.parent))
from models.playing_style_cnn import PlayingStyleCNN  # noqa: E402


def export_onnx(model: torch.nn.Module, out_path: Path) -> None:
    """Export model to ONNX format."""
    model.eval()
    dummy = torch.randn(1, 1, 64, 32)

    torch.onnx.export(
        model,
        dummy,
        str(out_path),
        input_names=["input"],
        output_names=["logits"],
        dynamic_axes={
            "input": {0: "batch_size"},
            "logits": {0: "batch_size"},
        },
        opset_version=17,
    )
    print(f"Exported: {out_path} ({out_path.stat().st_size / 1024:.1f} KB)")


def verify_onnx(onnx_path: Path, pytorch_model: torch.nn.Module) -> bool:
    """Verify ONNX model: checker, load, numerical match."""
    import onnx  # noqa: PLC0415

    # 1. Checker
    try:
        proto = onnx.load(str(onnx_path))
        onnx.checker.check_model(proto)
        print("✓ onnx.checker passed")
    except Exception as e:
        print(f"✗ onnx.checker FAILED: {e}", file=sys.stderr)
        return False

    # 2. Load in onnxruntime
    import onnxruntime as ort  # noqa: PLC0415

    try:
        session = ort.InferenceSession(str(onnx_path))
        print(f"✓ onnxruntime loaded: {session.get_inputs()[0].name} → "
              f"{session.get_outputs()[0].name}")
    except Exception as e:
        print(f"✗ onnxruntime load FAILED: {e}", file=sys.stderr)
        return False

    # 3. Shape check
    dummy = np.random.randn(1, 1, 64, 32).astype(np.float32)
    onnx_out = session.run(None, {"input": dummy})
    print(f"  ONNX output shape: {onnx_out[0].shape} (expected: (1, 5))")

    if onnx_out[0].shape != (1, 5):
        print(f"✗ Shape mismatch: got {onnx_out[0].shape}", file=sys.stderr)
        return False

    # 4. Numerical comparison
    pytorch_model.eval()
    with torch.no_grad():
        torch_out = pytorch_model(torch.from_numpy(dummy)).numpy()

    max_diff = float(np.max(np.abs(torch_out - onnx_out[0])))
    print(f"  PyTorch vs ONNX max difference: {max_diff:.2e}")

    if max_diff > 1e-5:
        print(f"✗ Numerical mismatch: {max_diff:.2e} > 1e-5", file=sys.stderr)
        return False

    print("✓ Numerical match within 1e-5")

    # 5. Batch size variation
    dummy_batch = np.random.randn(4, 1, 64, 32).astype(np.float32)
    onnx_batch = session.run(None, {"input": dummy_batch})
    with torch.no_grad():
        torch_batch = pytorch_model(torch.from_numpy(dummy_batch)).numpy()
    batch_diff = float(np.max(np.abs(torch_batch - onnx_batch[0])))
    print(f"  Batch-4 match: max diff {batch_diff:.2e}")

    # 6. File size check
    file_size_mb = onnx_path.stat().st_size / (1024 * 1024)
    if file_size_mb > 5:
        print(f"✗ File size {file_size_mb:.1f} MB > 5 MB", file=sys.stderr)
        return False
    print(f"✓ File size: {file_size_mb:.1f} MB (limit: 5 MB)")

    return True


def main() -> int:
    model_path = _MODEL_DIR / "best_model.pt"
    if not model_path.exists():
        print(f"ERROR: model not found at {model_path}", file=sys.stderr)
        return 1

    model = PlayingStyleCNN(n_classes=5)
    model.load_state_dict(torch.load(model_path, map_location="cpu", weights_only=True))

    out_path = _ASSETS_DIR / "classifier.onnx"
    _ASSETS_DIR.mkdir(parents=True, exist_ok=True)

    print(f"Exporting {model_path} → {out_path}")
    export_onnx(model, out_path)

    print("\nVerifying ONNX model...")
    ok = verify_onnx(out_path, model)

    if ok:
        print(f"\n✓ ONNX model ready: {out_path}")
        return 0
    else:
        print(f"\n✗ ONNX verification failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
