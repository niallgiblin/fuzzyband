#!/usr/bin/env python3
"""Extract BassNet training data from Lakh+GMD MIDI files (D-26-01).

Parses MIDI files via `prep_midi._events_from_file`, detects bass tracks
(channel 2 with GM bass program 33–40), quantizes to 16-step piano-roll grids,
derives root pitch per bar, and extracts 7-float feature proxies per bar.

Output: bass_train.pt / bass_val.pt dicts with X [N,7], Y [N,32].
"""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

import mido
import numpy as np
import torch

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from build_dataset import _compute_proxy_row  # noqa: E402
from prep_midi import _events_from_file  # noqa: E402

_BASS_GM_PROGRAMS = frozenset(range(33, 41))  # GM bass instruments
_ROOT_CANDIDATES = [40, 45, 35]
_DEFAULT_BPM = 120.0
_EPS = 1e-8


def _clamp_bpm(bpm: float) -> float:
    return float(np.clip(bpm, 80.0, 220.0))


def _first_bpm_from_midi_file(path: Path) -> float:
    """First set_tempo in merged tracks → BPM, else default."""
    try:
        mid = mido.MidiFile(str(path), clip=True)
    except (OSError, EOFError, ValueError):
        return _DEFAULT_BPM
    for msg in mido.merge_tracks(mid.tracks):
        if msg.type == "set_tempo":
            return _clamp_bpm(60000000.0 / float(msg.tempo))
    return _DEFAULT_BPM


def _program_at_first_ch2_note(path: Path) -> int | None:
    """GM program number on melodic channel 2 active at first MIDI note-on on that channel.

    MIDI channel 2 (human 1-indexed) ⇒ ``mido`` ``channel == 1``. Returns ``None``
    if a note fires before any ``program_change`` on that channel, or on corrupt files.
    """
    try:
        mid = mido.MidiFile(str(path), clip=True)
    except (OSError, EOFError, ValueError):
        return None
    prog: int | None = None
    for msg in mido.merge_tracks(mid.tracks):
        if msg.type == "program_change" and getattr(msg, "channel", None) == 1:
            prog = int(msg.program)
        elif msg.type == "note_on" and getattr(msg, "channel", None) == 1 and getattr(msg, "velocity", 0) > 0:
            return prog
    return None


def _events_in_time_range(events: list[dict], t0: float, t1: float) -> list[dict]:
    out = []
    for e in events:
        t = float(e.get("time_sec", 0.0))
        if t0 <= t < t1:
            out.append(e)
    return out


def extract_bass_events(events: list[dict], channel: int = 2) -> list[dict]:
    """Bass notes as {pitch, velocity, start_sec, end_sec, channel}."""

    pending: dict[int, dict[str, float | int]] = {}
    finished: list[dict] = []

    for e in events:
        if int(e.get("channel", 0)) != channel:
            continue

        et = e.get("type")
        if et == "note_on" and int(e.get("velocity", 0)) > 0:
            pitch = int(e.get("note", 0))
            pending[pitch] = {
                "pitch": pitch,
                "velocity": float(e.get("velocity", 0)),
                "start_sec": float(e.get("time_sec", 0.0)),
                "channel": channel,
                "end_sec": float(e.get("time_sec", 0.0)),
            }
        elif et in ("note_off",) or (et == "note_on" and int(e.get("velocity", 0)) == 0):
            pitch = int(e.get("note", 0))
            if pitch in pending:
                st = pending.pop(pitch)
                st["end_sec"] = float(e.get("time_sec", float(st["start_sec"]) + 0.1))
                finished.append(
                    {
                        "pitch": int(st["pitch"]),
                        "velocity": int(st["velocity"]),
                        "start_sec": float(st["start_sec"]),
                        "end_sec": float(st["end_sec"]),
                        "channel": channel,
                    }
                )

    for pitch, st in pending.items():
        finished.append(
            {
                "pitch": pitch,
                "velocity": int(st["velocity"]),
                "start_sec": float(st["start_sec"]),
                "end_sec": float(st["start_sec"]) + 0.25,
                "channel": channel,
            }
        )

    finished.sort(key=lambda n: float(n["start_sec"]))
    return finished


def _derive_root_from_events(
    bass_notes: list[dict],
    candidates: list[int] | None = None,
) -> int:
    """Pick E2/A2/B1 candidate minimizing sum of circular pitch-class distance."""
    if not bass_notes:
        return _ROOT_CANDIDATES[0]

    cand_list = candidates or _ROOT_CANDIDATES

    def pc_dist_deg(p: int, c: int) -> float:
        a, b = p % 12, c % 12
        d = abs(a - b)
        return float(min(d, 12 - d))

    pitches = [int(n["pitch"]) for n in bass_notes]
    best = cand_list[0]
    best_sum = math.inf

    for candidate in cand_list:
        s = sum(pc_dist_deg(p, candidate) for p in pitches)
        if s < best_sum:
            best_sum = s
            best = candidate

    return best


def _infer_state_drums(bar_events_global: list[dict]) -> float:
    """LOUD≥3 drum onsets, SOFT≥1, else SILENT in this bar."""
    drums = [
        e
        for e in bar_events_global
        if e.get("type") == "note_on"
        and int(e.get("velocity", 0)) > 0
        and int(e.get("channel", 0)) == 10
    ]
    n = len(drums)
    if n >= 3:
        return 2.0
    if n >= 1:
        return 1.0
    return 0.0


def _quantize_bass_to_pianoroll(
    bass_notes: list[dict],
    root: int,
    bpm: float = 120.0,
    start_beat: float = 0.0,
    steps_per_bar: int = 16,
    beats_per_bar: int = 4,
) -> tuple[np.ndarray, np.ndarray]:
    beat_duration_sec = 60.0 / float(bpm)
    step_duration_sec = beat_duration_sec * (float(beats_per_bar) / float(steps_per_bar))
    bar_start_sec = start_beat * beat_duration_sec
    bar_end_sec = bar_start_sec + beat_duration_sec * float(beats_per_bar)

    pitch_offsets = np.zeros(steps_per_bar, dtype=np.float32)
    velocities = np.zeros(steps_per_bar, dtype=np.float32)

    for step in range(steps_per_bar):
        step_start = bar_start_sec + step * step_duration_sec
        step_end = step_start + step_duration_sec

        active = []
        for note in bass_notes:
            ns = float(note.get("start_sec", 0.0))
            if ns >= step_start and ns < step_end:
                active.append(note)

        if not active:
            continue

        best_note = active[0]
        best_dist = abs(int(best_note["pitch"]) - root)
        for n in active[1:]:
            d = abs(int(n["pitch"]) - root)
            if d < best_dist:
                best_dist = d
                best_note = n

        offset = float(int(best_note["pitch"]) - root)
        pitch_offsets[step] = float(np.clip(offset, -12.0, 12.0))
        velocities[step] = float(int(best_note["velocity"])) / 127.0

    return pitch_offsets, velocities


def build_bass_rows_for_file(midi_path: Path, bpm_fallback: float | None = None) -> list[dict]:
    """Return flat list of per-bar records {X row [7], Y row [32], meta dict}."""

    gm_seen = _program_at_first_ch2_note(midi_path)
    if gm_seen is None or gm_seen not in _BASS_GM_PROGRAMS:
        return []

    try:
        events = _events_from_file(midi_path)
    except Exception:
        return []

    if not events:
        return []

    bpm = bpm_fallback if bpm_fallback is not None else _first_bpm_from_midi_file(midi_path)
    bpm = _clamp_bpm(bpm)

    bass_notes_abs = extract_bass_events(events, channel=2)
    if not bass_notes_abs:
        return []

    beat_duration_sec = 60.0 / bpm
    bar_duration_sec = beat_duration_sec * 4.0
    duration_sec = max(float(e.get("time_sec", 0.0)) for e in events)
    total_bars = max(1, int(np.ceil(duration_sec / bar_duration_sec)))

    rows_out: list[dict] = []

    for bar_idx in range(total_bars):
        t0 = bar_idx * bar_duration_sec
        t1 = (bar_idx + 1) * bar_duration_sec

        bar_events_global = _events_in_time_range(events, t0, t1)
        bar_notes = []
        for n in bass_notes_abs:
            ns = float(n["start_sec"])
            if t0 <= ns < t1:
                nn = dict(n)
                nn["start_sec"] = ns - t0
                nn["end_sec"] = float(nn["end_sec"]) - t0
                bar_notes.append(nn)

        if not bar_notes:
            continue

        root_pitch = _derive_root_from_events(bar_notes)

        drum_state = _infer_state_drums(bar_events_global)
        proxy_row = _compute_proxy_row(bar_events_global, bpm).astype(np.float64)
        proxy_row[4] = float(drum_state)

        feature_7 = np.array(
            [
                proxy_row[0],
                proxy_row[1],
                proxy_row[2],
                proxy_row[3],
                proxy_row[4],
                float(root_pitch),
                0.8,
            ],
            dtype=np.float32,
        )

        po, vv = _quantize_bass_to_pianoroll(
            bar_notes,
            root=root_pitch,
            bpm=bpm,
            start_beat=0.0,
        )
        if float(np.max(vv)) < _EPS:
            continue

        y32 = np.empty(32, dtype=np.float32)
        for s in range(16):
            y32[s * 2] = po[s]
            y32[s * 2 + 1] = vv[s]

        rows_out.append({
            "X": feature_7,
            "Y": y32,
            "meta": {
                "midi_path": str(midi_path.resolve()),
                "root_pitch": int(root_pitch),
                "gm_program_ch2_seen": int(gm_seen),
                "bpm": float(bpm),
                "bar_index": bar_idx,
            },
        })

    return rows_out


def build_bass_dataset(
    midi_dir: Path,
    max_files: int = 5000,
    seed: int = 42,
    val_fraction: float = 0.2,
    bpm_override: float | None = None,
) -> tuple[dict, dict]:
    """Load MIDI directory, aggregate rows, split train/val by source file."""

    midi_files: list[Path] = []
    for ext in ("*.mid", "*.midi", "*.MID", "*.MIDI"):
        midi_files.extend(sorted(midi_dir.rglob(ext)))
    midi_files = sorted(set(midi_files))[:max_files]

    by_file: dict[str, list[dict]] = {}
    for p in midi_files:
        bars = build_bass_rows_for_file(p, bpm_fallback=bpm_override)
        if not bars:
            continue
        key = str(p.resolve())
        by_file.setdefault(key, []).extend(bars)

    if not by_file:
        raise ValueError(f"No usable bass bars from MIDI under {midi_dir}")

    groups = sorted(by_file.keys())

    rng = np.random.default_rng(seed)

    def rows_to_blob(rows: list[dict]) -> dict:
        xs = [r["X"] for r in rows]
        ys = [r["Y"] for r in rows]
        met = [r["meta"] for r in rows]

        X = np.stack(xs, axis=0).astype(np.float32)
        Y = np.stack(ys, axis=0).astype(np.float32)
        return {"X": torch.from_numpy(X), "Y": torch.from_numpy(Y), "meta": met}

    if len(groups) < 2:
        flat: list[dict] = []
        for g in groups:
            flat.extend(by_file[g])

        rng.shuffle(flat)

        if len(flat) == 0:
            raise ValueError("No rows after grouping (empty flat list)")

        if len(flat) == 1:
            return rows_to_blob(flat), {
                "X": torch.zeros(0, 7, dtype=torch.float32),
                "Y": torch.zeros(0, 32, dtype=torch.float32),
                "meta": [],
            }

        split_i = max(1, min(int(len(flat) * (1.0 - val_fraction)), len(flat) - 1))
        return rows_to_blob(flat[:split_i]), rows_to_blob(flat[split_i:])

    rng.shuffle(groups)
    split_at = max(1, int(len(groups) * (1.0 - val_fraction)))
    split_at = min(split_at, len(groups) - 1)

    train_groups = set(groups[:split_at])
    val_groups = set(groups[split_at:])

    def pack(group_set: set[str]) -> dict:
        xs, ys, metas = [], [], []
        for g in group_set:
            for rec in by_file[g]:
                xs.append(rec["X"])
                ys.append(rec["Y"])
                metas.append(rec["meta"])

        X = np.stack(xs, axis=0).astype(np.float32)
        Y = np.stack(ys, axis=0).astype(np.float32)
        return {"X": torch.from_numpy(X), "Y": torch.from_numpy(Y), "meta": metas}

    return pack(train_groups), pack(val_groups)


def main() -> int:
    parser = argparse.ArgumentParser(description="Extract BassNet training tensors (D-26-01).")
    parser.add_argument("--midi-dir", type=Path, required=True, help="Root directory of .mid/.midi files.")
    parser.add_argument(
        "--processed-dir",
        type=Path,
        default=_TRAINING_DIR / "data/processed",
        help="Directory for bass_train.pt and bass_val.pt (default: training/data/processed)",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="Directory with norm_stats.json (documentation only)",
    )
    parser.add_argument("--max-files", type=int, default=5000)
    parser.add_argument("--val-fraction", type=float, default=0.2)
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--bpm", type=float, default=None)
    args = parser.parse_args()

    if not args.midi_dir.is_dir():
        print(f"error: --midi-dir not found: {args.midi_dir}", file=sys.stderr)
        return 1

    try:
        train_blob, val_blob = build_bass_dataset(
            midi_dir=args.midi_dir,
            max_files=args.max_files,
            seed=args.seed,
            val_fraction=args.val_fraction,
            bpm_override=args.bpm,
        )
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    args.processed_dir.mkdir(parents=True, exist_ok=True)
    train_pt = args.processed_dir / "bass_train.pt"
    val_pt = args.processed_dir / "bass_val.pt"

    torch.save(train_blob, str(train_pt))
    torch.save(val_blob, str(val_pt))

    print(f"Saved: {train_pt}  X shape: {tuple(train_blob['X'].shape)}", flush=True)
    print(f"Saved: {val_pt}  X shape: {tuple(val_blob['X'].shape)}", flush=True)

    if args.data_dir is not None:
        stats = args.data_dir / "norm_stats.json"
        if stats.is_file():
            print(f"norm_stats.json reference OK: {stats}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
