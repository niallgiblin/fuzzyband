#!/usr/bin/env python3
"""Filter Lakh lmd_matched MIDI files by channel-10 drum presence and BPM [80,220] (Phase 25 DATA-07)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import mido

_TRAINING_DIR = Path(__file__).resolve().parent
_MIN_BPM = 80.0
_MAX_BPM = 220.0
_MIN_FILTERED = 500
_DEFAULT_DATA_DIR = _TRAINING_DIR / "data/lakh"
_DEFAULT_OUTPUT = _DEFAULT_DATA_DIR / "filtered_manifest.jsonl"


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _extract_bpm(mid_path: Path) -> float:
    """Extract BPM from MIDI tempo events. Default 120 if no set_tempo found."""
    try:
        mid = mido.MidiFile(str(mid_path))
        for msg in mid:
            if msg.type == "set_tempo":
                return 60_000_000.0 / msg.tempo
    except Exception:
        pass
    return 120.0


def _has_drum_track(mid_path: Path) -> bool:
    """Check if MIDI file has note_on events on channel 10 (mido channel 9)."""
    try:
        mid = mido.MidiFile(str(mid_path))
        for msg in mid:
            if msg.type == "note_on" and msg.channel == 9:
                return True
    except Exception:
        pass
    return False


def main() -> int:
    repo_root = _repo_root()

    parser = argparse.ArgumentParser(
        description="Filter Lakh lmd_matched by channel-10 + BPM [80,220] (Phase 25 DATA-07)."
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=_DEFAULT_DATA_DIR,
        help="Directory containing lmd_matched/ (default: training/data/lakh)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=_DEFAULT_OUTPUT,
        help="Output manifest JSONL (default: training/data/lakh/filtered_manifest.jsonl)",
    )
    args = parser.parse_args()

    # Path traversal guard
    resolved_data = args.data_dir.resolve()
    training_root = (repo_root / "training").resolve()
    try:
        resolved_data.relative_to(training_root)
    except ValueError:
        print(
            f"error: --data-dir must resolve under {training_root}, got {resolved_data}",
            file=sys.stderr,
        )
        return 1

    midi_dir = resolved_data / "lmd_matched"
    if not midi_dir.is_dir():
        print(
            f"error: {midi_dir} not found — run download_lakh.py first",
            file=sys.stderr,
        )
        return 1

    midi_files = sorted(midi_dir.rglob("*.mid"))
    print(f"Scanning {len(midi_files)} MIDI files ...", file=sys.stderr)

    filtered: list[dict] = []
    no_drums = 0
    bpm_oob = 0
    corrupt = 0

    for i, fpath in enumerate(midi_files):
        if (i + 1) % 1000 == 0:
            print(f"  ... {i + 1}/{len(midi_files)}", file=sys.stderr)

        # Channel-10 check
        if not _has_drum_track(fpath):
            no_drums += 1
            continue

        # BPM check
        bpm = _extract_bpm(fpath)
        if bpm < _MIN_BPM or bpm > _MAX_BPM:
            bpm_oob += 1
            continue

        # Compute relative path from midi_dir
        rel_path = str(fpath.relative_to(midi_dir))
        filtered.append({"path": rel_path, "bpm": round(bpm, 2)})

    # ── Write manifest ────────────────────────────────────────────────────
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8") as f:
        for entry in filtered:
            f.write(json.dumps(entry, sort_keys=True) + "\n")

    # ── Log ───────────────────────────────────────────────────────────────
    filtered_count = len(filtered)
    print(f"\nSCANNED:  {len(midi_files)}", flush=True)
    print(f"FILTERED: {filtered_count}", flush=True)
    print(f"REJECTED: no-drums={no_drums}  bpm-oob={bpm_oob}  corrupt={corrupt}", flush=True)

    # ── Gate ──────────────────────────────────────────────────────────────
    if filtered_count < _MIN_FILTERED:
        print(
            f"FAIL: filtered count {filtered_count} < {_MIN_FILTERED} minimum",
            flush=True,
        )
        return 2

    print(f"PASS: filtered {filtered_count} >= {_MIN_FILTERED}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
