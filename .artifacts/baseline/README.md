# Baseline MIDI corpus (T0.2)

Pre-fix captures of the v0.9.67 engine, used as the Phase 9 A/B corpus.

Regenerate after a clean Release build:

```bash
./build/MetalAccompanimentIntegrationTests "[baseline]"
```

| Stem | Scenario |
| --- | --- |
| `record_dropc_{128,512,2048}` | 4-bar Drop-C palm-mute into a fresh Record session (LOCK=4, TRANSITION=4, Metal, 120 BPM) |
| `record_dropc_loop4_{128,512,2048}` | Same riff with a 4-bar DAW loop wrapping the playhead |
| `play_default_form_512` | Play mode, one pass of `INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4` |

Each stem has a `.mid` (SMF) and a `.tsv` (absolute sample, kind, channel, note, velocity). `MANIFEST.txt` records the version and settings.

T9.2 diffs the TSV absolute sample columns across the three buffer sizes; they must be identical after Phase 1.
