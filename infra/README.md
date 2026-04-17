# Infrastructure — Terraform (Phase 16)

Terraform provisions **versioned** S3 storage for ONNX model artifacts, **GitHub OIDC** IAM roles (read for CI, write for promotion), and related IAM. The plugin does **not** call AWS at runtime; CI may download a model into `assets/` before CMake.

## Prerequisites

- [AWS CLI](https://aws.amazon.com/cli/) configured with credentials able to create S3, DynamoDB, and IAM in your account
- [Terraform](https://www.terraform.io/) **>= 1.5**
- Permissions to create S3 buckets, DynamoDB tables, IAM roles/policies, and the OIDC identity provider

**Cost / scope:** One small S3 bucket (versioning on), minimal IAM, one DynamoDB table for state locking — no plugin network usage.

## Step 1 — Bootstrap remote state (one-time)

From the repository root, set a **globally unique** state bucket name and a lock table name, then run:

```bash
TFSTATE_BUCKET=your-unique-tfstate-bucket-name \
TF_LOCK_TABLE=your-tf-lock-table \
AWS_REGION=us-east-1 \
  ./scripts/bootstrap-tfstate.sh
```

The script creates (or no-ops if present) the S3 bucket with **versioning enabled** and a DynamoDB table for Terraform state locking. It prints:

- `TFSTATE_BUCKET=...`
- `TF_LOCK_TABLE=...`
- `AWS_REGION=...`

Use these in `backend.hcl` (next step).

## Step 2 — Configure the Terraform backend

Copy the example backend file and fill placeholders:

```bash
cp infra/backend.hcl.example backend.hcl
# Edit backend.hcl: set bucket, dynamodb_table, region from bootstrap output
```

## Step 3 — Initialize and apply

From the repository root:

```bash
cd infra
terraform init -backend-config=../backend.hcl
terraform plan
terraform apply
```

To use variable defaults only:

```bash
terraform apply
```

Optional: copy `terraform.tfvars.example` to `terraform.tfvars` and customize (do not commit secrets).

## Step 4 — Wire outputs into GitHub

After `terraform apply`, copy outputs into GitHub **Variables** / **Secrets** for CI and promotion scripts:

| Terraform output      | Suggested GitHub name   | Notes |
|-----------------------|-------------------------|--------|
| `model_bucket_id`     | `AWS_MODEL_BUCKET`      | Repository **Variable** — bucket id for `download-model.sh` |
| `ci_role_arn`         | `AWS_CI_ROLE_ARN`       | Repository **Secret** — OIDC role for CI read |
| `promote_role_arn`    | (local / optional)      | Use when assuming the promote role for `./scripts/promote-model.sh` |

Exact CI wiring is documented in **## Promote a model (CLOUD-02)** below and in `.github/workflows/ci.yml`.

## Post-apply verification (optional)

Confirm model bucket versioning:

```bash
aws s3api get-bucket-versioning --bucket "$(terraform output -raw model_bucket_id)"
```

Expect `Status` = `Enabled`.

## Promote a model (CLOUD-02)

Promotion uses a **pointer file** `s3://<bucket>/current.json` with JSON shape:

```json
{"path":"models/<run-id>/model.onnx","sha256":"<64-char lowercase hex>"}
```

Updating `current.json` is the promotion act; the bucket is versioned so prior objects remain addressable.

### Prerequisites

- AWS CLI installed
- Credentials that can **assume** the Terraform output **`promote_role_arn`** (via `aws configure sso`, environment variables, or `aws sts assume-role` — not covered in detail here)

### Promote a build

From the repository root:

```bash
export MODEL_BUCKET="<model_bucket_id>"   # terraform output -raw model_bucket_id
export AWS_REGION=us-east-1                # optional; default in scripts is us-east-1
./scripts/promote-model.sh path/to/your/model.onnx YOUR_RUN_ID
```

`YOUR_RUN_ID` should be a filesystem-safe token (e.g. short git SHA or dated tag). The script uploads to `models/<run-id>/model.onnx`, writes `current.json`, and prints `PROMOTE_OK` plus the `s3://` URI of `current.json`.

### CI consumption

GitHub Actions can run **`./scripts/download-model.sh`** before CMake so **`assets/accompaniment_model.onnx`** matches `current.json`. Configure:

| Kind    | Name               | Value (from Terraform)   |
|---------|--------------------|----------------------------|
| Variable | `AWS_MODEL_BUCKET` | `model_bucket_id` output |
| Secret   | `AWS_CI_ROLE_ARN`  | `ci_role_arn` output     |

If either is missing or the repo is a **fork**, the workflow **skips** download and the build uses the checked-in placeholder under `assets/` (see [`docs/ONNX_IO.md`](../docs/ONNX_IO.md) and Phase 10 BinaryData behavior).

The plugin’s runtime load path is unchanged: JUCE **BinaryData** bundles whatever ONNX is present at build time.
