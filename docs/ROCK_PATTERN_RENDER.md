# Pattern Reference Audio — how it's made (Option B: realistic GM-kit render)

**Status:** The pattern reference audio for the 28-class mel-CNN is now produced by a
self-contained, procedural GM drum-kit renderer — **no soundfont, DAW, dataset, or
download required.** This replaces the original reference takes, which turned out to be
*low-frequency synthesized tones* (~0% energy above 3 kHz — no hats/cymbals/snare crack)
and gave the classifier a crude, unrealistic imprint.

---

## Why this exists (provenance found)

Forensics on the original `data/raw/pattern_00…21/*.wav` (all 50 takes) showed:

- mono 44.1 kHz / 24-bit, ~12–15 s, hot levels;
- **88.6% of energy below 500 Hz and ~0.000% above 3 kHz**;
- every "hit" was a **smoothly decaying tone** (~65–590 Hz, sine-like, no broadband noise).

That is the signature of a synthesized-tone render (MIDI played as pitched low tones),
not real or sample-based drums. The `.reapeaks` caches in `peaks/` show the audio went
through **REAPER**, but the exact instrument/project is not in the repo, so it can't be
reproduced. The groove *definitions* are hand-authored C++ (`src/midi/MidiPatternLibrary.cpp`);
only the feel/priors come from GMD/Lakh (see `data/MANIFEST.md`).

## The renderer (reproducible, Option B)

**`training/scripts/render_pattern_kit.py`** procedurally synthesizes a GM percussion kit
(kick / snare / hats / toms / cymbals with real high-frequency content) and renders each
pattern from `data/pattern_midi/pattern_*.mid` (drums ch 10) to mono 44.1 kHz / 24-bit WAV,
3 takes per pattern, at idiomatic tempos (blast ~224 BPM, ballad ~70 BPM, …).

Run it:

```
python3 training/scripts/render_pattern_kit.py --out-dir data/pattern_kit_wav
```

Output validation (vs the old tones):
- format: mono 44.1 kHz / 24-bit, ~15 s (matches reference format);
- **median 46% of energy above 3 kHz** (was ~0%) — real hats/cymbals/snare crack;
- silence class kept near-silent (peak ~-51 dB), drums normalized to ~0 dBFS.

The kit is already swapped into `data/raw/pattern_*`.

## Option A — render in a DAW (alternative, if you want a specific kit)

Import the per-pattern MIDI onto a GM drum kit (ch 10) + bass (ch 2), loop ~12–15 s,
render **mono 44.1 kHz / 24-bit** WAV, ≥3 takes per pattern (so the grouped split has a
held-out class). Use the same kit for all 0–27. If you do this, place the takes under
`data/raw/pattern_<idx>_<slug>/` (assemble with `training/scripts/prepare_rock_pattern_takes.py`).

## Next pipeline steps

```
python3 training/scripts/build_mel_groove_dataset.py        # -> data/processed/* (28 classes, grouped split)
python3 training/train_groove_model.py --device mps --epochs 80   # -> best_groove_model.pt (28-class)
python3 training/export_centroids.py --embedding-dim 128 \
    --checkpoint training/models/best_groove_model.pt       # -> pattern_embeddings.h + assets/metal_groove.onnx
```
Then rebuild the plugin. No C++ change needed — the selector already handles 28 patterns.

## Notes / risks

- The renderer is deterministic (seeded) and needs only numpy + scipy.
- `--include-bass` adds a simple sustained root note on ch 2 (off by default so the groove
  classifier keys on drum rhythm, not bass pitch).
- Keep the C++ `MelSpectrogramExtractor` aligned with `build_mel_groove_dataset.py`
  (n_fft 2048 / hop 512 / 64 mel / fmax 8000, per-window max-normalise, dB −80 floor).
  The known normalisation mismatch (`norm_stats.json` centroid std `1e-8`) is the first
  suspect if live inference looks near-random — see `docs/DATA_STRATEGY.md`.
- Re-rendering all 28 on **one** kit (as this does) is exactly the "same kit" guidance:
  it avoids the out-of-distribution problem of mixing a realistic render with the old tone set.
