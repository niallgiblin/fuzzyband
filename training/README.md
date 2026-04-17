# Training — data prep (Phase 9)

Offline tooling for **DATA-03**: MIDI → event JSON aligned with `docs/TOKENIZATION.md`.

The plugin (C++/JUCE) does **not** depend on this directory; Phase 15 may add full training.

## Setup (macOS)

```bash
cd training
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Prep stub

Convert a MIDI file to the Phase 9 event JSON schema:

```bash
python prep_midi.py --input /path/to/file.mid --output /path/to/out.json
```

If `--output` is omitted, prints JSON to stdout.

## See also

- `docs/TOKENIZATION.md` — field definitions
- `docs/DATASET_AUDIT.md` — dataset go/no-go
