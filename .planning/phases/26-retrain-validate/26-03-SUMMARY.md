---

phase: 26-retrain-validate
plan: "03"
subsystem: promotion-verification
tags: [onnx, reaper, pval-01, jam]

requires:

- plan: "01"
provides: Data prep + C++ integration absorbed into phase
- plan: "02"
provides: Retrained PatternNet, BassNet, StructureNet + promoted assets/

provides:

- Contract-validated assets/*.onnx at promotion paths
- Plugin build MA_ENABLE_ONNX=ON with bundled BinaryData models
- Human Reaper jam verdict (PVAL-01)

tech-stack:
  added: []
  patterns: [BinaryData ONNX bundle, UI inference-name proof]

key-files:
  created:
    - .planning/phases/26-retrain-validate/26-03-SUMMARY.md
  modified:
    - assets/accompaniment_model.onnx
    - assets/bass_model.onnx
    - assets/structure_model.onnx
    - CMakeLists.txt
    - "[TODO: list any src/ or script paths touched during 26-03 promotion]"

requirements-addressed: [PMODEL-04, BMODEL-03, STRUC-04, PVAL-01]

## duration: "[TODO: wall-clock for plan 03]"  
completed: 29-4-26  
plugin-version-at-jam: v0.4.7

# Phase 26 Plan 03 Summary — ONNX promotion + Reaper jam + phase close



## What was built (Plans 26-01 … 26-03)



- **26-01:** [TODO: one sentence]
- **26-02:** [TODO: one sentence]
- **26-03:** [TODO: promotion steps taken, build dir, ORT root if relevant]

---

## Model quality report


| Model        | ONNX contract                          | Gate metric                                                                        | Value                         | Gate pass?    |
| ------------ | -------------------------------------- | ---------------------------------------------------------------------------------- | ----------------------------- | ------------- |
| PatternNet   | X `[1,7]` → Y `[1]` int64              | macro-F1 (`val_gmd.pt`)                                                            | [TODO]                        | [ ] ✓ / [ ] ✗ |
| BassNet      | X_bass `[1,7]` → Y_bass `[1,32]`       | Interval vocab (`check_bass_intervals.py`): P5 / m3 / tritone / chromatic ≥1% each | [TODO: list % or “PASS/FAIL”] | [ ] ✓ / [ ] ✗ |
| StructureNet | X_struct `[1,12,7]` → Y_struct `[1,3]` | macro-F1 (held-out)                                                                | [TODO]                        | [ ] ✓ / [ ] ✗ |


**Training artifact dirs (optional traceability):**


| Model     | `training/artifacts/…` directory |
| --------- | -------------------------------- |
| Pattern   | [TODO]                           |
| Bass      | [TODO]                           |
| Structure | [TODO]                           |


---

## Reaper jam results (PVAL-01) — testimonial

**Date / time (local):** [TODO]

**Environment:**

- DAW: Reaper [TODO: version]
- Plugin: Metal Accompaniment **v[TODO]** (must match `CMakeLists.txt` for the build you tested)
- Binary: VST3 / AU: [TODO which]

**Observations (during ~5 minutes playing):**


| Criterion                                               | Met?              | Notes                                                               |
| ------------------------------------------------------- | ----------------- | ------------------------------------------------------------------- |
| ≥ **3 unique pattern indices** in UI (0–6)              | [ X] Yes / [ ] No | **0,1,2,3**                                                         |
| **Inference:** stays `**OnnxInference*`* entire session | [ X] Yes / [ ] No |                                                                     |
| Drums audible; intensity/groove reacts to playing       | [ X] Yes / [ ] No | it sort of does but not musically. Doesn't change seemlessly at all |
| Bass audible; follows root / has within-bar variation   | [ X] Yes / [ ] No | kind of random and not in sync with drums. RIght pitch though,      |
| No crash; no dropout > ~1 s                             | [ X] Yes / [ ] No | Stable                                                              |


**Free-form notes:** System needs to listen better for the tempo set by player and stick to it

### Verdict

- **PASS** — PVAL-01 satisfied; Phase 26 may close from a jam perspective.

---

## Decision traceability (D-26-XX)


| ID      | Decision (short)                                            | Where enforced                              | Outcome |
| ------- | ----------------------------------------------------------- | ------------------------------------------- | ------- |
| D-26-01 | Bass data from Lakh+GMD bass channels → `Y_bass [N,32]`     | `training/build_bass_dataset.py` / pipeline | [TODO]  |
| D-26-02 | Structure sequences `[12,7]` from `train_merged.pt` windows | `training/build_structure_dataset.py`       | [TODO]  |
| D-26-03 | Pattern macro-F1 ≥ 0.55 on `val_gmd.pt`                     | `train_gmd.py` / metrics                    | [TODO]  |
| D-26-04 | Bass behavioral interval gate (P5, m3, tri, chr ≥ 1%)       | `scripts/check_bass_intervals.py`           | [TODO]  |
| D-26-05 | Structure macro-F1 ≥ 0.80                                   | `train_structure.py`                        | [TODO]  |
| D-26-06 | Jam evidence = written testimonial (this file)              | `26-03-SUMMARY.md`                          | [TODO]  |
| D-26-07 | ≥3 unique pattern **indices in UI**                         | Reaper jam section above                    | [TODO]  |
| D-26-08 | Within-state repetition OK if ≥3 indices across jam         | Jam notes                                   | [TODO]  |
| D-26-09 | UI shows `Inference: OnnxInference` (no silent fallback)    | Jam section                                 | [TODO]  |
| D-26-10 | Atomic ship: all models + jam                               | Phase outcome                               | [TODO]  |
| D-26-11 | Failed atomic scope → Phase 27 backlog (if applicable)      | [TODO or N/A]                               |         |
| D-26-12 | Absorb prior WIP INF/C++ changes into phase tasks           | Git history / plans                         | [TODO]  |


---

## Automated verification (reference)

```bash
python scripts/validate_onnx_contract.py \
  --pattern assets/accompaniment_model.onnx \
  --bass assets/bass_model.onnx \
  --structure assets/structure_model.onnx
python scripts/check_bass_intervals.py --model assets/bass_model.onnx
```

**Last run date / exit codes:** [TODO]

---

## Deviations



[TODO: None or list]

---

## Artifacts produced / touched

- `assets/accompaniment_model.onnx`
- `assets/bass_model.onnx`
- `assets/structure_model.onnx`
- `CMakeLists.txt` (version ***[TODO]***)
- [TODO: VST3/AU install confirmation — manual]

---

## Self-Check

- Contract validators pass on final `assets/*.onnx` paths
- Plugin built with `MA_ENABLE_ONNX=ON`; UI version matches tested binary
- Jam testimonial complete; verdict PASS or FAIL is explicit
- If PASS: consider updating `.planning/STATE.md` for Phase 26 complete (per your workflow)

**Status:** [ ] **PASSED** — Phase 26 COMPLETE (models retrained + promoted + jam OK) · [ ] **FAILED / BLOCKED** — [TODO: reason]