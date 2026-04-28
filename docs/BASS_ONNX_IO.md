# ONNX I/O Contract — Generative Bass (Phase 13)

> **Status:** Frozen for milestone **v0.4.0** (Phase 22).
> **Scope:** Plugin-side tensor contract for `assets/bass_model.onnx` and `src/inference/OnnxBassInference.cpp` (implemented in the plugin).
> Requirement refs: **ONNX-05** (bass piano-roll contract), **PYTR-03** (Phase 15 export must match).

`Y_bass` uses a **piano-roll** representation: 16 sixteenth-note steps, each with a
**relative pitch offset** and **velocity**. Pitch offsets are relative intervals
(semitones from the conditioned root), not absolute MIDI note numbers. The plugin
reconstructs absolute MIDI notes during decoding by adding the offset to the root
pitch from `X_bass`.

Training and export pipelines (Phase 15) **must** produce a model that matches this contract. Changing names, shapes, dtypes, or column semantics requires a requirements update.

---

## Input tensor

| Property | Value |
|----------|-------|
| Name | `X_bass` |
| Dtype | `float32` |
| Shape | `[1, 7]` (row-major) |
| Semantics | Single feature window per bass inference step (background thread, same cadence family as other ONNX heads) |

### Feature order (row `X_bass[0]`, columns 0–6)

| Index | Field | Source (`FeatureVector`) | Notes |
|-------|-------|--------------------------|-------|
| 0 | `bpm` | `FeatureVector::bpm` | float |
| 1 | `rmsEnergy` | `FeatureVector::rmsEnergy` | float |
| 2 | `spectralCentroid` | `FeatureVector::spectralCentroid` | float; Hz |
| 3 | `highFreqFlux` | `FeatureVector::highFreqFlux` | float |
| 4 | `float(static_cast<int>(state))` | `StructureState` as numeric cast (`SILENT=0`, `SOFT=1`, `LOUD=2`) | Same convention as pattern `X` / ONNX pattern packing |
| 5 | `pitchRootMidi` | Root pitch (MIDI note number) | From Phase 11 pitch path |
| 6 | `pitchConfidence` | Pitch confidence | [0, 1] typical |

The column order aligns with **`FeatureVector`** and the conventions used by
**`OnnxInference`** / **`OnnxStructureInference`** for feature packing (extended
with pitch fields for conditioning).

---

## Output tensor

| Property | Value |
|----------|-------|
| Name | `Y_bass` |
| Dtype | `float32` |
| Shape | `[1, 32]` |
| Semantics | 16-step piano-roll: interleaved `[pitch_offset_n, velocity_n]` pairs for step `n = 0..15` |

### Column layout (row `Y_bass[0]`, 32 columns)

The output is a **flattened interleaved** sequence of 16 per-step pairs.
Each pair occupies two consecutive columns:

```
[pitch_offset_0, velocity_0, pitch_offset_1, velocity_1, ..., pitch_offset_15, velocity_15]
```

**Column indices:**

| Index | Field | Semantics |
|-------|-------|-----------|
| 2n | `pitch_offset_n` | **Relative** pitch offset in semitones from the conditioned root. Typical values: `0` (unison root), `+3` (m3), `+5` (P4), `+7` (P5), `+6` (tritone/b5), `+12` (octave), `−1` (chromatic approach below), `+1` (chromatic approach above). Ranges beyond `[−12, +12]` are clamped by the plugin. |
| 2n+1 | `velocity_n` | MIDI velocity for step `n` (`0` = silent step / rest, `1`–`127` = audible with that velocity). Float values; the plugin floors to nearest integer velocity. `0.0` is always interpreted as a rest. |

**Step timing:** Steps are evenly spaced at sixteenth-note intervals (bar-slice grid).
Step 0 aligns to beat position 0 (bar boundary); step 15 covers the last sixteenth
of the bar slice. The plugin derives absolute beat position from BPM and sample
position via `PatternPlayer` clock semantics.

**Rest handling:** Set `velocity_n = 0.0` for silent steps. The plugin emits no
MIDI note-on for a rest step. `pitch_offset_n` is ignored when velocity is zero.

**Decoding rule:** `absolute_midi_n = floor(pitchRootMidi + pitch_offset_n)`.
The plugin floors both the root and the offset before summing, then clamps to
the MIDI note range `[0, 127]`.

---

## Bundled model asset

| Property | Value |
|----------|-------|
| Asset path | `assets/bass_model.onnx` |
| Delivery | JUCE `BinaryData` symbols `BinaryData::bass_model_onnx` and `bass_model_onnxSize` (plus length pattern consistent with sibling `.onnx` assets) when `MA_ENABLE_ONNX=ON` |
| Regeneration | `training/.venv/bin/python scripts/build_minimal_bass_onnx.py` (see `CONTRIBUTING.md` §ONNX Runtime) |

The repository ships a **minimal stub** graph that satisfies the contract so integration
and tests can run before Phase 15 training. If load fails or `Run()` throws, the
processor must fall back to **static pattern bass** (no generative rank) — see
Phase 13 implementation plans.

---

## Handoff to Phase 15 (PYTR-03)

Any training or export pipeline landing in Phase 15 **must** emit a graph whose
input `X_bass` / output `Y_bass` names, shapes, dtypes, and column semantics match
this document. The spirit matches **`docs/ONNX_IO.md`**: byte-stable contracts for
the plugin, with regeneration scripts and CI posture documented in **`CONTRIBUTING.md`**.
