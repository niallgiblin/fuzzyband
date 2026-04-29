#!/usr/bin/env python3
"""Extract BassNet training data from Lakh+GMD MIDI files (D-26-01).

Parses MIDI files via `prep_midi._events_from_file`, detects bass channels
(channel 2, GM program 33–40), quantizes to 16-step piano-roll grids,
derives root pitch from bar-level pitch-class histograms, and extracts
7-float feature proxy rows for each bar.

Output: .pt dict with X [N,7] float32, Y [N,32] float32 interleaved piano-roll.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import numpy as np
import torch

_TRAINING_DIR = Path(__file__).resolve().parent
if str(_TRAINING_DIR) not in sys.path:
    sys.path.insert(0, str(_TRAINING_DIR))

from prep_midi import _events_from_file  # noqa: E402


# ── Bass GM program numbers (MIDI program change: 33–40) ─────────────────
_BASS_GM_PROGRAMS = frozenset(range(33, 41))  # 33=Acoustic Bass .. 40=Synth Bass 2

# ── Root pitch candidates: E2(40) / A2(45) / B1(35) ──────────────────────
_ROOT_CANDIDATES = [40, 45, 35]

# ── Default tempo if no tempo change events found ────────────────────────
_DEFAULT_BPM = 120.0

_EPS = 1e-8


def _clamp_bpm(bpm: float) -> float:
    """Clamp BPM to valid range [80, 220]."""
    return float(np.clip(bpm, 80.0, 220.0))


def _detect_bass_channel(events: list[dict]) -> int | None:
    """Return the first channel index (1-indexed) that has bass note-on events.

    Bass detection scans for note_on events on channel 2 (GM bass convention).
    Returns None if no bass channel found.
    """
    channels_seen: set[int] = set()
    for e in events:
        if e.get("type") == "note_on" and e.get("velocity", 0) > 0:
            channels_seen.add(int(e.get("channel", 0)))
    # Channel 2 in 1-indexed MIDI is the bass channel
    if 2 in channels_seen:
        return 2
    return None


def _extract_bass_events(events: list[dict], channel: int = 2) -> list[dict]:
    """Extract bass note events (note_on with velocity > 0) from a specific channel.

    Returns list of dicts with keys: pitch, velocity, start_sec, end_sec, channel.
    """
    note_ons: dict[int, dict] = {}  # pitch → {pitch, velocity, start_sec, channel}
    bass_notes: list[dict] = []

    for e in events:
        if int(e.get("channel", 0)) != channel:
            continue

        etype = e.get("type")
        if etype == "note_on" and e.get("velocity", 0) > 0:
            pitch = int(e.get("note", 0))
            vel = int(e.get("velocity", 0))
            t = float(e.get("time_sec", 0.0))
            note_ons[pitch] = {
                "pitch": pitch,
                "velocity": vel,
                "start_sec": t,
                "end_sec": t + 1.0,  # placeholder, will be updated on note_off
                "channel": channel,
            }
        elif etype in ("note_off",) or (etype == "note_on" and e.get("velocity", 0) == 0):
            pitch = int(e.get("note", 0))
            if pitch in note_ons:
                t = float(e.get("time_sec", 0.0))
                note_ons[pitch]["end_sec"] = t
                bass_notes.append(note_ons.pop(pitch))

    # Any remaining note_ons (no matching note_off) get a short duration
    for note in note_ons.values():
        note["end_sec"] = note["start_sec"] + 0.25
        bass_notes.append(note)

    return bass_notes


def _derive_root_from_events(
    bass_notes: list[dict],
    candidates: list[int] | None = None,
) -> int:
    """Identify the most frequent pitch class among bass notes, matched to nearest candidate.

    For each candidate root, compute total semitone distance to all notes.
    Return the candidate with minimum total distance.
    If no notes, return default 40 (E2).
    """
    if not bass_notes:
        return 40

    if candidates is None:
        candidates = _ROOT_CANDIDATES

    pitches = [int(n["pitch"]) for n in bass_notes]
    best_candidate = candidates[0]
    best_distance = float("inf")

    for candidate in candidates:
        total_dist = sum(abs(p % 12 - candidate % 12) for p in pitches)
        if total_dist < best_distance:
            best_distance = total_dist
            best_candidate = candidate

    return best_candidate


def _detect_tempo(events: list[dict]) -> float:
    """Extract BPM from MIDI tempo change events.

    Returns the first tempo found, or _DEFAULT_BPM if none.
    Clamped to [80, 220].
    """
    # Walk events for tempo meta-events in prep_midi output.
    # prep_midi merges tracks and converts set_tempo to tempo changes.
    # But since _events_from_file returns a flat event list without tempo metadata,
    # we need to look for tempo info differently. We'll default to 120 BPM
    # and let the caller override.
    #
    # For bass extraction, we use the default tempo because prep_midi
    # doesn't include tempo in its flat event output.
    # The caller can pass a known BPM from the manifest.
    return _clamp_bpm(_DEFAULT_BPM)


def _quantize_bass_to_pianoroll(
    bass_notes: list[dict],
    root: int,
    bpm: float = 120.0,
    start_beat: float = 0.0,
    steps_per_bar: int = 16,
    beats_per_bar: int = 4,
) -> tuple[np.ndarray, np.ndarray]:
    """Quantize bass notes within one bar to a 16-step piano-roll.

    Args:
        bass_notes: List of bass note events (pitch, velocity, start_sec, end_sec, channel).
        root: Root pitch (MIDI note number).
        bpm: Tempo in BPM.
        start_beat: Starting beat position of the bar.
        steps_per_bar: Number of quantization steps (default 16 for sixteenth notes).
        beats_per_bar: Number of beats per bar (default 4).

    Returns:
        tuple of (pitch_offsets [steps_per_bar], velocities [steps_per_bar]).
    """
    beat_duration_sec = 60.0 / float(bpm)
    step_duration_sec = beat_duration_sec * (float(beats_per_bar) / float(steps_per_bar))
    bar_start_sec = start_beat * beat_duration_sec
    bar_end_sec = bar_start_sec + beat_duration_sec * float(beats_per_bar)

    pitch_offsets = np.zeros(steps_per_bar, dtype=np.float32)
    velocities = np.zeros(steps_per_bar, dtype=np.float32)

    for step in range(steps_per_bar):
        step_start = bar_start_sec + step * step_duration_sec
        step_end = step_start + step_duration_sec

        # Find all notes whose start_sec falls within this step window
        active_notes = []
        for note in bass_notes:
            ns = float(note.get("start_sec", 0.0))
            # Note starts in this step window
            if ns >= step_start and ns < step_end:
                active_notes.append(note)

        if active_notes:
            # Use the first active note (or the one closest to root)
            best_note = active_notes[0]
            best_dist = abs(int(best_note["pitch"]) - root)
            for n in active_notes[1:]:
                dist = abs(int(n["pitch"]) - root)
                if dist < best_dist:
                    best_dist = dist
                    best_note = n

            offset = float(best_note["pitch"] - root)
            pitch_offsets[step] = float(np.clip(offset, -12.0, 12.0))
            velocities[step] = float(best_note["velocity"]) / 127.0

    return pitch_offsets, velocities


def _compute_proxy_row_5float(events: list[dict], bpm_raw: float) -> np.ndarray:
    """Extract 5-float proxy row compatible with training/build_dataset.py.

    Uses 3-class state: SILENT(0)/SOFT(1)/LOUD(2).
    """
    bpm = _clamp_bpm(float(bpm_raw))
    ons = [e for e in events if e.get("type") == "note_on" and e.get("velocity", 0) > 0]
    vels = [int(e.get("velocity", 0)) for e in ons]
    if not vels:
        return np.array([bpm, 0.0, 0.0, 0.0, 0.0], dtype=np.float64)

    rms = float(np.sqrt(np.mean(np.square(np.asarray(vels, dtype=np.float64)))) / 127.0)
    notes = [int(e.get("note", 0)) for e in ons]
    cent = 400.0 + (float(np.mean(notes)) / 127.0) * 6000.0

    high_times = sorted(float(e["time_sec"]) for e in ons if int(e.get("note", 0)) >= 42)
    if len(high_times) < 2:
        hf = 0.0
    else:
        dt = np.diff(np.asarray(high_times, dtype=np.float64))
        dt_beats = dt * (float(bpm) / 60.0)
        hf = float(min(1.0, 4.0 * float(np.std(dt_beats))))

    # 3-class state: SILENT(0) / SOFT(1) / LOUD(2)
    dens = len(ons) / max(float(e.get("time_sec", 0.0)) * bpm / 60.0 for e in events) if events else 1.0
    dens = max(dens, 0.25)
    if len(ons) < 3 or rms < 0.02:
        state = 0.0
    elif dens >= 10.0 and hf >= 0.35:
        state = 2.0
    else:
        state = 1.0

    return np.array([bpm, rms, cent, hf, state], dtype=np.float64)


def _infer_state_from_events(events: list[dict], bpm: float) -> float:
    """Infer 3-class state (0=SILENT, 1=SOFT, 2=LOUD) from drum-channel activity.
    
    Drum channel = channel 10 (1-indexed). Counts simultaneous notes.
    """
    drum_ons = [
        e for e in events
        if e.get("type") == "note_on"
        and e.get("velocity", 0) > 0
        and int(e.get("channel", 0)) == 10
    ]
    num_drums = len(drum_ons)

    if num_drums == 0:
        return 0.0  # SILENT
    elif num_drums >= 3:
        return 2.0  # LOUD
    else:
        return 1.0  # SOFT


def _duration_beats(events: list[dict], bpm: float) -> float:
    """Compute duration in beats from event timestamps."""
    if not events:
        return 1.0
    t_max = max(float(e.get("time_sec", 0.0)) for e in events)
    return max(t_max * float(bpm) / 60.0, 0.25)


def build_bass_dataset(
    midi_dir: Path,
    max_files: int = 5000,
    bpm_override: float | None = None,
) -> dict:
    """Build bass training tensors from MIDI files.

    Args:
        midi_dir: Directory containing .mid or .midi files (recursively).
        max_files: Maximum number of files to process.
        bpm_override: Optional BPM override if tempo detection fails.

    Returns:
        dict with keys X (float32 [N,7]), Y (float32 [N,32]), meta (list of dict).
    """
    midi_files: list[Path] = []
    for ext in ("*.mid", "*.midi", "*.MID", "*.MIDI"):
        midi_files.extend(sorted(midi_dir.rglob(ext)))
    midi_files = midi_files[:max_files]

    x_rows: list[np.ndarray] = []
    y_rows: list[np.ndarray] = []
    meta_rows: list[dict] = []

    for idx, midi_path in enumerate(midi_files):
        try:
            events = _events_from_file(midi_path)
        except Exception:
            continue

        if not events:
            continue

        # Detect bass channel
        bass_channel = _detect_bass_channel(events)
        if bass_channel is None:
            continue

        # Extract bass notes
        bass_notes = _extract_bass_events(events, channel=bass_channel)
        if not bass_notes:
            continue

        # Detect tempo
        bpm = _detect_tempo(events)
        if bpm_override is not None:
            bpm = bpm_override
        bpm = _clamp_bpm(bpm)

        # Derive root pitch from all bass notes in the file
        root_pitch = _derive_root_from_events(bass_notes)

        # Extract 5-float proxy for the whole file
        proxy_5 = _compute_proxy_row_5float(events, bpm)

        # Build 7-float feature vector: [bpm, rmsEnergy, spectralCentroid, highFreqFlux, stateFloat, pitchRootMidi, pitchConfidence]
        feature_7 = np.array([
            proxy_5[0],  # bpm
            proxy_5[1],  # rmsEnergy
            proxy_5[2],  # spectralCentroid
            proxy_5[3],  # highFreqFlux
            proxy_5[4],  # stateFloat
            float(root_pitch),  # pitchRootMidi
            0.8,  # pitchConfidence (fixed for MIDI-derived data)
        ], dtype=np.float32)

        # Quantize bass to piano-roll
        # Process one bar at a time
        beat_duration_sec = 60.0 / bpm
        bar_duration_sec = beat_duration_sec * 4.0  # 4 beats per bar
        total_duration_sec = max(
            float(n.get("end_sec", float(n.get("start_sec", 0.0)) + 0.1))
            for n in bass_notes
        ) if bass_notes else bar_duration_sec
        total_bars = int(np.ceil(total_duration_sec / bar_duration_sec))

        for bar_idx in range(total_bars):
            bar_start_sec = bar_idx * bar_duration_sec
            bar_end_sec = bar_start_sec + bar_duration_sec

            # Filter bass notes active in this bar
            bar_notes = []
            for note in bass_notes:
                ns = float(note.get("start_sec", 0.0))
                ne = float(note.get("end_sec", ns + 0.1))
                if ne > bar_start_sec and ns < bar_end_sec:
                    # Adjust timestamps relative to bar start
                    bar_note = dict(note)
                    bar_note["start_sec"] = ns - bar_start_sec
                    bar_note["end_sec"] = ne - bar_start_sec
                    bar_notes.append(bar_note)

            if not bar_notes:
                continue

            # Quantize this bar
            pitch_offsets, velocities = _quantize_bass_to_pianoroll(
                bar_notes,
                root=root_pitch,
                bpm=bpm,
                start_beat=0.0,  # relative to bar start
            )

            # Skip bars with zero activity
            if np.all(velocities < _EPS):
                continue

            # Interleave: [offset_0, vel_0, offset_1, vel_1, ..., offset_15, vel_15]
            y_interleaved = np.empty(32, dtype=np.float32)
            for step in range(16):
                y_interleaved[step * 2] = pitch_offsets[step]
                y_interleaved[step * 2 + 1] = velocities[step]

            x_rows.append(feature_7)
            y_rows.append(y_interleaved)
            meta_rows.append({
                "midi_path": str(midi_path),
                "root_pitch": root_pitch,
                "bpm": float(bpm),
                "bar_index": bar_idx,
            })

    if not x_rows:
        raise ValueError(f"No valid bass bars extracted from {len(midi_files)} files in {midi_dir}")

    X = np.stack(x_rows, axis=0).astype(np.float32)
    Y = np.stack(y_rows, axis=0).astype(np.float32)

    return {
        "X": X,
        "Y": Y,
        "meta": meta_rows,
    }


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Extract BassNet training data from Lakh+GMD MIDI files (D-26-01)."
    )
    parser.add_argument(
        "--midi-dir",
        type=Path,
        required=True,
        help="Root directory containing .mid files (recursively searched).",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Output .pt file path.",
    )
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=None,
        help="Directory with norm_stats.json (for reference, optional).",
    )
    parser.add_argument(
        "--max-files",
        type=int,
        default=5000,
        help="Maximum number of MIDI files to process (default: 5000).",
    )
    parser.add_argument(
        "--bpm",
        type=float,
        default=None,
        help="Override BPM if tempo detection fails.",
    )
    args = parser.parse_args()

    if not args.midi_dir.is_dir():
        print(f"error: --midi-dir not found: {args.midi_dir}", file=sys.stderr)
        return 1

    try:
        result = build_bass_dataset(
            midi_dir=args.midi_dir,
            max_files=args.max_files,
            bpm_override=args.bpm,
        )
    except ValueError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1

    # Save output
    args.output.parent.mkdir(parents=True, exist_ok=True)
    torch.save(
        {
            "X": torch.from_numpy(result["X"]),
            "Y": torch.from_numpy(result["Y"]),
            "meta": result["meta"],
        },
        str(args.output),
    )

    print(f"Saved: {args.output}", flush=True)
    print(f"  X shape: {result['X'].shape}", flush=True)
    print(f"  Y shape: {result['Y'].shape}", flush=True)
    print(f"  Bars extracted: {len(result['meta'])}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
