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

### `data/raw/pattern_00_*..pattern_27_*/` — 28-class groove reference audio
- **What:** reference takes, one directory per groove pattern class (index in
  `data/processed/class_map.json`), feeding the production mel-CNN
  (`assets/metal_groove.onnx`). Indices 0-21 are the metal set, 22-27 the rock set.
- **Provenance:** **project-generated** by `training/scripts/render_pattern_kit.py`
  (a procedural GM drum-kit render of the hand-authored `MidiPatternLibrary` patterns —
  drums ch 10, mono 44.1 kHz / 24-bit, 3 takes each). The original takes, added with the
  initial prototype, were *low-frequency synthesized tones* (~0% energy above 3 kHz) and
  have been replaced; the old set is backed up at `data/raw_synth_backup/`. Groove
  *definitions* are hand-authored C++; only feel/priors come from GMD/Lakh (below).
- **License:** project-owned.

### External datasets (Phase 4 — arrangement layer, C1–C3 integrated)

Caches live under `training/data/` (**gitignored**); only the small **derived
artifacts** below are committed. Each source's license must be verified before
any redistribution — we redistribute *derived aggregate statistics only*, never
the source corpora or tag files.

#### C1 — GMD (Groove MIDI Dataset) → groove templates
- **Source:** GMD v1.0.0 MIDI-only (CC-BY 4.0), fetched by `training/download_gmd.py`
  (TFDS cache under `training/data/tfds/`). ~1150 human drum takes with genre +
  BPM in `info.csv`.
- **Derived (committed):** `data/groove_templates.json` (per-genre velocity
  hierarchy + microtiming) and the generated `src/midi/GrooveTemplateData.h` baked
  into `Groove::Template`. Rock is data-derived directly; metal/punk are documented
  transforms of the rock stats (GMD has no metal, and its punk is almost all fills).
- **License note:** GMD is CC-BY 4.0; only aggregate per-genre statistics ship.

#### C2 — Lakh `lmd_matched` + MSD genre tags → selection priors
- **Source:** Lakh `lmd_matched` (`training/download_lakh.py`, ~116k MIDI) +
  tagtraum CD2 MSD genre ground truth (`training/download_msd_genre.py`, separates
  Rock/Metal/Punk). Tags map to files via the MSD track id in each path.
- **Derived (committed):** `data/lakh_priors.json` (per-genre content-derived
  tempo + groove-bucket distribution + pattern weights) and the generated
  `src/inference/PatternPriors.h` (per-genre pattern popularity weights).
- **License note:** tagtraum genre annotations are research/non-commercial —
  verify terms before redistribution. The `.cls` tag file is **not** committed
  (gitignored); only aggregate priors ship. Tempo is content-derived (drum-onset
  pulse), not header BPM.

#### C3 — DadaGP → articulation grammar *(access-gated; tooling only)*
- **Source:** DadaGP (~26k GuitarPro token songs, rock/metal-heavy). **Access-gated**
  — accept its terms / request access, then point
  `training/build_dadagp_articulation.py --tokens-dir` at the local token set. Not
  bundled.
- **Derived (committed when run):** `data/dadagp_articulation.json` — per-perception-label
  articulation distribution that augments/validates the perception classifier
  (human self-labels stay authoritative for real audio, `DATA_STRATEGY.md` §6.3).

#### C4 — Slakh2100 / MoisesDB *(gated — not in committed scope)*
Deferred per `DATA_STRATEGY.md` §6.4. No tooling yet; only revisited if the §6.4
A/B gate opens.

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

### Phase 4 arrangement-layer artifacts (C1–C3)

```bash
# C1 — GMD → groove templates (needs training/download_gmd.py first)
python3 training/build_groove_template.py
#   → data/groove_templates.json + src/midi/GrooveTemplateData.h

# C2 — Lakh + MSD genre tags → selection priors
python3 training/download_lakh.py         # ~1.3 GB (once)
python3 training/download_msd_genre.py    # tagtraum CD2 genre tags (gitignored)
python3 training/build_lakh_priors.py     # --max-files-per-genre 0 for the full 116k scan
#   → data/lakh_priors.json + src/inference/PatternPriors.h

# C3 — DadaGP → articulation grammar (DadaGP is access-gated, not bundled)
python3 training/build_dadagp_articulation.py --tokens-dir /path/to/dadagp/tokens
#   → data/dadagp_articulation.json
```

The generated headers (`GrooveTemplateData.h`, `PatternPriors.h`) and the JSON
sidecars are committed; the external caches and the `.cls` tag file are not.
