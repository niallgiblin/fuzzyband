"""Gate tests for the DEPLOYED inference path (MetalGrooveInference).

The training loop reports macro-F1 of the 22-way classifier head — a predictor
that never runs in the plugin. The deployed path is: backbone bottleneck →
cosine-similarity nearest-centroid lookup against the SHIPPED centroid table
(src/inference/pattern_embeddings.h). These tests gate THAT path, so a retrain
or a bad export cannot silently regress the musical behaviour users actually
hear while the reported number stays green.

Leak guard: centroids must be computed from the TRAINING split only. Computing
them from train + val bakes held-out audio into the deployed lookup table and
inflates any val measurement of the nearest-centroid path.

These tests skip when the gitignored dataset tensors or the checkpoint are not
present (e.g. a fresh clone / CI without regenerated data).
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
import pytest

_TRAINING_DIR = Path(__file__).resolve().parent.parent
_REPO_ROOT = _TRAINING_DIR.parent
_DATA_DIR = _REPO_ROOT / "data" / "processed"
_MODEL_DIR = _TRAINING_DIR / "models"
_ONNX_PATH = _REPO_ROOT / "assets" / "metal_groove.onnx"

# dataset_split lives in training/scripts/.
if str(_TRAINING_DIR / "scripts") not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR / "scripts"))

# ─── Optional dependencies ────────────────────────────────────────────────────

try:
    import torch  # noqa: F401
    _HAS_TORCH = True
except ImportError:
    _HAS_TORCH = False

try:
    import onnxruntime as ort  # noqa: F401
    _HAS_ORT = True
except ImportError:
    _HAS_ORT = False

_X_PATH = _DATA_DIR / "X_groove.npy"
_Y_PATH = _DATA_DIR / "y_groove.npy"
_META_PATH = _DATA_DIR / "meta_groove.csv"
_CKPT_PATH = _MODEL_DIR / "best_groove_model.pt"

_HAS_DATA = all(p.is_file() for p in (_X_PATH, _Y_PATH, _META_PATH)) and _CKPT_PATH.is_file()

pytestmark = [
    pytest.mark.skipif(not _HAS_TORCH, reason="torch not installed"),
    pytest.mark.skipif(not _HAS_DATA,
                       reason="dataset tensors/checkpoint not present (regenerate data, or run in full env)"),
]


def _cosine_sim_matrix(queries: np.ndarray, centroids: np.ndarray) -> np.ndarray:
    """queries (N, D), centroids (K, D) -> (N, K) cosine similarities."""
    qn = queries / (np.linalg.norm(queries, axis=1, keepdims=True) + 1e-12)
    cn = centroids / (np.linalg.norm(centroids, axis=1, keepdims=True) + 1e-12)
    return qn @ cn.T


@pytest.fixture(scope="module")
def deployed():
    """Load data + checkpoint, compute bottlenecks once, return all pieces."""
    from dataset_split import load_split_indices
    from models.groove_model import GrooveClassifier

    X = np.load(_X_PATH)
    y = np.load(_Y_PATH)
    n_classes = int(y.max()) + 1

    frozen = load_split_indices(_META_PATH, len(X))
    assert frozen is not None, "meta_groove.csv missing or stale"
    train_idx, val_idx, test_idx = frozen

    model = GrooveClassifier(n_classes=n_classes)
    sd = torch.load(_CKPT_PATH, weights_only=True, map_location="cpu")
    model.load_state_dict(sd)
    model.eval()

    # Bottlenecks for the whole dataset (deployed selection input).
    Z = []
    with torch.no_grad():
        for s in range(0, len(X), 256):
            Z.append(model.backbone(torch.from_numpy(X[s:s + 256])).numpy())
    Z = np.concatenate(Z, axis=0)

    # Shipped centroid table (what the plugin actually queries).
    shipped_path = _MODEL_DIR / "groove_centroids.json"
    shipped_c = None
    shipped_provenance = None
    if shipped_path.is_file():
        sc = json.loads(shipped_path.read_text())
        shipped_c = np.array(sc["centroids"], dtype=np.float32)
        shipped_provenance = sc.get("split_provenance", {})

    return {
        "X": X, "y": y, "n_classes": n_classes, "Z": Z,
        "train_idx": train_idx, "val_idx": val_idx, "test_idx": test_idx,
        "shipped_centroids": shipped_c, "shipped_provenance": shipped_provenance,
    }


def _centroid_nn_acc_f1(deployed, centroids: np.ndarray, idx: np.ndarray):
    """Score the deployed cosine-NN selector over the given row indices."""
    from sklearn.metrics import accuracy_score, f1_score
    sims = _cosine_sim_matrix(deployed["Z"][idx], centroids)
    preds = sims.argmax(axis=1)
    y = deployed["y"][idx]
    n = deployed["n_classes"]
    cm = np.zeros((n, n), dtype=int)
    for t, p in zip(y, preds):
        cm[t, p] += 1
    row = cm.sum(axis=1)
    recall = np.divide(np.diag(cm), row, out=np.zeros(n), where=row > 0)
    dead = [int(i) for i in range(n) if row[i] > 0 and recall[i] == 0.0]
    # Macro-F1 over classes actually present in this subset (partial test split
    # must not be dragged down by predictions into untested classes).
    present = sorted(set(int(v) for v in y))
    return {
        "acc": float(accuracy_score(y, preds)),
        "macro_f1": float(f1_score(y, preds, labels=present, average="macro", zero_division=0)),
        "dead_classes": dead,
    }


def _train_only_centroids(deployed) -> np.ndarray:
    """Recompute centroids from the train split only (honest reference)."""
    n = deployed["n_classes"]
    dim = deployed["Z"].shape[1]
    sums = np.zeros((n, dim), dtype=np.float64)
    cnt = np.zeros(n)
    for i in deployed["train_idx"]:
        sums[deployed["y"][i]] += deployed["Z"][i]
        cnt[deployed["y"][i]] += 1
    return (sums / np.maximum(cnt, 1)[:, None]).astype(np.float32)


class TestDeployedSelectorGate:
    """The number that ships must be the number that is gated."""

    def test_val_macro_f1_above_threshold(self, deployed) -> None:
        """Deployed cosine-NN (train-only centroids) must beat a real bar on val."""
        centroids = _train_only_centroids(deployed)
        m = _centroid_nn_acc_f1(deployed, centroids, deployed["val_idx"])
        # Measured 0.834 acc / 0.834 macro-F1 after the train-only fix. The gate
        # is set well below the measurement so small data drift does not flake,
        # but a genuine regression (bad retrain, wrong centroids) fails hard.
        assert m["macro_f1"] >= 0.70, f"deployed val macro-F1 {m['macro_f1']:.3f} < 0.70"
        assert m["acc"] >= 0.70, f"deployed val acc {m['acc']:.3f} < 0.70"

    def test_test_macro_f1_above_threshold(self, deployed) -> None:
        """Deployed cosine-NN on the true held-out TEST takes (honest number)."""
        if len(deployed["test_idx"]) == 0:
            pytest.skip("no test split in this dataset (record a third take per class)")
        centroids = _train_only_centroids(deployed)
        m = _centroid_nn_acc_f1(deployed, centroids, deployed["test_idx"])
        # Test takes are completely unseen by the centroid table (train-only) and
        # by the checkpoint selection (val-based). Gate lower than the val bar —
        # test covers only the 6 three-take classes, so the estimate is noisier.
        assert m["macro_f1"] >= 0.60, f"deployed test macro-F1 {m['macro_f1']:.3f} < 0.60"
        assert m["acc"] >= 0.60, f"deployed test acc {m['acc']:.3f} < 0.60"

    def test_no_dead_classes_on_val(self, deployed) -> None:
        """Every class present in val must be predicted correctly at least once."""
        centroids = _train_only_centroids(deployed)
        m = _centroid_nn_acc_f1(deployed, centroids, deployed["val_idx"])
        assert m["dead_classes"] == [], f"dead (zero-recall) classes: {m['dead_classes']}"


class TestDeployedStyleHeadGate:
    """The deployed style head must be TRAINED, not the random-init export head.

    The shipped ONNX once carried a randomly-initialised Linear(128,5) style head
    (measured 0.285 acc / 0.0 silence recall). train_style_head.py + the export
    now bake in a trained head; this gate fails if the artifact reverts to noise.
    """

    @pytest.mark.skipif(not _HAS_ORT or not _ONNX_PATH.is_file(),
                        reason="onnxruntime/model not present")
    def test_style_head_above_chance_on_val(self) -> None:
        """Score the deployed style_logits on the perception held-out (val) takes."""
        from sklearn.metrics import accuracy_score, f1_score
        import onnxruntime as ort

        Xp = np.load(_DATA_DIR / "X.npy")
        yp = np.load(_DATA_DIR / "y.npy")
        from dataset_split import load_split_indices as _lsi
        pfrozen = _lsi(_DATA_DIR / "meta.csv", len(Xp))
        if pfrozen is None:
            pytest.skip("perception meta.csv missing/stale")
        _, pval_idx, _ = pfrozen

        so = ort.SessionOptions()
        so.intra_op_num_threads = 1
        sess = ort.InferenceSession(str(_ONNX_PATH), sess_options=so,
                                    providers=["CPUExecutionProvider"])
        Xv = Xp[pval_idx].astype(np.float32)
        yv = yp[pval_idx]
        logits = np.concatenate([
            sess.run(["style_logits"], {"mel": Xv[s:s + 64]})[0]
            for s in range(0, len(Xv), 64)
        ])
        preds = logits.argmax(axis=1)
        acc = float(accuracy_score(yv, preds))
        f1 = float(f1_score(yv, preds, average="macro", zero_division=0))
        # Trained head measured 0.769 acc / 0.796 F1. The random head was 0.285 /
        # 0.20-ish with 0.0 silence recall. Gate well above random, below measured.
        assert acc >= 0.50, f"deployed style head val acc {acc:.3f} < 0.50 (looks untrained)"
        assert f1 >= 0.50, f"deployed style head val macro-F1 {f1:.3f} < 0.50 (looks untrained)"

    @pytest.mark.skipif(not _HAS_ORT or not _ONNX_PATH.is_file(),
                        reason="onnxruntime/model not present")
    def test_style_head_silence_recall_nonzero(self) -> None:
        """The old random head NEVER predicted silence (0.0 recall). A trained
        head must classify real silence windows correctly at least sometimes."""
        import onnxruntime as ort

        Xp = np.load(_DATA_DIR / "X.npy")
        yp = np.load(_DATA_DIR / "y.npy")
        from dataset_split import load_split_indices as _lsi
        pfrozen = _lsi(_DATA_DIR / "meta.csv", len(Xp))
        if pfrozen is None:
            pytest.skip("perception meta.csv missing/stale")
        _, pval_idx, _ = pfrozen
        mask = yp[pval_idx] == 4  # silence class index
        if not mask.any():
            pytest.skip("no silence windows in perception val")

        so = ort.SessionOptions()
        so.intra_op_num_threads = 1
        sess = ort.InferenceSession(str(_ONNX_PATH), sess_options=so,
                                    providers=["CPUExecutionProvider"])
        Xv = Xp[pval_idx][mask].astype(np.float32)
        logits = sess.run(["style_logits"], {"mel": Xv})[0]
        preds = logits.argmax(axis=1)
        silence_recall = float((preds == 4).mean())
        assert silence_recall > 0.5, (
            f"deployed style head silence recall {silence_recall:.3f} — "
            "the head cannot hear silence (random-init symptom)")


class TestShippedCentroidLeakGuard:
    """The deployed centroid table must come from train only."""

    def test_shipped_table_exists(self, deployed) -> None:
        assert deployed["shipped_centroids"] is not None, "groove_centroids.json missing"

    def test_shipped_centroids_match_train_only(self, deployed) -> None:
        """The shipped table must equal the train-only recomputation (leak guard)."""
        train_only = _train_only_centroids(deployed)
        shipped = deployed["shipped_centroids"]
        sims = np.diag(_cosine_sim_matrix(train_only, shipped))
        mean_sim = float(sims.mean())
        assert mean_sim > 0.999, (
            f"shipped centroids deviate from train-only recompute (mean cos {mean_sim:.6f}) — "
            "re-export with training/export_centroids.py")

    def test_shipped_centroids_are_not_all_data(self, deployed) -> None:
        """Guard against reverting to all-data centroids (val baked into the table)."""
        n = deployed["n_classes"]
        dim = deployed["Z"].shape[1]
        sums = np.zeros((n, dim), dtype=np.float64)
        cnt = np.zeros(n)
        for i in range(len(deployed["Z"])):
            sums[deployed["y"][i]] += deployed["Z"][i]
            cnt[deployed["y"][i]] += 1
        all_data = (sums / np.maximum(cnt, 1)[:, None]).astype(np.float32)

        shipped = deployed["shipped_centroids"]
        sim_train = float(np.diag(_cosine_sim_matrix(_train_only_centroids(deployed), shipped)).mean())
        sim_all = float(np.diag(_cosine_sim_matrix(all_data, shipped)).mean())
        # The train-only fix makes the shipped table closer to train-only than
        # to the all-data recomputation (measured: 1.0000 vs 0.9820).
        assert sim_train >= sim_all, (
            f"shipped centroids look all-data (train sim {sim_train:.4f} <= all-data sim {sim_all:.4f})")

    def test_provenance_recorded(self, deployed) -> None:
        prov = deployed["shipped_provenance"] or {}
        assert prov.get("computed_from") == "train_only", (
            f"split_provenance missing/incorrect: {prov}")


class TestOnnxBackboneIntegrity:
    """The shipped ONNX must be the checkpoint we evaluated (no export drift)."""

    @pytest.mark.skipif(not _HAS_ORT or not _ONNX_PATH.is_file(), reason="onnxruntime/model not present")
    def test_onnx_bottleneck_matches_checkpoint(self, deployed) -> None:
        import onnxruntime as ort
        so = ort.SessionOptions()
        so.intra_op_num_threads = 1
        sess = ort.InferenceSession(str(_ONNX_PATH), sess_options=so,
                                    providers=["CPUExecutionProvider"])

        X = deployed["X"][:16]
        ort_bn = sess.run(["bottleneck"], {"mel": X.astype(np.float32)})[0]
        ckpt_bn = deployed["Z"][:16]
        sims = np.diag(_cosine_sim_matrix(ort_bn, ckpt_bn))
        mean_sim = float(sims.mean())
        assert mean_sim > 0.999, (
            f"ONNX bottleneck deviates from checkpoint backbone (mean cos {mean_sim:.6f}) — "
            "re-export assets/metal_groove.onnx from the checkpoint")
