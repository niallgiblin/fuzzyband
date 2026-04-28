#!/usr/bin/env python3
"""Download and extract Lakh MIDI lmd_matched subset (Phase 25 DATA-07)."""

from __future__ import annotations

import argparse
import hashlib
import sys
import tarfile
import urllib.request
from pathlib import Path

_TRAINING_DIR = Path(__file__).resolve().parent
_URL = "http://hog.ee.columbia.edu/craffel/lmd/lmd_matched.tar.gz"
_DEFAULT_DATA_DIR = _TRAINING_DIR / "data/lakh"


def _repo_root() -> Path:
    return _TRAINING_DIR.parent


def _validate_data_dir(data_dir: Path, repo_root: Path) -> Path:
    """Resolved --data-dir must live under repo training/ (path traversal safe)."""
    resolved = data_dir.resolve()
    training_root = (repo_root / "training").resolve()
    try:
        resolved.relative_to(training_root)
    except ValueError:
        print(
            f"error: --data-dir must resolve under {training_root}, got {resolved}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    return resolved


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def _reporthook(block_num: int, block_size: int, total_size: int) -> None:
    """Simple progress reporter — prints a dot every 50 blocks."""
    if block_num == 0:
        return
    if block_num % 50 == 0:
        downloaded = min(block_num * block_size, total_size)
        print(
            f"\r  downloading... {downloaded / (1024 * 1024):.1f} MB",
            end="",
            file=sys.stderr,
        )


def main() -> int:
    repo_root = _repo_root()

    parser = argparse.ArgumentParser(
        description="Download Lakh MIDI lmd_matched subset (Phase 25 DATA-07)."
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=_DEFAULT_DATA_DIR,
        help="Directory for Lakh data (default: training/data/lakh)",
    )
    args = parser.parse_args()

    data_dir = _validate_data_dir(args.data_dir, repo_root)
    data_dir.mkdir(parents=True, exist_ok=True)

    tarball_path = data_dir / "lmd_matched.tar.gz"
    extracted_dir = data_dir / "lmd_matched"

    # ── Download ──────────────────────────────────────────────────────────
    if tarball_path.exists():
        print(f"Tarball already exists: {tarball_path}", file=sys.stderr)
    else:
        print(f"Downloading {_URL} ...", file=sys.stderr)
        try:
            urllib.request.urlretrieve(_URL, str(tarball_path), _reporthook)
            print("", file=sys.stderr)  # newline after progress dots
        except Exception as exc:
            print(f"error: download failed: {exc}", file=sys.stderr)
            if tarball_path.exists():
                tarball_path.unlink()
            return 1

    tarball_size_mb = tarball_path.stat().st_size / (1024 * 1024)
    print(f"Tarball: {tarball_path} ({tarball_size_mb:.1f} MB)", file=sys.stderr)

    # ── SHA-256 ───────────────────────────────────────────────────────────
    sha = _sha256_file(tarball_path)
    print(f"SHA-256: {sha}", file=sys.stderr)

    # ── Extract ───────────────────────────────────────────────────────────
    if extracted_dir.exists():
        existing = len(list(extracted_dir.rglob("*.mid")))
        print(f"Extracted dir already exists: {extracted_dir} ({existing} MIDI files)", file=sys.stderr)
    else:
        print("Extracting tarball ...", file=sys.stderr)
        try:
            with tarfile.open(str(tarball_path), "r:gz") as tf:
                # Path traversal guard: ensure all members are under data_dir
                for member in tf.getmembers():
                    member_path = (data_dir / member.name).resolve()
                    try:
                        member_path.relative_to(data_dir.resolve())
                    except ValueError:
                        print(
                            f"error: tarball member escapes data_dir: {member.name}",
                            file=sys.stderr,
                        )
                        return 2
                tf.extractall(path=str(data_dir))
        except tarfile.ReadError as exc:
            print(f"error: tarball extraction failed: {exc}", file=sys.stderr)
            return 2

    # ── Count MIDI files ──────────────────────────────────────────────────
    midi_files = sorted(extracted_dir.rglob("*.mid"))
    print(f"\nLakh lmd_matched: {len(midi_files)} MIDI files", flush=True)
    print(f"  Location: {extracted_dir}", flush=True)
    print(f"  SHA-256:  {sha}", flush=True)
    print("DONE: lmd_matched downloaded and extracted.", flush=True)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
