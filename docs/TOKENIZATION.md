# Tokenization (symbolic MIDI events)

Phase 9 defines an **event-based** representation for offline prep. **Model tensors and batched training inputs** are out of scope here; see Phase 15 for tensor pipelines. This document is the contract for `training/prep_midi.py` output.

## Bridge to the plugin (long-term)

Runtime policy will eventually consume **`FeatureVector`** (and time history) as described in `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md` — see `src/analysis/FeatureVector.h` for the feature bundle passed toward `IInference`. **Prep output in Phase 9 stays purely symbolic** (MIDI-derived events only); no requirement to emit `FeatureVector`-shaped fields in JSON.

## Time base

- **Absolute time** per event: **`time_sec`** (float, seconds from the start of the file), derived from MIDI tick time using the file’s **ticks per beat** and **tempo** meta-events.
- **Alternative for debugging:** **`tick`** (integer, raw MIDI ticks from file start) may be included when useful; **`time_sec`** remains canonical for cross-file comparison.

## Event types

Each record is one of:

- **`note_on`** — Note started.
- **`note_off`** — Note ended (emitted when MIDI signals note-off or note-on with velocity 0).

Fields common to note events:

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | `"note_on"` or `"note_off"` |
| `time_sec` | number | Absolute time in seconds |
| `channel` | integer | **1–16** (MIDI channel; **10** = drums in GM convention) |
| `note` | integer | MIDI note number **0–127** |
| `velocity` | integer | **0–127** |

## Drum channel (GM)

- **Channel 10** (1-based) / **index 9** (0-based) is the **standard drum** channel in General MIDI. Events on that channel are still encoded with `channel: 10` in JSON for readability.

## Output format

- **Default file output:** **JSON array** of event objects: `[{...}, {...}, ...]`
- **Optional:** **JSONL** (one JSON object per line) when streaming — same field names per line.

## Example object (excerpt)

```json
{
  "type": "note_on",
  "time_sec": 0.0,
  "channel": 10,
  "note": 36,
  "velocity": 100
}
```

## Schema versioning

Bump this document when the event vocabulary or field names change (e.g. when a base model tokenizer is chosen).
