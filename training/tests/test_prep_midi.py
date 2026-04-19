"""Tests for training/prep_midi.py — MIDI → JSON event conversion."""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

# training/ must be on sys.path (set by conftest.py)
from prep_midi import _events_from_file


# ─── _events_from_file ────────────────────────────────────────────────────────

class TestEventsFromFile:
    def test_returns_list(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        assert isinstance(events, list)

    def test_each_event_has_required_keys(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        assert len(events) > 0
        for ev in events:
            assert "type" in ev
            assert "time_sec" in ev
            assert "channel" in ev
            assert "note" in ev
            assert "velocity" in ev

    def test_note_on_event_present(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        types = {ev["type"] for ev in events}
        assert "note_on" in types

    def test_note_off_event_present(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        types = {ev["type"] for ev in events}
        assert "note_off" in types

    def test_time_sec_is_non_negative(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        for ev in events:
            assert ev["time_sec"] >= 0.0, f"negative time_sec: {ev}"

    def test_time_sec_is_monotone(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        times = [ev["time_sec"] for ev in events]
        for i in range(1, len(times)):
            assert times[i] >= times[i - 1], (
                f"non-monotone time at index {i}: {times[i - 1]} -> {times[i]}"
            )

    def test_channel_is_1_indexed(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        for ev in events:
            assert ev["channel"] >= 1, f"channel < 1: {ev}"

    def test_velocity_is_non_negative(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        for ev in events:
            assert ev["velocity"] >= 0, f"negative velocity: {ev}"

    def test_note_number_in_range(self, minimal_mid: Path) -> None:
        events = _events_from_file(minimal_mid)
        for ev in events:
            assert 0 <= ev["note"] <= 127, f"note out of range: {ev}"


# ─── CLI entrypoint (main()) ─────────────────────────────────────────────────

class TestCliMain:
    def test_outputs_valid_json_to_file(self, minimal_mid: Path, tmp_path: Path) -> None:
        from prep_midi import main
        out = tmp_path / "out.json"
        rc = main(["--input", str(minimal_mid), "--output", str(out)])
        assert rc == 0
        assert out.exists()
        assert out.stat().st_size > 0
        events = json.loads(out.read_text())
        assert isinstance(events, list)
        assert len(events) > 0

    def test_outputs_jsonl(self, minimal_mid: Path, tmp_path: Path) -> None:
        from prep_midi import main
        out = tmp_path / "out.jsonl"
        rc = main(["--input", str(minimal_mid), "--output", str(out), "--jsonl"])
        assert rc == 0
        lines = out.read_text().splitlines()
        assert len(lines) > 0
        for line in lines:
            ev = json.loads(line)
            assert "type" in ev

    def test_missing_input_returns_nonzero(self, tmp_path: Path) -> None:
        from prep_midi import main
        rc = main(["--input", str(tmp_path / "nonexistent.mid")])
        assert rc != 0

    def test_existing_repo_fixture_if_present(self, repo_minimal_mid: Path | None) -> None:
        """Runs the CI smoke test path if the repo fixture exists."""
        if repo_minimal_mid is None:
            pytest.skip("training/fixtures/minimal.mid not committed to repo")
        from prep_midi import main
        import tempfile, os
        with tempfile.NamedTemporaryFile(suffix=".json", delete=False) as f:
            out = Path(f.name)
        try:
            rc = main(["--input", str(repo_minimal_mid), "--output", str(out)])
            assert rc == 0
            assert out.stat().st_size > 0
        finally:
            out.unlink(missing_ok=True)
