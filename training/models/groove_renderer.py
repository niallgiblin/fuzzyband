"""GrooveRenderer: heteroscedastic groove model.

Predicts, per 10-voice x 16-step cell, a *mean* and *standard deviation* for
velocity and microtiming offset. There is no latent variable: variation comes
from sampling N(mean, std) at runtime (deterministic per bar via a hash seed),
so the model cannot posterior-collapse the way the β-VAE did. This is the
standard "regression with aleatoric uncertainty" formulation.

Tensor contract (batch B) — docs/TIER1_GROOVE_MODEL_CONTRACT.md:
    score       [B, 10, 16]  float 0/1
    condition   [B, 18]      float
    velocity_mean / velocity_std   [B, 10, 16]  float  (std in [0.01, 0.41])
    offset_mean  / offset_std      [B, 10, 16]  float  (offset = fraction of a
                                                        16th; std in [0.01, 0.51])
"""

from __future__ import annotations

import torch
import torch.nn as nn

NUM_VOICES = 10
STEPS = 16
COND_DIM = 18


class GrooveRenderer(nn.Module):
    """Deterministic (score, condition) -> (mean, std) heads. Exported to ONNX."""

    def __init__(self, num_voices: int = NUM_VOICES, steps: int = STEPS,
                 cond_dim: int = COND_DIM, hidden: int = 256):
        super().__init__()
        self.num_voices = num_voices
        self.steps = steps
        n = num_voices * steps

        self.score_net = nn.Sequential(nn.Linear(n, hidden), nn.ReLU())
        self.cond_net = nn.Sequential(nn.Linear(cond_dim, 64), nn.ReLU())
        self.body = nn.Sequential(
            nn.Linear(hidden + 64, hidden), nn.ReLU(),
            nn.Linear(hidden, hidden), nn.ReLU(),
        )
        self.vel_mean_head = nn.Linear(hidden, n)
        self.vel_std_head = nn.Linear(hidden, n)
        self.off_mean_head = nn.Linear(hidden, n)
        self.off_std_head = nn.Linear(hidden, n)

    def forward(self, score: torch.Tensor, condition: torch.Tensor):
        b = score.shape[0]
        s = score.reshape(b, -1)
        h = torch.cat([self.score_net(s), self.cond_net(condition)], dim=1)
        h = self.body(h)

        # velocity is a *multiplier* on the authored velocity (~1.0, not absolute):
        # mean in [0.6, 1.4], std capped at ±0.21 so runtime humanisation stays tasteful.
        velocity_mean = (1.0 + 0.4 * torch.tanh(self.vel_mean_head(h))).reshape(b, self.num_voices, self.steps)
        velocity_std = (torch.sigmoid(self.vel_std_head(h)) * 0.2 + 0.01).reshape(b, self.num_voices, self.steps)
        offset_mean = torch.tanh(self.off_mean_head(h)).reshape(b, self.num_voices, self.steps)
        offset_std = (torch.sigmoid(self.off_std_head(h)) * 0.5 + 0.01).reshape(b, self.num_voices, self.steps)
        return velocity_mean, velocity_std, offset_mean, offset_std


def gaussian_nll(mean: torch.Tensor, std: torch.Tensor, target: torch.Tensor,
                 mask: torch.Tensor) -> torch.Tensor:
    """Masked Gaussian negative log-likelihood (aleatoric regression loss)."""
    var = std * std + 1e-6
    nll = 0.5 * ((target - mean) ** 2 / var + torch.log(var))
    return (nll * mask).sum() / mask.sum().clamp(min=1)
