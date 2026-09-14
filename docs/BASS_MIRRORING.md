# Bass Mirroring — Contract, Implementation, and the Regression Cycle

**Status:** the live bass mirror is the most repeatedly-regressed feature in the
project. It has been "fixed" at **0.8.4, 0.9.8, 0.9.10, 0.9.11, 0.9.12, 0.9.13,
0.9.59, 1.0.1 and 1.0.3** — at least nine times — and the user still reports that bass
does not mirror.
**Audited at:** v1.0.3, commit `5d5f410` ("Mirroring choices fix"), 2026-09-14.
All `file:line` citations below are valid at that commit.
**Companion docs:** [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md),
[`TEST_AUDIT.md`](TEST_AUDIT.md), [`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md).

---

## 1. The user-facing contract

> "The bass plays what I am playing, as I play it."

Concretely, in **Play** mode and **RiffBListen**, while the guitarist is audible:

1. Every detected guitar attack produces **one bass note** at the played pitch
   class, promptly (within ~30 ms).
2. The authored/harmonic **grid line must not layer underneath it**. The bass
   voice is monophonic, so a grid root/fifth line retriggers and cuts the
   mirrored note — the user then hears root/fifth instead of a mirror.
3. When the guitarist **stops**, the harmony/grid line is the fallback. Bass must
   not simply go silent.

This contract was written down in 1.0.3 and is the correct one. Point 2 is the
subtle one and is the source of most of the churn.

---

## 2. Current implementation — exact call chain

There are **two** bass producers that fight over one monophonic voice:

| Producer | Code | When it runs |
|---|---|---|
| **Mirror** (live) | `AccompanimentProcessor.cpp:1920-1940` → `PatternPlayer::triggerLearnedBassNote` → consumed at `PatternPlayer.cpp:1522-1550` | `enginePhase == PlaySection \|\| RiffBListen` and `bassNote.trigger` |
| **Grid** (authored / harmonic) | `PatternPlayer.cpp:1561-1580` → `emitBassRange` → `emitPatternBass` (`:1100-1158`) / `emitHarmonicBass` (`:1160-1238`) | `beatGridBassEnabled_` is true |
| **Frozen riff** | `AccompanimentProcessor.cpp:1910-1918` → `emitFrozenRiff` | `enginePhase == RiffA \|\| RiffBLocked` (grid bass off) |

### Attack → note

```
audio in
  └─ EnergyAnalyser::process                        EnergyAnalyser.cpp:105-157
       ├─ rmsEnergy      = 0.1 s window × 4, clamp   EnergyAnalyser.cpp:23, :141
       └─ onsetRmsEnergy = 0.02 s window × 4, clamp  EnergyAnalyser.cpp:31, :155
  └─ AccompanimentProcessor.cpp:1529   onsetRms = getOnsetRmsEnergy()
  └─ PhraseLearner::process(..., onsetRms, ...)      PhraseLearner.cpp:530+
       ├─ detectAttack(rms)                          PhraseLearner.cpp:53-93
       ├─ risePending_ / canAttack                   PhraseLearner.cpp:583-587
       └─ if (attack) result.trigger = true          PhraseLearner.cpp:691-696
  └─ AccompanimentProcessor.cpp:1906   shouldTrigger = trigger && !guitarStopped
  └─ AccompanimentProcessor.cpp:1939   triggerLearnedBassNote(note, vel, offset, dur)
  └─ PatternPlayer.cpp:1522-1550       emitBassNote(...) + stamp mirrorVoiceEndSample_
```

### `detectAttack` — the exact predicate (`PhraseLearner.cpp:53-93`)

```cpp
const bool fell = (rms < prevRms);                 // ANY decrease counts
if (fell)           { fallCounter_ = kFallWindowBlocks; rmsFloorSinceArm_ = rms; }
else if (fallCounter_ > 0) { --fallCounter_; if (rms < rmsFloorSinceArm_) rmsFloorSinceArm_ = rms; }

if (rms < 0.002f) { risePending_ = false; return false; }

const bool sharpRise  = (rms > rmsSmooth_ * 1.15f) || (rms > prevRms * 1.08f);
const bool clearsFloor = (rms > rmsFloorSinceArm_ * 1.15f + 0.005f);
return (fallCounter_ > 0) && sharpRise && clearsFloor && rms > 0.01f;
```

**An attack requires all four:** a fall within the last `kFallWindowBlocks` blocks,
a sharp rise, a rise that clears the trough by 15 % + 0.005, and `rms > 0.01`
(on the ×4-scaled signal, i.e. raw RMS > 0.0025).

### The arbitration that decides what you hear

`mirrorVoiceEndSample_` (`PatternPlayer.h:436-440`) is stamped to
`blockStart + offset + duration` whenever a mirror note is emitted
(`PatternPlayer.cpp:1546-1548`), and is passed as `suppressBeforeAbs` to the grid
line (`:1567`). Both grid emitters drop any event earlier than it
(`:1131-1132`, `:1227-1228`). It is cleared only by
`flushAllPendingNoteOffs` (`:534-544`) — i.e. seek, silence, bypass — and by
`reset()` (`:100`).

**Consequence:** the mirror is *only* audible if `result.trigger` is true. If the
attack detector stops firing, `mirrorVoiceEndSample_` stays `-1`, nothing is
suppressed, and the grid line plays unfettered. **That is exactly the "bass is
not mirroring, it's a root/harmony line" symptom.**

**Update (1.0.6) — the grid is no longer gated on `mirrorVoiceEndSample_` alone.**
`AccompanimentProcessor` also requires the guitarist to be **silent**:
`setBeatGridBassEnabled(listenBass && !guitarAudible)`, where `guitarAudible` is
the structure tagger's not-SILENT state. A live mirror note is emitted in `hold`
mode (`triggerLearnedBassNote(..., hold=true)`) and sustained until the guitar
stops, so a missed attack holds the last mirrored pitch instead of opening the
harmony underneath the player. `mirrorVoiceEndSample_` is now only the
pre-learning fallback *within* a silence; it no longer decides mirror-vs-harmony
while the guitarist is playing. Measured effect: harmony is 0 in every audible
window across the raw takes (was up to 26 notes per 30 s). See §3 (1.0.6).

**Update (1.0.8) — the detector is driven at a fixed hop, not once per host
block.** Even with time-based windows, the attack detector was called *once per
`processBlock`* with a single onset value from the last ~20 ms. Its **sampling
rate was therefore the host block rate**, so a 64-sample buffer saw the waveform
every ~1.5 ms and a 4096-sample buffer every ~93 ms — and a ~1 s block (a 1.2 s
media buffer) saw one value per second, below the 200 ms decay-recency window, so
`armed` was almost never true and the mirror stopped. `EnergyAnalyser` now records
the onset envelope at a fixed ~10.7 ms hop and `AccompanimentProcessor` drives
`PhraseLearner` once per hop, so the emitted mirror is identical from 64 to 4096
samples per block. Guarded by `bass mirror: the emitted mirror is
host-buffer-size invariant`. **A time-based window is necessary but not
sufficient — the *update rate* of a streaming detector must also be decoupled
from the host block.**

---

## 3. The regression cycle — the attempts, in order

| Ver | Commit / date | Diagnosis at the time | Change |
|---|---|---|---|
| 0.8.4 | `afe13bb` | — | Introduced `RiffMirror`: learning bass that mirrors after one repeat |
| 0.8.4 | `4f905f8`, `30d7044` | Bass blips (0.25-beat) | 0.9-beat legato, immediate trigger |
| 0.9.8 | (pre-`0f225f6`) | Bass didn't follow root | Detector `rms > prev × 1.2` |
| 0.9.10 | (pre-`0f225f6`) | **`rms > prev × 1.2` never fired on real playing** — the 100 ms RMS window smooths chugs to a few-% swing → ~1 attack / 6 s | New rule: "sharp rise *following a recent decay*" |
| 0.9.11 | (pre-`0f225f6`) | **Pitch-confidence gate starved the learner** — YIN confidence on distorted palm-mute is bimodal (≈0 at attacks) | Attacks gated on RMS transient **only**, never pitch confidence. Added immediate mirror |
| 0.9.12 | (pre-`0f225f6`) | Immediate mirror only ran *pre-lock*; the lock fires on first repeat and plays a skeletal 2-note slice | Live mirror now runs in `Locked` too |
| 0.9.13 | (pre-`0f225f6`) | Play mode mirrored but follow mode didn't — lock suppressed the mirror | `mirrorWhileHeld_`; suppress mirror only for solo licks |
| 0.9.26 | `f54b058` | — | `state_ != Locked && !userCapturing_ && !gridCapturing_` |
| 0.9.59 | `cdf4bac` | Record-transition bass | `(state_ != Locked \|\| mirrorWhileHeld_)` |
| 0.9.59 | (same release) | — | **`setMirrorWhileHeld` declared a stub and removed** |
| 0.9.63 | `ac576f8` | Mirror and grid layered | "Unified listen bass mixer" |
| 0.9.67 | `d90bc3e` | — | `GrooveRenderer` disconnected from live path |
| 1.0.1 | `db31dd7` | Riff phase / loop wrap / onset capture | — |
| 1.0.3 | `5d5f410` | **Mirror fired on every attack but the grid line played on top** — 128 attacks produced 512 bass ons; the monophonic voice kept retriggering | Mirror owns the voice; grid gated on `mirrorVoiceEndSample_`. Onset window 0.1 s → 0.02 s |
| — | `cf88acd` | — | **`mirrorWhileHeld_` removed again** — back to `state_ != State::Locked` (`PhraseLearner.cpp:691`) |
| 1.0.6 | — | **Harmony played during audible sustains.** On `data/raw/sustain/sustain.wav`, a 30 s window with the guitar audible 41 % of the time produced **7 mirror notes vs 25 harmony notes** — "no attack detected" was being treated as a gap, so any sustain/legato phrase (or missed detection) opened the harmony | Grid/harmony gated on guitar **SILENCE** (`!guitarAudible`), not on attack absence; live mirror notes are **held** until the guitar stops; the fallback remembers the last played key |

Two commits are literally named for the churn: `1d36e47 "Fix bass regression"`
and `a898b06 "Still chasing bass regression"` (2026-09-04).

### The cycle, abstracted

The bug alternates between **two independent failure modes**, and each fix
addresses only one of them:

- **Mode A — detector starvation.** The attack detector does not fire on real
  distorted guitar. Every detector rewrite (0.9.8 → 0.9.10 → 0.9.11 → 1.0.3) is
  an attempt to fix this. Each iteration trades one failure for another:
  - `rms > prev×1.2` → fires never (0.9.8)
  - rise-after-decay → fires on real chugging (0.9.10)
  - + pitch gate → starves (0.9.11)
  - remove pitch gate → machine-guns ~4×/note (1.0.2)
  - + `clearsFloor` and a 20 ms window → correct on the *test fixture*, possibly
    too strict on a *real* distorted signal (1.0.3 — **this is where we are**)
- **Mode B — path arbitration.** Something else owns the bass voice: the lock
  (0.9.12, 0.9.13), the grid/harmony line (1.0.3), or a removed API
  (`mirrorWhileHeld_`, 0.9.59 / `cf88acd`).

**Diagnosing "bass doesn't mirror" therefore always requires answering two
questions separately:** (1) is `result.trigger` firing? (2) if it is, is the note
being heard, or is the grid/frozen path overwriting it?

---

## 4. Why it keeps coming back — structural causes

These are the reasons this specific bug recurs, and they are the things to fix
rather than the symptom.

### 4.1 The detector is validated against synthetic audio, not the user's guitar

The mirror's integration test
(`tests/test_processor_pipeline.cpp:2478-2595`) feeds a **pure sine with an
exponential pluck envelope** (`0.55 * exp(-t*9)`) alternating 65.4 Hz / 98 Hz.
That is a near-ideal decay — a deep, clean inter-note trough. A distorted,
palm-muted, drop-C guitar is not that: distortion compresses dynamics and the
trough is shallow and noisy. The test passes on input the user never produces.

The one fix that demonstrably worked — **0.9.11** — was the one grounded in the
user's real recordings ("Ran the actual pipeline over `data/raw/` takes").
That is the methodology to keep.

### 4.2 The detector is block-count based, so it behaves differently at every buffer size

`PhraseLearner.h`:

| Constant | Value | Comment claims | Reality |
|---|---|---|---|
| `kFallWindowBlocks` (`:385`) | `20` | "~200 ms at 512/48k" | 213 ms @512, **427 ms @1024, 853 ms @2048** |
| `kSilenceResetBlocks` (`:396`) | `200` | "~4 seconds at 512 samples/block" | **2.13 s** @512/48k — the comment is wrong by ~2×; 8.5 s @2048 |
| `kMinAttackIntervalSamples` (`:384`) | `2000` | "~40 ms" | correct — this one is in *samples*, so buffer-independent |

`kFallWindowBlocks` is the **decay-recency window**. At 128-sample buffers it is
53 ms; at 2048 it is 853 ms. A 16th note at 200 BPM lasts 75 ms, so at small
buffers the fall window can expire before the next rise and attacks are lost,
while at large buffers a stale fall survives long enough to arm an attack it
should not. **The mirror therefore behaves differently depending on the host's
buffer size**, and no test covers this: the buffer-invariance tests
(`test_midi_probe.cpp`, `test_phase1_rendering.cpp`, `test_baseline_capture.cpp`)
exercise `PatternPlayer` MIDI rendering, and `test_phrase_learner.cpp` feeds
synthetic RMS values straight into the learner, so it is buffer-agnostic by
construction. See [`TEST_AUDIT.md`](TEST_AUDIT.md) §4.

### 4.3 Two sources of truth for the mirror, one of them repeatedly deleted

`PhraseLearner.cpp:691` gates the live mirror on `state_ != State::Locked`.
`mirrorWhileHeld_` was added at `cdf4bac` to relax that, then removed at
`cf88acd`. The 0.9.59 changelog calls `setMirrorWhileHeld` "a stub", and the
Phase 8 dead-code sweep (T8.1) lists it for deletion. So the *documented contract*
("bass mirrors while held") and the *code* (mirror suppressed when locked) have
disagreed, in both directions, more than once.

Note the interaction: in `Locked`, `PhraseLearner` still sets `result.trigger`
from the **frozen snapshot** (`PhraseLearner.cpp:744-765`), not from live attacks.
So once locked, the bass is playing a learned loop — which *sounds like* the
mirror to a test and like "not my playing" to a guitarist.

### 4.4 Tests reimplement the engine instead of calling it

`tests/test_golden_signal.cpp:105-133` defines its own `RmsWindow` copying
`EnergyAnalyser`'s 0.02 s ×4 clamped window. The 1.0.3 commit had to update this
fixture *because the engine changed*, and `tests/fixtures/README.md` still
describes the old 0.1 s window. Any future detector change silently desynchronises
the fixture from the engine.

### 4.5 Realistic dropout: `armActive`

`bassNote.trigger` is forced false unless armed (`AccompanimentProcessor.cpp:1530`),
and arming requires Play, count-in, capture, lock or transition
(`:1185-1190`). A user who has not pressed **Play** or **Record riff** gets no
mirror at all. This is by design but is a frequent false lead — check it first.

---

## 5. Most likely current cause — ranked hypotheses

> **These are hypotheses, ranked by evidence, not confirmed diagnoses.** Each has
> a decisive experiment. Run them before changing code.

### H1 (highest confidence) — `clearsFloor` is too strict for a real distorted signal

`detectAttack` now additionally requires `rms > rmsFloorSinceArm_ * 1.15f + 0.005f`.
This was added in 1.0.3 to stop a single pick mirroring as 2–3 notes on low
drop-C notes. But the threshold was tuned against a *clean exponential* fixture.
On a distorted signal the 20 ms window RMS sits in a compressed band, so a 15 %
rise above the trough plus an absolute 0.005 may never be reached.

- **Experiment:** replay `tests/fixtures/palm_mute_chug.wav` (real distorted
  palm-mute, 44.1 kHz mono) through the **real** `EnergyAnalyser` +
  `PhraseLearner` — not the `RmsWindow` copy — and log per-block
  `onsetRms`, `prevRms`, `rmsFloorSinceArm_`, and which of the four predicate
  terms failed. Count attacks/second and compare with the ~9/s the 0.9.11
  changelog measured on real chugging.
- **If confirmed:** the fix is a relative-only floor (drop the `+0.005`
  absolute term, or make it proportional), or an adaptive floor. **Do not
  weaken the fixture** — see §7.

### H2 — block-size dependence (`kFallWindowBlocks`)

If the user's DAW runs a buffer other than 512, the decay-recency window is a
different length in time than anything that was ever tested.

- **Experiment:** run the golden-signal pipeline at 128 / 256 / 512 / 1024 / 2048
  sample blocks over the same fixture and compare attacks/second and lock time.
  A difference means H2 is live.
- **If confirmed:** convert `kFallWindowBlocks` and `kSilenceResetBlocks` to
  samples (or seconds → samples in `prepare()`), exactly as
  `kMinAttackIntervalSamples` already is.

### H3 — the grid still wins in some phases

`setBeatGridBassEnabled(listenBass)` is true only for `PlaySection` and
`RiffBListen` (`AccompanimentProcessor.cpp:1871-1873`). Verify that the user's
symptom is not simply "the phase is not one of those two".

- **Experiment:** log `enginePhase`, `armActive`, `result.trigger`,
  `mirrorVoiceEndSample_`, and every bass note-on's source path
  (mirror / grid-authored / grid-harmonic / frozen) for a 30 s real take.

### H4 — locked-state regression (Mode B)

Auto-lock is disabled in Play (`PhraseLearner.cpp:707` + `:361` comment), so in
Play the learner should stay in `Learning` and keep mirroring live. Confirm this
is still true after the `cf88acd` removal — a leftover `Locked` state from a
prior Record session would swap the bass to the frozen loop.

- **Experiment:** assert `phraseLearner.isLocked() == false` throughout a Play
  session.

---

## 6. Debug recipe

There is **no existing instrumentation** for "which producer emitted this bass
note". Adding it is the single highest-value change for this bug. Suggested:
a per-note-on tag (mirror / grid-authored / grid-harmonic / frozen / pickup)
written to the `[GROOVE]`-style diagnostic stream or a test-only log.

Then, for any `fuzzyband.vst3` build:

```bash
# Confirm the installed plugin really is the build you think it is.
shasum ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
       build/MetalAccompaniment_artefacts/Release/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband
strings -a ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
  | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' | sort -u
```

*(This was checked on 2026-09-14: the installed VST3, the installed AU and the
build artefacts were byte-identical at v1.0.3. The "stale installed binary"
theory in `docs/IMPLEMENTATION_PLAN.md` T0.1 is **resolved** — do not re-chase
it without re-running the hashes.)*

---

## 7. What is verified, and what is not

**Verified working (do not regress):**

- Mirror ownership arbitration (`mirrorVoiceEndSample_`) — the 1.0.3 fix is
  correct and is covered by
  `test_processor_pipeline.cpp:2478` (density ≤ attacks + attacks/3) and
  `:2597` (harmony resumes in the gaps). This mechanism is now the contract.
- Mirror density on **synthetic** input: ≥90 % of attacks mirrored within 30 ms.
- Real-take **locking**: all four golden fixtures lock within 5–8 s at 512 blocks
  (`test_golden_signal.cpp:203-232`).

**Not verified (the hole this bug lives in):**

- Real-take **mirror output** — golden-signal tests assert lock time and the
  *learner's* notes/sec, never the emitted bass MIDI, and never the
  mirror-vs-grid arbitration.
- Anything at a block size other than 512 for the analysis layer.
- The `detectAttack` predicate against real distorted audio through the real
  `EnergyAnalyser` (the fixture copies the window logic).
- Which producer emitted a given bass note, in a running DAW.

**Do not, as a fix, relax an assertion or tag a test `[!mayfail]`.** Phase 0
(T0.3) established that three tests had been pinning regressions; that mistake
has been made here before.

---

## 8. Open questions for the frontier model

1. Is `clearsFloor`'s absolute `+0.005` term defensible, or should the trough
   test be purely relative? What does the real-take histogram say?
2. Should the decay-recency and silence-reset windows be time-based? What is the
   correct time for each (the comments disagree with the arithmetic)?
3. Should the mirror and the grid be separate *voices* rather than one
   arbitrated monophonic voice? The current design is inherently a
   "who wins" race; the recurring symptom is a consequence of the architecture,
   not just of tuning.
4. In `Locked`, should live attacks still drive the bass (the `mirrorWhileHeld_`
   question)? The contract and the code have disagreed twice. Pick one and
   encode it as a test.
5. What is the correct acceptance test for "bass mirrors my playing" that uses
   real audio at multiple buffer sizes and asserts the *emitted MIDI*, not the
   learner's internal counters?
