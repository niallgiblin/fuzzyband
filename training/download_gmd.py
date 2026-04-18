#!/usr/bin/env python3
"""Download and verify Groove MIDI Dataset (GMD) via TensorFlow Datasets (Phase 17 DATA-04)."""

from __future__ import annotations

import argparse
import hashlib
import json
import sys
from pathlib import Path

# Pinned dataset (must match build_dataset.py)
_DATASET_SPEC = "groove/full-midionly:2.0.1"
_EXPECTED_ZIP_SHA256 = (
    "651cbc524ffb891be1a3e46d89dc82a1cecb09a57c748c7b45b844c4841dcc1e"
)
_ZIP_NAME = "groove-v1.0.0-midionly.zip"


def _repo_root() -> Path:
    return Path(__file__).resolve().parent.parent


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


def _find_midionly_zip(data_dir: Path) -> Path | None:
    for p in data_dir.rglob(_ZIP_NAME):
        if p.is_file():
            return p
    return None


def _sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> int:
    try:
        import tensorflow_datasets as tfds  # noqa: PLC0415
    except ImportError as exc:
        print(f"error: tensorflow_datasets import failed: {exc}", file=sys.stderr)
        print(
            "error: install training deps: pip install -r training/requirements.txt",
            file=sys.stderr,
        )
        return 1

    repo_root = _repo_root()
    default_data = repo_root / "training/data/tfds"

    parser = argparse.ArgumentParser(description="GMD download + SHA-256 verify (Phase 17).")
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=default_data,
        help="TFDS data_dir (default: training/data/tfds)",
    )
    parser.add_argument(
        "--download-only",
        action="store_true",
        help="Only ensure TFDS cache + checksum (default behavior; flag for documentation).",
    )
    parser.add_argument(
        "--smoke",
        action="store_true",
        help="After verification, load train split and print one example id.",
    )
    args = parser.parse_args()

    _ = args.download_only  # idempotent regardless; kept for CLI parity with plan

    data_dir = _validate_data_dir(args.data_dir, repo_root)
    data_dir.mkdir(parents=True, exist_ok=True)

    try:
        _ = tfds.load(
            _DATASET_SPEC,
            data_dir=str(data_dir),
            download=True,
        )
    except Exception as exc:
        print(f"error: tfds.load({_DATASET_SPEC!r}) failed: {exc}", file=sys.stderr)
        try:
            builders = tfds.list_builders()
            print(f"hint: tfds has {len(builders)} builders (showing first 30):", file=sys.stderr)
            print(", ".join(sorted(builders)[:30]), file=sys.stderr)
        except Exception:
            pass
        return 1

    zip_path = _find_midionly_zip(data_dir)
    if zip_path is None:
        print(
            f"error: could not find {_ZIP_NAME} under {data_dir} after download",
            file=sys.stderr,
        )
        return 2

    actual = _sha256_file(zip_path)
    if actual != _EXPECTED_ZIP_SHA256:
        print(
            f"error: SHA-256 mismatch for MIDI-only zip: expected {_EXPECTED_ZIP_SHA256}, got {actual}",
            file=sys.stderr,
        )
        return 2

    manifest_dir = repo_root / "training/data"
    manifest_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = manifest_dir / "gmd_download_manifest.json"
    manifest = {
        "dataset": _DATASET_SPEC,
        "data_dir": str(data_dir),
        "expected_zip_sha256": _EXPECTED_ZIP_SHA256,
        "actual_zip_sha256": actual,
        "zip_path": str(zip_path),
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    if not args.smoke:
        print(f"OK: GMD downloaded and SHA-256 verified ({_ZIP_NAME}).", flush=True)

    if args.smoke:
        ds = tfds.load(_DATASET_SPEC, data_dir=str(data_dir), split="train", download=False)
        for ex in ds.take(1):
            raw_id = ex.get("id")
            if raw_id is None:
                print("smoke: example keys:", list(ex.keys()), flush=True)
                return 0
            if hasattr(raw_id, "numpy"):
                eid = raw_id.numpy().decode("utf-8")
            else:
                eid = str(raw_id)
            print(f"smoke: example id={eid}", flush=True)
            break

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
