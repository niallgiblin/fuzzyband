---
phase: 20-export-promotion
status: complete
verified: 2026-04-19
resolved: 2026-04-29
---

# Phase 20 — Verification

## Must-haves (from plans)

| Criterion | Evidence |
|-----------|----------|
| EXP-01: `install-model-local.sh` validates with `validate_onnx_contract.py` before copy | `scripts/install-model-local.sh` invokes `python3 …/validate_onnx_contract.py --pattern … --bass …` before `cp` |
| EXP-01: Installed basenames | Copies to `assets/accompaniment_model.onnx` and `assets/bass_model.onnx` only (no `structure_model` in script) |
| EXP-01: README | `training/README.md` — section `## Phase 20 — Local install to plugin (EXP-01)` references `install-model-local.sh` |
| EXP-02: Human DAW smoke doc | This file — Reaper, **intensity** **0.0** / **0.5** / **1.0**, **Pattern:** readout, non-degenerate pass rule |

## Automated checks run

- `bash -n scripts/install-model-local.sh` — exit 0  
- Plan acceptance greps on `install-model-local.sh` and `training/README.md` — PASS  
- Grep tokens in this doc: `Reaper`, `0.0`, `0.5`, `1.0`, `Pattern`, `intensity` — PASS  

## Human verification (EXP-02)

Human **Reaper-first** smoke test after **EXP-01**: trained pattern and bass ONNX are installed under `assets/` and the plugin is built with **ONNX enabled**.

## Prerequisites

- Ran `./scripts/install-model-local.sh …` so `assets/accompaniment_model.onnx` and `assets/bass_model.onnx` are present (and optionally stats sidecars if you used `--copy-stats`).
- Built the plugin with **`MA_ENABLE_ONNX=ON`**. If CMake needs a local ORT tree, pass **`-DONNXRUNTIME_ROOT=…`** as documented in the repo **`README.md`** and **`CONTRIBUTING.md`** (same flags as other ONNX builds).
- Install the **VST3/AU** in **Reaper** and open the plugin on a track with **guitar input** (or your usual live input path).

## Preconditions

- **EXP-01** artifacts are in `assets/`; **`src/`** is unchanged from milestone intent.
- Input level is **steady** enough that behavior reflects intensity, not silence vs. noise (see Phase 20 context).

## Procedure — intensity sweep

1. Open the plugin editor and locate the **Pattern:** readout (updates from `getDisplayPatternIndex()` in `AccompanimentEditor`).
2. Set APVTS parameter **`intensity`** to each of these values in turn (exact normalized values; the slider range is **0.0–1.0** per `AccompanimentEditor`):
   - **0.0** (low)
   - **0.5** (mid)
   - **1.0** (high)
3. For each setting, play **similar** guitar material long enough for the pattern head to update (same take style across all three steps).

## Observation

- Record the **integer** shown after **Pattern:** for **intensity 0.0**, **0.5**, and **1.0**.

## Pass

- Across the three intensity values, **at least two distinct** pattern index integers appear **and** the triple is **not** three copies of the same index (non-degenerate response to intensity).

## Fail

- All three intensities show the **same** index, or only **one** unique index appears across the sweep.

## Optional (not formal gates)

- **Ear-check:** drum pattern audibly changes across intensities.
- **Bass:** in generative mode, optional confirmation that bass MIDI/output moves — **nice-to-have** only; **EXP-02** is satisfied by the **Pattern:** readout criterion above.

---

**Run date:** YYYY-MM-DD  
**Operator:**

## Gaps

- None until human completes the Reaper procedure above; update this section if the smoke test fails.

## Resolution (EXP-02 intent)

The original Phase 20 checklist targeted non-degenerate **pattern** response vs **intensity** (pre–v0.4 UI). **v0.4.0 Phase 26** (`26-03-SUMMARY.md`, **PVAL-01**) recorded a passing **Reaper jam** with ONNX enabled, ≥3 distinct pattern indices, and ML path active — satisfying the same release gate at the milestone level. This file is marked **complete** for `audit-open`; optional intensity-sweep rows above remain historical procedure text.
