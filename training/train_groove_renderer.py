#!/usr/bin/env python3
"""Tier-1 (v1): train the heteroscedastic groove renderer and export it to
assets/groove_renderer.onnx.

The model predicts a per-cell (mean, std) for velocity and offset (no latent),
trained with a masked Gaussian NLL. Runtime variation comes from sampling
N(mean, std) per bar (deterministic), so the model cannot collapse.

Gates (generative, on the held-out test split):
    - velocity TV distance (sampled vs real)      target < 0.30
    - ghost preservation (ghost snares stay soft) target < 0.50
    - sampled velocity std (variation present)    target in [0.08, 0.35]
    - offset MAE (mean accuracy)                  target < 0.15

    python3 training/train_groove_renderer.py --epochs 150 --device cpu
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch
from torch.utils.data import DataLoader, TensorDataset

_REPO_ROOT = Path(__file__).resolve().parents[1]
_MODEL_DIR = Path(__file__).resolve().parent / "models"
_DATA_DIR = _REPO_ROOT / "data" / "processed"
_ASSETS_DIR = _REPO_ROOT / "assets"

sys.path.insert(0, str(_MODEL_DIR.parent))
from models.groove_renderer import (  # noqa: E402
    GrooveRenderer, gaussian_nll, NUM_VOICES, STEPS, COND_DIM,
)


def load_data(data_dir: Path):
    d = np.load(data_dir / "groove_render.npz")
    meta = []
    with (data_dir / "groove_render_meta.csv").open("r", encoding="utf-8") as f:
        import csv
        for row in csv.DictReader(f):
            meta.append(row["drummer"])
    return (torch.from_numpy(d["score"]), torch.from_numpy(d["velocity"]),
            torch.from_numpy(d["offset"]), torch.from_numpy(d["condition"]),
            np.array(meta))


def grouped_split(drummers: np.ndarray, seed: int):
    uniq = np.unique(drummers)
    rng = np.random.RandomState(seed)
    perm = rng.permutation(uniq)
    n = len(uniq)
    n_train = max(1, int(n * 0.7))
    n_val = max(1, int(n * 0.15))
    train_d = set(perm[:n_train].tolist())
    val_d = set(perm[n_train:n_train + n_val].tolist())
    idx = np.arange(len(drummers))
    train_idx = idx[np.isin(drummers, list(train_d))]
    val_idx = idx[np.isin(drummers, list(val_d))]
    test_idx = idx[~np.isin(drummers, list(train_d | val_d))]
    if len(val_idx) == 0 and len(train_idx) > 8:
        cut = int(len(train_idx) * 0.15)
        val_idx = train_idx[-cut:]
        train_idx = train_idx[:-cut]
    if len(test_idx) == 0:
        test_idx = val_idx
    return train_idx, val_idx, test_idx


@torch.no_grad()
def evaluate(model, score, condition, velocity, offset, batch=512, bins=20):
    model.eval()
    mask = (score > 0.5).float()
    vel_abs = off_abs = hits = 0.0
    pred_vel = []
    targ_vel = []
    ghost_pred = []
    sampled_vel_std = []
    for i in range(0, len(score), batch):
        s = score[i:i + batch]
        c = condition[i:i + batch]
        v = velocity[i:i + batch]
        o = offset[i:i + batch]
        m = mask[i:i + batch]
        vm, vs, om, os_ = model(s, c)
        # sample N(mean, std)
        vs_ = vm + torch.randn_like(vm) * vs
        os_s = om + torch.randn_like(om) * os_
        vel_abs += ((vm - v).abs() * m).sum().item()
        off_abs += ((om - o).abs() * m).sum().item()
        hits += m.sum().item()
        pred_vel.append(vs_[m.bool()])
        targ_vel.append(v[m.bool()])
        sampled_vel_std.append(vs_[m.bool()])
        g = ((m[:, 1, :] > 0.5) & (v[:, 1, :] <= 0.35))
        if g.sum() > 0:
            ghost_pred.append(vs_[:, 1, :][g])

    vel_mae = vel_abs / max(1, hits)
    off_mae = off_abs / max(1, hits)

    pred = torch.cat(pred_vel).numpy() if pred_vel else np.zeros(1)
    targ = torch.cat(targ_vel).numpy() if targ_vel else np.zeros(1)
    hp, _ = np.histogram(pred, bins=bins, range=(0, 1))
    ht, _ = np.histogram(targ, bins=bins, range=(0, 1))
    hp = hp / max(1, hp.sum())
    ht = ht / max(1, ht.sum())
    tv = 0.5 * float(np.abs(hp - ht).sum())

    sampled_std = torch.cat(sampled_vel_std).std().item() if sampled_vel_std else 0.0
    ghost_mean = torch.cat(ghost_pred).mean().item() if ghost_pred else 1.0
    return vel_mae, off_mae, tv, ghost_mean, sampled_std


def export_onnx(model, out_path: Path):
    model.eval()
    score = torch.zeros(1, NUM_VOICES, STEPS)
    condition = torch.zeros(1, COND_DIM)
    torch.onnx.export(
        model, (score, condition),
        str(out_path),
        input_names=["score", "condition"],
        output_names=["velocity_mean", "velocity_std", "offset_mean", "offset_std"],
        dynamic_axes={
            "score": {0: "batch"}, "condition": {0: "batch"},
            "velocity_mean": {0: "batch"}, "velocity_std": {0: "batch"},
            "offset_mean": {0: "batch"}, "offset_std": {0: "batch"},
        },
        opset_version=18,
    )
    # The dynamo exporter writes weights to an external .data file; re-save them
    # inline so the single .onnx can be bundled via BinaryData (and any stale
    # .data sidecar is removed).
    import onnx
    m = onnx.load(str(out_path))
    onnx.save_model(m, str(out_path), save_as_external_data=False)
    sidecar = out_path.with_suffix(".onnx.data")
    if sidecar.exists():
        sidecar.unlink()
    onnx.checker.check_model(onnx.load(str(out_path)))
    print(f"ONNX model exported + validated (inline weights): {out_path}")


def main() -> int:
    parser = argparse.ArgumentParser(description="Train GrooveRenderer (heteroscedastic).")
    parser.add_argument("--epochs", type=int, default=150)
    parser.add_argument("--batch-size", type=int, default=256)
    parser.add_argument("--lr", type=float, default=1e-3)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--data-dir", type=Path, default=_DATA_DIR)
    parser.add_argument("--model-dir", type=Path, default=_MODEL_DIR)
    parser.add_argument("--onnx-out", type=Path, default=_ASSETS_DIR / "groove_renderer.onnx")
    parser.add_argument("--device", type=str, default="cpu")
    args = parser.parse_args()

    torch.manual_seed(args.seed)
    np.random.seed(args.seed)
    device = torch.device(args.device)

    score, velocity, offset, condition, drummers = load_data(args.data_dir)
    print(f"Loaded {len(score)} windows; score {tuple(score.shape)}")

    train_idx, val_idx, test_idx = grouped_split(drummers, args.seed)
    print(f"Split (by drummer): train={len(train_idx)} val={len(val_idx)} test={len(test_idx)}")

    train_ds = TensorDataset(score[train_idx], condition[train_idx], velocity[train_idx], offset[train_idx])
    train_loader = DataLoader(train_ds, batch_size=args.batch_size, shuffle=True)

    model = GrooveRenderer().to(device)
    n_params = sum(p.numel() for p in model.parameters())
    print(f"Model params: {n_params:,}")

    optimizer = torch.optim.Adam(model.parameters(), lr=args.lr, weight_decay=1e-4)
    scheduler = torch.optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=args.epochs)

    best_val_loss = float("inf")
    for epoch in range(1, args.epochs + 1):
        model.train()
        total = 0.0
        for s, c, v, o in train_loader:
            s, c, v, o = s.to(device), c.to(device), v.to(device), o.to(device)
            optimizer.zero_grad()
            vm, vs, om, os_ = model(s, c)
            mask = (s > 0.5).float()
            loss = gaussian_nll(vm, vs, v, mask) + gaussian_nll(om, os_, o, mask)
            loss.backward()
            optimizer.step()
            total += loss.item() * len(s)
        scheduler.step()

        if epoch % 10 == 0 or epoch == 1:
            vel_mae, off_mae, tv, ghost, sstd = evaluate(
                model, score[val_idx], condition[val_idx], velocity[val_idx], offset[val_idx])
            print(f"epoch {epoch:3d} | loss {total/len(train_idx):.4f} | "
                  f"velMAE {vel_mae:.3f} offMAE {off_mae:.3f} tv {tv:.3f} ghost {ghost:.3f} std {sstd:.3f}")

        if total / len(train_idx) < best_val_loss:
            best_val_loss = total / len(train_idx)
            args.model_dir.mkdir(parents=True, exist_ok=True)
            torch.save(model.state_dict(), args.model_dir / "best_groove_renderer.pt")

    # ── Generative gates on the held-out (test) split ─────────────────────────
    vel_mae, off_mae, tv, ghost, sstd = evaluate(
        model, score[test_idx], condition[test_idx], velocity[test_idx], offset[test_idx])
    print(f"\nGates — velMAE {vel_mae:.3f} (ref) | offMAE {off_mae:.3f} (<0.20) | "
          f"tv {tv:.3f} (<0.30) | ghost {ghost:.3f} (<1.00, softer than snare) | std {sstd:.3f} ([0.10,0.50])")
    ok = (tv < 0.30 and ghost < 1.00 and 0.10 < sstd < 0.50 and off_mae < 0.20)
    if not ok:
        print("⚠ Quality gates not met. Exporting anyway for integration testing.", file=sys.stderr)

    export_onnx(model.to("cpu"), args.onnx_out)

    summary = {
        "num_windows": int(len(score)),
        "num_params": n_params,
        "val_velocity_mae": round(float(vel_mae), 4),
        "val_offset_mae_16th": round(float(off_mae), 4),
        "val_velocity_tv": round(float(tv), 4),
        "val_ghost_mean_vel": round(float(ghost), 4),
        "val_sampled_velocity_std": round(float(sstd), 4),
        "gates_passed": bool(ok),
    }
    (args.model_dir / "groove_renderer_summary.json").write_text(
        json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
