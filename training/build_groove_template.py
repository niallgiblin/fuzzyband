#!/usr/bin/env python3
"""C1 (DATA_STRATEGY.md §6.1): reduce (E-)GMD drum MIDI to per-genre groove
templates — a per-16th-cell velocity hierarchy and microtiming distribution that
map 1:1 onto ``Groove::Template`` in ``src/midi/GrooveTemplate.h``.

This replaces the hand-authored ``rock()/metal()/punk()`` numbers with statistics
from real human drumming. The output is a *fixed-size* struct (16 velocity
multipliers + 16 timing offsets + jitter/ghost scalars) consumed on the audio
thread — **no** model inference is added to the real-time path (the §6.1
guardrail). The script emits:

  * ``data/groove_templates.json``    — committed, human-readable stats + provenance
  * ``src/midi/GrooveTemplateData.h`` — committed, generated C++ constants that
    ``GrooveTemplate.h`` bakes into ``rock()/metal()/punk()``

Source: the Groove MIDI Dataset (GMD, CC-BY 4.0). GMD has no *metal* genre, so
the metal template is derived from the rock-family stats by a documented
tightening transform (less laid-back backbeat, lower timing jitter) — the same
musical relationship the hand-authored ``metal()`` encoded. Rock and punk are
derived directly from GMD data.

Grid convention (matches ``Groove::grid16Of``): cell = round((beat mod 4) * 4),
so cell 0 = downbeat, 4 = beat-2 backbeat, 8 = beat 3, 12 = beat-4 backbeat.
``timingMs`` sign: positive = late (laid back), negative = early (punchy).
"""

from __future__ import annotations

import argparse
import csv
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import mido

_TRAINING_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _TRAINING_DIR.parent

# The GMD MIDI-only corpus is extracted here by download_gmd.py (TFDS unpacks the
# CC-BY zip). The tree is groove/<drummer>/<session>/<id>_<style>_<bpm>_<beat>_<ts>.mid
# alongside info.csv. We read the named files directly (no TensorFlow needed).
_DEFAULT_GMD_GLOB = (
    _TRAINING_DIR / "data/tfds/downloads/extracted"
)
_DEFAULT_JSON_OUT = _REPO_ROOT / "data/groove_templates.json"
_DEFAULT_HEADER_OUT = _REPO_ROOT / "src/midi/GrooveTemplateData.h"

# General MIDI drum note → coarse instrument bucket (only used for ghost/pocket stats).
_KICK = {35, 36}
_SNARE = {38, 40}

# Genres sampled directly from GMD. Key = output template name; value = GMD
# primary style tokens (style.split('/')[0]) that feed it. Only *rock* has enough
# steady 4/4 grooves in GMD to derive robustly (punk in GMD is almost all fills,
# and GMD has no metal genre), so metal and punk are derived from the real rock
# stats by documented transforms — see _derive_metal / _derive_punk.
_GENRE_SOURCES: dict[str, tuple[str, ...]] = {
    "rock": ("rock",),
}

_MIN_FILES_PER_GENRE = 15   # honest gate: below this the stats are too thin to trust
_MIN_HITS_PER_CELL = 20     # a cell with fewer hits falls back to neutral (1.0 / 0.0)


def _validate_under_training(path: Path) -> Path:
    resolved = path.resolve()
    training_root = (_REPO_ROOT / "training").resolve()
    resolved.relative_to(training_root)  # raises if it escapes training/
    return resolved


def _find_info_csv(gmd_root: Path) -> Path:
    """Locate GMD info.csv under the extracted tree."""
    matches = sorted(gmd_root.rglob("info.csv"))
    if not matches:
        raise SystemExit(
            f"error: no GMD info.csv under {gmd_root} — run "
            f"`python3 training/download_gmd.py` first."
        )
    return matches[0]


def _primary_style(style: str) -> str:
    return style.split("/", 1)[0].strip().lower()


def _iter_gmd_rows(info_csv: Path):
    """Yield (midi_path, primary_style, bpm) for 4/4 steady-beat GMD rows."""
    base = info_csv.parent
    with info_csv.open("r", encoding="utf-8", newline="") as f:
        for row in csv.DictReader(f):
            if row.get("time_signature", "").strip() != "4-4":
                continue
            if row.get("beat_type", "").strip() != "beat":  # exclude fills
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
            yield midi_path, _primary_style(row.get("style", "")), bpm


def _hits_from_midi(midi_path: Path):
    """Return [(beat, velocity, note)] for channel-10 note-on hits in the file.

    beat is absolute quarter-note position from the start of the take.
    """
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


def _accumulate(hits, bpm, acc):
    """Fold one take's hits into the per-cell accumulators (in-place)."""
    ms_per_beat = 60000.0 / bpm
    for beat, vel, note in hits:
        beat_in_bar = beat % 4.0
        # Nearest 16th line. raw_cell is 0..16; using it (pre-modulo) to compute
        # the deviation keeps a hit just before the next bar's downbeat reading as
        # slightly *early* (dev ≈ beat_in_bar - 4.0) instead of a full bar late.
        raw_cell = int(round(beat_in_bar * 4.0))  # 0..16
        dev_beats = beat_in_bar - raw_cell / 4.0  # naturally in [-0.125, 0.125]
        cell = raw_cell % 16
        acc["vel"][cell].append(vel)
        acc["ms"][cell].append(dev_beats * ms_per_beat)
        if note in _SNARE:
            acc["snare_vel"].append(vel)


def _new_acc() -> dict:
    return {
        "vel": defaultdict(list),
        "ms": defaultdict(list),
        "snare_vel": [],
        "files": 0,
    }


def _mad_sigma(values: list[float]) -> float:
    """Robust std estimate = 1.4826 * median-absolute-deviation.

    Robust to the heavy tails in pooled drum data (swung 16ths, flams, ghost
    ornaments) that would otherwise inflate a plain standard deviation.
    """
    if len(values) < 2:
        return 0.0
    med = statistics.median(values)
    mad = statistics.median([abs(v - med) for v in values])
    return 1.4826 * mad


# The jitter knobs feed a *bounded gaussian* humanisation on top of the structured
# per-cell offset — they model within-groove human wobble, not the full corpus
# spread. Clamp the data-derived robust sigma to a musical band so a few very
# swung/ornamented takes can't turn the humaniser into white noise.
# T3.3: the data-derived MAD sigma used to clamp at 6 ms and, stacked on a
# negative-mean template, rushed every downbeat. Centre the offsets (below) and
# keep the residual gaussian in a musical band: ~2.5–3 ms rock, ~2 ms metal.
_TIMING_JITTER_MIN, _TIMING_JITTER_MAX = 1.0, 3.0
_VEL_JITTER_MIN, _VEL_JITTER_MAX = 2.0, 8.0
_METAL_TIMING_JITTER_MAX = 2.0
_MAX_TIMING_MEAN_ABS_MS = 1.5

# The engine multiplies these accents on top of the pattern's *already-accented*
# authored velocities (PatternPlayer), so an uncapped data peak (~1.25) double-
# accents and clips the loudest notes at 127, erasing verse/chorus contrast. We
# keep the data-derived *shape* (relative hierarchy) but scale the deviations so
# the peak boost matches the engine's calibrated headroom — preserving dynamic
# range without hand-authoring the hierarchy.
_MAX_ACCENT = 1.12


def _centre_timing(timing_ms: list[float]) -> list[float]:
    """Subtract the mean offset so the grid is centred (T3.3).

    GMD's pooled medians rush every downbeat (mean ≈ −2 to −7 ms). The residual
    per-cell shape is the musical feel; the gaussian jitter then sits around it
    instead of adding a systematic shove.
    """
    if not timing_ms:
        return timing_ms
    mean = statistics.fmean(timing_ms)
    return [round(t - mean, 3) for t in timing_ms]


def _normalize_accents(velocity_mul: list[float], target_peak: float = _MAX_ACCENT) -> list[float]:
    """Scale (mul - 1) deviations so max(mul) == target_peak, keeping the shape."""
    peak = max(velocity_mul)
    if peak <= target_peak or peak <= 1.0:
        return velocity_mul
    scale = (target_peak - 1.0) / (peak - 1.0)
    return [round(1.0 + (m - 1.0) * scale, 4) for m in velocity_mul]


def _reduce_template(acc: dict) -> dict:
    """Turn per-cell accumulators into a Groove::Template-shaped dict."""
    all_vel = [v for cell in acc["vel"].values() for v in cell]
    global_mean_vel = statistics.fmean(all_vel) if all_vel else 100.0

    velocity_mul = [1.0] * 16
    timing_ms = [0.0] * 16
    # Per-cell robust dispersion, later reduced to a single scalar jitter knob.
    per_cell_vel_sigma: list[float] = []
    per_cell_ms_sigma: list[float] = []
    for cell in range(16):
        vels = acc["vel"].get(cell, [])
        mss = acc["ms"].get(cell, [])
        if len(vels) >= _MIN_HITS_PER_CELL:
            velocity_mul[cell] = statistics.fmean(vels) / global_mean_vel
            per_cell_vel_sigma.append(_mad_sigma(vels))
        if len(mss) >= _MIN_HITS_PER_CELL:
            # Median is robust to off-grid ornaments dragging the structured offset.
            timing_ms[cell] = round(statistics.median(mss), 3)
            per_cell_ms_sigma.append(_mad_sigma(mss))

    velocity_mul = _normalize_accents(velocity_mul)
    timing_ms = _centre_timing(timing_ms)

    timing_jitter = statistics.median(per_cell_ms_sigma) if per_cell_ms_sigma else 1.5
    velocity_jitter = statistics.median(per_cell_vel_sigma) if per_cell_vel_sigma else 3.0
    timing_jitter = round(min(max(timing_jitter, _TIMING_JITTER_MIN), _TIMING_JITTER_MAX), 3)
    velocity_jitter = round(min(max(velocity_jitter, _VEL_JITTER_MIN), _VEL_JITTER_MAX), 3)

    # T1.5: pin the documented ghost band. GMD's 5th–30th snare percentile
    # (~15–42) is too quiet and disagrees with MidiPatternLibrary (authored
    # ghosts at velocity <= 62, rendered in 30–55). Regeneration must not undo this.
    lo, hi, ghost_threshold = 30.0, 55.0, 62

    return {
        "velocityMul": velocity_mul,
        "timingMs": timing_ms,
        "timingJitterMs": timing_jitter,
        "velocityJitter": velocity_jitter,
        "ghostVelocityLo": float(lo),
        "ghostVelocityHi": float(hi),
        "ghostThreshold": int(ghost_threshold),
        "files": acc["files"],
        "hits": len(all_vel),
    }


def _derive_metal(rock: dict) -> dict:
    """GMD has no metal genre: derive metal from rock by tightening the feel.

    Metal drumming is tighter and more aggressive than rock: the laid-back
    backbeat is pulled toward the grid and the timing jitter shrinks, while the
    accent hierarchy is kept and slightly sharpened. This mirrors the musical
    relationship the hand-authored metal() encoded, now anchored to real rock
    stats instead of arbitrary constants.
    """
    m = json.loads(json.dumps(rock))  # deep copy
    m["timingMs"] = [round(t * 0.5, 3) for t in rock["timingMs"]]  # pull toward grid
    m["timingJitterMs"] = round(min(rock["timingJitterMs"] * 0.7, _METAL_TIMING_JITTER_MAX), 3)
    # Sharpen the primary accents (downbeat + backbeats) a touch.
    for cell in (0, 4, 12):
        m["velocityMul"][cell] = round(m["velocityMul"][cell] * 1.02, 4)
    m["_derived_from"] = "rock"
    return m


def _derive_punk(rock: dict) -> dict:
    """GMD has too few steady punk grooves (mostly fills): derive punk from rock.

    Punk drumming is straight and driving — everything sits near the grid with
    minimal laid-back feel and tight timing. We pull the backbeat almost to the
    grid, shrink the jitter hard, and keep the accent hierarchy. This mirrors the
    hand-authored punk() (which was itself derived from metal()), now anchored to
    real rock stats.
    """
    p = json.loads(json.dumps(rock))  # deep copy
    p["timingMs"] = [round(t * 0.25, 3) for t in rock["timingMs"]]  # near-grid
    p["timingJitterMs"] = round(rock["timingJitterMs"] * 0.55, 3)   # very tight
    p["_derived_from"] = "rock"
    return p


def _fmt_arr(values: list[float]) -> str:
    return ", ".join(f"{v:.4f}f" for v in values)


def _emit_header(templates: dict, out_path: Path) -> None:
    """Generate src/midi/GrooveTemplateData.h with the baked constants."""
    lines = [
        "#pragma once",
        "",
        "// AUTO-GENERATED by training/build_groove_template.py (DATA_STRATEGY.md C1).",
        "// Do NOT edit by hand. Regenerate with:",
        "//   python3 training/build_groove_template.py",
        "// T1.5 pins ghostVelocityLo/Hi/Threshold to 30/55/62 in the generator so a",
        "// regenerate cannot disagree with MidiPatternLibrary's authored ghost band.",
        "// T3.3 recentres timingMs (mean ≈ 0) and caps jitter at 3 ms rock / 2 ms metal.",
        "// Source: Groove MIDI Dataset (GMD, CC-BY 4.0). See data/groove_templates.json",
        "// for provenance (file/hit counts per genre). 'metal' is derived from 'rock'",
        "// (GMD has no metal genre) by a documented tightening transform.",
        "//",
        "// These are data-derived per-16th velocity multipliers + microtiming offsets",
        "// consumed as a fixed-size struct on the audio thread (no RT inference).",
        "",
        "namespace Groove::data",
        "{",
    ]
    for name in ("rock", "metal", "punk"):
        t = templates[name]
        lines.append(f"// {name}: {t.get('files', 0)} files, {t.get('hits', 0)} hits"
                     + (f" (derived from {t['_derived_from']})" if "_derived_from" in t else ""))
        lines.append(f"inline constexpr float k{name.capitalize()}VelocityMul[16] = {{ {_fmt_arr(t['velocityMul'])} }};")
        lines.append(f"inline constexpr float k{name.capitalize()}TimingMs[16] = {{ {_fmt_arr(t['timingMs'])} }};")
        lines.append(f"inline constexpr float k{name.capitalize()}TimingJitterMs = {t['timingJitterMs']:.3f}f;")
        lines.append(f"inline constexpr float k{name.capitalize()}VelocityJitter = {t['velocityJitter']:.3f}f;")
        lines.append(f"inline constexpr float k{name.capitalize()}GhostVelocityLo = {t['ghostVelocityLo']:.1f}f;")
        lines.append(f"inline constexpr float k{name.capitalize()}GhostVelocityHi = {t['ghostVelocityHi']:.1f}f;")
        lines.append(f"inline constexpr unsigned char k{name.capitalize()}GhostThreshold = {t['ghostThreshold']};")
        lines.append("")
    lines.append("} // namespace Groove::data")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="C1: GMD → per-genre groove templates.")
    parser.add_argument("--gmd-root", type=Path, default=_DEFAULT_GMD_GLOB,
                        help="Root under which GMD info.csv + MIDI live "
                             "(default: training/data/tfds/downloads/extracted).")
    parser.add_argument("--json-out", type=Path, default=_DEFAULT_JSON_OUT)
    parser.add_argument("--header-out", type=Path, default=_DEFAULT_HEADER_OUT)
    parser.add_argument("--max-files", type=int, default=None,
                        help="Cap files per genre (smoke tests).")
    args = parser.parse_args()

    gmd_root = _validate_under_training(args.gmd_root)
    info_csv = _find_info_csv(gmd_root)
    print(f"GMD info.csv: {info_csv}", file=sys.stderr)

    accs: dict[str, dict] = {name: _new_acc() for name in _GENRE_SOURCES}
    style_to_genre: dict[str, str] = {}
    for genre, styles in _GENRE_SOURCES.items():
        for s in styles:
            style_to_genre[s] = genre

    scanned = 0
    for midi_path, primary, bpm in _iter_gmd_rows(info_csv):
        genre = style_to_genre.get(primary)
        if genre is None:
            continue
        if args.max_files is not None and accs[genre]["files"] >= args.max_files:
            continue
        hits = _hits_from_midi(midi_path)
        if not hits:
            continue
        _accumulate(hits, bpm, accs[genre])
        accs[genre]["files"] += 1
        scanned += 1
        if scanned % 50 == 0:
            print(f"  ... {scanned} files", file=sys.stderr)

    templates: dict[str, dict] = {}
    for genre in _GENRE_SOURCES:
        n = accs[genre]["files"]
        if n < _MIN_FILES_PER_GENRE:
            print(f"FAIL: genre {genre!r} has {n} files < {_MIN_FILES_PER_GENRE} minimum",
                  file=sys.stderr)
            return 2
        templates[genre] = _reduce_template(accs[genre])
        print(f"  {genre}: {n} files, {templates[genre]['hits']} hits", file=sys.stderr)

    templates["metal"] = _derive_metal(templates["rock"])
    templates["punk"] = _derive_punk(templates["rock"])

    payload = {
        "source": "Groove MIDI Dataset (GMD) v1.0.0, CC-BY 4.0",
        "generator": "training/build_groove_template.py",
        "grid": "16th-note cells within a 4/4 bar; cell = round((beat mod 4)*4)",
        "timing_sign": "positive = late (laid back), negative = early (punchy)",
        "min_files_per_genre": _MIN_FILES_PER_GENRE,
        "min_hits_per_cell": _MIN_HITS_PER_CELL,
        "templates": templates,
    }
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.json_out}", flush=True)

    _emit_header(templates, args.header_out)
    print(f"Wrote {args.header_out}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
