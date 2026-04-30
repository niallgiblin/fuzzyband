# Metal Accompaniment — plugin hosting

## Quick start — where does this plug-in go?

Put **Metal Accompaniment** on your **guitar track** as an **insert that sees the guitar *before*** amp simulations, distortions, and most time-based FX:

**guitar/instrument → Metal Accompaniment → amp & FX**

The plug-in analyzes your **dry** playing so tempo, onsets, and energy cues match how the accompaniment was designed.

## Why pre‑FX?

Onset detection, spectral flux, and BPM tracking assume a signal with **clear attacks** similar to DI guitar. Heavy distortion, limiting, or speaker simulation beforehand can smear attacks and shift brightness — so analysis and accompaniment quality suffer compared to inserting here.

## When post‑FX is wrong — what goes wrong?

If the plug-in sits *after* a big distortion / cab / limiter chain, you may see:

- **Unstable BPM** — readout jumps or stalls because the harmonic content no longer matches the expected guitar envelope.
- **Sluggish onset response** — pick attacks are softened, so rhythmic features lag.
- **Misleading RMS / centroid / flux** — pattern and structure inference may not match how you perceive the groove.

This plug-in is **designed for dry or lightly processed guitar upstream** — not as an insert on a fully mastered guitar bus.

## Reaper

After building or installing the VST3, copy it under `~/Library/Audio/Plug-Ins/VST3/` on macOS, then **re-scan VST** in Reaper. See the root [README.md](../README.md) (**Build** / **Reaper setup**) for full paths and troubleshooting (FX browser search — **Metal Accompaniment**, category **Tools**).

## Research & roadmap — v0.5.0 rhythmic coherence

Structured design notes and milestone scope for rhythmic coherence live under:

- [`.planning/research/rhythmic-coherence/`](../.planning/research/rhythmic-coherence/) — research index for this milestone.
- [`.planning/ROADMAP.md`](../.planning/ROADMAP.md) — **v0.5.0 — Rhythmic Coherence** (Phases 27–30).

Some clones may not include every Markdown file in the research folder until synced; the index explains what belongs there.

## Contributors

- [CONTRIBUTING.md](../CONTRIBUTING.md) — build options, ONNX, tests  
- [ARCHITECTURE.md](../ARCHITECTURE.md) — threading, analysis ↔ inference wiring  
