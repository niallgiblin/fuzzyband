#!/usr/bin/env python3
"""C3 (DATA_STRATEGY.md §6.3): DadaGP → guitar articulation grammar.

DadaGP is ~26k GuitarPro songs tokenized as text (rock/metal-heavy). We do **not**
use it for accompaniment directly — we use it to learn the *articulation grammar*
of real riffs (palm-mute vs open-chord vs single-note vs sustain) and map it onto
the **perception taxonomy** (``training/perception_taxonomy.py``). The result
augments/validates the perception classifier where distortion collapses the
spectral centroid; **human self-labels stay authoritative** for real audio
(§6.3) — this output is only a corroborating symbolic prior.

DadaGP token stream (one song per ``.txt``), the tokens we care about:
  * ``new_measure``                       — bar boundary
  * ``wait:<ticks>``                       — advance time (a rest between events)
  * ``<inst><n>:note:s<string>:f<fret>``   — a struck note on a guitar instrument
  * ``<inst><n>:nfx:palm_mute`` / ``...:dead_note`` / ``...:let_ring`` / ``...:harmonic``
    (also accepted as ``:effect:palm_mute`` for tolerance)
Only guitar instruments (distorted/clean/guitar) are considered; bass and drums
are ignored.

DadaGP requires accepting its terms / access request (see data/MANIFEST.md); it
is not bundled. Point ``--tokens-dir`` at your local DadaGP token files. The unit
test proves the extractor on a synthetic fixture so CI needs no download.

Output: ``data/dadagp_articulation.json`` — per-perception-label event counts +
normalized distribution, usable to augment/validate the perception classifier.
"""

from __future__ import annotations

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from perception_taxonomy import STYLE_LABELS, is_valid_style  # noqa: E402

_REPO_ROOT = _TRAINING_DIR.parent
_DEFAULT_TOKENS = _TRAINING_DIR / "data/dadagp/tokens"
_DEFAULT_JSON_OUT = _REPO_ROOT / "data/dadagp_articulation.json"

_GUITAR_PREFIXES = ("distorted", "clean", "guitar")
# A single note held past this many ticks (with no re-strike) reads as a sustain
# rather than a plucked single note. DadaGP wait tokens are in GuitarPro ticks
# (960 per quarter by convention); ~a half note at that resolution.
_SUSTAIN_WAIT_TICKS = 960


def _is_guitar_token(tok: str) -> bool:
    head = tok.split(":", 1)[0]
    return any(head.startswith(p) for p in _GUITAR_PREFIXES)


def _wait_ticks(tok: str) -> int:
    try:
        return int(tok.split(":", 1)[1])
    except (IndexError, ValueError):
        return 0


def classify_group(note_count: int, effects: set[str], next_wait: int) -> str | None:
    """Map one time-aligned guitar event group to a perception style label.

    Returns None for a group that carries no articulation signal (e.g. a bar with
    only non-guitar activity) so callers can skip it.
    """
    if "palm_mute" in effects or "dead_note" in effects:
        return "palm_mute"
    if note_count >= 2:
        return "open_chord"
    if note_count == 1:
        if "let_ring" in effects or next_wait >= _SUSTAIN_WAIT_TICKS:
            return "sustain"
        return "single_note"
    return None  # no guitar notes in this group


def _extract_effect(tok: str) -> str | None:
    """Pull the effect name from a guitar nfx/effect token, else None."""
    for sep in (":nfx:", ":effect:"):
        if sep in tok:
            return tok.split(sep, 1)[1]
    return None


def iter_articulations(tokens: list[str]):
    """Yield a perception label per guitar event group in a token stream.

    A "group" is the set of guitar notes/effects between successive time advances
    (``wait:``) or bar boundaries (``new_measure``). ``silence`` is emitted for a
    rest — a time advance with no guitar notes since the last advance.
    """
    note_count = 0
    effects: set[str] = set()
    saw_any_guitar = False

    def flush(next_wait: int):
        nonlocal note_count, effects, saw_any_guitar
        if note_count > 0:
            label = classify_group(note_count, effects, next_wait)
            if label is not None:
                yield_val = label
            else:
                yield_val = None
        elif saw_any_guitar is False and next_wait > 0:
            yield_val = "silence"  # a rest with no guitar notes
        else:
            yield_val = None
        note_count = 0
        effects = set()
        saw_any_guitar = False
        return yield_val

    for tok in tokens:
        if tok.startswith("wait:"):
            w = _wait_ticks(tok)
            val = flush(w)
            if val is not None:
                yield val
            continue
        if tok == "new_measure":
            val = flush(0)
            if val is not None:
                yield val
            continue
        if not _is_guitar_token(tok):
            continue
        saw_any_guitar = True
        if ":note:" in tok:
            note_count += 1
        else:
            eff = _extract_effect(tok)
            if eff:
                effects.add(eff)

    # Trailing group at end of stream.
    val = flush(0)
    if val is not None:
        yield val


def _tokens_from_file(path: Path) -> list[str]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    return text.split()


def build_distribution(tokens_dir: Path, max_files: int | None = None) -> tuple[Counter, int]:
    counts: Counter = Counter()
    files = 0
    for path in sorted(tokens_dir.rglob("*.txt")):
        for label in iter_articulations(_tokens_from_file(path)):
            counts[label] += 1
        files += 1
        if max_files is not None and files >= max_files:
            break
    return counts, files


def main() -> int:
    parser = argparse.ArgumentParser(description="C3: DadaGP → articulation grammar.")
    parser.add_argument("--tokens-dir", type=Path, default=_DEFAULT_TOKENS,
                        help="Directory of DadaGP .txt token files "
                             "(not bundled — see data/MANIFEST.md).")
    parser.add_argument("--json-out", type=Path, default=_DEFAULT_JSON_OUT)
    parser.add_argument("--max-files", type=int, default=None)
    args = parser.parse_args()

    if not args.tokens_dir.is_dir():
        print(f"error: {args.tokens_dir} not found. DadaGP is access-gated and not "
              f"bundled — download the token set and pass --tokens-dir. "
              f"See data/MANIFEST.md (C3).", file=sys.stderr)
        return 1

    counts, files = build_distribution(args.tokens_dir, args.max_files)
    if files == 0:
        print(f"error: no .txt token files under {args.tokens_dir}", file=sys.stderr)
        return 1

    total = sum(counts.values()) or 1
    dist = {label: round(counts.get(label, 0) / total, 4) for label in STYLE_LABELS}
    # Guard: every emitted label must be a known perception style.
    for label in counts:
        assert is_valid_style(label), f"extractor produced non-taxonomy label {label!r}"

    payload = {
        "source": "DadaGP (GuitarPro token corpus) — access-gated, not redistributed",
        "generator": "training/build_dadagp_articulation.py",
        "role": "augments/validates the perception classifier; human labels remain "
                "authoritative for real audio (DATA_STRATEGY.md §6.3)",
        "files": files,
        "events": int(total),
        "counts": {label: int(counts.get(label, 0)) for label in STYLE_LABELS},
        "distribution": dist,
    }
    args.json_out.parent.mkdir(parents=True, exist_ok=True)
    args.json_out.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {args.json_out} ({files} files, {total} articulation events)", flush=True)
    for label in STYLE_LABELS:
        print(f"  {label}: {counts.get(label, 0)} ({dist[label]:.1%})", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
