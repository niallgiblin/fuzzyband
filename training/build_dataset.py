#!/usr/bin/env python3
"""Build train/val tensors from GMD + MIDI proxies (Phase 17 DATA-06)."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

import numpy as np
import torch

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from prep_midi import _events_from_file  # noqa: E402

_DATASET_SPEC = "groove/full-midionly:2.0.1"
_DEFAULT_DATA_DIR = _TRAINING_DIR / "data/tfds"
_DEFAULT_OUT = _TRAINING_DIR / "data/processed"
_ALPHA = 0.5
_EPS = 1e-8
_FEATURE_ORDER = [
    "bpm",
    "rmsEnergy",
    "spectralCentroid",
    "highFreqFlux",
    "state_float",
]


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _validate_under_training(path: Path) -> Path:
    resolved = path.resolve()
    training_root = (_repo_root() / "training").resolve()
    resolved.relative_to(training_root)
    return resolved


def _clamp_bpm(bpm: float) -> float:
    return float(np.clip(bpm, 80.0, 220.0))


def _duration_beats(events: list[dict], bpm: float) -> float:
    if not events:
        return 1.0
    t_max = max(float(e.get("time_sec", 0.0)) for e in events)
    return max(t_max * float(bpm) / 60.0, 0.25)


def _compute_proxy_row(events: list[dict], bpm_raw: float) -> np.ndarray:
    bpm = _clamp_bpm(float(bpm_raw))
    ons = [e for e in events if e.get("type") == "note_on"]
    vels = [int(e.get("velocity", 0)) for e in ons]
    if not vels:
        rms = 0.0
        cent = 0.0
        hf = 0.0
        state = 0.0
        return np.array([bpm, rms, cent, hf, state], dtype=np.float64)

    rms = float(np.sqrt(np.mean(np.square(np.asarray(vels, dtype=np.float64)))) / 127.0)
    notes = [int(e.get("note", 0)) for e in ons]
    cent = 400.0 + (float(np.mean(notes)) / 127.0) * 6000.0

    high_times = sorted(float(e["time_sec"]) for e in ons if int(e.get("note", 0)) >= 42)
    if len(high_times) < 2:
        hf = 0.0
    else:
        dt = np.diff(np.asarray(high_times, dtype=np.float64))
        dt_beats = dt * (float(bpm) / 60.0)
        hf = float(min(1.0, 4.0 * float(np.std(dt_beats))))

    dens = len(ons) / _duration_beats(events, bpm)
    kick_hits = sum(1 for e in ons if int(e.get("note", 0)) == 36)

    if len(ons) < 3 or rms < 0.02:
        state = 0.0
    elif dens >= 10.0 and hf >= 0.35:
        state = 2.0
    elif dens <= 2.5 and kick_hits <= 1:
        state = 3.0
    else:
        state = 1.0

    return np.array([bpm, rms, cent, hf, state], dtype=np.float64)


def _heuristic_class_guess(events: list[dict], row: np.ndarray) -> int:
    ons = [e for e in events if e.get("type") == "note_on"]
    n = len(ons)
    bpm = float(row[0])
    rms = float(row[1])
    hf = float(row[3])
    dens = n / _duration_beats(events, bpm)

    if n < 3 or rms < 0.02:
        return 0
    if dens < 5.5:
        return 1
    if dens < 10.5:
        return 2
    if dens < 16.0:
        return 3
    if dens < 24.0:
        return 4 if hf < 0.45 else 5
    if hf < 0.4 and dens < 30.0:
        return 6
    return 5


def _l2_normalize(v: np.ndarray) -> np.ndarray:
    n = float(np.linalg.norm(v))
    if n < _EPS:
        return v * 0.0
    return v / max(n, _EPS)


def _cosine(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.dot(a, b) / (max(np.linalg.norm(a), _EPS) * max(np.linalg.norm(b), _EPS)))


def _build_seed_midi_bytes(seed_idx: int) -> bytes:
    import io

    import mido

    mid = mido.MidiFile(type=0)
    mid.ticks_per_beat = 480
    tr = mido.MidiTrack()
    mid.tracks.append(tr)
    tr.append(mido.MetaMessage("set_tempo", tempo=500000, time=0))
    tp = 480

    def add_note(on_delta: int, pitch: int, vel: int, hold: int) -> None:
        tr.append(mido.Message("note_on", channel=9, note=pitch, velocity=vel, time=on_delta))
        tr.append(mido.Message("note_off", channel=9, note=pitch, velocity=0, time=hold))

    if seed_idx == 0:
        pass
    elif seed_idx == 1:
        add_note(0, 36, 110, tp)
        add_note(0, 38, 105, tp)
        add_note(0, 42, 80, tp)
        add_note(0, 38, 98, tp)
    elif seed_idx == 2:
        for i in range(8):
            pitch = [36, 42, 38, 42][i % 4]
            vel = 105 if pitch == 38 else 100
            add_note(0, pitch, vel, tp // 2)
    elif seed_idx == 3:
        for i in range(16):
            pitch = [36, 42, 38, 42][i % 4]
            add_note(0, pitch, 105 - (i % 3), tp // 4)
    elif seed_idx == 4:
        for i in range(8):
            pitch = [36, 46, 38, 46][i % 4]
            add_note(0, pitch, 118, tp // 2)
    elif seed_idx == 5:
        for i in range(32):
            pitch = 36 if i % 2 == 0 else (38 if (i // 2) % 4 == 1 else 46)
            add_note(0, pitch, 120, tp // 4)
    else:
        add_note(0, 36, 118, tp)
        add_note(0, 38, 72, tp * 3 // 4)
        add_note(0, 38, 74, tp * 5 // 4)
        add_note(0, 36, 112, tp)
        add_note(0, 38, 70, tp)

    bio = io.BytesIO()
    mid.save(file=bio)
    return bio.getvalue()


def _seed_matrix() -> np.ndarray:
    seeds: list[np.ndarray] = []
    tmp_root = _validate_under_training(_TRAINING_DIR / "data/tmp")
    tmp_root.mkdir(parents=True, exist_ok=True)
    for i in range(7):
        data = _build_seed_midi_bytes(i)
        path = tmp_root / f"seed_{i}.mid"
        path.write_bytes(data)
        ev = _events_from_file(path)
        row = _compute_proxy_row(ev, 120.0)
        seeds.append(row)
    return np.stack(seeds, axis=0)


_SEED_ROWS = None


def _get_seed_rows() -> np.ndarray:
    global _SEED_ROWS
    if _SEED_ROWS is None:
        _SEED_ROWS = _seed_matrix()
    return _SEED_ROWS


def _hybrid_label(row: np.ndarray, events: list[dict]) -> int:
    seeds = _get_seed_rows()
    xh = _l2_normalize(row.astype(np.float64))
    sh = np.stack([_l2_normalize(seeds[i]) for i in range(7)], axis=0)
    cos = np.array([_cosine(xh, sh[i]) for i in range(7)], dtype=np.float64)
    h = _heuristic_class_guess(events, row)
    one = np.zeros(7, dtype=np.float64)
    one[h] = 1.0
    scores = (1.0 - _ALPHA) * cos + _ALPHA * one
    if float(np.max(scores)) < 0.25:
        return 2
    return int(np.argmax(scores))


def _midi_bytes_from_tfds(example: object) -> bytes:
    mid = example["midi"] if isinstance(example, dict) else example.midi
    if hasattr(mid, "numpy"):
        mid = mid.numpy()
    if isinstance(mid, dict):
        raw = mid.get("bytes", mid.get("b"))
        if raw is None:
            raw = next(iter(mid.values()))
        if hasattr(raw, "numpy"):
            raw = raw.numpy()
    else:
        raw = mid
    if isinstance(raw, memoryview):
        raw = raw.tobytes()
    if isinstance(raw, str):
        raw = raw.encode("utf-8")
    return bytes(raw)


def _example_id(example: object) -> str:
    eid = example["id"] if isinstance(example, dict) else example.id
    if hasattr(eid, "numpy"):
        eid = eid.numpy()
    if isinstance(eid, bytes):
        return eid.decode("utf-8")
    return str(eid)


def _example_bpm(example: object) -> float:
    b = example["bpm"] if isinstance(example, dict) else example.bpm
    if hasattr(b, "numpy"):
        b = b.numpy()
    arr = np.asarray(b).reshape(())
    return float(arr) if arr.size else 120.0


def main() -> int:
    parser = argparse.ArgumentParser(description="GMD → train.pt / val.pt (Phase 17).")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--val-fraction", type=float, default=0.2)
    parser.add_argument("--data-dir", type=Path, default=_DEFAULT_DATA_DIR)
    parser.add_argument("--split", type=str, default="train", help="TFDS split name (default: train).")
    parser.add_argument("--out-dir", type=Path, default=_DEFAULT_OUT)
    parser.add_argument("--max-examples", type=int, default=None)
    args = parser.parse_args()

    try:
        import tensorflow_datasets as tfds  # noqa: PLC0415
    except ImportError as exc:
        print(f"error: tensorflow_datasets import failed: {exc}", file=sys.stderr)
        return 1

    from sklearn.model_selection import GroupShuffleSplit  # noqa: PLC0415

    data_dir = _validate_under_training(args.data_dir)
    out_dir = _validate_under_training(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    ds = tfds.load(
        _DATASET_SPEC,
        data_dir=str(data_dir),
        split=args.split,
        download=True,
    )

    xs: list[np.ndarray] = []
    ys: list[int] = []
    groups: list[str] = []
    ids: list[str] = []

    tmp_root = _validate_under_training(_TRAINING_DIR / "data/tmp")
    tmp_root.mkdir(parents=True, exist_ok=True)

    count = 0
    for ex in ds:
        eid = _example_id(ex)
        bpm = _example_bpm(ex)
        raw = _midi_bytes_from_tfds(ex)
        digest = hashlib.sha256(raw).hexdigest()[:16]
        tmp = tmp_root / f"ex_{digest}.mid"
        tmp.write_bytes(raw)
        events = _events_from_file(tmp)
        row = _compute_proxy_row(events, bpm)
        y = _hybrid_label(row, events)
        gid = eid.split(":", 1)[0] if ":" in eid else eid

        xs.append(row)
        ys.append(y)
        groups.append(gid)
        ids.append(eid)

        count += 1
        if args.max_examples is not None and count >= args.max_examples:
            break

    X = np.stack(xs, axis=0).astype(np.float64)
    Y = np.asarray(ys, dtype=np.int64)
    groups = np.asarray(groups, dtype=object)

    gss = GroupShuffleSplit(
        n_splits=1,
        test_size=args.val_fraction,
        random_state=args.seed,
    )
    tr_idx, va_idx = next(gss.split(X, Y, groups=groups))

    X_train = X[tr_idx]
    Y_train = Y[tr_idx]
    X_val = X[va_idx]
    Y_val = Y[va_idx]

    mean = np.mean(X_train, axis=0)
    std = np.std(X_train, axis=0, ddof=0)
    std = np.maximum(std, _EPS)

    X_train_n = ((X_train - mean) / std).astype(np.float32)
    X_val_n = ((X_val - mean) / std).astype(np.float32)

    norm_stats = {
        "feature_order": _FEATURE_ORDER,
        "mean": mean.astype(float).tolist(),
        "std": std.astype(float).tolist(),
        "epsilon": _EPS,
    }
    (out_dir / "norm_stats.json").write_text(
        json.dumps(norm_stats, indent=2) + "\n",
        encoding="utf-8",
    )

    meta = {"tfds": _DATASET_SPEC, "norm": "train_fit"}
    torch.save(
        {
            "X": torch.from_numpy(X_train_n),
            "Y": torch.from_numpy(Y_train),
            "meta": meta,
        },
        out_dir / "train.pt",
    )
    torch.save(
        {
            "X": torch.from_numpy(X_val_n),
            "Y": torch.from_numpy(Y_val),
            "meta": meta,
        },
        out_dir / "val.pt",
    )

    def _write_manifest(path: Path, idxs: np.ndarray, split: str) -> None:
        with path.open("w", encoding="utf-8") as f:
            for i in idxs:
                rec = {
                    "example_id": ids[int(i)],
                    "group_id": str(groups[int(i)]),
                    "label": int(Y[int(i)]),
                    "split": split,
                }
                f.write(json.dumps(rec, sort_keys=True) + "\n")

    _write_manifest(out_dir / "manifest_train.jsonl", tr_idx, "train")
    _write_manifest(out_dir / "manifest_val.jsonl", va_idx, "val")

    counts = np.bincount(Y_train, minlength=7)
    print("HISTOGRAM (train split labels)")
    for i in range(7):
        print(f"  class {i}: {int(counts[i])}")
    if np.any(counts < 50):
        print(
            "FAIL: HISTOGRAM — at least one class below 50 examples in train split (below 50)",
            flush=True,
        )
        sys.exit(3)
    print("PASS: HISTOGRAM — all classes >= 50 in train split", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
