"""Structure classifier (STRUC-04): temporal MLP + ONNX export."""

from __future__ import annotations

import json
from pathlib import Path

import torch
import torch.nn as nn


class StructureNet(nn.Module):
    """12×7 temporal window → flatten → MLP → 3 logits; LayerNorm + Dropout."""

    def __init__(self, dropout_p: float = 0.2) -> None:
        super().__init__()
        self.dropout_p = dropout_p
        input_dim = 12 * 7
        self.fc1 = nn.Linear(input_dim, 64)
        self.ln1 = nn.LayerNorm(64)
        self.fc2 = nn.Linear(64, 32)
        self.ln2 = nn.LayerNorm(32)
        self.fc3 = nn.Linear(32, 3)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x = x.reshape(x.shape[0], -1)
        x = self.fc1(x)
        x = self.ln1(x)
        x = torch.relu(x)
        x = nn.functional.dropout(x, p=self.dropout_p, training=self.training)
        x = self.fc2(x)
        x = self.ln2(x)
        x = torch.relu(x)
        x = nn.functional.dropout(x, p=self.dropout_p, training=self.training)
        return self.fc3(x)


class StructureOnnxExport(nn.Module):
    """Raw X_struct [1,12,7] → normalize with training stats → StructureNet → Y_struct [1,3] logits.

    Mean/std buffers are baked into the ONNX graph as initializers named ``mean`` and ``std``,
    so the C++ runtime feeds raw FeatureVector windows directly with no external normalization.
    """

    _EPS = 1e-8

    def __init__(self, structure_net: StructureNet, mean: torch.Tensor, std: torch.Tensor) -> None:
        super().__init__()
        self.structure_net = structure_net
        self.register_buffer("mean", mean.view(1, 1, 7).to(dtype=torch.float32))
        self.register_buffer("std", torch.clamp(std.view(1, 1, 7), min=self._EPS).to(dtype=torch.float32))

    @classmethod
    def from_norm_stats(
        cls, path: Path | str, structure_net: StructureNet
    ) -> "StructureOnnxExport":
        """Build a from_norm_stats wrapper by loading 7-feature mean/std from structure_norm_stats.json."""
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
        mean = raw.get("mean")
        std = raw.get("std")
        if not isinstance(mean, list) or not isinstance(std, list):
            raise ValueError("structure_norm_stats.json must contain 'mean' and 'std' lists")
        if len(mean) != 7 or len(std) != 7:
            raise ValueError("mean/std must have length 7")
        order = raw.get("feature_order")
        if order is not None and len(order) != 7:
            raise ValueError("feature_order must have length 7 when present")
        mean_t = torch.tensor(mean, dtype=torch.float32)
        std_t = torch.tensor(std, dtype=torch.float32)
        return cls(structure_net, mean_t, std_t)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        x_norm = (x - self.mean) / self.std
        logits = self.structure_net(x_norm)
        return logits.reshape(1, 3)
