# Baseline MIDI corpus (T0.2 / T3.1)

Current captures of the **v0.9.70** engine (post Phase 3: velocity trim, bidirectional
energy, centred microtiming, deterministic humanise). Used as the Phase 9 A/B corpus.

The pre-Phase-3 (v0.9.68) files live in `pre-phase3/` so T9.3 can still A/B
dynamics against the wall-of-127 kit.

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

T9.2 diffs the TSV absolute sample columns across the three buffer sizes; they must be identical after Phase 1 + T3.4.
