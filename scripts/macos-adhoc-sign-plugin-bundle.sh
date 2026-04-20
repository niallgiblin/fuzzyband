#!/usr/bin/env bash
# Ad-hoc sign a JUCE .vst3 / .component / .app so Gatekeeper accepts it after local build + copy.
# When Contents/Frameworks/libonnxruntime*.dylib exists: re-sign real dylibs first, then sign the bundle with
# disable-library-validation (otherwise macOS shows "damaged" even when codesign --verify passes).
#
# If signing a .component on iCloud Desktop/Documents fails with "resource fork, Finder information…",
# use scripts/stage-and-sign-macos-plugins.sh (sign under $TMPDIR, then copy) instead of signing in DEST.
set -euo pipefail

bundle="${1:?Usage: $0 /path/to/Plugin.vst3}"

if [[ "$(uname -s)" != "Darwin" ]]; then
  exit 0
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ENT_ONNX="${SCRIPT_DIR}/macos-plugin-onnx-embed.entitlements"

# Recursive clear (Finder flags / iCloud Desktop file-provider attrs / quarantine).
# `cp -R` onto iCloud Desktop often adds attrs that `xattr -cr` does not fully remove;
# codesign then fails: "resource fork, Finder information, or similar detritus not allowed".
# Prefer: COPYFILE_DISABLE=1 cp -R -X src dst  and/or sign under /tmp then move once.
strip_all_xattrs_on_path() {
  local p="$1"
  local a
  xattr -l "$p" 2>/dev/null | sed -n 's/^\([^:]*\):.*/\1/p' | while IFS= read -r a; do
    [[ -n "$a" ]] || continue
    xattr -d "$a" "$p" 2>/dev/null || true
  done
}

strip_bundle_metadata() {
  local root="$1"
  find "$root" -name '.DS_Store' -delete 2>/dev/null || true
  find "$root" -name '._*' -delete 2>/dev/null || true
  find "$root" -print0 2>/dev/null | while IFS= read -r -d '' p; do
    strip_all_xattrs_on_path "$p"
  done
}

strip_bundle_metadata "$bundle"

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
