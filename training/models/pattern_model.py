"""Pattern classifier (PMODEL-01): MLP + ONNX export with baked input normalization."""

from __future__ import annotations

import json
from pathlib import Path

import torch
import torch.nn as nn


class PatternNet(nn.Module):
    """5 → 32 → 16 → 7 logits; BatchNorm + Dropout on trunk (D-18-07)."""

    def __init__(self, dropout_p: float = 0.2) -> None:
        super().__init__()
        self.dropout_p = dropout_p
        self.fc1 = nn.Linear(5, 32)
        self.bn1 = nn.BatchNorm1d(32)
        self.fc2 = nn.Linear(32, 16)
        self.bn2 = nn.BatchNorm1d(16)
        self.fc3 = nn.Linear(16, 7)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.fc1(x)
        x = self.bn1(x)
        x = torch.relu(x)
        x = nn.functional.dropout(x, p=self.dropout_p, training=self.training)

        x = self.fc2(x)
        x = self.bn2(x)
        x = torch.relu(x)
        x = nn.functional.dropout(x, p=self.dropout_p, training=self.training)

        return self.fc3(x)


class PatternOnnxExport(nn.Module):
    """Raw X [N,5] → normalize with dataset stats → PatternNet → argmax → Y int64 [N] (rank-1)."""

    _EPS = 1e-8

    def __init__(self, pattern_net: PatternNet, mean: torch.Tensor, std: torch.Tensor) -> None:
        super().__init__()
        self.pattern_net = pattern_net
        self.register_buffer("mean", mean.view(1, 5).to(dtype=torch.float32))
        self.register_buffer("std", torch.clamp(std.view(1, 5), min=self._EPS).to(dtype=torch.float32))

    @classmethod
    def from_norm_stats(cls, path: Path | str, pattern_net: PatternNet) -> PatternOnnxExport:
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
        mean = raw.get("mean")
        std = raw.get("std")
        if not isinstance(mean, list) or not isinstance(std, list):
            raise ValueError("norm_stats.json must contain 'mean' and 'std' lists")
        if len(mean) != 5 or len(std) != 5:
            raise ValueError("mean/std must have length 5")
        order = raw.get("feature_order")
        if order is not None and len(order) != 5:
            raise ValueError("feature_order must have length 5 when present")
        mean_t = torch.tensor(mean, dtype=torch.float32)
        std_t = torch.tensor(std, dtype=torch.float32)
        return cls(pattern_net, mean_t, std_t)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x_norm = (x - self.mean) / self.std
        logits = self.pattern_net(x_norm)
        y = torch.argmax(logits, dim=-1, keepdim=True).to(torch.int64)
        return y.reshape(-1)
