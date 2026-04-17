# Tokenization — symbolic MIDI (DATA-02)

**Binding default:** **Event-based** representation (see `999.1-CONTEXT.md` D-03).

This spec is what **`training/prep_midi.py`** implements for Phase 9. Future model training (Phase 15) may add special tokens (`<BAR>`, velocity bins, etc.) — bump the **schema version** when that happens.

---

## Schema version

| Field | Value |
|-------|--------|
| `schema_version` | `1` |

---

## Time base

- All times are **seconds** from the start of the MIDI file (`float`).
- Events are sorted by **`t`** ascending.

---

## Event types

Each emitted object is one of:

### `note_on`

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"note_on"` |
| `t` | float | Start time (seconds) |
| `pitch` | int | MIDI pitch 0–127 |
| `velocity` | int | 1–127 |
| `channel` | int | 0–15 |

### `note_off`

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"note_off"` |
| `t` | float | End time (seconds) |
| `pitch` | int | MIDI pitch 0–127 |
| `channel` | int | 0–15 |

### `meta` (optional)

Reserved for future use (time signature, tempo map). Phase 9 stub may omit or include `tempo` if trivially available.

---

## File format (prep output)

- **Default:** One JSON object per file with:
  - `schema_version`
  - `source` — original file path string
  - `events` — array of event objects

JSON Lines (`events` one per line) is **not** required for DATA-03; single JSON file is acceptable.

---

## Drum vs melodic

- **No separate field in v1.** Use **channel 9** (1-based: channel 10) as **General MIDI drums** when present; otherwise treat all channels as given in the file.

---

## Reference implementation

- `training/prep_midi.py` — canonical encoder (uses **mido** to parse MIDI; first `set_tempo` only for tick→second — stub does not model tempo ramps).
