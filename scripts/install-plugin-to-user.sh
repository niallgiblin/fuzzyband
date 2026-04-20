#!/usr/bin/env bash
# Copy freshly built Metal Accompaniment VST3 (and AU on macOS) into the user
# plug-in folders Reaper scans by default. Removes the destination bundle first
# so you never keep a half-updated .vst3/.component.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${REPO_ROOT}/build"
CONFIG="" # empty = auto (prefer newest Release/Debug artefact)

usage() {
  cat <<'EOF'
Usage: install-plugin-to-user.sh [--build DIR] [--config Release|Debug|auto]

  --build DIR   CMake build directory (default: <repo>/build)
  --config      Release or Debug; default auto picks the newest built VST3

Copies:
  VST3 → ~/Library/Audio/Plug-Ins/VST3/Metal Accompaniment.vst3
  AU   → ~/Library/Audio/Plug-Ins/Components/Metal Accompaniment.component (if built)

Then in Reaper: re-scan VST (or restart Reaper). Same install path → usually no
full cache clear needed after the first time.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --build)
      BUILD_DIR="$(cd "${2:?}" && pwd)"
      shift 2
      ;;
    --config)
      CONFIG="${2:?}"
      shift 2
      ;;
    *)
      echo "install-plugin-to-user: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

ART="${BUILD_DIR}/MetalAccompaniment_artefacts"
VST3_NAME="Metal Accompaniment.vst3"
AU_NAME="Metal Accompaniment.component"

bundle_main_mtime() {
  local bundle="$1"
  local f
  f="$(find "$bundle/Contents/MacOS" -type f 2>/dev/null | head -1)"
  if [[ -z "$f" ]]; then
    echo 0
    return
  fi
  stat -f %m "$f" 2>/dev/null || stat -c %Y "$f" 2>/dev/null || echo 0
}

pick_config_auto() {
  local r="${ART}/Release/VST3/${VST3_NAME}"
  local d="${ART}/Debug/VST3/${VST3_NAME}"
  if [[ ! -d "$r" && ! -d "$d" ]]; then
    echo "install-plugin-to-user: no VST3 bundle under ${ART}/{Release,Debug}/VST3/" >&2
    echo "Build first: cmake --build <build> --config Release  (or Debug)" >&2
    exit 1
  fi
  if [[ -d "$r" && ! -d "$d" ]]; then
    echo Release
    return
  fi
  if [[ ! -d "$r" && -d "$d" ]]; then
    echo Debug
    return
  fi
  local mr md
  mr="$(bundle_main_mtime "$r")"
  md="$(bundle_main_mtime "$d")"
  if [[ "$md" -gt "$mr" ]]; then
    echo Debug
  else
    echo Release
  fi
}

if [[ -z "$CONFIG" || "$CONFIG" == "auto" ]]; then
  CFG="$(pick_config_auto)"
else
  CFG="$CONFIG"
  VST3_SRC_CHECK="${ART}/${CFG}/VST3/${VST3_NAME}"
  if [[ ! -d "$VST3_SRC_CHECK" ]]; then
    echo "install-plugin-to-user: missing ${VST3_SRC_CHECK} (wrong --config or not built?)" >&2
    exit 1
  fi
fi

VST3_SRC="${ART}/${CFG}/VST3/${VST3_NAME}"
DEST_VST3="${HOME}/Library/Audio/Plug-Ins/VST3"
DEST_VST3_BUNDLE="${DEST_VST3}/${VST3_NAME}"

# macOS Gatekeeper: copied bundles are often quarantined and/or unsigned, which
# shows as “Metal Accompaniment is damaged” → Trash. Strip xattrs + ad-hoc sign.
# ONNX builds embed libonnxruntime.dylib — it must be re-signed before the bundle
# (see scripts/macos-adhoc-sign-plugin-bundle.sh).
macos_prepare_bundle() {
  local bundle="${1:?}"
  if [[ "$(uname -s)" != "Darwin" ]]; then
    return 0
  fi
  local sign_script="${REPO_ROOT}/scripts/macos-adhoc-sign-plugin-bundle.sh"
  if [[ -x "$sign_script" ]]; then
    "$sign_script" "$bundle"
  else
    xattr -cr "$bundle"
    codesign --force --deep --sign - --timestamp=none "$bundle"
  fi
}

mkdir -p "$DEST_VST3"
rm -rf "${DEST_VST3_BUNDLE}"
cp -R "$VST3_SRC" "$DEST_VST3/"
echo "install-plugin-to-user: VST3 (${CFG}) → ${DEST_VST3_BUNDLE}"
macos_prepare_bundle "$DEST_VST3_BUNDLE"
echo "install-plugin-to-user: VST3 ad-hoc signed + xattrs cleared (Gatekeeper)"

if [[ "$(uname -s)" == "Darwin" ]]; then
  AU_SRC="${ART}/${CFG}/AU/${AU_NAME}"
  if [[ -d "$AU_SRC" ]]; then
    DEST_AU="${HOME}/Library/Audio/Plug-Ins/Components"
    DEST_AU_BUNDLE="${DEST_AU}/${AU_NAME}"
    mkdir -p "$DEST_AU"
    rm -rf "${DEST_AU_BUNDLE}"
    cp -R "$AU_SRC" "$DEST_AU/"
    echo "install-plugin-to-user: AU (${CFG}) → ${DEST_AU_BUNDLE}"
    macos_prepare_bundle "$DEST_AU_BUNDLE"
    echo "install-plugin-to-user: AU ad-hoc signed + xattrs cleared (Gatekeeper)"
  else
    echo "install-plugin-to-user: no AU bundle at ${AU_SRC} (standalone-only build?)" >&2
  fi
fi

echo "install-plugin-to-user: OK — re-scan VST in Reaper or restart the app."

# Second copy under /Library/Audio/... is a common foot-gun: hosts that scan both
# user + system paths may load the older bundle (or one with a broken seal → “damaged”).
if [[ "$(uname -s)" == "Darwin" ]]; then
  sys_vst3="/Library/Audio/Plug-Ins/VST3/${VST3_NAME}"
  if [[ -d "$sys_vst3" ]]; then
    echo "install-plugin-to-user: WARNING — another VST3 exists at ${sys_vst3}" >&2
    echo "  If your DAW scans ~/Library and /Library, it may open that copy instead of the one just installed." >&2
    if ! codesign --verify "$sys_vst3" 2>/dev/null; then
      echo "  That system copy fails \`codesign --verify\` (often after editing files inside the bundle) → macOS / hosts report “damaged”." >&2
    fi
    echo "  Dev workflow: remove the system copy, or install only under ~/Library and drop /Library/... from VST paths:" >&2
    echo "    sudo rm -rf \"${sys_vst3}\"" >&2
  fi
  sys_au="/Library/Audio/Plug-Ins/Components/${AU_NAME}"
  if [[ -d "$sys_au" ]]; then
    echo "install-plugin-to-user: WARNING — another AU exists at ${sys_au}" >&2
    echo "  Same issue as VST3 if both locations are scanned. Remove with: sudo rm -rf \"${sys_au}\"" >&2
  fi
fi
