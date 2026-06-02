# Feature proxies - MIDI → ONNX pattern row (Phase 17)

## Purpose

This document defines **offline MIDI-derived proxies** for the five floats in **`docs/ONNX_IO.md`** pattern input **`X[0,5]`**, plus the **hybrid label oracle** for **`Y ∈ {0,...,6}`**. It targets **Phase 17-18** dataset construction (`build_dataset.py`). It is **not** a specification of live guitar analysis: **`OnnxInference`** reads **audio-thread** features from **`FeatureVector`**.

## Frozen input layout (matches `docs/ONNX_IO.md`)

| Index | Field name | Runtime source (plugin) | Phase 17 proxy (MIDI) |
|-------|------------|-------------------------|------------------------|
| 0 | `adjustedBpm` | `PatternRules::adjustedBpm(f)` - `f.bpm + (f.policyIntensity - 0.5f) * 40.0f`, clamped **[80, 220]** | **Adjusted BPM:** `raw_bpm + (policy_intensity - 0.5) * 40`, clamped **[80, 220]**; runtime matches via `PatternRules::adjustedBpm`. See `docs/ONNX_IO.md` column 0 |
| 1 | `rmsEnergy` | Rolling **100 ms** RMS, **~[0, 1]** | **RMS proxy:** over the full performance window, take all **`note_on` velocities** \(v_i\); **RMS\_vel** \(=\sqrt{\mathrm{mean}_i(v_i^2)}\); **proxy** \(=\mathrm{RMS\_vel}/127.0\) (dimensionless, comparable to ~[0,1]) |
| 2 | `spectralCentroid` | **Hz** (audio spectral centroid) | **Brightness proxy (Hz) — Phase 36 formula:** Idle/sparse → **9853 Hz** (capture SILENT p50). Active → \(8750 + (\overline{n}/127)\cdot 2450\) where \(\overline{n}\) is mean MIDI note over `note_on` events. (Updated from Phase 17 \(400+6000\cdot\overline{n}/127\) in Phase 36 Plan 02 — see remediation section below.) |
| 3 | `highFreqFlux` | **2 kHz+ band flux** (audio) | **Onset-irregularity proxy (unitless, ~small positive):** collect `note_on` times for "high" drums (**note ≥ 42**). If fewer than **2** such events, **0.0**. Else let \(\Delta t_k\) be inter-onset times in **beats** (\(\Delta t_k = (time_{k+1}-time_k)\cdot bpm/60\)); **proxy** \(=\min(1.0,\ 4\cdot \mathrm{std}(\Delta t_k))\) (emphasizes hat/cymbal chatter vs steady kicks) |
| 4 | `float(state)` | `static_cast<float>(static_cast<int>(StructureState))` - **`SILENT=0`, `SOFT=1`, `LOUD=2`** | **MIDI section mapping** (three-class, Phase 25): **(1)** If **total `note_on` count < 3** OR **RMS proxy < 0.02** → **SILENT (0.0)**. **(2)** Else if **dens ≥ 10.0** and **highFreqFlux proxy ≥ 0.35** → **LOUD (2.0)**. **(3)** Else → **SOFT (1.0)** |

**Units / ranges:** Indices **0-4** follow **`docs/ONNX_IO.md`** and **`FeatureVector.h`** semantics: **bpm** float; **rmsEnergy** ~[0,1]; **spectralCentroid** Hz; **highFreqFlux** small positive float; **state** **0-2** cast to float **without** softmax.

## Domain gap (MIDI vs audio)

Phase 17 proxies are **symbolic / MIDI-statistical** approximations derived from **GMD** performances. **`OnnxInference`** consumes **audio-thread** features (STFT-derived RMS, centroid, HF flux, structure tagger). **Do not** treat offline rows as **pointwise identical** to live guitar features. Phase 18 **domain adaptation** (if any) is **out of scope** for this document; this file **must not** claim **identity** between MIDI proxies and runtime audio features.

## Phase 34 captured-vs-proxy gap

Analyzer command:

```bash
training/.venv/bin/python training/analyze_feature_proxy_gap.py \
  --capture /Users/ng/Documents/MetalAccompaniment/captures/feature_capture_20260525T111203Z.jsonl \
  --markdown-out training/artifacts/feature_proxy_gap.md \
  --json-out training/artifacts/feature_proxy_gap.json
```

Capture source: local real-guitar session `feature_capture_20260525T111203Z.jsonl`. The raw capture remains outside the repository. Proxy tensors used: `training/data/processed/train.pt` and `training/data/lakh/lakh_tensors.pt`. Row counts: 13,151 captured rows and 161,474 proxy rows.

| Feature | Captured n | Captured mean | Captured p50 | Captured p95 | Proxy n | Proxy mean | Proxy p50 | Proxy p95 | standardized mean delta | Verdict |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---|
| `bpm` | 13,151 | 120.6862 | 120.0000 | 124.5294 | 161,474 | 59.1658 | 3.2776 | 146.0000 | 0.9929 | risky |
| `rmsEnergy` | 13,151 | 0.0166 | 0.0000 | 0.1038 | 161,474 | 0.3583 | 0.6416 | 1.2925 | 0.4277 | acceptable |
| `spectralCentroid` | 13,151 | 8004.7984 | 9791.6133 | 10154.4077 | 161,474 | 1483.7511 | 2.6771 | 3229.4922 | 4.3525 | needs proxy change |
| `highFreqFlux` | 13,151 | 0.0326 | 0.0102 | 0.0394 | 161,474 | 0.3782 | 0.6686 | 1.1674 | 0.4230 | acceptable |
| `state_float` | 13,151 | 0.2020 | 0.0000 | 1.0000 | 161,474 | 0.8137 | 0.7618 | 2.0000 | 0.5395 | risky |
| `pitchRootMidi` | 13,151 | 40.8927 | 40.0000 | 47.4254 | - | - | - | - | - | not modeled by pattern proxy |
| `pitchConfidence` | 13,151 | 0.0253 | 0.0000 | 0.0226 | - | - | - | - | - | not modeled by pattern proxy |
| `policyIntensity` | 13,151 | 0.4850 | 0.4850 | 0.4850 | - | - | - | - | - | not modeled by pattern proxy |
| `rmsDelta` | 13,151 | 0.0007 | -0.0005 | 0.0249 | - | - | - | - | - | not modeled by pattern proxy |

Residual gap summary: `rmsEnergy` and `highFreqFlux` are acceptable by the Phase 34 standardized-delta threshold, but the central tendency differs enough that downstream evaluation should keep using real captures as the authority. `bpm` and `state_float` are risky: the proxy corpus contains many low-BPM or silent/soft rows that do not match this captured session's distribution. The runtime-only fields `pitchRootMidi`, `pitchConfidence`, `policyIntensity`, and `rmsDelta` have no pattern-proxy columns, so the current pattern proxy cannot validate their live distribution.

Spectral-centroid decision: **fixed in Phase 36** (D-05). The proxy now maps idle/sparse MIDI rows to **9853 Hz** (capture SILENT p50) and active rows to **8750 + (mean_note/127)×2450 Hz**. `analyze_feature_proxy_gap.py` denormalizes merged `.pt` tensors before comparison and uses **p50 delta** for the centroid verdict. Rebuild `train.pt` / `lakh_tensors.pt` and promote `assets/accompaniment_model.onnx` after merge + `train_gmd.py`.

## Phase 36 centroid remediation (Plan 02 — completed)

| Metric | Before (Phase 17–34) | After (Phase 36) |
|--------|-----------------------|------------------|
| Captured p50 | 9791.6 Hz | 9791.6 Hz (reference) |
| Proxy p50 | ~2.7 Hz (old formula) | **9525.9 Hz** (merged train centroid mean) |
| Standardized mean delta | 4.35 (needs proxy change) | **acceptable** (rebuilt GMD + Lakh with new formula) |
| Verdict | needs proxy change | **acceptable** ✓ |

**Formula (both `build_dataset.py` and `build_lakh_dataset.py` `_compute_proxy_row`):**
- Idle / no onsets / velocity < 20: **9853 Hz** (capture SILENT p50)
- Active: **8750 + (mean_note / 127) × 2450 Hz**

**Deliverables (2026-06-02):** GMD tensors rebuilt, merged with Lakh (centroid confirmed), pattern model retrained (macro-F1 0.6391 ≥ 0.55 gate), promoted to `assets/accompaniment_model.onnx`, CMakeLists.txt → 0.5.6.

## Pattern class index - `Y` ∈ [0, 6]

Constructor order in `MidiPatternLibrary.cpp` (`patterns.push_back`):

0. **Silent** - `Silent`
1. **Verse slow** - `Verse slow`
2. **Verse mid** - `Verse mid`
3. **Verse fast** - `Verse fast`
4. **Chorus mid** - `Chorus mid`
5. **Chorus fast** - `Chorus fast`
6. **Breakdown** - `Breakdown`

## Label oracle - Y ∈ [0, 6]

Labels are assigned by a Python port of `PatternRules::rulePatternForState`
(`src/inference/pattern_rules.h`) applied to each proxy row. This makes the
pattern model a learned distillation of the rule path, not an activity ranker.

### Intensity variants (`_INTENSITY_VARIANTS`)

Each source MIDI row expands to **3 training rows** via `_INTENSITY_VARIANTS = (0.0, 0.5, 1.0)` in `build_dataset.py` and `build_lakh_dataset.py`:

- **X[0]** is **adjusted BPM** = `raw_bpm + (policy_intensity - 0.5) * 40`, clamped **[80, 220]** (mirrors `PatternRules::adjustedBpm`)
- **Y** is recomputed per variant via `_oracle_label(..., policy_intensity=pi)` so labels stay aligned with adjusted BPM near the **120** / **160** thresholds
- Intensity **0.0** → **-20** BPM, **0.5** → **0**, **1.0** → **+20**

Breakdown class **6** override still uses **raw** BPM `< 110` (not adjusted). Cross-ref runtime packing in [`docs/ONNX_IO.md`](../docs/ONNX_IO.md).

### Rule oracle (classes 0-5)

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

- **BPM (index 0):** **Adjusted BPM** as above; always clamp **[80,220]** for training stability.
- **RMS proxy (index 1):** As above - uses **velocity RMS / 127**.
- **Spectral centroid proxy (index 2):** Phase 36 formula: idle→9853 Hz, active→8750 + (mean_note/127)×2450 Hz (see Phase 36 remediation section).
- **HF flux proxy (index 3):** Standard deviation of **beat-scaled** inter-onset gaps for **high** drums, capped at **1.0**.
- **`float(state)` (index 4):** Discrete **0-2** rules above; cast to float for the fifth column.

**Heuristic class guess `h` (coarse):** Labels are assigned by rule-oracle + Breakdown heuristic. See **Label oracle** section above.

## Failure modes

- **Empty or parse-failed MIDI:** Treat as **Silent** for labeling purposes (**Y = 0**) and **zero/low** proxies where defined; **RMS** 0, **centroid** 0, **HF flux** 0, **state** 0.

## References

- [`docs/ONNX_IO.md`](../docs/ONNX_IO.md) - frozen **`X` / `Y`** contract
- [`src/midi/MidiPatternLibrary.h`](../src/midi/MidiPatternLibrary.h) - seven pattern slots
- [`training/prep_midi.py`](prep_midi.py) - **`_events_from_file`** (import-only for Phase 17; **do not edit** `prep_midi.py`)
