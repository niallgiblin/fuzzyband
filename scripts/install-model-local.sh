#!/usr/bin/env bash
# install-model-local.sh — validate-then-copy Phase 18/19 trained ONNX into assets/ (EXP-01).
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: install-model-local.sh [options]

  Validate pattern + bass ONNX with scripts/validate_onnx_contract.py, then copy to:
    assets/accompaniment_model.onnx  (from pattern_trained.onnx or --pattern-onnx)
    assets/bass_model.onnx           (from bass_trained.onnx or --bass-onnx)

Options:
  --pattern-dir DIR   Directory containing pattern_trained.onnx (unless --pattern-onnx)
  --bass-dir DIR      Directory containing bass_trained.onnx (unless --bass-onnx)
  --pattern-onnx PATH Explicit pattern ONNX path
  --bass-onnx PATH    Explicit bass ONNX path
  --copy-stats        If norm_stats.json / bass_norm_stats.json sit next to sources, copy to assets/
  -h, --help          Show this help

Example:
  ./scripts/install-model-local.sh --pattern-dir /path/to/pattern/run --bass-dir /path/to/bass/run
EOF
}

PATTERN_DIR=""
BASS_DIR=""
PATTERN_ONNX=""
BASS_ONNX=""
COPY_STATS=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    --pattern-dir)
      PATTERN_DIR="${2:?}"
      shift 2
      ;;
    --bass-dir)
      BASS_DIR="${2:?}"
      shift 2
      ;;
    --pattern-onnx)
      PATTERN_ONNX="${2:?}"
      shift 2
      ;;
    --bass-onnx)
      BASS_ONNX="${2:?}"
      shift 2
      ;;
    --copy-stats)
      COPY_STATS=1
      shift
      ;;
    *)
      echo "install-model-local: unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -n "$PATTERN_ONNX" ]]; then
  PATTERN_RESOLVED="$PATTERN_ONNX"
else
  if [[ -z "$PATTERN_DIR" ]]; then
    echo "install-model-local: need --pattern-dir or --pattern-onnx" >&2
    exit 1
  fi
  PATTERN_RESOLVED="${PATTERN_DIR}/pattern_trained.onnx"
fi

if [[ -n "$BASS_ONNX" ]]; then
  BASS_RESOLVED="$BASS_ONNX"
else
  if [[ -z "$BASS_DIR" ]]; then
    echo "install-model-local: need --bass-dir or --bass-onnx" >&2
    exit 1
  fi
  BASS_RESOLVED="${BASS_DIR}/bass_trained.onnx"
fi

if [[ ! -f "$PATTERN_RESOLVED" ]]; then
  echo "install-model-local: pattern ONNX not found: ${PATTERN_RESOLVED}" >&2
  exit 1
fi
if [[ ! -f "$BASS_RESOLVED" ]]; then
  echo "install-model-local: bass ONNX not found: ${BASS_RESOLVED}" >&2
  exit 1
fi

if ! python3 "${REPO_ROOT}/scripts/validate_onnx_contract.py" --pattern "$PATTERN_RESOLVED" --bass "$BASS_RESOLVED"; then
  echo "install-model-local: validate_onnx_contract.py failed — not copying" >&2
  exit 1
fi

mkdir -p "${REPO_ROOT}/assets"
if ! cmp -s "$PATTERN_RESOLVED" "${REPO_ROOT}/assets/accompaniment_model.onnx"; then
  cp -f "$PATTERN_RESOLVED" "${REPO_ROOT}/assets/accompaniment_model.onnx"
fi
if ! cmp -s "$BASS_RESOLVED" "${REPO_ROOT}/assets/bass_model.onnx"; then
  cp -f "$BASS_RESOLVED" "${REPO_ROOT}/assets/bass_model.onnx"
fi

if [[ "$COPY_STATS" -eq 1 ]]; then
  _pat_dir="$(cd "$(dirname "$PATTERN_RESOLVED")" && pwd)"
  if [[ -f "${_pat_dir}/norm_stats.json" ]]; then
    cp -f "${_pat_dir}/norm_stats.json" "${REPO_ROOT}/assets/norm_stats.json"
  fi
  _bas_dir="$(cd "$(dirname "$BASS_RESOLVED")" && pwd)"
  if [[ -f "${_bas_dir}/bass_norm_stats.json" ]]; then
    cp -f "${_bas_dir}/bass_norm_stats.json" "${REPO_ROOT}/assets/bass_norm_stats.json"
  fi
fi

echo "install-model-local: OK"
echo "  pattern source: ${PATTERN_RESOLVED}"
echo "  bass source:    ${BASS_RESOLVED}"
echo "  installed:      ${REPO_ROOT}/assets/accompaniment_model.onnx"
echo "  installed:      ${REPO_ROOT}/assets/bass_model.onnx"
echo "  validation:     scripts/validate_onnx_contract.py passed (pattern + bass)"
