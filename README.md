# Fuzzyband

Fuzzyband is a guitar plugin that listens to your playing inside a DAW and emits MIDI notes to drum and bass instruments for real-time accompaniment in rock and metal genres. It is not a music production tool but rather a scrapbook for improvisation practice or experimenting with songwriting ideas.

It locks to your DAWs set tempo and using both rules-based logic and machine learning it reacts to your playing live, learns your phrasing and acts on-the-fly much like a real drummer and bassist you are improvising with.

Demo video: [![Watch the video](https://img.youtube.com/vi/fv9Rx1gKaZM&t/maxresdefault.jpg)](https://www.youtube.com/watch?v=fv9Rx1gKaZM&t)   

---

## Download

Download available here: [niallgiblin.github.io/fuzzyband](https://niallgiblin.github.io/fuzzyband/)

- **macOS** (universal, Apple Silicon + Intel) — VST3 + AU + Standalone
- **Windows** (x64) — VST3 + Standalone
- **Linux** (x64) — VST3 + Standalone

NB: macOS may report the vst3 build as unsigned. Either use AU or clear the quarantine flag in the terminal:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3 /Library/Audio/Plug-Ins/VST3/fuzzyband.vst3
```

---

## Tech stack

- C++20, built with CMake 3.22+
- JUCE 8.0.10 - VST3, AU, and Standalone
- Python 3  data work and scripting
- ONNX Runtime - `metal_groove.onnx` groove selector and `style_cnn.onnx` perception head
- Catch2 - unit and integration tests

---

## What you need

Electric guitar

Audio interface

Computer with a DAW

Drums and bass plugins

---

## Setup

fuzzyband is an audio effect that produces MIDI:

```
Guitar (DI) ──► fuzzyband ──► dry guitar out ──► whatever other FX you want
                         │
                         └── MIDI out ──► drum track  (ch 10)
                                       └► bass track  (ch 2)
```

1. Insert fuzzyband on the guitar audio track, before any amp, cab or distortion. The
   analyser is trained on dry DI; distortion upstream confuses energy, style and pitch detection.
2. Add a drum track with a GM drum instrument. Route MIDI to it from the guitar track,
   filtered to channel 10.
3. Add a bass track with a bass instrument. Same MIDI source, filtered to channel 2.
4. Set the project BPM, arm the guitar track, press play, and play in time.

Controls, modes, MIDI map and troubleshooting: **[USER_MANUAL.md](USER_MANUAL.md)**.

---

## Build from source

macOS is the primary target. Requires CMake 3.22+, a C++20 compiler, Git and
ONNX Runtime:

```bash
brew install onnxruntime

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build --config Release --parallel
ctest  --test-dir build --output-on-failure -C Release
```

Install into your user plug-in folders (Reaper and most hosts scan these):

```bash
./scripts/install-plugin-to-user.sh --build build --config Release
```

Built artefacts land in `build/MetalAccompaniment_artefacts/Release/`.

| CMake option                  | Default | Purpose                                               |
| ----------------------------- | ------- | ----------------------------------------------------- |
| `MA_ENABLE_ONNX`            | ON      | ONNX Runtime inference. Requires`ONNXRUNTIME_ROOT`. |
| `MA_BUILD_TESTS`            | ON      | Unit + integration test binary                        |
| `MA_BUILD_STANDALONE`       | ON      | Standalone app target                                 |
| `MA_BUNDLE_GROOVE_RENDERER` | ON      | Bundle`assets/groove_renderer.onnx`                 |

---

## How it works

The plugin only reads the guitar to extract features, it doesn't record or route your audio.

- Analysis (`src/analysis/`) — RMS and onset energy, spectral centroid, high-frequency flux,
  YIN pitch tracking, and a mel spectrogram computed on the audio thread.
- Inference (`src/inference/`) — a 28-class mel-CNN pattern selector (`assets/metal_groove.onnx`)
  running on a background thread, with a rule-based fallback if the model cannot load.
- MIDI (`src/midi/`) — 28 authored grooves with humanisation, generated fills, and a
  monophonic bass voice that can mirror what you play. Emitted on the audio thread.

Tempo and the bar grid are host-authoritative: BPM comes from the DAW playhead, not from the
guitar. In Play a groove holds for a phrase: 2 bars for verse, chorus and solo, 4 for
breakdown, intro and outro. A large dynamic jump can move the kit on the next beat.

---

## Rules-based Decisions and Machine Learning

fuzzyband chooses which groove to play with two cooperating layers: a mel-CNN that reads
how you are playing and proposes one of the 28 authored grooves, and deterministic rules
that decide whether that proposal is allowed to play.

### The decision pipeline

```
guitar audio
   │
   ├─ EnergyAnalyser ──────────► rmsEnergy · spectralCentroid · highFreqFlux · subBassRatio
   ├─ StructureTagger ─────────► state: SILENT / SOFT / LOUD   (hysteresis-gated)
   ├─ PitchEstimator ──────────► pitchRootMidi · pitchConfidence
   ├─ onset statistics ────────► onsetDensityPerBeat · onsetIoiBeats
   └─ MelSpectrogramExtractor ─► 64 × 32 log-mel
              │
      FeatureVector + mel window  (lock-free queues, audio → background)
              │
   ┌──────────┴────────────────────────────────────────────┐
   │ background thread (~50 Hz)                            │
   │   metal_groove.onnx ──► pattern vote 0–27             │
   │   style_cnn.onnx    ──► articulation 0–4              │
   │   pattern_rules.h   ──► state filter + rhythm refine  │
   └──────────┬────────────────────────────────────────────┘
              │ atomic pattern index (a vote, not a commit)
              │
   ┌──────────┴────────────────────────────────────────────┐
   │ audio thread                                          │
   │   Play: section pool, then keep the vote if it        │
   │         re-homes into that pool                       │
   │   idle / follow: commit the vote                      │
   │         (genre + style rules; frozen while locked)    │
   └──────────┬────────────────────────────────────────────┘
              │
      PatternPlayer ──► MIDI
```

The model runs on the background thread, and the audio thread stays allocation-free.
During Play the audio thread is the only writer of the playing pattern: it does not
take a groove commit from inference, and it reads the atomic index as a vote at each
phrase boundary. Outside Play, inference may commit that vote itself, unless capture
or a locked groove has frozen selection.

### What the rules decide

`src/inference/pattern_rules.h` lists the thresholds. Both the ML and
the rule-only path call into it.

| Stage                 | What it does                                                                                                                                                                                                                                          |
| --------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Energy → base groove | `SILENT` → Silent. `SOFT` → Verse Groove / Half-Time / Verse Fast by BPM. `LOUD` → Chorus Mid / Chorus Fast.                                                                                                                                 |
| State compatibility   | A hard filter. No groove from any source may contradict the detected energy.                                                                                                                                                                          |
| Diversification       | Energy, spectral centroid, BPM and bar phase route within the family — half-time for quiet sludge, blast beat for fast and bright, sparse breakdown for quiet and slow, thrash for fast 16ths.                                                       |
| Rhythm refinement     | At ≥1.8 attacks per beat (8th-note chugging or faster) the groove is corrected upward in density, so the kit tracks your picking and not just your loudness.                                                                                         |
| Style steering        | Follow and idle only. The perception head's articulation routes the family: palm-mute chugs → half-time/breakdown, open chords → chorus/breakdown, single-note runs → fast/thrash, sustain → sparse. Play does not restyle the vote.              |
| Genre routing         | Each of the 13 presets owns a vocabulary. Rock-leaning genres re-home the metal-era indices into the rock set (22–27); metal-family genres keep the metal routing. In Play this re-home runs on the audio thread, immediately before the pool check. |

Play's section pools, phrase lengths (2 bars for verse/chorus/solo, 4 for
breakdown/intro/outro), fill selection, and the post-lock contrast ladder are
rule-based. The groove inside a pool is not a fixed rotation. On each phrase boundary
the audio thread draws a seeded pool member, then replaces it with the model's vote
when genre re-homing lands that vote in the pool, it is not the groove just played,
and it is not Silent. Fast picking (≥2.5 attacks per beat) shrinks the pool to its
denser members first, and a vote outside that set is dropped. A locked Record riff
does not take a new vote: capture and the locked A/B grooves freeze pattern selection.

### What the model does

`assets/metal_groove.onnx` (~675 KB) is a small convolutional network over a log-mel
spectrogram:

|                  |                                                                                                                                                                                                                                                                                                                                          |
| ---------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Input            | `1 × 64 × 32` — 64 mel bands (0–8 kHz), 32 frames (~370 ms), from a 2048-point FFT at 512-sample hop                                                                                                                                                                                                                               |
| Backbone         | 3 convolution blocks (16 → 32 → 64 channels) + adaptive average pool to 4×4 + linear layer → a 128-d bottleneck                                                                                                                                                                                                                      |
| Runtime decision | the bottleneck vector is compared by cosine similarity against 28 precomputed per-pattern centroids                                                                                                                                                                                                                                      |
| Variety          | the live path samples the top 3 nearest centroids, weighted by similarity and seeded from the section entry bar, so a phrase stays stable while a neighbour can still win. The Record-riff lock pick is the one deterministic nearest-neighbour                                                                                          |
| Perception head  | `assets/style_cnn.onnx`, a separate 5-way classifier on the same mel window — palm mute, open chord, single note, sustain, silence. If that model is missing, classification falls back to the `style_logits` head on `metal_groove.onnx`. In Play the style updates the readout and fill context; it does not reroute the groove |

Every vote is checked for state compatibility on the background thread, then
rhythm-refined. In Play the audio thread re-homes it for the genre and keeps it only
when it belongs to the current section pool. Outside Play the same vote is genre-routed
and style-steered, then committed to the player, except while a riff is being captured
or a locked groove is held. If the model cannot load, the plugin falls back to
`RuleBasedInference` and keeps working — you only lose the timbre-aware proposals.
Configure with `-DMA_ENABLE_ONNX=OFF` for a rules-only build.

### Training data

The groove definitions are hand-authored C++. Nothing generates them. The data below teaches
the model to recognise the authored patterns, and tunes the human feel.

| Source                                                          | What it does                                                                                              |
| --------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| Hand-authored patterns (`src/midi/MidiPatternLibrary.cpp`)    | the 28 groove definitions themselves                                                                      |
| Procedural renders (`training/scripts/render_pattern_kit.py`) | the 28-class training audio — a GM kit rendered from the authored MIDI, 3 takes per class, mono 44.1 kHz |
| Guitar takes recorded on the project's own gear                 | the 5-class perception corpus (palm mute, open chord, single note, sustain, silence)                      |
| Groove MIDI Dataset v1.0.0 (Magenta)                            | per-genre velocity hierarchy and microtiming for the humanisation, plus the 48-fill bank                  |
| Lakh**`lmd_matched`** + tagtraum CD2 MSD tags                 | per-genre pattern popularity priors, used only to*order* section pools                                  |

Three things are worth being explicit about:

- **The reference audio is deliberately synthetic.** The original reference takes were
  low-frequency synthesised tones with almost no energy above 3 kHz — no hats, no cymbal
  crack — which gave the classifier a crude imprint. They were replaced with a procedural
  GM-kit render so the model learns realistic drum timbre.
- **GMD has no metal.** The rock statistics are data-derived; the metal and punk templates are
  documented transforms of the rock stats, because GMD's punk is almost entirely fills. Only
  aggregate per-genre statistics are redistributed — never the source corpora, and the tagtraum
  tag file is not committed.
- **Held-out, not memorised.** Small classes are augmented with time-stretch variants, and the
  train/validation split is grouped by source recording so no augmented variant of a take can
  leak across the line. Both trainers fail their quality gate on any zero-recall class rather
  than ship a model that quietly ignores a groove.

### What is *not* machine learning

Swing, humanisation and microtiming come from the GMD-derived templates. Section pacing, phrase length, fill selection, the contrast
ladder, the bass voice and the live mirror are deterministic rules. Which groove a Play
section actually plays can come from the model, inside those rules.
