#!/usr/bin/env python3
"""Phase 39-01: mine a drum-fill bank from the Groove MIDI Dataset (GMD).

The committed groove templates (`build_groove_template.py`) deliberately use only
GMD's steady `beat_type == "beat"` takes. The 243 usable 4/4 *fills* are unused.
This script quantises a diverse selection of them to the 16th grid and bakes a
fixed-size bank the audio thread can pick from — real drummer fills, no model.

Grid: 16th cells within the fill, cell = round(beat*4); the sub-cell deviation is
kept as `off16` in [-0.5, 0.5] of a 16th so the engine can re-apply its own
microtiming on top. Velocities are stored as a fraction of the fill's peak so the
engine can scale them by section/energy.

Outputs:
  * data/fill_bank.json      — committed, human-readable provenance + bank
  * src/midi/FillBankData.h  — committed, generated C++ constants

Source: Groove MIDI Dataset (GMD, CC-BY 4.0).
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from collections import Counter, defaultdict
from pathlib import Path

import mido

_TRAINING_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _TRAINING_DIR.parent
_DEFAULT_JSON_OUT = _REPO_ROOT / "data" / "fill_bank.json"
_DEFAULT_HEADER_OUT = _REPO_ROOT / "src" / "midi" / "FillBankData.h"

# Voices (mirror src/midi/GrooveGrid.h voice mapping).
_VOICE_NAMES = ["kick", "snare", "hatClosed", "hatOpen", "ride", "rideBell",
                "tomLo", "tomMid", "tomHi", "crash"]
_VOICE_OF_NOTE = {
    35: 0, 36: 0,          # kick
    38: 1, 40: 1,          # snare
    42: 2,                 # hat closed
    44: 2,                 # pedal hat -> closed
    46: 3,                 # hat open
    51: 4,                 # ride
    53: 5,                 # ride bell
    41: 6, 43: 6,          # tom lo
    45: 7, 47: 7,          # tom mid
    48: 8, 50: 8,          # tom hi
    49: 9, 52: 9, 55: 9, 57: 9,   # crash / china / splash
}

_STYLES = ("rock", "punk")
_MAX_HITS = 24
_BANK_SIZE = 48
_MIN_HITS = 4


def _find_info_csv(root: Path) -> Path:
    m = sorted(root.rglob("info.csv"))
    if not m:
        raise SystemExit(f"error: no GMD info.csv under {root} — run training/download_gmd.py first.")
    return m[0]


def _hits(midi_path: Path):
    """Return [(beat, velocity, gm_note)] for channel-10 note-ons."""
    try:
        mid = mido.MidiFile(str(midi_path))
    except Exception:
        return []
    tpb = mid.ticks_per_beat or 480
    out = []
    for tr in mid.tracks:
        tick = 0
        for msg in tr:
            tick += msg.time
            if msg.type == "note_on" and msg.velocity > 0 and msg.channel == 9:
                out.append((tick / tpb, int(msg.velocity), int(msg.note)))
    return out


def _quantise(hits):
    """-> (events, bars) where events = [(voice, cell, vel_frac, off16)]."""
    peak = max((v for _, v, _ in hits), default=1)
    events = []
    for beat, vel, note in hits:
        voice = _VOICE_OF_NOTE.get(note)
        if voice is None:
            continue
        raw = beat * 4.0
        cell = int(round(raw))
        off = raw - cell                      # in [-0.5, 0.5] of a 16th
        events.append([voice, cell, round(vel / max(1, peak), 3), round(off, 3)])
    bars = 1
    if events:
        bars = max(1, events[-1][1] // 16 + 1)
    return events, bars


def _pattern_key(events):
    """Coarse signature for dedup: voice + 8th-note cell (ignores dynamics)."""
    return tuple(sorted((v, c // 2) for v, c, _, _ in events))


def build_bank(info_csv: Path) -> list:
    rows = list(csv.DictReader(info_csv.open(encoding="utf-8", newline="")))
    base = info_csv.parent
    candidates = []
    for r in rows:
        if r.get("beat_type", "").strip() != "fill":
            continue
        if r.get("time_signature", "").strip() != "4-4":
            continue
        primary = r.get("style", "").split("/")[0].strip().lower()
        if primary not in _STYLES:
            continue
        p = base / r.get("midi_filename", "").strip()
        if not p.is_file():
            continue
        h = _hits(p)
        if not (_MIN_HITS <= len(h) <= _MAX_HITS):
            continue
        events, bars = _quantise(h)
        if bars != 1 or len(events) < _MIN_HITS:
            continue
        # A fill should have some pitched drums (toms/snares), not only hats/kick.
        if not any(v in (1, 6, 7, 8) for v, _, _, _ in events):
            continue
        candidates.append({
            "name": p.stem,
            "style": primary,
            "bpm": float(r.get("bpm", 0) or 0),
            "bars": bars,
            "hits": len(events),
            "events": events,
        })

    # Dedup by coarse pattern signature, keeping the closest-to-median
    # representative per group, then select a bank STRATIFIED by hit count so it
    # spans sparse -> dense rather than just the busiest fills.
    groups = defaultdict(list)
    for c in candidates:
        groups[_pattern_key(c["events"])].append(c)
    reps = []
    for _key, members in groups.items():
        members.sort(key=lambda c: abs(c["hits"] - 14))
        reps.append(members[0])
    reps.sort(key=lambda c: (c["hits"], c["name"]))

    def spread(items, n):
        if not items:
            return []
        if len(items) <= n:
            return list(items)
        step = len(items) / n
        return [items[int(i * step)] for i in range(n)]

    sparse = [c for c in reps if c["hits"] <= 9]
    mid = [c for c in reps if 10 <= c["hits"] <= 15]
    dense = [c for c in reps if c["hits"] >= 16]
    per = _BANK_SIZE // 3 + 2
    bank = (spread(sparse, per) + spread(mid, per) + spread(dense, per))[:_BANK_SIZE]
    print(f"candidates={len(candidates)} distinct-pattern groups={len(groups)} "
          f"sparse={len(sparse)} mid={len(mid)} dense={len(dense)} bank={len(bank)}",
          file=sys.stderr)
    return bank


def _fmt_events(events):
    # {voice, cell, vel(0-127 int), off16(int, x1000)}
    parts = []
    for v, c, vf, off in events:
        parts.append("{%d,%d,%d,%d}" % (v, c, max(1, min(127, round(vf * 127))), round(off * 1000)))
    return ", ".join(parts)


def _emit_header(bank: list, out_path: Path) -> None:
    lines = [
        "#pragma once",
        "",
        "// AUTO-GENERATED by training/build_fill_bank.py (Phase 39-01).",
        "// Do NOT edit by hand. Regenerate with: python3 training/build_fill_bank.py",
        "// Real 1-2 bar drum fills mined from the Groove MIDI Dataset (GMD, CC-BY 4.0),",
        "// quantised to the 16th grid. Velocities are a fraction of each fill's peak",
        "// (0-127 int); off16 is the sub-cell deviation x1000 (of a 16th).",
        "",
        "namespace FillBank",
        "{",
        f"static constexpr int kFillCount = {len(bank)};",
        f"static constexpr int kMaxEvents = {_MAX_HITS};",
        "",
        "struct FillEvent { unsigned char voice; unsigned char cell; unsigned char vel; short off16; };",
        "struct FillDef { int eventCount; int bars; FillEvent events[kMaxEvents]; };",
        "",
        "inline constexpr FillDef kFills[kFillCount] = {",
    ]
    for c in bank:
        lines.append(f"    {{ {c['hits']}, {c['bars']}, {{ {_fmt_events(c['events'])} }} }},"
                     f"   // {c['name']} ({c['style']}, {c['bpm']:.0f} BPM)")
    lines += ["};", "", "}  // namespace FillBank", ""]
    out_path.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {out_path}", file=sys.stderr)


def main() -> int:
    ap = argparse.ArgumentParser(description="Phase 39-01: mine a GMD fill bank.")
    ap.add_argument("--gmd-root", type=Path, default=_TRAINING_DIR / "data" / "tfds" / "downloads" / "extracted")
    ap.add_argument("--json-out", type=Path, default=_DEFAULT_JSON_OUT)
    ap.add_argument("--header-out", type=Path, default=_DEFAULT_HEADER_OUT)
    args = ap.parse_args()

    info_csv = _find_info_csv(args.gmd_root)
    bank = build_bank(info_csv)
    if len(bank) < 24:
        print(f"FAIL: only {len(bank)} fills in the bank (<24)", file=sys.stderr)
        return 2

    args.json_out.write_text(json.dumps({
        "source": "Groove MIDI Dataset (GMD) v1.0.0, CC-BY 4.0",
        "generator": "training/build_fill_bank.py",
        "grid": "16th cells; cell = round(beat*4), off16 = sub-cell deviation x1000",
        "voice_names": _VOICE_NAMES,
        "styles": list(_STYLES),
        "fills": bank,
    }, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.json_out}", file=sys.stderr)
    _emit_header(bank, args.header_out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
