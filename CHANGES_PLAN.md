# Changes Plan — Metal Accompaniment ML Pipeline

**Context:** Based on the findings in `ML_REPORT.md`. The plugin currently ships ONNX models but runs rule-based inference by default (`MA_ENABLE_ONNX=OFF`). The models exist but have not been validated against real guitar audio, contain a significant domain gap, and have several correctness gaps in the training and inference plumbing. This plan addresses those gaps in five areas, ordered by risk and dependency.

Each area below is intended as a standalone planning unit — a separate agent can plan detailed phases for each one.

---

## Area 1 — Fix Training Labels and Pattern Semantics

**Problem:** Pattern model training labels Y∈[0,6] are ordinal quantile bins of a scalar "activity score," not semantic indices into `MidiPatternLibrary`. Class 4 in training does not mean "Chorus mid." The model learns low→high activity ordering, which directionally agrees with the rule path but has no guaranteed correspondence to the seven named patterns at inference time.

**Goal:** Labels used during training should map as closely as possible to the same pattern a reasonable rule-based system would choose for that input — so the ONNX model is a learned approximation of the same decision, not a learned approximation of activity ranking.

**What to change:**

1. **Re-derive labels using the rule oracle.** For each training example, run the same logic as `PatternRules::rulePatternForState` on the proxy feature vector (adjusting BPM, reading state float) and use the result as Y. This makes ONNX a learned distillation of the rule path, ensuring they agree by construction on in-distribution data and giving the model a fighting chance of generalizing correctly.

2. **Update `build_dataset.py` and `build_lakh_dataset.py`** to use rule-oracle labeling instead of quantile binning. Keep the histogram gate (DATA-06) so training still fails fast on degenerate splits.

3. **Re-export and replace `assets/accompaniment_model.onnx`** with a model trained on the corrected labels.

4. **Update `training/FEATURE_PROXY.md`** to document the rule-oracle labeling approach and retire the quantile-bin description.

**Dependencies:** None. Can be done independently of all other areas.

---

## Area 2 — Close the Domain Gap (MIDI Proxies → Guitar Audio)

**Problem:** All three models are trained on symbolic MIDI statistics (drum kit velocities, note density, MIDI pitch). At runtime they receive STFT-derived guitar audio features (rolling RMS, spectral centroid, HF flux). The proxy mapping is documented but explicitly not pointwise equivalent. No evaluation of ONNX model performance on real guitar input exists anywhere.

**Goal:** Either (a) produce training data that is statistically closer to live guitar features, or (b) create a rigorous evaluation that bounds the actual performance gap, or both.

**What to change:**

1. **Build a guitar audio feature capture pipeline.** Add a utility mode (standalone or offline tool) that records `FeatureVector` values from a real guitar performance to a JSONL log on disk. This gives ground-truth audio-domain features for analysis and eventual fine-tuning.

2. **Create an offline evaluation harness.** Given a set of captured feature logs with human-annotated pattern labels, measure pattern model accuracy against rule-path accuracy on the same inputs. This establishes a baseline before any retraining.

3. **Improve proxies where they diverge most.** From the evaluation data, identify which proxy features have the largest distribution shift (centroid proxy from mean MIDI note vs Hz from STFT is the most obvious candidate). Update `build_dataset.py` proxies to be statistically closer to the audio distribution.

4. **Consider data augmentation for the bass model.** The bass model is conditioned on `pitchRootMidi` and `pitchConfidence` — values that have no MIDI equivalent in training. Augment training rows with realistic pitch confidence distributions so the model learns to degrade gracefully at low confidence.

5. **Document the residual gap.** Whatever gap remains after the above should be clearly documented in `training/FEATURE_PROXY.md` with quantitative estimates so future maintainers know what they are accepting.

**Dependencies:** The capture pipeline (step 1) is foundational — steps 2–4 depend on having recorded data.

---

## Area 3 — Model Quality Gates and Training Robustness

**Problem:** The pattern model has a hard quality gate (`macro_f1 ≥ 0.55`) but the bass and structure models have no equivalent. A poorly converged bass model or a structure model that degrades on an update would be silently exported and shipped. Additionally, the structure model's ONNX graph has no baked input normalization, creating a hidden runtime/training alignment requirement.

**Goal:** Every model export must meet a measurable quality bar, and every model's normalization must be self-contained in its ONNX graph.

**What to change:**

1. **Add a quality gate to `train_bass.py`.** Define a maximum acceptable val MSE (e.g., as a multiple of a rule-baseline MSE computed from the training split). If `best_val_mse > gate`, exit non-zero and block the ONNX export.

2. **Add a quality gate to `train_structure.py`.** Minimum val macro-F1 matching or exceeding the rule-path agreement rate on the same split (i.e., the structure model must beat or tie the rule baseline, not just converge to any non-trivial value).

3. **Bake normalization into the structure model ONNX graph.** Follow the same pattern as `PatternOnnxExport`: create a `StructureOnnxExport` wrapper that registers `mean`/`std` buffers and normalizes input inside `forward()` before passing to `StructureNet`. Update `train_structure.py` to use it and update `OnnxStructureInference.cpp` to remove any normalization it currently performs externally.

4. **Add per-class metrics to `train_bass.py` output.** The pattern model already logs per-class F1 and a confusion matrix. Add analogous per-step statistics for bass (e.g., per-step velocity prediction error, pitch offset distribution) to `validation.json` for post-training inspection.

5. **Add a contract validation test for the structure model.** `training/tests/test_onnx_contract.py` should verify structure model I/O shape/dtype alongside the pattern and bass models.

**Dependencies:** Area 1 (corrected labels) should ideally land first so quality gates are evaluated against meaningful labels, but gates can be added independently.

---

## Area 4 — Inference Path Consistency (Rule ↔ ONNX)

**Problem:** Several subtle inconsistencies exist between what the rule path does and what the ONNX path sees or does. These cause the two paths to behave differently for the same input in ways that are not intentional.

**Specific issues:**

- `policyIntensity` shifts effective BPM by ±20 in the rule path but is **not forwarded to any ONNX model tensor**. The ONNX pattern model sees raw BPM; the rule path uses adjusted BPM. At intensity=0 or intensity=1 this is a 20 BPM discrepancy.
- Pattern 6 (Breakdown) is reachable via the D-23-04 single-shot exclusion modulo wrap (`(last + 1) % 7`) even though neither inference path ever directly targets it. A user could get a breakdown pattern fired unexpectedly.
- The `structureBlend` parameter ([0,1]) blends between rule state and ONNX shadow state, but the ONNX shadow is non-authoritative for silence detection. The blend logic should be documented and tested explicitly.

**Goal:** Rule path and ONNX path should produce the same output for the same input whenever the ONNX model is well-calibrated, with the only intentional divergence being the model's learned generalization.

**What to change:**

1. **Forward `adjustedBpm` (not raw `bpm`) into ONNX tensor column 0 for the pattern model.** Update `OnnxInference.cpp` to call `PatternRules::adjustedBpm(f)` when packing the input tensor. Update `training/FEATURE_PROXY.md` and `docs/ONNX_IO.md` to document that column 0 is adjusted BPM. Re-train the pattern model with adjusted BPM in the proxy column (using `_clamp_bpm(PatternRules::adjustedBpm(proxy_row))`).

2. **Fix the exclusion modulo to respect state compatibility.** Instead of `(excludeIndex + 1) % 7`, walk forward modulo 7 until a state-compatible pattern is found (using `isPatternCompatibleWithState`). This prevents Breakdown from being selected as a fallback for LOUD or SOFT states.

3. **Add explicit tests for both fixes.** `test_rule_based_inference.cpp` should verify that exclusion never produces an incompatible pattern, and `test_pattern_rules.cpp` should verify the intensity-adjusted BPM is used consistently.

4. **Document `structureBlend` behavior in a short design note** (inline comment or `docs/` file) clarifying that the rule state is authoritative for silence/gate, and ONNX shadow only shapes non-silent sections at `blend > 0.5`.

**Dependencies:** The adjusted-BPM tensor change (step 1) requires retraining the pattern model — coordinate with Area 1 so both label and BPM fixes go into the same training run.

---

## Area 5 — Evaluation, Benchmarking, and ONNX Default Readiness

**Problem:** There is no end-to-end benchmark of the ONNX models against real guitar input. `MA_ENABLE_ONNX` is off by default because the models have not been validated at system level. CPU cost of ONNX inference has not been measured. There is no plan for when/how ONNX becomes the production default.

**Goal:** Define and run a repeatable evaluation that gives confidence in the ONNX path, measure CPU cost, and establish the conditions under which `MA_ENABLE_ONNX=ON` becomes the recommended build configuration.

**What to change:**

1. **Create a benchmark test that measures ONNX inference latency** on target hardware (M-series Mac). Add a CTest or standalone executable that runs 10,000 inference calls against all three models and reports min/mean/max wall-clock time. Gate: all three models must complete a single inference call in < 5 ms (leaving the 20 ms polling budget with margin).

2. **Build an A/B comparison tool** (offline, not real-time). Given a sequence of `FeatureVector` values (from the capture pipeline in Area 2), run both `RuleBasedInference` and `OnnxInference` and print a side-by-side comparison of their outputs. This makes model behavior on real data inspectable without a DAW.

3. **Define ONNX default readiness criteria.** Document in `docs/ONNX_READINESS.md` the exact conditions that must be true before `MA_ENABLE_ONNX` flips to ON by default:
   - Pattern model macro-F1 ≥ 0.65 on real-audio evaluation set (Area 2)
   - Pattern model agrees with rule path ≥ 80% of the time on the same set
   - ONNX inference latency < 5 ms per call on reference hardware
   - Bass quality gate passing
   - Structure model normalization baked into graph (Area 3)
   - All ONNX contract tests passing in CI

4. **Add CPU budget tracking to CI.** After the latency benchmark exists, run it in CI on macOS and fail if any model exceeds the 5 ms gate. This prevents model size/complexity growth from silently breaking the real-time budget.

5. **Once all criteria are met, flip the default and remove the rule-path fallback for non-silence cases** — or document explicitly why the fallback is retained (graceful degradation is a valid reason, but it should be a conscious choice).

**Dependencies:** Area 2 (capture pipeline + evaluation data) is a prerequisite for step 2 and the real-audio F1 criterion in step 3. Areas 1, 3, 4 should be complete before evaluating readiness in step 3.

---

## Suggested Sequencing

```
Area 1 (label fix)      ──┐
Area 3 (quality gates)  ──┤──→  Area 4 (inference consistency + retrain)  ──→  Area 5 (evaluation + default)
Area 2 (domain gap)     ──┘
  └── capture pipeline needed for Area 5 real-audio evaluation
```

Areas 1, 2, and 3 can be planned and executed in parallel. Area 4 depends on Area 1 (for the retraining step). Area 5 depends on all preceding areas being substantially complete.

---

## What Is Explicitly Out of Scope

- Any changes to the audio-thread analysis pipeline (OnsetDetector, BeatTracker, StructureTagger, etc.) — these are performing well and have extensive test coverage.
- The `IInference` interface contract — it must remain stable for Phase 2 ONNX compatibility.
- Switching from ONNX Runtime to another inference backend.
- Online learning or real-time model updates during playback.
- Adding new MIDI patterns to `MidiPatternLibrary` — that is a musical content decision separate from ML correctness.
