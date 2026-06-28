"""GrooveModel — MelCNN backbone + 22-way pattern classifier + embedding head.

Matches SIMPLIFY.md §2.1 architecture:
  Conv backbone: (1, 64, 32) mel → bottleneck (128,)
  Classifier: 128 → 22 (train-time only)
  Embedding head: 128 → 64 L2-norm (for export + nearest-neighbor lookup)
"""

from __future__ import annotations

import torch
import torch.nn as nn
import torch.nn.functional as F


class MelCNNBackbone(nn.Module):
    """Conv backbone: (1, 64, 32) → bottleneck (128,)."""

    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            # Block 1: (1, 64, 32) → (16, 32, 16)
            nn.Conv2d(1, 16, kernel_size=3, padding=1),
            nn.BatchNorm2d(16),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            # Block 2: (16, 32, 16) → (32, 16, 8)
            nn.Conv2d(16, 32, kernel_size=3, padding=1),
            nn.BatchNorm2d(32),
            nn.ReLU(inplace=True),
            nn.MaxPool2d(2, 2),

            # Block 3: (32, 16, 8) → (64, 4, 4)
            nn.Conv2d(32, 64, kernel_size=3, padding=1),
            nn.BatchNorm2d(64),
            nn.ReLU(inplace=True),
            nn.AdaptiveAvgPool2d((4, 4)),
        )
        self.flatten = nn.Flatten()
        self.bottleneck = nn.Linear(64 * 4 * 4, 128)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: (B, 1, 64, 32) → (B, 128)"""
        return self.bottleneck(self.flatten(self.conv(x)))


class GrooveClassifier(nn.Module):
    """22-way pattern classifier on top of MelCNN backbone."""

    def __init__(self, n_classes: int = 22, dropout: float = 0.3):
        super().__init__()
        self.backbone = MelCNNBackbone()
        self.classifier = nn.Sequential(
            nn.Dropout(dropout),
            nn.Linear(128, n_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x: (B, 1, 64, 32) → (B, 22) logits"""
        return self.classifier(self.backbone(x))

    def extract_bottleneck(self, x: torch.Tensor) -> torch.Tensor:
        """Extract 128-dim bottleneck before classifier — for centroid computation."""
        return self.backbone(x)


class GrooveEmbeddingHead(nn.Module):
    """128 → 64 → L2-normalize. Cosine similarity to precomputed pattern embeddings."""

    def __init__(self, input_dim: int = 128, embedding_dim: int = 64):
        super().__init__()
        self.fc = nn.Linear(input_dim, embedding_dim)

    def forward(self, bottleneck: torch.Tensor) -> torch.Tensor:
        """bottleneck: (B, 128) → (B, 64) L2-normalized"""
        x = self.fc(bottleneck)
        return F.normalize(x, p=2, dim=1)


class GrooveModelForExport(nn.Module):
    """Full export model: backbone + embedding head + style head.

    This is what gets exported to ONNX as metal_groove.onnx.
    Input:  mel [1, 1, 64, 32]
    Output: groove_embedding [1, 64], style_logits [1, 5]
    """

    def __init__(self, n_classes: int = 22, embedding_dim: int = 64, n_styles: int = 5):
        super().__init__()
        self.backbone = MelCNNBackbone()
        self.embedding = GrooveEmbeddingHead(128, embedding_dim)
        self.style_head = nn.Linear(128, n_styles)

    def forward(self, x: torch.Tensor):
        """x: (B, 1, 64, 32) → (groove_embedding, style_logits)"""
        bottleneck = self.backbone(x)
        groove_emb = self.embedding(bottleneck)
        style_logits = self.style_head(bottleneck)
        return groove_emb, style_logits
