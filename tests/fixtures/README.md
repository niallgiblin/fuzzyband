# Golden-signal fixtures

Short (10 s) excerpts of real recorded guitar, used by `tests/test_golden_signal.cpp`
to catch analysis-layer regressions invisible to synthetic tests. All are 16-bit
mono PCM WAV at 44.1 kHz, converted from the raw 24-bit 44.1 kHz captures under
`data/raw/` (each several minutes long).

| Fixture | Source | Offset | Why it matters |
|---|---|---|---|
| `palm_mute_chug.wav` | `data/raw/palm_mute/palm_mute.wav` | 54.0 s | Distorted palm-mute 16th chug — YIN confidence collapses at attacks, so this locks only if the attack detector is pitch-agnostic (0.9.11 regression). |
| `thrash_chug.wav` | `data/raw/palm_mute/palm_mute2.wav` | 134.0 s | Faster palm-mute chug — exercises the note-density fix (0.9.12). |
| `open_chord_passage.wav` | `data/raw/open_chord/open_chord.wav` | 74.0 s | Ringing open chords — a real-riff case for the rhythm mirror. |
| `single_note_run.wav` | `data/raw/single_note/single_note.wav` | 99.0 s | Single-note lines — a second real-riff texture. |

## How they were produced

1. The raw captures are 24-bit mono 44.1 kHz WAV (`RIFF ... PCM`).
2. A scratch script computed the plugin's 0.1 s RMS window (ring buffer, ×4,
   clamped to [0,1]) per 512-sample block, then counted fall-armed RMS rises
   (the same attack logic as `PhraseLearner::detectAttack`) in sliding 10 s
   windows.
3. The densest 10 s window for each texture was excerpted and re-encoded to
   16-bit PCM (clipped to [-1,1], scaled by 32767).

The exact offsets are recorded above so the fixtures can be regenerated from
`data/raw/` if the capture set changes. The test itself only asserts *lock time*
and *note density* on the current code — not specific notes — so it is robust to
the guitarist's exact intonation while still failing on the detector/gate/density
regressions it guards against.
