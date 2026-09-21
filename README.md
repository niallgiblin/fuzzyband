# fuzzyband

A JUCE 8 **VST3 / AU** plugin that listens to a live guitar and emits **drum and bass MIDI** in
real time, so a drum kit and a bass instrument in your DAW play along with you.

It does **not** guess your tempo. It follows the **DAW's project tempo** and locks the groove to
the host transport, so everything stays on the grid across play, stop, seek and loop.

Built for rock and metal: 28 authored grooves, 13 genre presets, and a 28-class mel-CNN pattern
selector that reacts to how you play.

---

## Download

Prebuilt installers, self-contained — the ML model and ONNX Runtime are bundled, so there is
nothing else to install:

- **macOS** (universal, Apple Silicon + Intel) — VST3 + AU + Standalone
- **Windows** (x64) — VST3 + Standalone
- **Linux** (x64) — VST3 + Standalone

[**Download fuzzyband →**](https://niallgiblin.github.io/fuzzyband/)

macOS will report the build as unsigned. Right-click the plugin → **Open**, or clear the
quarantine flag once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3
```

---

## What you need

| | |
|---|---|
| **A DAW** | Reaper, Ableton Live, Logic Pro, or similar. The host must publish a playhead and project tempo. |
| **Audio interface** with a Hi-Z / instrument input | The analyser is built for clean DI guitar. |
| **Guitar** | Drop tunings supported; bass root tracking works down to roughly C2. |
| **Drum instrument** | Anything that speaks **General MIDI percussion**, on **channel 10**. |
| **Bass instrument** | A bass synth or sampler, on **channel 2**. |

Not included: an amp or cab sim. Put those **after** the plugin.

---

## Setup

fuzzyband is an **audio effect that produces MIDI**. It has an audio input and no MIDI input.

```
Guitar (DI) ──► fuzzyband ──► dry guitar out
                         │
                         └── MIDI out ──► drum track  (ch 10)
                                       └► bass track  (ch 2)
```

1. Insert fuzzyband on the **guitar audio track**, **before** any amp, cab or distortion. The
   analyser is trained on dry DI; distortion upstream confuses energy, style and pitch detection.
2. Add a **drum track** with a GM drum instrument. Route MIDI to it **from the guitar track**,
   filtered to **channel 10**.
3. Add a **bass track** with a bass instrument. Same MIDI source, filtered to **channel 2**.
4. Set the **project BPM**, arm the guitar track, **press play**, and play in time.

Controls, modes, MIDI map and troubleshooting: **[PLUGIN_GUIDE.md](PLUGIN_GUIDE.md)**.

---

## Build from source

macOS is the primary target. Requires **CMake 3.22+**, a C++20 compiler, Git and
**ONNX Runtime**:

```bash
brew install onnxruntime

cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build --config Release --parallel
ctest  --test-dir build --output-on-failure --config Release
```

Install into your user plug-in folders (Reaper and most hosts scan these):

```bash
./scripts/install-plugin-to-user.sh --build build --config Release
```

Built artefacts land in `build/MetalAccompaniment_artefacts/Release/`.

| CMake option | Default | Purpose |
|---|---|---|
| `MA_ENABLE_ONNX` | ON | ONNX Runtime inference. Requires `ONNXRUNTIME_ROOT`. |
| `MA_BUILD_TESTS` | ON | Unit + integration test binary |
| `MA_BUILD_STANDALONE` | ON | Standalone app target |
| `MA_BUNDLE_GROOVE_RENDERER` | ON | Bundle `assets/groove_renderer.onnx` |

---

## How it works

The plugin only reads the guitar to extract features — it never records or routes your audio.

- **Analysis** (`src/analysis/`) — RMS and onset energy, spectral centroid, high-frequency flux,
  YIN pitch tracking, and a mel spectrogram computed on the audio thread.
- **Inference** (`src/inference/`) — a 28-class mel-CNN pattern selector (`assets/metal_groove.onnx`)
  running on a background thread, with a rule-based fallback if the model cannot load.
- **MIDI** (`src/midi/`) — 28 authored grooves with humanisation, generated fills, and a
  monophonic bass voice that can mirror what you play. Emitted on the audio thread.

Tempo and the bar grid are **host-authoritative**: BPM comes from the DAW playhead, not from the
guitar, and pattern changes commit on bar boundaries.

---

## Third-party licences and attribution

| Component | Licence |
|---|---|
| [JUCE 8](https://juce.com) | GPLv3 or commercial |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | MIT |
| [moodycamel::ConcurrentQueue](https://github.com/cameron314/concurrentqueue) | BSD 2-Clause |
| [Catch2](https://github.com/catchorg/Catch2) | BSL-1.0 |
| Alegreya Sans, IBM Plex Mono | SIL Open Font Licence — `assets/fonts/*-OFL.txt` |
| Groove training data | Derived from the [Groove MIDI Dataset (GMD)](https://magenta.tensorflow.org/datasets/groove), CC-BY 4.0 |
