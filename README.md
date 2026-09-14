# fuzzyband (formerly Metal Accompaniment) — v1.0.3

A JUCE **8** **VST3 / AU** plugin for guitarists. You play guitar into it; it listens, and it writes **drum + bass MIDI** in real time so a drum kit and a bass instrument in your DAW can play along with you.

It does **not** guess your tempo from the guitar. It accompanies you at the **DAW’s global project tempo**, locked to the host transport and the project grid.

Current plugin version (shown top-right in the UI): **v1.0.3** — always confirm against
`CMakeLists.txt` line 4, which is the only authoritative source. Full history: [`CHANGELOG.md`](CHANGELOG.md).

> **New here, or picking this up to fix a bug?** Read
> [`docs/CONTEXT_HANDOFF.md`](docs/CONTEXT_HANDOFF.md) first — it states the real
> architecture, what is *not* wired up, and the active bug — then
> [`docs/PITFALLS_AND_INVARIANTS.md`](docs/PITFALLS_AND_INVARIANTS.md).

---

## Download

The download page is a **hub** that lists **fuzzyband** and **Fairo** (Pharaoh fuzz), and
each one's download links populate automatically when its version is tagged. Fuzzyband's
newest builds cover all three platforms:

- **macOS** (universal, Apple Silicon + Intel) — **VST3 + AU + Standalone**
- **Windows** (x64) — **VST3 + Standalone**
- **Linux** (x64) — **VST3 + Standalone**

Each fuzzyband download is **self-contained** (the ONNX model and runtime are bundled), so
there's nothing else to install.

> **Non-tech users:** grab the package for your OS below and follow the install note.
> **Developers:** skip the download and build from source (see [Build](#build-developers)).

[**Download fuzzyband + Fairo →**](https://niallgiblin.github.io/fuzzyband/)

For the macOS Gatekeeper "unverified" notice, see the installation notes in the
[`docs/RELEASING.md`](docs/RELEASING.md) — right-click **Open**, or
`xattr -dr com.apple.quarantine` the bundle once.

---

## What you need to play it

This is an **audio effect that produces MIDI**, not a standalone band. You need a guitar into a DAW, plus virtual drums and bass that can *hear* that MIDI.

### Computer and software

| Need | Notes |
|------|--------|
| **macOS** | Primary target. VST3 + AU. |
| **A DAW with MIDI routing** | Reaper, Ableton Live, Logic Pro, or similar. The host must expose project tempo and a playhead. |
| **fuzzyband v0.9.29** | VST3: `~/Library/Audio/Plug-Ins/VST3/`. AU: `~/Library/Audio/Plug-Ins/Components/` (after install). |
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
Guitar (DI) ──► fuzzyband ──► guitar audio out (dry pass-through)
                         │
                         └── MIDI out ──► drum track  (ch 10)
                                       └► bass track  (ch 2)
```

Insert **fuzzyband** on the **guitar audio track**, **before** amp/cab/distortion. The analyser is trained on **dry DI**. Distortion upstream will confuse energy, style, and pitch.

Typical insert order on the guitar track:

1. Guitar input (DI)
2. **fuzzyband**
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

1. **Guitar track** — audio in, fuzzyband inserted, audio out to your amp sim / master as usual.
2. **Drum track** — a GM drum instrument. Receive MIDI **from the guitar track**, **channel 10 only**.
3. **Bass track** — a bass instrument. Receive MIDI **from the guitar track**, **channel 2 only**.

If drums and bass share one MIDI cable/bus, filter by channel on each instrument so the kit does not play bass notes and the bass does not play kicks.

**Reaper (typical):**

1. Scan plugins; search **fuzzyband** or **Niall**. Category: **Tools**.
2. Guitar track: input = your interface instrument channel; insert fuzzyband (before amp).
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

## Controls (v1.0.3)

Verified against the editor at v1.0.3. Only the controls in this table actually
have a widget in the panel:

| Control | What it does |
|---------|----------------|
| **Genre** | 13 presets: Rock (default), Hard Rock, Punk, Metal, Sludge, Thrash Metal, Death Metal, Black Metal, Doom Metal, Djent, Classic Rock, Alternative, Grunge. Sets groove feel, velocity profile, swing default, half-time bias and BPM range (`GrooveTemplate.h:254-266`). |
| **Swing** | Delays off-8th drum events (0–100%). The genre supplies a default. |
| **Humanize** | Scales the per-bar ornament probabilities (open hat, extra ghost, micro-fill, ride switch). At 0 the bar is played verbatim. |
| **Lock** | How long a riff lock holds after you stop playing the riff. |
| **Transition** (bars / sections) | After lock expires: how long each contrast section lasts, and how many distinct contrast families rotate (`A→B→A→C→A…`). |
| **Play** | On = song-form playback. Off = follow/listen. |
| **Record riff / Forget** | Capture or clear a riff lock. |
| **Section list** | The custom song form, editable in place. |

Live readouts: one quiet line — `120.0 bpm · SILENT · P23 · Open Chord`
(BPM · structure state · pattern index · playing style).

> **Parameters without an editor widget.** `bassTranspose`, `songForm`, `loop`
> and `outputGain` exist in the APVTS (so they are saved with the session and
> automatable) but have **no control in the panel**. An earlier version of this
> README listed them as UI controls; it was wrong.

> **Bass mirroring.** In Play and Riff-listening the intent is that the bass plays
> what *you* play, with the authored/harmonic line filling only the gaps. That
> contract is implemented but has regressed repeatedly and is **currently
> unreliable** — see [`docs/BASS_MIRRORING.md`](docs/BASS_MIRRORING.md).

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

## What's new in v1.0.3

- **Bass mirror owns the voice.** The live mirror no longer plays *underneath* the
  authored/harmonic line. A mirrored note claims the monophonic bass voice and the
  grid line is muted while it rings, so Play mode tracks your playing instead of
  sounding like a root/fifth drone. (Whether this is reliable on a real distorted
  guitar is still open — see [`docs/BASS_MIRRORING.md`](docs/BASS_MIRRORING.md).)
- **A 20 ms onset envelope.** `EnergyAnalyser` gained `getOnsetRmsEnergy()`; the
  attack detector now sees a real decay→rise edge instead of leaning on stale
  state, which had made one sustained note mirror as 2–3 notes.
- **Editor fits the screen** (v1.0.2) — the panel scrolls, the layout adapts down
  to a 460 px window, and the opening size is clamped to the display.
- **Riff phase and DAW loop fixes** (v1.0.1) — the frozen riff no longer enters a
  bar late, a bar-aligned loop wrap is a no-op instead of re-phasing the riff, and
  onset capture no longer collapses at large buffer sizes.

Full detail: [`CHANGELOG.md`](CHANGELOG.md).

---

## Build (developers)

Requirements: **CMake 3.22+**, C++20, **Git**, **ONNX Runtime** (`brew install onnxruntime`). There is one local tree: `build/`, with ONNX, tests, and standalone all on.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build --parallel

./scripts/install-plugin-to-user.sh --build build --config Release
```

Artifacts: VST3, AU, and Standalone under `build/MetalAccompaniment_artefacts/Release/`.

```bash
ctest --test-dir build --output-on-failure --config Release
```

Training the mel-CNN and exporting `assets/metal_groove.onnx` is documented under `training/` and [`docs/DATA_STRATEGY.md`](docs/DATA_STRATEGY.md).

---

## Documentation

**Start here:** [`docs/CONTEXT_HANDOFF.md`](docs/CONTEXT_HANDOFF.md) — a
self-contained briefing (architecture, build, the active bug, what is not wired).

| Doc | Content |
|-----|---------|
| [`docs/CONTEXT_HANDOFF.md`](docs/CONTEXT_HANDOFF.md) | **Start here.** What the project is, what actually works, active bug |
| [`docs/PITFALLS_AND_INVARIANTS.md`](docs/PITFALLS_AND_INVARIANTS.md) | Traps that keep recurring; contracts not to break; pre-flight checklist |
| [`docs/BASS_MIRRORING.md`](docs/BASS_MIRRORING.md) | The hard active bug: contract, history of nine attempts, ranked hypotheses |
| [`docs/PROJECT_TIMELINE.md`](docs/PROJECT_TIMELINE.md) | Every era: what was tried, what failed, what was reverted |
| [`docs/TEST_AUDIT.md`](docs/TEST_AUDIT.md) | Suite state (348 cases green) and its six real coverage gaps |
| [`docs/DOCS_INDEX.md`](docs/DOCS_INDEX.md) | Which docs are current, which are archived, and why |
| [`ARCHITECTURE.md`](ARCHITECTURE.md) | Source-verified architecture (+ [`docs/ARCHITECTURE_DETAIL.md`](docs/ARCHITECTURE_DETAIL.md) for citations) |
| [`CHANGELOG.md`](CHANGELOG.md) | Version history (a narrative, not a version-order record) |
| [`docs/RELEASING.md`](docs/RELEASING.md) | Cut a release; how the download site populates itself |
| [`CONTRIBUTING.md`](CONTRIBUTING.md) | Source build and tests |

Archived, superseded material is in [`docs/archive/`](docs/archive/).
