# Phase 19: Bass Model - Context

**Gathered:** 2026-04-19
**Status:** Ready for planning

<domain>
## Phase Boundary

Train **BassNet** (7→32→16→4 MLP) on **synthetic E2/A2/B1 metal-key pitch distributions** (no external corpus), export **`bass_trained.onnx`** at opset 17 with **normalization baked into `BassOnnxExport`**, and pass **`scripts/validate_onnx_contract.py --bass`**.

**Not in scope:** Pattern model (Phase 18), plugin install / DAW smoke (Phase 20), changing `docs/BASS_ONNX_IO.md` tensor contract, modifying `src/` C++, Lakh MIDI or real bass corpus.

</domain>

<decisions>
## Implementation Decisions

### Target label design
- **D-19-01:** `root_midi` (Y_bass col 1) = **echo of input `pitchRootMidi`** (X_bass[5]). The model learns to pass through the detected pitch. For the three training roots (E2=40, A2=45, B1=35) this is the musically correct behaviour — bassist tracks guitarist.
- **D-19-02:** `duration_beats` (Y_bass col 2) = **fixed 1.0** for all training targets. Phase 20 smoke-test only requires non-degenerate output; musical nuance is deferred to v0.4.0.
- **D-19-03:** `proposal_confidence` (Y_bass col 0) = **mirrors input `pitchConfidence`** (X_bass[6]). The model learns to gate on actual pitch certainty.
- **D-19-04:** `margin` (Y_bass col 3) = **always 0.0** (reserved column — not used during training).

### Synthetic input distribution
- **D-19-05:** Feature distributions are **uniform across the metal operational envelope**: BPM ~ Uniform[80, 220]; rmsEnergy ~ Uniform[0.01, 0.9]; spectralCentroid ~ Uniform[1000, 6000] Hz; highFreqFlux ~ Uniform[0, 0.5]; StructureState uniform over {SILENT=0, VERSE=1, CHORUS=2, BREAKDOWN=3}.
- **D-19-06:** `pitchRootMidi` cycles across **{E2=40, A2=45, B1=35} in equal thirds** (~1,000 examples each). `pitchConfidence` ~ Uniform[0.3, 1.0] (includes non-trivial low-confidence rows so gating signal is present).
- **D-19-07:** **Total: ~3,000 synthetic rows** with an **80/20 train/val split** (no performance-level grouping needed — purely synthetic data).

### Normalization & ONNX export
- **D-19-08:** **`BassOnnxExport`** wraps trained `BassNet` and prepends affine normalization: **mean/std computed from the synthetic training set**, written to **`bass_norm_stats.json`** alongside the artifact. Plugin passes raw `FeatureVector` rows (consistent with pattern model behaviour). Mirror the `PatternOnnxExport` pattern exactly.
- **D-19-09:** At ONNX export use **eval mode** (LayerNorm running stats, Dropout off). Export at **opset 17**. Tensor names must be **`X_bass`** (float32 [1,7]) and **`Y_bass`** (float32 [1,4]) per `docs/BASS_ONNX_IO.md`.

### Training run
- **D-19-10:** **Loss:** `MSELoss` over the full Y_bass [N,4] output (single symmetric loss across all columns). No per-column weighting for v0.3.0.
- **D-19-11:** **Early stopping:** monitors **validation MSE** (lower is better) with patience in range **5–10 epochs**. Also stop on NaN/inf loss.
- **D-19-12:** **Logging:** `metrics.jsonl` records `epoch`, `train_loss` (MSE), `val_loss` (MSE) per epoch. Mirrors Phase 18 logging convention.
- **D-19-13:** Default **`--out-dir`** to a **timestamped directory** under `training/artifacts/` (e.g. `training/artifacts/bass-synth-<UTC-timestamp>/`). Final artifact written as **`bass_trained.onnx`** inside `--out-dir`.

### LayerNorm (carried from Phase 18)
- **D-19-14:** Use **LayerNorm + Dropout** in BassNet trunk (not BatchNorm) — same fix as Phase 18 (WR-001). BatchNorm raises in train mode when batch size = 1.

### Claude's Discretion
- Exact optimizer (Adam is standard), base LR, batch size, max epochs, patience numeric value
- Random seed handling (fixed seed recommended for reproducibility on synthetic data)
- Whether `bass_norm_stats.json` includes a `feature_order` key for documentation

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — **BMODEL-01, BMODEL-02**
- `.planning/ROADMAP.md` — Phase 19 goal and success criteria

### Frozen I/O contract for bass model
- `docs/BASS_ONNX_IO.md` — **`X_bass` [1,7] float32**, **`Y_bass` [1,4] float32**; all 7 input column semantics; all 4 output column semantics; tensor names are frozen

### Phase 18 reference implementation (mirror patterns)
- `training/models/pattern_model.py` — `PatternNet` (LayerNorm+Dropout trunk) + `PatternOnnxExport` (baked norm wrapper, argmax → int64); BassNet/BassOnnxExport should mirror this structure
- `training/train_gmd.py` — training loop, early stopping, metrics.jsonl logging, timestamped out-dir, argparse conventions; bass training script mirrors these conventions
- `.planning/phases/18-pattern-model/18-CONTEXT.md` — Phase 18 decisions (all D-18-xx carry forward unless explicitly overridden above)

### Validation
- `scripts/validate_onnx_contract.py` — `check_bass()` function: verifies `X_bass` float32 dim-7, `Y_bass` float32 [1,4], tensor names

### Project state
- `.planning/STATE.md` — opset/ORT pins, C++ freeze

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable assets
- **`training/models/pattern_model.py`**: Direct template for `BassNet` (swap 5→7 input, 7→4 output, regression not classifier) and `BassOnnxExport` (baked norm wrapper — same subtract/divide pattern, but forward returns float [N,4] not argmax int64)
- **`training/train_gmd.py`**: Template for `train_bass.py` — argparse, metrics.jsonl, early stopping loop, timestamped out-dir, onnx export call
- **`scripts/validate_onnx_contract.py`**: `check_bass()` already implemented — target contract is clear

### Established patterns
- **Export at opset 17** (`torch.onnx.export(..., opset_version=17)`) — frozen project-wide
- **Eval mode before export** (`model.eval()`) — prevents train-mode BatchNorm/Dropout in graph
- **Timestamped out-dir** under `training/artifacts/` — prevents run overwrites

### Integration points
- **Input:** Synthetic data generated in-script (no external data dependency)
- **Output:** `bass_trained.onnx` + `bass_norm_stats.json` for `validate_onnx_contract.py` and later `assets/` install (Phase 20)
- **Validation:** `scripts/validate_onnx_contract.py --bass <path>` is the Phase 20-compatible gate

</code_context>

<specifics>
## Specific Ideas

- User selected recommended defaults across all four gray areas — minimal divergence from Phase 18 conventions.
- `proposal_confidence` mirrors `pitchConfidence` input so the model learns meaningful gating — not always 1.0.
- BassNet is a **regression** model (MSELoss, float32 [1,4] output), not a classifier — the only structural difference from PatternNet.

</specifics>

<deferred>
## Deferred Ideas

- Per-column weighted loss (upweight `root_midi`, zero-weight `margin`) — post v0.3.0 experiment
- Structure-dependent `duration_beats` targets (VERSE=1.0, CHORUS=0.5, BREAKDOWN=2.0) — v0.4.0 musicality pass
- Real bass corpus (Lakh MIDI) for improved generalization — explicitly deferred to v0.4.0 per REQUIREMENTS.md

### Reviewed Todos (not folded)

- None

</deferred>

---

*Phase: 19-bass-model*
*Context gathered: 2026-04-19*
