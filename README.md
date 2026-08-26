# Metal Accompaniment — v0.9.27

A JUCE **8** **VST3 / AU** plugin for guitarists. You play guitar into it; it listens, and it writes **drum + bass MIDI** in real time so a drum kit and a bass instrument in your DAW can play along with you.

It does **not** guess your tempo from the guitar. It accompanies you at the **DAW’s global project tempo**, locked to the host transport and the project grid.

Current plugin version (shown top-right in the UI): **v0.9.27**. Full history: [`CHANGELOG.md`](CHANGELOG.md).

---

## What you need to play it

This is an **audio effect that produces MIDI**, not a standalone band. You need a guitar into a DAW, plus virtual drums and bass that can *hear* that MIDI.

### Computer and software

| Need | Notes |
|------|--------|
| **macOS** | Primary target. VST3 + AU. |
| **A DAW with MIDI routing** | Reaper, Ableton Live, Logic Pro, or similar. The host must expose project tempo and a playhead. |
| **Metal Accompaniment v0.9.27** | VST3: `~/Library/Audio/Plug-Ins/VST3/`. AU: `~/Library/Audio/Plug-Ins/Components/` (after install). |
| **CPU / buffer** | Designed for M-series at **256-sample** buffers. Smaller buffers (128) are fine if the session stays xrun-free. |

Windows/Linux VST3 builds exist in CMake, but day-to-day development and the AU path are macOS.

### Audio hardware

| Need | Why |
|------|-----|
| **Audio interface** with a **Hi-Z / instrument** input | Clean DI guitar is what the analyser hears. |
| **Low-latency monitoring** | You are playing *with* a drummer. Round-trip latency should feel like a click track (interface direct monitor or DAW monitoring, not a long software chain). |
| **Headphones or monitors** | Hear guitar + drums + bass together. |
| **Instrument cable** | Guitar → interface input 1 (typical). |

A USB interface with one instrument input is enough. You do **not** need a MIDI controller — the plugin *emits* MIDI; it does not take MIDI in.

### Instruments and virtual instruments

| Need | Why |
|------|-----|
| **Electric guitar** | The plugin classifies playing style (palm mute / open chords / single-note / sustain) and energy (silent / soft / loud). Drop-C and similar low tunings are supported for bass root tracking. |
| **Drum VSTi or sampler that speaks GM drums** | MIDI channel **10**. Kick = 36, snare = 38, hats/cymbals/toms use the General MIDI percussion map (see [MIDI map](#midi-map) below). Any kit that maps GM percussion will work (e.g. Addictive Drums, Superior Drummer, Battery, a GM drum sampler). |
| **Bass VSTi or sampler** | MIDI channel **2**. The plugin sends pitched bass notes (not GM percussion). Use a bass instrument, not a drum kit. If the bass sounds an octave too low/high, use **Bass octave** in the plugin UI. |

You can use hardware drum/bass modules instead of VSTs as long as they receive MIDI from the DAW on those channels.

### What this plugin is *not*

- Not a guitar amp. Put your amp/cab/IR **after** it (or on a parallel guitar path).
- Not a MIDI instrument you “play from a keyboard.” It has audio input, not MIDI input.
- Not a tempo detector. Set the **project BPM**; play in time with it (or with the drums it generates).

---

## How it interacts with your DAW

### Signal flow

```
Guitar (DI) ──► Metal Accompaniment ──► guitar audio out (dry pass-through)
                         │
                         └── MIDI out ──► drum track  (ch 10)
                                       └► bass track  (ch 2)
```

Insert **Metal Accompaniment** on the **guitar audio track**, **before** amp/cab/distortion. The analyser is trained on **dry DI**. Distortion upstream will confuse energy, style, and pitch.

Typical insert order on the guitar track:

1. Guitar input (DI)
2. **Metal Accompaniment**
3. Amp / cab / IR / FX

The plugin passes guitar through (`outputGain` scales that audio only — not MIDI velocity).

### Tempo: the DAW is the drummer’s click

**The drums and bass play at the DAW global tempo.**

- While the transport is **running**, BPM comes from the host playhead (`getBpm()`). The beat clock is anchored to host sample position, so grooves stay on the **project grid** across play, stop, seek, and loop.
- You do **not** tap tempo. You do **not** match the plugin to your playing speed. You set the session BPM (e.g. 120) and play *into* that grid, the same way you would play to a click or a drummer who already knows the song tempo.
- The on-plugin **Tempo (BPM)** parameter is a **fallback** for hosts with no playhead (the standalone app). In a normal DAW session it is ignored when the host reports a valid tempo.
- If you **jam with the transport stopped**, the plugin keeps time itself at the last valid host BPM (or the fallback knob / 120). That is for noodling; for a real take, **press play** so MIDI lines up with the arrangement.

Change the DAW tempo → the accompaniment changes with it on the next blocks. Loop the timeline → patterns stay locked to bar 1 of the loop, not to “how long you’ve been playing.”

### MIDI routing (the part that makes it audible)

The plugin **produces MIDI** and **does not accept MIDI**. Your DAW must route that output to instruments:

1. **Guitar track** — audio in, Metal Accompaniment inserted, audio out to your amp sim / master as usual.
2. **Drum track** — a GM drum instrument. Receive MIDI **from the guitar track**, **channel 10 only**.
3. **Bass track** — a bass instrument. Receive MIDI **from the guitar track**, **channel 2 only**.

If drums and bass share one MIDI cable/bus, filter by channel on each instrument so the kit does not play bass notes and the bass does not play kicks.

**Reaper (typical):**

1. Scan plugins; search **Metal** or **Niall**. Category: **Tools**.
2. Guitar track: input = your interface instrument channel; insert Metal Accompaniment (before amp).
3. Drum track: VSTi; route MIDI from the guitar track; MIDI filter **channel 10**.
4. Bass track: bass VSTi; same MIDI source; filter **channel 2**.
5. Set **project BPM**, arm the guitar track, **press play**, play in time.

Ableton / Logic: same idea — audio effect on the guitar audio channel, then a MIDI send or sidechain-style MIDI routing from that track to two instrument tracks. Logic users load the **AU** (`kAudioUnitType_MusicEffect`).

### Two ways to sit in a session

| DAW transport | What happens |
|---------------|----------------|
| **Playing** | Accompaniment locked to the arrangement: bars, loops, and tempo changes. This is the intended mode. |
| **Stopped** | Internal beat clock so you can still hear drums/bass while sketching. MIDI will **not** line up with existing clips until you hit play. |

---

## Playing: follow vs play vs riff lock

### Follow (default — PLAY button off)

The plugin **listens** and picks a groove from how you play:

- Energy → **SILENT / SOFT / LOUD** (verse-like vs chorus-like).
- Mel-CNN style head (once stable ~150 ms) steers the groove family: chugging → half-time/breakdown, open chords → chorus/breakdown, single-note runs → fast/thrash, sustain → sparse.
- Pattern changes commit on **bar boundaries**, with a hold so the kit does not flicker every strum.

You still play **at the DAW tempo**. Follow changes *which* groove, not *how fast*.

### Play (PLAY button on)

Scripted **song form** (intro / verse / chorus / …) at the DAW tempo. The sequencer walks the form; grooves rotate by musical phrase (2 bars for verse/chorus/solo, 4 for breakdown/intro/outro) so it does not sit on one pattern forever. Use **Loop** to repeat the form.

### Record riff / groove lock

**Record riff:** 1-bar count-in (kick on 1, stick on 2/3/4), then play a **4-bar** riff to the click. The plugin locks drums + bass to that take. While locked, the UI shows how many bars remain before a **transition** (contrast sections, then back to follow). **Forget** clears the riff.

**Lock (bars)** / **Transition (bars)** / **Transition sections** control how long the lock holds after you leave the riff, and how many contrast sections play before follow returns.

---

## Controls (v0.9.27)

| Control | What it does |
|---------|----------------|
| **Genre** | Rock (default), Hard Rock, Punk, Metal, Sludge — groove feel, velocities, pattern pool. |
| **Swing** | Delays off-8th drum events (0–100%). Genre can set a default. |
| **Bass octave** | −12 / 0 / +12 for bass VSTs with a limited or mis-labelled range. MIDI 36 = C2 (some instruments display that as C1). |
| **Song form** | Presets (Standard Metal, Sludge/Drone, Short Punk, …) plus an editable custom section list. |
| **Loop** | Repeat the song form in Play mode. |
| **Lock (bars)** | How long a riff lock holds after you stop playing the riff (returning to it extends it). |
| **Transition (bars / sections)** | After lock expires: length and count of contrast sections before follow. |
| **PLAY** | On = song-form playback. Off = follow/listen. |
| **Record riff / Forget** | Capture or clear a 4-bar riff lock. |
| **Output Gain** | Guitar pass-through level only. |

Live readouts: **BPM** (host tempo), **State**, **Pattern**, **Style**, **RMS**, **Centroid**, **HF Flux**, **noise floor**, groove/lock status.

---

## MIDI map

**Drums — channel 10 (GM percussion)**

| Note | Instrument |
|------|------------|
| 36 | Kick |
| 38 | Snare |
| 41 / 45 / 48 | Floor / mid / high tom |
| 42 / 46 | Closed / open hat |
| 49 | Crash |
| 51 / 53 | Ride / ride bell |
| 52 | China |
| 55 | Splash |

**Bass — channel 2** — pitched notes, transposed to the tracked guitar root (drop-C tracking down to ~C2). Octave shift is the **Bass octave** control.

---

## What’s new in v0.9.26

- **Follow-mode style steering** — palm-mute / open-chord / single-note / sustain actually biases which groove family you get (still gated by SOFT/LOUD and the 2-bar commit hold).
- **Play-mode phrase rotation** — grooves hold for a phrase (not one pattern per bar); verse 1 ≠ verse 2.
- **Fill variety** — last-bar fills scale with section energy (Fill Medium is now used).
- **Riff-lock progress** in the UI — “bar X/Y · N left before transition.”

---

## Build (developers)

Requirements: **CMake 3.22+**, C++20, **Git**, **ONNX Runtime** (`brew install onnxruntime`).

```bash
cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build-onnx --parallel

cp -R "build-onnx/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3" \
  ~/Library/Audio/Plug-Ins/VST3/
```

Artifacts: VST3 and AU under `build-onnx/MetalAccompaniment_artefacts/Release/`.

```bash
build-onnx/MetalAccompanimentTests
build-onnx/MetalAccompanimentIntegrationTests
```

Training the mel-CNN and exporting `assets/metal_groove.onnx` is documented under `training/` and [`docs/DATA_STRATEGY.md`](docs/DATA_STRATEGY.md).

---

## Documentation

| Doc | Content |
|-----|---------|
| [`CHANGELOG.md`](CHANGELOG.md) | Version history |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Threading, DAW-tempo clock, inference |
| [`docs/ONNX_IO.md`](docs/ONNX_IO.md) | ONNX tensor contracts |
| [`docs/DATA_STRATEGY.md`](docs/DATA_STRATEGY.md) | Data / model improvement |
| [`.planning/ROADMAP.md`](.planning/ROADMAP.md) | Milestone tracking |
