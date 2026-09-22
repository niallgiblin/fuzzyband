#!/usr/bin/env bash
#
# release-macos-universal.sh — turn two single-arch JUCE Release artefact dirs into one
# self-contained, ad-hoc-signed UNIVERSAL (arm64 + x86_64) macOS package, with ONNX
# Runtime embedded so the downloaded plugin needs nothing on the user's machine.
#
# Why this exists
# --------------
# ONNX Runtime ships separate arm64 and x86_64 dylibs (not fat), so JUCE cannot build a
# true universal plugin in one pass. We build once per arch, then `lipo` the two plugin
# Mach-Os and the two ONNX dylibs together. A raw JUCE build also links libonnxruntime
# by an ABSOLUTE path (the CI ONNXRUNTIME_ROOT) and embeds that as the dylib's load
# command, which would break the instant the user moves the bundle. This script, per arch:
#
#   1. rewrites the ONNX load command to @rpath/<soname>,
#   2. adds the @executable_path/../Frameworks rpath,
#   3. copies that arch's real ONNX dylib into Contents/Frameworks and sets its own id,
#   4. creates the unversioned compat symlink,
# then lipo-merges both archs into one fat bundle and ad-hoc signs it.
#
# The output is a *self-contained, unsigned* distribution bundle: no ONNXRUNTIME_ROOT,
# no brew, no extra dylibs, no code-signing certificate required.
#
# Usage
# -----
#   release-macos-universal.sh \
#     --arm-art  <Release artefacts dir built with -DCMAKE_OSX_ARCHITECTURES=arm64> \
#     --x64-art  <Release artefacts dir built with -DCMAKE_OSX_ARCHITECTURES=x86_64> \
#     --onnx-lib <ONNXRUNTIME_ROOT/lib dir for the arm64 build> \
#     --x64-onnx-lib <ONNXRUNTIME_ROOT/lib dir for the x86_64 build> \
#     --out      <staging dir to receive the universal bundle>
#
# The <Release artefacts dir> is the folder that CONTAINS VST3/, AU/, Standalone/
# (i.e. .../MetalAccompaniment_artefacts/Release). Only formats that exist on BOTH arch
# builds are emitted (e.g. AU is skipped if a build was produced without it).
#
# Produced layout under --out:
#   out/VST3/fuzzyband.vst3
#   out/AU/fuzzyband.component
#   out/Standalone/fuzzyband.app
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

usage() {
  sed -n '2,62p' "$0" | sed 's/^# \{0,1\}//'
}

ARM_ART=""
X64_ART=""
ONNX_LIB=""
X64_ONNX_LIB=""
OUT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --arm-art) ARM_ART="${2:?}"; shift 2 ;;
    --x64-art) X64_ART="${2:?}"; shift 2 ;;
    --onnx-lib) ONNX_LIB="${2:?}"; shift 2 ;;
    --x64-onnx-lib) X64_ONNX_LIB="${2:?}"; shift 2 ;;
    --out) OUT="${2:?}"; shift 2 ;;
    -h|--help) usage; exit 0 ;;
    *) echo "release-macos-universal: unknown option: $1" >&2; usage >&2; exit 1 ;;
  esac
done

[[ -n "$ARM_ART" && -n "$X64_ART" && -n "$ONNX_LIB" && -n "$OUT" ]] || {
  echo "release-macos-universal: --arm-art, --x64-art, --onnx-lib and --out are required (--x64-onnx-lib defaults to --onnx-lib)" >&2
  usage >&2
  exit 1
}
X64_ONNX_LIB="${X64_ONNX_LIB:-$ONNX_LIB}"
[[ -d "$ARM_ART" ]] || { echo "release-macos-universal: --arm-art not a dir: $ARM_ART" >&2; exit 1; }
[[ -d "$X64_ART" ]] || { echo "release-macos-universal: --x64-art not a dir: $X64_ART" >&2; exit 1; }
[[ -d "$ONNX_LIB" ]] || { echo "release-macos-universal: --onnx-lib not a dir: $ONNX_LIB" >&2; exit 1; }
[[ -d "$X64_ONNX_LIB" ]] || { echo "release-macos-universal: --x64-onnx-lib not a dir: $X64_ONNX_LIB" >&2; exit 1; }

# Resolve the real (non-symlink) ONNX dylib inside a lib dir, e.g. libonnxruntime.1.20.1.dylib.
real_onnx_dylib() {
  local dir="${1:?}"
  for f in "$dir"/libonnxruntime*.dylib; do
    [[ -e "$f" ]] || continue
    [[ -L "$f" ]] && continue
    echo "$f"
    return 0
  done
  return 1
}

ARM_SRC="$(real_onnx_dylib "$ONNX_LIB" || true)"
X64_SRC="$(real_onnx_dylib "$X64_ONNX_LIB" || true)"
[[ -n "$ARM_SRC" ]] || { echo "release-macos-universal: no libonnxruntime*.dylib in $ONNX_LIB" >&2; exit 1; }
[[ -n "$X64_SRC" ]] || { echo "release-macos-universal: no libonnxruntime*.dylib in $X64_ONNX_LIB" >&2; exit 1; }
SONAME="$(basename "$ARM_SRC")"          # e.g. libonnxruntime.1.20.1.dylib
[[ "$SONAME" == "$(basename "$X64_SRC")" ]] || {
  echo "release-macos-universal: ONNX soname differs between arch builds ($SONAME vs $(basename "$X64_SRC"))" >&2
  exit 1;
}
echo "release-macos-universal: ONNX soname = $SONAME"

# The product name inside every JUCE bundle binary (matches PRODUCT_NAME in CMakeLists).
PRODUCT="fuzzyband"

# Map a JUCE format dir to its bundle name. Deliberately a `case` rather than a
# `declare -A` associative array: the macOS GitHub runner (and stock macOS) ships
# bash 3.2, which lacks associative arrays, and under `set -u` a `[VST3]` subscript
# is read as an unset variable and aborts. See the release workflow's Package step.
format_bundle_name() {
  case "$1" in
    VST3)       echo "${PRODUCT}.vst3" ;;
    AU)         echo "${PRODUCT}.component" ;;
    Standalone) echo "${PRODUCT}.app" ;;
    *)          echo "release-macos-universal: unknown format: $1" >&2; return 1 ;;
  esac
}

# ── Per-arch self-containment (steps 1-4) ───────────────────────────────────────
selfcontain() {
  local bundle="${1:?bundle}"
  local onnx_src="${2:?onnx dylib source}"
  local bin
  bin="$(find "$bundle/Contents/MacOS" -maxdepth 1 -type f 2>/dev/null | head -1 || true)"
  [[ -n "$bin" && -f "$bin" ]] || { echo "  - no Mach-O in $bundle (skipping)"; return 0; }

  local dep
  dep="$(otool -L "$bin" 2>/dev/null | awk '/onnx/ {print $1; exit}')"
  if [[ -z "$dep" ]]; then
    echo "  - no ONNX load command in $bin (not an ONNX build?)"
    return 0
  fi
  local soname="$(basename "$dep")"      # same basename on both archs

  local frame="$bundle/Contents/Frameworks"
  mkdir -p "$frame"

  # Rewrite the absolute ONNX path -> @rpath/<soname> and add the Frameworks rpath.
  # The signature-invalidation warning is expected; we re-sign the finished universal
  # bundle at the end.
  install_name_tool -change "$dep" "@rpath/$soname" "$bin" 2>/dev/null || true
  install_name_tool -add_rpath "@executable_path/../Frameworks" "$bin" 2>/dev/null || true

  # Embed this arch's ONNX dylib and give it an @rpath id so nested deps resolve.
  cp -f "$onnx_src" "$frame/$soname"
  install_name_tool -id "@rpath/$soname" "$frame/$soname" 2>/dev/null || true

  # Compat: some loader paths want the unversioned name.
  rm -f "$frame/libonnxruntime.dylib"
  ln -sf "$soname" "$frame/libonnxruntime.dylib"

  echo "  - self-contained $bundle (onnx -> @rpath/$soname)"
}

# ── Merge + sign (steps 5-6) ────────────────────────────────────────────────────
merge_universal() {
  local fmt="${1:?fmt}"
  local name="${2:?name}"
  local arm_bundle="$ARM_ART/$fmt/$name"
  local x64_bundle="$X64_ART/$fmt/$name"

  if [[ ! -d "$arm_bundle" && ! -d "$x64_bundle" ]]; then
    echo "release-macos-universal: skipping $fmt (not built)" >&2
    return 0
  fi
  if [[ ! -d "$arm_bundle" || ! -d "$x64_bundle" ]]; then
    echo "release-macos-universal: $fmt missing on one arch — cannot make universal" >&2
    return 0
  fi

  echo "release-macos-universal: merging $fmt ..."
  selfcontain "$arm_bundle" "$ARM_SRC"
  selfcontain "$x64_bundle" "$X64_SRC"

  local out_bundle="$OUT/$fmt/$name"
  rm -rf "$out_bundle"
  mkdir -p "$(dirname "$out_bundle")"
  cp -R "$arm_bundle" "$out_bundle"

  lipo -create \
    "$arm_bundle/Contents/MacOS/$PRODUCT" \
    "$x64_bundle/Contents/MacOS/$PRODUCT" \
    -output "$out_bundle/Contents/MacOS/$PRODUCT"

  # Merge the embedded ONNX dylibs into a single fat one.
  local arm_onnx="$arm_bundle/Contents/Frameworks/$SONAME"
  local x64_onnx="$x64_bundle/Contents/Frameworks/$SONAME"
  if [[ -f "$arm_onnx" && -f "$x64_onnx" ]]; then
    lipo -create "$arm_onnx" "$x64_onnx" -output "$out_bundle/Contents/Frameworks/$SONAME"
    rm -f "$out_bundle/Contents/Frameworks/libonnxruntime.dylib"
    ln -sf "$SONAME" "$out_bundle/Contents/Frameworks/libonnxruntime.dylib"
  fi

  if [[ "$(uname -s)" == "Darwin" ]]; then
    "$SCRIPT_DIR/macos-adhoc-sign-plugin-bundle.sh" "$out_bundle"
  fi
  echo "  - universal: $out_bundle"
}

rm -rf "$OUT"
mkdir -p "$OUT"

for fmt in VST3 AU Standalone; do
  merge_universal "$fmt" "$(format_bundle_name "$fmt")"
done

# Guard: we must actually have produced something (an empty package is a silent failure).
if ! find "$OUT" -mindepth 2 -maxdepth 3 -name "$PRODUCT.*" -type d | grep -q .; then
  echo "release-macos-universal: no universal bundles produced (check --arm-art/--x64-art paths)" >&2
  exit 1
fi

echo "release-macos-universal: done -> $OUT"
