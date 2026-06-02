#!/usr/bin/env python3
"""Build proxy tensors from filtered Lakh MIDI files (Phase 25 DATA-08, updated Phase 32).

Extracts 5-float proxy rows in GMD-compatible format using 3-class state
(SILENT/SOFT/LOUD, per Phase 21 / D-25-01). Assigns oracle labels via
rule-oracle + Breakdown heuristic (Phase 32 label correction). Runs domain
compatibility KS-test vs GMD train tensors (advisory only, per D-25-04).
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from prep_midi import _events_from_file  # noqa: E402

_EPS = 1e-8
_FEATURE_ORDER = [
    "bpm",
    "rmsEnergy",
    "spectralCentroid",
    "highFreqFlux",
    "state_float",
]

_DEFAULT_DATA_DIR = _TRAINING_DIR / "data/lakh"
_DEFAULT_MANIFEST = _DEFAULT_DATA_DIR / "filtered_manifest.jsonl"
_DEFAULT_OUT = _DEFAULT_DATA_DIR / "lakh_tensors.pt"


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _clamp_bpm(bpm: float) -> float:
    return float(np.clip(bpm, 80.0, 220.0))


def _duration_beats(events: list[dict], bpm: float) -> float:
    if not events:
        return 1.0
    t_max = max(float(e.get("time_sec", 0.0)) for e in events)
    return max(t_max * float(bpm) / 60.0, 0.25)


def _compute_proxy_row(events: list[dict], bpm_raw: float) -> np.ndarray:
    """Extract 5-float proxy row with 3-class state (Phase 25 D-25-01)."""
    bpm = _clamp_bpm(float(bpm_raw))
    ons = [e for e in events if e.get("type") == "note_on"]
    vels = [int(e.get("velocity", 0)) for e in ons]
    if not vels:
        return np.array([bpm, 0.0, 9853.0, 0.0, 0.0], dtype=np.float64)

    rms = float(np.sqrt(np.mean(np.square(np.asarray(vels, dtype=np.float64)))) / 127.0)
    notes = [int(e.get("note", 0)) for e in ons]
    mean_note = float(np.mean(notes))

    high_times = sorted(float(e["time_sec"]) for e in ons if int(e.get("note", 0)) >= 42)
    if len(high_times) < 2:
        hf = 0.0
    else:
        dt = np.diff(np.asarray(high_times, dtype=np.float64))
        dt_beats = dt * (float(bpm) / 60.0)
        hf = float(min(1.0, 4.0 * float(np.std(dt_beats))))

    dens = len(ons) / _duration_beats(events, bpm)

    if len(ons) < 3 or rms < 0.02:
        state = 0.0
        cent = 9853.0
    elif dens >= 10.0 and hf >= 0.35:
        state = 2.0
        cent = 8750.0 + (mean_note / 127.0) * 2450.0
    else:
        state = 1.0
        cent = 8750.0 + (mean_note / 127.0) * 2450.0

    return np.array([bpm, rms, cent, hf, state], dtype=np.float64)


_K_SOFT_MID_BPM  = 120.0   # mirrors kSoftMidBpmThreshold from src/inference/pattern_rules.h
_K_SOFT_LOUD_BPM = 160.0   # mirrors kSoftLoudBpmThreshold
_INTENSITY_VARIANTS = (0.0, 0.5, 1.0)


def _adjusted_bpm(raw_bpm: float, policy_intensity: float) -> float:
    return _clamp_bpm(raw_bpm + (policy_intensity - 0.5) * 40.0)


def _rule_pattern_for_state(bpm: float, state_float: float, policy_intensity: float = 0.5) -> int:
    adj_bpm = _adjusted_bpm(bpm, policy_intensity)
    state = min(int(state_float), 2)
    if state == 0: return 0
    elif state == 1:
        if adj_bpm < _K_SOFT_MID_BPM: return 1
        if adj_bpm < _K_SOFT_LOUD_BPM: return 2
        return 3
    elif state == 2:
        return 4 if adj_bpm < _K_SOFT_LOUD_BPM else 5
    return 0


def _oracle_label(
    bpm: float,
    state_float: float,
    events: list[dict],
    policy_intensity: float = 0.5,
) -> int:
    label = _rule_pattern_for_state(bpm, state_float, policy_intensity=policy_intensity)
    if label != 0:
        ons = [e for e in events if e.get("type") == "note_on"]
        dens = len(ons) / _duration_beats(events, bpm)
        if dens < 2.5 and bpm < 110.0:
            label = 6
    return label


def main() -> int:
    repo_root = _repo_root()

    parser = argparse.ArgumentParser(
        description="Build Lakh proxy tensors (Phase 25 DATA-08)."
    )
    parser.add_argument(
        "--manifest",
        type=Path,
        default=_DEFAULT_MANIFEST,
        help="Filtered manifest JSONL (default: training/data/lakh/filtered_manifest.jsonl)",
    )
    parser.add_argument(
        "--out",
        type=Path,
        default=_DEFAULT_OUT,
        help="Output .pt file (default: training/data/lakh/lakh_tensors.pt)",
    )
    parser.add_argument(
        "--max-examples",
        type=int,
        default=None,
        help="Limit examples for dry-run (default: process all)",
    )
    args = parser.parse_args()

    if not args.manifest.exists():
        print(f"error: manifest not found: {args.manifest}", file=sys.stderr)
        print("hint: run filter_lakh.py first (requires download_lakh.py)", file=sys.stderr)
        return 1

    # ── Load manifest ─────────────────────────────────────────────────────
    manifest: list[dict] = []
    with args.manifest.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                manifest.append(json.loads(line))

    print(f"Manifest: {len(manifest)} filtered files", file=sys.stderr)

    midi_dir = args.manifest.parent / "lmd_matched"

    # ── Extract proxies ───────────────────────────────────────────────────
    xs: list[np.ndarray] = []
    Y_list: list[int] = []
    ids: list[str] = []
    failed = 0

    total = len(manifest)
    if args.max_examples is not None:
        total = min(total, args.max_examples)

    for i, entry in enumerate(manifest):
        if args.max_examples is not None and i >= args.max_examples:
            break

        if (i + 1) % 100 == 0:
            print(f"  Processed {i + 1}/{total} ...", file=sys.stderr)

        rel_path = entry["path"]
        bpm = float(entry["bpm"])
        mid_path = midi_dir / rel_path

        if not mid_path.exists():
            failed += 1
            continue

        try:
            events = _events_from_file(mid_path)
        except Exception:
            failed += 1
            continue

        row = _compute_proxy_row(events, bpm)
        raw_bpm = float(row[0])
        state_float = float(row[4])

        for pi in _INTENSITY_VARIANTS:
            variant = row.copy()
            variant[0] = _adjusted_bpm(raw_bpm, pi)
            y = _oracle_label(raw_bpm, state_float, events, policy_intensity=pi)
            xs.append(variant)
            Y_list.append(y)
            ids.append(rel_path)

    if not xs:
        print("error: no valid examples extracted from filtered manifest", file=sys.stderr)
        return 1

    X = np.stack(xs, axis=0).astype(np.float32)
    Y_all = np.asarray(Y_list, dtype=np.int64)

    print(f"\nLAKH TENSORS: {len(xs)} examples, shape {X.shape}", flush=True)
    print(f"  Failed/not found: {failed}", flush=True)

    # ── Save ──────────────────────────────────────────────────────────────
    args.out.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "X": torch.from_numpy(X),
            "Y": torch.from_numpy(Y_all),
            "ids": ids,
            "meta": {"source": "lakh_lmd_matched"},
        },
        str(args.out),
    )
    print(f"  Saved: {args.out}", flush=True)

    # ── Histogram (Lakh Y distribution — advisory) ─────────────────────────
    counts_lakh = np.bincount(Y_all, minlength=7)
    print("HISTOGRAM (Lakh Y distribution — advisory):", flush=True)
    for i in range(7):
        print(f"  class {i}: {int(counts_lakh[i])}", flush=True)

    # ── Domain compatibility (advisory KS-test, D-25-04) ──────────────────
    gmd_train_path = _TRAINING_DIR / "data/processed/train.pt"
    gmd_norm_path = _TRAINING_DIR / "data/processed/norm_stats.json"

    if gmd_train_path.exists() and gmd_norm_path.exists():
        try:
            from scipy import stats  # noqa: PLC0415
        except ImportError:
            print("\nSKIP: scipy not available for KS-test domain check", flush=True)
            return 0

        gmd_data = torch.load(str(gmd_train_path), map_location="cpu", weights_only=True)
        X_gmd_norm = gmd_data["X"].numpy()

        with gmd_norm_path.open("r", encoding="utf-8") as f:
            norm = json.load(f)
        gmd_mean = np.asarray(norm["mean"], dtype=np.float64)
        gmd_std = np.asarray(norm["std"], dtype=np.float64)

        # De-normalize GMD
        X_gmd_raw = X_gmd_norm * gmd_std + gmd_mean

        # KS-test per feature
        print("\nDOMAIN COMPATIBILITY (KS-test per feature vs GMD train):", flush=True)
        feature_names = _FEATURE_ORDER
        for j, name in enumerate(feature_names):
            ks_stat, p_val = stats.ks_2samp(X_gmd_raw[:, j], X[:, j])
            flag = " [WARN p<0.01]" if p_val < 0.01 else ""
            print(f"  {name:20s}: KS={ks_stat:.4f}  p={p_val:.4f}{flag}", flush=True)
        print("DOMAIN COMPAT: advisory check complete", flush=True)
    else:
        print(
            "\nSKIP: GMD train.pt or norm_stats.json not found — domain check skipped",
            flush=True,
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
