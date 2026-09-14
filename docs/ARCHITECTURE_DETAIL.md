> **Canonical evidence base.** Every claim carries a `file:line` citation into the working tree
> at v1.0.3 (`5d5f410`). The short, readable version is [`/ARCHITECTURE.md`](../ARCHITECTURE.md).
> Sections §6 (doc-vs-code contradictions) and §8 (undocumented but important) are the most
> valuable parts.

# Verified runtime architecture — fuzzyband (Metal Accompaniment) v1.0.3

**Method.** Every claim below was read out of the source, not out of the docs. Citations are
`path:line` into the working tree at `/Users/ng/projects/fuzzyband`. Line numbers are from the
files as they exist today (v1.0.3, `CMakeLists.txt:4`). Where the code is ambiguous or I could
not statically verify something, that is stated explicitly in §9.

**Version facts.** `project(MetalAccompaniment VERSION 1.0.3)` (`CMakeLists.txt:4`). Bundle id
`com.ng.fuzzyband`, product name `fuzzyband`, `NEEDS_MIDI_OUTPUT TRUE`, `NEEDS_MIDI_INPUT FALSE`
(`CMakeLists.txt:141-158`). Formats: VST3 always; AU + Standalone on Apple
(`CMakeLists.txt:128-140`).

---

## 1. The real thread model

There are exactly **three** threads of interest in the shipping plugin, plus the host's timer
threads. There is no capture thread (see §8.2).

| Thread | Created / owned at | What actually runs there |
|---|---|---|
| **Audio thread** (host callback) | host calls `processBlock` (`AccompanimentProcessor.cpp:724`) | all DSP analysis, **all mel-spectrogram extraction**, `FeatureVector` enqueue, the whole engine state machine, phrase learning, bass mirroring, `PatternPlayer::process`, MIDI output, gain passthrough, display atomics, riff-UI snapshot publish |
| **Background inference thread** | `std::thread` started in the constructor (`AccompanimentProcessor.cpp:139-140`), function `inferenceLoop` (`AccompanimentProcessor.cpp:662-679`) | drains `featureQueue` + `melQueue`, style classification, **ONNX `Run()`** (pattern selection), commits `latestPatternIndex` when not frozen |
| **Message / UI thread** | JUCE editor, `AccompanimentEditor`, `startTimerHz(20)` (`AccompanimentEditor.cpp:547`), `timerCallback` (`AccompanimentEditor.cpp:567`) | draws UI from display atomics + the riff triple buffer + the scope ring; writes `playActive`, riff-capture request atomics, song form, APVTS params |

Notes that matter:

- **`inferencePaused` defaults to `false`** (`AccompanimentProcessor.h:305`). The inference thread
  is *not* idle after construction. `prepareToPlay` sets it `true` at entry
  (`AccompanimentProcessor.cpp:159`) and `false` at exit (`AccompanimentProcessor.cpp:261`), so the
  pause window is inside `prepareToPlay`, not before it.
- The inference thread holds **`inferenceDrainMutex`** (`AccompanimentProcessor.h:307`) around every
  drain (`AccompanimentProcessor.cpp:672-675`). The only other acquirer is the test hook
  `flushBackgroundInferenceForTests` (`AccompanimentProcessor.cpp:686-690`) on the message thread.
  **This mutex is never taken on the audio thread** — the real-time path is mutex-free apart from the
  caveat in §8.7.
- The inference loop sleeps 20 ms per iteration (`AccompanimentProcessor.cpp:677`) → ~50 Hz drain
  cadence, and 5 ms while paused (`AccompanimentProcessor.cpp:668`). Pattern selection therefore
  happens at ~50 Hz, but **style classification is ~2 Hz** because it only runs when a new mel window
  arrives (one 22050-sample window ≈ 0.5 s; `AccompanimentProcessor.cpp:463-476`).
- ONNX sessions are created in the **constructor**, on whichever thread instantiates the plugin
  (message thread in practice): `inference(makeInference())` is a member initialiser
  (`AccompanimentProcessor.cpp:135`) and `makeInference()` calls `tryLoadModel()`
  (`AccompanimentProcessor.cpp:19-34`). The inference thread starts afterwards
  (`AccompanimentProcessor.cpp:140`).

### 1.1 Handoff primitives (the real ones)

| Data | Direction | Mechanism | Citation |
|---|---|---|---|
| `FeatureVector` | audio → inference | `moodycamel::ReaderWriterQueue<FeatureVector> featureQueue{4096}`; audio does `(void)try_enqueue(fv)` — **failure is ignored, not "always succeeds"** | `AccompanimentProcessor.h:279`, enqueue `AccompanimentProcessor.cpp:893`, drain-all-keep-newest `AccompanimentProcessor.cpp:440-447` |
| mel window (2048 floats) | audio → inference | `ReaderWriterQueue<MelWindow> melQueue{32}` (also drained keep-newest) | `AccompanimentProcessor.h:283-285`, enqueue `AccompanimentProcessor.cpp:771`, drain `AccompanimentProcessor.cpp:463-469` |
| groove commit | inference → audio | `ReaderWriterQueue<GrooveCommit> grooveCommitQueue{32}` — **drained and discarded**; see §8.4 | `AccompanimentProcessor.h:280`, enqueue `AccompanimentProcessor.cpp:624`, discard `AccompanimentProcessor.cpp:1205-1209` |
| pattern index | inference → audio | `std::atomic<int> latestPatternIndex`, release store / acquire load | `AccompanimentProcessor.h:288`, store `AccompanimentProcessor.cpp:619`, load `AccompanimentProcessor.cpp:896` |
| pattern-select freeze | audio → inference | `std::atomic<bool> patternSelectFrozen` | `AccompanimentProcessor.h:290`, store `AccompanimentProcessor.cpp:1142`, read `AccompanimentProcessor.cpp:482` |
| B-lock one-shot request | audio → inference | `requestBLockPick` / `bLockPick` atomics | `AccompanimentProcessor.h:366-367`, request `AccompanimentProcessor.cpp:1786`, fulfil `AccompanimentProcessor.cpp:486-500`, consume `AccompanimentProcessor.cpp:1652` |
| display values | audio → UI | plain `std::atomic` scalars (bpm, state, pattern, style, rms, centroid, hfFlux, noiseFloor) | `AccompanimentProcessor.h:295-302`, stores `AccompanimentProcessor.cpp:1979-1985` |
| riff A/B + phase snapshot | audio → UI | 3-slot triple buffer `riffUiSlots` + `riffUiPublished`/`riffUiReading`; reader pins a slot (`RiffUiRead`) | `AccompanimentProcessor.h:328-351`, writer `AccompanimentProcessor.cpp:264-281`, reader ctor/dtor `AccompanimentProcessor.cpp:283-305` |
| input scope ring | audio → UI | **non-atomic** `std::array<float,16384> scopeSamples` + relaxed `scopeWriteIndex` (see §8.8) | `AccompanimentProcessor.h:470-471`, write `AccompanimentProcessor.cpp:745-753`, read `AccompanimentProcessor.cpp:2387-2401` |
| riff capture commands | message → audio | `riffCaptureStart` / `riffCaptureStop` / `riffForget` atomics, consumed with `exchange` | `AccompanimentProcessor.h:387-389`, consumed `AccompanimentProcessor.cpp:1369`, `1405`, `1549` |
| Play button | message → audio | public `std::atomic<bool> playActive` | `AccompanimentProcessor.h:206`, write `AccompanimentEditor.cpp:412`, read `AccompanimentProcessor.cpp:928` |
| song form | message → audio | `std::shared_ptr<SongForm> pendingSongForm` + `songFormVersion` int, exchanged with **`std::atomic_load` / `std::atomic_store` free functions** (see §8.7) | `AccompanimentProcessor.h:479-481`, publish `AccompanimentProcessor.cpp:2416-2419`, consume `AccompanimentProcessor.cpp:838-845` |
| APVTS params | message ↔ audio | `apvts.getRawParameterValue(...)->load()` on the audio thread; editor writes through APVTS | read sites `AccompanimentProcessor.cpp:819`, `902`, `907`, `912`, `919`, `1219`, `1719`, `1733` |

---

## 2. One audio block, entry to MIDI output

`AccompanimentProcessor::processBlock` (`AccompanimentProcessor.cpp:724-2035`), audio thread
throughout. Ordered:

| # | Line(s) | Step | Notes |
|---|---|---|---|
| 1 | 726 | `juce::ScopedNoDenormals` | |
| 2 | 727 | `midi.clear()` | output buffer |
| 3 | 729-732 | size guard; `in = buffer.getWritePointer(0)` | mono in, stereo out (`isBusesLayoutSupported`, 150-155) |
| 4 | 735-740 | scrub non-finite → 0; `FloatVectorOperations::clip(in,in,-2,2)` | **in place** — the "dry passthrough" is the clipped signal |
| 5 | 745-753 | display scope: every 8th sample into `scopeSamples`, `scopeWriteIndex.fetch_add(relaxed)` | |
| 6 | 755-756 | `outL`/`outR` pointers | |
| 7 | 759 | `energyAnalyser.process(in, numSamples)` | RMS(0.1 s), onset RMS(0.02 s), centroid, HF flux, sub-bass ratio, peak RMS (`EnergyAnalyser.cpp:23,30`) |
| 8 | 760 | `pitchEstimator.process(in, numSamples)` | YIN, ring 4096 (`PitchEstimator.h:34`) |
| 9 | 761 | `audioRingBuffer.write(in, numSamples)` | SPSC ring, tripled buffer (`AudioRingBuffer.cpp:7`) |
| 10 | **764-773** | **if `audioRingBuffer.isWindowReady()`: `readWindow` → `melExtractor.process(melScratch, mw)` → `melQueue.try_enqueue(mw)`** | **~40 × 2048-pt FFT + the 64×32×1025 mel filterbank, on the audio thread.** Happens once per 22050 samples. See §8.1 |
| 11 | 775-785 | read rms/centroid/hfFlux/subBassRatio; `rmsDelta = (rms-prevBlockRms)/prevBlockRms` guarded at `>1e-3`, else 0; `prevBlockRms = rms` | |
| 12 | 786 | `structureTagger.setSubBassRatio(subBassRatio)` | |
| 13 | 790-792 | `noteRinging = phraseLearner.hasRecentAttack(hostSampleTime, 4 s)` | |
| 14 | 793-794 | `st = structureTagger.update(rms, centroid, hfFlux, numSamples, peakRms, noteRinging)` | 6-arg signature (`StructureTagger.h:36`) |
| 15 | 796-797 | `digitalSilence = rms < 1e-6`; `sr = cachedSampleRate` | |
| 16 | 803-823 | BPM: playhead `getBpm()`, `getTimeInSamples()`, `getIsPlaying()||getIsRecording()`; fallback APVTS `bpm`; fallback 120 | host-authoritative |
| 17 | 831 | `clockSample = patternPlayer.previewResolvedHostSample(rawHostPos, numSamples, hostRolling)` | **transport frame**; frozen-transport aware |
| 18 | 837-856 | song-form version check → `structureSequencer.loadForm`; `loop` param → `setLooping` | |
| 19 | 859-866 | `playbackGate.update(st, numSamples, sr)` → on `resetTrackers` set `resetDrumHoldRequested`; on `armCrash` `patternPlayer.armTransitionCrash()` | |
| 20 | 869-893 | build `FeatureVector` (bpm, rms, centroid, hfFlux, state, `sampleTimestamp=hostSampleTime`, pitchRootMidi, pitchConfidence, rmsDelta, policyIntensity=0.5, subBassRatio, onsetDensityPerBeat, onsetIoiBeats) → `(void)featureQueue.try_enqueue(fv)` | `FeatureVector.h:16-48` |
| 21 | 896 | `patternIdx = latestPatternIndex.load(acquire)` | |
| 22 | 897-926 | `patternPlayer` params: bpm, genre preset, swing, humanize, bass semitone offset | |
| 23 | 928-966 | Play edge: on rising edge → reset riffs, `setAutoLockEnabled(false)`, `enginePhase = PlayCountIn`, `setBeatGridBassEnabled(true)`; on falling edge cancel count-in | |
| 24 | 970-993 | Play count-in: wait for next bar, click for 1 bar (4 beats) → `structureSequencer.reset()`, `enginePhase = PlaySection` | |
| 25 | 995-1002 | `playOn = playRequested && !playCountInActive`; if form complete → `playActive=false`, `playEndedThisBlock=true` | |
| 26 | 1004-1077 | Play branch: section-pool rotation (`pickPoolPattern`, `barsPerGrooveForSection`), section-entry re-seed, outgoing fills (`updateOutgoingFill` / `playLastBarOriginBeat`) → `effectivePatternIdx` | |
| 27 | 1082-1120 | non-Play: `RiffBListen` rotates `transitionPool`; `RiffBLocked` uses `drumB`/`drumB0`; `RiffA` uses `drumA` | |
| 28 | 1121-1137 | `wasPlayOn = playOn`; follow-mode section seed from structure-state index | |
| 29 | 1138-1143 | `patternSelectFrozen.store(freezeSelect, release)` — frozen in every phase except Idle / PlaySection / RiffBListen | |
| 30 | 1144 | `patternPlayer.setPatternIndex(effectivePatternIdx)` | |
| 31 | 1147-1155 | gesture (|rmsDelta| > 0.6 in Play) → `queueGrooveCommit({idx, alignToBeat=true})` | audio-thread commit; **the only commit that takes effect** |
| 32 | 1157-1166 | section for velocity contrast + bass harmony → `patternPlayer.setSection(...)` | |
| 33 | 1168-1177 | ~500 ms RMS swell → `patternPlayer.setGuitarEnergy(...)` | |
| 34 | 1185-1199 | `armActive` (playOn ‖ count-in ‖ capture ‖ grooveLocked ‖ transition); `trulySilent`; decrement `debugPreviewSamplesRemaining`; `setClickTrack`; `setStructureSilent` | |
| 35 | 1205-1209 | drain `grooveCommitQueue` and **throw the result away** (`(void)gotCommit`) | §8.4 |
| 36 | **1215-1941** | the engine core: stable-pitch update, loop-wrap re-anchor, scope playhead, transition-cycle helpers, riff capture state machine, generative groove lock, post-lock transition hold, `emitFrozenRiff`, live mirror trigger | §3, §4 |
| 37 | **1943-1944** | `patternPlayer.process(midi, numSamples, rawHostPos, hostRolling)` | emits all drum + bass MIDI |
| 38 | 1947-1955 | `gain = outputGain` (0..2); `outL[i] = outR[i] = in[i]*gain` | mono → both channels |
| 39 | 1957 | `hostSampleTime += numSamples` | monotonic plugin clock |
| 40 | 1959-1976 | re-`setPatternIndex` for RiffBListen/RiffBLocked/RiffA **after** `process()` | takes effect next block |
| 41 | 1979-1985 | display atomics (bpm, rms, centroid, hfFlux, noiseFloor, state, `armActive ? effectivePatternIdx : 0`) | |
| 42 | 1987-2030 | unified section progress (Play / Transition / Lock) → `sectionPhase/Bar/BarsTotal/BarsRemaining/Progress` | |
| 43 | 2034 | `publishRiffUiSnapshot()` | |

**Inside `PatternPlayer::process`** (`PatternPlayer.cpp:1279-1595`), still audio thread:

1. 1298-1307 frozen-transport resolution (re-derived independently of `previewResolvedHostSample`).
2. 1335-1350 seek/loop detection → `transportJumped_`, flush note-offs, drop pending state.
3. 1356-1371 silence path: flush note-offs, all-notes-off once, clear pending, return.
4. 1386-1390 click-track path (count-in / riff record).
5. 1392-1453 pending pattern commit resolved at the next bar line, or next beat when
   `alignToBeat` (`quant = 1.0`, 1398-1400). Groove is emitted in two ranges around `changeBeat`.
6. 1458-1475 armed crash (`armCrashPending`), deduped via `patternCrashesNear`.
7. 1477-1491 bar fills and the Tier-0 micro-fill.
8. 1503-1520 section-handoff bass pickup (`bassLeadInArmed`, "and of 4").
9. 1525-1550 learned/mirrored bass notes: sorted by offset, `emitBassNote(forceRetrigger=true)`,
   then `mirrorVoiceEndSample_` extended.
10. 1561-1581 grid bass (`emitBassRange`) with `suppressBeforeAbs = mirrorVoiceEndSample_`.
11. 1585-1591 close a ringing bass note; 1594 `flushDueDrumNoteOffs`.

---

## 3. Engine phase / state machine

The engine is **four nested state variables**, not one enum. All four live on the audio thread as
plain members; only their UI projections are atomic.

### 3.1 `EnginePhase` — the real enum

`AccompanimentProcessor.h:316-320`:

```cpp
enum class EnginePhase {
    Idle, PlayCountIn, PlaySection,
    RecWaitBar, RecCountIn, RecCapture,
    RiffA, RiffBListen, RiffBLocked
};
```

Nine phases. There is no `TransitionHold` phase in this enum — the post-lock transition is a
separate `PostLockPhase` (`AccompanimentProcessor.h:408`), and the UI-facing
`SectionPhase` (`AccompanimentProcessor.h:176`) is a third, purely-derived four-value projection
(`Idle=0, Play=1, Lock=2, Transition=3`).

### 3.2 Every transition, with the real condition

| From → To | Condition (code) | Citation |
|---|---|---|
| * → `Idle` | reset in `prepareToPlay` | 184 |
| `Idle` → `PlayCountIn` | Play rising edge: `playRequested && !wasPlayOn && !playCountInActive` | 929-946 |
| `PlayCountIn` → `PlaySection` | crossed the bar-aligned count-in start + 4 beats; also `structureSequencer.reset()` | 985-992 |
| `PlayCountIn` → (cancel) | Play released: `playCountInActive=false` | 961-966 |
| `PlaySection` → `Idle` | `structureSequencer.isComplete()` after `advance` (form finished, non-looping) → `playActive=false`, `playEndedThisBlock=true` | 1018-1025 |
| `Idle` → `RecWaitBar` | `riffCaptureStart.exchange(false)` (from `requestRiffCaptureStart`) → `phraseLearner.beginGridCapture()`, `setClickTrack(true)`, `setBeatGridBassEnabled(false)` | 1405-1435 |
| `RecWaitBar` → `RecCountIn` | next bar line crossed | 1445-1454 |
| `RecCountIn` → `RecCapture` | count-in start + 4 beats | 1456-1461 |
| `RecCapture` → `RiffA` | `finishRiffCapture()`: `commitGridCapture()` ok (≥2 occupied slots) → `exportPattern(riffA)` → `enterRiffA()` | 1350-1367, 1494-1495, 1321-1348 |
| `RecCapture` → `Idle` | `commitGridCapture()` fails, or Stop with <2 occupied slots → `abortRiffCapture()` | 1365-1366, 1549-1557, 1305-1318 |
| `* Rec*` → `Idle` | `riffForget` or `playEndedThisBlock` → abort + full wipe | 1369-1403 |
| `RiffA` → (lock expiry) | `hostSampleTime >= grooveLockEndMono` → `grooveLockActive=false`, `grooveLockReleaseArmed=true` | 1640-1644 |
| `(armed)` → `RiffBListen` | `grooveLockReleaseArmed`: pick/pin contrast slot (`pickNextSectionAfterLock`, `contrastHomePattern`), `beginLiveGridListen`, `setMatchReference(riffA)`, `enginePhase = RiffBListen`, `requestBLockPick=true`, `postLockPhase = TransitionHold` | 1713-1803 |
| `RiffBListen` → `RiffBLocked` | `phraseLockEdge` (`phraseLearner.isLocked()` rising edge) and `riffB.valid` → `drumB` from `bLockPick`/`lastBListenPoolPattern`/`drumB0`, `cancelLiveGridListen()`, `setBeatGridBassEnabled(false)` | 1646-1666 |
| `RiffBLocked` → `RiffBListen` | `!phraseLearner.isLocked()` (drift unlock) → `setAutoLockEnabled(true)`, `setBeatGridBassEnabled(true)`, `riffB = {}` | 1667-1675 |
| `TransitionHold` → `Idle` (+ `enterRiffA`) | `hostSampleTime >= transitionEndMono` → `postLockPhase = Idle`, `transitionSectionActive=false`, `cancelLiveGridListen`, `armTransitionCrash`, `enterRiffA()` | 1828-1839 |
| `TransitionHold` shortened | same riff returned: `sameRiffMatchCount >= 4` ∧ `lastRiffMatchSample > transitionStartMono` ∧ `(lastRefMatchMono - firstRefMatchMono) >= 1 beat` → snap `transitionEndMono` to the next bar | 1806-1827 |
| `*` → `Idle` | `playEndedThisBlock` (Play finished) also clears `playSectionIndex` | 1374-1403 |
| Play turned on during any other phase | wipes riffs / capture / transition, `enginePhase = PlayCountIn` | 929-960, 1602-1620 |

### 3.3 Derived flags (all recomputed at 1677-1684 every block)

```cpp
grooveLocked.store(enginePhase == EnginePhase::RiffA, release);   // 1677
riffLoopActive = (RiffA || RiffBListen || RiffBLocked);           // 1678-1680
grooveLockActive = (enginePhase == EnginePhase::RiffA);           // 1681
phraseLearner.setHoldActive(RiffA || RiffBLocked);                // 1682-1683
riffHeld.store(riffA.valid || phraseLearner.isLocked(), release); // 1684
```

So `isGrooveLocked()` (UI) means "engine is in `RiffA`". `RiffBListen`/`RiffBLocked` are *not*
reported as "locked" by that getter, but they *are* `riffLoopActive`.

### 3.4 The two other sub-state machines

- `RiffCapturePhase { Idle, WaitBar, CountIn, Recording }` — `AccompanimentProcessor.h:395`, driven
  at 1445-1461. `riffCaptureBar` is published 0 during count-in, 1..4 while recording (1488-1490).
- `PostLockPhase { Idle, TransitionHold }` — `AccompanimentProcessor.h:408`. `TransitionHold` is
  entered at 1759 and exited at 1830; it always ends by re-entering `RiffA` (1838), never by
  returning to follow mode. `transitionSections` (APVTS, default 2, range 1..4,
  `AccompanimentProcessor.cpp:122-125`) controls how many distinct contrast families are rotated
  (`A→B→A→C→A…`), and each slot is pinned on first visit
  (`transitionSlotNames`/`transitionSlotPinned`, 1740-1757).

### 3.5 Pattern-ownership rules (which subsystem picks the drums)

| Playback mode | Drum pattern owner | Citation |
|---|---|---|
| `PlaySection` | audio thread section pool (`orderedSectionPatternPoolForGenre` → `pickPoolPattern`, 2 bars/phrase, 4 for BREAKDOWN/INTRO/OUTRO) | 1029-1075, `pattern_rules.h:471-478` |
| `RiffBListen` | audio thread transition pool | 1082-1109 |
| post-lock `TransitionHold` | same pool, re-applied after `process()` | 1959-1963 |
| `RiffBLocked` | frozen `drumB` (fallback `drumB0`) | 1110-1116, 1964-1971 |
| `RiffA` | frozen `drumA` | 1117-1120, 1972-1976 |
| Idle / follow | inference thread writes `latestPatternIndex` (Mel-CNN argmax constrained to a section pool) | 618-624, 530-555 |

### 3.6 Section / form state

`StructureSequencer` (plain members, audio thread) walks `SongForm` sections on bar boundaries
(`structureSequencer.advance`, 1017). The form arrives lock-free as a parsed `SongForm` shared_ptr
(§1.1). Default form when no custom form is persisted:
`"INTRO:4,VERSE:8,CHORUS:8,VERSE:8,CHORUS:8,OUTRO:4"` (`AccompanimentProcessor.cpp:2453`).

---

## 4. The mirroring / live-bass data path

"A guitarist's attack becomes a bass note." Every step is on the **audio thread**; nothing about the
live mirror crosses a thread boundary.

### 4.1 Call chain, in order

| Step | Function | Citation | What it does |
|---|---|---|---|
| 1 | `AccompanimentProcessor::processBlock` — scrub + clip | `AccompanimentProcessor.cpp:735-740` | input cleaned |
| 2 | `EnergyAnalyser::process` | `AccompanimentProcessor.cpp:759` → `EnergyAnalyser.cpp:117-119` | produces the **20 ms onset RMS** (`getOnsetRmsEnergy`, `EnergyAnalyser.h:35`), explicitly a separate fast window from the 0.1 s structure RMS (`EnergyAnalyser.cpp:23,30`) |
| 3 | `PitchEstimator::process` | `AccompanimentProcessor.cpp:760` | YIN MIDI + confidence |
| 4 | `silentNow` / `digitalSilence` | `AccompanimentProcessor.cpp:1216`, `796` | gates the learner |
| 5 | `StablePitchTracker::update` | `AccompanimentProcessor.cpp:1229-1234` | returns a **pitch-class offset 0..11** or `INT_MIN`; the processor falls back to `getLastPitchClassOffset()` (`1235-1237`). Result `pcForBass` |
| 6 | `PhraseLearner::process(hostSampleTime, onsetRms, pitchMidi, pitchConf, bpm, numSamples, pcForBass)` | `AccompanimentProcessor.cpp:1530-1540` | the actual "is that an attack?" decision |
| 6a | ↳ `PhraseLearner::detectAttack(rms)` on the **onset RMS** | `PhraseLearner.cpp:584` | rise edge → `risePending_` |
| 6b | ↳ attack accepted if `canAttack && risePending_`; `canAttack = (sampleTime - lastAttackSample_) > kMinAttackIntervalSamples` | `PhraseLearner.cpp:583-587` | one attack per decay→rise |
| 6c | ↳ `resolveBassNote(attackPitch, stablePitchClassOffset)` | `PhraseLearner.cpp:605` (`resolveBassNote` at `235-246`) | maps pitch class into the bass register |
| 6d | ↳ **`result.trigger = true`** only when `state_ != State::Locked && !userCapturing_ && !gridCapturing_` | `PhraseLearner.cpp:691-696` | the live mirror is a *Learning-state* feature |
| 6e | ↳ `justMatched_` / `justMatchedRef_` computed from the last IOI vs the learned/listed intervals | `PhraseLearner.cpp:632-676` | `justMatchedRiff()` = `justMatched_` (`PhraseLearner.h:218`) |
| 7 | `shouldTrigger = bassNote.trigger && !guitarStopped` | `AccompanimentProcessor.cpp:1904-1906` | `guitarStopped` = 1 s of guitar silence (`1896-1900`) |
| 8 | branch on phase | `AccompanimentProcessor.cpp:1907-1940` | see §4.2 |
| 9 | `PatternPlayer::triggerLearnedBassNote(note, vel, offset, duration)` | `AccompanimentProcessor.cpp:1939` → `PatternPlayer.cpp:151-173` | folds note into [28,55], stores a `PendingLearnedNote` in a fixed 8-slot queue; **if full it drops the newest** (`170-172`) |
| 10 | `PatternPlayer::process` — learned-note block | `PatternPlayer.cpp:1525-1550` | sorts by offset, `emitBassNote(..., forceRetrigger=true)`, then extends **`mirrorVoiceEndSample_`** (`1546-1548`) |
| 11 | `PatternPlayer::emitBassNote` | `PatternPlayer.cpp:1035-1080` | monophonic: closes the previous note, `noteOn` on **channel 2**, schedules/flushes `noteOff` |
| 12 | `PatternPlayer::emitBassRange` | `PatternPlayer.cpp:1561-1580` | grid bass with `suppressBeforeAbs = mirrorVoiceEndSample_` |

### 4.2 The three branches at step 8

```
if (enginePhase == RiffA)            emitFrozenRiff(riffA, riffAPlayOriginMono, ...)   // 1907-1913
else if (enginePhase == RiffBLocked) emitFrozenRiff(riffB, riffBPlayOriginMono, ...)   // 1914-1919
else if (shouldTrigger && !riffCaptureActive
         && !phraseLearner.isGridCapturing() && listenBass)                            // 1920-1922
        triggerLearnedBassNote(note + bassTranspose, mirrorVel, offset, duration)      // 1939
```

- `listenBass = (enginePhase == PlaySection || enginePhase == RiffBListen)`
  (`AccompanimentProcessor.cpp:1871-1872`), and that value is pushed into
  `patternPlayer.setBeatGridBassEnabled(listenBass)` (`1873`).
- **16th-note snap**: `durationSamples = 0.85 * samplesPerBeat` (`1903`); the offset is the delta to
  the nearest 16th grid line only if `|delta| <= min(30 ms, 16th*0.5)` **and** the grid point is in
  the future and inside this block (`1925-1938`). Otherwise offset 0 — the code deliberately never
  chases the next 16th (comment at 1937).
- `emitFrozenRiff` (`AccompanimentProcessor.cpp:2313-2371`) converts the 64-slot `LearnedRiff` into
  notes by absolute-sample arithmetic — `loopSamples = round(lenBeats * spb)`, per-slot
  `k` = smallest integer with `origin + slotOffset + k*loopSamples >= clockSample` (`2357-2367`) —
  specifically to be buffer-size invariant (comment 2327-2333, review T9.2). It emits with
  hard-coded velocity `0.58` (`2369`).
- Note the parameter is named `clockSample` but the call site passes **`hostSampleTime`**
  (monotonic frame, `1912`/`1917`), matching `riffAPlayOriginMono` (`2118-2125`, derived from
  `lockOriginMono` in the monotonic frame). Only block-relative deltas are used, so this is correct
  but easy to misread.

### 4.3 How the mirror and the grid/harmony bass line interact

The bass voice is **monophonic and arbitrated by `mirrorVoiceEndSample_`**
(`PatternPlayer.h:436-440`, `PatternPlayer.cpp:1546-1548`, `1567`, `1131-1132`, `1227-1228`):

1. Every emitted mirrored/learned note extends
   `mirrorVoiceEndSample_ = max(mirrorVoiceEndSample_, sampleCounter + offset + duration)`.
2. The grid path (`emitBassRange` → `emitPatternBass` if `pattern.bassEvents` is non-empty, else
   `emitHarmonicBass`) is only reached at all when `beatGridBassEnabled_` is true (`1561`).
3. Inside each grid emitter, an event is skipped when
   `suppressBeforeAbs >= 0 && absSample < suppressBeforeAbs` (`1131-1132`, `1227-1228`).
4. Consequence: **the live mirror is the primary bass part; the authored/harmonic grid is a
   gap-filler that only sounds between mirror notes.** The comments say this explicitly at
   `1557-1560` and `PatternPlayer.h:436-439`.
5. Conversely, on a grid hit that does land, `emitBassNote(..., forceRetrigger=true)`
   (`1155`, `1236`) closes a still-ringing mirror note at the new onset (`T5.3`), so a pickup cannot
   swallow the next downbeat.
6. `emitPatternBass` (authored `bassEvents`) transposes the authored interval pattern
   (`ev.note - kPatternBassRoot` where `kPatternBassRoot = 36`, `PatternPlayer.h:484`) onto
   `bassRootMidi + bassSemitoneOffset` and folds it into [28,55]
   (`PatternPlayer.cpp:1136-1140`). `emitHarmonicBass` instead walks a fixed degree table per
   section — Chorus/Solo `{0,7,12,5}`, Verse root (fourth on beat 3 of every 4th bar), everything
   else root (`harmonyDegree`, `1240-1261`). **There is no chord/key inference**; "harmony" is
   root + fixed intervals.
7. `patternPlayer.setBassParams(bassRoot, notesPerBar)` is set every block from the tracked pitch
   class (`1875-1888`): root folded into [28,55], `notesPerBar` 4 for CHORUS/SOLO, else 2.
8. `justMatchedRiff()` does **not** gate the mirror. It is consumed at
   `AccompanimentProcessor.cpp:1591-1596` (outside `RiffBListen`/`RiffBLocked`) purely to stamp
   `lastRiffMatchSample`, which later feeds the transition cut-short test at `1813-1817` — which
   actually reads `justMatchedReference()` (`1576-1583`), the riff-A matching variant. In other
   words the T6.2 "same riff returned" logic is driven by `justMatchedReference`, and
   `justMatchedRiff` only feeds the older `lastRiffMatchSample` bookkeeping.

---

## 5. Verified supporting subsystems

| Subsystem | Real facts | Citation |
|---|---|---|
| `EnergyAnalyser` | 0.1 s RMS window; **separate 0.02 s onset window**; FFT 1024, hop 256; adaptive noise floor; sub-bass 30–120 Hz ratio | `EnergyAnalyser.cpp:23,30`, `EnergyAnalyser.h:50,83` |
| `StructureTagger` | `enum class StructureState { SILENT, SOFT, LOUD }`. **Centroid is ignored** (`float /*centroid*/`). Adaptive silence floor `max(0.012, min(noiseFloor*1.5, 0.06))`; LOUD via `min(kLoudRms=0.45, max(kLoudRmsFloor=0.20, peak*0.40))`; sub-bass ratio can force LOUD/SOFT. Holds: SILENT 0 s, SOFT→LOUD 0.4 s, SOFT→SILENT 1.0 s, LOUD→SOFT 2.0 s, LOUD→SILENT 1.0 s | `StructureTagger.h:14-19,64-85`, `StructureTagger.cpp:11-39` |
| `PlaybackGate` | Two outputs only (`armCrash`, `resetTrackers`). A silence shorter than **8 s** (`kPhraseBreathHoldSec`) keeps state and arms a crash on re-entry; longer requests a full reset | `PlaybackGate.h:22-26,46`, `PlaybackGate.cpp:10-47` |
| `PhraseLearner` | Auto-locks after `attackCount_ >= 4` and a matched repeat finds `len` in `[2, min(16, attackCount_/2)]`; drift-unlock after `kDriftUnlockBars` of non-matching attacks; `justMatched*` are edge flags cleared every block | `PhraseLearner.cpp:608-609,713-725,770-783`, `PhraseLearner.h:68-79` |
| `MidiPatternLibrary` | **28** patterns (0..27), `kPatternCount = 28`, built at runtime into `std::vector<MidiPattern>` — not `constexpr`. 27 of 28 patterns assign a `bassEvents` vector (patterns 11, 16, 18, 19 have `{}`, so the harmonic engine runs for them) | `MidiPatternLibrary.h:40,48`, `MidiPatternLibrary.cpp:972-999` |
| `MidiEvent` / `MidiPattern` | `MidiEvent` also carries `bool isGhost`; `MidiPattern` is `{name, lengthInBars, drumEvents, bassEvents}` | `MidiPatternLibrary.h:12-28` |
| Channels | Drums **ch 10**, bass **ch 2** (`kBassChannel = 2`, `kDrumChannel = 10`) | `PatternPlayer.h:239,480` |
| `MetalGrooveInference` | Loads `BinaryData::metal_groove_onnx`; requires exactly **1 input** and **≥2 outputs**; input name `"mel"`, outputs `"bottleneck"` + `"style_logits"`; validates input shape `[batch,1,64,32]`; optional second session from `BinaryData::style_cnn_onnx` preferred for `classifyStyle()` | `MetalGrooveInference.cpp:112-179,194-225,392-407` |
| Pattern selection | `bottleneck` → cosine similarity against **28** centroids of dim **128**; seed < 0 = argmax; seed ≥ 0 = softmax-weighted draw over top-3 (K=3, temp 14) | `MetalGrooveInference.cpp:286-383`, `pattern_embeddings.h:18-19` |
| Fallback | `makeInference()` returns `MetalGrooveInference` if the load succeeds, else `RuleBasedInference` **after a `jassertfalse`**; there is no second ONNX model | `AccompanimentProcessor.cpp:19-34` |
| Editor | 20 Hz `Timer`; reads only public getters / atomics / `copyScopeSamples`; writes `playActive`, riff request atomics, song form, params | `AccompanimentEditor.cpp:412-434,547,567-667` |
| ONNX thread | `Ort::Session::Run` is called only from `drainFeatureQueueAndRunInference` (inference thread) at `494` and `530` | `AccompanimentProcessor.cpp:486-500,520-533` |
| Build switches | `MA_ENABLE_ONNX` default **ON**, `MA_BUNDLE_GROOVE_RENDERER` default **ON**, `MA_BUILD_TESTS` ON, `MA_BUILD_STANDALONE` ON. `ONNXRUNTIME_ROOT` unset with ONNX on = `FATAL_ERROR`. Bundled BinaryData: `metal_groove.onnx`, `style_cnn.onnx`, `groove_renderer.onnx` (+ forest.png, 5 fonts) | `CMakeLists.txt:17-20,88-113` |

---

## 6. Doc-vs-code contradiction table

Legend: **doc** = citation into the doc under test; **code** = citation that contradicts it.

### 6.1 `ARCHITECTURE.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| A1 | `ARCHITECTURE.md:8` | "updated to reflect … **feature capture** …" (implied runtime subsystem) | `CMakeLists.txt:163-185` (plugin sources) has no `src/capture/…`; `FeatureCapture.cpp` is compiled only into the unit-test binary at `CMakeLists.txt:308` | Feature capture is not part of the plugin binary |
| A2 | `ARCHITECTURE.md:154` | `enum class StructureState { SILENT, VERSE, CHORUS, BREAKDOWN };` | `StructureTagger.h:14-19` | Real enum is `{ SILENT, SOFT, LOUD }` |
| A3 | `ARCHITECTURE.md:159-164` | transitions keyed on `rmsEnergy < 0.05`, `centroid < 1200 Hz`, `centroid < 2400 Hz`, `centroid >= 2400 Hz` | `StructureTagger.cpp:11-39`; `centroid` is an unused parameter (`float /*centroid*/`) | No centroid logic at all; SILENT = below adaptive floor; LOUD = `min(0.45, max(0.20, peak*0.40))` or high sub-bass; else SOFT |
| A4 | `ARCHITECTURE.md:166` | "Hysteresis: minimum 2 seconds in any state" | `StructureTagger.h:77-81`, `StructureTagger.cpp` hold table | Holds are 0 / 0.4 / 1.0 / 2.0 / 1.0 s depending on the transition |
| A5 | `ARCHITECTURE.md:171-176` | `StructureTagger::prepare(double)`, `update(float,float,float)`, `getCurrentState()` | `StructureTagger.h:29,36,41` | `update` takes 6 args (`rms, centroid, hfFlux, numSamples, peakRms, noteRinging)`); `getCurrentState` is `const` |
| A6 | `ARCHITECTURE.md:188-195` | `FeatureVector { bpm, rmsEnergy, spectralCentroid, highFreqFlux, state, sampleTimestamp }` | `FeatureVector.h:16-48` | Also carries `pitchRootMidi`, `pitchConfidence`, `policyIntensity`, `rmsDelta`, `subBassRatio`, `onsetDensityPerBeat`, `onsetIoiBeats` |
| A7 | `ARCHITECTURE.md:218` | `virtual int selectPattern(const FeatureVector& features) = 0;` | `IInference.h:28` | Signature is `selectPattern(const FeatureVector&, int excludeIndex = -1)` |
| A8 | `ARCHITECTURE.md:234-242` | RuleBasedInference: `VERSE + bpm<120 → 1`, `BREAKDOWN → 6`, `CHORUS + bpm<160 → 4`, … | `pattern_rules.h:42-57` | Real: SILENT→0; SOFT→1/2/3 by adjusted BPM; LOUD→4 (fast) or 5. There is no VERSE/CHORUS/BREAKDOWN input |
| A9 | `ARCHITECTURE.md:251` | "`assets/metal_groove.onnx` (22-class mel-CNN)" | `pattern_embeddings.h:19`, `MidiPatternLibrary.h:40` | 28 classes |
| A10 | `ARCHITECTURE.md:259` | "64×32 (or 64×40) mel window" | `MelSpectrogramExtractor.h:39-42` | Only 64×32 exists |
| A11 | `ARCHITECTURE.md:261-262` | "Style classification (`classifyStyle`) runs from the same drain" | `AccompanimentProcessor.cpp:471-475` | True, but it is ~2 Hz (one window per 22050 samples), not per 50 Hz drain |
| A12 | `ARCHITECTURE.md:272` | patterns stored "as `constexpr` data" | `MidiPatternLibrary.h:48`, `MidiPatternLibrary.cpp:972-999` | Runtime `std::vector<MidiPattern>` built in the constructor |
| A13 | `ARCHITECTURE.md:275-280` | `MidiEvent { note, velocity, beatOffset, durationBeats }` | `MidiPatternLibrary.h:12-19` | Also `bool isGhost` |
| A14 | `ARCHITECTURE.md:296` | "Pattern indices 0–6 correspond to the outputs of `RuleBasedInference`" | `MidiPatternLibrary.h:40` | 28 patterns; rule path returns 0–5 only |
| A15 | `ARCHITECTURE.md:310-311` | "velocity humanised: ±10 random offset per hit; timing: ±2 ms random offset" | `PatternPlayer.cpp:883-889` and `PatternPlayer.h:386-387` | Timing is template offset + swing + ghost offset + a **deterministic per-(bar,grid16,voice,salt) gaussian**, sigma `grooveTemplate.timingJitterMs` (default 1.5 ms). Humanisation is a hash, not a random stream, and is not a flat ±10 / ±2 ms |
| A16 | `ARCHITECTURE.md:373` | "inference thread … stays idle (`inferencePaused == true`) until `prepareToPlay()` finishes" | `AccompanimentProcessor.h:305` (`{ false }`), `AccompanimentProcessor.cpp:139-140,159,261` | Default is **false**; the thread starts unpaused at construction and is only paused *during* each `prepareToPlay` call |
| A17 | `ARCHITECTURE.md:402` | "Pushes a `FeatureVector` … (non-blocking, **always succeeds**)" | `AccompanimentProcessor.cpp:893` | Result is discarded (`(void)`); enqueue can fail |
| A18 | `ARCHITECTURE.md:411-413` | background thread "Call `IInference::selectPattern()`, then write `latestPatternIndex`" | `AccompanimentProcessor.cpp:518-555` | Production path calls `MetalGrooveInference::selectPatternFromMel`; `selectPattern` is only the fallback. Mel/centroid/style/B-lock work is omitted |
| A19 | `ARCHITECTURE.md:398-405` and 448-464 | per-block step list / data-flow diagram | `AccompanimentProcessor.cpp:1215-1941` | The entire engine (mel extraction, phrase learning, riff capture, groove lock, post-lock transition, mirroring, fills) is missing from both |
| A20 | `ARCHITECTURE.md:488` | Build target table row "`PluginData` — BinaryData library" | `CMakeLists.txt:117-119` | Real target name is `MetalAccompanimentData`. `MetalAccompanimentIntegrationTests` (`CMakeLists.txt:327`) and `MetalAccompanimentExportPatterns` (`CMakeLists.txt:391`) are missing from the table |
| A21 | `ARCHITECTURE.md:498` | "`AccompanimentProcessor`'s factory (`makeInference()`)" | `AccompanimentProcessor.cpp:16-34` | It is a file-local free function in an anonymous namespace, not a member |
| A22 | `ARCHITECTURE.md:36` (and 3-9) | header still frames docs around v0.5.0 / "Data Improvement Strategy" | `CMakeLists.txt:4` | Repo is 1.0.3 |

### 6.2 `docs/RUNTIME_ARCHITECTURE.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| R1 | `RUNTIME_ARCHITECTURE.md:12` | Thread table: "Capture writer thread \| `FeatureCapture` \| Write JSONL rows" | `CMakeLists.txt:163-185` vs `:308` | No such thread exists in the plugin |
| R2 | `RUNTIME_ARCHITECTURE.md:23` | "Rule fallback, optional ONNX pattern/**structure**/**bass**" | `makeInference` `AccompanimentProcessor.cpp:19-34` | Only a pattern head + style head exist; no structure or bass ONNX anywhere in `src/` |
| R3 | `RUNTIME_ARCHITECTURE.md:24` | "Start inference thread paused until prepare completes" | `AccompanimentProcessor.h:305`, `AccompanimentProcessor.cpp:139-140` | Thread starts unpaused |
| R4 | `RUNTIME_ARCHITECTURE.md:37` | audio thread step "`OnsetDetector` spectral flux + onset BPM" | file absent; `AccompanimentProcessor.cpp:799-802` comment | Onset/tempo estimation was retired; BPM is host-transport only |
| R5 | `RUNTIME_ARCHITECTURE.md:38` | "`BeatTracker` beat phase + confidence" | file absent (verified `src/analysis/BeatTracker.*` missing) | No beat tracker |
| R6 | `RUNTIME_ARCHITECTURE.md:39` | "`TempoStabiliser` stable playback BPM" | file absent | No tempo stabiliser |
| R7 | `RUNTIME_ARCHITECTURE.md:41` | "StructureTagger SILENT / VERSE / CHORUS / BREAKDOWN" | `StructureTagger.h:14-19` | `{ SILENT, SOFT, LOUD }` |
| R8 | `RUNTIME_ARCHITECTURE.md:47` | "Read … **bass handoff atomics**" | `AccompanimentProcessor.h` has none; grep for `genBass` returns nothing in `src/` | No bass handoff exists |
| R9 | `RUNTIME_ARCHITECTURE.md:84-94` | inference-thread subgraph: `StructureShadow`, `BlendStructure` (structureBlend), `CaptureRow`, `BassMode`, `BassProposal`, `BassGuard`, `StoreBass` | `AccompanimentProcessor.cpp:428-627` | None of those exist. Real drain: mel drain → style smoothing → freeze check → B-lock one-shot → selectPatternFromMel → state-compat check → `refineByRhythm` → optional commit + `latestPatternIndex` store |
| R10 | `RUNTIME_ARCHITECTURE.md:129` | ownership "(`src/AccompanimentProcessor.h:103`)" | `AccompanimentProcessor.h:262-275` | Line 103 is a comment |
| R11 | `RUNTIME_ARCHITECTURE.md:135` | `featureQueue{4096}` at `h:122` | `AccompanimentProcessor.h:279` | stale line |
| R12 | `RUNTIME_ARCHITECTURE.md:139` | drain "keeps the newest (`cpp:194`)" | `AccompanimentProcessor.cpp:440-447` | stale line |
| R13 | `RUNTIME_ARCHITECTURE.md:140` | "audio thread drops if enqueue fails (`cpp:503`)" | `AccompanimentProcessor.cpp:893` | stale line |
| R14 | `RUNTIME_ARCHITECTURE.md:146` | "inference thread writes `latestPatternIndex` (`h:123`)" | `AccompanimentProcessor.h:288` | stale line |
| R15 | `RUNTIME_ARCHITECTURE.md:148` | commits "at the next bar boundary (`PatternPlayer.cpp:429`)" | `PatternPlayer.cpp:1392-1453` | stale line; also beat-aligned commits exist |
| R16 | `RUNTIME_ARCHITECTURE.md:156-162` | "Shared Groove Commit Policy": a fixed-size commit carrying **target drum pattern + optional generated bass frame + optional transition fill**, produced by inference and activated by `PatternPlayer` at the next bar boundary | `PatternPlayer.h:37-41` (`GrooveCommit` = `{patternIndex, alignToBeat}` only); `AccompanimentProcessor.cpp:1205-1209` (drained and discarded); `AccompanimentProcessor.cpp:1147-1155` (the only commit that takes effect is queued on the audio thread) | The documented commit struct and handoff do not exist; the queue is vestigial |
| R17 | `RUNTIME_ARCHITECTURE.md:166` | "Display values are atomics (… **beat confidence**)" | `AccompanimentProcessor.h:295-302` | No beat-confidence display exists |
| R18 | `RUNTIME_ARCHITECTURE.md:170-182` | "Generative Bass Handoff": `genBassPitchOffsets[16]`, `genBassVelocities[16]`, `genBassRootMidi`, `genBassStepsReady`, `useGenerativeBass`, plain arrays guarded by an atomic flag, "audio thread reads and clears … (`cpp:519`)" | grep for `genBass`/`useGenerativeBass`/`generativeBass` in `src/` returns nothing; `h:127` is an unrelated getter | Entire section describes code that does not exist |
| R19 | `RUNTIME_ARCHITECTURE.md:195` | construction "happens in `cpp:111`" | `AccompanimentProcessor.cpp:130-141` | stale |
| R20 | `RUNTIME_ARCHITECTURE.md:199` | prepare at `cpp:140` | `AccompanimentProcessor.cpp:157-262` | stale |
| R21 | `RUNTIME_ARCHITECTURE.md:209` | `releaseResources` at `cpp:184`; destructor at `cpp:126` | `AccompanimentProcessor.cpp:423-426`, `143-148` | stale |
| R22 | `RUNTIME_ARCHITECTURE.md:211` | FeatureCapture RAII at `FeatureCapture.cpp:105,138` | `FeatureCapture.cpp:115-120,150-159` | stale, and moot (not in the plugin) |
| R23 | `RUNTIME_ARCHITECTURE.md:223` | "**Default builds leave ONNX off.**" | `CMakeLists.txt:19` (`option(MA_ENABLE_ONNX … ON)`) | Default is ON |
| R24 | `RUNTIME_ARCHITECTURE.md:232-245` | test-coverage list including "onset detection", "beat tracking", "generative bass" | `CMakeLists.txt:284-305` | No such test files; the list omits the real ones (phrase learner, structure sequencer, feature capture, groove renderer, mel spectrogram, ONNX latency) |
| R25 | `RUNTIME_ARCHITECTURE.md:254` | extracted classes include "`TempoStabiliser`" | file absent | remove |
| R26 | `RUNTIME_ARCHITECTURE.md:258` | "`AccompanimentProcessor.cpp` is still the largest source file at **633 lines**" | `AccompanimentProcessor.cpp` = 2483 lines | 4× larger |
| R27 | `RUNTIME_ARCHITECTURE.md:259` | "`PatternPlayer.cpp` is **456 lines**" | `PatternPlayer.cpp` = 1595 lines | 3.5× larger |
| R28 | `RUNTIME_ARCHITECTURE.md:261` | "generative bass handoff uses plain arrays across threads" | same as R18 | not real |
| R29 | `RUNTIME_ARCHITECTURE.md:249` | "ONNX does not run on the audio thread" | `AccompanimentProcessor.cpp:486-500,520-533` | **True** for ONNX `Run()`, but the *mel spectrogram that feeds it* is computed on the audio thread (§8.1) — the doc's inference-thread framing is incomplete |

### 6.3 `docs/CODEBASE_WALKTHROUGH.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| W1 | `CODEBASE_WALKTHROUGH.md:22-28` | entry-point line numbers `cpp:111`, `:140`, `:402`, `:367`, `:604`, `:630`, `:625` | `130`, `157`, `724`, `662`, `2456`, `2480`, `2475` | all stale |
| W2 | `CODEBASE_WALKTHROUGH.md:36,38,44,45` | owns `OnsetDetector`, `BeatTracker`, `TempoStabiliser`, `FeatureCapture`, `IStructureInference`, `OnnxBassInference` | files absent; `AccompanimentProcessor.h:262-275` | none of these classes exist |
| W3 | `CODEBASE_WALKTHROUGH.md:40,89` | `StructureTagger`: "SILENT / SOFT / LOUD" | `StructureTagger.h:14-19` | **Correct** (this doc is right; `ARCHITECTURE.md` is wrong) |
| W4 | `CODEBASE_WALKTHROUGH.md:47` | ownership list at `h:103` | `AccompanimentProcessor.h:262-275` | stale |
| W5 | `CODEBASE_WALKTHROUGH.md:57` | `PatternPlayer` tracks "static bass, and **generative bass** state" | `PatternPlayer.h:422-434` | No generative bass; there is grid bass (authored/harmonic) and the learned mirror |
| W6 | `CODEBASE_WALKTHROUGH.md:63-73` | "`MidiPatternLibrary` … builds **seven** patterns: 0 Silent … 6 Breakdown"; "Breakdown is present but not chosen" | `MidiPatternLibrary.h:40`, `MidiPatternLibrary.cpp:972-999`, `pattern_rules.h:74` | 28 patterns; pattern 6 is LOUD-compatible and reachable via `diversifyPattern`/pools |
| W7 | `CODEBASE_WALKTHROUGH.md:77` | editor has "APVTS-backed controls for **intensity, structure blend, and generative bass mode**"; polls at `cpp:132` | `AccompanimentProcessor.cpp:48-128`; `AccompanimentEditor.cpp:547` | Real params: `outputGain, bpm, genre, swing, humanize, bassTranspose, songForm, loop, lockBars, transitionBars, transitionSections`. Timer is at `:547` |
| W8 | `CODEBASE_WALKTHROUGH.md:88-97` | 13-step block list omitting the learner/lock/mirror/mel; all line numbers stale | `AccompanimentProcessor.cpp:724-2035` | see §2 |
| W9 | `CODEBASE_WALKTHROUGH.md:101,103` | `inferenceThread` at `cpp:123`; drain at `cpp:189` | `140`, `440-447` | stale |
| W10 | `CODEBASE_WALKTHROUGH.md:107-112` | "Optionally processes structure shadow inference. Blends ML structure with rule structure based on `structureBlend`… Enforces **two-bar** pattern and bass hold guards. Pushes feature-capture rows… Runs ONNX bass proposal" | `AccompanimentProcessor.cpp:561-627`; no `structureBlend` param exists | No structure inference, no blend, no capture rows, no bass ONNX; the drum hold is **1 bar** (`holdSamples = 4 beats`, `:565-569`) |
| W11 | `CODEBASE_WALKTHROUGH.md:121` | RAII on `FeatureCapture::~FeatureCapture` | `CMakeLists.txt:163-185` | not in the plugin |
| W12 | `CODEBASE_WALKTHROUGH.md:130` | "Change tempo tracking → `BeatTracker.cpp`, `OnsetDetector.cpp`, `TempoStabiliser.cpp`" | files absent | dead pointer |
| W13 | `CODEBASE_WALKTHROUGH.md:132` | "Change pattern choice → … `OnnxInference.cpp`" | file absent | dead pointer |
| W14 | `CODEBASE_WALKTHROUGH.md:136` | "Change training/capture workflow → `src/capture/FeatureCapture.*`" | `CMakeLists.txt:308` | test-only |

### 6.4 `docs/SECTION_TRANSITIONS.md`

This file is written as a present-tense description of behaviour **plus** an implementation roadmap.
Most roadmap items have since shipped, so its "current behaviour" sections are now false in both
directions (things it says are missing exist; the numbers it quotes are wrong).

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| S1 | `SECTION_TRANSITIONS.md:15` | "RMS rises above `kLoudRms` (**0.35**)" | `StructureTagger.h:67` | `kLoudRms = 0.45f` |
| S2 | `SECTION_TRANSITIONS.md:17` | "StructureTagger hold: must stay LOUD for **2.5 seconds**" | `StructureTagger.h:78` | SOFT→LOUD is `kHoldSoftToLoudSec = 0.4 s` |
| S3 | `SECTION_TRANSITIONS.md:21-23` | inference thread runs "`RuleBasedInference` → new pattern index" | `AccompanimentProcessor.cpp:520-533` | Mel-CNN `selectPatternFromMel` is the production path |
| S4 | `SECTION_TRANSITIONS.md:25-26` | "**2-bar** hold guard … at 100 BPM: **4.8 seconds**" | `AccompanimentProcessor.cpp:561-569` | 1-bar hold (`holdSamples = 4 beats` = 2.4 s at 100 BPM) |
| S5 | `SECTION_TRANSITIONS.md:35,37` | "Realistic total latency … **7–11 seconds**" | `AccompanimentProcessor.cpp:570-578`, `1147-1155` | a >0.6 RMS step can commit at the next **beat** in Play mode (~250 ms), plus the 0.4 s state hold |
| S6 | `SECTION_TRANSITIONS.md:45` | "**Two seconds** prevents flickering" | `StructureTagger.h:77-81` | holds are 0–2 s depending on the pair |
| S7 | `SECTION_TRANSITIONS.md:63,167` | "`playbackGateOpen` is killed and the **BeatTracker** resets … starts its entire count-in sequence again" | `PlaybackGate.h:22-26`, `PlaybackGate.cpp:21-28`; `AccompanimentProcessor.cpp:929-960` | No `playbackGateOpen`/BeatTracker. Gate only requests a reset after **8 s** of silence; the Play count-in runs only on the Play button edge |
| S8 | `SECTION_TRANSITIONS.md:98` | "The `snapBpm()` fix addresses this at **gate-open**" | `PatternPlayer.cpp:287` | `snapBpm` exists but there is no gate-open concept |
| S9 | `SECTION_TRANSITIONS.md:106-133` | crash on pattern change is an unimplemented proposal with a `fireTransitionCrash` flag | `PatternPlayer.h:154`, `PatternPlayer.cpp:1458-1475`, called from `AccompanimentProcessor.cpp:866,1043,1801,1837` | Implemented as `armTransitionCrash`/`armCrashPending`, deduped by `patternCrashesNear`, and triggered by section entry / lock expiry / transition boundaries — not on every `activePatternIndex` change |
| S10 | `SECTION_TRANSITIONS.md:137-163` | energy-delta bypass of the hold guard is a proposal | `FeatureVector.h:35`, `AccompanimentProcessor.cpp:570-578,1053-1055,1147-1155` | Implemented (`rmsDelta`, threshold 0.6, plus beat-aligned gesture commits) |
| S11 | `SECTION_TRANSITIONS.md:165-201` | breath detection is missing; threshold ">1 beat" | `PlaybackGate.h:46`, `PlaybackGate.cpp:14-43` | Implemented; threshold is **8 s**, not 1 beat |
| S12 | `SECTION_TRANSITIONS.md:203-214` | "Set-interval / drummer agency … add an `autoChangeBars` parameter (default 8 bars)" | `AccompanimentProcessor.cpp:110-125` | Not implemented; no `autoChangeBars` param exists |
| S13 | `SECTION_TRANSITIONS.md:216-218` | "Transition fill … requires knowing the change is coming before it happens, which the current architecture doesn't support" | `AccompanimentProcessor.cpp:2037-2087`, `1011-1015`, `1075`; `PatternPlayer.h:169-176` | Implemented: `playLastBarOriginBeat` + `updateOutgoingFill` + `armBarFillAtBeat`, plus `armBassLeadIn` |
| S14 | `SECTION_TRANSITIONS.md:230-234` | constants `kHoldSoftSec = 2.0`, `kHoldLoudSec = 1.5` | `StructureTagger.h:77-81` | Those names do not exist; real names are `kHoldSoftToLoudSec`, `kHoldLoudToSoftSec`, `kHoldSoftToSilentSec`, `kHoldLoudToSilentSec`, `kHoldSilentSec` |
| S15 | `SECTION_TRANSITIONS.md:9` | "There is **no transition detection**" | `AccompanimentProcessor.cpp:570-578`, `1598`, `1813-1827`, `PlaybackGate.cpp:38-39` | Gesture detection, phrase-lock edges, riff-match cut-short and breath crashes all exist |

### 6.5 `docs/ONNX_IO.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| N1 | `ONNX_IO.md:4` | contract "consumed by `src/inference/OnnxInference.cpp` (legacy)" | file absent | remove |
| N2 | `ONNX_IO.md:16,29-33` | "three output heads": `bottleneck`, `style_logits`, `groove_embedding` | Graph decoded from `assets/metal_groove.onnx` **does** expose exactly those 3 outputs; but `MetalGrooveInference.cpp:173-179` only requires `GetOutputCount() >= 2` and `267-278` requests only `bottleneck` + `style_logits` | The doc is **correct about the graph**; what it omits is that `groove_embedding` is never requested or consumed, and its exported second dim is symbolic so the doc's "64" is not verifiable from the graph |
| N3 | `ONNX_IO.md:37-40` | mel → `melQueue` → background thread | `AccompanimentProcessor.cpp:764-773` (extraction on audio thread), `463-469` (drain on inference thread) | Correct as written, and it is the **only** doc that gets the 22 050-sample envelope right — but it never says the STFT is on the audio thread |
| N4 | `ONNX_IO.md:42` | "22 precomputed centroids" | `pattern_embeddings.h:19` | 28 |
| N5 | `ONNX_IO.md:43` | "Best-match pattern index [**0–21**]" | `pattern_embeddings.h:19`, `MetalGrooveInference.h:42` | [0,27] |
| N6 | `ONNX_IO.md:51` | "`std::array<std::array<float, 128>, 22>` — 22 patterns" | `pattern_embeddings.h:23` | `…, kNumPatterns>` with `kNumPatterns = 28` |
| N7 | `ONNX_IO.md:60` | model size "~685 KB" | `assets/metal_groove.onnx` = 690,163 bytes (≈674 KiB) | off by ~11 KB / unit confusion (KB vs KiB) |
| N8 | `ONNX_IO.md:63` | Opset 18 | `opset_import` version 18, read directly from `assets/metal_groove.onnx` | **Correct** |
| N8b | `ONNX_IO.md:69` | "p99 latency <1ms per ONNX `Run()` call" | the repo's only assertion is `tests/test_onnx_latency_benchmark.cpp:115` `REQUIRE(stats.p99Ms < 5.0)`, for `metal_groove` only | `<1 ms` is unverified; the project's own gate is `< 5 ms` |
| N8c | `ONNX_IO.md:70` | "Thread \| Background inference thread (~50 Hz drain)" | `AccompanimentProcessor.cpp:662-678` polls every 20 ms, but the mel path that actually runs the CNN fires ~2 Hz (one 22050-sample window) | The drain is ~50 Hz; **CNN inference is ~2 Hz** |
| N8d | `ONNX_IO.md:33` | `groove_embedding` `[batch, 64]` | the exported axis is a symbolic `dim_param`, not a literal 64 | unverifiable from the graph; no code consumes it either way |
| N8e | `ONNX_IO.md` (whole file) | does not mention `assets/style_cnn.onnx` at all | `MetalGrooveInference.cpp:199-225,392-407` prefers a separate `style_cnn.onnx` session for `classifyStyle()`; bundled at `CMakeLists.txt:111` | The **preferred** style classifier is undocumented |
| N9 | `ONNX_IO.md:71` | "Fallback `OnnxInference` (legacy scalar-feature) → `RuleBasedInference`" | `AccompanimentProcessor.cpp:19-34`; `MetalGrooveInference.cpp:235-240` | No `OnnxInference`. The real fallbacks are (a) mel path → `selectPattern` → `PatternRules::rulePatternForState`, and (b) failure to load → `RuleBasedInference` |
| N10 | `ONNX_IO.md:75-107` | "Legacy model `accompaniment_model.onnx` … **Still bundled as fallback**", full `X`/`Y` tensor contract | `CMakeLists.txt:96-113`; `assets/` listing (no `accompaniment_model.onnx`) | Not bundled, file absent, consumer class absent. The whole section describes a retired path |
| N11 | `ONNX_IO.md:118` | "Generative bass … Bass stays rule-based" | `PatternPlayer.cpp:1160-1261` | Correct — and it directly contradicts `docs/BASS_ONNX_IO.md` |
| N12 | `ONNX_IO.md:121-132` | "Source files removed from build (kept on disk)" lists `src/analysis/PitchEstimator.*`, `src/analysis/StablePitchTracker.*`, `src/capture/FeatureCapture.*` | `CMakeLists.txt:173-174` (plugin), `:308` (tests) | PitchEstimator and StablePitchTracker are **in the plugin build**; FeatureCapture is in the test build. The other listed files are not on disk at all. The list also omits `style_cnn.onnx`, which *is* bundled (`CMakeLists.txt:108`) |
| N13 | `ONNX_IO.md:155,159` | references `src/inference/OnnxInference.cpp` and `SIMPLIFY.md` §0 | `OnnxInference.cpp` absent; `SIMPLIFY.md` is not at the repo root — it lives at `.MDignore/SIMPLIFY.md` | `OnnxInference.cpp` is a dead reference; `SIMPLIFY.md` exists but at a stale path |
| N14 | `ONNX_IO.md:1,3` | "v0.8.x / updated for v0.8.0" | `CMakeLists.txt:4` | doc is 2 minor versions behind (repo 1.0.3) |

### 6.6 `docs/BASS_ONNX_IO.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| B1 | `BASS_ONNX_IO.md:4` | "`src/inference/OnnxBassInference.cpp` (implemented in the plugin)" | absent from `src/`; grep for `OnnxBass` finds nothing | No bass ONNX inference exists |
| B2 | `BASS_ONNX_IO.md:4,87-88` | model `assets/bass_model.onnx`, `BinaryData::bass_model_onnx` | `assets/` listing; `CMakeLists.txt:96-113` | Asset absent, never bundled |
| B3 | `BASS_ONNX_IO.md:7-11,53-79` | 16-step piano-roll `Y_bass` with relative pitch offsets, decoded as `absolute_midi = floor(root + offset)` | `PatternPlayer.cpp:1160-1261` (`emitHarmonicBass` fixed degree table) and `1082-1158` (`emitPatternBass` authored intervals) | No such decoding path. Bass harmony is a fixed per-section interval table plus authored `bassEvents` |
| B4 | `BASS_ONNX_IO.md:24` | "background thread, same cadence family as other ONNX heads" | — | No such head |
| B5 | `BASS_ONNX_IO.md:91` | "The repository ships a minimal stub graph" | `CMakeLists.txt:96-113` | False |
| B6 | `BASS_ONNX_IO.md:3` | "Frozen for milestone v0.4.0 (Phase 22)" | `ONNX_IO.md:118` says the model was removed in v0.8.0 | The two ONNX docs contradict each other; the code agrees with `ONNX_IO.md` |

### 6.7 `docs/ONNX_READINESS.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| O1 | `ONNX_READINESS.md:9` | criterion 3 "ORT p99 latency (pattern, **structure**, **bass**)" all PASS | `AccompanimentProcessor.cpp:19-34`; `assets/` | Only a pattern head + a style head are loaded |
| O2 | `ONNX_READINESS.md:10-12` | criteria 4-6 reference `assets/bass_model.onnx`, structure norm "baked in graph", `--structure`/`--bass` contract tests all PASS | assets absent; `ONNX_IO.md:111-119` says structure and bass models were removed | claims cannot be true of the current tree |
| O3 | `ONNX_READINESS.md:16` | "`OnnxInference::selectPattern` falls back to `PatternRules::rulePatternForState`" | class absent; real fallback `AccompanimentProcessor.cpp:536-550` | rename/rewrite |
| O4 | `ONNX_READINESS.md:20` | "`MA_ENABLE_ONNX` is **ON** by default" | `CMakeLists.txt:19` | **Correct** |
| O5 | `ONNX_READINESS.md:40-42` | re-verify command names `assets/accompaniment_model.onnx`, `structure_model.onnx`, `bass_model.onnx` | all absent | dead command |
| O6 | `ONNX_READINESS.md:51-57` | centroid proxy section and "Promote new `assets/accompaniment_model.onnx` after `merge_datasets.py` + `train_gmd.py`" | `assets/` has only `metal_groove.onnx`, `style_cnn.onnx`, `groove_renderer.onnx` | describes the retired legacy pipeline |
| O7 | `ONNX_READINESS.md:1` | "Phase 36" checklist | `CMakeLists.txt:4` | historical/planning artefact |

### 6.8 `docs/TIER1_GROOVE_MODEL_CONTRACT.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| T1 | `TIER1_GROOVE_MODEL_CONTRACT.md:36` | model `assets/groove_renderer.onnx` | `assets/groove_renderer.onnx` (1.45 MB); `CMakeLists.txt:20,110-112` | **Correct** — it exists and ships |
| T2 | `TIER1_GROOVE_MODEL_CONTRACT.md:28-30` | "Runs on the inference thread, committed to the audio thread … the same kind of handoff" as `grooveCommitQueue` | `CMakeLists.txt:163-185`; grep for `GrooveRenderer` in `src/` finds only its own files + `PatternPlayer.h`/`GrooveGrid.h` comments; grep in `tests/` finds `test_groove_renderer*.cpp` | `GrooveRenderer` is **never instantiated in the plugin**. It is compiled and unit-tested but not wired |
| T3 | `TIER1_GROOVE_MODEL_CONTRACT.md:205-215` | "push it through a **new** lock-free `grooveGridQueue` (32 capacity) to the audio thread"; free function `renderGroove(...)`, `scoreGridFromPattern(...)` | no `grooveGridQueue` in `src/`; only `featureQueue`, `melQueue`, `grooveCommitQueue` | No such queue or functions; the design is unimplemented |
| T4 | `TIER1_GROOVE_MODEL_CONTRACT.md:42-43` | inputs `score [1,10,16]` + `condition [1,18]` | `GrooveRenderer.cpp:161-162,176` | **Correct** (names `score`, `condition`) |
| T5 | `TIER1_GROOVE_MODEL_CONTRACT.md:47-52` | 4 outputs `velocity_mean/std`, `offset_mean/std` `[1,10,16]` | `GrooveRenderer.cpp:177,180-181` | **Correct** |
| T6 | `TIER1_GROOVE_MODEL_CONTRACT.md:181` | export snippet uses `output_names=["velocity","offset_ms"]` (2 outputs) | `GrooveRenderer.cpp:177` expects 4 names | Doc contradicts itself and the code; the shipped graph has 4 heads |
| T7 | `TIER1_GROOVE_MODEL_CONTRACT.md:49` | "`velocity_mean` … Per-hit velocity mean, `[0,1]` (`1.0 = MIDI 127`)" | `GrooveRenderer.cpp:202-204` clamps to `[0.2,2.5]`; `PatternPlayer.cpp:903-904` comment: "grid velocity is a **multiplier** on the authored velocity" | It is a multiplier, not an absolute velocity. `GrooveGrid.h:11-13` also says "absolute velocity" and is therefore wrong against its own consumer |
| T8 | `TIER1_GROOVE_MODEL_CONTRACT.md:82-91` | 18-dim condition layout with rms/centroid/density/style/state populated | `GrooveRenderer.cpp:60-81` fills only dims 0, 12-16, 17; dims 1-11 are hard-zero with an explicit v2-alignment comment | The runtime feeds a different (sparser) condition vector than documented |
| T9 | `TIER1_GROOVE_MODEL_CONTRACT.md:100-117` | decoder takes `z [32]`; `z ~ N(0,I)` sampled per bar | `GrooveRenderer.cpp:176,180` — the session is run with **2 inputs** (`score`, `condition`) and no `z` | No latent input in the runtime call |
| T10 | `TIER1_GROOVE_MODEL_CONTRACT.md:217` | "Audio thread (`emitDrumEventsForRange`, lines **264–339**)" | `PatternPlayer.cpp:789` (function), consumption at `851-854`, `875-880`, `901-904` | stale line range |
| T11 | `TIER1_GROOVE_MODEL_CONTRACT.md:4-9` | "Replace the fixed per-16th mean groove templates (the **current** `Groove::Template` + bounded-gaussian jitter)" | `PatternPlayer.cpp:881-889`, `PatternPlayer.h:470-471` | Accurate: the template path **is** still the live one; the "replace" never landed |
| T12 | `TIER1_GROOVE_MODEL_CONTRACT.md:90` | genre one-hot "(Rock / Hard Rock / Punk / Metal / Sludge)" | `GrooveTemplate.h:240-256` defines **13** genres; `GrooveRenderer.cpp:77` clamps `grooveSlot` to 0..4 | 5 is the *model's* input width; the UI offers 13. Doc describes the model, not the UI |

### 6.9 `README.md`

| # | Doc (line) | Claim | Code that contradicts it | Correction |
|---|---|---|---|---|
| D1 | `README.md:1,7,9,48,167` | version "**v0.9.29**" throughout; "Controls (v0.9.29)" | `CMakeLists.txt:4` | Repo is **1.0.3** |
| D2 | `README.md:150` | Mel-CNN style head steers the groove "once stable **~150 ms**" | `AccompanimentProcessor.cpp:40-44` (3 consecutive ~0.5 s windows ≈ 1.5 s to lock, then a 4-window hold), `648-657` | ~1.5 s to commit, ~2 s minimum hold — 10× the documented figure |
| D3 | `README.md:151` | "Pattern changes commit on **bar boundaries**, with a hold" | `AccompanimentProcessor.cpp:1147-1155`; `PatternPlayer.cpp:1396-1403` | Also commits on the **next beat** for large RMS gestures and for T6.1/T4.1 gesture picks |
| D4 | `README.md:163,176` | Lock/Transition params "control how long the lock holds **after you leave the riff**"; "returning to it **extends it**"; contrast sections then "follow returns" | `AccompanimentProcessor.cpp:1332-1336` (fixed `lockBars` window measured from the bar-aligned riff origin), `1705-1712`, `1838` | The hold is a fixed window from the riff origin, not extended by returning to the riff (`justMatchedRiff` only stamps `lastRiffMatchSample`). After the contrast sections the engine **always re-enters `RiffA`** — it does not return to follow mode |
| D5 | `README.md:207` | "still gated by SOFT/LOUD and the **2-bar** commit hold" | `AccompanimentProcessor.cpp:561-569` | 1 bar (4 beats) |
| D6 | `README.md:171` | Genre list "Rock (default), Hard Rock, Punk, Metal, Sludge" | `GrooveTemplate.h:240-256`, `kPresetCount = 13` | 13 genres; Rock is index 0 (`AccompanimentProcessor.cpp:70`) |
| D7 | `README.md:161` | "Record riff: 1-bar count-in …, then play a 4-bar riff" | `AccompanimentProcessor.cpp:1441-1443` (`kCountInBeats = 4`, `kRecordBeats = kGridBars*4`), `PhraseLearner.h:68` | Correct on count-in length and 4-bar take — but omits the `RecWaitBar` phase that waits for the next bar line first (`1445-1454`) |
| D8 | `README.md:24` | "Each fuzzyband download is **self-contained** (the ONNX model and runtime are bundled)" | `CMakeLists.txt:104-113` bundles the model; the runtime is linked from `${ONNXRUNTIME_ROOT}` with only a build RPATH (`CMakeLists.txt:253-258`) | Models are bundled by this CMake file; the ORT runtime is not. Release packaging (outside this file) may differ — see §9 |
| D9 | `README.md:107` | grooves stay on the project grid "across play, stop, seek, and loop" | `PatternPlayer.cpp:1298-1307`, `AccompanimentProcessor.cpp:2127-2161` | Correct in intent; note the frozen-transport free-run clock (`PatternPlayer.cpp:1304-1305`) means MIDI is off-grid while the transport is stopped, which `README.md:139` does say |

### 6.10 Genuinely wrong-but-minor / mixed

| # | Doc (line) | Note |
|---|---|---|
| M1 | `ARCHITECTURE.md:123-128` | "rmsEnergy — 100ms rolling RMS", "onsetRmsEnergy — 20ms rolling RMS" — **correct** (`EnergyAnalyser.cpp:23,30`). This is the one quantitative claim in that section that holds |
| M2 | `ARCHITECTURE.md:334-337` | The two-clock table is **correct and useful**: transport (`previewResolvedHostSample`, `PatternPlayer.cpp:226-234`) vs monotonic `hostSampleTime` (`AccompanimentProcessor.cpp:1957`), with `latchLockClock` (`2089-2116`) and `reanchorLockClockOnJump` (`2127-2161`). Note `consumeTransportJumped()` (`PatternPlayer.h:100-105`) is **not** called by the processor — the processor does its own jump detection at `1245-1259`, so that doc mention (`ARCHITECTURE.md:343`) is aspirational |
| M3 | `ARCHITECTURE.md:379` | soft bypass described correctly (`AccompanimentProcessor.cpp:2456-2473`), except it also flushes pending note-offs and resets `playbackGate` |
| M4 | `RUNTIME_ARCHITECTURE.md:139,140` etc. | Many line numbers in `RUNTIME_ARCHITECTURE.md` point at unrelated code; treat every citation in that file as stale |

### 6.11 Additions from binary- and exporter-level verification

The shipped `.onnx` graphs were decoded directly (protobuf) and the TensorRT-free training/export
sources were read. These add to the tables above.

| # | Doc (line) | Claim | Evidence | Correction |
|---|---|---|---|---|
| X1 | `ONNX_READINESS.md:9` | criterion 3 "ORT p99 latency (pattern, structure, bass) \| < 5 ms each \| PASS \| all three models < 5 ms p99" | `tests/test_onnx_latency_benchmark.cpp:94,115` benchmarks **only** `BinaryData::metal_groove_onnx` and asserts `p99Ms < 5.0`; it is the only `ctest -L onnx` test (`CMakeLists.txt:365-370`) | Only the pattern model has a latency gate; there is no structure or bass model to measure |
| X2 | `ONNX_READINESS.md:10` | criterion 4 (bass quality gate) PASS, citing `assets/bass_model.onnx` + `training/tests/test_onnx_contract.py` | asset absent; that test is `@pytest.mark.skipif(not _BASS_ONNX.exists(), …)` (`training/tests/test_onnx_contract.py:139`) | Cannot be PASS: the asset is missing and the covering test skips |
| X3 | `ONNX_READINESS.md:11` | criterion 5 "Structure norm baked in graph … PASS" via `validate_onnx_contract.py --structure` | `scripts/validate_onnx_contract.py:81-106` (`check_structure`) checks shape/type only, never mean/std initializers | The cited validator cannot support the claim |
| X4 | `ONNX_READINESS.md:12` | criterion 6 "all green \| CI validates `--pattern`, `--structure`, `--bass`" | `.github/workflows/ci.yml:91-98` generates *stub* structure/bass graphs with `scripts/build_minimal_*.py` and then validates them (plus the nonexistent `accompaniment_model.onnx`) | "All green" describes stub self-consistency, not the shipped runtime models |
| X5 | `ONNX_READINESS.md:51` | names `training/build_dataset.py`, `training/build_lakh_dataset.py` | both files absent; `training/analyze_feature_proxy_gap.py` exists | dead paths |
| X6 | `ONNX_READINESS.md:57` | names `merge_datasets.py` + `train_gmd.py` | both absent | dead paths |
| X7 | `TIER1_GROOVE_MODEL_CONTRACT.md:49` | `velocity_mean` is "`[0,1]` (`1.0 = MIDI 127`)" and `velocity_std` "`[0.01, 0.41]`" | `training/models/groove_renderer.py:54-57` emits `velocity_mean = 1.0 + 0.4*tanh(·)` ∈ [0.6,1.4] (a **multiplier**) and `velocity_std = sigmoid(·)*0.2+0.01` ∈ [0.01,0.21] | Multiplier, not absolute; std ceiling is 0.21, not 0.41 |
| X8 | `TIER1_GROOVE_MODEL_CONTRACT.md:100-117,156-157` | "VAE latent `z`", "standard ELBO", "β-VAE encoder `q(z \| score, velocity, offset, condition)`" | `training/models/groove_renderer.py:1-7,63-68` — masked Gaussian NLL over (mean, std) heads, **no latent, no KL/ELBO**; the runtime passes 2 inputs (`GrooveRenderer.cpp:176`) | The doc's own §1 ("No latent — a heteroscedastic (mean, std) model") is right and §3/§5 contradict it |
| X9 | `TIER1_GROOVE_MODEL_CONTRACT.md:161-170` | quality gates "velocity MAE < 0.08", "offset MAE < 5 ms" | `training/models/groove_renderer_summary.json` — `val_velocity_mae 0.5115`, `val_offset_mae_16th 0.1897`, `gates_passed false` | The shipped model **fails** its documented gates by ~6× (and the offset unit differs) |
| X10 | `TIER1_GROOVE_MODEL_CONTRACT.md:200` | `struct GrooveGrid { … velocity, offsetMs; }` | `GrooveGrid.h:59-66` — fields are `velocity` and **`offset`**, plus `valid`, `patternIndex`, `barNumber`; units are a fraction of a 16th scaled at playback (`GrooveRenderer.cpp:205-206`, `PatternPlayer.cpp:877-879`) | field is `offset`; no `offsetMs` |
| X11 | `TIER1_GROOVE_MODEL_CONTRACT.md:212-213` | "`z[i]` ← Box–Muller applied to **`hashMix`**(barNumber, i)" | the renderer's helper is named `barHash` (`GrooveRenderer.cpp:17-22`); `PatternRules::hashMix` (`pattern_rules.h:456`) is a different function | wrong identifier |
| X12 | `TIER1_GROOVE_MODEL_CONTRACT.md:113` | "~100–500k parameters" | `training/models/groove_renderer_summary.json` — **354,880** params | **Correct** (within range) |
| X13 | `README.md:171,173,174,176,180` | "Controls (v0.9.29)" table lists Genre, Swing, **Bass octave**, **Song form**, **Loop**, **Lock (bars)**, Transition, PLAY, Record/Forget, **Output Gain** | `AccompanimentEditor.*` creates widgets only for genre, swing, humanize, the editable section list, lockBars, transitionBars, transitionSections (`AccompanimentEditor.cpp:444-453,382-402,455-490`); grep for `bpm`/`outputGain`/`bassTranspose`/`songForm`/`loop` parameter IDs in the editor returns nothing | Bass octave, Song form, Loop, Output Gain and Tempo are **APVTS parameters with no UI widget**; the Song form dropdown was explicitly removed (`AccompanimentEditor.cpp:383-385`) |
| X14 | `README.md:171-180` | Controls table (exhaustive-looking) | `humanize` is a real param (`AccompanimentProcessor.cpp:78-82`, default 0.35) with a real slider (`AccompanimentEditor.h:476`, `.cpp:444-445`) | **Humanize** and **Tempo (BPM)** are missing from the table |
| X15 | `README.md:182` | "Live readouts: BPM, State, Pattern, Style, **RMS**, **Centroid**, **HF Flux**, **noise floor**, groove/lock status" | the readout row is exactly `bpm · state · P<pattern> · style` (`AccompanimentEditor.cpp:582-586`); RMS/centroid/HF flux/noise floor have processor getters (`AccompanimentProcessor.h:299-302`) but no widget | Only BPM/State/Pattern/Style are displayed, plus a separate status/progress row |
| X16 | `SECTION_TRANSITIONS.md:98` | "The `snapBpm()` fix addresses this at **gate-open**" | `PatternPlayer::snapBpm` exists (`PatternPlayer.cpp:287-290`) but has **no call sites anywhere** in `src/` (grep: declaration + definition only) | `snapBpm` is dead code; nothing calls it at gate-open or anywhere else |
| X17 | `SECTION_TRANSITIONS.md:125` | "before `emitEventsForRange`" | real function is `PatternPlayer::emitDrumEventsForRange` (`PatternPlayer.cpp:789`) | renamed |
| X18 | `SECTION_TRANSITIONS.md:115` | "`setGenerativeBassSteps`" | no such method anywhere in `src/` | nonexistent API |
| X19 | `README.md:24-25` | downloads bundle "the ONNX model **and runtime**" | `CMakeLists.txt:104-113` bundles models; the runtime is `${ONNXRUNTIME_ROOT}/lib/libonnxruntime.dylib` with a build RPATH (`:253-258`) | model bundled by this CMake file; runtime linking is external (§9 gives the uncertainty) |
| X20 | `ARCHITECTURE.md:343` | "A seek (`PatternPlayer::consumeTransportJumped`) re-latches bar phase" | `consumeTransportJumped` (`PatternPlayer.h:100-105`) has **no caller**; the processor does its own jump detection (`AccompanimentProcessor.cpp:1245-1259`) and `reanchorLockClockOnJump` | the named method is never called |
| X21 | `SECTION_TRANSITIONS.md:15,17,230-234` | constants `kLoudRms (0.35)`, `kHoldSoftSec`, `kHoldLoudSec` | real: `kLoudRms = 0.45f` (`StructureTagger.h:67`); names are `kHoldSoftToLoudSec 0.4`, `kHoldSoftToSilentSec 1.0`, `kHoldLoudToSoftSec 2.0`, `kHoldLoudToSilentSec 1.0`, `kHoldSilentSec 0.0` (`:77-81`) | wrong values and wrong names |
| X22 | `README.md:18` | "macOS (universal, Apple Silicon + Intel)" | no architecture/universal declaration in `CMakeLists.txt` (JUCE default) | packaging claim, not verifiable from this file (§9) |
| X23 | `TIER1_GROOVE_MODEL_CONTRACT.md:57-70` | 10-voice GM map (kick 35/36, snare 38/40, hats 42/46, ride 51/53, crash 49/52/55, toms 48/45/41) | `GrooveGridUtil::voiceForNote` (`GrooveGrid.h:31-45`) | **Correct** |
| X24 | `TIER1_GROOVE_MODEL_CONTRACT.md:76-78` | "4-bar patterns (11 Intro Build, 16 Outro Decay)" | `MidiPatternLibrary.cpp:386` (index 11 at `:983`) and `:574` (index 16 at `:988`) | **Correct** — these are the only two 4-bar patterns |

---

## 7. Doc claims that DID verify

- `ARCHITECTURE.md:107-111` — DAW transport is the tempo source; manual `bpm` then 120 fallback; no audio-derived tempo (`AccompanimentProcessor.cpp:803-823`).
- `ARCHITECTURE.md:123-128` — the 100 ms / 20 ms RMS window split (`EnergyAnalyser.cpp:23,30`).
- `ARCHITECTURE.md:334-337` — transport vs monotonic clock roles, including the loop-wrap rationale (`AccompanimentProcessor.cpp:825-831`, `2089-2161`).
- `ARCHITECTURE.md:346-348` / `RUNTIME_ARCHITECTURE.md` triple-buffer intent — the trio `riffA`/`riffB`/`enginePhase` really is published through a 3-slot triple buffer with a pinning reader (`AccompanimentProcessor.cpp:264-305`).
- `ARCHITECTURE.md:375` — NaN scrub then ±2 clip (`AccompanimentProcessor.cpp:735-740`).
- `ARCHITECTURE.md:379` — bypass behaviour (`AccompanimentProcessor.cpp:2456-2473`).
- `ARCHITECTURE.md:469-477` — MIDI channel convention: drums 10, bass 2 (`PatternPlayer.h:239,480`).
- `RUNTIME_ARCHITECTURE.md:249` — "ONNX does not run on the audio thread" (`AccompanimentProcessor.cpp:494,530` are the only `Run` call sites, both in the drain).
- `RUNTIME_ARCHITECTURE.md:140,142` — feature queue drop-on-full policy ("stale beats slow") is the actual behaviour, even though the cited line is wrong (`AccompanimentProcessor.cpp:893`).
- `RUNTIME_ARCHITECTURE.md:223` — one tracked claim is false, but `ONNX_READINESS.md:20` correctly says ONNX defaults ON (`CMakeLists.txt:19`).
- `ONNX_IO.md:20-33,42` (tensor names/shapes) — `mel` / `bottleneck` / `style_logits` names and `[1,1,64,32]` shape all verified (`MetalGrooveInference.cpp:47-56,148-163,255-270`), with the class-count caveat (N4/N5).
- `ONNX_IO.md:37-40` — the mel pipeline order is right (`AccompanimentProcessor.cpp:761-771`, `463-469`).
- `ONNX_IO.md:52` — centroids generated by `training/export_centroids.py` per `pattern_embeddings.h:6-7`.
- `ONNX_IO.md:118` — generative bass is gone; bass is rule/harmonic-based (`PatternPlayer.cpp:1160-1261`).
- `BASS_ONNX_IO.md:34` — `StructureState` numeric cast `SILENT=0, SOFT=1, LOUD=2` matches the real enum (`StructureTagger.h:14-19`).
- `ONNX_READINESS.md:20` — `MA_ENABLE_ONNX` defaults ON (`CMakeLists.txt:19`).
- `TIER1_GROOVE_MODEL_CONTRACT.md:42-52,54` — `score`/`condition` input names & shapes, 4 output names, per-cell `mean + ε·std` sampling (all verified, except T6/T7/T9 caveats).
- `TIER1_GROOVE_MODEL_CONTRACT.md:59-70` — the 10-voice GM map is exactly `GrooveGridUtil::voiceForNote` (`GrooveGrid.h:31-47`).
- `TIER1_GROOVE_MODEL_CONTRACT.md:4-9,11` — the premise that `Groove::Template` + gaussian jitter is still the live groove engine, and that the model was meant to replace it.
- `README.md:105-112,138-139` — host tempo, no tap tempo, off-grid free-run when stopped (`PatternPlayer.cpp:1285-1307`).
- `README.md:157` — phrase rotation: 2 bars for verse/chorus/solo, 4 for breakdown/intro/outro (`pattern_rules.h:471-478`).
- `README.md:68,149-150` — style classes (palm mute / open chord / single note / sustain / silence) and SILENT/SOFT/LOUD energy model (`MetalGrooveInference.h:53`, `StructureTagger.h:14-19`).
- `README.md:161` — 1-bar count-in + 4-bar take (`AccompanimentProcessor.cpp:1441-1443`, `PhraseLearner.h:68`).
- `README.md:188-199` — the GM drum map matches the notes the library and `voiceForNote` use (`GrooveGrid.h:31-47`).
- `README.md:216` — CMake defaults (ONNX/tests/standalone on) (`CMakeLists.txt:18-20`).
- Offline decoding of `assets/metal_groove.onnx`: input `mel` `[batch,1,64,32]` float32; outputs `bottleneck` `[batch,128]`, `style_logits` `[batch,5]`, `groove_embedding` `[batch,<symbolic>]`; `opset_import` version 18 — matches `ONNX_IO.md:20-33,63` (except the unverifiable 64).
- `assets/style_cnn.onnx`: `mel` `[batch,1,64,32]` → `style_logits` `[batch,5]`.
- `assets/groove_renderer.onnx`: inputs `score` `[batch,10,16]` + `condition` `[batch,18]`; outputs `velocity_mean`, `velocity_std`, `offset_mean`, `offset_std`, all `[batch,10,16]` — matches `TIER1_GROOVE_MODEL_CONTRACT.md:42-52` and `GrooveRenderer.cpp:176-181`.

---

## 8. Undocumented but important

### 8.1 The mel spectrogram is computed on the audio thread (biggest RT hazard)

`AccompanimentProcessor.cpp:764-773` runs, inside `processBlock`:

```cpp
if (audioRingBuffer.isWindowReady())
    if (audioRingBuffer.readWindow(melScratch.data()) > 0)
        if (melExtractor.process(melScratch.data(), mw.data.data()))
            melQueue.try_enqueue(mw);
```

`MelSpectrogramExtractor::process` (`MelSpectrogramExtractor.cpp:114-179`) loops
`for (start = 0; start + 2048 <= 22050; start += 512)` → **≈40 forward FFTs of size 2048**, then
applies a 64 × 32 × 1025 mel filterbank (≈2.1 M multiply-adds) and a dB pass over 2048 outputs — all
in one audio callback. It fires every 22050 input samples (≈0.5 s at 44.1 kHz).

Three doc comments assert the opposite thread:
- `MelSpectrogramExtractor.h:25` — "Thread-safe for use on inference background thread."
- `AudioRingBuffer.h:8` — "Single-producer (audio), single-consumer (inference thread)."
- `AudioRingBuffer.h:19-20` — "Poll from inference thread via `isWindowReady` and read the full window via `readWindow`."

In the shipping code the **audio thread is both producer and consumer** of `AudioRingBuffer`, and
the extractor's own header calls `fftScratch`/`frameMagnitudes` "no heap allocation **on audio
thread**" (`MelSpectrogramExtractor.cpp:136`) — so the code knows, and only the header comments are
wrong. Neither `ARCHITECTURE.md` nor `RUNTIME_ARCHITECTURE.md` lists STFT under audio-thread work;
`ONNX_IO.md:37-40` implies it but never says it. This is the single most consequential omission in
the doc set: a periodic ~40-FFT spike on the real-time thread is exactly the kind of thing an
architecture document exists to flag.

Secondary: `mw` (`MelWindow`, 8 KB) is stack-allocated per block in `processBlock`
(`AccompanimentProcessor.cpp:769`) — harmless, but it means the "no allocation" rule is being met by
stack discipline, not by a preallocated member.

### 8.2 `FeatureCapture` is not in the plugin at all

`src/capture/FeatureCapture.h/.cpp` implement a bounded queue + JSONL writer thread. They are
compiled **only** into `MetalAccompanimentTests` (`CMakeLists.txt:308`) and **not** into the plugin
(`CMakeLists.txt:163-185`). `grep -rn FeatureCapture src/` returns only files under `src/capture/`
itself — nothing in `AccompanimentProcessor` references it. Yet three documents describe it as a
live runtime subsystem: `ARCHITECTURE.md:56-59`, `RUNTIME_ARCHITECTURE.md:12,111-117`,
`CODEBASE_WALKTHROUGH.md:44,121,136`, `ARCHITECTURE.md:488`. There is no capture thread, no capture
queue, no JSONL output in production.

Related drift: `tests/test_processor_pipeline.cpp:394-395` sets APVTS properties
`"structureBlend"` and `"generativeBassMode"`, which **do not exist** in the real parameter layout
(`AccompanimentProcessor.cpp:48-128`). The test is asserting that legacy serialized properties are
ignored — but it also shows how the docs' phantom parameters (`structureBlend`, `generativeBassMode`)
got there.

### 8.3 The Tier-1 groove renderer is fully implemented, shipped, and dead

- `GrooveRenderer.cpp` is compiled into both targets (`CMakeLists.txt:180, 319`).
- `MA_BUNDLE_GROOVE_RENDERER` defaults ON (`CMakeLists.txt:20`) and `assets/groove_renderer.onnx`
  (1,452,891 bytes) is bundled as `BinaryData::groove_renderer_onnx` (`CMakeLists.txt:110-112`).
- But **nothing in `src/` ever constructs a `GrooveRenderer`.** The only references outside its own
  translation unit are comments in `PatternPlayer.h:221-226` and `GrooveGrid.h:6`.
- The consumer side exists and is written: `PatternPlayer::emitDrumEventsForRange` branches on
  `grooveGrid.valid` (`PatternPlayer.cpp:852-854, 875-880, 901-904`). But `grooveGrid` is reset to
  `{}` in `PatternPlayer::reset()` (`:104`) and only ever set by `setGrooveGrid`
  (`PatternPlayer.h:226`), whose only callers are `tests/test_groove_renderer.cpp`.
- `PatternPlayer.h:221-223` says so outright: *"Offline / unit-test hook … The live processor does
  not call this — drums use `Groove::Template` humanize."*

So `grooveGrid.valid` is permanently `false` in production and the entire Tier-1 path (including a
1.45 MB bundled model) never executes. `TIER1_GROOVE_MODEL_CONTRACT.md` describes this as the
integration plan and is written in the present/future tense without saying it was never wired.

(The trained model also fails its own documented gates: `val_velocity_mae 0.5115` against a
documented `< 0.08`, `gates_passed false`, per `training/models/groove_renderer_summary.json`.)

### 8.4 The inference→audio groove-commit handoff is drained and discarded

`grooveCommitQueue` (`AccompanimentProcessor.h:280`) is:
- enqueued by the inference thread at `AccompanimentProcessor.cpp:623-624`,
- drained on the audio thread at `1205-1209` — and the dequeued value is **thrown away**:

```cpp
PatternPlayer::GrooveCommit commit{};
bool gotCommit = false;
while (grooveCommitQueue.try_dequeue(commit)) gotCommit = true;
(void)gotCommit;  // T4.1: Play rotation is authoritative...
```

The only commit that reaches `PatternPlayer` is the audio-thread gesture commit at `1147-1155`
(`queueGrooveCommit`, `alignToBeat = true`). `RUNTIME_ARCHITECTURE.md:150-162` documents the
inference→audio commit as *the* coordination mechanism ("activate as one musical event"); in the
current code it is vestigial. `GrooveCommit` itself carries only `{patternIndex, alignToBeat}`
(`PatternPlayer.h:37-41`) — no bass frame, no transition fill as the doc claims.

### 8.5 `inferencePaused` defaults to `false`

`AccompanimentProcessor.h:305` — `std::atomic<bool> inferencePaused{ false };`. The constructor
starts the thread (`:139-140`) without pausing it. `prepareToPlay` sets `true` on entry (`:159`) and
`false` on exit (`:261`). Consequences:
- between construction and the first `prepareToPlay`, the thread drains and can write
  `latestPatternIndex` (`:619`) even though nothing has been prepared;
- `ARCHITECTURE.md:373`'s safety argument ("stays idle until `prepareToPlay()` finishes, so
  `IInference::prepare(sampleRate)` always runs before the loop calls `selectPattern()`") is not
  true of the code.
- In practice this is benign today only because `MetalGrooveInference::prepare` is a no-op
  (`MetalGrooveInference.cpp:230-233`) and the ONNX session is built in the constructor
  (`:112-228`). It is a latent ordering hazard, not a live bug.

### 8.6 Hidden global state: process-wide `Ort::Env` singletons

`MetalGrooveInference.cpp:19-23` and `GrooveRenderer.cpp:90-94` each define a function-local
`static Ort::Env` (`"MetalAccompanimentGroove"`, `"MetalAccompanimentGrooveRenderer"`). These are
constructed lazily on first `tryLoadModel()` and live until process exit. Effects:
- Every plugin instance in a host shares one ORT environment per name; there is no reference
  counting or teardown.
- Any DAW that instantiates/unloads the plugin repeatedly accumulates these for the process
  lifetime.
- Nothing in the docs mentions global state in the plugin.
- Also note `Ort::SessionOptions` is created and *destroyed* per load (`MetalGrooveInference.cpp:127-136`),
  so the thread-count/optimisation policy is re-derived per instantiation but the env is not.

### 8.7 `std::atomic_load` on a `shared_ptr` from the audio thread

`AccompanimentProcessor.cpp:842` does `std::atomic_load(&pendingSongForm)` on the audio thread to
pick up a new song form (published from the message thread at `2416-2419` with
`std::atomic_store`). Neither `ARCHITECTURE.md` nor `RUNTIME_ARCHITECTURE.md` mentions this
handoff at all, and `AccompanimentProcessor.h:474-478` presents it as "lock-free".

The free-function `std::atomic_load`/`atomic_store` overloads for `shared_ptr` are **not lock-free**
in libc++: they use an internal spin lock / atomic pair on the control block. On a real-time thread
this is a genuine (if usually uncontended) priority-inversion risk and is exactly the pattern the
audio-thread rules forbid. **Uncertainty:** the exact implementation is library-defined; libstdc++
also uses a lock. I did not measure it. But calling it "lock-free" in the header comment is not
supported.

### 8.8 Non-atomic audio→UI data with only a relaxed index

- `scopeSamples` is `std::array<float, 16384>` — a plain, non-atomic array (`AccompanimentProcessor.h:470`).
  The audio thread writes elements (`:751`) while the UI thread reads them
  (`copyScopeSamples`, `:2391-2400`), ordered only by `scopeWriteIndex` with `memory_order_relaxed`
  (`:750`, `:2391`). This is a data race under the C++ memory model (torn/`-ffast-math`-reordered
  reads), not a formally benign one. The architecture docs describe the display handoff as
  "atomics" (`RUNTIME_ARCHITECTURE.md:166`), which is true for the scalars but not for the scope ring.
- `StructureSequencer` is mutated on the audio thread (`advance`, `:1017`) and read from the message
  thread by `getCurrentSectionName()`/`getSectionName()` (`:697-712`), which the editor calls at
  20 Hz (`AccompanimentEditor.cpp:647`). `currentSection`/`barsElapsed`/`globalBarCount` are plain
  ints (`StructureSequencer.h:125-127`); there is no atomic or snapshot. Same class of race.
- `getRiffAPlayOriginSample()`/`getRiffBPlayOriginSample()` (`:413-421`) read the plain
  `int64_t riffAPlayOriginMono`/`riffBPlayOriginMono` (`h:360-361`) directly, bypassing the triple
  buffer that exists precisely to avoid this. Currently only tests call them
  (`tests/test_phase2_clock.cpp:173`, `tests/test_processor_pipeline.cpp:610,2937`), so it is a
  latent API hazard rather than a live one.

### 8.9 Two clocks, implemented twice

The processor resolves "this block's host sample" with `PatternPlayer::previewResolvedHostSample`
(`PatternPlayer.cpp:226-234`) for its own beat math (`clockSample`, `:831`), then calls
`PatternPlayer::process(... rawHostPos ...)` (`:1944`), which **re-derives the same frozen-transport
decision** independently (`PatternPlayer.cpp:1298-1307`) and updates the very fields
(`lastHostSample`, `sampleCounter`) that `previewResolvedHostSample` reads. They happen to agree
today (preview runs first and sees the previous block's state, process updates it after), but the
duplication means any future change to one branch silently desynchronises drum placement from the
learner/capture/scope math that uses `clockSample`. This coupling is undocumented.

### 8.10 One 16th-slot tracker shared by two different capture phases

`stampLearnerGridSlots` (`:2203-2311`) is invoked from two unrelated places:
- `RecCapture` with `origin = recStart`, `wrapLoop = false` (`:1484-1485`);
- `RiffBListen` (post-lock contrast listening) with `origin = beatStart - elapsedMonoBeats`,
  `wrapLoop = true` (`:1519-1520`).

Both funnel into the same `captureSlotIndex_` / `captureSlotPeak_` / `prevSlotPeak_` /
`prevSlotOccupied_` tracker (`h:352-359`), whose flush/onset logic (`flushPendingCaptureSlot`,
`:2174-2201`) therefore has to be reset (`resetSlotOnsetTracker`, `:2163-2172`) at every phase
boundary — at `1416` (capture start), `1773` (entering `RiffBListen`), and flushed at `1352`
(finish capture) and `1648` (B-lock edge). Getting one of those resets wrong would silently corrupt
either a user take or the contrast listen; nothing documents the invariant.

### 8.11 Once armed, the accompaniment deliberately ignores guitar silence

`armActive` (`:1185-1190`) is true whenever `playOn || playCountInActive || riffCapture* ||
grooveLocked || transitionSectionActive`. `trulySilent = digitalSilence || !armActive` (`:1191`), so
once a riff is locked or a transition is playing, **the drums and frozen bass keep sounding through
guitar silence** (T6.4, comment at `1182-1184`) — only transport-level digital silence
(`rms < 1e-6`, `:796`) cuts it. Additionally, while locked, `emitFrozenRiff` runs unconditionally for
`RiffA`/`RiffBLocked` (`:1907-1919`), and only the *live mirror* is gated by `guitarStopped`
(`:1896-1906`). Neither `README.md` nor `ARCHITECTURE.md` states that the kit will not stop when you
stop playing during a lock; `README.md:176`'s "holds after you stop playing" is the closest, and it
is about duration, not about the accompaniment continuing.

### 8.12 Dead-but-public capture API

`PhraseLearner::beginUserCapture` / `commitUserCapture` / `isUserCapturing`
(`PhraseLearner.h:52-66`, `.cpp:257-278`) are documented as the user-armed capture path, but the
processor only ever calls `cancelUserCapture()` (`AccompanimentProcessor.cpp:1307`). The live capture
path is the grid API (`beginGridCapture` `:1415`, `stampLearnerGridSlots`, `commitGridCapture`
`:1353`). The user-capture attack-list path (`attacks_[]`, `commitUserCapture`) is unreachable from
the plugin.

### 8.13 A 28-class centroid table with a 22-class backup model on disk

`PatternEmbeddings::kNumPatterns = 28` (`pattern_embeddings.h:19`) and the runtime always loads
`BinaryData::metal_groove_onnx` (`MetalGrooveInference.cpp:116`) with no model-selection path —
while `data/model_backup_22class/metal_groove.onnx` (a 22-class model) exists in the tree. If that
backup were ever swapped in, `selectPatternFromMel` would index 28 centroids against a 22-class
embedding space. This is presumably why `ONNX_IO.md` still says "22". Nothing in code prevents the
mismatch. **Uncertainty:** I did not verify that the backup's bottleneck is incompatible in
practice, only that the class counts differ and nothing enforces agreement.

### 8.14 `snapBpm()` and `consumeTransportJumped()` are unreachable

Both are public API on `PatternPlayer` (`PatternPlayer.h:107-111`) and both are described in the
docs as load-bearing (`SECTION_TRANSITIONS.md:98`, `ARCHITECTURE.md:343`), but neither has a call
site anywhere in `src/`. `setBpm` (host-authoritative, `:208-214`) and the processor's own
jump detection (`:1245-1259`) superseded them.

### 8.15 The plugin's own harness drifts from the plugin

- `CMakeLists.txt:308` compiles `FeatureCapture.cpp` into the test binary only (§8.2).
- `tests/test_processor_pipeline.cpp:394-395` writes APVTS properties that do not exist (§8.2).
- The orphan-test guard (`CMakeLists.txt:519-548`) enforces that every `tests/*.cpp` is compiled,
  which is good — but it cannot catch phantom *parameters* or doc/UI drift.

---

## 9. Uncertainty and limits of this review

1. **No build, no test run.** Every statement is static reading of the tree at `5d5f410`. I did not
   compile, run `ctest`, or profile. Real-time cost claims (§8.1) are derived from loop bounds and
   window sizes, not measurement — the FFT count (~40) and MAC count (~2.1 M) are arithmetic from
   `MelSpectrogramExtractor.cpp:123-145`.
2. **Latency/quality numbers** in `ONNX_IO.md:69` (`<1 ms`) and `ONNX_READINESS.md:9-12` (PASS
   values) were assessed as *assertions present or absent in the tree*, not as measurements. The
   repo's own gate is `< 5 ms` for `metal_groove` only (`tests/test_onnx_latency_benchmark.cpp:115`).
3. **`groove_embedding` dim.** The exported axis is a symbolic `dim_param`; the doc's "64" is
   plausible but I could not confirm it from graph metadata, and no code consumes it.
4. **`std::atomic_load(shared_ptr*)` lock-freedom** (§8.7) is library-defined; I flagged it because
   libc++/libstdc++ implement it with a lock, but I did not disassemble or measure.
   Treat "may take a spin lock on the audio thread" as a strong caution, not a proven defect.
5. **`ARCHITECTURE.md:488`** Build-target naming (`PluginData` vs `MetalAccompanimentData`) is
   verified from `CMakeLists.txt:117-119`, but the *artefact* names (`MetalAccompaniment_VST3` etc.)
   in that table are JUCE-generated and I did not inspect the build tree for every format.
6. **Which thread constructs the plugin.** I state the message thread "in practice"; JUCE may create
   the processor on another thread in some hosts. The ONNX load is therefore on "whatever thread
   constructs the processor", which I could not pin down statically.
7. **`README.md:24` "model and runtime are bundled"** and `README.md:18` "macOS universal": packaging
   claims that live in `scripts/`, `infra/` and CI, which I did not audit. `CMakeLists.txt` bundles
   only the model and links the runtime from `ONNXRUNTIME_ROOT`.
8. **`ARCHITECTURE.md:377`** claims `EnergyAnalyser` and `StructureTagger` "each guard again if
   `prepare()` is ever called with an invalid rate". `StructureTagger::prepare` does
   (`StructureTagger.cpp:7`); I did not open `EnergyAnalyser::prepare` to confirm the second half.
9. **`PatternPlayer` microtiming/humanisation** is documented in `ARCHITECTURE.md:310-311` as flat
   ±10 velocity / ±2 ms jitter. I read the call sites (`PatternPlayer.cpp:883-889`) and the bounded
   Gaussian helper, but I did not exhaustively trace every velocity contributor (preset
   `velocityScale`, `sectionVelocityMultiplier`, ornamentation, `applyVelocityHeadroom`, and the
   Tier-1 branch) — so my correction states the *mechanism*, not the exact final range.
10. **`SECTION_TRANSITIONS.md` intent.** It reads as a design memo whose present-tense sections were
    accurate when written (git history suggests a v0.4.x-era document) and whose roadmap has since
    shipped. I flag both directions rather than calling the whole file "wrong".
11. **Sub-agent-verified items** folded into §6.11 (`X1`-`X24`): the ONNX graphs were decoded by
    parsing protobufs and cross-checked against `training/models/groove_renderer.py` and
    `training/models/groove_renderer_summary.json`; the UI-widget absence claims are grep-based over
    `AccompanimentEditor.*`. I re-read the cited `src/` lines myself; I did not re-run the ONNX
    decode or the training-side reads.
