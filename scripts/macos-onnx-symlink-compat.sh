#!/usr/bin/env bash
# Optional libonnxruntime.dylib → soname symlink when the loader looks for the versioned name.
set -euo pipefail

fw="${1:?Frameworks dir}"
soname="${2:?soname basename}"

rm -f "${fw}/libonnxruntime.dylib"
if [[ "${soname}" != "libonnxruntime.dylib" ]]; then
  ln -sf "${soname}" "${fw}/libonnxruntime.dylib"
fi
