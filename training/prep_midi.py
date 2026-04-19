#!/usr/bin/env python3
"""Convert a Standard MIDI file to JSON or JSONL event records (see docs/TOKENIZATION.md)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import mido


def _events_from_file(path: Path) -> list[dict]:
    mid = mido.MidiFile(str(path))
    ticks_per_beat = mid.ticks_per_beat
    tempo = 500_000  # default 120 BPM until first set_tempo
    sec = 0.0
    out: list[dict] = []

    for msg in mido.merge_tracks(mid.tracks):
        delta = msg.time
        sec += delta * (tempo / (ticks_per_beat * 1_000_000.0))

        if msg.type == "set_tempo":
            tempo = msg.tempo
        elif msg.type == "note_on":
            if msg.velocity == 0:
                out.append(
                    {
                        "type": "note_off",
                        "time_sec": sec,
                        "channel": msg.channel + 1,
                        "note": msg.note,
                        "velocity": 0,
                    }
                )
            else:
                out.append(
                    {
                        "type": "note_on",
                        "time_sec": sec,
                        "channel": msg.channel + 1,
                        "note": msg.note,
                        "velocity": msg.velocity,
                    }
                )
        elif msg.type == "note_off":
            out.append(
                {
                    "type": "note_off",
                    "time_sec": sec,
                    "channel": msg.channel + 1,
                    "note": msg.note,
                    "velocity": msg.velocity,
                }
            )

    return out


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="MIDI → JSON/JSONL event stream (Phase 9 prep stub).")
    p.add_argument("--input", "-i", required=True, type=Path, help="Path to a .mid file")
    p.add_argument(
        "--output",
        "-o",
        type=Path,
        default=None,
        help="Output file (default: stdout)",
    )
    p.add_argument(
        "--jsonl",
        action="store_true",
        help="Write JSON Lines (one object per line) instead of a JSON array",
    )
    args = p.parse_args(argv)

    if not args.input.is_file():
        print(f"error: input not found: {args.input}", file=sys.stderr)
        return 1

    events = _events_from_file(args.input)

    if args.jsonl:
        body = "".join(json.dumps(e, sort_keys=True) + "\n" for e in events)
    else:
        body = json.dumps(events, indent=2, sort_keys=True) + "\n"

    if args.output is None:
        sys.stdout.write(body)
    else:
        args.output.write_text(body, encoding="utf-8")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
