#!/usr/bin/env python3
"""Tier-1 (v1): reduce GMD drum MIDI to a groove-renderer training set.

For each 4/4 steady "beat" take in the Groove MIDI Dataset, split into 1-bar
windows and produce aligned arrays:
    score     [N, 10, 16]  float 0/1   (quantized hit grid)
    velocity  [N, 10, 16]  float [0,1] (1.0 = MIDI 127)
    offset    [N, 10, 16]  float       (fraction of a 16th note, tanh target)
    condition [N, 18]      float       (bpm + genre one-hot; audio features neutral)

GMD is MIDI-only, so energy/centroid/density/style/state are held at 0 (neutral);
the model conditions on bpm + genre + (implicitly, through z) the groove
distribution. The 10-voice grid matches the Tier-1 contract (voice mapping in
docs/TIER1_GROOVE_MODEL_CONTRACT.md §2.1).

Outputs:
    data/processed/groove_render.npz
    data/processed/groove_render_meta.csv   (drummer, session, bpm, genre)
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
import mido

_REPO_ROOT = Path(__file__).resolve().parents[1]
_TRAINING_DIR = _REPO_ROOT / "training"
_DEFAULT_GMD_ROOT = _TRAINING_DIR / "data/tfds/downloads/extracted"
_DEFAULT_OUT = _REPO_ROOT / "data/processed/groove_render.npz"
_DEFAULT_META = _REPO_ROOT / "data/processed/groove_render_meta.csv"

NUM_VOICES = 10
STEPS = 16
COND_DIM = 18

# GM note -> voice index (matches the C++ GrooveGrid voice mapping).
_NOTE_TO_VOICE = {
    35: 0, 36: 0,           # kick
    38: 1, 40: 1,           # snare
    42: 2,                  # hat_closed
    46: 3,                  # hat_open
    51: 4,                  # ride
    53: 5,                  # ride_bell
    49: 6, 52: 6, 55: 6,    # crash / china / splash -> crash
    48: 7,                  # tom_hi
    45: 8,                  # tom_mid
    41: 9,                  # tom_lo
}


def _genre_id(style: str) -> int:
    """Coarse GMD primary-style -> plugin genre id (0 Rock, 2 Punk, 3 Metal)."""
    s = style.split("/", 1)[0].strip().lower()
    if "metal" in s:
        return 3
    if "punk" in s:
        return 2
    return 0  # rock + everything else -> Rock


def _find_info_csv(gmd_root: Path) -> Path:
    matches = sorted(gmd_root.rglob("info.csv"))
    if not matches:
        raise SystemExit(
            "error: no GMD info.csv — run `python3 training/download_gmd.py` first."
        )
    return matches[0]


def _iter_gmd_rows(info_csv: Path):
    base = info_csv.parent
    with info_csv.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("time_signature", "").strip() != "4-4":
                continue
            if row.get("beat_type", "").strip() != "beat":
                continue
            midi_rel = row.get("midi_filename", "").strip()
            if not midi_rel:
                continue
            midi_path = base / midi_rel
            if not midi_path.is_file():
                continue
            try:
                bpm = float(row.get("bpm", "") or 0.0)
            except ValueError:
                bpm = 0.0
            if bpm <= 0.0:
                continue
            style = row.get("style", "").strip()
            yield midi_path, midi_rel, bpm, style


def _hits_from_midi(midi_path: Path):
    """Return [(beat, velocity, note)] for channel-10 note-on hits."""
    try:
        mid = mido.MidiFile(str(midi_path))
    except Exception:
        return []
    tpb = mid.ticks_per_beat or 480
    hits: list[tuple[float, int, int]] = []
    for track in mid.tracks:
        abs_tick = 0
        for msg in track:
            abs_tick += msg.time
            if msg.type == "note_on" and msg.velocity > 0 and msg.channel == 9:
                hits.append((abs_tick / tpb, int(msg.velocity), int(msg.note)))
    return hits


def _take_to_windows(hits, bpm: int):
    """Split a take's hits into 1-bar windows -> list of (score, vel, off, bar).

    off is a tempo-independent *fraction of a 16th note* (tanh target, [-0.5, 0.5]).
    """
    # window_id -> { (voice, step): (velocity, offset_fraction) }
    windows: dict[int, dict] = defaultdict(dict)
    for beat, vel, note in hits:
        voice = _NOTE_TO_VOICE.get(note)
        if voice is None:
            continue
        window = int(beat // 4.0)
        beat_in_window = beat - 4.0 * window
        step = int(round(beat_in_window * 4.0))
        if step == 16:
            window += 1
            beat_in_window -= 4.0
            step = 0
        offset = (beat_in_window - step / 4.0) * 4.0  # fraction of a 16th note
        key = (voice, step)
        cur = windows[window].get(key)
        if cur is None or vel > cur[0]:
            windows[window][key] = (vel, offset)

    out = []
    for window, cells in windows.items():
        score = np.zeros((NUM_VOICES, STEPS), dtype=np.float32)
        vel = np.zeros((NUM_VOICES, STEPS), dtype=np.float32)
        off = np.zeros((NUM_VOICES, STEPS), dtype=np.float32)
        for (voice, step), (v, o) in cells.items():
            score[voice, step] = 1.0
            vel[voice, step] = v / 127.0
            off[voice, step] = o
        out.append((score, vel, off, window))
    return out


def _condition(bpm: int, genre_id: int) -> np.ndarray:
    c = np.zeros(COND_DIM, dtype=np.float32)
    c[0] = np.clip((bpm - 40.0) / 260.0, 0.0, 1.0)
    c[12 + genre_id] = 1.0  # genre one-hot at indices 12..16
    # energy/centroid/density/style/state/phase left at 0 (neutral).
    return c


def main() -> int:
    parser = argparse.ArgumentParser(description="GMD -> groove-renderer training set.")
    parser.add_argument("--gmd-root", type=Path, default=_DEFAULT_GMD_ROOT)
    parser.add_argument("--out", type=Path, default=_DEFAULT_OUT)
    parser.add_argument("--meta", type=Path, default=_DEFAULT_META)
    parser.add_argument("--max-files", type=int, default=None,
                        help="Cap GMD files processed (smoke tests).")
    args = parser.parse_args()

    info_csv = _find_info_csv(args.gmd_root)
    print(f"GMD info.csv: {info_csv}", file=sys.stderr)

    scores, vels, offs, conds = [], [], [], []
    meta_rows: list[tuple[str, str, int, int]] = []
    files = 0
    for midi_path, midi_rel, bpm, style in _iter_gmd_rows(info_csv):
        if args.max_files is not None and files >= args.max_files:
            break
        hits = _hits_from_midi(midi_path)
        if not hits:
            continue
        parts = Path(midi_rel).parts
        drummer = parts[0] if len(parts) > 0 else "?"
        session = parts[1] if len(parts) > 1 else "?"
        genre = _genre_id(style)
        for score, vel, off, _bar in _take_to_windows(hits, int(round(bpm))):
            scores.append(score)
            vels.append(vel)
            offs.append(off)
            conds.append(_condition(int(round(bpm)), genre))
            meta_rows.append((drummer, session, int(round(bpm)), genre))
        files += 1
        if files % 100 == 0:
            print(f"  ... {files} files, {len(scores)} windows", file=sys.stderr)

    if not scores:
        print("FAIL: no windows produced.", file=sys.stderr)
        return 2

    # ── v2: velocity as a *multiplier*, not absolute ─────────────────────────
    # The model should humanise the authored pattern velocity, not replace it.
    # Target = vel / mean_vel(voice), so the model learns "this hit is x% louder
    # than this voice's typical" (~1.0, centered). At runtime the C++ multiplies
    # the authored velocity by this multiplier. Offset stays absolute (a fraction
    # of a 16th) — the authored patterns have no microtiming to preserve.
    vels = np.stack(vels).astype(np.float32)          # [N, 10, 16] absolute 0..1
    scores_arr = np.stack(scores).astype(np.float32)  # [N, 10, 16] 0/1
    mean_vel = np.ones(NUM_VOICES, dtype=np.float32)
    for v in range(NUM_VOICES):
        mask = scores_arr[:, v, :] > 0.5
        if mask.sum() > 0:
            mean_vel[v] = vels[:, v, :][mask].mean().clip(min=0.05)
    vels = vels / mean_vel[None, :, None]             # multiplier ~1.0
    vels = np.clip(vels, 0.2, 3.0).astype(np.float32)

    args.out.parent.mkdir(parents=True, exist_ok=True)
    np.savez(
        args.out,
        score=scores_arr,
        velocity=vels,                                 # velocity *multiplier*
        offset=np.stack(offs).astype(np.float32),
        condition=np.stack(conds).astype(np.float32),
    )
    with args.meta.open("w", encoding="utf-8", newline="") as f:
        w = csv.writer(f)
        w.writerow(["drummer", "session", "bpm", "genre"])
        w.writerows(meta_rows)

    # Provenance manifest alongside the data.
    manifest = {
        "source": "Groove MIDI Dataset (GMD) v1.0.0, CC-BY 4.0",
        "generator": "training/build_groove_render_dataset.py",
        "num_windows": len(scores),
        "num_files": files,
        "voices": NUM_VOICES,
        "steps": STEPS,
        "condition_dim": COND_DIM,
    }
    manifest_path = args.out.with_suffix(".json")
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.out} ({len(scores)} windows from {files} files)", flush=True)
    print(f"Wrote {args.meta}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
