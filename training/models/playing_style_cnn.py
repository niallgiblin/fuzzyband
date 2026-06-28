"""PlayingStyleCNN — small CNN for guitar playing-style classification (M009).

Architecture from REAL_TIME_AUDIO_CLASSIFIER.md Phase C:
  3 conv blocks (16→32→64 channels) + classifier (1024→128→5)
  Input: (N, 1, 64, 32) mel-spectrogram windows
  Output: (N, 5) logits — palm_mute, open_chord, single_note, sustain, silence
"""

from __future__ import annotations

import torch
import torch.nn as nn


class PlayingStyleCNN(nn.Module):
    """CNN for 5-class playing-style detection from mel spectrograms.

    Input:  (N, 1, 64, 32)  — 64 mel bands × 32 time frames
    Output: (N, 5)          — logits for 5 playing styles
    """

    def __init__(self, n_classes: int = 5, dropout: float = 0.3):
        super().__init__()
        self.features = nn.Sequential(
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
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(64 * 4 * 4, 128),
            nn.ReLU(inplace=True),
            nn.Dropout(dropout),
            nn.Linear(128, n_classes),
        )

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.classifier(self.features(x))
