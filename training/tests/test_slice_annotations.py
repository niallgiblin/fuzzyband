"""Tests for the annotation → clip slicing bridge (DATA_STRATEGY.md §5.3/§5.4)."""

from __future__ import annotations

from pathlib import Path

import pytest

np = pytest.importorskip("numpy")
sf = pytest.importorskip("soundfile")

import slice_annotations as sa


def _write_take(path: Path, seconds: float = 10.0, sr: int = 44100) -> None:
    n = int(seconds * sr)
    t = np.linspace(0.0, seconds, n, endpoint=False, dtype=np.float32)
    y = (0.1 * np.sin(2.0 * np.pi * 220.0 * t)).astype(np.float32)
    sf.write(str(path), y, sr)


def _write_annotations(path: Path, lines: list[str]) -> None:
    path.write_text("start_seconds,end_seconds,label\n" + "\n".join(lines) + "\n", encoding="utf-8")


def test_slices_into_per_class_dirs(tmp_path: Path) -> None:
    audio = tmp_path / "take.wav"
    ann = tmp_path / "labels.csv"
    out = tmp_path / "raw"
    _write_take(audio)
    _write_annotations(ann, ["0.0,3.0,palm_mute", "3.0,6.0,open_chord", "6.0,9.0,palm_mute"])

    rc = sa.main(["--audio", str(audio), "--annotations", str(ann), "--out-dir", str(out)])
    assert rc == 0

    palm = sorted((out / "palm_mute").glob("*.wav"))
    chord = sorted((out / "open_chord").glob("*.wav"))
    assert len(palm) == 2
    assert len(chord) == 1
    # Written clip has the expected duration (~3 s).
    info = sf.info(str(palm[0]))
    assert info.frames / info.samplerate == pytest.approx(3.0, abs=0.05)


def test_unknown_label_rejected(tmp_path: Path) -> None:
    audio = tmp_path / "take.wav"
    ann = tmp_path / "labels.csv"
    _write_take(audio)
    _write_annotations(ann, ["0.0,3.0,chorus"])
    assert sa.main(["--audio", str(audio), "--annotations", str(ann),
                    "--out-dir", str(tmp_path / "raw")]) == 1


def test_overlapping_spans_rejected(tmp_path: Path) -> None:
    ann = tmp_path / "labels.csv"
    _write_annotations(ann, ["0.0,4.0,palm_mute", "3.0,6.0,open_chord"])
    with pytest.raises(sa.SliceError):
        sa.load_annotations(ann)


def test_dry_run_writes_nothing(tmp_path: Path) -> None:
    audio = tmp_path / "take.wav"
    ann = tmp_path / "labels.csv"
    out = tmp_path / "raw"
    _write_take(audio)
    _write_annotations(ann, ["0.0,3.0,sustain"])
    rc = sa.main(["--audio", str(audio), "--annotations", str(ann),
                  "--out-dir", str(out), "--dry-run"])
    assert rc == 0
    assert not out.exists()


def test_offset_shifts_into_wav_time(tmp_path: Path) -> None:
    audio = tmp_path / "take.wav"
    ann = tmp_path / "labels.csv"
    out = tmp_path / "raw"
    _write_take(audio, seconds=10.0)
    # Annotation says 100-103 s; a -98 s offset shifts it into the 2-5 s WAV range.
    _write_annotations(ann, ["100.0,103.0,single_note"])
    rc = sa.main(["--audio", str(audio), "--annotations", str(ann),
                  "--out-dir", str(out), "--offset-seconds", "-98"])
    assert rc == 0
    assert len(list((out / "single_note").glob("*.wav"))) == 1


def test_too_short_span_skipped(tmp_path: Path) -> None:
    audio = tmp_path / "take.wav"
    ann = tmp_path / "labels.csv"
    out = tmp_path / "raw"
    _write_take(audio)
    _write_annotations(ann, ["0.0,0.1,palm_mute"])  # below MIN_CLIP_SECONDS
    rc = sa.main(["--audio", str(audio), "--annotations", str(ann), "--out-dir", str(out)])
    # No clips produced → non-zero exit + nothing written.
    assert rc == 1
    assert not (out / "palm_mute").exists()
