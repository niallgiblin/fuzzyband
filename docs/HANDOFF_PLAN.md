# Handoff Plan — Lock Rework (P0), Phase 2, Phase 3, and Regression Tests

**Prepared for:** the next agent (fresh context).
**Snapshot:** v0.9.13. All prior bass/mirror/lock work is in `CHANGELOG.md` [0.9.8 → 0.9.13].
**Read this first:** `docs/MUSICALITY_ROCK_PIVOT_PLAN.md` (the strategy doc), `ARCHITECTURE.md` (layers), `CHANGELOG.md` (recent history), and this file.

---

## 0. Where the product stands (so you don't re-derive it)

The plugin (`Metal Accompaniment`) listens to a guitarist and outputs drum MIDI (ch.10) + bass MIDI (ch.2) in real time. Two modes, one engine:

- **Generative / "follow" mode** (`playOn == false`): audio-thread analysis + background-thread inference pick a pattern every 2–4 bars; the **PhraseLearner** detects the guitarist's repeated riff and mirrors it on bass; the **groove lock** freezes drums+bass onto a detected riff.
- **Structure / "Play" mode** (`playOn == true`): a `StructureSequencer` plays a canned song form (INTRO/VERSE/CHORUS/…), patterns from per-section pools, with fills/crashes at boundaries.

### Bass pipeline map (the part you'll be changing first)

| Concern | File / location |
|---|---|
| Attack detection (RMS-transient, fall-armed; pitch-agnostic) | `src/analysis/PhraseLearner.cpp` `detectAttack()` |
| Attack recording + live riff mirror (fires `BassNote.trigger`) | `PhraseLearner.cpp` `process()` — the `if (attack)` block (~line 245) |
| "Following the riff" match (any-interval) + drift-unlock | same `process()` — the match is computed in the `if (attack)` block; unlock in `case State::Locked` |
| Pattern capture (len search) + pattern growth (re-lock) | `process()` `case State::Learning` (ascending len loop) and `case State::Locked` (growth block) |
| **Live-root retune** (the thing to freeze) | `PhraseLearner.cpp` `retuneToLiveRoot()` (~line 159), called from `AccompanimentProcessor.cpp` (~line 701) |
| Root tracking (pitch class → semitone offset) | `src/analysis/StablePitchTracker.{h,cpp}` |
| Groove lock state machine (engage/hold/expire/re-engage) | `AccompanimentProcessor.cpp` step **9b** (~lines 608–660) |
| Live-mirror gating + riff-active fallback suppression | `AccompanimentProcessor.cpp` step **9c** (~lines 663–715) |
| Bass rendering (learned note + authored `bassEvents` + harmonic fallback) | `src/midi/PatternPlayer.cpp` `process()`, `emitPatternBass()`, `emitHarmonicBass()` |
| Transition fills (drum) | `PatternPlayer.cpp` `emitTransitionFill()` + `AccompanimentProcessor.cpp` `chooseTransitionFillKind()` (line 33) |

### The bug pattern that kept recurring (why the test plan matters)

Every bass regression in this cycle was a *layer* issue, not a syntax bug:

1. **Wrong-note bug** (0.9.8): `StablePitchTracker` anchored pitch class at C but the processor folded it onto E2=40 → bass always a major third off. *Caught by:* unit tests on the tracker + processor mapping (were **not registered in CMakeLists** — the first symptom was "all tests pass but it's broken").
2. **Detector regression** (0.9.10): `rms > prev×1.2` never fired on real chugging (the 100 ms analyser RMS window smooths pulses). *Caught by:* a realistic-chug unit test feeding the actual windowed RMS.
3. **Pitch-gate starvation** (0.9.11): YIN confidence is bimodal on distorted guitar, so `pitchConf > 0.05` rejected almost every attack. *Caught by:* the same realistic test **with confidence fed as 0**.
4. **Sparse learned playback** (0.9.12): the lock captures a 2-note slice; playback = sparse staccato. *Caught by:* note-density measurement on real recordings.
5. **Mode-specific lock bug** (0.9.13): the groove lock suppressed the live mirror, but only follow mode engages the lock — so Play mode was fine and follow mode wasn't.

**Lesson for every change:** a fix that passes synthetic unit tests can still be broken on real audio, and a fix that works in one mode can be broken in the other. Ground every detector/root change in the real recordings under `data/raw/` (see §5).

---

## 1. P0 — Groove-lock rework (do this first)

### 1.1 User feedback (verbatim intent)

> "It locks on something but changes way too quickly. Remove the ability of the bass to change root note. When it locks into a riff it stays on that riff (some embellishment/fills welcome in both drums and bass but only a little) for as many bars as listed. When the bars are over, transition to something else smoothly, but listen to me and lock into something else if necessary."

### 1.2 Requirements

- **R1 — Freeze the bass note while locked.** While the groove lock is active, the bass plays the learned riff **note-for-note**. It must NOT transpose to the live root, and must NOT follow the live pitch of the current attack.
- **R2 — Fixed hold.** The lock holds for exactly `lockBars` bars (the existing UI slider, default 16). Remove the "returning to the riff extends the hold" behaviour — the window is fixed.
- **R3 — Light embellishment (optional, "only a little").** During the hold, allow *subtle* variation: drums — the existing ghost notes (GrooveTemplate `ghostDensity`) plus at most one small fill per 4 bars; bass — at most an occasional passing tone (e.g. the 5th/octave on beat 3 of the last bar of a 4-bar group). Must not read as "the riff changed".
- **R4 — Smooth transition at lock end.** When `lockBars` elapse: fire a transition fill/crash at the bar boundary, hand pattern selection back to the listener, and if the guitarist is playing a *new* riff, let it lock onto that. No abrupt pattern swap, no hard cut of the bass.

### 1.3 What to change (mechanism-level)

1. **Kill live-root retune while locked.**
   - `AccompanimentProcessor.cpp` ~line 700: the `retuneToLiveRoot()` call inside the `if (phraseLocked)` block. Remove it entirely (or gate it off). The user wants the *ability* gone, not just gated — cleanest is to delete the call and the `retuneToLiveRoot()` method (or leave the method but stop calling it; prefer delete to avoid dead code).
   - Result: the learned pattern's recorded pitches are authoritative during the lock. `StablePitchTracker` still runs (it feeds the *fallback* root when NOT locked) — that path is unchanged.

2. **Freeze the live-mirror pitch while locked.**
   - In `PhraseLearner.cpp` `process()`, the `if (attack)` mirror branch sets `result.midiNote = mapToBassRange(attackPitch)` (live pitch). While locked, the mirror should instead replay the learned riff's note — the pattern note for the current step (or the note the pattern would play). Design decision: either (a) use `pattern_[currentStep].midiNote`, or (b) drop the live-mirror entirely while locked and rely on the learned-playback only. **Recommend (b) for simplicity and correctness**: while `holdActive_`, the bass = learned playback only (which already loops the riff note-for-note); the live mirror remains only for the *not locked* (learning/following) states. Verify note density stays high (the 0.9.12 fix's purpose) via the pattern-growth + density tests.

3. **Stop the phase-jumping re-lock (pattern growth).**
   - The 0.9.13 pattern-growth block (`case State::Locked`, `following_ && patternLen_ < kMaxPattern && …`) calls `lockPattern()`, which re-aligns `playbackPhase_`/`playbackStep_` — a perceptible "jump" every time the pattern grows. This is a likely contributor to "changes way too quickly".
   - **Recommend:** capture the full-length pattern *before* locking (see §1.4) so growth-during-lock is unnecessary; or, if growth is kept, make it *phase-preserving* (don't reset the phase/step).

4. **Fixed hold (remove extension).**
   - `AccompanimentProcessor.cpp` step 9b: remove the `if (phraseLearner.justMatchedRiff()) grooveLockEndSample = hostSampleTime + lockDuration;` extension. On engage, set `grooveLockEndSample = hostSampleTime + lockDuration` once and leave it.

5. **Smooth release transition.**
   - On release (hold expiry), instead of the drain's immediate `drumHoldExpired` commit, emit a `TransitionFillKind::Release` (or Entry) fill + crash at the boundary, and let the listener pick the next pattern (already bar-quantized). Track the release as an explicit transition event (a flag the audio thread sets when the lock expires, consumed to arm the fill).

### 1.4 (Optional but recommended) Capture the full riff at lock, not a 2-note slice

The current lock fires at `attackCount_ >= 4` and, for a uniform chug, matches `len == 2` first. Two options, pick one and make it consistent with R1/R2:

- **A — delay the lock until more evidence:** attempt locking only when `attackCount_ >= 8` (or `>= 2 * desiredLen`), and scan `len` **descending** (longest first). Captures a denser pattern in one shot; the immediate mirror covers the 1–2 s before the lock.
- **B — keep early lock + grow, but phase-preserving.** Keep 0.9.13 growth but stop it from resetting the phase.

**Recommend A** (simpler, no re-lock churn, pairs cleanly with R1).

### 1.5 Acceptance criteria (all must pass)

- Play a riff in follow mode → it locks within ~1–2 s; the bass plays the riff's own notes and **does not change pitch** when the guitarist changes chord/root during the hold.
- The lock lasts `lockBars` bars; returning to the riff does **not** extend it.
- At the boundary, a fill/crash transitions to a new groove; if the guitarist is playing a different riff, the listener re-locks on it.
- Play mode (structure) bass behaviour is unchanged (root-following fallback still works there when not locked).
- Subtle fills/ghost notes appear at most "a little"; no perceptible riff change mid-hold.

---

## 2. Phase 2 — Structure editor + reactive fills/transitions

*(Already scoped in the previous discussion; `StructureSequencer` already accepts arbitrary forms via `loadForm()` — only UI + persistence + wiring are missing.)*

### 2.1 Requirements

- **Editable song form.** Replace/augment the static `songForm` combo with a section list editor:
  - Each row: section-type dropdown (INTRO / VERSE / CHORUS / BREAKDOWN / SOLO / OUTRO) + bar count (1–64).
  - Add / remove / move-up / move-down.
  - Keep the existing 3 presets as defaults.
- **Persistence.** Serialize the custom form to a string APVTS parameter (e.g. `"VERSE:8,CHORUS:8,BREAKDOWN:4,SOLO:8,OUTRO:4"`), parsed on change in `processBlock` (like the existing `lastSongFormIdx` polling) and loaded via `structureSequencer.loadForm()`.
- **Reactive fills/transitions in structure mode.** At section boundaries, make fill selection energy/section/genre-aware (upgrade `chooseTransitionFillKind()`), keep the crash-on-loud-re-entry, and keep "scripted sections + reactive fills" (the user's chosen behaviour — do NOT add intensity-steered section skipping yet).

### 2.2 Relevant code

- `src/analysis/StructureSequencer.h` — `SongSection`, `SongForm`, `loadForm()`, `getPresets()`.
- `src/inference/pattern_rules.h` — `sectionPatternPoolForGenre()` (per-section pattern pools), `selectFillPattern()`.
- `src/AccompanimentEditor.{h,cpp}` — `songFormCombo`, `loopToggle`, `resized()` rows; add the section list here.
- `src/AccompanimentProcessor.cpp` — `createParameterLayout()` (add the string param), the `lastSongFormIdx` polling block (~line 437).

### 2.3 Acceptance criteria

- User can build, reorder, and save a custom song form; it persists across plugin reloads.
- Section boundaries fire appropriate (energy-aware) fills/crashes; the arrangement itself stays predictable.

---

## 3. Phase 3 — Dataset improvements (Workstream C)

*(This is the plan's `C1–C6`; do it only after P0 + Phase 2 land, so selection improvements sit on a stable musical foundation.)*

Priority order:

1. **C5 — Real-audio captures + human labels (most important ML correction).** `FeatureCapture` already records real feature rows; the labels currently come from the plugin's own rules (circular). Collect 30–60 min of annotated playing (verse riff / palm-mute chug / heavy / etc.) and train a small classifier on **human-labeled** audio features. **Prereq:** fix the ONNX normalization mismatch (centroid std clamped to `1e-8`, per the audit §8.1).
2. **C1 — E-GMD groove statistics.** Learn velocity hierarchy + microtiming distributions and bake them into `src/midi/GrooveTemplate.h` (replacing the hand-written `rock()/metal()/punk()` templates). Directly improves drum feel.
3. **C2 — Lakh MIDI genre subsets.** Replace blind channel-10 filtering with rock-weighted subsets (MSD tags) for the pattern selector, using content-derived tempo (header BPM is unreliable).
4. **C4 — Slakh2100 / MoisesDB** only if committing to *generative* accompaniment (the A4.2 end-state). Skip otherwise.
5. **C3 — DadaGP** for articulation/riff-grammar (palm-mute vs open-chord vs single-note) — feeds a "playing style" classifier.

See `docs/MUSICALITY_ROCK_PIVOT_PLAN.md` §5 and §6 for details, licensing caveats, and sequencing rationale.

---

## 4. Regression test plan (catch the recurring problems)

### 4.1 The permanent test harness — "golden signal" tests

The recurring failures were all *real-audio* failures invisible to synthetic tests. Add a small, self-contained golden-signal test:

- Pre-extract 3–4 representative 8–10 s segments from `data/raw/` (e.g. one palm-mute chug, one open-chord passage, one thrash) into `tests/fixtures/` as float32/16-bit files (document how they were produced).
- Add a minimal PCM reader to the test target (the files are 24-bit mono 44.1 kHz today — either read 24-bit or convert to 16-bit/float when extracting).
- In the test, reproduce the plugin's exact analysis (the 0.1 s RMS window — see `RmsWindow` in `tests/test_phrase_learner.cpp` — plus the real `PitchEstimator`) and feed `PhraseLearner`, then assert **lock time** and **note density** thresholds.

### 4.2 Regression matrix (each maps to a bug we actually hit)

| # | Bug / scenario | Permanent test |
|---|---|---|
| 1 | Bass root a major third off (pitch-anchor vs E2=40) | Processor test: C2→bass 36, E2→40, G2→43 (and +12→48). Already have "bass octave +12"; add E2/G2 cases. |
| 2 | Attack detector too strict (1.2× rise) | Realistic palm-mute chug through the RMS window must lock (exists in `test_phrase_learner.cpp`). |
| 3 | Pitch-confidence gate starving attacks | Same realistic chug **with `conf = 0`** must still lock (exists — keep it, it's the key one). |
| 4 | Sparse learned playback | Note-density assertion: during a locked chug, bass triggers ≥ ~6/s (not 1–2/s). |
| 5 | Mode-specific lock bug (mirror suppressed in follow mode) | Run the riff through the processor in **both** `playOn=false` and `playOn=true`; assert the mirror fires in both. |
| 6 | Lock re-engagement freeze (stale learner re-freezing) | After hold expiry with no riff activity, the drain must be able to change the pattern (existing `[lock]` integration test). |
| 7 | **New — R1/R2:** bass must NOT change note while locked | Lock on a riff at pitch X, then play a *different* chord/root; assert the locked bass notes stay at X (this is the P0 acceptance test). |
| 8 | **New — R2:** fixed hold, no extension | Lock, keep returning to the riff; assert the lock releases at `lockBars` bars anyway. |
| 9 | **New — R4:** smooth release | Assert a fill/crash fires at lock expiry (observable via MIDI ch10 events around the boundary). |
| 10 | Plugin won't load (quarantine) | Not a unit test — a workflow step (see §5.2). Consider a tiny `dlopen` smoke test in the build script. |

### 4.3 Test-file registration

`test_stable_pitch_tracker.cpp`, `test_pitch_estimator.cpp`, and `test_phrase_learner.cpp` are registered in `CMakeLists.txt` (unit target). New golden-signal tests go in the **unit** target (they don't need the full plugin). Processor-level tests go in the **integration** target (`tests/test_processor_pipeline.cpp`). **Always register new test files in `CMakeLists.txt`** — unregistered tests were the root cause of "all green but broken" in bug #1.

---

## 5. Handoff / workflow notes

### 5.1 Build, version, install

- **Version bump rule (mandatory):** bump `project(MetalAccompaniment VERSION X.Y.Z)` in `CMakeLists.txt` line 4 before every build; rebuild so the string is baked in. Current: 0.9.13 → next is 0.9.14.
- Build the two dirs the user actually runs:
  - `cmake --build build --target MetalAccompaniment_VST3 MetalAccompaniment_AU -j8`
  - `cmake --build build-onnx --target MetalAccompaniment_VST3 MetalAccompaniment_AU -j8`
  - Unit/integration: `cmake --build build --target MetalAccompanimentTests MetalAccompanimentIntegrationTests -j8` then run the binaries in `build/`.
- **Install from `build-onnx/`** (matches the user's ONNX runtime at `/opt/homebrew`).

### 5.2 The quarantine gotcha (load failures)

The project lives in an iCloud-synced folder; freshly built bundles carry `com.apple.quarantine`, which makes macOS refuse to load them ("library load disallowed by system policy" / REAPER "could not be loaded"). **Every install must end with:**

```bash
cp -R "build-onnx/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -R "build-onnx/MetalAccompaniment_artefacts/Release/AU/Metal Accompaniment.component" ~/Library/Audio/Plug-Ins/Components/
xattr -cr ~/Library/Audio/Plug-Ins/VST3/"Metal Accompaniment.vst3"
xattr -cr ~/Library/Audio/Plug-Ins/Components/"Metal Accompaniment.component"
```

Verify with a `dlopen` smoke test (a tiny `dlopen(argv[1], RTLD_NOW)` program) → expect "LOAD OK". After install, the user must **rescan in REAPER** (Options → Preferences → Plug-ins → VST → Clear cache and rescan) if it doesn't appear.

### 5.3 Analysis tooling (recreate with one-off scratch programs)

The real-audio verification used throwaway C++ scratch programs (`/tmp/analyze_*.cpp`): they read a WAV/float file, run the 0.1 s RMS window + `PitchEstimator` + `PhraseLearner`, and print lock time / note rate / confidence distribution. **Before trusting any detector/root change, run this against `data/raw/` recordings and against `tests/fixtures/`.** The golden-signal test in §4.1 is the permanent version of this.

### 5.4 Definitions / gotchas for the codebase

- `AudioParameterInt`/`AudioParameterChoice` report their **raw integer value** (not normalized) through `getRawParameterValue()` — do not multiply by range when reading discrete params (see `lockBars` read at `AccompanimentProcessor.cpp` ~line 615).
- `displayPatternIndex` is the *latest computed* pattern, not necessarily committed; the committed pattern is `getLatestPatternIndex()` (added 0.9.9) — use the right one in tests.
- The real recordings are **24-bit mono 44.1 kHz**; the plugin's analyser RMS window is `0.1 * sampleRate` samples, scaled ×4 and clamped to [0,1].

---

## 6. Suggested execution order (for the next agent)

1. **P0** (lock rework) — smallest, highest value, unblocks the user's core complaint. Write the R1/R2/R4 tests *first*, then implement.
2. **Golden-signal test harness** (§4.1) — before or alongside P0, so every later change is verified against real audio.
3. **Phase 2** (structure editor + reactive fills).
4. **Phase 3** (datasets, in the C5 → C1 → C2 → C4/C3 order).
