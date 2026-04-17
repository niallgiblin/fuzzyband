variable "aws_region" {
  type        = string
  description = "AWS region for all resources"
  default     = "us-east-1"
}

variable "project_name" {
  type        = string
  description = "Short project prefix for resource names"
  default     = "fuzzyband"
}

variable "github_repository" {
  type        = string
  description = "GitHub repo in org/name form (used in IAM OIDC trust subject)"
  default     = "niallgiblin/fuzzyband"
}

variable "environment_name" {
  type        = string
  description = "Environment suffix (e.g. prod) for resource names"
  default     = "prod"
}
