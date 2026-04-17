---
phase: 16-terraform-model-storage
plan: 02
subsystem: infra
tags: [bash, s3, github-actions, oidc]

requires:
  - phase: 16-terraform-model-storage
    provides: model bucket id and ci_role_arn for wiring
provides:
  - promote-model.sh and download-model.sh for current.json workflow
  - Optional CI steps using OIDC + vars/secrets
  - README promotion runbook section
affects: [ci, release]

tech-stack:
  added: []
  patterns: [pointer manifest current.json with sha256 verification]

key-files:
  created:
    - scripts/promote-model.sh
    - scripts/download-model.sh
  modified:
    - infra/README.md
    - .github/workflows/ci.yml

key-decisions:
  - "download-model.sh exits 0 with SKIP when MODEL_BUCKET unset or current.json missing"
  - "CI runs configure-aws-credentials + download only when fork=false and vars/secrets set"

patterns-established:
  - "Pre-build model fetch into assets/accompaniment_model.onnx before CMake"

requirements-completed: [CLOUD-02]

duration: —
completed: 2026-04-17
---

# Phase 16 plan 02: Promotion runbook and CI (CLOUD-02)

**SHA256-validated promote script, graceful CI download via OIDC, and README runbook for tag → checksum → path**

## Performance

- **Tasks:** 4
- **Files modified:** 4

## Accomplishments

- `scripts/promote-model.sh` uploads `models/<run-id>/model.onnx` and overwrites `current.json`
- `scripts/download-model.sh` pulls `current.json` and object into `assets/accompaniment_model.onnx` with hash check; skips cleanly when cloud is unavailable
- `ci.yml` adds `permissions` for OIDC and optional AWS + download steps before the existing build

## Task Commits

1. **promote + download scripts** — `feat(16-02): add promote-model and download-model scripts`
2. **CI wiring** — `ci(16-02): optional OIDC and S3 model download before build`
3. **README** — promotion section included in `infra/README.md` (same wave as 16-01 doc commit in repo history; content satisfies CLOUD-02)

## Deviations from Plan

None.

## Issues Encountered

None.

---
*Phase: 16-terraform-model-storage · Plan 02 · 2026-04-17*
