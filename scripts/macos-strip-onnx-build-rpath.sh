#!/usr/bin/env bash
# Remove the absolute LC_RPATH to ONNXRUNTIME_ROOT/lib from a freshly linked bundle.
# Linking against libonnxruntime.dylib by full path injects that rpath; the loader then
# resolves @rpath/libonnxruntime.*.dylib from Downloads (unsigned) instead of the
# bundled copy → Gatekeeper "could not verify … malware" on the ONNX dylib.
set -euo pipefail

bundle_root="${1:?bundle root}"
lib_dir="${2:?ONNX lib dir to remove from rpath}"

macos_exe=""
if [[ -d "${bundle_root}/Contents/MacOS" ]]; then
  # One Mach-O per JUCE plugin / standalone app bundle.
  while IFS= read -r -d '' f; do
    macos_exe="$f"
    break
  done < <(find "${bundle_root}/Contents/MacOS" -maxdepth 1 -type f -print0 2>/dev/null)
fi

if [[ -z "$macos_exe" || ! -f "$macos_exe" ]]; then
  exit 0
fi

install_name_tool -delete_rpath "${lib_dir}" "${macos_exe}" 2>/dev/null || true
