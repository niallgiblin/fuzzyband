# Phase 19: Bass Model - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-19
**Phase:** 19-bass-model
**Areas discussed:** Target label design, Synthetic input distribution, Normalization in ONNX export, Training approach

---

## Target label design

### root_midi column

| Option | Description | Selected |
|--------|-------------|----------|
| Echo input root | root_midi = pitchRootMidi from X_bass[5] | ✓ |
| Nearest metal-key degree | Map to nearest E/A/B key degree (same for E2/A2/B1 roots) | |
| You decide | Claude picks | |

**User's choice:** Echo input root
**Notes:** The model learns to pass through the detected pitch — musically correct for metal where the bassist tracks the guitarist.

### duration_beats column

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed 1.0 beat | All targets = 1.0 | ✓ |
| Structure-dependent | VERSE=1.0, CHORUS=0.5, BREAKDOWN=2.0 | |
| BPM-scaled | duration = 60.0 / bpm | |

**User's choice:** Fixed 1.0 beat
**Notes:** Phase 20 smoke-test only requires non-degenerate output; complexity deferred.

### proposal_confidence + margin columns

| Option | Description | Selected |
|--------|-------------|----------|
| confidence = pitchConfidence, margin = 0.0 | Mirrors input pitch certainty for gating signal | ✓ |
| confidence = 1.0 always, margin = 0.0 | All "perfect" proposals | |
| You decide | Claude picks | |

**User's choice:** confidence = pitchConfidence, margin = 0.0

---

## Synthetic input distribution

### Feature distributions

| Option | Description | Selected |
|--------|-------------|----------|
| Uniform across metal range | BPM[80,220], energy[0.01,0.9], centroid[1000,6000Hz], all 4 structure states equal | ✓ |
| Metal-weighted | CHORUS+VERSE=70%, metal-biased BPM | |
| You decide | Claude picks | |

**User's choice:** Uniform across metal range

### Pitch distribution + sample count

| Option | Description | Selected |
|--------|-------------|----------|
| Equal thirds E2/A2/B1, ~3,000 samples | 1,000 per root, pitchConfidence~[0.3,1.0], 80/20 split | ✓ |
| Equal thirds E2/A2/B1, ~10,000 samples | Same but 10× data | |
| You decide | Claude picks | |

**User's choice:** Equal thirds E2/A2/B1, ~3,000 samples

---

## Normalization in ONNX export

| Option | Description | Selected |
|--------|-------------|----------|
| Yes — bake norm from synthetic stats | Compute mean/std from synthetic training set, write bass_norm_stats.json, wrap in BassOnnxExport | ✓ |
| Yes — reuse Phase 17 stats for 0–4, fixed for 5–6 | Phase 17 norm_stats.json + hard-coded pitch ranges | |
| No normalization in graph | Simpler export, plugin must normalize | |

**User's choice:** Yes — bake norm from synthetic stats

---

## Training approach

### Loss function + validation

| Option | Description | Selected |
|--------|-------------|----------|
| MSE on all 4 columns, val-loss early stopping | Single MSELoss [N,4], patience 5–10 epochs | ✓ |
| MSE with column weights | Upweight root_midi ×2.0, zero-weight margin | |
| Train to convergence, no val split | Fixed epoch count, all 3,000 samples | |

**User's choice:** MSE on all 4 columns, val-loss early stopping

### Metrics logging

| Option | Description | Selected |
|--------|-------------|----------|
| train_loss, val_loss per epoch | MSE on train and val sets, logged to metrics.jsonl | ✓ |
| train_loss, val_loss + per-column val MSE | Full column-level breakdown | |
| You decide | Claude picks | |

**User's choice:** train_loss, val_loss per epoch

---

## Claude's Discretion

- Optimizer choice, LR, batch size, max epochs, patience numeric value
- Random seed handling
- Whether bass_norm_stats.json includes a feature_order documentation key

## Deferred Ideas

- Per-column weighted loss — post v0.3.0 experiment
- Structure-dependent duration targets — v0.4.0 musicality pass
- Real bass corpus (Lakh MIDI) — v0.4.0 per roadmap
