# Musicality & Rock/Metal Pivot Plan

**Status:** Planning memo (major plan). Not legal advice — verify each dataset's current terms on its official page before redistribution or commercial use.
**Implementation status (v0.9.0):** **P0 and P1 are implemented** — Workstream A (A1 bass engine incl. the previously-dead `bassEvents`, A2 groove/humanisation + swing knob, A3 dynamic contrast + ghost notes, A4.1 rock pattern set) plus B1 genre-preset scaffold and B2 tempo unification. See `CHANGELOG.md` [0.9.0]. **P2–P4 remain** (C1–C6 data strategy: E-GMD stats, Lakh genre subsets, capture+human labels, DadaGP, Slakh/MoisesDB; A4.2 parameterized grooves; A5.2 smart reactive form; B3 rebrand). `bass_model.onnx` retirement decision is still open (A1.3).
**Date:** 2025 (codebase snapshot: v0.8.x unified Mel-CNN pipeline)
**Scope:** Raise the plugin's *musical ceiling* (what it can express) and *data ceiling* (what it can learn), then pivot genre positioning from "metal-only" to "rock-first, metal-as-a-preset."

---

## 0. TL;DR

The plugin is not "musical/playable" for a reason that is **only partly data**. Two ceilings exist:

1. **Musical ceiling (binding, and fixable with no new data).** The plugin can only *select* one of 22 hard-coded GM 4/4 patterns and play a root-note bass. It has no groove (timing jitter is white noise), no dynamic contrast (verse ≈ chorus loudness), and the carefully-authored bass lines are **dead code** that never reaches the MIDI output.
2. **Data ceiling (real, but secondary).** Training labels come from the plugin's own rules (circular), so the ML model can never be more musical than the rules. The datasets are genre-mismatched (Lakh = pop/rock/jazz, not metal).

**Recommendation:** Fix the musical ceiling first (Workstream A — cheap, no data, immediate audible payoff), then pivot to rock-first (Workstream B) and upgrade data (Workstream C). A rock pivot is a *strict superset* of the current metal identity and unlocks dramatically larger, better-matched datasets.

---

## 1. Diagnosis — the musical ceiling (focused pass)

### 1.1 What actually plays today

Confirmed from source, `src/midi/PatternPlayer.cpp` `process()`:

| Voice | Code path | Reality |
|---|---|---|
| Drums | `emitDrumEventsForRange()` | Hard-coded pattern events, ±10 velocity + ±2 ms white-noise jitter |
| Bass (PhraseLearner locked) | `triggerLearnedBassNote()` | Mirror of detected riff rhythm/pitch |
| Bass (default) | `emitBeatAlignedBass()` | **Root note only**, beats 1/3 (or 1/2/4 by section), **fixed velocity 100**, **no humanization** |
| Bass (`MidiPatternLibrary` `bassEvents`) | **never called** | ⚠️ Dead code — 20 authored bass lines (intervals, rhythms) are never emitted |

**Finding 1 (new):** `MidiPatternLibrary.cpp` authors detailed bass lines in `MidiPattern::bassEvents` (e.g. `buildChorusMid()` plays root → +5 → root → +7, i.e. root/fourth/fifth movement). `PatternPlayer` never reads `pattern.bassEvents`. The bass that actually plays is a metronome: one root pitch, on a coarse beat grid, fixed velocity, no dynamics, no microtiming. (`grep bassEvents src/` shows writes in the library only; the player has no read site.)

**Finding 2 (new):** Tempo is now **DAW-host-authoritative** (`AccompanimentProcessor.cpp:387–405`). The old onset-detection BPM path (`OnsetDetector`, `TempoStabiliser`) no longer exists in `src/analysis/`. This is a *good* change for musicality — tempo is now solid and host-synced — but it means the "zero manual tempo" value prop is now "host tempo or a 40–300 knob" (`AccompanimentEditor.cpp:52`). Any plan item referencing the removed onset detector is obsolete.

### 1.2 The six binding constraints on "musical/playable"

1. **Bass is a metronome, not a bassline.** No harmony beyond a single root, no dynamics, no humanization, and the authored material is dead code.
2. **Groove is white noise.** `humanVel()` = base ± 10 uniform; `humanSamples()` = ± 2 ms uniform (`PatternPlayer.cpp:101–111`). Real drumming has a *structured* microtiming and velocity hierarchy (backbeat slightly late, hats on-grid, ghost notes 30–55, accent on 1). White noise sounds robotic in a different way than perfect quantization — it never sounds *human*.
3. **No dynamic contrast.** Verse patterns sit at ~108–115 velocity and chorus at ~118–125 (`MidiPatternLibrary.cpp`) — a near-inaudible 10–15 difference. Real rock has verse backbeat ~95–105 and chorus ~115–125, plus cymbal changes (closed hats → open/ride/crash). Contrast is what makes a song "breathe."
4. **Pattern library is hard-coded and metal-only.** 22 patterns, all 4/4, GM kit, mostly one-bar loops, oriented to thrash/death idioms (blast, thrash, double-kick). Missing the rock vocabulary (shuffle/swing, 6/8, punk d-beat, half-time-in-loud, ballad). Fills (17–19) are selectable patterns, not contextual "3 bars groove + 1 bar fill" phrasing.
5. **Selection, not generation.** `IInference` maps features → one index. The output can only ever be one of 22 canned loops; it cannot *make* a groove.
6. **Rule-oracle circularity.** Training labels = `PatternRules::rulePatternForState()`. The ML model learns to imitate rules it can never exceed. On live audio the model is additionally broken by a normalization mismatch (centroid std clamped to `1e-8`).

### 1.3 Why datasets alone won't fix it

More/better data improves **which** pattern is selected, not **how** it is played. A rock-pivot + E-GMD upgrade is worthwhile, but it layers on top of Workstream A. If you ship better selection into a metronome bass + white-noise drums, it will still not be "musical/playable."

---

## 2. Strategic direction — rock-first, metal as a preset

**Positioning:** "Rock/Metal Accompaniment" — default = rock/pop-rock/hard-rock; metal = a heavier preset/mode.

Why this is a strict win:

- **Data:** Lakh MIDI (the only large corpus you have) is naturally pop/rock/jazz. A rock default makes it *in-distribution* instead of "wrong genre." GMD's human drumming skews rock/funk. DadaGP (26k GuitarPro songs) is rock/metal-heavy. Metal-specific data is effectively *only* DadaGP's metal subset + ProgGP (173 songs).
- **Product:** rock is a 100× larger market than any single metal subgenre, and "metal" can be retained as a preset so no existing user/identity is lost.
- **Musical ceiling:** rock idioms (groove, swing, ghost notes, dynamics) are exactly the ones the current library lacks; building them for rock also improves the metal preset.

Keep the current plugin identity intact: preserve the name/bundle (`com.ng.MetalAccompaniment`, `MtAc`) or rebrand at a minor-version boundary — this is a marketing decision, not a technical one. Technically, treat it as "add a genre preset dimension."

---

## 3. Workstream A — musical ceiling (highest priority, no new data required)

### A1. Bass engine — fix the dead code and make it musical

**Problem:** authored `bassEvents` are dead; live bass is a root metronome.

**Fix (staged):**

1. **Wire `pattern.bassEvents`** into `PatternPlayer::process()` (emit them like drum events) so the authored lines actually play. *This is a one-line-adjacent change that immediately restores intended behavior.*
2. **Replace the coarse beat-grid bass with a real bass engine** (`src/midi/`):
   - **Harmony map from root** (root already tracked via `StablePitchTracker`): root / octave-down / perfect fourth / perfect fifth / minor third — selected per section and intensity. Example: chorus walks root→fifth→octave; verse holds root with occasional fourth. Reuse the *intent* already expressed in the dead `bassEvents`.
   - **Accent structure:** velocity accent on beat 1, slightly softer on 3, passing notes quieter. Humanize ±5 (bass should sit *slightly behind* the kick for pocket, ~1–3 ms).
   - **Note length/articulation:** whole/half for verse/breakdown, quarters for chorus, 85% gate already exists — keep but make it a parameter.
3. **Decide the fate of `bass_model.onnx`** (16-step generative). It is currently not in the live path (the processor calls `setBassParams`, not the ONNX bass). Either integrate it properly in a later phase or retire it — dead/parallel code is a correctness liability.

**Acceptance:** bass follows the guitarist's root with audible harmonic movement and dynamics; "verse" and "chorus" bass are distinguishable by ear.

### A2. Groove & humanization — structured microtiming + velocity hierarchy

**Problem:** white-noise jitter reads as "robotic, but noisy."

**Fix:**

1. **Velocity hierarchy** (replace uniform ±10): a per-grid-position accent model — beat 1 > backbeat (2/4) > 8th-note hats > off-16th ghost notes (30–55). Derive the hierarchy from **GMD** (its velocity distributions per grid cell) and bake it as a "groove template" struct.
2. **Structured microtiming** (replace uniform ±2 ms): per-voice offsets — kick on-grid or ~1 ms early, backbeat snare ~2–8 ms late (the "laid-back" feel), hats on-grid with ~1 ms variance, ghost notes slightly early. This is the difference between a metronome and a drummer.
3. **Swing parameter** (0–100%): offset 8th-note events by swing ratio, mapped to a UI knob. Critical for rock's swung/loping feel.
4. Keep `juce::Random` but make it seed-stable and use a *bounded gaussian* around the structured offset rather than pure white noise.

**Acceptance:** A/B the same pattern with jitter off vs on — the "on" version should feel like a drummer, not like quantization noise.

### A3. Dynamic contrast & articulation

**Problem:** verse ≈ chorus loudness; no ghost-note dynamics; GM-only articulations.

**Fix:**

1. **Per-section velocity presets** applied as a *gain offset* over the pattern's base velocities: verse backbeat ~100, chorus ~118–122. Implement as a multiplier in `emitDrumEventsForRange` keyed off the active `StructureState`/section, not hard-coded into 22 patterns.
2. **Add ghost notes** to verse/breakdown patterns (snare 30–55 between backbeats).
3. **Articulation map:** add rimshot (37), side-stick (37 alt), pedal hat (44) transitions, and flam pairs where idiomatic. Keep GM for compatibility; note the sound source in the DAW determines final timbre.

### A4. Pattern library v2 — parameterized, rock-first

**Problem:** 22 hard-coded metal loops; no rock idiom; fills are not contextual.

**Fix (staged, do the cheap part first):**

1. **Quick:** add a small rock-first pattern set — rock backbeat (closed hat), half-time rock, shuffle/swing 8ths, 6/8 feel, punk d-beat, and a ballad. Keep the existing metal set as the "heavy" pool.
2. **Structural:** move from *fixed patterns* to *parameterized grooves* — a pattern = (kick grid, snare grid, hat/ride choice, velocity profile, swing, ghost density, half-time flag). Then "verse × swing 30% × energy 0.6" is *generated*, not enumerated. This is the real ceiling-raiser and sets up Workstream C4 (generative accompaniment).
3. **Contextual fills:** play "N bars groove + 1 bar fill" at section boundaries (the sequencer already knows `isLastBar()`), instead of filling only via separate selectable patterns.

### A5. Structure & reactivity

**Problem:** two disjoint modes — fixed song form (`StructureSequencer`, "play along") vs reactive (`IInference`). Neither is fully musical.

**Fix:**

1. Make the reactive path's selection *stateful* (hysteresis already exists in `StructureTagger`; ensure pattern changes are bar-quantized and transition-crashed — already partially handled in `PatternPlayer`).
2. **Merge the modes:** in "smart" mode, the sequencer's section (INTRO/VERSE/CHORUS/…) is *suggested* by the form but *confirmed/adjusted* by live intensity, so the guitarist can steer it. This is the "reacts to the guitarist" value prop, made musical.
3. Keep the manual form mode for users who want to play along to a fixed structure.

---

## 4. Workstream B — genre strategy (rock pivot)

1. **Add a "genre/preset" dimension** (JSON config, as the existing audit's F14 already proposes): `rock`, `hard-rock`, `punk`, `metal`, `sludge`, etc. Each preset selects: pattern pool, velocity profile, swing default, half-time bias, and BPM range. This is how "metal" survives as a preset while rock becomes the default.
2. **Unify tempo ranges:** the slider and `PatternPlayer` already span 40–320/40–300; ensure every clamp site agrees (no stray 80 BPM floors from the removed onset path — verify `pattern_rules.h` thresholds, which still assume 120/160 metal BPM bands).
3. **Marketing/README:** reposition as rock/metal; update `README.md`, `PROJECT.md`, `PLUGIN_GUIDE.md`.

---

## 5. Workstream C — data strategy

### C1. Adopt E-GMD (Expanded Groove MIDI) — biggest drum-quality win

- **What:** 444 h audio / ~43 h drums with **aligned drum MIDI**, human + synthesized performances. For a *rock* drum model it is now the right primary source.
- **Use:** learn velocity hierarchy + microtiming statistics (feed A2), and train a drum "groove encoder" rather than the proxy-feature classifier.
- **License:** verify on [E-GMD](https://magenta.withgoogle.com/datasets/e-gmd) / [Zenodo](https://zenodo.org/records/4300943). GMD itself is CC-BY 4.0.

### C2. Lakh MIDI — use the genre tags you're ignoring

- **What:** ~176k MIDI files; `lmd_matched` ≈ 45k with **Million Song Dataset genre tags** ([LMD](https://colinraffel.com/projects/lmd/)).
- **Fix:** replace blind channel-10 + BPM filtering with **genre-weighted rock subsets** (MSD tags: rock, alternative, punk, metal, hard rock). This converts "wrong-genre bulk" into "in-distribution bulk" under a rock pivot.
- **Also:** replace header-BPM with content-derived tempo (your own audit §5.7 flags header BPM as unreliable).

### C3. DadaGP — articulation/rhythm context (the missing "riff grammar")

- **What:** 26,181 GuitarPro songs, heavily rock/metal ([DadaGP](https://github.com/d-kitamura/dadaGP)).
- **Use:** NOT for accompaniment directly — for learning **palm-mute vs open-chord vs single-note articulation patterns and riff rhythm**, which your `EnergyAnalyser`/`StructureTagger` currently can't distinguish well (distortion collapses centroid). This feeds the "playing style" classifier.
- **Complement:** [ProgGP](https://ar5iv.labs.arxiv.org/html/2307.05328) (173 progressive-metal tabs) only if you keep a strong metal-preset story.

### C4. Slakh2100 / MoisesDB — only for *true* accompaniment generation

- **What:** Slakh2100 = 2,100 synthesized multi-tracks with clean stems ([HF](https://huggingface.co/datasets/schism-audio/slakh2100)); MoisesDB = 45 real multi-tracks, 12 genres.
- **Use:** the *only* route to simultaneous drum+bass+guitar supervision. Adopt **only if** you commit to A4's "generative accompaniment" end-state; otherwise it's overkill for pattern selection.

### C5. Real-audio captures + human labels — break the circularity

- **What:** `FeatureCapture.cpp` already records real guitar features (13k+ frames exist) but they are unused for training.
- **Fix:** collect 30–60 min of annotated playing (you play a verse riff, tag "verse"; palm-mute chug, tag "heavy"; etc.), and train a small classifier on **audio features with human labels** instead of the rule oracle. This is the single most important ML correction and works *with* the rock pivot (rock playing is far easier to self-annotate than metal subgenres).
- **Normalization fix (prereq):** remove baked normalization from the ONNX graph or recompute stats from captures (audit §8.1). The `1e-8` centroid std makes live inference effectively random.

### C6. License checklist (per dataset, before redistribution)

| Dataset | Primary license (verify current) | Notes |
|---|---|---|
| GMD / E-GMD | CC-BY 4.0 (GMD); confirm E-GMD | human + synthesized drum audio/MIDI |
| Lakh MIDI | CC-BY (via [LMD page](https://colinraffel.com/projects/lmd/)) | MSD-matched subset + tags |
| DadaGP | CC-BY 4.0 (confirm) | GuitarPro-derived tokens |
| Slakh2100 | permissively released (confirm) | synthesized audio |
| MoisesDB | research, non-commercial likely | real stems — check before shipping |

---

## 6. Roadmap & sequencing

| Phase | Scope | Exit criterion |
|---|---|---|
| **P0 (days)** | A1.1 (wire `bassEvents`), A2.1 (velocity hierarchy), A3.1 (section velocity offsets) | Bass has harmony+dynamics; verse/chorus are audibly distinct |
| **P1 (1–2 wks)** | A2 (microtiming, swing knob), A3.2 (ghost notes), A4.1 (rock pattern set), B2 (tempo unify), B1 (preset scaffold) | Rock idiom patterns present; swing/microtiming audibly human |
| **P2 (2–4 wks)** | C1 (E-GMD stats → groove template), C2 (Lakh genre subsets), C5 (capture + human labels), C5 normalization fix | Groove template is data-derived; model trained on real audio, not oracle |
| **P3 (4–8 wks)** | A4.2 (parameterized grooves), A5.2 (smart reactive form), C4 (Slakh/MoisesDB *if* generative) | "Generated" groove responds to guitar + section; ML beats rule baseline on a held-out human-labeled set |
| **P4** | C3 (DadaGP articulation model), B3 (docs/rebrand), full eval | Articulation classifier improves palm-mute/open-chord discrimination |

**Sequencing rationale:** P0/P1 are pure C++ changes with immediate audible payoff and zero data risk — do them first so every later data investment lands on a musical foundation. P2 fixes the *circular* training before adding more data (more data through a broken oracle is wasted). P3 is the architectural bet; only commit to it after P0–P2 validate the direction.

---

## 7. Metrics & success criteria

- **Groove feel (subjective, but gate it):** same pattern with humanization on vs off — blind A/B by a musician; "on" must win for "feels like a drummer."
- **Dynamic contrast:** measured velocity delta between verse and chorus backbeat ≥ 15 (currently ~10).
- **Bass harmony:** bass root follows guitar (existing), *plus* ≥ 1 harmonic interval per bar in chorus and an accent on beat 1.
- **Selection quality:** on a **human-labeled** rock capture set (not rule oracle), pattern/state accuracy ≥ 0.75 macro-F1, with all classes non-zero (current model has 4/7 dead classes).
- **Latency/CPU regressions:** none — all A-stream changes are audio-thread-safe (no alloc, atomics only), matching existing constraints (<30 ms, <15% CPU).

---

## 8. Risks & open questions

1. **Dead-code cleanup scope:** `bass_model.onnx` and the old proxy pipeline (`build_dataset.py`, `train_gmd.py`) may be superseded by C5. Decide whether to retire or integrate before P2, to avoid two competing ML paths.
2. **License diligence is on you:** this memo is a solo-dev gate, not counsel.
3. **Rebrand timing:** a bundle-ID/name change forces DAW re-scan and saved-session implications; coordinate with a minor version bump.
4. **Parameterized-groove scope (A4.2):** largest single build; de-risk by prototyping one "generated" groove in a spike before committing the whole library to the new representation.
5. **Human-label effort (C5):** 30–60 min of annotated playing is a real time cost but is the only way to break the rule-oracle circularity; it compounds in value across all future models.

---

*Prepared from direct source review of `src/midi/MidiPatternLibrary.{h,cpp}`, `src/midi/PatternPlayer.{h,cpp}`, `src/inference/pattern_rules.h`, `src/AccompanimentProcessor.cpp`, and `src/analysis/*`.*
