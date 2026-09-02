# Tier 1 — Conditional Groove Renderer: ONNX contract + data pipeline

**Goal.** Replace the fixed per-16th *mean* groove templates (the current
`Groove::Template` + bounded-gaussian jitter) with a small **conditional
generative model** that produces a *new, coherent velocity + microtiming take
every bar*, conditioned on the authored score and the live playing context.
This is Magenta **GrooVAE**'s "Groove model" — *score in, groove out* — the
documented, open-source way to make drums "alter patterns tastefully" while the
score stays authored (so it can never play something unmusical).

Reference: [GrooVAE (Magenta, ICML 2019)](https://magenta.tensorflow.org/groovae),
trained on the [Groove MIDI Dataset](https://magenta.tensorflow.org/datasets/groove)
— which this repo **already consumes** for `GrooveTemplateData.h`.

---

## 1. Design boundary (why this shape)

- **Score stays authored.** The model only emits *how hard* and *when*, never
  *which* hits. Score-level variety is already owned by Tier-0 ornamentation and
  the 28-pattern library. This keeps the output musically grounded and the
  model small.
- **No latent — a heteroscedastic (mean, std) model.** The model predicts a
  per-cell *mean* and *standard deviation* for velocity and microtiming, and the
  C++ side samples `N(mean, std)` deterministically per bar (hash seed). This
  is collapse-proof (the β-VAE's latent ignored the noisy target) and gives
  data-driven "different take every bar" variation.
- **Runs on the inference thread, committed to the audio thread.** The audio
  thread already receives pre-rendered decisions via a lock-free queue
  (`grooveCommitQueue`); a rendered groove grid is the same kind of handoff.

---

## 2. ONNX tensor contract

One model, `assets/groove_renderer.onnx`, batch dimension 1.

### Inputs

| Name | Shape | dtype | Meaning |
|---|---|---|---|
| `score` | `[1, 10, 16]` | float32 | Quantized drum score, 1 bar × 16 sixteenths, 10 voices. `1.0` = hit, `0.0` = rest. |
| `condition` | `[1, 18]` | float32 | Playing context (see §2.2). |

### Outputs

| Name | Shape | dtype | Meaning |
|---|---|---|---|
| `velocity_mean` | `[1, 10, 16]` | float32 | Per-hit velocity mean, `[0,1]` (`1.0 = MIDI 127`). |
| `velocity_std`  | `[1, 10, 16]` | float32 | Per-hit velocity std, `[0.01, 0.41]`. |
| `offset_mean`   | `[1, 10, 16]` | float32 | Microtiming mean, fraction of a 16th (tanh, `[-1,1]`). |
| `offset_std`    | `[1, 10, 16]` | float32 | Microtiming std, `[0.01, 0.51]`. |

The C++ runtime samples `value = mean + ε·std` per cell with `ε ~ N(0,1)`
deterministic per `(bar, voice, step)`.

### 2.1 The 10-voice grid (maps to `MidiPatternLibrary.cpp` GM notes)

| idx | voice | GM note(s) |
|---|---|---|
| 0 | kick | 35, 36 |
| 1 | snare | 38, 40 |
| 2 | hat_closed | 42 |
| 3 | hat_open | 46 |
| 4 | ride | 51 |
| 5 | ride_bell | 53 |
| 6 | crash | 49, 52, 55 (crash + china + splash collapse to one voice) |
| 7 | tom_hi | 48 |
| 8 | tom_mid | 45 |
| 9 | tom_lo | 41 |

Step axis = **1 bar × 16 sixteenths = 16**. Step `s` ↔ beat `s/4` within the bar.
The groove model only predicts *how* each hit is played (velocity + pocket), not
*which* hits exist — the score already carries any 2-bar structure (2-bar
patterns simply have different hits in bar 2), so a 1-bar granularity matches the
existing per-16th `Groove::Template` hierarchy exactly and drops into
`PatternPlayer` where `grid16` is already computed. 4-bar patterns (11 Intro
Build, 16 Outro Decay) render the model per bar with a fresh `z`.

### 2.2 The 18-dim condition vector

| slice | dims | feature | normalization |
|---|---|---|---|
| 0 | 1 | bpm | `(bpm - 40) / 260` → `[0,1]` |
| 1 | 1 | rms energy | as-is (`[0,1]`) |
| 2 | 1 | spectral centroid | `/ 4000` clipped `[0,1]` |
| 3 | 1 | onset density | `/ 8` clipped `[0,1]` |
| 4–8 | 5 | style class | one-hot (palm_mute / open_chord / single_note / sustain / silence) |
| 9–11 | 3 | structure state | one-hot (SILENT / SOFT / LOUD) |
| 12–16 | 5 | genre | one-hot (Rock / Hard Rock / Punk / Metal / Sludge) |
| 17 | 1 | bar phase | `barNumber % 4` → `/ 4` |

> **GMD is MIDI-only** (no audio), so energy/centroid/density/style/state are
> not in the raw dataset. §5 explains how to synthesize them. A v1 can ship
> with only bpm + genre + phase populated and the rest held at neutral (0), and
> still capture the dominant velocity/timing-vs-tempo behaviour.

---

## 3. Decoder architecture (small, RT-friendly)

No encoder is exported — only the decoder ships in ONNX:

```
score [10,16]  → MLP (or conv1d over 16 steps) → step features [16·H]
condition [18] → MLP → context [H]
z [32]         → MLP → latent context [H]
concat(step features, context, latent context) → MLP (1–2 layers)
    ├─ velocity head → sigmoid → [10,16]
    └─ offset head   → tanh·k   → [10,16]   (k ≈ 25 ms)
```

- ~100–500k parameters, **< 1 ms** on M-series with 1 ORT thread. No
  performance risk (and it runs off the audio thread anyway).
- VAE latent `z` is trained with the standard ELBO (reconstruction + β·KL). At
  runtime the caller samples `z ~ N(0, I)` deterministically from a per-bar
  seed, so "same bar → same take", "different bar → different take".

---

## 4. Training data pipeline (reuses existing infra)

New script: `training/build_groove_render_dataset.py`.

**Step 1 — score/groove extraction (no audio).** Reuse the GMD readers already
in `build_groove_template.py` (`_iter_gmd_rows`, `_hits_from_midi`). For each
4/4 `beat_type == "beat"` take, produce aligned arrays:

```python
# per take (each take split into 1-bar windows):
score[V, 16]      = 1.0 at (voice, step) for each channel-10 hit
velocity[V, 16]   = msg.velocity / 127.0
offset_ms[V, 16]  = (beat - step/4) * ms_per_beat   # deviation from the grid
condition[18]     = bpm + genre(one-hot) + phase; others neutral
```

Grouped train/val/test split by drummer/session — the same no-leakage rule the
repo already enforces via `training/scripts/dataset_split.py` (referenced in
`train_groove_model.py`).

**Step 2 — (optional, for full conditioning) synth render → audio features.**
The repo already synthesizes MIDI→audio for mel-CNN training
(`data/raw_synth_backup`, `build_dadagp_articulation.py`, and the
`X_groove.npy` builder). Render each GMD take through the same synth, run the
existing `MelSpectrogramExtractor`, then:
- `style` ← `style_cnn.onnx` `style_logits` argmax,
- `structure` ← the `StructureTagger` thresholds (or a frozen rule),
- `energy/centroid/density` ← `EnergyAnalyser`/`PhraseLearner` equivalents.

This is the bulk of the effort. **Ship v1 conditioned on bpm + genre + phase
first** (Step 1 only), then add the synth conditioning as a v2 — it is additive
and does not change the tensor contract.

**Step 3 — training.** `training/train_groove_renderer.py` (new), mirroring
`train_groove_model.py`'s structure: VAE encoder `q(z | score, velocity,
offset, condition)`, decoder above, β-VAE. Loss = Huber on `velocity` + Huber on
`offset_ms`, masked to `score == 1`, + β·KL. Adam, cosine LR, ~100 epochs.

---

## 5. Quality gates (mirror the repo's existing gate style)

- **velocity MAE < 0.08** (`[0,1]`) and **offset MAE < 5 ms** on held-out takes.
- **Per-voice distribution match:** KS distance between generated and GMD
  velocity histograms < threshold per voice — catches "collapsed to flat 100".
- **Ghost preservation:** recall of low-velocity (≤ 0.35) snare hits ≥ 0.7 — the
  model must not erase ghost notes.
- **Latent sanity:** moving `z` along a principal axis changes velocity/timing
  smoothly (no degenerate latent). Same "no dead class" philosophy as the
  existing `train_groove_model.py` gate.

---

## 6. ONNX export (same conventions as `export_centroids.py`)

```python
torch.onnx.export(
    decoder, (score, condition, z),
    "assets/groove_renderer.onnx",
    input_names=["score", "condition", "z"],
    output_names=["velocity", "offset_ms"],
    dynamic_axes={  # batch dim only; the 10x16 grid and 18/32 vectors are fixed
        "score": {0: "batch"}, "condition": {0: "batch"}, "z": {0: "batch"},
        "velocity": {0: "batch"}, "offset_ms": {0: "batch"},
    },
)
import onnx
onnx.checker.check_model(onnx.load("assets/groove_renderer.onnx"))
```

Bundle via JUCE BinaryData exactly like `metal_groove.onnx` / `style_cnn.onnx`.

---

## 7. Runtime integration (C++)

New wrapper mirroring `MetalGrooveInference::Impl`:

```cpp
struct GrooveGrid { std::array<std::array<float, 16>, 10> velocity, offsetMs; };

// Build the score grid ONCE per pattern (pure function of drumEvents; cache it).
ScoreGrid scoreGridFromPattern(const MidiPattern& p);

// On the inference thread (~50 Hz), alongside the existing mel selection:
GrooveGrid renderGroove(const ScoreGrid& score, const FeatureVector& f,
                        int barNumber, int genreId, int excludeSeed);
```

- `score` ← cached per-pattern grid.
- `condition` ← `FeatureVector` + genre + `barNumber % 4`.
- `z[i]` ← Box–Muller applied to `hashMix(barNumber, i)` (the same deterministic
  hash as the Tier-0 spec), so each bar draws a fresh but reproducible latent.
- Run the decoder, wrap the result in a `GrooveGrid`, push it through a new
  lock-free `grooveGridQueue` (32 capacity) to the audio thread.

**Audio thread** (`emitDrumEventsForRange`, lines 264–339): when a fresh
`GrooveGrid` for the active pattern/bar is available, use its per-step
`velocity` and `offsetMs` instead of `grooveTemplate.velocityMul[grid16]`,
`timingMs[grid16]` and the bounded-gaussian jitter. When the grid is stale or
the model failed to load, fall back to the existing template path — matching
the repo's established ONNX→rule fallback philosophy.

---

## 8. Effort estimate

| Piece | Effort |
|---|---|
| Step 1 dataset builder (score/velocity/offset from GMD) | Low — reuse existing readers |
| Step 3 training + Step 5 gates | Medium — new but patterned on `train_groove_model.py` |
| Step 6 ONNX export + BinaryData | Low — copy `export_centroids.py` |
| Step 7 C++ wrapper + commit queue | Medium — copy `MetalGrooveInference::Impl` pattern |
| Step 2 synth conditioning (style/structure/energy) | **High** — the long pole; defer to v2 |

**Decision: build v1 groove-only, conditioned on bpm + genre + phase, with the
template as fallback.** It captures the dominant "drummer varies velocity and
pocket" behaviour with modest effort, and the contract is forward-compatible
with the full conditioning later.
