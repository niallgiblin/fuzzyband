#!/usr/bin/env python3
"""Annotation + slicing bridge: labeled take → per-class clips (DATA_STRATEGY.md §5.3/§5.4).

This is the capture → training bridge. You record a take (a WAV) and annotate the
spans you can *honestly* label — the perception taxonomy of your own playing
(palm_mute / open_chord / single_note / sustain / silence). This tool slices the
WAV at those spans and writes per-class clips under ``data/raw/<label>/`` so the
mel dataset builder (`scripts/build_mel_dataset.py`) can turn your real playing
into training data — instead of the 13k FeatureCapture frames staying
evaluation-only.

Annotation CSV format (§5.4), one labeled span per line::

    start_seconds,end_seconds,label
    0.0,4.5,palm_mute
    4.5,9.0,open_chord

Timestamps are seconds relative to the start of the WAV. If your annotations were
taken against a plugin FeatureCapture session whose clock started before the WAV,
pass ``--offset-seconds`` to shift them into WAV-relative time.

Only perception style labels are accepted — section/verse/chorus identity is NOT
self-labeled here (that comes from the arrangement layer, Phase 4).
"""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass
from pathlib import Path

from perception_taxonomy import STYLE_LABELS, is_valid_style

ANNOTATION_HEADER = ("start_seconds", "end_seconds", "label")
MIN_CLIP_SECONDS = 0.4  # a clip shorter than one mel window (~370 ms) yields nothing


class SliceError(ValueError):
    """User-facing validation failure."""


@dataclass(frozen=True)
class Span:
    start_seconds: float
    end_seconds: float
    label: str


def load_annotations(path: Path) -> list[Span]:
    """Parse and validate a ``start_seconds,end_seconds,label`` CSV."""
    with path.open("r", encoding="utf-8", newline="") as fh:
        reader = csv.reader(fh)
        rows = [r for r in reader if r and not (len(r) == 1 and not r[0].strip())]
    if not rows:
        raise SliceError("annotation CSV is empty")

    start = 0
    header = [c.strip() for c in rows[0]]
    if tuple(header[:3]) == ANNOTATION_HEADER:
        start = 1

    spans: list[Span] = []
    for line_no, row in enumerate(rows[start:], start=start + 1):
        if len(row) < 3:
            raise SliceError(f"annotation row {line_no}: expected start,end,label")
        try:
            s = float(row[0])
            e = float(row[1])
        except ValueError as exc:
            raise SliceError(f"annotation row {line_no}: non-numeric time") from exc
        label = row[2].strip()
        if e <= s:
            raise SliceError(f"annotation row {line_no}: end must be > start")
        if not is_valid_style(label):
            raise SliceError(
                f"annotation row {line_no}: unknown label {label!r}; "
                f"expected one of: {', '.join(STYLE_LABELS)}"
            )
        spans.append(Span(s, e, label))

    spans.sort(key=lambda a: a.start_seconds)
    for prev, cur in zip(spans, spans[1:]):
        if cur.start_seconds < prev.end_seconds:
            raise SliceError("annotation spans must not overlap")
    return spans


def slice_take(
    audio_path: Path,
    spans: list[Span],
    out_dir: Path,
    *,
    offset_seconds: float = 0.0,
    dry_run: bool = False,
) -> list[dict]:
    """Slice *audio_path* at each span, writing per-class clips under *out_dir*.

    Returns a list of manifest dicts describing each written clip. With
    *dry_run* the same manifest is returned but no files are written.
    """
    import soundfile as sf

    info = sf.info(str(audio_path))
    sr = info.samplerate
    n_frames = info.frames
    duration = n_frames / float(sr)
    stem = audio_path.stem

    manifest: list[dict] = []
    y = None
    if not dry_run:
        y, sr = sf.read(str(audio_path), dtype="float32", always_2d=False)

    for i, span in enumerate(spans):
        start_s = span.start_seconds + offset_seconds
        end_s = span.end_seconds + offset_seconds
        start_s = max(0.0, start_s)
        end_s = min(duration, end_s)
        if end_s - start_s < MIN_CLIP_SECONDS:
            print(
                f"  skip span {i} [{span.label}] {start_s:.2f}-{end_s:.2f}s "
                f"(< {MIN_CLIP_SECONDS}s after clamping)",
                file=sys.stderr,
            )
            continue

        clip_dir = out_dir / span.label
        clip_name = f"{stem}__{i:03d}_{span.label}.wav"
        clip_path = clip_dir / clip_name

        if not dry_run:
            clip_dir.mkdir(parents=True, exist_ok=True)
            a = int(round(start_s * sr))
            b = int(round(end_s * sr))
            sf.write(str(clip_path), y[a:b], sr)

        manifest.append({
            "label": span.label,
            "clip": str(clip_path),
            "start_seconds": round(start_s, 4),
            "end_seconds": round(end_s, 4),
            "duration_seconds": round(end_s - start_s, 4),
        })

    return manifest


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--audio", required=True, type=Path, help="recorded take (.wav)")
    parser.add_argument("--annotations", required=True, type=Path,
                        help="CSV: start_seconds,end_seconds,label")
    parser.add_argument("--out-dir", type=Path,
                        default=Path(__file__).resolve().parents[1] / "data" / "raw",
                        help="root for per-class clips (default: data/raw/)")
    parser.add_argument("--offset-seconds", type=float, default=0.0,
                        help="shift annotation times into WAV-relative time")
    parser.add_argument("--dry-run", action="store_true",
                        help="validate + report without writing clips")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(sys.argv[1:] if argv is None else argv)
    if not args.audio.is_file():
        print(f"ERROR: audio not found: {args.audio}", file=sys.stderr)
        return 1
    try:
        spans = load_annotations(args.annotations)
        manifest = slice_take(
            args.audio, spans, args.out_dir.resolve(),
            offset_seconds=args.offset_seconds, dry_run=args.dry_run,
        )
    except SliceError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    counts: dict[str, int] = {}
    for row in manifest:
        counts[row["label"]] = counts.get(row["label"], 0) + 1

    verb = "Would write" if args.dry_run else "Wrote"
    print(f"{verb} {len(manifest)} clip(s) from {args.audio.name} into {args.out_dir}")
    for label in STYLE_LABELS:
        if label in counts:
            print(f"  {label:>12s}: {counts[label]} clip(s)")
    if not manifest:
        print("WARNING: no clips produced — check annotation spans and offset.",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
