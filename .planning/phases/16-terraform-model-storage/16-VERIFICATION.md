---
status: passed
phase: 16-terraform-model-storage
verified: 2026-04-17
---

# Phase 16 verification — Terraform model storage

## Must-haves

| ID | Check | Evidence |
|----|--------|----------|
| CLOUD-01 | `terraform apply` creates versioned bucket + IAM OIDC roles | `infra/main.tf`: `aws_s3_bucket_versioning` Enabled; `aws_s3_bucket_public_access_block` all blocks true; `aws_iam_openid_connect_provider` + `AssumeRoleWithWebIdentity` roles scoped to `repo:${var.github_repository}:*`; `scripts/bootstrap-tfstate.sh` + `infra/README.md` |
| CLOUD-02 | Runbook: tag/run id → checksum → path for release/build | `scripts/promote-model.sh` writes `current.json` with `path` + `sha256`; `scripts/download-model.sh` verifies SHA256 to `assets/accompaniment_model.onnx`; `infra/README.md` **Promote a model**; `.github/workflows/ci.yml` optional OIDC + download |

## Automated (local)

- `terraform fmt -check infra/` — pass
- `terraform init -backend=false` + `terraform validate` — pass
- `MODEL_BUCKET="" ./scripts/download-model.sh` — exit 0, prints SKIP
- `./scripts/check_onnx_audio_thread.sh` — exit 0 (unchanged)

## Human

- **AWS apply:** Run `terraform apply` in a real account to confirm bucket versioning (`aws s3api get-bucket-versioning`) and IAM — not executed in this environment.

## Notes

- `gsd-sdk` was unavailable; phase executed inline with manual planning-doc updates.
- Shellcheck not installed locally; scripts follow `set -euo pipefail` patterns from plan.
