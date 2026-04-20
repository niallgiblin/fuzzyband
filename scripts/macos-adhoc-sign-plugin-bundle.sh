#!/usr/bin/env bash
# Ad-hoc sign a JUCE .vst3 / .component / .app so Gatekeeper accepts it after local build + copy.
# When Contents/Frameworks/libonnxruntime*.dylib exists: re-sign real dylibs first, then sign the bundle with
# disable-library-validation (otherwise macOS shows "damaged" even when codesign --verify passes).
set -euo pipefail

bundle="${1:?Usage: $0 /path/to/Plugin.vst3}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENT_ONNX="${SCRIPT_DIR}/macos-plugin-onnx-embed.entitlements"

# Recursive clear (handles Finder flags / resource-fork metadata on Mach-O and dylibs).
xattr -cr "$bundle" 2>/dev/null || true
if [[ -d "$bundle/Contents" ]]; then
  find "$bundle/Contents" -type f 2>/dev/null | while IFS= read -r f; do
    xattr -c "$f" 2>/dev/null || true
  done
fi
xattr -cr "$bundle" 2>/dev/null || true

sign_ent=()
shopt -s nullglob
for fw in "$bundle/Contents/Frameworks"/libonnxruntime*.dylib; do
  [[ -e "$fw" ]] || continue
  [[ -L "$fw" ]] && continue
  codesign --remove-signature "$fw" 2>/dev/null || true
  codesign --force --sign - --timestamp=none "$fw"
  if [[ -f "$ENT_ONNX" ]]; then
    sign_ent=(--entitlements "$ENT_ONNX")
  fi
done
shopt -u nullglob

codesign --force --deep --sign - --timestamp=none "${sign_ent[@]}" "$bundle"
