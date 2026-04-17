output "model_bucket_id" {
  description = "S3 bucket id for ONNX model artifacts"
  value       = aws_s3_bucket.models.id
}

output "model_bucket_arn" {
  description = "S3 bucket ARN for model artifacts"
  value       = aws_s3_bucket.models.arn
}

output "ci_role_arn" {
  description = "IAM role ARN for CI read access (GitHub Actions OIDC)"
  value       = aws_iam_role.ci_model_read.arn
}

output "promote_role_arn" {
  description = "IAM role ARN for promotion uploads (GitHub Actions OIDC / maintainer)"
  value       = aws_iam_role.promote_model_write.arn
}

output "oidc_provider_arn" {
  description = "GitHub Actions OIDC provider ARN"
  value       = aws_iam_openid_connect_provider.github.arn
}
