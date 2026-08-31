#!/usr/bin/env python3
"""Render the 28 patterns through a proper GM-style drum kit (Option B).

The original pattern reference audio (data/raw/pattern_00..21/*.wav) turned out to
be *synthesized low-frequency tones* — ~0% of energy above 3 kHz (no hat/cymbal/
snare-crack), because it was rendered as pitched tones, not real drums. The mel-CNN
was trained on that crude imprint.

This script renders each pattern from data/pattern_midi/ through a procedural
GM percussion kit (kick / snare / hats / toms / cymbals with real high-frequency
content), producing mono 44.1 kHz / 24-bit WAVs that match the reference format but
sound like actual drums. It renders ALL 28 patterns (0-27) on ONE kit so the rock
patterns (22-27) get the same treatment as 0-21, and so the classifier finally has
real drum timbre to learn from.

The render is deterministic (seeded RNG) and needs only numpy + scipy — no
soundfont, no DAW, no external downloads, no license risk.

Usage:
  python3 training/scripts/render_pattern_kit.py \
      [--midi-dir data/pattern_midi] [--out-dir data/pattern_kit_wav] \
      [--loop-seconds 15] [--include-bass]
"""

from __future__ import annotations

import argparse
import math
import sys
import wave
from pathlib import Path

import numpy as np
import scipy.signal as ss

_SR = 44100
_TARGET_BITS = 3          # 24-bit
_TARGET_SECONDS = 15.0
_KIT_CHANNEL = 10         # drums (GM ch 10 = SMF index 9)
_BASS_CHANNEL = 2

# Idiomatic tempo per pattern index (the original renders were tempo-appropriate,
# e.g. blast ~224 BPM, chorus-mid ~132 BPM). Beats in the MIDI are tempo-free; we
# re-time them at these tempos.
_TEMPO = {
    0: 120, 1: 120, 2: 90, 3: 165, 4: 132, 5: 185, 6: 90, 7: 80, 8: 224, 9: 80,
    10: 185, 11: 120, 12: 120, 13: 132, 14: 132, 15: 90, 16: 110, 17: 120, 18: 120,
    19: 120, 20: 120, 21: 205, 22: 110, 23: 90, 24: 120, 25: 160, 26: 70, 27: 100,
}

# GM kit instrument -> the Sound it triggers
_MIDI_NOTE_TO_SOUND = {
    36: "kick", 35: "kick",
    38: "snare", 40: "snare",
    42: "hat_closed", 44: "hat_pedal", 46: "hat_open",
    48: "tom_hi", 45: "tom_mid", 41: "tom_lo", 43: "tom_hi",
    49: "crash", 51: "ride", 53: "ride_bell", 52: "china", 55: "splash",
}


# ── SMF parsing (self-contained; no mido dependency) ─────────────────────────
def _vlq(data: bytes, i: int) -> tuple[int, int]:
    v = 0
    while True:
        c = data[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not (c & 0x80):
            return v, i


def parse_smf(path: Path) -> list[tuple[int, int, float, int]]:
    """Return [(channel, note, beat, velocity)] from note-on events.

    Times are in *beats* (ticks / PPQ), tempo-independent.
    """
    data = path.read_bytes()
    assert data[:4] == b"MThd", "not a MIDI file"
    ntrks = int.from_bytes(data[10:12], "big")
    div = int.from_bytes(data[12:14], "big")

    events: list[tuple[int, int, float, int]] = []
    i = 14
    for _ in range(ntrks):
        while i < len(data) and data[i:i + 4] != b"MTrk":
            i += 1
        if i >= len(data):
            break
        ln = int.from_bytes(data[i + 4:i + 8], "big")
        tr = data[i + 8:i + 8 + ln]
        i = i + 8 + ln
        j = 0
        beat = 0.0
        status = 0
        while j < len(tr):
            d, j = _vlq(tr, j)
            beat += d / div
            c = tr[j]
            if c & 0x80:
                status = c; j += 1
            hi = status >> 4
            lo = status & 0x0F
            if hi in (0x9, 0x8):
                note = tr[j]; vel = tr[j + 1]; j += 2
                # no running-status data bytes here; but SMF can run status
                if hi == 0x9 and vel > 0:
                    events.append((lo + 1, note, beat, vel))
            elif hi in (0xC, 0xD):
                j += 1
            elif hi == 0xE:
                j += 2
            elif c == 0xFF:
                # meta: type + len + payload
                mt = tr[j]; j += 1
                mlen, j = _vlq(tr, j)
                j += mlen
            elif c == 0xF0 or c == 0xF7:
                slen, j = _vlq(tr, j)
                j += slen
            else:
                j += 1
    return events


# ── GM-style drum synthesis ─────────────────────────────────────────────────
def _bandpass(x: np.ndarray, lo: float, hi: float, fs: int) -> np.ndarray:
    sos = ss.butter(3, [max(lo, 20.0), min(hi, 20000.0)], "bandpass", fs=fs, output="sos")
    return ss.sosfilt(sos, x)


def _env(n: int, decay: float, fs: int) -> np.ndarray:
    t = np.arange(n) / fs
    return np.exp(-t / decay)


def _kick(fs: int) -> np.ndarray:
    n = int(fs * 0.45)
    t = np.arange(n) / fs
    f = 45.0 + 150.0 * np.exp(-t / 0.028)              # pitch sweep 150 -> ~45 Hz
    phase = 2 * np.pi * np.cumsum(f) / fs
    body = np.sin(phase) * np.exp(-t / 0.16)
    click = np.random.randn(int(fs * 0.004)) * 0.6 * np.exp(-np.arange(int(fs * 0.004)) / (0.001 * fs))
    out = body.copy(); out[:len(click)] += click
    return out


def _snare(fs: int) -> np.ndarray:
    n = int(fs * 0.24)
    t = np.arange(n) / fs
    tone = np.sin(2 * np.pi * 190 * t) * np.exp(-t / 0.05)
    thump = np.sin(2 * np.pi * 70 * t) * np.exp(-t / 0.045) * 0.5
    noise = _bandpass(np.random.randn(n), 1500, 6000, fs) * np.exp(-t / 0.045) * 1.4
    return tone + thump + noise


def _tom(fs: int, freq: float) -> np.ndarray:
    n = int(fs * 0.32)
    t = np.arange(n) / fs
    body = np.sin(2 * np.pi * freq * t) * np.exp(-t / 0.11)
    body += np.sin(2 * np.pi * freq * 1.5 * t) * np.exp(-t / 0.07) * 0.25
    return body


def _cymbal(fs: int, decay: float, lo: float, hi: float, ping: float | None = None) -> np.ndarray:
    n = int(fs * decay * 4)
    t = np.arange(n) / fs
    noise = _bandpass(np.random.randn(n), lo, hi, fs) * np.exp(-t / decay)
    if ping is not None:
        noise += np.sin(2 * np.pi * ping * t) * np.exp(-t / (decay * 0.7)) * 0.25
    return noise


def _hat(fs: int, decay: float, lo: float, hi: float) -> np.ndarray:
    n = int(fs * decay * 3)
    t = np.arange(n) / fs
    return _bandpass(np.random.randn(n), lo, hi, fs) * np.exp(-t / decay)


def synth_sound(sound: str, fs: int) -> np.ndarray:
    if sound == "kick":      return _kick(fs)
    if sound == "snare":     return _snare(fs)
    if sound == "tom_lo":    return _tom(fs, 95)
    if sound == "tom_mid":   return _tom(fs, 145)
    if sound == "tom_hi":    return _tom(fs, 205)
    if sound == "hat_closed": return _hat(fs, 0.03, 7000, 12500)
    if sound == "hat_pedal":  return _hat(fs, 0.02, 7000, 12500)
    if sound == "hat_open":   return _hat(fs, 0.42, 6000, 11500)
    if sound == "crash":      return _cymbal(fs, 1.3, 4500, 11000, 1300)
    if sound == "ride":       return _cymbal(fs, 0.7, 5000, 9500, 950)
    if sound == "ride_bell":  return _cymbal(fs, 0.32, 4000, 9000, 1800)
    if sound == "china":      return _cymbal(fs, 1.1, 3500, 9000, 1200)
    if sound == "splash":     return _cymbal(fs, 0.45, 5000, 11000, 2000)
    return np.zeros(1, dtype=np.float32)


# ── Rendering ────────────────────────────────────────────────────────────────
def render_pattern(events: list[tuple[int, int, float, int]], bpm: float,
                   target_seconds: float, fs: int, include_bass: bool) -> np.ndarray:
    # Keep only the requested channels.
    ev = [e for e in events if e[0] == _KIT_CHANNEL or (include_bass and e[0] == _BASS_CHANNEL)]
    if not ev:
        # Silent pattern: a very low noise floor (like the original takes) so the
        # silence class is near-silent but not literally all-zero (avoids a
        # degenerate all-zero mel window).
        quiet = np.random.randn(int(fs * target_seconds)).astype(np.float32) * 0.0005
        return quiet

    sec_per_beat = 60.0 / bpm
    max_beat = max(e[2] for e in ev)
    loop_beats = max(1.0, math.ceil(max_beat / 4.0) * 4.0)  # bar-align the loop
    loop_sec = loop_beats * sec_per_beat

    # Render one loop, then tile to the target length.
    loop = np.zeros(int(loop_sec * fs) + int(fs * 1.6), dtype=np.float64)
    for ch, note, beat, vel in ev:
        if ch == _BASS_CHANNEL:
            # simple sustained root note (sine) so the bass is audible but subtle
            freq = 440.0 * 2.0 ** ((note - 69) / 12.0)
            start = int(beat * sec_per_beat * fs)
            dur = int(fs * (0.9 * sec_per_beat))
            t = np.arange(dur) / fs
            tone = np.sin(2 * np.pi * freq * t) * np.exp(-t / 0.5) * 0.4
            end = min(start + dur, len(loop))
            if start < len(loop):
                loop[start:end] += tone[:end - start] * (vel / 127.0)
            continue
        sound = _MIDI_NOTE_TO_SOUND.get(note, "kick")
        sig = synth_sound(sound, fs)
        start = int(beat * sec_per_beat * fs)
        sig = sig * (0.5 + 0.5 * (vel / 127.0))          # velocity -> amplitude
        end = min(start + len(sig), len(loop))
        if start < len(loop):
            loop[start:end] += sig[:end - start]

    # Normalize loop peak to ~0.7 (headroom), then tile.
    peak = np.max(np.abs(loop))
    if peak > 0:
        loop *= 0.7 / peak

    reps = int(math.ceil(target_seconds / loop_sec))
    take = np.tile(loop, reps)[: int(fs * target_seconds)]
    return take.astype(np.float32)


def write_wav_24(path: Path, x: np.ndarray, fs: int, normalize: bool = True) -> None:
    peak = np.max(np.abs(x))
    if normalize and peak > 0:
        x = x / peak * (2 ** 23 - 2)                     # normalize to near full scale, no clip
    else:
        x = x * (2 ** 23 - 2)                            # preserve the (quiet) level as-is
    x16 = np.clip(np.round(x), -(2 ** 23), 2 ** 23 - 1).astype(np.int32)
    raw = np.zeros((len(x16), 3), dtype=np.uint8)
    raw[:, 0] = x16 & 0xFF
    raw[:, 1] = (x16 >> 8) & 0xFF
    raw[:, 2] = (x16 >> 16) & 0xFF
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(3)
        w.setframerate(fs)
        w.writeframes(raw.tobytes())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = Path(__file__).resolve().parents[2]
    ap.add_argument("--midi-dir", type=Path, default=root / "data" / "pattern_midi")
    ap.add_argument("--out-dir", type=Path, default=root / "data" / "pattern_kit_wav")
    ap.add_argument("--loop-seconds", type=float, default=_TARGET_SECONDS)
    ap.add_argument("--include-bass", action="store_true")
    args = ap.parse_args()

    midi_dir = args.midi_dir.resolve()
    out_dir = args.out_dir.resolve()
    if not midi_dir.is_dir():
        print(f"ERROR: midi dir not found: {midi_dir}", file=sys.stderr)
        return 1
    out_dir.mkdir(parents=True, exist_ok=True)

    midis = sorted(midi_dir.glob("pattern_*.mid"))
    print(f"Rendering {len(midis)} patterns at 44.1kHz/24-bit -> {out_dir}")
    for m in midis:
        idx = int(m.stem.split("_")[1])
        events = parse_smf(m)
        bpm = _TEMPO.get(idx, 120)
        d = out_dir / m.stem
        d.mkdir(exist_ok=True)
        for take in range(1, 4):                          # 3 takes per pattern (grouped split)
            # re-roll the RNG per take so each take is a distinct performance
            np.random.seed(1000 + idx * 100 + take)
            x2 = render_pattern(events, bpm, args.loop_seconds, _SR, args.include_bass)
            write_wav_24(d / f"{m.stem}_{take}.wav", x2, _SR, normalize=(idx != 0))
            print(f"  {m.stem}/take_{take}: bpm={bpm} dur={len(x2)/_SR:.1f}s")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
