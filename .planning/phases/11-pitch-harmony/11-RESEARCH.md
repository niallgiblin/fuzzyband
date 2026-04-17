# Phase 11 — Technical research (Pitch & harmony)

**Status:** Complete — ready for planning  
**Question answered:** What do we need to know to implement PITCH-01–04 on this codebase?

---

## 1. Architecture fit

- **Feature pipeline:** `AccompanimentProcessor::processBlock` builds a `FeatureVector`, enqueues it with `featureQueue.try_enqueue`, and the background thread drains the queue and calls `IInference::selectPattern`. Pitch/root for **bass routing** should be computed on the **audio thread** (same block as onset/energy) so it stays synchronous with MIDI emission; values are copied into `FeatureVector` for inference/logging parity with other features (~50 Hz effective rate is already dominated by `inferenceLoop`’s 20 ms sleep, not the enqueue rate).

- **Bass playback:** `PatternPlayer::emitEventsForRange` emits `ev.note` from `MidiPattern::bassEvents` unchanged. Nominal authoring uses `kBassRoot = 40` (E2) in `MidiPatternLibrary.cpp`. **Transpose at playback** = add integer semitone offset to bass channel notes only (D-11-03), leaving drums on channel 10 untouched.

- **Phase 10 ONNX:** `OnnxInference` implements `IInference` only; it does not own pitch. Phase 11 adds an **optional** second ONNX path for pitch (CREPE-class) that mirrors the “try load, else fallback” pattern in `makeInference()` — integration points in CMake/BinaryData can wait until a pitch ONNX asset exists; YIN must work with `MA_ENABLE_ONNX=OFF`.

---

## 2. Baseline pitch: YIN

- **Why YIN:** Standard monophonic F0 estimator, no ML dependency, suitable for real-time use with modest buffer history (autocorrelation-style search over lag range mapped to ~80 Hz–1 kHz guitar fundamentals).

- **Placement:** New class e.g. `PitchEstimator` in `src/analysis/` with `prepare(sampleRate, maxBlock)`, `reset()`, `process(const float* mono, int numSamples)` returning `float midiNote` and `float confidence` (e.g. clarity of YIN minimum / normalized residual).

- **Windowing:** Use the current audio block plus optional small ring buffer if YIN needs longer analysis than one block at low frequencies — document minimum buffer policy (e.g. accumulate until N samples for stable low notes).

---

## 3. Harmony hints (PITCH-02)

- Expose **one** stable value per analysis window: **MIDI root** (float or rounded int) + **confidence** [0,1]. No chord spelling in Phase 11.

---

## 4. Activation policy (D-11-04)

- **Cold start / silence:** Effective bass root = nominal **40** (offset 0). Reuse `StructureState::SILENT` and/or RMS below a floor consistent with existing energy analysis for “silence.”

- **Low confidence / noisy frames:** **Hold last good** root and confidence (state on audio thread, not in queue).

- **Thresholds:** Start with conservative defaults (e.g. confidence floor 0.3–0.5, min RMS for “pitched”) — tune in implementation with comments; tests use deterministic signals so behavior is stable in CI.

---

## 5. Optional ONNX pitch (PITCH-01 extension)

- **When `MA_ENABLE_ONNX`:** Optional `OnnxPitchEstimator` (or merged stub in `OnnxInference` scope) loading a separate pitch `.onnx` from BinaryData if present; if load fails, fall back to YIN only.

- **Threading:** ONNX must **not** run on the audio thread — either skip ONNX pitch in real-time until a background worker is ready (Phase 11 can ship **YIN-only realtime** + **ONNX path disabled or one-frame delayed** with documented latency) or run ONNX on the inference thread and pass results back via atomic snapshot (adds latency). **Recommendation for Phase 11:** Implement **YIN on audio thread** as the realtime source; add **optional** ONNX behind `#if MA_ENABLE_ONNX` on **background thread** with clear “last result” atomics and documented extra latency — or stub interface + docs if timeboxed.

---

## 6. Testing (PITCH-04)

- **Unit tests:** Sine wave at known frequency → expected MIDI note within tolerance (± cents or ±0.25 semitone).

- **PatternPlayer:** With fixed offset + known pattern, assert emitted bass note numbers shift by delta while drums unchanged.

- **CI:** `.github/workflows/ci.yml` already runs `ctest`; new tests must link into the existing `MetalAccompanimentTests` target.

---

## 7. Files likely touched

| Area | Files |
|------|--------|
| Features | `src/analysis/FeatureVector.h`, new `PitchEstimator.*` (or `YinPitchEstimator.*`) |
| Processor | `src/AccompanimentProcessor.cpp`, `AccompanimentProcessor.h` (optional display atomics) |
| MIDI | `src/midi/PatternPlayer.cpp`, `PatternPlayer.h` |
| Build | `CMakeLists.txt` (new sources) |
| Tests | `tests/test_pitch_estimator.cpp` (new), `tests/test_pattern_player.cpp` (extend) |
| Docs | Short note near tests or `docs/` on regression thresholds |

---

## 8. Risks

| Risk | Mitigation |
|------|------------|
| YIN unstable on distorted guitar | Confidence gating + hold-last-root; future ML optional |
| ONNX on audio thread | Never — background or stub |
| Queue overflow dropping pitch | Same as other features — `try_enqueue` drop is acceptable; hold-last masks single drops |

---

## Validation Architecture

> Nyquist validation is **disabled** for this project run (`nyquist_validation_enabled: false`). No `VALIDATION.md` artifact required.

---

## RESEARCH COMPLETE

Proceed to PLAN.md creation with waves: (1) pitch + FeatureVector + policy, (2) PatternPlayer transpose + wiring, (3) tests + CI + optional ONNX hook/docs.
