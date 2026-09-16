# Fuzzyband Rules — How the Plugin Thinks

**Audience:** anyone who wants to understand the product without being a DSP engineer.  
**Authority:** verified against live source at **v1.0.27** (`CMakeLists.txt` line 4).  
**Companion docs:** `docs/CONTEXT_HANDOFF.md` (engineer briefing), `docs/BASS_MIRRORING.md` (bass bug history — some early sections are stale; trust this file + code for current behaviour).

If a older doc disagrees with this file, **believe the code**. This document exists because that happens often.

---

## 1. What the plugin is (one paragraph)

You play guitar into a DAW. Fuzzyband listens to that audio, figures out *how hard* you are playing and *which notes*, and emits **MIDI** — not audio — to drive a drum sampler and a bass instrument. Tempo comes from the **DAW**, not from tapping or guessing. The plugin’s job is to feel like a metal/rock rhythm section that reacts to you in real time.

### Hard input rule: clean DI, before any FX

**The plugin must sit first on the guitar chain** — clean DI (or a clean DI-level preamp), *before* amp sims, distortion, cab, compression, or other colour. Analysis, attack detection, and pitch tracking are designed for that signal.

| In scope | Out of scope |
|---|---|
| Clean DI dynamics and pick attacks | Amp / pedal distortion |
| Palm-mute as a **playing technique** on DI | Tuning detectors for compressed, harmonically flattened FX |
| Drop-C / low tuned DI fundamentals | “It failed because the tone was distorted” as a product bug |

Older comments and docs that say “distorted palm-mute” often meant “aggressive muted chugs.” Treat that as **historical wording**, not a requirement that the input be distorted. Agents must not invent distortion-hardening that hurts clean DI.

| Output | MIDI channel | What it does |
|---|---|---|
| Drums | 10 (GM drums) | Pattern grooves, fills, click during count-in |
| Bass | 2 | Either a **live mirror** of your notes, a **frozen learned riff**, or a **harmony fallback** when you stop |

The guitar audio is analysed and discarded. Nothing records your performance as audio.

---

## 2. Plain-language glossary

| Term | Meaning here |
|---|---|
| **BPM / tempo** | How fast the song is. Always taken from the DAW playhead when available. |
| **Beat / bar** | Musical time. A bar is usually 4 beats. The plugin’s clock follows the DAW’s sample position. |
| **16th note** | One sixteenth of a bar in 4/4 (four slots per beat). Used for riff grids and soft bass snapping. |
| **Pitch / pitch class** | Which note (C, C#, D, …). “Pitch class” ignores octave — C2 and C3 are the same class. Bass is folded into a low register (roughly C2–B2). |
| **Attack / pick** | The moment you strike a note. The mirror fires from detected picks, not from continuous loudness alone. |
| **RMS / energy** | How loud the signal is over a short window. Used for “are you playing?” and “how hard?” |
| **SILENT / SOFT / LOUD** | Three coarse energy states. Not the same as song sections (VERSE/CHORUS). |
| **Song form / section** | The authored structure: INTRO, VERSE, CHORUS, BREAKDOWN, SOLO, OUTRO. |
| **Mirror** | Bass tries to play *what you play, when you play it*. |
| **Grid / harmonic bass** | Authored or root/fifth fallback line that fills gaps when you are **not** playing. |
| **Frozen riff** | A captured snapshot of your bass notes, replayed on a 16th grid (Record mode lock, or Play section recall). |
| **Monophonic voice** | Only **one** bass note can ring at a time. Mirror and grid must not layer — whoever “owns” the voice wins. |
| **ONNX / mel-CNN** | Optional ML that picks among 28 drum patterns from a mel spectrogram. Falls back to simple rules if the model fails to load. |

---

## 3. The two user modes

There is no “mode” parameter in the settings tree. You arm one of two workflows with the UI buttons.

### 3.1 Idle (default)

Plugin is silent. Analysis may still run for readouts, but MIDI stays off until you arm **Play** or **Record riff**.

### 3.2 Play — “play through my song form”

1. Press **Play**.
2. One-bar **count-in** click.
3. The plugin walks your **section list** once (INTRO → VERSE → … → OUTRO).
4. Drums come from per-section pattern pools for the chosen **genre**.
5. Bass **mirrors** your live playing while you are audible.
6. First time through each section name, it **listens and stores** a bass sketch for that section. When that section returns it **replays** the stored sketch while **re-capturing** in parallel, so the memory keeps tracking what you actually play (Step-2 per-section learning). A pass where you rest does not wipe a good memory.
7. When the form ends, Play turns off and the plugin goes Idle.

Play does **not** loop the form (loop is forced off). Play cancels any Record session.

### 3.3 Record riff — “learn this riff and lock it”

1. Press **Record riff**.
2. Wait for the next bar → **count-in** → **4-bar capture** on a 16th grid.
3. Take commits → **Riff A**: drums + bass freeze on that take for **Lock bars**.
4. Then a **transition** contrast (Riff B listen / optional lock) for **Transition bars**, cycling A → B → A → C… according to **Transition sections**.
5. During the **first** visit to a contrast slot the bass **mirrors you** (and captures in parallel, exactly like Riff A). On every **later** visit to the same slot it **replays the learned contrast riff**, so the bass keeps going when you rest instead of going silent.
6. **Forget** wipes the riff and returns to Idle.

Stopping Record early with enough occupied 16ths still commits; too little material aborts.

---

## 4. Engine phases (what is happening under the hood)

These are internal states. The UI summarises them as Idle / Play / Lock / Transition.

| Phase | You experience | Drums | Bass |
|---|---|---|---|
| **Idle** | Armed off | Silent | Silent |
| **PlayCountIn** | Click for one bar | Click only | No song bass yet |
| **PlaySection** | Song form running | Section/genre pools | Live mirror; learn/replay per section |
| **RecWaitBar** | Record armed | Click | Waiting |
| **RecCountIn** | One bar click | Click (kick on 1) | Waiting |
| **RecCapture** | 4 bars recording | Click continues | Stamping 16ths into the learner |
| **RiffA** | Locked to take A | Frozen pattern | Frozen riff snapshot |
| **RiffBListen** | Contrast section | Rotating contrast pool | Live mirror (first visit) **and** captures the contrast riff; a later visit to the same slot replays the stored one |
| **RiffBLocked** | Contrast locked | Frozen B pattern | Live mirror while transition hold; stored B is not the main live emit path |

**Post-lock TransitionHold** means: drums contrast; bass mirrors/rests with you — **no** harmony fill.

---

## 5. User controls

### 5.1 What you see in the editor

| Control | What it does |
|---|---|
| **Genre** | Chooses a groove preset (Rock, Metal flavours, Punk, etc.). Sets drum feel and default swing for that preset. |
| **Swing** | Delays off-beat 8ths for a loping feel (0 = straight, higher = more swung). |
| **Humanize** | How much Tier-0 drum ornamentation / human feel (ghosts, hat/ride colour, micro fills). |
| **Section list** | Editable song form: section type, bars, reorder, add/remove. This is the **real** Play structure. |
| **Play** | Arm Play mode (see §3.2). |
| **Record riff** | Arm Record capture (see §3.3). |
| **Forget** | Clear locked riff / return Idle. |
| **Lock (bars)** | How long Riff A holds (4–64, default 16). |
| **Transition (bars)** | How long each contrast holds (2–32, default 8). |
| **Transition sections** | How many distinct B/C/… slots before wrapping (1–4, default 2). |

Status readouts show energy state, pattern, progress, and a small scope. They are observational.

### 5.2 Parameters that exist but have no dedicated UI widget

| Param ID | Default | Effect |
|---|---|---|
| `outputGain` | 1.0 | Scales **dry guitar** passthrough only (not MIDI loudness). |
| `bpm` | 120 | Tempo **fallback** if the host does not report BPM. |
| `bassTranspose` | 0 | Bass octave: −12 / 0 / +12. |
| `songForm` | preset choice | **Created but unused** for runtime — the custom section list drives Play. |
| `loop` | false | Can enable form looping, but **Play always forces loop off**. |

---

## 6. How decisions are made

### 6.1 Tempo (always host-first)

1. DAW playhead BPM, if valid.  
2. Else APVTS `bpm`.  
3. Else 120.

There is **no** in-plugin tempo chasing. Older docs mentioning onset BPM / BeatTracker describe deleted code.

The drum/MIDI clock follows host sample time when the transport is rolling, and can free-run when the transport is frozen so jamming still works. Lock/transition *durations* use a monotonic sample clock so DAW loop wraps do not freeze schedules.

### 6.2 “Are you playing?” — structure energy

`StructureTagger` maps loudness into **SILENT / SOFT / LOUD** with hysteresis (so it does not flicker). Spectral centroid is largely unused. A “note still ringing” hold prevents jumping to SILENT mid-sustain too eagerly.

This state affects:

- Whether the guitar is treated as **audible** (drives bass mirror vs harmony).
- Compatibility filters for ML/rule pattern choices.
- Follow-mode dynamics when Play is not driving sections.
- Silence-based kit resets / transition crashes via `PlaybackGate` (after long silence, or after a short phrase breath).

It does **not** rename VERSE/CHORUS — those come from the song-form sequencer in Play.

### 6.3 Drum pattern selection

**When Play is on:** the song-form sequencer + genre section pools pick patterns. Background ML pattern commits are drained and **discarded** on purpose so Play stays musically authored.

**When following without Play overrides:** a mel spectrogram CNN (`MetalGrooveInference`) picks among **28** library grooves, with rule-based fallback. Style CNN runs but does not currently steer pattern choice hard.

Patterns live in `MidiPatternLibrary` (silent, verse/chorus/breakdown variants, fills, rock set including shuffle / d-beat / 6/8, etc.).

**Human feel on drums:**

- Genre `GrooveTemplate` — structured velocity hierarchy and microtiming (data-derived from Groove MIDI Dataset ideas), not pure white noise.
- `swing` and `humanize` knobs.
- Tier-0 ornaments (ghosts, hat/ride, micro-fills) scaled by humanize.
- Velocity also reacts to your playing energy.

### 6.4 Bass — three producers, one voice

Only one bass note rings at a time (`BassVoice`). Producers:

| Producer | When |
|---|---|
| **Mirror** | Live picks while listening (Play section / Riff B listen or locked). |
| **Frozen** | Riff A lock, or Play returning to a section with a stored take. |
| **Grid authored** | Pattern’s written bass line, only when guitar is **silent** and grid is allowed. |
| **Grid harmonic** | Root / fourth / fifth style fallback if the pattern has no authored bass. |
| **Pickup** | Short approach note at section hand-off (bypasses grid gate). |

**Arbitration rule (the product contract):**

> While you are audible, bass mirrors you.  
> While you are silent, harmony/grid may fill (except during TransitionHold, where bass rests with you).  
> Frozen lock/replay owns the voice when those modes are active.

Harmony must never quietly replace your mirror mid-phrase. That regression (“bass sounds like root/fifth instead of me”) is the project’s most repeated bug class.

---

## 7. Bass mirroring — pitch and beat matching

This is the behaviour you care about most: **same notes, same time feel**, without needing identical attack intensity.

### 7.1 The intended contract

When mirror is active:

1. Each accepted **pick** produces **one** bass note.
2. Pitch = your **pitch class**, placed in a low bass register, then optional octave transpose.
3. Timing ≈ your pick time, optionally nudged onto the nearest **16th** if within ±15 ms, then delayed ~40 ms so pitch can be measured at the attack. This delay applies **only while a riff is being learned** (see §7.3).
4. The note **holds** until the next pick, a real decay/silence, or you stop for ~1 s — it is not a short blip.
5. Legato / held pitch changes (no new pick) can retune the held bass after a short debounce, when the pitch tracker agrees.

You do **not** need the bass to match pick velocity or amp grind. Pitch + timing are the contract.

### 7.2 Pipeline (simplified)

```
Guitar audio
  → short onset loudness (~20 ms) at a fixed ~10.7 ms hop
  → AttackDetector: “was that a pick?”
  → Soft snap to nearest 16th if within 15 ms
  → Wait ~40 ms, run onset-aligned pitch (YIN)
  → Emit MIDI bass note (channel 2), held
  → BassVoice owns the voice; grid stays out while you are audible
```

Pitch is resolved two ways, both on the same fixed ~10.7 ms hop: an
**onset-aligned** window starting at the pick (primary note pitch), and a
**fixed-hop** estimate for the held-note / legato follow. The whole pitch chain
is hop-driven (not once per host block) so its behaviour is buffer-size
invariant — the same lesson the attack detector learned in 1.0.8.

Attack detection is deliberately **not** gated on pitch confidence: a pick is accepted from the onset envelope first; pitch is resolved *after* on the clean DI window. (Some older comments blame “distorted” YIN for this — under the clean-DI rule, the real reasons are attack-time uncertainty and muted/low notes, not amp distortion.)

### 7.3 What “in time” means here

| Mechanism | Effect |
|---|---|
| Host BPM + sample clock | Bass and drums share the same musical timeline as the DAW. |
| Optional 16th snap (±15 ms) | Tightens loose picks onto the grid without hard quantizing everything. |
| ~40 ms pitch window | **Learning-phase only.** The window must be long enough to name a drop-tuned low note (two periods of D2 ≈ 27 ms); shorter windows return the wrong note. It costs nothing once a riff is learned, because frozen/section replay is placed on the absolute 16th grid with no pitch analysis. |
| Hold / legato follow | Sustains and slides with phrase motion instead of chopping every note to a fixed gate. |
| Frozen riff path | Places notes on an absolute 16th grid from the capture origin (Record / section recall) — exact, no extra latency. |

Mirror is **not** forced onto every beat. If you play syncopation, the mirror should follow that syncopation (within the soft snap). Beat-matching means “locked to the song’s clock,” not “only on downbeats.”

### 7.4 What “right pitch” means here

| Mechanism | Effect |
|---|---|
| Onset YIN at the pick | Primary pitch for the emitted note (confidence > 0.25). |
| Learner / stable tracker fallback | Used when onset pitch is weak. |
| Fold to pitch class + C2–B2 | Same note name as you, in bass register. |
| `bassTranspose` | Whole-octave shift for the instrument patch. |
| Voice clamp ~MIDI 28–55 | Keeps notes in a playable bass range. |

Pitch confidence is `1 - CMNDF` at the chosen lag (classic YIN). Until 1.0.22 it
was a "spread between the two smallest CMNDF samples", which read ~0.00 on every
real DI window even when the pitch was correct — so every conf-gated stage fell
back to a stale note and ~45 % of mirrored pitches were wrong. Do not tighten a
conf gate without checking it against a real DI (`docs/BASS_MIRRORING.md` §16).

Chords: the system is **monophonic**. It hears a dominant pitch, not a full voicing. Power chords often read as the root (or sometimes the fifth) depending on the spectrum.

### 7.5 Why mirroring still fails (failure modes)

Diagnose with two separate questions:

1. **Did a pick fire?** (`AttackDetector` accepted / pending mirror queued)  
2. **Did mirror own the voice?** (or did frozen/grid win / was the plugin unarmed?)

Common causes of “wrong pitch or wrong time”:

| Symptom | Likely cause |
|---|---|
| Root/fifth line instead of your riff | Grid/harmony enabled while you still sound “audible,” or mirror never fired so you only hear fallback when you pause — historically Mode B arbitration. |
| Holes / missing notes | Attack detector starvation (no recent decay, shallow trough, no HF transient, min-interval gate) — Mode A. |
| Late or jumpy timing | Large DAW buffer (mitigated by fixed-hop feeding); snap fighting loose playing; pitch window delay. |
| Wrong note name | Onset window too short for very low DI notes; low YIN confidence → stale fallback; chord/fifth ambiguity. If the chain is post-FX, that is a **routing** bug, not something to “fix” in the detector. |
| ~40–56 ms late bass on the FIRST pass of a section | **By design** (learning-phase pitch window). Returns are frozen and grid-exact — see §7.3. |
| Sticks on old note through a slide | Legato follow retunes the held note once the fixed-hop pitch estimate agrees for ~3 hops (~30 ms). Low-amplitude windows are gated (no pitch on near-silence). |
| “Not mirroring” in Riff A / returning section | **By design** — frozen snapshot owns the voice. |
| Silence during TransitionHold when you stop | **By design** — no harmony bed under drum contrast. |
| Nothing at all | Not armed (Idle), or still in count-in / capture-only path. |

**Important methodology note:** ground truth is **real clean DI** takes (including DI palm-mute / drop-C) in `data/raw/` or Desktop Media stem `01-*`. Synthetic plucks can pass while real DI chugs fail — but do **not** validate against post-amp distorted audio; that is outside the product contract.

### 7.6 Offline agent DI / riff audit harness

Env-gated Catch tests in `tests/test_bass_mirror_play_realaudio.cpp`:

```bash
# Play-mode mirror audit (required: mono DI path + host BPM)
MA_DI_WAV=/path/to/01-fairo_di-….wav MA_DI_BPM=85 \
  ./build/MetalAccompanimentIntegrationTests "[di]"

# Play-mode audit on the take's real song form (so Step-2 recall is exercised)
MA_DI_WAV=/path/to/01-….wav MA_DI_BPM=170 MA_DI_FORM="VERSE:8,CHORUS:8,VERSE:8,CHORUS:8" \
  ./build/MetalAccompanimentIntegrationTests "[di]"

# Record-riff capture dump
MA_RIFF_WAV=/path/to/01-fairo_di-….wav MA_RIFF_BPM=85 \
  ./build/MetalAccompanimentIntegrationTests "[riff]"

# Pitch-estimator probe: dump the real PitchEstimator at given times (seconds)
MA_PROBE_WAV=/path/to/01-….wav MA_PROBE_TIMES="4.22,5.66,6.73" \
  ./build/MetalAccompanimentIntegrationTests "[pitch][probe]"
```

Rules for agents using this harness:

| Do | Don't |
|---|---|
| Feed **mono clean DI** stem `01-*` only | Feed `02` (drums) or `03` (bass) output stems |
| Always set `MA_DI_BPM` / `MA_RIFF_BPM` to the take's host tempo | Assume 120 BPM |
| Trust **rise-candidate outcomes** + BassVoice producer tags | Compare rates to external spectral-flux / librosa onset tools |
| Set `MA_DI_FORM` to the take's real song form so section recall runs | Assume the default single-section form exercises Step 2 |
| Treat missing BPM or stereo WAV as a **failed** audit | Treat silent `SUCCEED` skips as evidence |

Bad WAV / missing BPM fails the test hard (no quiet skip). Rates after Play count-in are reported separately from full-file averages.

---

## 8. What is built but not live

Useful so you do not chase ghosts:

| Piece | Status |
|---|---|
| Mel groove CNN + style CNN | **Live** (pattern select / style classify) |
| Rule-based pattern fallback | **Live** |
| GrooveRenderer ONNX (Tier-1) | Built + tested, **never instantiated** in the plugin |
| Generative bass ONNX | **Not wired** (retired path) |
| Structure ONNX head | **Not live** |
| Audio BPM / BeatTracker / OnsetDetector | **Deleted** |
| APVTS `songForm` / Play `loop` UI story | Param or behaviour mostly **unused / overridden** |

---

## 9. Deepening musicality — research directions

Priorities are ordered for **audible payoff that fits the current architecture**. Pitch/beat mirror reliability is the foundation; everything else sits on top.

### 9.1 Highest leverage (product pain)

1. **Make mirror pitch + pick timing boringly reliable on real DI/distortion**  
   Keep provenance tagging (who emitted each bass note). Drive AttackDetector + onset YIN from real `data/raw/` takes across buffer sizes. Prefer fixing starvation and wrong-class fallbacks over new generative bass models.

2. **Never let harmony fight the player**  
   The audible/silent gate is correct in spirit. Keep tightening release so rests feel intentional and sustains do not open a root/fifth bed. TransitionHold “rest with the player” is the right musical rule for contrasts.

3. **Richer frozen / Step-2 recall**  
   Per-section memory already exists. Improve capture density and gate coalescing so returning to VERSE sounds like *your* take, not a thin 16th sketch.

### 9.2 Strong musical upgrades (no new ML required)

4. **Phrase-aware fills**  
   Fill patterns (library indices 17–19) exist but are not automatic “bar 4 of the phrase” events. Schedule fills at section edges / last bars using the existing crash/lead-in hooks.

5. **Thicker authored bass lines for silence gaps**  
   Authored `bassEvents` are live again. Enrich rock/metal pocket (behind the kick by a few ms), better degree maps, and section-aware note density so the fallback feels composed when you stop.

6. **Genre presets as musical policy**  
   Groove templates already encode feel. Push genre further into: pool defaults, fill aggressiveness, bass pocket, and humanize ceilings so Rock vs Thrash are obviously different bands.

7. **Dynamic storytelling**  
   Verse vs chorus contrast is partly there (velocity maps, open hats/ride, notes-per-bar). Increase the *perceived* dynamic range and articulation changes so form breathes.

### 9.3 Perception → better choices

8. **Use style CNN as a soft prior**  
   Style is classified today but barely steers drums. After better `single_note` / palm-mute data (see `docs/SINGLE_NOTE_DETECTION_PLAN.md`), bias pools toward sparse vs dense grooves.

9. **Mel selection variety**  
   Top-K sampling already exists. Tune temperature and state gates rather than training a brand-new groove generator first.

### 9.4 Later / carefully

10. **Wire GrooveRenderer only after selection + mirror feel solid**  
    Tier-1 mutation fights the same ownership/clock invariants that caused nine mirror regressions. Treat it as a later ceiling raise, not a fix for “bass doesn’t follow me.”

11. **Generative bass models**  
    Retired for good reasons while mirror ownership was unstable. Revisit only with a clear arbitration rule: generative fills gaps; mirror owns audible playing.

12. **Data ceiling**  
    Rock-first corpora and human-labelled grooves raise *which* pattern is chosen and *how* templates feel. They do not replace fixing pick→pitch on the audio you actually play.

### 9.5 Explicit non-goals (for now)

- Reintroducing audio-derived tempo chasing.
- Matching pick intensity in the bass MIDI.
- Supporting or tuning for post-FX / distorted input (plugin belongs first on clean DI).
- Polyphonic chord-complete bass voicings.
- Trusting stale docs that still name BeatTracker, OnsetDetector, or `mirrorVoiceEndSample_`.

---

## 10. Mental model cheat sheet

```
DAW tempo + transport
        │
        ▼
   PatternPlayer clock ──► drums (genre pool / ML / frozen)
        │
Guitar ─┼─ energy → SILENT/SOFT/LOUD ──► audible?
        │                                    │
        ├─ picks → AttackDetector            │
        │              │                     │
        │              ▼                     │
        │         onset pitch ──► Mirror ────┤── BassVoice (one note)
        │                                    │
        └─ (when silent) authored/harmonic grid ┘

Play:  song form owns drum story; bass mirrors + per-section memory
Record: capture → freeze A → contrast B (mirror) → back to A
Idle:   silence until you arm something
```

---

## 11. Where to look in code

| Topic | Primary files |
|---|---|
| Modes / phases / controls | `src/AccompanimentProcessor.{h,cpp}`, `src/AccompanimentEditor.{h,cpp}` |
| Attack + learn | `src/analysis/AttackDetector.*`, `src/analysis/PhraseLearner.*` |
| Pitch | `src/analysis/PitchEstimator.*`, `src/analysis/StablePitchTracker.*` |
| Energy / hops | `src/analysis/EnergyAnalyser.*` |
| Bass ownership | `src/midi/BassVoice.*`, `src/midi/PatternPlayer.*` |
| Drum library / rules | `src/midi/MidiPatternLibrary.*`, `src/inference/pattern_rules.h` |
| Groove feel | `src/midi/GrooveTemplate.h`, `GrooveTemplateData.h` |
| Song form | `src/analysis/StructureSequencer.*` |

---

*Last verified against source: v1.0.27. When behaviour changes, update this file in the same change that updates the code — do not leave it to a later “docs pass.”*
