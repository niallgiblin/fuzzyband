"""Structure classifier (STRUC-04): temporal MLP + ONNX export."""

from __future__ import annotations

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
    """X_struct [1,12,7] → StructureNet → Y_struct [1,3] logits (no softmax)."""

    def __init__(self, structure_net: StructureNet) -> None:
        super().__init__()
        self.structure_net = structure_net

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        logits = self.structure_net(x)
        return logits.reshape(1, 3)
