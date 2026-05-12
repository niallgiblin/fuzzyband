"""Tests for training/build_dataset.py — dataset preprocessing pipeline.

These tests use a minimal synthetic MIDI file and exercise the internal helper
functions that do not require the full GMD TensorFlow Datasets download.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

# training/ must be on sys.path (set by conftest.py)
try:
    import numpy as np
    _HAS_NUMPY = True
except ImportError:
    _HAS_NUMPY = False

pytestmark = pytest.mark.skipif(not _HAS_NUMPY, reason="numpy not installed")


# ─── Import helpers under test ───────────────────────────────────────────────

@pytest.fixture(scope="module")
def build_dataset_module():
    """Import build_dataset; skip if any required dependency is missing."""
    try:
        import build_dataset as bd
        return bd
    except ImportError as exc:
        pytest.skip(f"build_dataset.py import failed: {exc}")


# ─── _clamp_bpm ──────────────────────────────────────────────────────────────

class TestClampBpm:
    def test_within_range(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._clamp_bpm(120.0) == pytest.approx(120.0)

    def test_below_min_clamped_to_80(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._clamp_bpm(40.0) == pytest.approx(80.0)

    def test_above_max_clamped_to_220(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._clamp_bpm(300.0) == pytest.approx(220.0)

    def test_boundary_80_passes(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._clamp_bpm(80.0) == pytest.approx(80.0)

    def test_boundary_220_passes(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._clamp_bpm(220.0) == pytest.approx(220.0)


# ─── _duration_beats ─────────────────────────────────────────────────────────

class TestDurationBeats:
    def test_empty_events_returns_1(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._duration_beats([], 120.0) == pytest.approx(1.0)

    def test_single_event_at_time_0(self, build_dataset_module) -> None:
        bd = build_dataset_module
        events = [{"type": "note_on", "time_sec": 0.0, "note": 60, "velocity": 80, "channel": 1}]
        result = bd._duration_beats(events, 120.0)
        # max time = 0 → clamped to minimum 0.25 beats
        assert result == pytest.approx(0.25)

    def test_1_second_at_120bpm_is_2_beats(self, build_dataset_module) -> None:
        bd = build_dataset_module
        events = [{"type": "note_on", "time_sec": 1.0, "note": 60, "velocity": 80, "channel": 1}]
        result = bd._duration_beats(events, 120.0)
        # 1.0 s × 120 BPM / 60 = 2.0 beats
        assert result == pytest.approx(2.0)


# ─── _compute_proxy_row ──────────────────────────────────────────────────────

class TestComputeProxyRow:
    def test_returns_numpy_array(self, build_dataset_module, minimal_mid: Path) -> None:
        import numpy as np
        from prep_midi import _events_from_file
        bd = build_dataset_module
        events = _events_from_file(minimal_mid)
        row = bd._compute_proxy_row(events, 120.0)
        assert isinstance(row, np.ndarray)

    def test_has_correct_feature_count(self, build_dataset_module, minimal_mid: Path) -> None:
        from prep_midi import _events_from_file
        bd = build_dataset_module
        events = _events_from_file(minimal_mid)
        row = bd._compute_proxy_row(events, 120.0)
        # Must have 5 features as defined in _FEATURE_ORDER
        assert row.shape == (5,), f"expected shape (5,), got {row.shape}"

    def test_no_nan_or_inf(self, build_dataset_module, minimal_mid: Path) -> None:
        import numpy as np
        from prep_midi import _events_from_file
        bd = build_dataset_module
        events = _events_from_file(minimal_mid)
        row = bd._compute_proxy_row(events, 120.0)
        assert not np.any(np.isnan(row)), "NaN in proxy row"
        assert not np.any(np.isinf(row)), "Inf in proxy row"

    def test_empty_events_returns_default_row(self, build_dataset_module) -> None:
        import numpy as np
        bd = build_dataset_module
        row = bd._compute_proxy_row([], 120.0)
        assert isinstance(row, np.ndarray)
        assert row.shape == (5,)
        assert not np.any(np.isnan(row))


# ─── _rule_pattern_for_state ─────────────────────────────────────────────────

class TestRulePatternForState:
    def test_silent_returns_0(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(120.0, 0.0) == 0

    def test_soft_slow_returns_1(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(100.0, 1.0) == 1

    def test_soft_mid_returns_2(self, build_dataset_module) -> None:
        # bpm=140 is between _K_SOFT_MID_BPM (120) and _K_SOFT_LOUD_BPM (160)
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(140.0, 1.0) == 2

    def test_soft_fast_returns_3(self, build_dataset_module) -> None:
        # bpm=170 >= _K_SOFT_LOUD_BPM (160)
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(170.0, 1.0) == 3

    def test_loud_mid_returns_4(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(140.0, 2.0) == 4

    def test_loud_fast_returns_5(self, build_dataset_module) -> None:
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(170.0, 2.0) == 5

    def test_gmd_state3_clamped_to_soft(self, build_dataset_module) -> None:
        # GMD uses a 4-class state (0-3). state=3 is "sparse/breakdown" which the GMD
        # labels separately from LOUD. We clamp it to SOFT (1) because GMD has no true
        # BREAKDOWN category corresponding to SILENT — sparse GMD patterns are quiet SOFT grooves.
        bd = build_dataset_module
        assert bd._rule_pattern_for_state(100.0, 3.0) == 1


# ─── _oracle_label ───────────────────────────────────────────────────────────

class TestOracleLabel:
    def test_breakdown_override_fires(self, build_dataset_module) -> None:
        # 5 sparse note_on events spread over 4 seconds at 80 BPM.
        # duration_beats = 4.0 * 80/60 = 5.33 beats; density = 5/5.33 ~= 0.94 < 2.5.
        # bpm=80 < 110 => Breakdown override fires => label == 6.
        bd = build_dataset_module
        events = [
            {"type": "note_on", "time_sec": 0.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 1.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 2.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 3.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 4.0, "note": 36, "velocity": 64},
        ]
        assert bd._oracle_label(80.0, 1.0, events) == 6

    def test_silent_never_overridden(self, build_dataset_module) -> None:
        # Same sparse events but state_float=0.0 (SILENT). D-04: SILENT rows must
        # never be overridden to Breakdown — oracle label stays 0.
        bd = build_dataset_module
        events = [
            {"type": "note_on", "time_sec": 0.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 1.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 2.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 3.0, "note": 36, "velocity": 64},
            {"type": "note_on", "time_sec": 4.0, "note": 36, "velocity": 64},
        ]
        assert bd._oracle_label(80.0, 0.0, events) == 0
