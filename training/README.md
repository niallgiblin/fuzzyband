# Training / data prep (Phase 9)

Offline tooling to turn **symbolic MIDI** into a **JSON or JSONL** event stream aligned with `docs/TOKENIZATION.md`. Model training, tensor batching, and export live in later phases (see roadmap).

## Environment

- **Python 3.10+** recommended (3.x on macOS CI).

### Phase 15 (PYTR) — locked Python stack (macOS)

Use a **venv under `training/`** and install **all** prep + training + export dependencies from one file:

```bash
cd /path/to/repo
python3 -m venv training/.venv
source training/.venv/bin/activate
pip install --upgrade pip
pip install -r training/requirements.txt
```

This single `training/requirements.txt` installs **prep** (`mido`) and **training/export** stacks (**torch**, **onnx**, **onnxruntime**, **numpy**).

**Artifacts:** training runs write under **`training/artifacts/`** (gitignored) unless you pass **`--out-dir`** to a training script (see Phase 15 stub training below).

### Phase 17 (GMD) — bulk dataset download

The full **Groove MIDI Dataset** is fetched via **`training/download_gmd.py`** into gitignored **`training/data/`** (TFDS cache + manifest).

```bash
source training/.venv/bin/activate
python3 training/download_gmd.py
```

The first run downloads **large** files; after download, a **SHA-256** check validates the **MIDI-only** zip against the pinned TFDS checksum.

**Python 3.13 + TensorFlow 2.21 + protobuf 6.x:** TFDS 4.9.9 uses an older `FieldDescriptor` API; `training/tfds_compat.py` patches `DatasetInfo.read_from_directory` at import time so `tfds.load` works (no extra install).

After **`download_gmd.py`**, build **train/val tensors** (writes under **`training/data/processed/`**):

```bash
source training/.venv/bin/activate
python3 training/build_dataset.py
```

`build_dataset.py` performs a **HISTOGRAM** audit on **train-split** labels: if any pattern class **0–6** has **fewer than 50** examples, it prints **`FAIL`** / **`below 50`** and exits with code **3** (DATA-06 gate — tune **`FEATURE_PROXY.md`** / oracle constants; do not silently oversample in code).

For Phase 9 prep only (lighter install), you can still use a venv and `pip install -r training/requirements.txt` — the same pins apply.

## Prep CLI (`prep_midi.py`)

Convert a single `.mid` file to events:

```bash
python3 training/prep_midi.py --input training/fixtures/minimal.mid --output /tmp/prep_out.json
```

- Default output is a **JSON array** of event objects.
- Add `--jsonl` for **one JSON object per line** (streaming-friendly).

## Stub training (PYTR-02/03)

End-to-end **stub** train + ONNX export (synthetic data; no bulk dataset):

```bash
source training/.venv/bin/activate
python3 training/train_pytr_stub.py --epochs 3
```

Validate exported graphs against frozen I/O docs:

```bash
python3 scripts/validate_onnx_contract.py --pattern training/artifacts/<run>/pattern_trained.onnx --bass training/artifacts/<run>/bass_trained.onnx
```

Replace `<run>` with the timestamped directory printed when the script starts (under `training/artifacts/`).

**Note:** Phase 10 consumes **`pattern_trained.onnx`** when copied to **`assets/accompaniment_model.onnx`** (optional dev workflow).

## Phase 18 — GMD pattern training (PMODEL-02)

Prerequisite: **`build_dataset.py`** has produced **`train.pt`**, **`val.pt`**, and **`norm_stats.json`** under **`training/data/processed/`** (histogram gate passed).

Train **`PatternNet`** on the normalized tensors, log **`metrics.jsonl`**, and export **`pattern_trained.onnx`** (opset 17, baked affine normalization) into a timestamped directory:

```bash
source training/.venv/bin/activate
python3 training/train_gmd.py
```

The script prints **`Output directory:`** and **`Wrote …/pattern_trained.onnx`**. Use that absolute path with the contract checker:

```bash
python3 scripts/validate_onnx_contract.py --pattern /absolute/path/to/pattern_trained.onnx
```

Optional **`--data-dir`** defaults to **`training/data/processed`**. Runs outside **`training/`** are allowed but warned — only use trusted paths.

## Phase 34 — Feature capture evaluation

Evaluate a real plugin `feature_capture.v1` JSONL file against human time-range annotations:

```bash
source training/.venv/bin/activate
python3 training/evaluate_feature_capture.py \
  --capture ~/Documents/MetalAccompaniment/captures/feature_capture_SESSION.jsonl \
  --annotations path/to/labels.csv \
  --min-onnx-accuracy 0.65 \
  --top-disagreements 20
```

The annotation CSV header is:

```csv
start_seconds,end_seconds,label_index,label_name
```

The evaluator fails fast on bad capture schema, bad annotation ranges, label name/index mismatches, missing required capture fields, and ONNX accuracy below the threshold when captured ONNX predictions are available.

## Phase 19 — Bass model training (BMODEL-01/02)

No prerequisite — synthetic data is generated in-script (no external corpus required).

Train **`BassNet`** (7→32→16→4 MLP) on synthetic E2/A2/B1 metal-key pitch distributions, log **`metrics.jsonl`**, and export **`bass_trained.onnx`** (opset 17, baked affine normalization) into a timestamped directory:

```bash
source training/.venv/bin/activate
python3 training/train_bass.py
```

The script prints **`Output directory:`** and **`Wrote …/bass_trained.onnx`**. Use that absolute path with the contract checker:

```bash
python3 scripts/validate_onnx_contract.py --bass /absolute/path/to/bass_trained.onnx
```

Optional flags: `--seed 42`, `--batch-size 64`, `--max-epochs 200`, `--patience 8`, `--lr 1e-3`, `--out-dir /custom/path`.

## Phase 20 — Local install to plugin (EXP-01)

Copy validated Phase 18 / Phase 19 ONNX into `assets/` so `MA_ENABLE_ONNX=ON` builds bundle the trained graphs via `CMakeLists.txt` BinaryData (no `src/` changes).

Example (replace dirs with your `training/artifacts/...` run folders):

```bash
./scripts/install-model-local.sh --pattern-dir /path/to/pattern/artifact --bass-dir /path/to/bass/artifact
```

The script always runs `scripts/validate_onnx_contract.py --pattern … --bass …` before copying. Optional **`--copy-stats`** copies `norm_stats.json` / `bass_norm_stats.json` from the artifact dirs into `assets/` when present; the plugin does not load these — they are audit-only reproducibility sidecars (see Phase 20 context).

**Ops / cloud paths (not required for milestone close):** `scripts/promote-model.sh` uploads versioned artifacts and `current.json`; `scripts/download-model.sh` fetches into `assets/` when `MODEL_BUCKET` is set; see `infra/README.md` for bucket and OIDC wiring.

## References

- `docs/TOKENIZATION.md` — field names and event types
- `docs/DATASET_AUDIT.md` — dataset go/no-go and shortlist
- `docs/ONNX_IO.md` — pattern selector **X** / **Y** export contract
- `docs/BASS_ONNX_IO.md` — generative bass **X_bass** / **Y_bass** export contract
