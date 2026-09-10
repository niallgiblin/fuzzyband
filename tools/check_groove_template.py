#!/usr/bin/env python3
"""Print per-cell groove-template stats and gate T3.3 (centred microtiming).

Reads data/groove_templates.json (the committed provenance file) so a regenerate
cannot silently reintroduce a rushed/lagged grid. Exit 0 if every template's
mean timing offset is within ±1.5 ms; otherwise exit 2.

Usage:
    python3 tools/check_groove_template.py
    python3 tools/check_groove_template.py --json data/groove_templates.json
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from pathlib import Path

_REPO = Path(__file__).resolve().parent.parent
_DEFAULT_JSON = _REPO / "data" / "groove_templates.json"
_MAX_MEAN_ABS_MS = 1.5
_ROCK_JITTER_MAX = 3.0
_METAL_JITTER_MAX = 2.0


def _stats(name: str, t: dict) -> dict:
    timing = [float(x) for x in t["timingMs"]]
    vel = [float(x) for x in t["velocityMul"]]
    mean_ms = statistics.fmean(timing)
    std_ms = statistics.pstdev(timing) if len(timing) > 1 else 0.0
    return {
        "name": name,
        "mean_ms": mean_ms,
        "std_ms": std_ms,
        "min_ms": min(timing),
        "max_ms": max(timing),
        "min_vel": min(vel),
        "max_vel": max(vel),
        "timingJitterMs": float(t["timingJitterMs"]),
        "velocityJitter": float(t["velocityJitter"]),
        "timing": timing,
        "velocityMul": vel,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description="T3.3 groove-template sanity check.")
    parser.add_argument("--json", type=Path, default=_DEFAULT_JSON)
    args = parser.parse_args()

    payload = json.loads(args.json.read_text(encoding="utf-8"))
    templates = payload["templates"]

    failed = False
    print(f"source: {payload.get('source', '?')}")
    print(f"file:   {args.json}")
    print()
    for name in ("rock", "metal", "punk"):
        s = _stats(name, templates[name])
        print(f"{name}")
        print(f"  timing mean {s['mean_ms']:+.3f} ms   std {s['std_ms']:.3f} ms"
              f"   range [{s['min_ms']:+.3f}, {s['max_ms']:+.3f}]")
        print(f"  jitter {s['timingJitterMs']:.3f} ms   velocityJitter {s['velocityJitter']:.3f}")
        print(f"  velocityMul peak {s['max_vel']:.4f}   trough {s['min_vel']:.4f}")
        cells = "  cells:"
        for i, (ms, mul) in enumerate(zip(s["timing"], s["velocityMul"])):
            cells += f"\n    {i:2d}  mul {mul:6.4f}  {ms:+7.3f} ms"
        print(cells)
        print()
        if abs(s["mean_ms"]) >= _MAX_MEAN_ABS_MS:
            print(f"FAIL: {name} |mean(timingMs)| = {abs(s['mean_ms']):.3f} >= {_MAX_MEAN_ABS_MS}",
                  file=sys.stderr)
            failed = True
        if name == "rock" and s["timingJitterMs"] > _ROCK_JITTER_MAX + 1.0e-6:
            print(f"FAIL: rock timingJitterMs {s['timingJitterMs']} > {_ROCK_JITTER_MAX}",
                  file=sys.stderr)
            failed = True
        if name == "metal" and s["timingJitterMs"] > _METAL_JITTER_MAX + 1.0e-6:
            print(f"FAIL: metal timingJitterMs {s['timingJitterMs']} > {_METAL_JITTER_MAX}",
                  file=sys.stderr)
            failed = True

    return 2 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
