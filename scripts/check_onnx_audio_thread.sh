#!/usr/bin/env bash
# check_onnx_audio_thread.sh — ONNX-03 threading guardrail
#
# Fails if ONNX Runtime C++ API (`onnxruntime_cxx_api` / `Ort::`) appears in the
# audio-thread sources (src/AccompanimentProcessor.{cpp,h}). ORT API must only
# appear under `src/inference/` (not in audio processor sources), so that
# Ort::Session::Run() cannot be invoked from processBlock on the real-time
# audio thread.
#
# Usage (run from repository root):
#   ./scripts/check_onnx_audio_thread.sh
#
# Exit codes:
#   0 — clean (no forbidden patterns found)
#   1 — forbidden pattern matched (prints offending file + line)

set -euo pipefail

FORBIDDEN='onnxruntime_cxx_api|Ort::'
TARGETS=(
  "src/AccompanimentProcessor.cpp"
  "src/AccompanimentProcessor.h"
)

fail=0
for f in "${TARGETS[@]}"; do
  if [[ ! -f "$f" ]]; then
    echo "check_onnx_audio_thread: target file missing: $f" >&2
    exit 1
  fi
  if grep -nE "$FORBIDDEN" "$f"; then
    echo "ERROR: forbidden ONNX Runtime symbol (pattern '$FORBIDDEN') found in $f" >&2
    echo "       ORT API must only appear under src/inference/ — not on the audio thread." >&2
    fail=1
  fi
done

if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "check_onnx_audio_thread: OK (no ORT symbols in AccompanimentProcessor sources)"
exit 0
