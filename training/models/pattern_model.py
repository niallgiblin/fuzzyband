"""Pattern classifier (PMODEL-01): MLP + ONNX export with baked input normalization."""

from __future__ import annotations

import json
from pathlib import Path

import torch
import torch.nn as nn


class PatternNet(nn.Module):
    """5 → 64 → 32 → 7 logits; LayerNorm + Dropout (PMODEL-04 / Phase 26 wider trunk)."""

    def __init__(self, dropout_p: float = 0.12) -> None:
        super().__init__()
        self.dropout_p = dropout_p
        self.fc1 = nn.Linear(5, 64)
        self.ln1 = nn.LayerNorm(64)
        self.fc2 = nn.Linear(64, 32)
        self.ln2 = nn.LayerNorm(32)
        self.fc3 = nn.Linear(32, 7)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = self.fc1(x)
        x = self.ln1(x)
        x = torch.relu(x)
        x = nn.functional.dropout(x, p=self.dropout_p, training=self.training)

        x = self.fc2(x)
        x = self.ln2(x)
        x = torch.relu(x)
        x = nn.functional.dropout(x, p=self.dropout_p, training=self.training)

        return self.fc3(x)


class PatternOnnxExport(nn.Module):
    """X [N,7] one-hot → 5 core features → normalize → PatternNet → argmax → Y int64 [N].

    Columns 0–3: bpm, rmsEnergy, spectralCentroid, highFreqFlux.
    Columns 4–6: one-hot SILENT / SOFT / LOUD (matches docs/ONNX_IO.md).
    Loads 5-float norm_stats (training tensors stay [N,5]); ONNX bridge is export-only.
    """

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
        # x: [N, 7] — columns 0–3 core, 4–6 one-hot state (SILENT/SOFT/LOUD)
        core_features = x[:, 0:4]
        one_hot_state = x[:, 4:7]
        state_numeric = one_hot_state @ torch.tensor(
            [0.0, 1.0, 2.0], dtype=torch.float32, device=x.device
        )
        x5 = torch.cat([core_features, state_numeric.unsqueeze(1)], dim=1)
        x_norm = (x5 - self.mean) / self.std
        logits = self.pattern_net(x_norm)
        y = torch.argmax(logits, dim=-1, keepdim=True).to(torch.int64)
        return y.reshape(-1)
