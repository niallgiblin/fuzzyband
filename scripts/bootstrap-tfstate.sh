#!/usr/bin/env bash
# bootstrap-tfstate.sh — one-time S3 + DynamoDB for Terraform remote state (Phase 16).
#
# Requires: AWS CLI configured; env vars TFSTATE_BUCKET, TF_LOCK_TABLE, AWS_REGION.
# Usage (from repo root):
#   TFSTATE_BUCKET=my-unique-tfstate AWS_REGION=us-east-1 TF_LOCK_TABLE=fuzzyband-tf-locks ./scripts/bootstrap-tfstate.sh

set -euo pipefail

: "${AWS_REGION:?set AWS_REGION (e.g. us-east-1)}"
: "${TFSTATE_BUCKET:?set TFSTATE_BUCKET (globally unique S3 bucket name)}"
: "${TF_LOCK_TABLE:?set TF_LOCK_TABLE (DynamoDB table name for Terraform locks)}"

set +e
aws s3 mb "s3://${TFSTATE_BUCKET}" --region "${AWS_REGION}" 2>&1 | tee /tmp/fuzzyband-bootstrap-s3-mb.log
mb_code=$?
set -e
if [[ "${mb_code}" -ne 0 ]]; then
  if grep -qiE 'BucketAlreadyOwnedByYou|BucketAlreadyExists|already exists' /tmp/fuzzyband-bootstrap-s3-mb.log; then
    echo "bootstrap-tfstate: bucket already exists, continuing"
  else
    exit "${mb_code}"
  fi
fi

aws s3api put-bucket-versioning \
  --bucket "${TFSTATE_BUCKET}" \
  --versioning-configuration Status=Enabled \
  --region "${AWS_REGION}"

set +e
aws dynamodb create-table \
  --table-name "${TF_LOCK_TABLE}" \
  --attribute-definitions AttributeName=LockID,AttributeType=S \
  --key-schema AttributeName=LockID,KeyType=HASH \
  --billing-mode PAY_PER_REQUEST \
  --region "${AWS_REGION}" 2>&1 | tee /tmp/fuzzyband-bootstrap-ddb.log
ddb_code=$?
set -e
if [[ "${ddb_code}" -ne 0 ]]; then
  if grep -q ResourceInUseException /tmp/fuzzyband-bootstrap-ddb.log; then
    echo "bootstrap-tfstate: DynamoDB table already exists, continuing"
  else
    exit "${ddb_code}"
  fi
fi

echo "TFSTATE_BUCKET=${TFSTATE_BUCKET}"
echo "TF_LOCK_TABLE=${TF_LOCK_TABLE}"
echo "AWS_REGION=${AWS_REGION}"
echo "bootstrap-tfstate: OK — copy the values above into infra/backend.hcl (from backend.hcl.example)"
