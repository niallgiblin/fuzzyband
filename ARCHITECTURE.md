# Architecture — fuzzyband (MetalAccompaniment)

**Verified against source at v1.0.3 (`5d5f410`).** Every statement here was read
out of `src/`, not out of the older docs. Where the previous version of this file
was wrong, the correction is noted.

- **Evidence base with `file:line` citations for everything below:**
  [`docs/ARCHITECTURE_DETAIL.md`](docs/ARCHITECTURE_DETAIL.md).
- **Do not trust** the archived `docs/archive/ARCHITECTURE-pre-1.0.3.md` or
  `docs/RUNTIME_ARCHITECTURE.md` — see
  [`docs/PITFALLS_AND_INVARIANTS.md`](docs/PITFALLS_AND_INVARIANTS.md) §1.

---

## 1. What it is

A JUCE 8 VST3/AU plugin (macOS-first) that takes a live guitar input and emits
**drum MIDI on channel 10** and **bass MIDI on channel 2**. Bundle
`com.ng.fuzzyband`, product `fuzzyband`, `NEEDS_MIDI_OUTPUT TRUE`,
`NEEDS_MIDI_INPUT FALSE`. VST3 always; AU + Standalone on Apple.

---

## 2. Thread model

**Three threads. There is no capture thread.**

| Thread | Entry | Responsibility |
|---|---|---|
| **Audio** | `AccompanimentProcessor::processBlock` (`AccompanimentProcessor.cpp:724`) | **All DSP**, including the mel spectrogram. Emits all MIDI. Never blocks; never allocates on the audio thread. |
| **Background inference** | `inferenceLoop` (`:662-679`), 20 ms poll, holds `inferenceDrainMutex` (never taken by audio) | Drains `featureQueue` + `melQueue`; the **only** place `Ort::Session::Run` runs (`:494`, `:530`). |
| **Message/UI** | editor `Timer`, 20 Hz (`:547`) | Readouts, scope, controls. |

**Handoff primitives (the real ones):** atomics; three `moodycamel` queues —
`featureQueue`, `melQueue`, `grooveCommitQueue`; and a 3-slot riff triple buffer.

**Real-time hazard, previously undocumented:** the **mel spectrogram is computed
on the audio thread** — `audioRingBuffer.isWindowReady()` → `melExtractor.process()`
(`:764-773`), i.e. ~40 × 2048-point FFTs plus the 64×32×1025 filterbank, once per
22050 samples. This is the dominant per-block cost and the reason p99 headroom is
thin (measured 1.44 ms against a 1.5 ms budget).

---

## 3. One audio block, in order

Full 43-step table with line numbers in
[`docs/ARCHITECTURE_DETAIL.md`](docs/ARCHITECTURE_DETAIL.md) §2. The shape:

1. Scrub non-finite input, clip in place (`:735-740`).
2. `energyAnalyser.process` — RMS (0.1 s), onset RMS (0.02 s), centroid, HF flux,
   sub-bass ratio, peak RMS (`:759`).
3. `pitchEstimator.process` — YIN (`:760`); `audioRingBuffer.write` (`:761`).
4. Mel window → `melQueue` (`:764-773`) — **on the audio thread**.
5. `structureTagger.update(...)` → `{SILENT, SOFT, LOUD}` (`:793-794`).
6. BPM resolution, host-authoritative (`:803-823`); `clockSample` in the transport
   frame (`:831`).
7. Song form, playback gate, `FeatureVector` → `featureQueue` (`:869-893`).
8. Play / Record / Riff phase machine and pattern ownership (`:928-1209`).
9. `patternPlayer.process(midi, ...)` (`:1943-1944`) — emits **all** drum and bass
   MIDI.
10. Gain passthrough (`:1947-1955`); `hostSampleTime += numSamples` (`:1957`);
    display atomics and section progress (`:1979-2030`).

The engine core (`:1215-1941`) is the largest and most stateful region: stable-pitch
update, loop-wrap re-anchor, riff capture, generative groove lock, post-lock
transition hold, `emitFrozenRiff`, and the live mirror trigger.

**`PatternPlayer::process`** (`PatternPlayer.cpp:1279-1595`) resolves the
frozen-transport clock, detects seek/loop and flushes note-offs, handles silence
and the click track, resolves a bar-quantised (or beat-quantised) pattern commit,
emits crashes/fills/micro-fills, the bass lead-in pickup, the learned/mirrored
bass notes, then the grid bass gated on `mirrorVoiceEndSample_`, and finally
flushes due drum note-offs.

---

## 4. Engine phases — four nested state variables, not one enum

**`EnginePhase`** (`AccompanimentProcessor.h:316-320`) — nine values:

```cpp
Idle, PlayCountIn, PlaySection,
RecWaitBar, RecCountIn, RecCapture,
RiffA, RiffBListen, RiffBLocked
```

Plus two sub-machines and one derived projection:

| Variable | Values | Role |
|---|---|---|
| `RiffCapturePhase` (`h:395`) | `Idle, WaitBar, CountIn, Recording` | Record-riff capture |
| `PostLockPhase` (`h:408`) | `Idle, TransitionHold` | Post-lock contrast section |
| `SectionPhase` (`h:176`) | `Idle=0, Play=1, Lock=2, Transition=3` | **Purely derived** for the UI |

Every transition with its real condition is tabulated in
[`docs/ARCHITECTURE_DETAIL.md`](docs/ARCHITECTURE_DETAIL.md) §3.2. Key ones:

- `Idle → PlayCountIn` on the Play rising edge; `PlayCountIn → PlaySection` after a
  bar-aligned 4-beat click.
- `RecCapture → RiffA` when `commitGridCapture()` succeeds with ≥2 occupied slots.
- `RiffA → (armed) → RiffBListen` on lock expiry; always via the transition cycle,
  which **always re-enters `RiffA`** — never back to follow mode.
- `RiffBListen → RiffBLocked` when the learner locks; back on drift unlock.

**Pattern ownership** (`ARCHITECTURE_DETAIL.md` §3.5): `PlaySection` uses the audio
thread's section pool rotation; `RiffBListen` the transition pool; `RiffBLocked`
and `RiffA` frozen snapshots; Idle/follow uses the inference thread's
`latestPatternIndex`.

**Derived flags** (`:1677-1684`): `isGrooveLocked()` means "engine is in `RiffA`".
`RiffBListen`/`RiffBLocked` are **not** reported as locked by that getter, though
they are `riffLoopActive`.

---

## 5. Bass: one monophonic voice, three producers

This is the project's most regression-prone area — see
[`docs/BASS_MIRRORING.md`](docs/BASS_MIRRORING.md).

| Producer | Where | Active when |
|---|---|---|
| **Mirror** (live) | `AccompanimentProcessor.cpp:1920-1940` → `PatternPlayer::triggerLearnedBassNote` → consumed `PatternPlayer.cpp:1522-1550` | `PlaySection` or `RiffBListen`, and `BassNote.trigger` |
| **Grid** (authored / harmonic) | `PatternPlayer.cpp:1561-1580` → `emitBassRange` → `emitPatternBass` (`:1100`) / `emitHarmonicBass` (`:1160`) | `beatGridBassEnabled_` |
| **Frozen riff** | `AccompanimentProcessor.cpp:1910-1918` → `emitFrozenRiff` | `RiffA` / `RiffBLocked` |

Ownership is arbitrated by `mirrorVoiceEndSample_` (`PatternPlayer.h:436-440`):
when the mirror emits, it claims the voice until the note ends, and the grid
producers skip every event before that sample (`:1131`, `:1227`). Cleared by
`flushAllPendingNoteOffs` (`:534-544`) on seek/silence/bypass, and by `reset()`.

**Attack chain:** clip → `EnergyAnalyser` 20 ms onset RMS → `PitchEstimator` YIN →
`StablePitchTracker` pitch class → `PhraseLearner::process` →
`detectAttack` (`PhraseLearner.cpp:53-93`) → `BassNote.trigger` (`:691-696`).

**Structure state** is three-valued — `{SILENT, SOFT, LOUD}`
(`StructureTagger.h:14-19`). The previous version of this file claimed
VERSE/CHORUS/BREAKDOWN with centroid thresholds; that was wrong, and the centroid
argument is in fact unused (`StructureTagger.cpp:11`).

---

## 6. Subsystems that are documented but not wired

Do not "fix" these without deciding first whether they are meant to be live:

| Subsystem | Status |
|---|---|
| `FeatureCapture` | Compiled **only into the test binary** (`CMakeLists.txt:308`); not in the plugin |
| Bass ONNX | No live path |
| Structure ONNX | No live path |
| `GrooveRenderer` (Tier-1) | Fully implemented, ships a 1.45 MB model, **never instantiated** |
| `grooveCommitQueue` (inference → audio) | Enqueued (`:624`), then **drained and discarded** (`:1205-1209`) — intentional, T4.1 |
| `snapBpm()`, `consumeTransportJumped()` | Unreachable |
| `OnsetDetector`, `TempoStabiliser`, `OnnxInference`, `BeatTracker` | **Do not exist in `src/`** (named in the generated agent profiles) |

---

## 7. Hidden state and hazards

- Process-wide `Ort::Env` statics.
- `std::atomic_load` on a `shared_ptr` from the audio thread — not lock-free in libc++.
- Non-atomic audio→UI data behind only a relaxed index (`scopeSamples`,
  `StructureSequencer` reads).
- Two clocks, implemented twice: transport frame for the drum grid, monotonic for
  lock schedules. Do not merge them.

Full list with citations: [`docs/ARCHITECTURE_DETAIL.md`](docs/ARCHITECTURE_DETAIL.md) §8.
