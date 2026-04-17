#!/usr/bin/env bash
# promote-model.sh — upload ONNX to versioned S3 path and update current.json (CLOUD-02).
#
# Usage (from repo root):
#   export MODEL_BUCKET=<model_bucket_id from terraform output>
#   ./scripts/promote-model.sh path/to/model.onnx RUN_ID
#
# RUN_ID is a filesystem-safe token (e.g. git short SHA or date tag).

set -euo pipefail

usage() {
  echo "Usage: $0 PATH_TO_LOCAL_ONNX RUN_ID" >&2
  exit 1
}

[[ "$#" -eq 2 ]] || usage

LOCAL="${1:?}"
RUN_ID="${2:?}"

: "${MODEL_BUCKET:?MODEL_BUCKET must be set to the S3 bucket id (terraform output model_bucket_id)}"

AWS_REGION="${AWS_REGION:-us-east-1}"

if [[ ! -f "$LOCAL" ]]; then
  echo "promote-model: file not found: $LOCAL" >&2
  exit 1
fi

if ! command -v aws >/dev/null 2>&1; then
  echo "promote-model: aws CLI not found" >&2
  exit 1
fi

SHA_HEX=$(shasum -a 256 "$LOCAL" | awk '{print $1}' | tr '[:upper:]' '[:lower:]')
OBJECT_KEY="models/${RUN_ID}/model.onnx"

aws s3 cp "$LOCAL" "s3://${MODEL_BUCKET}/${OBJECT_KEY}" --region "${AWS_REGION}"

TMP_JSON=$(mktemp)
OBJECT_KEY="${OBJECT_KEY}" SHA_HEX="${SHA_HEX}" python3 -c '
import json, os
path = os.environ["OBJECT_KEY"]
sha = os.environ["SHA_HEX"]
print(json.dumps({"path": path, "sha256": sha}, separators=(",", ":")))
' > "${TMP_JSON}"

aws s3 cp "${TMP_JSON}" "s3://${MODEL_BUCKET}/current.json" --region "${AWS_REGION}" --content-type application/json

rm -f "${TMP_JSON}"

echo "PROMOTE_OK"
echo "s3://${MODEL_BUCKET}/current.json"
