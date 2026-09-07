#!/usr/bin/env bash
# Sign VST3 + AU in a temp dir, then copy to DEST. Use this when DEST lives on iCloud
# Desktop/Documents: codesign rejects .component there ("resource fork, Finder information…")
# because file-provider metadata is re-applied while signing. VST3 may still sign in place;
# staging avoids flaky AU failures.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ART="${ART:-"$REPO_ROOT/build/MetalAccompaniment_artefacts/Release"}"
DEST="${1:?Usage: ART=/path/to/Release $0 /path/to/output-dir}"

STAGE="$(mktemp -d "${TMPDIR:-/tmp}/metal-accompaniment-sign.XXXXXX")"
cleanup() { rm -rf "$STAGE"; }
trap cleanup EXIT

cp -R "$ART/VST3/fuzzyband.vst3" "$STAGE/"
cp -R "$ART/AU/fuzzyband.component" "$STAGE/"

"$REPO_ROOT/scripts/macos-adhoc-sign-plugin-bundle.sh" "$STAGE/fuzzyband.vst3"
"$REPO_ROOT/scripts/macos-adhoc-sign-plugin-bundle.sh" "$STAGE/fuzzyband.component"

mkdir -p "$DEST"
rm -rf "$DEST/fuzzyband.vst3" "$DEST/fuzzyband.component"
cp -R "$STAGE/fuzzyband.vst3" "$STAGE/fuzzyband.component" "$DEST/"

echo "Signed plug-ins copied to: $DEST"
