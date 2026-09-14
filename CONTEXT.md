# Context — Domain Glossary

Shared vocabulary for **fuzzyband / MetalAccompaniment**. Use these terms in
design discussion, code comments, tests, and architecture reviews. If a concept
isn't here and gets used as a module name, add it.

This file is a *living* glossary. Terms are added as they crystallise. It does
not replace `docs/BASS_MIRRORING.md` (the mirror contract), `ARCHITECTURE.md`
(current architecture), or `AGENTS.md` (build rules) — those carry the
authoritative detail; this is the name layer that ties them together.

---

## Voicing

**Bass voice**
The single monophonic bass voice (MIDI ch. 2) the plugin emits. There is exactly
one, so every producer competes for it. Ownership is the subject of the plugin's
most-repeated bug. Source: `docs/BASS_MIRRORING.md` §2, `PatternPlayer.h`.

**Producer**
One of the things that can sound on the bass voice. See the five below. A
producer *requests* the voice; it does not own it.

**Mirror** (a.k.a. live mirror)
The bass producer that follows the guitarist's detected attacks in real time —
one note per attack, at the played pitch class, held through a sustain. The
user-facing contract is "the bass plays what I am playing, as I play it."
Source: `docs/BASS_MIRRORING.md` §1.

**Grid line**
The authored / harmonic bass producer that plays on the drum grid. Two flavours:
*GridAuthored* (`pattern.bassEvents`, transposed to the live root) and
*GridHarmonic* (root/fourth/fifth/octave built from the guitarist's root). When
the guitarist is audible the grid is a *gap-filler*, never a layer.
Source: `PatternPlayer.h` `emitPatternBass` / `emitHarmonicBass`.

**Frozen riff**
The bass producer that loops a captured 4-bar riff snapshot (the `RiffA` /
`RiffBLocked` engine phases). It replays learned note-on/gate data, not live
attacks. Source: `PatternPlayer::triggerLearnedBassNote`, engine `RiffA`.

**Harmony fallback**
The *behavioural contract* that the grid/harmony line resumes when the guitarist
stops. Not a producer — a rule about when the grid producer is allowed to sound.
Silence, not the absence of attacks, is the gap. Source: `docs/BASS_MIRRORING.md`
§4.3, `PatternPlayer::setGuitarAudible`.

**Pickup**
A short approach-to-tonic bass note armed at a section hand-off
(`armBassLeadIn`). A bass producer, but not one of the four the mirror bug fights
over. Source: `PatternPlayer::bassLeadInArmed`.

**BassVoice**
*(architecture module — added 2026-09)* The internal module inside
`PatternPlayer` that owns the bass voice: it receives producer requests, resolves
which producer sounds while the voice is held, emits the note-on/note-off pair,
and reports provenance (which producer owns the voice right now). The 1.0.3
ownership contract lives here, in one place. Not a new conversation concept — the
module that makes "bass voice" enforceable rather than implied.

---

## Engine

**Accompaniment engine**
*(architecture module — added 2026-09, not yet built)* The module that owns the
musical state and decisions: the engine phase machine, riff lock, transition
hold, section progress, riff capture, and the live bass mirror producer. Its
interface is one block of inputs in, read-only status out; it drives
`PatternPlayer` and owns `PhraseLearner` / `StructureSequencer`. It contains no
JUCE types and no DSP. `AccompanimentProcessor` becomes the adapter that
translates host transport, APVTS and the MIDI buffer. Replaces the ~1 300-line
`processBlock` body, which mixed the engine, display, analysis and MIDI.

**EngineInput**
One block's worth of engine inputs: block size, `sampleRate`, the transport
clock (`clockSample`) and the monotonic clock (`hostSampleTime`) as **separate
fields**, BPM, rolling flag, the analysis frame, user intents, and params. The
two clocks are deliberately distinct — see invariant 9 and `PITFALLS` §3; the
engine must never merge them.

**EngineStatus**
The engine's read-only per-block output: current engine phase, section
progress, riff snapshots, and display values. The processor publishes these to
its existing atomics and UI readouts; no new cross-thread channels.

**Golden MIDI snapshot**
A byte-exact capture of the MIDI stream for a fixed scripted session
(deterministic transport, fixture audio, scripted Play / Record / lock-expiry /
seek), at 128 / 512 / 2048 block sizes. The behaviour-freeze net for the engine
extraction, and for any change that touches rendered MIDI (invariant 5).

**Engine phase**
One of the nine states of the accompaniment state machine: `Idle`,
`PlayCountIn`, `PlaySection`, `RecWaitBar`, `RecCountIn`, `RecCapture`, `RiffA`,
`RiffBListen`, `RiffBLocked`. Source: `AccompanimentProcessor.h`.

**Riff lock**
Auto- or user-engaged freeze of the drum groove onto a repeated riff, with the
bass looping the learned snapshot. Also called "groove lock". Source:
`grooveLocked` / `EnginePhase::RiffA`.

**Transition hold**
The post-lock contrast section (B/C/D/E) played for a fixed number of bars
before the engine always returns to the locked riff (A). Source:
`PostLockPhase::TransitionHold`.

**Section phase**
The *derived*, UI-facing four-value view used for the one consistent
"bar X of Y / N left" readout: `Idle` / `Play` / `Lock` / `Transition`. Distinct
from engine phase — it exists so the three armed phases display identically.
Source: `AccompanimentProcessor::SectionPhase`.

**Attack**
A detected guitar pick onset — the input event that drives the live mirror. Not
an "onset" in the generic DSP sense; it is the specific verdict of the attack
detector after its four-term predicate. Source: `PhraseLearner::detectAttack`.

**Attack detector**
*(architecture module — added 2026-09, not yet built)* The module that owns the
attack predicate and its state (decay-recency window, trough tracker, rise
latch, min-interval gate). Deliberately **not** named `OnsetDetector`:
`PITFALLS_AND_INVARIANTS.md` §1.2 records that phantom name as a recurring
time-sink — no such class exists.

**AttackVerdict**
The attack detector's per-block output: whether an attack was accepted, the
primary reason it was blocked, the four predicate-term booleans, and the numeric
margins (`troughMargin`, `riseRatio`). Exists so the `clearsFloor` starvation
hypothesis (`docs/BASS_MIRRORING.md` H1) can be measured per block rather than
inferred from cumulative counters.

**Blocked reason**
The `AttackVerdict::Blocked` enum naming which predicate term refused the block:
`None` (accepted), `NoRecentFall`, `NoSharpRise`, `TroughTooShallow`,
`BelowAmplitudeFloor`, `MinIntervalGate`. Diagnostic only — behaviour does not
depend on the reason.

---

## Inference

**Inference backend**
An adapter satisfying `IInference` that maps guitar analysis to a drum pattern
index. Two exist: `RuleBasedInference` (scalar features) and
`MetalGrooveInference` (mel-CNN). The seam is real, not hypothetical.

**Audio window**
The chunk of raw input audio an inference backend declares it needs
(`getAudioWindowSize()`). `MetalGrooveInference` needs the 512 ms window behind
its 64×40 mel frame; `RuleBasedInference` needs none. The audio window is pulled
and transformed on the inference thread — never on the audio thread.

**Mel frame**
The transformed input `MetalGrooveInference` feeds its ONNX session. Its
production (the mel spectrogram) is an internal detail of that backend, not a
processor step.

---

## Anti-vocabulary

Names that **must not** be used, because they are phantoms in this repo's docs:

- `OnsetDetector`, `TempoStabiliser`, `OnnxInference`, `BeatTracker` — do not
  exist in `src/`. See `docs/PITFALLS_AND_INVARIANTS.md` §1.2, §1.5.
