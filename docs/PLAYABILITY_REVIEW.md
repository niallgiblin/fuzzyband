# Fuzzyband — Drum & Bass Response Review

**Subject:** `Metal Accompaniment` / Fuzzyband — guitar-audio → MIDI drums (ch 10) + bass (ch 2)
**Source reviewed:** `/Users/ng/projects/fuzzyband` @ `d68c9d5` (v0.9.67)
**Method:** full read of the MIDI/selection/hold/clock path, git archaeology across v0.9.44 → v0.9.67, both test binaries run, numerical simulation of the riff-lock clock and the velocity chain. Read-only — no source files were modified.

---

## 0. Read this first

### 0.1 You have been play-testing a stale binary

| Artefact | Version | Built |
| --- | --- | --- |
| Installed `~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3` | **0.9.62** | 9 Sep 12:46 |
| Source tree `HEAD` | **0.9.67** | 9 Sep 15:25 |

Verified from the bundle `Info.plist` and the binary's embedded version string.

Everything from **v0.9.63** (committed 14:00) onward landed *after* that build. v0.9.63 is
specifically the "unified listen bass mixer" slice written to fix the failures in your own log
(`LOCKED RIFF DOESN'T PERSIST`, B/C bass holes, guitar-stop cut); v0.9.64–v0.9.67 followed.

Some of what you logged has already been rewritten in the tree you are asking about. **Rebuild and
re-install before judging the current engine.** The repo's own `build/` output is also incomplete —
the VST3 bundle contains `Info.plist` but no binary — so a clean rebuild is required regardless.

### 0.2 The short version

The *selection* logic is over-engineered and mostly unreachable; the *rendering* logic has hard
defects that make the kit flat, choked and late. Fix these six first:

1. **Every drum note is released at the end of its own audio block** — note lengths become
   buffer-size dependent and every cymbal is choked (`PatternPlayer.cpp:583`).
2. **A DAW loop shorter than `lockBars` wedges the riff lock permanently**, and the locked bass is
   silent for the part of each loop pass before the lock was armed (`AccompanimentProcessor.cpp:1013,1748-1767`).
3. **The velocity chain saturates the 1–127 clamp** — kicks, snares and accents all render as **127**
   at any normal playing level, so the dynamic hierarchy is gone (`PatternPlayer.cpp:928,561-567`).
4. **Play mode has no groove rotation** — the rotation engine is bypassed in Play and the pool
   constraint snaps to a fixed member. Verified empirically: a 300 s run used only patterns 14 and 22.
5. **Response latency is up to ~6 s** (512 ms mel window + 2-bar hold + bar quantisation). The
   project's own stated budget in `docs/MUSICALITY_ROCK_PIVOT_PLAN.md` §7 is **< 30 ms**.
6. **The authored per-pattern bass lines are dead code for the second time** — v0.9.0 wired them,
   v0.9.63 disconnected them again, and the test suite now asserts they must not play.

§2 is correctness defects, §3 playability improvements, §4 musicality improvements, §5 tests to add.

---

## 1. How response actually works today

Since v0.9.30 the engine is idle and silent until armed (`AccompanimentProcessor.cpp:884-898`).
There are only **two audible surfaces**:

* **Play** — walks the Sections list once; `enginePhase = PlaySection`; patterns constrained to a
  per-section pool.
* **Record riff** — `RecWaitBar → RecCountIn → RecCapture → RiffA → RiffBListen → RiffBLocked`,
  then one post-lock contrast and back to A.

Everything else is silent by construction: in `Idle`, `armActive` is false so `trulySilent` is true
regardless of what you play. This matters — the reactive "follow" path that the whole ML pipeline
feeds **cannot be heard** (see §2.4).

Response-latency chain for a drum pattern change (120 BPM = 2 s/bar):

| Stage | Worst case | Where |
| --- | --- | --- |
| 512 ms mel window + inference tick | ~0.53 s | `MelSpectrogramExtractor.h:33`, `AccompanimentProcessor.cpp:256` |
| 2-bar drum hold (`8 beats`) | 4.0 s | `AccompanimentProcessor.cpp:370-374` |
| Bar-boundary quantisation in the player | 2.0 s | `PatternPlayer.cpp:1017-1025` |
| **Total** | **~6.5 s** | |

The two triggers that used to short-circuit the hold (`|rmsDelta| > 0.6` and a 4-bar auto-change
timer) were deleted in `cdf4bac` (v0.9.57–v0.9.60) and never restored. `rmsDelta` is still computed
and published (`AccompanimentProcessor.cpp:566,668`) but nothing reads it.

---

## 2. Correctness defects (ranked)

### 2.1 Drum note-offs are force-fitted into the triggering block — **blocker**

`src/midi/PatternPlayer.cpp:580-584`

```cpp
const int durSamps = juce::jmax(1, static_cast<int>(std::round(
    static_cast<double>(ev.durationBeats) * samplesPerBeat)));
const int noteOffOffset = juce::jmin(numSamples - 1, off + durSamps);
midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, outNote), sampleOffsetBase + noteOffOffset);
```

`jmin(numSamples - 1, …)` guarantees the note-off cannot be scheduled past the current block, and
unlike the bass (`:697-701`) and the armed crash (`:402-405`) there is **no deferred note-off
mechanism for drums**.

At 120 BPM a beat is 22 050 samples @44.1 kHz, so a `durationBeats = 0.25` hit is 5 512 samples —
larger than *any* realistic buffer. Therefore at every normal buffer size (128–2048) **every drum
note is released within its onset block** (3–46 ms) rather than after its authored duration.

* Cymbals are authored long on purpose (`drum(kCrash, 110, 0.0f, 2.5f)` and friends) — they get
  choked at the block boundary.
* Open hats inherit the closed-hat default `dur = 0.25f`, so an "open" hat is closed after a 16th.
* The delivered artefact changes with buffer size, so a take recorded at 512 samples renders
  differently offline.
* Note 49 is inconsistent: `armTransitionCrash()` *does* defer its note-off across blocks ("let the
  crash ring ~1 beat") while a pattern's own crash does not.

Not caught by tests because `tests/test_pattern_player.cpp:148` renders **one giant block**, where
the clamp never trims anything.

**Fix:** add a fixed-size deferred drum note-off table (mirroring `bassNoteOffSample` /
`bassNoteOffMidi`) flushed at the top of `process()`; give open hats and cymbals a long or absent
gate. Do this **together with §2.14**, or you convert a choke bug into a stuck-note bug.

---

### 2.2 A DAW loop wedges the riff lock, and silences the locked bass — **blocker**

This is the most likely root cause of the `LOCKED RIFF DOESN'T PERSIST` entry in your log, and it
also explains why B3–B5 were never filled in: with a loop armed, the lock never advances.

The lock schedule is anchored to **absolute host samples captured once**, with no re-anchor:

```cpp
// AccompanimentProcessor.cpp:1013  (enterRiffA)
riffAPlayOriginSample = clockSample;              // set once
grooveLockEndSample   = clockSample + lockDuration;
grooveLockStartSample = clockSample;
```

`clockSample` (`:620`) follows loop wraps and seeks, because `PatternPlayer::previewResolvedHostSample`
returns the raw host position whenever it moved (`PatternPlayer.cpp:186-194`). The emitter then has
no negative-phase handling (`:1748-1767`):

```cpp
const double beat0 = static_cast<double>(clockSample - origin) / spb;
const double beat1 = beat0 + static_cast<double>(numSamples) / spb;
for (int s ...) {
    double k = std::ceil((beat0 - tSlot) / loopBeats - 1.0e-12);
    if (k < 0.0) k = 0.0;                 // <- forbids negative placement
    const double t = tSlot + k * loopBeats;
    if (t < beat0 - 1.0e-12 || t >= beat1 - 1.0e-12) continue;   // nothing fires when beat1 <= 0
}
```

When the transport wraps, `clockSample < origin` for the part of the loop before the lock was armed,
so `beat0 < 0`, `k` is forced to 0, every `t >= 0 > beat1`, and **no bass note is emitted at all** —
while `enginePhase == RiffA` keeps the drums frozen and audible (`:847-850`).

And because expiry is also measured in absolute samples (`if (clockSample >= grooveLockEndSample)`,
`:1279`), a loop shorter than `lockBars` **can never expire**.

Simulated with the exact code arithmetic (120 BPM, 512-sample blocks, dense 16th chug, 4-bar DAW
loop, `LOCK = 16` bars, lock engaged 2 bars into the loop):

```
notes emitted over 32 bars : 128   (monotonic equivalent: 256 — half lost)
lock expires?              : False
max reachable clock        : 1 352 799  <  lockEnd = 2 587 600
```

So: drums frozen forever, bass dropping out for the first half of every loop pass, no transition,
and the UI bar counter stuck at "bar 1/N". That is exactly the reported combination.

**Fix (two parts, both needed):**

1. Make the emitter loop-relative: `double phase = fmod(beat0, loopBeats); if (phase < 0) phase += loopBeats;`
   and fire slots in `[phase, phase + blockBeats)` modulo `loopBeats`.
2. Re-anchor every lock/transition timer on a host jump. `PatternPlayer` already detects the jump
   (`:949-965`) but never tells the processor — expose it (e.g. `consumeTransportJumped()`) and
   either restart A at the new origin or, better, count bars from the musical position via
   `getPlayHead()->getPosition()->getPpqPosition()` (currently unused anywhere in `src/`) so loop
   wraps and seeks are naturally bar-relative.

**Test:** an integration test with a playhead whose `getTimeInSamples()` wraps at a 4-bar loop point
mid-lock, asserting (i) ≥1 ch.2 note-on per occupied 16th of the `riffA` snapshot and (ii) the
transition engages within `lockBars` bars. No existing test uses a non-monotonic host clock —
`tests/test_processor_pipeline.cpp` always drives the processor's own monotonic counter.

---

### 2.3 The velocity chain saturates — the kit has no dynamics — **blocker (musicality)**

`src/midi/PatternPlayer.cpp:928` and `:561-567`

```cpp
sectionVelMul = preset.sectionVelocityMultiplier(sectionId) * preset.velocityScale * guitarEnergy;
...
const float mul = grooveTemplate.velocityMul[grid16] * sectionVelMul;
vel = static_cast<int>(std::round(static_cast<float>(ev.velocity) * mul + boundedGaussian(...)));
vel = juce::jlimit(1, 127, vel);
```

`guitarEnergy` is `juce::jlimit(0.85f, 1.28f, 1.0f + rms * 1.5f)` (`AccompanimentProcessor.cpp:874`)
and `rms` is already `jlimit(0, 1, rawRms * 4)` (`EnergyAnalyser.cpp:128`). Two consequences:

* The effective range is **[1.0, 1.28]**, not [0.85, 1.28] — the documented "relax when they ease
  off" never happens; it is a constant +2.5 dB as soon as anything is audible.
* The 1.28 ceiling is reached at a raw RMS of ~0.047, i.e. *any* real guitar.

Four gains then multiply: `velocityMul[cell]` (metal backbeat 1.1424) × `verseVel` (0.95) ×
`velocityScale` (1.0) × `guitarEnergy` (1.28) = **1.389**. Verified by arithmetic:

| Authored | Rendered |
| --- | --- |
| 108 (backbeat) | **127** |
| 110 | **127** |
| 115 (kick) | **127** |
| 124 (accent) | **127** |

Kicks and snares peg at 127 for any authored value ≥ ~92. The GMD-derived accent hierarchy — the
entire point of `GrooveTemplateData.h` — is destroyed exactly when it should be audible. Only ghosts
(separately clamped to 15–42) keep dynamics, so the kit reads as *ghost + everything-else-at-max*:
a flat wall.

**Fix:** leave headroom (scale authored velocity by ~0.8, or soft-limit the product to ≤ ~1.1 instead
of hard-clamping at 127); make `guitarEnergy` a swell around a nominal < 1 (e.g. `0.92 + 0.22*e`)
with a wider RMS mapping so it can also relax.

---

### 2.4 Play mode has no groove rotation — long static stretches — **major**

`src/AccompanimentProcessor.cpp:822-823` constrains every block to the section pool:

```cpp
if (pool.count > 0)
    effectivePatternIdx = PatternRules::constrainToPool(patternIdx, pool, st);
```

`constrainToPool` (`pattern_rules.h:697-713`) returns the incoming index if it is already a pool
member, otherwise the **first state-compatible member**, otherwise `pool[0]`.

Two things the docs promise are missing:

* **Rotation.** `AccompanimentProcessor.cpp:383-392` runs `diversifyPatternForGenre` /
  `diversifyPatternForStyle` (the bar-phase variety engine) **only when `playSectionIndex < 0`**. In
  Play, `playSectionIndex >= 0`, so it is skipped. In every other non-Play phase,
  `patternSelectFrozen` is true (`:853-855`) and the inference thread returns early (`:290-293`).
  **The entire diversity engine is unreachable in the audible path.**
* **ML variety.** `MetalGrooveInference::selectPatternFromMel` has a top-K weighted draw for
  bar-to-bar variety (`MetalGrooveInference.cpp:324-341`), but production calls it with `seed = -1`
  (`AccompanimentProcessor.cpp:303,336`), which selects deterministic argmax.

The pieces exist and are correct — the pools, `orderedSectionPatternPoolForGenre`, and the rotation
helper `pickPoolPattern` — but nothing calls the rotation. `grep -rn pickPoolPattern src/` finds only
its own definition.

**Empirical confirmation.** Running `build/MetalAccompanimentIntegrationTests`:

```
[GROOVE] section 1..8 : patIdx=22 "Rock Backbeat" stateIdx=1 rms=0.0368
[GROOVE] section 9..12: patIdx=4  "Chorus Mid"    stateIdx=2 rms=0.3394
[STABILITY] 62 sections, 240s music — Pattern range: 14 – 22
```

Twelve consecutive sections produced **two** distinct patterns; a 300-second stability run exercised
only patterns 14 and 22.

**Fix:** in the Play branch replace the raw snap with
`pickPoolPattern(orderedSectionPatternPoolForGenre(secName, genreId), seed, grooveSlot, lastPlayed)`
seeded from the global bar count / section instance, keeping `constrainToPool` only as the
"is this pick legal" test. Change the fallback from `pool[0]` to a state-compatible global pattern so
a loud verse does not collapse to one groove.

---

### 2.5 The authored per-pattern bass lines are dead code — for the second time — **major**

`src/midi/PatternPlayer.cpp:647-663` defines `emitBassRange()`:

```cpp
if (!pattern.bassEvents.empty())
    emitPatternBass(midi, numSamples, beatStart, beatEnd, pattern, sampleOffsetBase);
else
    emitHarmonicBass(midi, numSamples, beatStart, beatEnd, sampleOffsetBase);
```

`grep -rn emitBassRange src/` returns **only the declaration and the definition — no call site.**
`process()` calls `emitHarmonicBass` directly (`:1159-1168`). 23 of the 28 patterns carry authored
bass lines; none can play.

`docs/MUSICALITY_ROCK_PIVOT_PLAN.md` §1.1 documented this exact problem in the v0.8.x era
("*Bass (`MidiPatternLibrary` `bassEvents`) — **never called** — ⚠️ Dead code*"), §3 A1.1 fixed it in
v0.9.0, and v0.9.63 (`ac576f8`) removed it again: *"Library `bassEvents` are not mixed into the live
path."* The live bass is now `emitHarmonicBass` — root/fourth/fifth/octave on a fixed beat grid,
2 notes/bar (verse/breakdown/intro/outro) or 4 (chorus/solo).

The test suite now **asserts the regression** (`tests/test_pattern_player.cpp:453-456`):

```cpp
// Listen grid is harmonic root from setBassParams, not authored intervals.
REQUIRE(bassNotes.count(40) > 0);  // root
REQUIRE(bassNotes.count(45) == 0); // library +5 must not leak
REQUIRE(bassNotes.count(47) == 0); // library +7 must not leak
```

while `PatternPlayer.h:228-229` still claims *"A1.1: authored bass lines … play when present"*.

**Fix:** call `emitBassRange()` (prefer the active pattern's `bassEvents`, transposed to the live
root) whenever `beatGridBassEnabled_`, keeping `emitHarmonicBass` as the fallback for patterns with
no authored line. Update that test to assert the authored line *does* play, transposed.

---

### 2.6 A locked riff's bass re-articulates every 16th — **major (musicality)**

Capture stamps the grid **one 16th slot at a time**, using each slot's own peak
(`AccompanimentProcessor.cpp:1691-1714`):

```cpp
phraseLearner.stampGridRange(s * 0.25, s * 0.25 + 0.25, slotPeak, bassMidi);
```

`PhraseLearner::stampGridRange` (`PhraseLearner.cpp:282-312`) marks a slot occupied whenever the slot
peak exceeds 0.025 — so **a sustained or ringing note paints every 16th it covers**.

Playback then emits one note per occupied slot with a fixed 16th gate
(`AccompanimentProcessor.cpp:1755,1757-1771`):

```cpp
const int duration = juce::jmax(1, static_cast<int>(0.25 * spb));   // exactly a 16th
...
if (!riff.occupied[s]) continue;
patternPlayer.triggerLearnedBassNote(note, 0.58f, offset, duration);
```

The snapshot stores **occupancy only — no note lengths** — so a held chord becomes eight 16th-note
retriggers per bar. That is the "machine-gun bass" symptom. Note also that `CHANGELOG.md` claims a
"~0.9 beat legato … instead of staccato blips"; that value exists only on the live-mirror call
(`:1505`), never on the locked path.

**Fix:** store gate lengths in `LearnedRiff` — e.g. `std::array<uint8_t, kGridSlots>` of 16th counts —
and emit `dur = min(gapToNextOccupiedSlot, 0.9 beat)` (with `gap = loopBeats` for the last note).
`emitBassNote` is explicitly monophonic (`:674-684`), so a longer duration is safe. A cheap
intermediate hotfix is to only trigger when the previous slot was unoccupied or the pitch changed.

---

### 2.7 Response latency: up to ~6 s — **major (playability)**

See the table in §1. The hold is `8.0 * 60/bpm * sr` samples = 2 bars
(`AccompanimentProcessor.cpp:370-374`) and the player quantises to the next bar boundary
(`PatternPlayer.cpp:1022`). The two triggers that used to bypass the hold were deleted in `cdf4bac`,
and their input (`rmsDelta`) is now write-only.

**Fix:** reintroduce a bounded reactivity path — on a large RMS step (`|rmsDelta| > 0.6`, already
computed) permit an immediate commit applied at the next **beat** rather than the next bar; reduce
the default hold to 1 bar; reset the hold timer on every accepted commit. Target ≤ 250 ms from a
clear dynamic change to an audible pattern change.

---

### 2.8 Transitions and B-listen are uninterruptible by design — contradicts the shipped contract — **major**

`src/AccompanimentProcessor.cpp:1421-1429`

```cpp
// Replaying the recorded riff mid-transition does not interrupt it — the loop is
// fixed A-B-A-C-A.
if (clockSample >= transitionEndSample)
```

`transitionBars` defaults to 8 (range 2–32), so the plugin ignores your most reliable "I'm back"
gesture for up to 16 seconds. `docs/END_USER_STRESS_TEST.md` §B4 still specifies the opposite as a
pass criterion: *"play Lock Riff A again. That contrast must cut short and re-lock."* A test pins the
new behaviour (`tests/test_processor_pipeline.cpp:1138`, *"riff replay mid-transition does NOT cut it
short"*).

The same pattern appears in `RiffBListen`: `drumB0` is pinned with no rotation
(`AccompanimentProcessor.cpp:838-846`) until `PhraseLearner` locks, which needs ≥8 attacks *and* two
consecutive phrases whose inter-onset intervals agree within a ¼ beat
(`PhraseLearner.cpp:76,555-567`). If you never repeat a phrase verbatim twice, B holds until the clock.

**Fix:** allow a cut-short when the *same* riff re-matches (keep the "don't re-lock onto a different
riff" rule); let B-listen follow inference constrained to the contrast pool instead of freezing;
lower the B lock threshold; reconcile the doc with whichever behaviour you choose.

---

### 2.9 The "guitar stop" gate kills the locked accompaniment mid-lock — **major**

`src/AccompanimentProcessor.cpp:1496-1502`

```cpp
if (silentNow || rms < 0.003f) guitarSilentSamples += numSamples;
else                           guitarSilentSamples = 0;
const bool guitarStopped = guitarSilentSamples >= static_cast<int64_t>(sr);
if (guitarStopped && (frozenRiffBass || enginePhase == EnginePhase::RiffBListen))
    patternPlayer.setStructureSilent(true);
```

`PatternPlayer.cpp:970-983` then emits `allNotesOff(1..16)` at offset 0, clears the deferred
note-offs and the learned queue, and returns — so notes queued by `emitFrozenRiff` in that same block
are dropped and the sounding note is cut.

`rms` is 4× the true RMS, so `rms < 0.003` is ≈ −62 dBFS; but `silentNow` only needs
`state == SILENT`, which is reached after ~1 s of hold once the 4 s `noteRinging` window expires
(`StructureTagger.h:64-81`, `AccompanimentProcessor.cpp:577-581`). So **~5 s without a new pick** —
a sustained lead note or a quiet passage — kills the locked bass and fires all-notes-off. That
contradicts the design intent recorded elsewhere: `PhraseLearner` deliberately disables its silence
reset while `holdActive_` to keep the riff through a breath (`PhraseLearner.cpp:444-457`), and
`END_USER_STRESS_TEST.md:884` says a phrase breath must not reset. The 1 s gate overrides both.

**Fix:** do not route the guitar-stop gate into `RiffA`/`RiffBLocked` (the point of a lock is
accompaniment independent of the player), or at minimum require a transport-level stop and emit the
pending bass note-off before clearing.

---

### 2.10 Humanisation moved the grid, and muted or ignored the ghosts — **major (musicality)**

`src/midi/GrooveTemplateData.h` (GMD-derived):

```cpp
kRockTimingMs[16] = { -5.4350f, 8.6540f, -4.1670f, -9.0910f, ... };
kRockTimingJitterMs = 6.000f;   // was 1.5
kRockVelocityJitter = 8.000f;   // was 3.0
kRockGhostVelocityLo = 15.0f;   // was 30
kRockGhostVelocityHi = 42.0f;   // was 55
kRockGhostThreshold = 42;       // was 62
```

Off-16th hits moved from on-grid to **+8.65 ms late** (cell 1) and **+8.55 ms** (cell 9) with a 6 ms
gaussian on *every* hit (±2.5σ = ±15 ms) — ~11 % of a 16th at 120 BPM, applied to every voice. That
reads as "the kit doesn't land with the pick", not as human. The downbeat cells are also
systematically early (−2.7 to −6.8 ms).

Separately, the ghost threshold no longer matches the library it describes.
`MidiPatternLibrary.cpp:755-757` still says *"Ghost notes are authored at velocities <= 62 so the
groove engine treats them as ghosts (30–55 band, slightly early)"*, but the live threshold is 42.
Pattern 20 "Verse Ghost" authors its ghosts at **68–72** — all above 42 — so they are not treated as
ghosts: they lose the early microtiming and the quiet clamp, and with §2.3's saturation they render
at ~80, only ~4 dB below the backbeat. Pattern 22's real ghost (velocity 40) is clamped to 15–42 as
intended. The two patterns are inconsistent.

**Fix:** re-derive `ghostThreshold`/ghost band against the current library (62 as documented, or
re-author pattern 20); median-centre the derived timing offsets and keep only a small residual
jitter behind a user "humanize" amount.

---

### 2.11 Per-bar ornaments rewrite the groove; "guitar energy" is a constant — **major (musicality)**

`src/midi/PatternPlayer.cpp:271-345, 570-575`

A deterministic per-bar hash can, invisibly and mid-phrase:

* switch the **entire hat voice to ride + bell** (28 % of Solo bars, 12 % of Chorus bars),
* drop a kick (12 % of breakdown/outro bars),
* open a hat (18 % chorus / 8 % elsewhere),
* add an extra ghost (15 % of verse/breakdown bars),
* add a two-tom micro-fill (12 % of every 4th bar).

Because these are hash mutations applied at render time, the groove the player locked onto can change
identity bar to bar without any committed pattern change — heard as "the kit wanders" rather than as
humanisation. And per §2.3, `guitarEnergy` is effectively pinned at 1.28 whenever you play, so it is
not a response either.

**Fix:** promote ornaments to distinct library variants selected by the (rotating) pattern-picker so a
change is a committed change at a bar line; gate `rideSwitch`/`dropKick` behind a single "humanize"
amount defaulting low.

---

### 2.12 Mid-block pattern changes emit the new pattern on the wrong time base — **major**

`src/midi/PatternPlayer.cpp:1036-1049, 1051-1075`

```cpp
auto emitGroove = [&](double from, double to, int patIdx) noexcept
{ ...
    emitDrumEventsForRange(midi, numSamples, from, to, library->getPattern(patIdx), orn, 0);
};
...
emitGroove(beatStart, changeBeat, activePatternIndex);   // correct base
... activePatternIndex = pendingPatternIndex;
emitGroove(changeBeat,   beatEnd,   activePatternIndex); // WRONG base
```

Inside `emitDrumEventsForRange`, `rel = t - beatStart` where `beatStart` is the **`from` argument**
(`:526`), and the result is inserted at `sampleOffsetBase + off` with `sampleOffsetBase` hard-wired
to `0` (`:1048`). So the second half's events are computed relative to `changeBeat` but placed
relative to the **block start** — early by `(changeBeat - blockStartBeat) * samplesPerBeat`, and the
two halves therefore overlap in the same sample range.

This fires in exactly the block a listener is paying attention to, and at every pattern change
(~every 2 bars in follow mode, at every section entry, and on any redundant `queueGrooveCommit` since
`:1020-1025` sets `changeBeat` even when the index is unchanged).

Untested: `tests/test_pattern_player.cpp:283` changes pattern on an exact bar line, and the fill
tests only count toms, so a one-block shift is invisible.

**Fix:** pass the block-relative base,
`sampleOffsetBase = round((from - blockBeatStart) * samplesPerBeat)`, or give
`emitDrumEventsForRange` an explicit `blockBeatStart`. Add a dual-block-size golden test.

---

### 2.13 Crash handling — **moderate**

`src/midi/PatternPlayer.cpp:1010-1015, 382-406, 970-983`

* `armCrashPending` fires at `sampleOffset = 0` — the **block start**, not a beat or the section bar
  line — so it is off-grid by up to a buffer.
* On a section change into a pattern that already crashes on beat 1 (4, 5, 8, 11, 12, 21) you get two
  note-49 ons within a few ms → flam or instant choke.
* The crash gate is one beat; the library authors crashes at 2.5 beats.
* `armCrashPending` is **not cleared** by the silent early-return (`:970-983` clears only the
  note-offs, click and learned notes), so a crash armed while silent fires at the start of the first
  later non-silent block — arbitrarily late and off-grid.

**Fix:** give `emitCrashHit` a computed grid offset (next beat / section bar line); skip it when the
active pattern already crashes there; lengthen the gate; clear `armCrashPending`,
`pendingBarFillIndex_` and `bassLeadInArmed` in the silent branch.

---

### 2.14 Seek/loop drops pending note-offs → stuck notes — **moderate**

`src/midi/PatternPlayer.cpp:949-965`

```cpp
if (delta < -slack || delta > slack)
{
    pendingPatternIndex = -1; pendingGrooveCommitValid = false; ...
    bassNoteOffSample = -1;      // note never released
    crashNoteOffSample = -1;     // note never released
    for (auto& p : pendingLearned_) p = {};
    armCrashPending = false;
    clickNoteOffSample = -1;
}
```

A bass note scheduled beyond the block (`:697-701`), a ringing crash, and a click all keep their only
note-off in these slots; a DAW loop wrap or seek clears them **without emitting the note-off**, so
they ring until an all-notes-off happens to fire. Because the output is MIDI that users record, that
is a note-on with no note-off in the track. Drums currently escape only because §2.1 force-fits their
note-offs into the same block — so fixing §2.1 without fixing this makes things worse.

**Fix:** emit the pending note-offs (or `allNotesOff` on channels 2 and 10) before clearing.

---

### 2.15 The bass octave control produces wrong pitch classes — **moderate**

`src/midi/PatternPlayer.cpp:127-134`

```cpp
note.midi = juce::jlimit(28, 55, midiNote);
```

called with an already-transposed note (`AccompanimentProcessor.cpp:1770`, `:1526`):

```cpp
const int note = riff.midi[static_cast<size_t>(s)] + bassTranspose;
```

The snapshot holds C2–B2 (36–47) and `bassTranspose` is a user parameter of `(idx-1)*12`
(`:703-711`). With `−12` the values become 24–35, and every value below 28 clamps **up to 28 (E1)** —
so pitch classes C, C♯, D and D♯ all become E; the drop-C tonic plays as E. With `+12` the values are
48–59 and G/G♯/A/A♯/B all clamp down to 55 (G3). `emitPatternBass` and `emitHarmonicBass` fold
octaves correctly (`:738-739`, `:787-788`); the learned/frozen path was never updated.

**Fix:** replace the clamp with the same fold — `while (note < 28) note += 12; while (note > 55) note -= 12;`.

---

### 2.16 A ringing mirror note suppresses the next downbeat — **moderate**

`src/midi/PatternPlayer.cpp:754-755` and `:808-810`

```cpp
const int64_t hitAbs = blockStart + static_cast<int64_t>(off);
if (bassNoteOffSample >= 0 && hitAbs < bassNoteOffSample)
    continue;                                   // skip this grid hit
```

A live-mirror note is 0.85 beat long (`AccompanimentProcessor.cpp:1505`). A pick on the "and of 4"
rings 0.35 beat past the downbeat, so the beat-1 harmonic-bass hit **at the chord change is skipped
entirely** — the new chord's root never sounds on the downbeat. In chorus/solo (`notesPerBar = 4`,
1-beat spacing) a quarter of the grid hits are dropped this way.

**Fix:** prefer retriggering (close the ringing note with its own note-off, then play the grid root)
over dropping the hit, or bound the mirror duration to the distance to the next grid hit.

---

### 2.17 The attack detector drops fast chugs — **moderate**

`src/analysis/PhraseLearner.cpp:52-71`

```cpp
const bool fell = (rms < prevRms * 0.97f);
if (fell) fallCounter_ = kFallWindowBlocks;
...
const bool sharpRise = (rms > rmsSmooth_ * 1.15f) || (rms > prevRms * 1.08f);
return (fallCounter_ > 0) && sharpRise && rms > 0.01f;
```

A 16th-note palm-mute run at 180 BPM is ~166 ms/16th against a ~100 ms RMS window, so consecutive
picks frequently never produce a ≥3 % block-to-block fall; `fallCounter_` expires and the attack is
dropped. Equal-level repeated chugs must also exceed the previous block by 8 %. Fewer attacks means
fewer mirrored bass notes and a harder time reaching the ≥8-attack lock threshold (§2.8) — your
Station D "blast/thrash bait" is exactly this case.

**Fix:** use the fall test only to reject a sustained constant level; accept a plain
`rms > prevRms * 1.08 && rms > 0.01` rise.

---

### 2.18 Fills: fill 19 is unreachable in Record; 17/18 can land a bar late and overlay the groove — **moderate**

* **Fill 19 ("Fill Big") cannot fire in the Record-riff phases.** `updateOutgoingFill` is called with
  `seed = 0u` for both `riffAFillArm` and `riffBFillArm` (`AccompanimentProcessor.cpp:1276,1455`), and
  `selectFillPattern` returns `(seed & 1u) ? 19 : 18` when `rms >= 0.45` (`pattern_rules.h:426`). With
  `seed = 0` that is always **18**. Play passes a varying seed, so 19 is reachable only there. Your
  Station G5 ("strum as hard as you can → want 19") fails by construction.
* **17/18 can be placed on the first bar of the *next* section.** `fromNext = (beatInBar >= 0.05)`
  (`:1659`) is a magic 0.05-beat guard (~25 ms at 120 BPM), so any longer buffer flips the fill into
  the following bar. Use an explicit section-boundary sample instead.
* **17/18 overlay the groove instead of replacing it.** Only fill 19 suppresses the groove
  (`PatternPlayer.cpp:1034,1040-1047`). Fill 17 starts at beat 3 and 18 at beat 2, so for two beats the
  pattern plays under the tom roll. Concrete collision: pattern 21 has `drum(kKick, 124, 3.75f)` and
  fill 18 also has `drum(kKick, 118, 3.75f)`; the fill layer bypasses microtiming
  (`emitBarFill`, `:434-435`, uses a plain `round`), so two same-note kicks land ~3 ms apart — a flam.
  The fill also bypasses the velocity hierarchy, `sectionVelMul`, `guitarEnergy` and swing, so the kit
  audibly "resets" to a quantised loud layer for the fill.
* **Fills ignore swing** while the groove does not (`:539-543` vs `:434`), so a swung groove gets a
  straight fill.

**Fix:** compute the section end as a host sample and arm against it; make 17/18 replace the groove
for their window; run fill events through the same microtiming/velocity path; pass a non-zero seed in
Record.

---

### 2.19 Genre change desyncs the Swing slider — **minor but user-visible**

`src/AccompanimentEditor.cpp:333-343`

```cpp
if (auto* swingParam = dynamic_cast<juce::AudioParameterFloat*>(
        audioProcessorRef.getApvts().getParameter("swing")))
    *swingParam = Groove::presetFor(g).defaultSwing;
```

`swingSlider` has a `SliderAttachment` on `"swing"` (`:426-427`). Writing the `AudioParameterFloat`
with `operator=` sets the value **without notifying**, so the attachment never fires, the slider thumb
does not move, the host records no automation, and the value may not be persisted.

This sits directly behind the stress-test question *"Swing slider: Still 0 unless you moved it"* — the
parameter did change, the UI just cannot show it.

**Fix:** `getApvts().getParameter("swing")->setValueNotifyingHost(normalised)`.

---

### 2.20 The idle Pattern readout shows the proposal, not 0 — **minor**

The inference thread stores `finalIdx` into `displayPatternIndex` every tick
(`AccompanimentProcessor.cpp:401`), including while idle; the audio thread overwrites it with
`effectivePatternIdx` (`:1583`), which in idle is the same live proposal. So the UI shows the engine
auditioning patterns while the output is silent, contradicting `docs/END_USER_STRESS_TEST.md` §3
("Pattern **0**" on idle) and matching your note *"Pattern changes while I play as expected though.
0 on silence."*

**Fix:** publish `0` while not armed, or update the doc to say the readout is the proposal.

---

### 2.21 The three groove templates are the same data — **minor (data quality)**

`src/midi/GrooveTemplateData.h`:

* `kRockVelocityMul` and `kPunkVelocityMul` are **byte-identical**.
* `kPunkTimingMs[i] == kRockTimingMs[i] / 4` exactly (−5.4350 → −1.3590, 8.6540 → 2.1630).
* All three blocks report the same provenance: *"204 files, 115534 hits"* — including metal, which
  the header says is *derived from rock*.

So "punk stats pulled near-grid with tight jitter" is a synthetic scale, not an independent
derivation; the generator appears to have summarised one corpus three times. Genre timing differences
therefore come only from `defaultSwing` and jitter, not from the templates.

**Fix:** regenerate per-genre from genuinely filtered subsets, or drop the pretence and document the
derivation.

---

### 2.22 Tests enshrine the regressions — **process risk**

* `tests/test_pattern_player.cpp:453-456` asserts the authored bass lines **must not** play.
* `tests/test_e2e_groove_variety.cpp` is titled *"multi-section jam produces ≥3 distinct groove
  names"* but asserts `seenNames.size() >= 2` (`:147`) — weakened to the point where the §2.4 collapse
  passes it.
* `tests/test_processor_pipeline.cpp:1138` pins "transition does not cut short", contradicting the
  shipped user-facing contract.
* `tests/test_pattern_player.cpp:148` renders one giant block, so §2.1 and §2.12 are invisible.
* No test ever moves the playhead backwards, so §2.2 is invisible.

All 227 unit cases and 56 integration cases pass. **Green tests are currently not evidence of
playability.**

---

### 2.23 Dead code, races and doc drift — **cleanup**

| Item | Where | Note |
| --- | --- | --- |
| `emitBassRange` / `emitPatternBass` | `PatternPlayer.cpp:647-760` | no call site (§2.5) |
| `snapBassToSectionHarmony` | `PatternPlayer.cpp:855-890` | no call site |
| `lastRiffMatchSample` | `AccompanimentProcessor.cpp:1019,1240` | write-only; the documented "returning to the riff extends the hold" is unimplemented |
| `GrooveCommit::fillKind` / `TransitionFillKind` | `PatternPlayer.h:36-49` | set to `None` by both producers and never read |
| `setMirrorWhileHeld` / `releaseForTransition` | `PhraseLearner.h:171-185` | no-op stubs, zero callers, still documented in CHANGELOG 0.9.59 |
| `pickPoolPattern`, `barsPerGrooveForSection` | `pattern_rules.h:455-502` | never called |
| `rmsDelta` | `AccompanimentProcessor.cpp:566,668` | computed, published, never consumed |
| `static bool lastLoopValue` | `AccompanimentProcessor.cpp:636` | function-local `static` mutated from the audio thread and **shared by all plugin instances** — a data race; make it a member |
| Non-atomic UI reads of lock state | `AccompanimentProcessor.h:130-179, 153-156` | reads `riffA`/`riffB`/`enginePhase` while the audio thread writes them → possible torn 64-slot snapshot; publish an immutable snapshot behind an atomic pointer |
| `pendingLearned_` overrun | `PatternPlayer.cpp:143` | `pendingLearned_.back() = note` silently drops an already-queued note when > 8 are queued |
| `ARCHITECTURE.md` | `OnnxInference` section | describes `src/inference/OnnxInference.h/.cpp`, which do not exist (the implementation is `MetalGrooveInference`) |

---

## 3. Playability improvements (prioritised)

**P0 — make the kit answer the player, and survive a loop**

1. Fix the lock/transition clock: loop-relative emitter phase + re-anchor on a host jump (§2.2). This
   alone restores the A-B-A-C-A cycle under a DAW loop.
2. Restore a bounded fast path: on `|rmsDelta| > 0.6`, commit at the next **beat**; default hold 1 bar
   (§2.7).
3. Re-enable the variety draw or the bar-phase rotation in Play so Play stops being a two-pattern loop
   (§2.4).
4. Fix the split-block time base — it glitches the exact block where the groove changes (§2.12).

**P1 — respond to gestures you already make**

5. Same-riff cut-short out of a transition (§2.8).
6. Unfreeze `RiffBListen` drums (follow the contrast pool), lower the lock threshold, and allow
   drift-unlock while held so a stuck riff can escape without Forget (§2.8).
7. Relax the attack detector so fast chugs register (§2.17).
8. Stop the 1 s guitar-stop gate from killing a locked accompaniment (§2.9).
9. Clear armed crash/fill/lead-in state in the silent and seek branches (§2.13, §2.14).

**P2 — make the MIDI correct and reproducible**

10. Deferred drum note-offs (§2.1) **with** seek-safe release (§2.14).
11. Deterministic humanisation: seed the groove RNG from the host position instead of
    `setSeedRandomly()` (`PatternPlayer.cpp:60`), so bounces are reproducible and drum feel no longer
    changes with block size or bass activity.
12. Fix the learned-bass octave fold (§2.15) and the downbeat suppression (§2.16).

---

## 4. Musicality improvements (prioritised)

**P0 — dynamics and articulation**

1. Fix the velocity saturation (§2.3). Acceptance: for a Metal chorus bar at `guitarEnergy = 1.28`,
   < 25 % of note-ons at 127 and velocity standard deviation > 12.
2. Re-derive the ghost threshold against the library (62, as documented) so pattern 20's ghosts
   actually whisper (§2.10).
3. Median-centre the derived microtiming, keep the residual small, and put it behind a "humanize"
   knob (§2.10). Verify against a click: a straight pattern should sound neither early nor late.

**P1 — bass as a musical part, not a drone**

4. Reconnect the authored per-pattern bass lines and transpose them to the live root (§2.5) — the
   largest available bass-musicality gain, and the data already exists.
5. Store note lengths in `LearnedRiff` so a locked chord is one note, not eight 16th retriggers (§2.6).
6. Let the bass follow the *player's* rhythm in Play: prefer the live mirror and use the section grid
   only for the gaps. The mirror currently snaps to a 16th only within 30 ms
   (`AccompanimentProcessor.cpp:1528-1539`), so it is either dead-on or a whole block late.

**P2 — arrangement-level feel**

7. Promote ornaments to selected pattern variants (§2.11) so the groove is stable within a phrase.
8. Vary the groove per section *instance*, not per bar: seed the pool rotation from
   `(sectionIndex, sectionInstance)` so verse 1 and verse 2 differ but each verse is internally
   consistent — this is what `pickPoolPattern` already implements.
9. Make fills replace the groove and inherit the humanisation and swing of the bar they end (§2.18).
10. Add one honest "humanize amount" master control that scales timing jitter, velocity jitter, ghost
    density and ornament probability together, instead of five implicit ones.

---

## 5. Tests to add (these would have caught the above)

1. **Transport-loop test** — playhead wraps at a 4-bar loop point mid-lock; assert one bass note per
   occupied 16th and that the transition engages within `lockBars` (§2.2).
2. **Dual-block-size golden test** — 8 bars with a forced mid-block pattern change at 128 / 512 / 2048
   samples; assert absolute event samples match (§2.1, §2.12).
3. **Note-off ledger** — for every note-on assert a matching note-off, and for hits authored
   `durationBeats >= 1` assert the note-off lands in a *later* block (§2.1).
4. **Velocity histogram** — Metal chorus at `guitarEnergy = 1.28`: assert spread, not saturation (§2.3).
5. **Play-mode coverage** — one full Play form, assert each section shows at least N distinct pool
   members (§2.4). Today's e2e test asks for ≥2 and gets it from the collapse.
6. **Latency budget** — large RMS step → committed pattern change within 250 ms (§2.7).
7. **Locked-riff articulation** — record a sustained 2-bar chord, play back, assert O(1) bass notes per
   chord, not one per 16th (§2.6).
8. **Seek/loop while ringing** — assert crash and bass each receive a note-off (§2.14).
9. **Section-end fill** — log `fromNext`, `fillOrigin` and whether the fill landed in the last bar at
   1024/2048-sample buffers and 120/240 BPM (§2.18).

---

## 6. Verification appendix

```bash
# version of the binary you actually played
strings -a ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
  | grep -E '^0\.9\.[0-9]+$'
# → 0.9.62   (source HEAD is 0.9.67)

# both suites pass — that is the point
./build/MetalAccompanimentTests              # 75366 assertions / 227 cases
./build/MetalAccompanimentIntegrationTests   # 1010 assertions / 56 cases
#   [STABILITY] 62 sections, 240s music — Pattern range: 14 – 22

# no call sites in the audible path
grep -rn 'emitBassRange' src/              # definition + declaration only
grep -rn 'pickPoolPattern' src/            # definition only
grep -rn 'snapBassToSectionHarmony' src/   # definition + declaration only

# the "react now" input is write-only
grep -rn 'rmsDelta' src/AccompanimentProcessor.cpp   # 566, 668 — computed, never read
```

**Files of interest**

| Concern | File |
| --- | --- |
| Drum render, humanisation, fills, timing | `src/midi/PatternPlayer.cpp` |
| Groove template data (timing/velocity/ghosts) | `src/midi/GrooveTemplateData.h` |
| Genre presets, section velocity | `src/midi/GrooveTemplate.h` |
| Pattern data (incl. dead authored bass) | `src/midi/MidiPatternLibrary.cpp` |
| Holds, phases, lock clock, bass mixer | `src/AccompanimentProcessor.cpp` |
| Pools, rotation, fills, state compatibility | `src/inference/pattern_rules.h` |
| Attack detection, riff capture, lock | `src/analysis/PhraseLearner.cpp` |
| Mel selector + variety draw | `src/inference/MetalGrooveInference.cpp` |

---

*Prepared from direct source review, `git log`/`git show` archaeology across v0.9.44 → v0.9.67, both
test binaries, and numerical simulation of the riff-lock clock and velocity chain. Read-only — no
source files were modified.*

### Open question for the play-tester

Was a **DAW loop** active during Station B2? That single fact decides whether the observed
`LOCKED RIFF DOESN'T PERSIST` was §2.2 (loop wedged the lock / bass silent before the lock origin) or
§2.8/§2.9 (the lock expired into the contrast section, or the guitar-stop gate cut it). Both are real;
the fix priority is the same either way, but it changes what you should re-test first.
