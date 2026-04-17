#!/usr/bin/env bash
# download-model.sh — fetch current.json + model into assets/ for CI / local builds (CLOUD-02).
#
# Skip with exit 0 when MODEL_BUCKET is unset or current.json is unreachable.
# Uses default AWS credential chain (OIDC in GitHub Actions, SSO/keys locally).

set -euo pipefail

AWS_REGION="${AWS_REGION:-us-east-1}"

if [[ -z "${MODEL_BUCKET:-}" ]]; then
  echo "download-model: SKIP (MODEL_BUCKET unset)"
  exit 0
fi

if ! command -v aws >/dev/null 2>&1; then
  echo "download-model: SKIP (aws CLI not found)" >&2
  exit 0
fi

TMP_JSON=$(mktemp)
if ! aws s3 cp "s3://${MODEL_BUCKET}/current.json" "${TMP_JSON}" --region "${AWS_REGION}" 2>/dev/null; then
  echo "download-model: SKIP (no current.json or access denied)"
  rm -f "${TMP_JSON}"
  exit 0
fi

OBJECT_KEY=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1],encoding="utf-8")); print(d.get("path") or "")' "${TMP_JSON}")
SHA_EXPECT=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1],encoding="utf-8")); print(d.get("sha256") or "")' "${TMP_JSON}")
rm -f "${TMP_JSON}"

if [[ -z "${OBJECT_KEY}" || -z "${SHA_EXPECT}" ]]; then
  echo "download-model: SKIP (invalid current.json shape)" >&2
  exit 0
fi

mkdir -p assets
TMP_ONNX=$(mktemp)
if ! aws s3 cp "s3://${MODEL_BUCKET}/${OBJECT_KEY}" "${TMP_ONNX}" --region "${AWS_REGION}"; then
  echo "download-model: failed to download model object" >&2
  rm -f "${TMP_ONNX}"
  exit 1
fi

SHA_GOT=$(shasum -a 256 "${TMP_ONNX}" | awk '{print $1}' | tr '[:upper:]' '[:lower:]')
if [[ "${SHA_GOT}" != "${SHA_EXPECT}" ]]; then
  echo "download-model: SHA256 mismatch (expected ${SHA_EXPECT}, got ${SHA_GOT})" >&2
  rm -f "${TMP_ONNX}"
  exit 1
fi

mv "${TMP_ONNX}" assets/accompaniment_model.onnx
echo "download-model: DOWNLOAD_OK"
