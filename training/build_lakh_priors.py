#!/usr/bin/env python3
"""C2 (DATA_STRATEGY.md §6.2): Lakh MIDI genre subsets → selection priors.

Replaces the old blind channel-10 + header-BPM Lakh filtering with a
**genre-weighted rock subset** (MSD tags: Rock / Metal / Punk) and derives, per
genre, how often each coarse groove type occurs — the *selection priors* that
weight the pattern pools in ``src/inference/pattern_rules.h``.

Two fixes over the legacy path:
  * **Genre weighting** — files are kept only for the rock-family genres and
    tagged with our GenrePreset id (Rock=0, Punk=2, Metal=3), using MSD track IDs
    from the ``lmd_matched`` path and tagtraum CD2 genre ground truth.
  * **Content-derived tempo** — tempo is estimated from the actual drum-onset
    pulse spacing, not the (unreliable) MIDI header ``set_tempo`` (audit §5.7).

These priors inform the pattern *pools/tables at build time* — they are baked
into the generated ``src/inference/PatternPriors.h`` and never run on the audio
thread. Only aggregate statistics are emitted; the tag file is not redistributed
(verify tagtraum terms — see data/MANIFEST.md).

Outputs:
  * ``data/lakh_priors.json``          — committed, per-genre tempo + pattern weights
  * ``src/inference/PatternPriors.h``  — committed, generated constexpr weight table
"""

from __future__ import annotations

import argparse
import json
import statistics
import sys
from collections import defaultdict
from pathlib import Path

import mido

_TRAINING_DIR = Path(__file__).resolve().parent
_REPO_ROOT = _TRAINING_DIR.parent
_DEFAULT_LMD = _TRAINING_DIR / "data/lakh/lmd_matched"
_DEFAULT_TAGS = _TRAINING_DIR / "data/lakh/msd_tagtraum_cd2.cls"
_DEFAULT_JSON_OUT = _REPO_ROOT / "data/lakh_priors.json"
_DEFAULT_HEADER_OUT = _REPO_ROOT / "src/inference/PatternPriors.h"

# tagtraum CD2 genre -> our GenrePreset id (Groove::presets(): 0 Rock, 1 Hard
# Rock, 2 Punk, 3 Metal, 4 Sludge). CD2 has no separate hard-rock/sludge, so we
# map the three rock-family genres it does separate.
_GENRE_TO_PRESET: dict[str, int] = {"Rock": 0, "Punk": 2, "Metal": 3}
_PRESET_NAMES = {0: "Rock", 1: "Hard Rock", 2: "Punk", 3: "Metal", 4: "Sludge"}
_NUM_PRESETS = 5

# Must stay in sync with MidiPatternLibrary::kPatternCount (unit-tested C++ side).
_PATTERN_COUNT = 28

# ── Coarse groove buckets → representative pattern indices (documented mapping) ──
# The buckets are derived from *content* (drum density / backbeat / double-kick);
# each spreads its statistical mass onto the pattern-library indices that render
# that feel. This is a selection PRIOR (pool weighting), not a training label, so
# it does not re-introduce the circular-labeling problem (#1) — no model is
# trained on this output.
_BUCKET_TO_PATTERNS: dict[str, tuple[int, ...]] = {
    "sparse":     (9, 6, 0),        # sparse/breakdown, heavy, silent-ish
    "half_time":  (7, 2, 23),       # half-time feels
    "backbeat":   (1, 22, 4, 14),   # verse/chorus backbeat grooves
    "busy":       (3, 10, 5),       # fast verse, thrash, chorus fast
    "blast":      (8, 21),          # blast beats
}


def _validate_under_training(path: Path) -> Path:
    resolved = path.resolve()
    training_root = (_REPO_ROOT / "training").resolve()
    resolved.relative_to(training_root)
    return resolved


def _load_genre_tags(cls_path: Path) -> dict[str, str]:
    """Parse a tagtraum .cls file into {trackId: majority_genre}."""
    tags: dict[str, str] = {}
    with cls_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) >= 2 and parts[0] and parts[1]:
                tags[parts[0]] = parts[1]
    return tags


def _track_id_from_path(midi_path: Path) -> str | None:
    """lmd_matched layout: .../<A>/<B>/<C>/<TRID>/<hash>.mid — the parent dir is the TRID."""
    parent = midi_path.parent.name
    return parent if parent.startswith("TR") else None


def _content_tempo_and_groove(midi_path: Path) -> tuple[float, str] | None:
    """Return (content_bpm, groove_bucket) for a MIDI file, or None if unusable.

    content_bpm is derived from the median spacing of drum onsets (seconds), i.e.
    the felt pulse, not the header set_tempo. groove_bucket is a coarse content
    class from drum density + backbeat + double-kick evidence.
    """
    try:
        mid = mido.MidiFile(str(midi_path))
    except Exception:
        return None

    onset_sec: list[float] = []      # channel-10 hits, absolute seconds
    kicks: list[float] = []
    snares: list[float] = []
    total_beats_est = 0.0
    try:
        t = 0.0
        for msg in mid:  # iterating MidiFile yields delta .time in SECONDS via tempo map
            t += msg.time
            if msg.type == "note_on" and msg.velocity > 0 and msg.channel == 9:
                onset_sec.append(t)
                if msg.note in (35, 36):
                    kicks.append(t)
                elif msg.note in (38, 40):
                    snares.append(t)
        total_beats_est = t
    except Exception:
        return None

    if len(onset_sec) < 8:
        return None

    onset_sec.sort()
    iois = [b - a for a, b in zip(onset_sec, onset_sec[1:]) if 0.05 < (b - a) < 2.0]
    if len(iois) < 4:
        return None
    # Beat period ~ the median inter-onset spacing at the quarter-note level.
    tatum = statistics.median(iois)
    # Snap the felt beat into 40..220 BPM by folding octaves of the tatum.
    beat = tatum
    while beat < 60.0 / 220.0:  # too fast -> double the period
        beat *= 2.0
    while beat > 60.0 / 40.0:   # too slow -> halve
        beat /= 2.0
    bpm = max(40.0, min(220.0, 60.0 / beat))

    # ── Groove bucket from content ──────────────────────────────────────────
    dur = max(onset_sec[-1] - onset_sec[0], 1e-6)
    hits_per_sec = len(onset_sec) / dur
    hits_per_beat = hits_per_sec * (60.0 / bpm)
    kick_per_beat = (len(kicks) / dur) * (60.0 / bpm)
    has_backbeat = len(snares) >= max(2, int(0.15 * (dur * bpm / 60.0)))

    if hits_per_beat >= 6.0 and kick_per_beat >= 2.0:
        bucket = "blast"
    elif hits_per_beat >= 4.0:
        bucket = "busy"
    elif hits_per_beat <= 1.2:
        bucket = "sparse"
    elif hits_per_beat <= 2.2 and has_backbeat:
        bucket = "half_time"
    else:
        bucket = "backbeat"
    return bpm, bucket


def _pattern_weights(bucket_dist: dict[str, float]) -> list[float]:
    """Spread normalized groove-bucket mass onto pattern-library indices."""
    weights = [0.0] * _PATTERN_COUNT
    for bucket, mass in bucket_dist.items():
        idxs = _BUCKET_TO_PATTERNS.get(bucket, ())
        if not idxs:
            continue
        share = mass / len(idxs)
        for i in idxs:
            if 0 <= i < _PATTERN_COUNT:
                weights[i] += share
    peak = max(weights) if any(weights) else 1.0
    return [round(w / peak, 4) for w in weights]  # normalize to [0,1]


def _fmt_row(values: list[float]) -> str:
    return ", ".join(f"{v:.4f}f" for v in values)


def _emit_header(genre_weights: dict[int, list[float]], out_path: Path) -> None:
    lines = [
        "#pragma once",
        "",
        "// AUTO-GENERATED by training/build_lakh_priors.py (DATA_STRATEGY.md C2).",
        "// Do NOT edit by hand. Regenerate with:",
        "//   python3 training/build_lakh_priors.py",
        "// Source: Lakh MIDI lmd_matched + tagtraum CD2 MSD genre tags. See",
        "// data/lakh_priors.json for provenance (per-genre file counts + tempo).",
        "//",
        "// Data-derived selection priors: per (genre, pattern-index) popularity",
        "// weight in [0,1]. These weight the pattern POOLS at build time (pool",
        "// ordering) and are never evaluated on the audio thread.",
        "",
        "namespace PatternPriors",
        "{",
        f"inline constexpr int kNumGenres = {_NUM_PRESETS};",
        f"inline constexpr int kPatternCount = {_PATTERN_COUNT};",
        "",
        "// [genre][patternIndex] -> popularity weight in [0,1].",
        "inline constexpr float kWeight[kNumGenres][kPatternCount] = {",
    ]
    for g in range(_NUM_PRESETS):
        w = genre_weights.get(g, [0.0] * _PATTERN_COUNT)
        lines.append(f"    {{ {_fmt_row(w)} }},  // {_PRESET_NAMES[g]}")
    lines.append("};")
    lines.append("")
    lines.append("} // namespace PatternPriors")
    lines.append("")
    out_path.write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description="C2: Lakh genre subsets → selection priors.")
    parser.add_argument("--lmd-dir", type=Path, default=_DEFAULT_LMD)
    parser.add_argument("--genre-tags", type=Path, default=_DEFAULT_TAGS)
    parser.add_argument("--json-out", type=Path, default=_DEFAULT_JSON_OUT)
    parser.add_argument("--header-out", type=Path, default=_DEFAULT_HEADER_OUT)
    parser.add_argument("--max-files-per-genre", type=int, default=400,
                        help="Cap parsed files per genre to bound runtime "
                             "(0 = no cap; the full run scans all 116k files).")
    args = parser.parse_args()

    lmd_dir = _validate_under_training(args.lmd_dir)
    tags_path = _validate_under_training(args.genre_tags)
    if not lmd_dir.is_dir():
        print(f"error: {lmd_dir} not found — run download_lakh.py first", file=sys.stderr)
        return 1
    if not tags_path.is_file():
        print(f"error: {tags_path} not found — run download_msd_genre.py first",
              file=sys.stderr)
        return 1

    tags = _load_genre_tags(tags_path)
    print(f"Loaded {len(tags)} MSD genre tags", file=sys.stderr)

    per_genre_buckets: dict[int, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    per_genre_tempo: dict[int, list[float]] = defaultdict(list)
    per_genre_files: dict[int, int] = defaultdict(int)
    cap = args.max_files_per_genre or None

    seen_tracks: set[str] = set()
    scanned = 0
    for midi_path in lmd_dir.rglob("*.mid"):
        trid = _track_id_from_path(midi_path)
        if trid is None or trid in seen_tracks:
            continue
        genre = tags.get(trid)
        preset = _GENRE_TO_PRESET.get(genre) if genre else None
        if preset is None:
            continue
        if cap is not None and per_genre_files[preset] >= cap:
            continue
        seen_tracks.add(trid)  # one representative MIDI per track (dedupe versions)
        res = _content_tempo_and_groove(midi_path)
        if res is None:
            continue
        bpm, bucket = res
        per_genre_buckets[preset][bucket] += 1
        per_genre_tempo[preset].append(round(bpm, 1))
        per_genre_files[preset] += 1
        scanned += 1
        if scanned % 100 == 0:
            counts = {_PRESET_NAMES[g]: per_genre_files[g] for g in sorted(per_genre_files)}
            print(f"  ... {scanned} files {counts}", file=sys.stderr)
        if cap is not None and all(per_genre_files[p] >= cap for p in _GENRE_TO_PRESET.values()):
            break

    if not per_genre_files:
        print("error: no rock-family files matched — check genre tags / lmd path",
              file=sys.stderr)
        return 1

    genres_out: dict[str, dict] = {}
    genre_weights: dict[int, list[float]] = {}
    for preset in sorted(per_genre_files):
        buckets = per_genre_buckets[preset]
        total = sum(buckets.values()) or 1
        bucket_dist = {b: c / total for b, c in buckets.items()}
        weights = _pattern_weights(bucket_dist)
        genre_weights[preset] = weights
        temps = sorted(per_genre_tempo[preset])
        genres_out[_PRESET_NAMES[preset]] = {
            "preset_id": preset,
            "files": per_genre_files[preset],
            "tempo_bpm": {
                "median": round(statistics.median(temps), 1),
                "mean": round(statistics.fmean(temps), 1),
                "p10": temps[int(0.10 * len(temps))],
                "p90": temps[int(0.90 * len(temps))],
            },
            "groove_buckets": {b: round(v, 4) for b, v in sorted(bucket_dist.items())},
            "pattern_weights": {str(i): w for i, w in enumerate(weights) if w > 0.0},
        }
        print(f"  {_PRESET_NAMES[preset]}: {per_genre_files[preset]} files, "
              f"buckets={ {b: round(v,2) for b,v in bucket_dist.items()} }", file=sys.stderr)

    # CD2 has no separate Hard Rock / Sludge genre, so those presets inherit the
    # nearest rock-family priors (Hard Rock <- Rock, Sludge <- Metal). Documented
    # so a user selecting those presets still gets a well-defined pool ordering.
    _INHERIT = {1: 0, 4: 3}
    for preset, source in _INHERIT.items():
        if preset not in genre_weights and source in genre_weights:
            genre_weights[preset] = list(genre_weights[source])
            genres_out[_PRESET_NAMES[preset]] = {
                "preset_id": preset,
                "inherits_from": _PRESET_NAMES[source],
                "pattern_weights": {str(i): w for i, w in enumerate(genre_weights[preset]) if w > 0.0},
            }

    payload = {
        "source": "Lakh MIDI lmd_matched + tagtraum CD2 MSD genre tags",
        "generator": "training/build_lakh_priors.py",
        "note": "Aggregate selection priors only; tag file not redistributed. "
                "Tempo is content-derived (drum-onset pulse), not header BPM.",
        "max_files_per_genre": args.max_files_per_genre,
        "bucket_to_patterns": {k: list(v) for k, v in _BUCKET_TO_PATTERNS.items()},
        "genres": genres_out,
    }
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.json_out}", flush=True)

    _emit_header(genre_weights, args.header_out)
    print(f"Wrote {args.header_out}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
