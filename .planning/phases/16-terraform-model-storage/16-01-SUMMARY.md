---
phase: 16-terraform-model-storage
plan: 01
subsystem: infra
tags: [terraform, aws, s3, oidc, github-actions]

requires:
  - phase: 10-onnx-runtime-iinference
    provides: BinaryData ONNX path unchanged
provides:
  - Versioned S3 bucket + GitHub OIDC IAM roles for CI read and promote write
  - Bootstrap script for remote tfstate (S3 + DynamoDB lock)
  - infra/README for bootstrap → init → apply
affects: [ci, release]

tech-stack:
  added: [Terraform AWS provider ~> 5.0]
  patterns: [S3 versioning + public access block; OIDC StringLike repo scope]

key-files:
  created:
    - scripts/bootstrap-tfstate.sh
    - infra/main.tf
    - infra/variables.tf
    - infra/outputs.tf
    - infra/terraform.tfvars.example
    - infra/backend.hcl.example
    - infra/README.md
    - infra/.terraform.lock.hcl
  modified:
    - .gitignore

key-decisions:
  - "GitHub OIDC trust uses StringLike repo:niallgiblin/fuzzyband:* per variable github_repository"
  - "Artifact bucket name pattern fuzzyband-prod-models via project_name + environment_name"

patterns-established:
  - "Remote state: bootstrap script before terraform init with backend.hcl"

requirements-completed: [CLOUD-01]

duration: —
completed: 2026-04-17
---

# Phase 16 plan 01: Terraform model storage (CLOUD-01)

**Versioned S3 model bucket with GitHub OIDC IAM roles, bootstrap script for tfstate, and apply documentation**

## Performance

- **Tasks:** 3
- **Files modified:** 9

## Accomplishments

- `scripts/bootstrap-tfstate.sh` creates versioned tfstate bucket and DynamoDB lock table (idempotent)
- Terraform defines OIDC provider, CI read role, promote write role, and versioned model bucket with public access blocked
- `infra/README.md` documents bootstrap → `backend.hcl` → `terraform init -backend-config=../backend.hcl` → apply

## Task Commits

1. **Task 1: bootstrap-tfstate.sh** — `feat(16-01): add bootstrap-tfstate script for S3 and DynamoDB lock`
2. **Task 2: Terraform** — `feat(16-01): add Terraform versioned model bucket and GitHub OIDC roles`
3. **Task 3: infra README** — `docs(16-01): add infra README for bootstrap, backend, and apply`

## Deviations from Plan

None — plan executed as written. Added `.terraform/` / `backend.hcl` to `.gitignore` for local Terraform hygiene.

## Issues Encountered

None.

---
*Phase: 16-terraform-model-storage · Plan 01 · 2026-04-17*
