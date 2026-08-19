#!/usr/bin/env python3
"""Download MSD genre ground-truth tags for the Lakh selection priors (C2).

DATA_STRATEGY.md §6.2 replaces blind channel-10 + header-BPM Lakh filtering with
**genre-weighted rock subsets** using Million Song Dataset genre tags. This
fetches the tagtraum MSD genre annotations (a widely-used MSD genre ground truth)
into gitignored ``training/data/lakh/``.

We only ever extract *aggregate priors* from these tags (per-genre pattern/section
weights) — the tag file itself is never redistributed. Verify the tagtraum terms
before any redistribution (research/non-commercial); see data/MANIFEST.md.

The ``.cls`` format is one line per track: ``TRID<TAB>genre[<TAB>genre2]``.
"""

from __future__ import annotations

import argparse
import hashlib
import sys
import urllib.request
import zipfile
from pathlib import Path

_TRAINING_DIR = Path(__file__).resolve().parent
# CD2 = finer 15-genre majority-vote ground truth (tagtraum). Unlike CD1 (which
# lumps everything into "Pop_Rock"), CD2 separates Rock / Metal / Punk — exactly
# the granularity C2's rock-weighted subset needs.
_URL = "https://www.tagtraum.com/genres/msd_tagtraum_cd2.cls.zip"
_DEFAULT_DATA_DIR = _TRAINING_DIR / "data/lakh"
_ZIP_NAME = "msd_tagtraum_cd2.cls.zip"
_CLS_NAME = "msd_tagtraum_cd2.cls"


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _validate_data_dir(data_dir: Path, repo_root: Path) -> Path:
    resolved = data_dir.resolve()
    training_root = (repo_root / "training").resolve()
    try:
        resolved.relative_to(training_root)
    except ValueError:
        print(f"error: --data-dir must resolve under {training_root}, got {resolved}",
              file=sys.stderr)
        raise SystemExit(1)
    return resolved


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    repo_root = _repo_root()
    parser = argparse.ArgumentParser(description="Download MSD genre tags (tagtraum) for C2.")
    parser.add_argument("--data-dir", type=Path, default=_DEFAULT_DATA_DIR,
                        help="Directory for the tag file (default: training/data/lakh).")
    args = parser.parse_args()

    data_dir = _validate_data_dir(args.data_dir, repo_root)
    data_dir.mkdir(parents=True, exist_ok=True)
    zip_path = data_dir / _ZIP_NAME
    cls_path = data_dir / _CLS_NAME

    if cls_path.is_file():
        n = sum(1 for _ in cls_path.open("r", encoding="utf-8", errors="ignore"))
        print(f"Already present: {cls_path} ({n} lines)", flush=True)
        return 0

    if not zip_path.exists():
        print(f"Downloading {_URL} ...", file=sys.stderr)
        try:
            urllib.request.urlretrieve(_URL, str(zip_path))
        except Exception as exc:
            print(f"error: download failed: {exc}", file=sys.stderr)
            if zip_path.exists():
                zip_path.unlink()
            return 1

    print(f"SHA-256: {_sha256_file(zip_path)}", file=sys.stderr)

    with zipfile.ZipFile(str(zip_path)) as zf:
        for member in zf.namelist():
            dest = (data_dir / member).resolve()
            try:
                dest.relative_to(data_dir.resolve())
            except ValueError:
                print(f"error: zip member escapes data_dir: {member}", file=sys.stderr)
                return 2
        zf.extractall(str(data_dir))

    if not cls_path.is_file():
        # Some archives nest the .cls; find and move it up.
        found = next((p for p in data_dir.rglob(_CLS_NAME)), None)
        if found is None:
            print(f"error: {_CLS_NAME} not found after extract", file=sys.stderr)
            return 2
        cls_path = found

    n = sum(1 for _ in cls_path.open("r", encoding="utf-8", errors="ignore"))
    print(f"DONE: {cls_path} ({n} lines)", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
