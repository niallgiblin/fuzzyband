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
| 4 | `float(state)` | `static_cast<float>(static_cast<int>(StructureState))` — **`SILENT=0`, `VERSE=1`, `CHORUS=2`, `BREAKDOWN=3`** | **MIDI section mapping** (same four numeric casts): **(1)** If **total `note_on` count < 3** OR **RMS proxy < 0.02** → **SILENT (0.0)**. **(2)** Else let **dens** = `note_on` count / **duration_beats** (duration from last event time × bpm/60). If **dens ≥ 10.0** and **highFreqFlux proxy ≥ 0.35** → **CHORUS (2.0)**. **(3)** Else if **dens ≤ 2.5** and **kick count per bar ≤ 1** (kick = note 36) → **BREAKDOWN (3.0)**. **(4)** Else → **VERSE (1.0)** |

**Units / ranges:** Indices **0–4** follow **`docs/ONNX_IO.md`** and **`FeatureVector.h`** semantics: **bpm** float; **rmsEnergy** ~[0,1]; **spectralCentroid** Hz; **highFreqFlux** small positive float; **state** **0–3** cast to float **without** softmax.

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

## Hybrid oracle — overview

- **(A) MIDI-derived heuristics:** **note density** (onsets per beat), **syncopation** (off-beat snare weight), **tempo-relative** onset spacing (std of inter-onset intervals in beats). These yield a **coarse class guess** `h ∈ {0,…,6}`.
- **(B) Seed similarity:** For each library index **i**, build a **synthetic one-bar MIDI loop** that reflects the **relative** density/energy of pattern **i**, run **`prep_midi._events_from_file`**, compute the **same five proxies**, **L2-normalize** the 5-vector (ε=1e-8), and store **seed vector** `s_i`. **Alignment:** seeds use the **same proxy definitions** as (A) and the **same index order** as the table above.

**Combined score:** Let `x̂` be the L2-normalized proxy vector for an example. Let `ŝ_i` be normalized `s_i`. **Default** **α = 0.5** (equal weight).  
`score_i = (1−α) · cosine_sim(x̂, ŝ_i) + α · one_hot(h)[i]`  
**Label** = `argmax_i score_i` (NumPy **`argmax`** breaks ties by **lowest index**).

### Implementation note — `build_dataset.py` (Phase 17)

For **DATA-06** (histogram gate, grouped train/val split), **`build_dataset.py`** assigns **Y** using **train-split quantile binning** on a scalar **activity score** derived from the same events as the proxies: `0.5·dens + 2·rms + 0.5·hf + 0.2·state` (dens = note_on count / duration in beats; rms/hf/state from the five-float row). **Quantile thresholds** use **`np.quantile(scores_train, [1/7 … 6/7])`** so **train** rows are **approximately** evenly spread across **0–6**; **validation** labels use the **same** edges. **Ordinal** meaning: **0** = lowest activity, **6** = highest — a practical stand-in for the seven **MidiPatternLibrary** slots when **seed cosine** would collapse (synthetic seeds were ~identical in normalized proxy space). Phase **18** can treat **Y** as a **7-way** target; domain alignment with live audio remains **out of scope** for this doc.

## Heuristic detail (implementation-facing)

- **BPM (index 0):** As above; always clamp **[80,220]** for training stability.
- **RMS proxy (index 1):** As above — uses **velocity RMS / 127**.
- **Spectral centroid proxy (index 2):** Linear map from mean MIDI pitch to **[400, 6400]** Hz band (documented formula).
- **HF flux proxy (index 3):** Standard deviation of **beat-scaled** inter-onset gaps for **high** drums, capped at **1.0**.
- **`float(state)` (index 4):** Discrete **0–3** rules above; cast to float for the fifth column.

**Heuristic class guess `h` (coarse):** For **Phase 17** tensors, see **Implementation note** above (quantile bins on activity score). The **density / energy band** story still describes how **Phase 18** may **interpret** classes **0–6** relative to pattern names.

## Tie-breaks and failure modes

- **Ties on `argmax`:** **`numpy.argmax`** returns the **first** maximum → **lowest class index** among ties.
- **Ambiguous / low confidence:** If **max score < 0.25** (all signals weak), fall back to **Verse mid (2)** — the dataset’s default “central” class.
- **Empty or parse-failed MIDI:** Treat as **Silent** for labeling purposes (**Y = 0**) and **zero/low** proxies where defined; **RMS** 0, **centroid** 0, **HF flux** 0, **state** 0.

## Seed similarity

- **Metric:** **Cosine similarity** on **L2-normalized** 5-vectors (per-dimension ε **1e-8** before normalize).
- **α default:** **0.5** — `build_dataset.py` uses this default unless overridden later in Phase 18.
- **Seeds:** Seven vectors from **synthetic MIDI** loops (see Hybrid oracle), recomputed deterministically in code.

## References

- [`docs/ONNX_IO.md`](../docs/ONNX_IO.md) — frozen **`X` / `Y`** contract  
- [`src/midi/MidiPatternLibrary.h`](../src/midi/MidiPatternLibrary.h) — seven pattern slots  
- [`training/prep_midi.py`](prep_midi.py) — **`_events_from_file`** (import-only for Phase 17; **do not edit** `prep_midi.py`)
