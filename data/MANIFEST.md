# Data Manifest

**Status:** Phase 3 deliverable of `docs/DATA_STRATEGY.md` (§5.5 — data provenance
/ versioning). Describes every dataset artifact, its source and license, and the
exact command to regenerate it.

---

## Layout convention

| Location | Contents | Tracked in git? | Regenerable? |
|---|---|---|---|
| `data/raw/<class>/*.wav` | source recordings (perception classes + 22 pattern reference takes) | **yes** (small corpus, ~300 MB) | no — these are the inputs |
| `data/processed/*.npy` | mel tensors (`X*.npy`, `y*.npy`) | **no** (`.gitignore`; exceed GitHub 100 MB limit) | **yes** — from `data/raw` |
| `data/processed/meta*.csv`, `class_map.json` | split/label metadata + class index map | **yes** (tiny, needed to interpret tensors) | yes — regenerated with the tensors |
| `training/data/`, `training/artifacts/` | external dataset caches + training run outputs | **no** (`.gitignore`) | yes — via download scripts |
| `tests/fixtures/*.wav`, `training/fixtures/**` | golden test fixtures | **yes** (needed for CI) | no |

Rule of thumb: **raw inputs and metadata are committed; derived tensors are
gitignored and regenerated locally.** Never commit `*.npy` — regenerate them.

---

## Sources

### `data/raw/palm_mute|open_chord|single_note|sustain|silence/` — perception corpus
- **What:** short guitar takes labeled by playing style (the perception taxonomy,
  `docs/LABEL_TAXONOMY.md` §2).
- **Provenance:** recorded by the project on its own gear/tone (in-domain).
  Honest self-labels — this is the circularity break (`DATA_STRATEGY.md` §5.2).
- **License:** project-owned.
- **Grow it with:** `training/slice_annotations.py` (slices a labeled take into
  per-class clips under `data/raw/<label>/`).

### `data/raw/pattern_00_*..pattern_21_*/` — 22-class groove reference audio
- **What:** reference takes, one directory per groove pattern class (index in
  `data/processed/class_map.json`), feeding the production mel-CNN
  (`assets/metal_groove.onnx`).
- **Provenance:** recorded/authored by the project.
- **License:** project-owned.

### External datasets (Phase 4 — not yet integrated)
E-GMD, Lakh (`lmd_matched`), DadaGP, Slakh2100/MoisesDB. See `DATA_STRATEGY.md` §6
and `training/README.md`. Each must have its license verified before
redistribution; caches live under `training/data/` (gitignored).

---

## Regenerate commands

All commands run from the repo root with the training venv active:

```bash
source training/.venv/bin/activate
```

### Perception dataset (5-class) → `X.npy`, `y.npy`, `meta.csv`
```bash
python3 training/scripts/build_mel_dataset.py \
  --raw-dir data/raw --out-dir data/processed
```

### Groove dataset (22-class) → `X_groove.npy`, `y_groove.npy`, `meta_groove.csv`, `class_map.json`
```bash
python3 training/scripts/build_mel_groove_dataset.py \
  --raw-dir data/raw --out-dir data/processed
```

Both builders assign a **grouped train/val/test split by source recording**
(§5.1) and write it to the `split` column of the meta CSV, so no augmented variant
of a take can leak across train/val. The trainers read that frozen split back.

### Train (reads the frozen grouped split)
```bash
python3 training/scripts/train_classifier.py            # perception (5-class)
python3 training/train_groove_model.py                  # groove (22-class)
```

Both fail their quality gate on any **dead (zero-recall) class** on the held-out
set (§5.1) — an honest signal to record another take of the weak class.
