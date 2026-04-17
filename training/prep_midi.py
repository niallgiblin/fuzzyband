#!/usr/bin/env python3
"""MIDI → event JSON per docs/TOKENIZATION.md (Phase 9 DATA-03)."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import mido


def _first_tempo_us(mid: mido.MidiFile) -> int:
    """Microseconds per quarter note; default 500000 (~120 BPM)."""
    for track in mid.tracks:
        for msg in track:
            if msg.type == "set_tempo":
                return int(msg.tempo)
    return 500_000


def midi_to_events(path: Path) -> dict:
    mid = mido.MidiFile(str(path))
    tempo = _first_tempo_us(mid)
    merged = mido.merge_tracks(mid.tracks)

    events: list[dict] = []
    tick = 0
    for msg in merged:
        tick += msg.time
        t = float(mido.tick2second(tick, mid.ticks_per_beat, tempo))
        if msg.type == "note_on" and msg.velocity > 0:
            events.append(
                {
                    "type": "note_on",
                    "t": t,
                    "pitch": int(msg.note),
                    "velocity": int(msg.velocity),
                    "channel": int(msg.channel),
                }
            )
        elif msg.type == "note_off" or (msg.type == "note_on" and msg.velocity == 0):
            events.append(
                {
                    "type": "note_off",
                    "t": t,
                    "pitch": int(msg.note),
                    "channel": int(msg.channel),
                }
            )

    events.sort(key=lambda e: (e["t"], e["type"] == "note_off"))

    return {
        "schema_version": 1,
        "source": str(path.resolve()),
        "events": events,
    }


def main() -> int:
    p = argparse.ArgumentParser(description="MIDI → event JSON (TOKENIZATION.md schema v1)")
    p.add_argument("--input", "-i", type=Path, required=True, help="Path to .mid / .midi")
    p.add_argument("--output", "-o", type=Path, help="Write JSON here (default: stdout)")
    args = p.parse_args()

    if not args.input.is_file():
        print(f"Error: not a file: {args.input}", file=sys.stderr)
        return 1

    payload = midi_to_events(args.input)
    text = json.dumps(payload, indent=2)

    if args.output:
        args.output.write_text(text, encoding="utf-8")
    else:
        print(text)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
