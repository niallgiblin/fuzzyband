#!/usr/bin/env python3
"""Evaluate the DEPLOYED inference path, not the training head.

The training loop reports macro-F1 of the 22-way classifier head
(GrooveClassifier.forward). The plugin never uses that head: MetalGrooveInference
runs backbone bottleneck -> cosine-similarity nearest-centroid lookup against the
SHIPPED centroids (src/inference/pattern_embeddings.h, from
training/models/groove_centroids.json). This script scores THAT path:

  1. Replicate the deployed selection: bottleneck -> cosine-NN to centroids.
  2. Compare centroids computed from TRAIN ONLY (honest) vs ALL DATA (what
     export_centroids.py actually does — a leak of val into the lookup table).
  3. Score the deployed path on the frozen grouped split (train / val; note: the
     builder emits NO test split, so "test" metrics elsewhere are val reused).
  4. Verify the shipped ONNX backbone matches the checkpoint backbone (artifact
     integrity: is the deployed .onnx the model we think it is?).
  5. Score the deployed STYLE head (classifyStyle, 5 classes) against the
     perception folders in data/raw/ — the label the UI displays. The export
     script says the style head is randomly initialised; measure it.
  6. Check the shipped centroid table vs the current dataset (staleness: the
     dataset was rebuilt 19 Aug, the model/centroids shipped 28 Jun).

Usage:
  python3 training/evaluate_deployed_path.py [--out training/artifacts/deployed_eval.json]
"""

from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

import numpy as np

_REPO_ROOT = Path(__file__).resolve().parents[1]
_DATA_DIR = _REPO_ROOT / "data" / "processed"
_MODEL_DIR = Path(__file__).resolve().parent / "models"
_RAW_DIR = _REPO_ROOT / "data" / "raw"
_ONNX_PATH = _REPO_ROOT / "assets" / "metal_groove.onnx"

# Shared grouped-split helpers + mel extractor (same code the dataset builder uses).
sys.path.insert(0, str(Path(__file__).resolve().parent / "scripts"))
from dataset_split import load_split_indices  # noqa: E402

sys.path.insert(0, str(Path(__file__).resolve().parent))
from models.groove_model import GrooveClassifier  # noqa: E402


# ─── Cosine similarity (mirrors MetalGrooveInference.cpp) ────────────────────

def cosine_sim_matrix(queries: np.ndarray, centroids: np.ndarray) -> np.ndarray:
    """queries (N, D), centroids (K, D) -> (N, K) cosine similarities."""
    qn = queries / (np.linalg.norm(queries, axis=1, keepdims=True) + 1e-12)
    cn = centroids / (np.linalg.norm(centroids, axis=1, keepdims=True) + 1e-12)
    return qn @ cn.T


# ─── Metric helpers ───────────────────────────────────────────────────────────

def score(y_true: np.ndarray, y_pred: np.ndarray, n_classes: int) -> dict:
    from sklearn.metrics import accuracy_score, f1_score
    acc = float(accuracy_score(y_true, y_pred))
    # Restrict the macro average to classes actually present in the evaluated
    # subset. On the partial test split (6 classes with takes), predictions that
    # land on the 16 untested classes must not drag the macro-F1 toward zero —
    # that would report the *split coverage* as a model failure.
    present = sorted(set(int(v) for v in y_true))
    f1 = float(f1_score(y_true, y_pred, labels=present, average="macro", zero_division=0))
    cm = np.zeros((n_classes, n_classes), dtype=int)
    for t, p in zip(y_true, y_pred):
        cm[t, p] += 1
    row = cm.sum(axis=1)
    recall = np.divide(np.diag(cm), row, out=np.zeros(n_classes), where=row > 0)
    dead = [int(i) for i in range(n_classes) if row[i] > 0 and recall[i] == 0.0]
    return {"acc": acc, "macro_f1": f1, "dead_classes": dead,
            "per_class_recall": recall.tolist(), "confusion": cm.tolist()}


# ─── Load model & data ───────────────────────────────────────────────────────

def load_checkpoint() -> GrooveClassifier:
    import torch
    model = GrooveClassifier(n_classes=22)
    sd = torch.load(_MODEL_DIR / "best_groove_model.pt", weights_only=True, map_location="cpu")
    model.load_state_dict(sd)
    model.eval()
    return model


def bottlenecks(model, X: np.ndarray, batch: int = 128) -> np.ndarray:
    import torch
    out = []
    with torch.no_grad():
        for s in range(0, len(X), batch):
            xb = torch.from_numpy(X[s:s + batch])
            out.append(model.backbone(xb).numpy())
    return np.concatenate(out, axis=0)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--out", type=Path, default=Path("training/artifacts/deployed_eval.json"))
    args = parser.parse_args()

    report: dict = {}
    t0 = time.time()

    # ── Data + frozen split ────────────────────────────────────────────────
    X = np.load(_DATA_DIR / "X_groove.npy")
    y = np.load(_DATA_DIR / "y_groove.npy")
    n_classes = int(y.max()) + 1
    print(f"Data: X {X.shape}, y {y.shape}, {n_classes} classes")

    meta = _DATA_DIR / "meta_groove.csv"
    frozen = load_split_indices(meta, len(X))
    if frozen is None:
        print("ERROR: no frozen split found", file=sys.stderr)
        return 1
    train_idx, val_idx, test_idx = frozen
    print(f"Frozen split: train={len(train_idx)} val={len(val_idx)} test={len(test_idx)}")
    report["split"] = {"train": int(len(train_idx)), "val": int(len(val_idx)),
                       "test": int(len(test_idx))}
    if len(test_idx) == 0:
        print("NOTE: builder emits NO test split — deployed-path scores below are on val.")

    # ── Backbone bottlenecks ───────────────────────────────────────────────
    model = load_checkpoint()
    print("Computing bottlenecks over all samples...")
    Z = bottlenecks(model, X)
    report["bottleneck_dim"] = int(Z.shape[1])
    print(f"Bottlenecks: {Z.shape} ({time.time()-t0:.1f}s)")

    # ── 1+2. Deployed selection: centroid cosine-NN ────────────────────────
    def centroid_nn(centroids: np.ndarray, idx: np.ndarray):
        sims = cosine_sim_matrix(Z[idx], centroids)
        return sims.argmax(axis=1)

    # (a) Honest: centroids from TRAIN only
    c_train = np.zeros((n_classes, Z.shape[1]), dtype=np.float64)
    cnt = np.zeros(n_classes)
    for i in train_idx:
        c_train[y[i]] += Z[i]
        cnt[y[i]] += 1
    c_train = (c_train / np.maximum(cnt, 1)[:, None]).astype(np.float32)

    # (b) Leaky reference: centroids from ALL data (what export_centroids.py used
    #     to do — kept as a reference so the leak's cost stays visible).
    c_all = np.zeros((n_classes, Z.shape[1]), dtype=np.float64)
    cnt_all = np.zeros(n_classes)
    for i in range(len(Z)):
        c_all[y[i]] += Z[i]
        cnt_all[y[i]] += 1
    c_all = (c_all / np.maximum(cnt_all, 1)[:, None]).astype(np.float32)

    report["centroid_nn"] = {
        "train_only_centroids": {
            "train": score(y[train_idx], centroid_nn(c_train, train_idx), n_classes),
            "val": score(y[val_idx], centroid_nn(c_train, val_idx), n_classes),
            "test": (score(y[test_idx], centroid_nn(c_train, test_idx), n_classes)
                     if len(test_idx) > 0 else None),
        },
        "all_data_centroids_leaky_reference": {
            "train": score(y[train_idx], centroid_nn(c_all, train_idx), n_classes),
            "val": score(y[val_idx], centroid_nn(c_all, val_idx), n_classes),
            "test": (score(y[test_idx], centroid_nn(c_all, test_idx), n_classes)
                     if len(test_idx) > 0 else None),
        },
    }

    # ── Reference: classifier head (what the training loop reports) ────────
    import torch
    preds = []
    with torch.no_grad():
        for s in range(0, len(X), 128):
            logits = model(torch.from_numpy(X[s:s + 128]))
            preds.append(logits.argmax(dim=1).numpy())
    preds = np.concatenate(preds)
    report["classifier_head_reference"] = {
        "train": score(y[train_idx], preds[train_idx], n_classes),
        "val": score(y[val_idx], preds[val_idx], n_classes),
        "test": (score(y[test_idx], preds[test_idx], n_classes)
                 if len(test_idx) > 0 else None),
    }
    print(f"Classifier head val: {report['classifier_head_reference']['val']['acc']:.4f} "
          f"F1 {report['classifier_head_reference']['val']['macro_f1']:.4f}")

    # ── 3. Shipped centroid table vs recomputed (staleness) ────────────────
    shipped_json = _MODEL_DIR / "groove_centroids.json"
    if shipped_json.exists():
        sc = json.loads(shipped_json.read_text())
        shipped_c = np.array(sc["centroids"], dtype=np.float32)
        # Which of our recomputations is closer to the shipped table?
        sim_train = np.mean([cosine_sim_matrix(shipped_c[i:i+1], c_train[i:i+1])[0, 0]
                             for i in range(n_classes)])
        sim_all = np.mean([cosine_sim_matrix(shipped_c[i:i+1], c_all[i:i+1])[0, 0]
                           for i in range(n_classes)])
        report["shipped_centroid_match"] = {
            "mean_cos_vs_train_only": float(sim_train),
            "mean_cos_vs_all_data": float(sim_all),
            "sample_counts": sc.get("sample_counts"),
        }
        print(f"Shipped centroids match train-only recompute: {sim_train:.4f}, "
              f"all-data recompute: {sim_all:.4f}")

    # ── 4. ONNX vs checkpoint backbone (artifact integrity) ────────────────
    try:
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.intra_op_num_threads = 1
        sess = ort.InferenceSession(str(_ONNX_PATH), sess_options=so, providers=["CPUExecutionProvider"])
        idx_check = np.arange(0, min(len(X), 32))
        ort_bn = np.concatenate([
            sess.run(["bottleneck"], {"mel": X[s:s+8].astype(np.float32)})[0]
            for s in range(0, len(idx_check), 8)
        ])
        ckpt_bn = Z[idx_check]
        cos = np.array([cosine_sim_matrix(ort_bn[i:i+1], ckpt_bn[i:i+1])[0, 0]
                        for i in range(len(idx_check))])
        report["onnx_backbone_match"] = {
            "mean_cos_checkpoint_vs_onnx": float(cos.mean()),
            "samples_checked": int(len(idx_check)),
        }
        print(f"ONNX bottleneck vs checkpoint backbone (cos): {cos.mean():.6f}")
    except Exception as e:  # noqa: BLE001
        report["onnx_backbone_match"] = {"error": str(e)}
        print(f"ONNX check skipped: {e}")

    # ── 5. Deployed style head vs perception held-out (val) ────────────────
    # The style head was trained on the perception TRAIN split, so it must be
    # scored on the frozen grouped VAL split (held-out take per class), not on
    # all windows. Uses the built perception tensors (X.npy/y.npy/meta.csv).
    try:
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.intra_op_num_threads = 1
        sess = ort.InferenceSession(str(_ONNX_PATH), sess_options=so, providers=["CPUExecutionProvider"])

        Xp = np.load(_DATA_DIR / "X.npy")
        yp = np.load(_DATA_DIR / "y.npy")
        pfrozen = load_split_indices(_DATA_DIR / "meta.csv", len(Xp))
        if pfrozen is None:
            report["deployed_style_head"] = {"error": "perception meta.csv missing/stale"}
        else:
            _, pval_idx, _ = pfrozen
            Xp_val = Xp[pval_idx].astype(np.float32)
            yp_val = yp[pval_idx]
            logits = np.concatenate([
                sess.run(["style_logits"], {"mel": Xp_val[s:s+64].astype(np.float32)})[0]
                for s in range(0, len(Xp_val), 64)
            ])
            pred_style = logits.argmax(axis=1)
            report["deployed_style_head"] = score(yp_val, pred_style, 5)
            report["deployed_style_head"]["n_samples"] = int(len(yp_val))
            from collections import Counter
            report["deployed_style_head"]["style_label_dist"] = dict(Counter(yp_val.tolist()))
            print(f"Deployed style head on perception VAL ({len(yp_val)} held-out windows): "
                  f"acc {report['deployed_style_head']['acc']:.4f}  "
                  f"F1 {report['deployed_style_head']['macro_f1']:.4f} (chance=0.20)")
    except Exception as e:  # noqa: BLE001
        report["deployed_style_head"] = {"error": str(e)}
        print(f"Style head check skipped: {e}")

    report["elapsed_sec"] = round(time.time() - t0, 1)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(f"\nReport written: {args.out}")

    # ── Summary ────────────────────────────────────────────────────────────
    print("\n=== DEPLOYED PATH SUMMARY ===")
    for label, key in [("Classifier head (reported)", "classifier_head_reference"),
                       ("Centroid-NN, train-only centroids (HONEST)", "centroid_nn.train_only_centroids"),
                       ("Centroid-NN, all-data centroids (leaky ref)", "centroid_nn.all_data_centroids_leaky_reference")]:
        blk = report
        for part in key.split("."):
            blk = blk[part]
        v = blk["val"]
        t = blk.get("test")
        tstr = (f"  test acc {t['acc']:.4f}  F1 {t['macro_f1']:.4f}" if t else "  test: n/a")
        print(f"  {label}: val acc {v['acc']:.4f}  macro-F1 {v['macro_f1']:.4f}  dead {v['dead_classes']} |{tstr}")
    if "deployed_style_head" in report and "acc" in report["deployed_style_head"]:
        s = report["deployed_style_head"]
        print(f"  Deployed style head: acc {s['acc']:.4f} (chance 0.20)")
    if "onnx_backbone_match" in report and "mean_cos_checkpoint_vs_onnx" in report["onnx_backbone_match"]:
        print(f"  ONNX backbone vs checkpoint: cos {report['onnx_backbone_match']['mean_cos_checkpoint_vs_onnx']:.6f}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
