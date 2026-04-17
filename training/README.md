# Training / data prep (Phase 9)

Offline tooling to turn **symbolic MIDI** into a **JSON or JSONL** event stream aligned with `docs/TOKENIZATION.md`. Model training, tensor batching, and export live in later phases (see roadmap).

## Environment

- **Python 3.10+** recommended (3.x on macOS CI).

```bash
python3 -m venv .venv
source .venv/bin/activate   # Windows: .venv\Scripts\activate
pip install -r requirements.txt
```

## Prep CLI (`prep_midi.py`)

Convert a single `.mid` file to events:

```bash
python3 training/prep_midi.py --input training/fixtures/minimal.mid --output /tmp/prep_out.json
```

- Default output is a **JSON array** of event objects.
- Add `--jsonl` for **one JSON object per line** (streaming-friendly).

## References

- `docs/TOKENIZATION.md` — field names and event types
- `docs/DATASET_AUDIT.md` — dataset go/no-go and shortlist
