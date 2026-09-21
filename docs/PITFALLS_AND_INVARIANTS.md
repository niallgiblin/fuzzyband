# Pitfalls and Invariants

**Purpose.** This is the document to read *before* changing anything, and the one
to hand to a coding agent so it does not re-derive the same wrong conclusions.
It records (a) the traps that keep catching this project, and (b) the behaviours
that work and must not be broken.

**Audited at:** v1.0.3, commit `d1fc62a`, 2026-09-14.
**Companion docs:** [`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md),
[`BASS_MIRRORING.md`](BASS_MIRRORING.md), [`TEST_AUDIT.md`](TEST_AUDIT.md),
[`PROJECT_TIMELINE.md`](PROJECT_TIMELINE.md).

---

## Part 1 — Why coding agents keep going wrong

This is the "cross-wiring" problem, and it is real, mechanical, and fixable.

### 1.1 There are two planning trees and they contradict each other

| Tree | State | Tracked? | Says the active milestone is |
|---|---|---|---|
| `.planning/` | **live** — last activity 2026-09-09, 410 md files | yes (git) | M001 Creative Companion, 82 % |
| `.gsd/` | **stale** — symlink to `~/.gsd/projects/002bd9a3f840`, untouched since 21 Aug, and **itself untracked** (`.gitignore:57`) | no | M002 Sludge Metal, "Phase: blocked" |

`.gsd/STATE.md` claims the project is blocked on a milestone that the live tree
finished months ago. Anything reading `.gsd/` gets a picture that is not merely
outdated but *actively wrong*. `.gsd/PROJECT.md` also references
`SLUDGE_METAL_DOMAIN_AUDIT.md`, which does not exist anywhere in the repo.

**Rule: `.planning/` is the only planning authority. Never read `.gsd/`.**

### 1.2 `AGENTS.md` and `CLAUDE.md` are generated, duplicated, and describe code that does not exist

Both are 304-line auto-generated files (`<!-- GSD:...-start source:PROJECT.md -->`,
etc.) with a managed block. They differ in only four places — one of which is the
architecture summary: `CLAUDE.md` is *missing* `StablePitchTracker`,
`TempoStabiliser`, pitch root/confidence, intensity and RMS delta that `AGENTS.md`
has. Two files, one staler, both authoritative-looking: exactly the cross-wiring
pattern.

Worse, the shared architecture section is not accurate. Verified absent from
`src/`:

| Named in `AGENTS.md` / `CLAUDE.md` | Reality |
|---|---|
| `OnsetDetector` (`src/analysis/OnsetDetector.h/.cpp`, line 220) | **does not exist** — no such file or symbol |
| `TempoStabiliser` (line 190) | **does not exist** |
| `OnnxInference` stub (line 200) | **does not exist** — the real classes are `MetalGrooveInference` and `RuleBasedInference` |

`AGENTS.md:190` also claims `src/analysis/` contains `OnsetDetector`. It does not.
Any agent that trusts this file will search for phantom symbols.

**Rule: the architecture summary in `AGENTS.md`/`CLAUDE.md` is untrustworthy.
Read [`ARCHITECTURE.md`](../ARCHITECTURE.md) and the source.**

### 1.3 `CHANGELOG.md` points readers at the dead tree

`CHANGELOG.md:3` directs the reader to `.gsd/STATE.md` and `.gsd/ROADMAP.md` for
milestone status. Both are stale and untracked. This is the single most
load-bearing dead link in the repo.

### 1.4 577 project markdown files, 173 of them vendored tooling

| Area | `.md` count | What it is |
|---|---|---|
| `.planning/` | 410 | live planning history (plans, summaries, research, debug logs) |
| `.cursor/get-shit-done/` | 173 | **vendored third-party agent framework — not project docs** |
| `docs/` | 24 | project documentation |
| `.MDignore/` | 13 | deliberately parked, mostly pre-2026 planning |
| `training/` | 7 | ML pipeline docs |
| root | 6 | `README`, `ARCHITECTURE`, `CHANGELOG`, `AGENTS`, `CLAUDE`, `CONTRIBUTING` |

A `grep` for a symbol across `*.md` returns mostly vendored framework text.
`find . -name "*.md"` is not a usable way to find project documentation here.

### 1.5 Docs describe subsystems that are not in the shipping plugin

Verified against `CMakeLists.txt` and the live call path:

| Documented as live | Reality |
|---|---|
| `FeatureCapture` runtime capture | Compiled **only into the test binary** (`CMakeLists.txt:308`) — not in the plugin |
| Bass ONNX (`BASS_ONNX_IO.md`) | No live bass ONNX path |
| Structure ONNX | No live structure ONNX path |
| `GrooveRenderer` / Tier-1 groove model | Ships a 1.45 MB model, is **never instantiated** in the live path (only its own source and a `PatternPlayer.h:223` comment reference it) |
| `BeatTracker` | does not exist |
| `GrooveCommit` inference handoff | Enqueued at `AccompanimentProcessor.cpp:624`, then **drained and discarded** at `:1205-1209` (`while (...try_dequeue(commit)) gotCommit = true; (void)gotCommit;`) |

The `GrooveCommit` case is the most dangerous: an agent can "fix" the producer
side, or add a consumer, without realising the consumer deliberately throws the
value away as part of the T4.1 rotation decision.

### 1.6 Version numbers and defaults in docs are stale

`README.md` advertises **v0.9.29**; the project is **1.0.3** (`CMakeLists.txt:4`).
`RUNTIME_ARCHITECTURE.md:223` says ONNX defaults off; `CMakeLists.txt:16` says
`MA_ENABLE_ONNX ... ON`. `ARCHITECTURE.md:154` describes a
`StructureState` of VERSE/CHORUS/BREAKDOWN; the real enum is
`{SILENT, SOFT, LOUD}` (`StructureTagger.h:14-19`) and the centroid argument is
unused (`StructureTagger.cpp:11`).

---

## Part 2 — Recurring engineering pitfalls

### 2.1 Block-count vs time — the buffer-size trap

**The single most recurring latent defect class.** Constants expressed in *blocks*
silently change meaning at every host buffer size.

Live instances (`PhraseLearner.h`):

| Constant | Declared | Comment claims | Actual |
|---|---|---|---|
| `kFallWindowBlocks` | `20` blocks | "~200 ms at 512/48k" | 213 ms @512 → **853 ms @2048** |
| `kSilenceResetBlocks` | `200` blocks | "~4 seconds at 512 samples/block" | **2.13 s** @512/48k — comment wrong ~2×; 8.5 s @2048 |
| `kMinAttackIntervalSamples` | `2000` samples | "~40 ms" | correct — buffer-independent |

The project already learned this lesson once: 1.0.1 fixed the onset-capture tail
window being "measured over the last quarter of the *block*, not of the *16th*",
which produced 9 / 5 / 1 onsets at 128 / 512 / 2048. **The same class of bug is
still present in the attack detector.**

**Rule: any window that describes musical time must be derived from
`sampleRate` in `prepare()`, never counted in blocks.** Add a buffer-size sweep
test whenever you touch a time window.

**The same trap in its second form: the detector's *update rate*.** A streaming
detector called once per `processBlock` samples the signal at the **host block
rate**, so even a time-based window behaves differently at every buffer size. In
1.0.8 the attack detector was decoupled from the host block: `EnergyAnalyser`
records the onset envelope at a fixed ~10.7 ms hop and the processor drives
`PhraseLearner` per hop. Before that, mirror density fell 370 → 78 notes/30 s
from a 64- to a 4096-sample block. **When you add a streaming detector, decouple
its update rate from the host block and assert invariance across 64 → 4096.**

### 2.2 The mirror/grid arbitration is a race by construction

`PatternPlayer` has **one monophonic bass voice** and three producers
(mirror, authored/harmonic grid, frozen riff). Ownership is arbitrated by
`mirrorVoiceEndSample_`. Every historical "bass doesn't mirror" symptom is one of
the producers winning: the lock (0.9.12–0.9.13), the grid line (1.0.3), or a
removed API (`mirrorWhileHeld_`). See [`BASS_MIRRORING.md`](BASS_MIRRORING.md).

### 2.3 Public API removed while docs still describe it

`setMirrorWhileHeld` / `mirrorWhileHeld_` / `releaseForTransition` were declared
stubs and removed (0.9.59, then `9d7ec26`), and the Phase 8 dead-code sweep lists
them for deletion. `grep` finds no trace. Yet `CHANGELOG` and older docs still
reference them, and the *contract* they encoded (mirror while held) is still
argued about in code comments.

### 2.4 Tests that reimplement the engine

`tests/test_golden_signal.cpp:105-133` defines its own `RmsWindow`, copying
`EnergyAnalyser`'s 0.02 s ×4 clamped window. The 1.0.3 change had to update the
*copy* as well as the engine, and `tests/fixtures/README.md` still documents the
older 0.1 s window. A duplicated engine in a test is a test that can pass while
the engine is broken.

### 2.5 Synthetic fixtures that do not resemble the user's signal

The mirror integration test (`tests/test_processor_pipeline.cpp:2478`) feeds a
clean sine with an exponential pluck envelope. Real input is distorted,
palm-muted, drop-C guitar, where the inter-note trough is shallow. The one fix
that actually worked (0.9.11) was the one developed against `data/raw/`
recordings. **Prefer real-take fixtures** (`tests/fixtures/*.wav`, sourced from
`data/raw/`).

### 2.6 Tests that pin a regression as correct

Phase 0 found three tests asserting the buggy behaviour as expected
(`test_e2e_groove_variety.cpp:147`, `test_pattern_player.cpp:453-456`,
`test_processor_pipeline.cpp:1138`). The project's own rule — "never weaken an
assertion to make a fix pass" — exists because this happened. Current suites
report **zero `[!mayfail]`**; keep it that way.

### 2.7 Stale build artefacts masquerading as the fix

Not currently a problem — on 2026-09-14 the installed VST3, installed AU and
build artefacts were **byte-identical at v1.0.3**:

```
aed3e455e0d3341af63bf83a566498a2970bed46  ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/.../fuzzyband
aed3e455e0d3341af63bf83a566498a2970bed46  build/.../Release/VST3/fuzzyband.vst3/.../fuzzyband
```

But `docs/IMPLEMENTATION_PLAN.md` T0.1 records the repo shipping a `.vst3`
containing only `Info.plist`, and the installed plugin being v0.9.62 against
v0.9.67 source. Re-run the hash check before believing any "it's fixed but you
still hear the bug" report.

### 2.8 Real-time-safety hazards documented nowhere

- **The mel spectrogram runs on the audio thread.** `AccompanimentProcessor.cpp:764-773`
  calls `melExtractor.process()` — ~40× 2048-point FFTs plus a 64×32×1025
  filterbank — inside `processBlock`, every 22050 samples. This is the dominant
  per-block cost and the reason the perf budget is tight.
- **`std::atomic_load` on a `shared_ptr` on the audio thread** is not lock-free
  in libc++.
- Process-wide `Ort::Env` statics; `scopeSamples` / `StructureSequencer` read
  cross-thread without atomics.

### 2.9 Performance headroom is thinner than the changelog claims

Measured on this machine at v1.0.3: **mean 0.95 ms, p99 1.44 ms** per 256-sample
block, against a **1.5 ms p99 release budget** (T9.5). The 1.0.3 changelog claims
"mean 0.50 ms, p99 0.81 ms". Do not assume the margin described in the changelog
is real; re-measure before adding work to the audio thread.

---

## Part 3 — Invariants: what works and must not be broken

Treat these as contracts. Each is load-bearing and each has burned the project
before.

1. **The audio thread never blocks and never allocates.** Buffers are pre-sized
   in `prepareToPlay`; cross-thread handoff is atomics + moodycamel queues + the
   3-slot riff triple buffer. `inferenceDrainMutex` is taken only by the
   inference thread, never by audio.
2. **ONNX `Ort::Session::Run` happens only on the background inference thread.**
3. **The mirror owns the monophonic bass voice while it rings.** The grid line is
   a gap-filler (`emitBassRange` / `emitPatternBass` / `emitHarmonicBass` all
   honour `suppressBeforeAbs`). This is the 1.0.3 contract and it is covered by
   `test_processor_pipeline.cpp:2478` and `:2597`.
4. **Harmony resumes when the guitarist stops.** Silence must never leave the
   bass muted (same two tests).
5. **Rendered MIDI is buffer-size invariant.** Absolute event samples must match
   at 128 / 512 / 2048. Guarded by `tests/test_midi_probe.cpp` and
   `tests/test_phase1_rendering.cpp`. Any new time-derived window must preserve
   this.
6. **Play-mode rotation is authoritative over the inference argmax.**
   `GrooveCommit` is deliberately discarded on the audio thread
   (`AccompanimentProcessor.cpp:1205-1209`). This is intentional — do not "fix"
   it by honouring the commit.
7. **Drum/china/crash/open-hat note-offs ring for their musical gate** (≥1 beat),
   deferred across blocks and flushed on seek/silence/bypass. Ungating them
   chokes every cymbal to 3–46 ms.
8. **Humanisation is deterministic.** The same take must render identically
   across bounces and block sizes (per-event draws keyed by
   `(bar, grid16, voice, salt)`, not a shared RNG).
9. **`structureSequencer` / `SectionPhase` drives the song form**; the drum grid
   follows the transport, the lock schedules follow the monotonic clock. Do not
   merge the two clocks.
10. **Every fix lands with a test, and no assertion is weakened to make a fix
    pass.** No `[!mayfail]` in a green suite.

---

## Part 4 — Pre-flight checklist for any change

Before making a change, answer all of these:

- [ ] Am I reading `.planning/`, and not `.gsd/`?
- [ ] Have I checked whether the thing I am about to "fix" is documented but
      **not actually wired** (FeatureCapture, bass/structure ONNX, GrooveRenderer,
      GrooveCommit)?
- [ ] Does my change add or touch a window measured in **blocks**? If so, convert
      it to samples in `prepare()`.
- [ ] Does my change affect rendered MIDI? Then prove invariance at
      **128 / 512 / 2048**.
- [ ] Does my change touch the bass voice? Then check **all four** producers
      (mirror, grid-authored, grid-harmonic, frozen) and the
      `mirrorVoiceEndSample_` gate.
- [ ] Am I testing with **real audio** (`tests/fixtures/*.wav` from
      `data/raw/`), or only with a synthetic envelope? Synthetic-only is how the
      mirror regressed nine times.
- [ ] Am I adding work to the **audio thread**? p99 headroom is ~0.06 ms against
      the 1.5 ms budget.
- [ ] Did I bump the version in `CMakeLists.txt:4` **before** building, and does
      the installed binary hash match the build?
- [ ] Am I about to weaken or `[!mayfail]` an assertion? Do not.
