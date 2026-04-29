---
phase: 26
plan: "01"
status: complete
completed: "2026-04-29"
requirements:
  - BMODEL-03
  - STRUC-04
---

# Phase 26 — Plan 01 summary

## Objective

Generate BassNet (`[N,7]` → `[N,32]`) and StructureNet (`[N,12,7]` → `[N]` class) training tensors from the merged Phase 25 corpus plus Lakh MIDI bass lanes; capture prior INF/C++/training WIP in `26-WIP-ABSORPTION.md`.

## What shipped

### `training/build_bass_dataset.py`

- Parses MIDI via `prep_midi._events_from_file`, validates GM bass programs **33–40** on melodic channel **2** (`mido` channel **1**) using `mido.MidiFile(..., clip=True)` plus defensive handling for truncated/corrupt files (`EOFError`, etc.).
- Per-bar BPM from first `set_tempo`, bar-quantized 16-step piano-roll `(pitch_offset, velocity)` pairs interleaved into **32** floats.
- Per-bar proxy row uses `training/build_dataset._compute_proxy_row`; `stateFloat` (column 4) overwritten from drum activity in the same bar (ch **10** percussion: ≥3 onsets → LOUD, else any → SOFT, else SILENT).
- Outputs **`bass_train.pt` / `bass_val.pt`** under `--processed-dir`, with `GroupShuffleSplit`-style file grouping (or row split when only one MIDI qualifies).

### `training/build_structure_dataset.py`

- CLI now uses `--out-dir` + `--stem structure` → `structure_train.pt`, `structure_val.pt`, and `manifest_structure.jsonl`.
- Extended **5→7** features with placeholders; windowing **12×7** with `GroupShuffleSplit` on synthetic `group_id` blocks when merged `meta` lacks row-level IDs (Phase 25 `train_merged.pt` case).
- Labels clamp from `stateFloat` to **{0,1,2}**.

### `26-WIP-ABSORPTION.md`

- All **12** tracked WIP paths mapped to `verify-as-baseline` rg checks or `execute in 26-02` (`train_gmd.py`); eight baseline commands recorded with **`result: pass`**.

### Generated tensors (local, gitignored `training/**/*.pt`)

Regenerate with:

```bash
cd training
python3 build_bass_dataset.py --midi-dir data/lakh/lmd_matched --processed-dir data/processed --data-dir data/processed --max-files 5000
python3 build_structure_dataset.py --input data/processed/train_merged.pt --out-dir data/processed --stem structure
```

Smoke run used `--max-files 80` for bass (1991 train / 652 val bars). Structure run: **50,589** windows from `train_merged.pt` (class 2 rare — expected from upstream proxy distribution).

## Verification

- Plan inline Python probes for `_quantize_bass_to_pianoroll`, `_derive_root_from_events`, `_extend_features`, `_sample_sequence_window` — **PASSED** (structure window unit test uses ≥120-row single group so windows are non-empty).
- **`./build/MetalAccompanimentTests`** — all tests passed (74 cases, 2026-04-29).
- D-26-12 absorption script — **PASSED** (`verify_command` ×8, `result: pass` ×8).

## Deviations / notes

1. **Row-level `group_id` on `train_merged.pt`**: tensor `meta` is a single-source dict; Phase 25 `manifest_train.jsonl` is per-session (717 rows vs 64 867 tensor rows). Structure windows use **`_block_*` synthetic groups** (~50-row blocks) so [12,7] sequences stay within-file ordered — not true session IDs.
2. **`build_minimal_pattern_onnx.py` baseline**: original plan `rg` referenced `torch.zeros(1, 7)` — current stub uses ONNX helper `TensorProto.FLOAT, [1, 7]`. Documented working command: `rg '\\[1,\\s*7\\]' scripts/build_minimal_pattern_onnx.py`.
3. **`26-01` roadmap title** mentions an “inference name UI label”; executable scope for this PLAN file was tasks **1–3** only (datasets + absorption). UI label work remains for a later 26.x commit if not already present in `AccompanimentEditor`.

## Self-Check: PASSED

- Key files present: `training/build_bass_dataset.py`, `training/build_structure_dataset.py`, `.planning/phases/26-retrain-validate/26-WIP-ABSORPTION.md`.
- Gitignored `.pt` artifacts reproducible via commands above.
- C++ tests green; no plugin source changes in this plan.
