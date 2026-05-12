# Feature proxies — MIDI → ONNX pattern row (Phase 17)

## Purpose

This document defines **offline MIDI-derived proxies** for the five floats in **`docs/ONNX_IO.md`** pattern input **`X[0,5]`**, plus the **hybrid label oracle** for **`Y ∈ {0,…,6}`**. It targets **Phase 17–18** dataset construction (`build_dataset.py`). It is **not** a specification of live guitar analysis: **`OnnxInference`** reads **audio-thread** features from **`FeatureVector`**.

## Frozen input layout (matches `docs/ONNX_IO.md`)

| Index | Field name | Runtime source (plugin) | Phase 17 proxy (MIDI) |
|-------|------------|-------------------------|------------------------|
| 0 | `bpm` | `FeatureVector::bpm`, clamped **[80, 220]** by `OnsetDetector` | Use GMD example **`bpm`** metadata; cast to `float32`, clamp to **[80, 220]** (same clamp applied to training rows) |
| 1 | `rmsEnergy` | Rolling **100 ms** RMS, **~[0, 1]** | **RMS proxy:** over the full performance window, take all **`note_on` velocities** \(v_i\); **RMS\_vel** \(=\sqrt{\mathrm{mean}_i(v_i^2)}\); **proxy** \(=\mathrm{RMS\_vel}/127.0\) (dimensionless, comparable to ~[0,1]) |
| 2 | `spectralCentroid` | **Hz** (audio spectral centroid) | **Brightness proxy (Hz):** \(400 + (\overline{n}/127)\cdot 6000\) where \(\overline{n}\) is the mean MIDI note number over **`note_on`** events (drums skew high; acts as a monotonic “brightness” statistic). If **no** `note_on`, use **0.0** (matches “no energy” semantics; exporters still see a float) |
| 3 | `highFreqFlux` | **2 kHz+ band flux** (audio) | **Onset-irregularity proxy (unitless, ~small positive):** collect `note_on` times for “high” drums (**note ≥ 42**). If fewer than **2** such events, **0.0**. Else let \(\Delta t_k\) be inter-onset times in **beats** (\(\Delta t_k = (time_{k+1}-time_k)\cdot bpm/60\)); **proxy** \(=\min(1.0,\ 4\cdot \mathrm{std}(\Delta t_k))\) (emphasizes hat/cymbal chatter vs steady kicks) |
| 4 | `float(state)` | `static_cast<float>(static_cast<int>(StructureState))` — **`SILENT=0`, `SOFT=1`, `LOUD=2`** | **MIDI section mapping** (three-class, Phase 25): **(1)** If **total `note_on` count < 3** OR **RMS proxy < 0.02** → **SILENT (0.0)**. **(2)** Else if **dens ≥ 10.0** and **highFreqFlux proxy ≥ 0.35** → **LOUD (2.0)**. **(3)** Else → **SOFT (1.0)** |

**Units / ranges:** Indices **0–4** follow **`docs/ONNX_IO.md`** and **`FeatureVector.h`** semantics: **bpm** float; **rmsEnergy** ~[0,1]; **spectralCentroid** Hz; **highFreqFlux** small positive float; **state** **0–2** cast to float **without** softmax.

## Domain gap (MIDI vs audio)

Phase 17 proxies are **symbolic / MIDI-statistical** approximations derived from **GMD** performances. **`OnnxInference`** consumes **audio-thread** features (STFT-derived RMS, centroid, HF flux, structure tagger). **Do not** treat offline rows as **pointwise identical** to live guitar features. Phase 18 **domain adaptation** (if any) is **out of scope** for this document; this file **must not** claim **identity** between MIDI proxies and runtime audio features.

## Pattern class index — `Y` ∈ [0, 6]

Constructor order in `MidiPatternLibrary.cpp` (`patterns.push_back`):

0. **Silent** — `Silent`  
1. **Verse slow** — `Verse slow`  
2. **Verse mid** — `Verse mid`  
3. **Verse fast** — `Verse fast`  
4. **Chorus mid** — `Chorus mid`  
5. **Chorus fast** — `Chorus fast`  
6. **Breakdown** — `Breakdown`

## Label oracle — Y ∈ [0, 6]

Labels are assigned by a Python port of `PatternRules::rulePatternForState`
(`src/inference/pattern_rules.h`) applied to each proxy row. This makes the
pattern model a learned distillation of the rule path, not an activity ranker.

### policyIntensity assumption

Training proxy rows have no `policyIntensity` field. The oracle assumes neutral
intensity (0.5), which produces `adjustedBpm = raw_bpm` (zero offset). This is
**intentional** — not an oversight. Phase 35 will pack `adjustedBpm` into X[0]
and retrain with it; Phase 32 changes only label semantics.

### Rule oracle (classes 0–5)

| Condition | Label |
|-----------|-------|
| state = SILENT (0) | 0 |
| state = SOFT (1) AND bpm < 120 | 1 |
| state = SOFT (1) AND 120 ≤ bpm < 160 | 2 |
| state = SOFT (1) AND bpm ≥ 160 | 3 |
| state = LOUD (2) AND bpm < 160 | 4 |
| state = LOUD (2) AND bpm ≥ 160 | 5 |

Thresholds: `kSoftMidBpmThreshold = 120.0`, `kSoftLoudBpmThreshold = 160.0`
(mirrored exactly from `src/inference/pattern_rules.h`).

GMD-specific note: `build_dataset.py`'s `_compute_proxy_row` can emit `state = 3.0`
(low-density, kick-heavy rows). The oracle clamps this to `min(int(state), 2) = SOFT`
before lookup. `build_lakh_dataset.py` uses strict 3-class state and is unaffected.

### Breakdown heuristic (class 6)

After the rule oracle assigns a label, a narrow override promotes to class 6
when all three conditions hold:
- `state != SILENT` (rule oracle result ≠ 0)
- note density < 2.5 onsets/beat
- bpm < 110

This is a post-oracle override: the state guard ensures SILENT rows (class 0)
are never overridden to Breakdown.

## Heuristic detail (implementation-facing)

- **BPM (index 0):** As above; always clamp **[80,220]** for training stability.
- **RMS proxy (index 1):** As above — uses **velocity RMS / 127**.
- **Spectral centroid proxy (index 2):** Linear map from mean MIDI pitch to **[400, 6400]** Hz band (documented formula).
- **HF flux proxy (index 3):** Standard deviation of **beat-scaled** inter-onset gaps for **high** drums, capped at **1.0**.
- **`float(state)` (index 4):** Discrete **0–2** rules above; cast to float for the fifth column.

**Heuristic class guess `h` (coarse):** Labels are assigned by rule-oracle + Breakdown heuristic. See **Label oracle** section above.

## Failure modes

- **Empty or parse-failed MIDI:** Treat as **Silent** for labeling purposes (**Y = 0**) and **zero/low** proxies where defined; **RMS** 0, **centroid** 0, **HF flux** 0, **state** 0.

## References

- [`docs/ONNX_IO.md`](../docs/ONNX_IO.md) — frozen **`X` / `Y`** contract  
- [`src/midi/MidiPatternLibrary.h`](../src/midi/MidiPatternLibrary.h) — seven pattern slots  
- [`training/prep_midi.py`](prep_midi.py) — **`_events_from_file`** (import-only for Phase 17; **do not edit** `prep_midi.py`)
