# Context Handoff — fuzzyband / MetalAccompaniment

**Read this first.** It is the single self-contained briefing for an engineer or
model picking up this codebase cold. It states what the project is, what is
actually built, what is verified, what is broken, and what must not be touched.

**Audited at:** commit `d1fc62a` ("Mirroring choices fix"), 2026-09-14,
`CMakeLists.txt` `VERSION 1.0.3`. Working tree clean. 326 commits,
2026-04-16 → 2026-09-14.

**Companion docs (canonical set):**

| Doc | Use it for |
|---|---|
| [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md) | Before changing anything: traps, contracts, pre-flight checklist |
| [`BASS_MIRRORING.md`](BASS_MIRRORING.md) | The hard active bug, full history, ranked hypotheses, debug recipe |
| [`PROJECT_TIMELINE.md`](PROJECT_TIMELINE.md) | Every era, what failed, what was reverted, what was retried |
| [`TEST_AUDIT.md`](TEST_AUDIT.md) | What the suite covers, what it misses |
| [`DOCS_INDEX.md`](DOCS_INDEX.md) | Which docs are current, which are archived |

---

## 1. What this project is

A **JUCE 8 VST3/AU audio plugin for macOS**. It listens to a guitarist's live
audio input and emits **drum (MIDI ch. 10) and bass (MIDI ch. 2) MIDI** in real
time, driving a drum sampler and a bass instrument in the DAW. Metal/rock
oriented; drop-C and palm-mute are the design centre.

Two inference worlds have existed. The **rule-based** path is what actually
ships and drives everything. An **ONNX** path was built extensively and mostly
retired; treat ONNX docs with suspicion (§5).

Modes: **Play** (the plugin drives a song form) and **Record/Riff** (it learns a
riff you play and then accompanies you with it).

---

## 2. Build, test, install

```bash
# Requirements: CMake 3.22+, C++20, Git, ONNX Runtime (brew install onnxruntime)

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

./scripts/install-plugin-to-user.sh --build build --config Release   # copies VST3+AU to ~/Library/Audio/Plug-Ins/
ctest --test-dir build --output-on-failure --config Release

# Direct binaries (what the release gate actually is):
./build/MetalAccompanimentTests             # unit
./build/MetalAccompanimentIntegrationTests  # integration
```

**Measured at v1.0.3 on this machine:** unit **269 cases / 87 984 assertions**,
integration **79 cases / 1 251 assertions**, **all passing**, no `[!mayfail]`.
Perf: **mean 0.95 ms, p99 1.44 ms** per 256-sample block against a **1.5 ms p99
budget** (T9.5). The changelog's claimed 0.50/0.81 ms is not reproducible here —
re-measure before adding audio-thread work.

**Version-bump rule (`AGENTS.md:276`):** every debug/test build must bump the
patch version in `CMakeLists.txt:4` *before* building, because the version string
is the only way to confirm which binary is loaded in the DAW. *(Note:
`AGENTS.md:279` still says "Current: 0.9.16" — stale; the real value is in
`CMakeLists.txt`.)*

Verify what is actually installed:

```bash
strings -a ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
  | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -u
shasum ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
       build/MetalAccompaniment_artefacts/Release/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband
```

---

## 3. The architecture that actually exists

**Three threads. There is no capture thread.**

| Thread | Entry | Does |
|---|---|---|
| **Audio** | `AccompanimentProcessor::processBlock` (`AccompanimentProcessor.cpp:724`) | All DSP **including the mel spectrogram**: ~40× 2048-pt FFTs + 64×32×1025 filterbank in one callback, every 22050 samples (`:764-773`). Dominant cost. |
| **Background inference** | `inferenceLoop` (`:662-679`), 20 ms poll, holds `inferenceDrainMutex` (never taken by audio) | Drains `featureQueue` + `melQueue`. The **only** place `Ort::Session::Run` executes (`:494`, `:530`). Drain ~50 Hz; CNN inference ~2 Hz. |
| **Message/UI** | editor `Timer`, 20 Hz (`:547`) | Readouts, scope, controls. |

Handoff is atomics + three moodycamel queues (`featureQueue`, `melQueue`,
`grooveCommitQueue`) + a 3-slot riff triple buffer. Audio thread never blocks and
never allocates (buffers pre-sized in `prepareToPlay`).

**Engine phases** (`AccompanimentProcessor.h:316-320`) — nine:
`Idle, PlayCountIn, PlaySection, RecWaitBar, RecCountIn, RecCapture, RiffA,
RiffBListen, RiffBLocked`, plus `PostLockPhase{Idle, TransitionHold}`,
`RiffCapturePhase`, and a derived 4-value `SectionPhase` for the UI.

**Bass mirroring call chain (one line, audio thread):**
clip (`:735`) → `EnergyAnalyser` 20 ms onset RMS (`:759`) → `PitchEstimator` YIN
(`:760`) → `StablePitchTracker` pitch class (`:1229`) → `PhraseLearner::process`
→ `detectAttack` (`PhraseLearner.cpp:584`) → `BassNote.trigger` (`:691-696`) →
`triggerLearnedBassNote` (`:1939` → `PatternPlayer.cpp:151`) → `emitBassNote`
ch. 2 (`:1543`/`:1035`) → `mirrorVoiceEndSample_` (`:1546`), which suppresses the
grid bass in `emitBassRange`/`emitPatternBass`/`emitHarmonicBass`
(`:1561-1580`, `:1131`, `:1227`). Full detail in
[`BASS_MIRRORING.md`](BASS_MIRRORING.md).

**Structure state is three-valued:** `{SILENT, SOFT, LOUD}`
(`StructureTagger.h:14-19`). The spectral centroid argument to it is unused
(`StructureTagger.cpp:11`).

---

## 4. The active bug

**Symptom (user-reported):** bass does not mirror the riff; it sounds like a
root/harmony line. It "used to work a lot better".

**Status:** unresolved at v1.0.3. This is the **ninth** time the mirror has been
"fixed" (0.8.4, 0.9.8, 0.9.10, 0.9.11, 0.9.12, 0.9.13, 0.9.59, 1.0.1, 1.0.3).

**Two candidate mechanism classes — test them separately:**

- **Mode A — attack-detector starvation.** `detectAttack` (`PhraseLearner.cpp:53-93`)
  requires a fall within `kFallWindowBlocks`, a sharp rise, **and** a rise clearing
  the trough by `×1.15 + 0.005`. That absolute term was added in 1.0.3 and tuned
  on a *clean synthetic* envelope. On distorted palm-mute it may never be reached.
- **Mode B — path arbitration.** Something else owns the monophonic bass voice.
  Check `mirrorVoiceEndSample_`, `beatGridBassEnabled_`, and the learner's
  `Locked` state.

A strong secondary suspect: `kFallWindowBlocks = 20` is counted in **blocks**, so
the decay-recency window is 53 ms at a 128-sample buffer and 853 ms at 2048. No
test covers the analysis layer at any buffer size other than 512.

**Highest-value first action:** add per-note-on provenance tagging (mirror /
grid-authored / grid-harmonic / frozen) and replay
`tests/fixtures/palm_mute_chug.wav` through the **real** `EnergyAnalyser` +
`PhraseLearner` at 128 / 256 / 512 / 1024 / 2048, logging which `detectAttack`
predicate term fails per block. See [`BASS_MIRRORING.md`](BASS_MIRRORING.md) §5–6.

**Do not re-chase the stale-binary theory.** Verified 2026-09-14: installed VST3,
installed AU and build artefacts are byte-identical at v1.0.3
(`aed3e455e0d3341af63bf83a566498a2970bed46`).

---

## 5. Things the documentation claims that are NOT true

An agent that trusts the existing docs will waste hours. Verified contradictions:

| Claim | Where | Reality |
|---|---|---|
| `OnsetDetector`, `TempoStabiliser`, `OnnxInference` exist | `AGENTS.md:190,200,220`, `CLAUDE.md` | **Do not exist in `src/`** |
| `StructureState` is VERSE/CHORUS/BREAKDOWN | `ARCHITECTURE.md:154` | `{SILENT, SOFT, LOUD}` |
| `FeatureCapture` captures at runtime in the plugin | `FEATURE_CAPTURE.md`, `RUNTIME_ARCHITECTURE.md` | Compiled **only into the test binary** (`CMakeLists.txt:308`) |
| Bass ONNX / structure ONNX are live | `BASS_ONNX_IO.md`, `ONNX_IO.md` | No live path |
| Tier-1 `GrooveRenderer` groove model is live | `TIER1_GROOVE_MODEL_CONTRACT.md` | Ships a 1.45 MB model, **never instantiated** (only its own source + a `PatternPlayer.h:223` comment) |
| ONNX defaults **off** | `RUNTIME_ARCHITECTURE.md:223` | `MA_ENABLE_ONNX` defaults **ON** (`CMakeLists.txt:16`) |
| `GrooveCommit` carries inference decisions to the audio thread | `RUNTIME_ARCHITECTURE.md` | Enqueued (`:624`), then **drained and discarded** (`:1205-1209`) — intentional (T4.1) |
| Plugin is v0.9.29 | `README.md:1` | 1.0.3 |
| Milestone status in `.gsd/STATE.md` | `CHANGELOG.md:3` | `.gsd/` is stale and untracked; `.planning/` is authoritative |
| `BeatTracker` | `RUNTIME_ARCHITECTURE.md` | Deleted in `58c3dde` |
| Project is a "sludge metal" pivot, M002–M005 | `.gsd/PROJECT.md` | Abandoned direction; docs referenced (`SLUDGE_METAL_DOMAIN_AUDIT.md`) do not exist |

**Rule of thumb: trust `src/`, `CMakeLists.txt`, `tests/` and `git log`. Treat
every other document as a hypothesis to verify.**

---

## 6. Invariants — do not break these

Full list with evidence in [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md) §3.
The short version:

1. Audio thread never blocks, never allocates.
2. `Ort::Session::Run` only on the background inference thread.
3. The mirror owns the monophonic bass voice while it rings; the grid is a
   gap-filler.
4. Harmony resumes when the guitarist stops.
5. Rendered MIDI is buffer-size invariant (128 / 512 / 2048).
6. Play-mode rotation is authoritative; `GrooveCommit` is deliberately discarded.
7. Cymbal/drum note-offs ring for their musical gate; deferred across blocks.
8. Humanisation is deterministic across bounces and block sizes.
9. Transport drives the drum grid; the monotonic clock drives lock schedules.
   Never merge them.
10. Every fix lands with a test; never weaken an assertion or add `[!mayfail]`.

---

## 7. Working agreements

- **Version bump before every build** (`CMakeLists.txt:4`) — it is the only way
  to know which binary the DAW loaded.
- **Test with real audio.** `tests/fixtures/*.wav` are real excerpts from
  `data/raw/` (the full multi-minute takes are present). The mirror regressed nine
  times largely because it was validated against synthetic envelopes. The one fix
  that worked (0.9.11) was developed against the real recordings.
- **Suspect buffer-size dependence by default.** Windows measured in *blocks* are
  a recurring defect class in this repo.
- **`[!mayfail]` is not permitted.** Phase 0 found three tests pinning regressions
  as correct; the suite now reports zero.
- **`.planning/` is gitignored** (`.gitignore:27`) and only ~149 of 417 files are
  force-added. Anything you write there may not survive a clone. Put durable
  documentation in `docs/`.
- **Do not park verified fixes on worktree branches.** Phase 31's review fixes
  landed only on `origin/claude/wonderful-payne-cfe667` and had to be redone.

---

## 8. Where to start on any new bug

1. Read [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md) §1 so you do not
   act on a stale document.
2. Check whether the subsystem is actually wired (§5 above) before "fixing" it.
3. Reproduce with a **test that fails first**, ideally using real audio at more
   than one buffer size.
4. Check the invariant list before choosing a fix.
5. Land the fix with its test and a version bump.
