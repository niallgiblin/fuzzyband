"""Bass regression model (BMODEL-01): MLP + ONNX export with baked input normalization."""

from __future__ import annotations

import json
from pathlib import Path

import torch
import torch.nn as nn


class BassNet(nn.Module):
    """7 → 32 → 16 → 32 piano-roll regression; LayerNorm + Dropout (BMODEL-03 / v0.4.0)."""

    def __init__(self, dropout_p: float = 0.2) -> None:
        super().__init__()
        self.dropout_p = dropout_p
        self.fc1 = nn.Linear(7, 32)
        self.ln1 = nn.LayerNorm(32)
        self.fc2 = nn.Linear(32, 16)
        self.ln2 = nn.LayerNorm(16)
        self.fc3 = nn.Linear(16, 32)

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


class BassOnnxExport(nn.Module):
    """Raw X_bass → normalize with training stats → BassNet → Y_bass float32 [1,32].

    **Batch size:** this wrapper is **N=1 only** (ONNX trace dummy and plugin contract).
    `forward` rejects ``x`` with ``x.shape[0] != 1``.
    """

    _EPS = 1e-8

    def __init__(self, bass_net: BassNet, mean: torch.Tensor, std: torch.Tensor) -> None:
        super().__init__()
        self.bass_net = bass_net
        self.register_buffer("mean", mean.view(1, 7).to(dtype=torch.float32))
        self.register_buffer("std", torch.clamp(std.view(1, 7), min=self._EPS).to(dtype=torch.float32))

    @classmethod
    def from_norm_stats(cls, path: Path | str, bass_net: BassNet) -> BassOnnxExport:
        raw = json.loads(Path(path).read_text(encoding="utf-8"))
        mean = raw.get("mean")
        std = raw.get("std")
        if not isinstance(mean, list) or not isinstance(std, list):
            raise ValueError("bass_norm_stats.json must contain 'mean' and 'std' lists")
        if len(mean) != 7 or len(std) != 7:
            raise ValueError("mean/std must have length 7")
        order = raw.get("feature_order")
        if order is not None and len(order) != 7:
            raise ValueError("feature_order must have length 7 when present")
        mean_t = torch.tensor(mean, dtype=torch.float32)
        std_t = torch.tensor(std, dtype=torch.float32)
        return cls(bass_net, mean_t, std_t)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        # Skip during ONNX/JIT trace — N is fixed for the dummy input; avoids TracerWarning on shape[0].
        if not torch.jit.is_tracing() and not torch.jit.is_scripting():
            if x.shape[0] != 1:
                raise ValueError(
                    "BassOnnxExport supports batch size 1 only (ONNX/plugin contract); "
                    f"got N={x.shape[0]}"
                )
        x_norm = (x - self.mean) / self.std
        y = self.bass_net(x_norm)  # [1, 32] float32 when N=1
        return y.reshape(1, 32)
