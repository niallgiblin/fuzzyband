"""Shared pytest fixtures for the training test suite."""

from __future__ import annotations

import io
import json
import struct
import sys
from pathlib import Path

import pytest

# Make the training package importable from the tests subdirectory
_TRAINING_DIR = Path(__file__).resolve().parent.parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))


# ─── Minimal MIDI fixture ─────────────────────────────────────────────────────

def _build_minimal_mid() -> bytes:
    """Build a minimal standard MIDI file (Type 0, 120 BPM, one note-on/off pair)."""
    # MIDI header chunk: type 0, 1 track, 480 ticks/beat
    tpb = 480
    header = struct.pack(">4sIHHH", b"MThd", 6, 0, 1, tpb)

    # Track chunk body
    track_body = bytearray()

    def vlq(n: int) -> bytes:
        """Encode n as a MIDI variable-length quantity."""
        buf = [n & 0x7F]
        n >>= 7
        while n:
            buf.append((n & 0x7F) | 0x80)
            n >>= 7
        return bytes(reversed(buf))

    # dt=0: set tempo 500 000 µs/beat (120 BPM)
    track_body += vlq(0) + b"\xff\x51\x03" + struct.pack(">I", 500_000)[1:]
    # dt=0: note-on ch1, note 60, vel 80
    track_body += vlq(0) + bytes([0x90, 60, 80])
    # dt=480: note-off ch1, note 60, vel 0
    track_body += vlq(480) + bytes([0x80, 60, 0])
    # dt=0: end-of-track
    track_body += vlq(0) + b"\xff\x2f\x00"

    track = struct.pack(">4sI", b"MTrk", len(track_body)) + bytes(track_body)
    return header + track


@pytest.fixture(scope="session")
def minimal_mid(tmp_path_factory: pytest.TempPathFactory) -> Path:
    """Return path to a temporary minimal .mid file."""
    d = tmp_path_factory.mktemp("midi")
    p = d / "minimal.mid"
    p.write_bytes(_build_minimal_mid())
    return p


@pytest.fixture(scope="session")
def repo_minimal_mid() -> Path | None:
    """Return path to training/fixtures/minimal.mid if it exists, else None."""
    p = _TRAINING_DIR / "fixtures" / "minimal.mid"
    return p if p.is_file() else None
