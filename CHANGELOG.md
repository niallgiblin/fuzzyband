# Changelog

All notable changes to this project are documented here. For architecture and threading, see [`ARCHITECTURE.md`](ARCHITECTURE.md). Milestone/phase status: [`.gsd/STATE.md`](.gsd/STATE.md), [`.gsd/ROADMAP.md`](.gsd/ROADMAP.md).

## [0.9.54] — Locked riff persists through silence

- **Fix (stress test B2): the locked riff no longer drops on a breath.** The
  phrase learner's silence reset (a ~2 s quiet window) used to wipe the frozen
  riff mid-lock, so the bass fell back to live-mirroring the guitarist's solo
  instead of looping the recorded riff. The reset is now suppressed while the
  groove lock is holding the riff (`holdActive_`), so the recorded riff keeps
  looping through pauses until the lock releases. Follow/listen mode still
  resets on silence as before.

## [0.9.53] — ASCII Style placeholder on first paint

- **Style readout** shows `Style: -` from the first editor frame (before the
  timer, and while style is still unclassified). The old Unicode dash rendered
  as `â€`.

## [0.9.52] — Drop duplicate Stop; ASCII section status

- **Stop button removed.** It did the same thing as Forget (wipe the riff, go idle)
  and did not pause or resume. Forget is the one control that ends a take.
- **Section status separators** use ASCII hyphens so the UI no longer shows `Â·`.

## [0.9.50] — Deterministic record-riff loop; unified section countdown; Stop

The record-riff mini-structure is now a *scripted* loop, like Play mode, and the
UI shows how much of a section is left in every armed phase.

- **The record-riff loop is fully deterministic (A-B-A-C-A) and never "listens".**
  Once you record riff A, the engine loops `lockBars` of A → `transitionBars` of a
  contrast B → A → a different contrast C → A → … forever on the SAME recorded
  riff. Replaying the riff mid-transition no longer cuts that contrast short, and
  the engine no longer re-listens and re-locks onto a NEW riff while the loop is
  running. The learned riff is held through each transition (the bass still moves
  with the drums to the contrast section's harmony in Play-mode style), so the
  loop is predictable. To change A, press **Stop** (or Forget) then Record riff
  again.
- **Stop control.** A new Stop button ends the record-riff loop and returns the
  engine to idle/silent (same outcome as Forget). It is enabled whenever a riff
  loop is armed.
- **Unified section countdown + progress bar** across Play, riff lock (A), and
  transition (B/C): the status line reads e.g. `Section: VERSE · bar 3/8 ·
  5 left`, and a bar-segment progress bar shows how far through the section you
  are, so a change is easy to anticipate. The countdown is driven from the audio
  thread via new `getSectionPhase/Bar/BarsTotal/BarsRemaining/Progress` accessors.
- **Deterministic bass during the transition.** While the mini-structure plays a
  contrast section, the bass is a scripted beat-grid/section-harmony line (like
  Play mode) instead of a live mirror of the guitarist, so the loop does not
  "respond" to new playing.

## [0.9.48] — Record-mode A-B-A-C-A; Play stops at the end of the form

Two behaviour bugs in the armed modes:

- **Post-lock transitions return to the riff after every contrast.** `TRANSITION SECTIONS = 2` now means A → B → A → C → A (each contrast holds, then the locked riff re-engages before the next contrast). Previously the engine chained B straight into C (A-B-C-A). Playing the riff during B or C still cuts that contrast short and re-locks. Forget / Record / Play reset the contrast cycle.
- **Play mode plays the Sections list once and stops.** After OUTRO the engine returns to idle (silent) instead of wrapping back to INTRO. Pressing Play again restarts from the first section. The hidden loop parameter now defaults off.

## [0.9.45] — Bass follows the transition; stays in key in Play mode

The riff-lock bass was stuck on the recorded riff: after a Record-riff lock expired
into the post-lock contrast section, the drums rotated to the new groove but the bass
kept looping the old riff over it. And in Play mode the learned riff was played
note-for-note, which could pull the bass out of the song's harmony.

- **Transition guitar freedom.** `PhraseLearner::releaseForTransition()` drops the
  groove-lock hold and stops the autonomous riff loop when the post-lock transition
  engages, so the bass leaves the old riff and plays the new section's harmony (or
  live-mirrors your new lines). The learned pattern is retained so the riff is still
  recognised and the transition is cut short / the riff re-locks when you genuinely
  return to it. `setHoldActive` now only holds the riff during a real groove lock.
- **In-key bass everywhere else.** The riff is now played note-for-note only while the
  groove is genuinely locked (drums frozen on the riff). During a post-lock transition
  and in Play mode the bass snaps to the current section's harmony, keeping it musical
  and anchored to your root instead of following a stale riff out of key.

The bass root is still driven by your live pitch; the riff's rhythm is mirrored while
the notes stay inside the section's harmony.

## [0.9.31] — Scope now anchored to the transport (downbeat on 1)

The bar-aligned scope was showing the downbeat offset from beat 1 because its
playhead/bar grid used the plugin's own sample counter (`hostSampleTime`) while the
drums quantize to the DAW transport position. The playhead fraction is now computed
from the **resolved host clock** (`patternPlayer::previewResolvedHostSample`), so the
waveform's downbeat aligns with the audible beat 1 even when the transport isn't at
sample 0 (or loops/seeks).

## [0.9.30] — Demo polish: bar-aligned scope, cleaner UI, explicit arm, drum variety

Pre-demo pass to make the plugin feel intentional when you load it, plus more drum
variety. The engine is now **idle until you arm it** (Play or Record riff) instead of
always listening.

- **Scope is bar-aligned and shows the beat grid.** The waveform now puts the downbeat
  at the left edge and fills to the playhead, with notches labelled 1-2-3-4 at each
  beat (the old scroll was not anchored to the DAW clock). The ring was enlarged to
  hold a full bar (`kScopeSize` 2048 → 16384, 8× decimation) and a new
  `getScopeSamplesPerBar()` drives the editor's bar-window render.
- **UI cleanup.** Removed the Bass-octave dropdown, the Song-form preset dropdown, the
  Loop checkbox, and the RMS / Centroid / HF Flux / Noise-floor readouts. The editable
  section list is now titled **Sections** and defaults to
  INTRO → VERSE → CHORUS → VERSE → CHORUS → OUTRO. Fixed the "Groove: listening â€¦"
  mojibake by replacing non-ASCII em-dashes/ellipses/middle-dots in status text with
  ASCII equivalents (the plugin font can't render those glyphs).
- **Explicit arm (item: "only starts listening at Record riff / Play").** The plugin is
  silent and not learning until you press Play (plays the Sections song form) or Record
  riff (captures and locks). Auto-lock-by-listening in idle is removed; the status line
  shows "Groove: idle - press Play or Record riff". A recorded riff still plays its
  `lockBars` then the transition (B) for `transitionBars` × `transitionSections` and
  returns to the riff — a sectionizer for practicing riff changes.
- **Drum variety (item: "a diverse pattern the ML can alter slightly").**
  `MetalGrooveInference::selectPatternFromMel` now takes a bar-seed and draws a
  weighted pick from the **top-3 nearest grooves** (softmax temperature) instead of the
  fixed argmax. The best-fit groove stays dominant but a neighbouring groove can win,
  so the drums vary bar-to-bar while staying stylistically consistent. The style head
  (`classifyStyle`) continues to steer the groove family.

Tests updated to the new idle/arm contract (existing lock tests record a riff to arm
instead of relying on auto-lock-by-listening; a new test asserts idle does **not**
auto-lock). Configuration notes unchanged except the removed controls.


## [0.9.29] — Post-lock transition now truly plays the B section, then returns to the riff (A)

Fixes the A5.2 post-lock transition: after a recorded riff lock expires, the plugin now
plays a real contrast (B) section and then **firmly returns to the locked riff (A)**
instead of only slipping a fill and dropping back to reactive follow.

- **Transition section now engages.** The stale "riff fresh" check was re-engaging the
  groove lock on the very first block after expiry whenever the guitarist had been
  playing the riff right up to the lock end, cancelling the transition before it played
  a single bar. The re-engage path is now suppressed while a post-lock transition is
  active (`postLockPhase != TransitionHold`), and the "riff re-appears" cut is an **edge
  check** (`lastRiffMatchSample > transitionStartSample`) — a match from *before* the
  transition no longer cancels it; the B section plays until the riff genuinely comes
  back.
- **Returns to the riff, not follow.** When the transition sequence completes
  (after `transitionBars` × `transitionSections`), the engine re-engages the groove lock
  (A → B → A) instead of releasing to follow/listen. The learned riff is kept held for
  the whole transition (`setHoldActive` stays true) so it never drifts-unlocks and always
  re-locks cleanly.
- **Riff re-appears mid-B → cut short back to the riff.** If the guitarist re-plays the
  recorded riff while the B section is playing, the transition is cut immediately and the
  riff re-locks, keeping them anchored in time.

Tests updated/added in `tests/test_processor_pipeline.cpp` for the A → B → A cycle and the
riff-reappearance cut. Configuration: `transitionBars` (default 8) and `transitionSections`
(default 2).



Implements the four "feel more alive" items from the variety audit of the rock pivot
(see the `MUSICALITY_ROCK_PIVOT_PLAN.md` / `DATA_STRATEGY.md` A4.1/A4.3 workstreams),
plus the riff-lock progress display.

- **Style steering (A4.1 wiring).** The perception head (`classifyStyle` — palm-mute /
  open-chord / single-note / sustain / silence) was computed and displayed but never
  influenced selection. It now steers follow-mode grooves once a style is stable for
  3 consecutive inference windows (~150 ms): chugging → half-time/breakdown family,
  open chords → chorus/breakdown, single-note runs → fast/thrash, sustain → sparse.
  The style pool is filtered to the current structure state (SOFT/LOUD) so a style
  can never force a structurally-wrong pattern; silence and unknown styles leave the
  model/rule selection untouched. The existing 2-bar commit hold still gates actual
  pattern changes.
- **Phrased pool rotation (A4.3).** Play mode no longer cycles one pattern *per bar*
  (`bar % count`). Each groove is now held for a musical phrase — 2 bars for
  VERSE/CHORUS/SOLO, 4 for BREAKDOWN/INTRO/OUTRO — and the rotation is **seeded per
  section instance** (global bar count at section entry, re-seeded on section change,
  play start, and form-loop wrap), so verse 1 ≠ verse 2 and every Play session
  re-variates. The previously-played groove is never repeated immediately. Applies to
  post-lock transition pools too.
  *Implementation note:* the rotation is computed once per **phrase slot** (not per
  block or per bar) — computing it per block made `lastPlayedPoolPattern` flip-flop
  within a bar, so the bar-quantized change reverted the engine to per-bar cycling.
- **Fill variety.** `selectFillPattern` now sizes the last-bar fill to the section-end
  energy (loud → big/medium, quiet → short) and varies within the tier by the section
  seed. Fill Medium (18) — previously unreachable — is now used.
- **Riff-lock progress in the UI.** While a riff lock is held (recorded take or live
  grid listen), the Groove status label now shows how many bars are done and how many
  remain before the transition fires — "Groove: LOCKED — bar X/Y · N left before
  transition" — via new `getLockBarCurrent()` / `getLockBarsRemaining()` /
  `getLockBarsTotal()` published from the audio thread.
- **Variety rationale:** Lakh priors are deliberately NOT used as runtime weights —
  they are heavily peaked (0.0/1.0), which would collapse pools to one pattern; priors
  remain build-time pool ordering (`orderPoolByPriors`).
- Tests: new `pickPoolPattern` / `diversifyPatternForStyle` / `selectFillPattern` /
  `barsPerGrooveForSection` unit tests, a processor integration test proving play
  mode phrases grooves into 2-bar holds, re-seeds per section instance, and fills
  the last bar, and riff-lock progress assertions in the record + grid-listen tests.

- v0.9.26 (versioned build per workflow).

## Data Improvement P3 — honest data foundation (training only, no plugin build)

Implements `docs/DATA_STRATEGY.md` Phase 3 (§5). Training/tooling only — no C++
or plugin binary changes, so no version bump.

- **§5.1 Honest validation.** New `training/scripts/dataset_split.py` assigns a
  **grouped train/val/test split by source recording** — augmented variants of a
  take can no longer straddle train/val (the leak that inflated the ~0.96 F1).
  Both mel builders (`build_mel_groove_dataset.py`, `build_mel_dataset.py`) now
  record each window's `source` and write the frozen `split` into meta CSV; every
  class with ≥2 source recordings is guaranteed a val example. Both trainers
  (`train_groove_model.py`, `train_classifier.py`) read the frozen split, print an
  honest confusion matrix + macro-F1, and **fail the quality gate on any dead
  (zero-recall) class** on the held-out set.
- **§5.2 Two-layer taxonomy.** `training/perception_taxonomy.py` is the single
  source of truth for the self-labeled perception classes (palm_mute / open_chord
  / single_note / sustain / silence + soft/loud). `docs/LABEL_TAXONOMY.md`
  documents perception-vs-arrangement and the `style + intensity + section →
  pattern-pool` composition (via `src/inference/pattern_rules.h`).
- **§5.3 Capture→training bridge.** Removed the retired-legacy `build_dataset`
  import from `evaluate_feature_capture.py` (it had been un-runnable since P1);
  the rule derivation is inlined. This unbreaks the capture-eval test module.
- **§5.4 Annotation + slicing.** New `training/slice_annotations.py` slices a
  labeled take (`start_seconds,end_seconds,label` CSV) into per-class clips under
  `data/raw/<label>/`, ready for the mel builder.
- **§5.5 Provenance.** New `data/MANIFEST.md` documents every dataset artifact,
  its source/license, and the exact regenerate command; codifies the
  raw-committed / tensors-gitignored / fixtures-committed convention.
- **§5.6 Phase 4 interfaces.** Documented the existing plug points
  (`GrooveTemplate.h` for C1, `pattern_rules.h` pools for C2, perception taxonomy
  for C3) in `docs/LABEL_TAXONOMY.md`.
- **Tests.** Added `test_dataset_split.py`, `test_perception_taxonomy.py`,
  `test_slice_annotations.py`; full training suite: 52 passed, 40 skipped
  (legacy/ONNX-gated), 0 failures. Smoke-tested the whole pipeline end-to-end on
  the existing ~60 WAVs (grouped split verified leak-free; dead-class gate fires).

## [0.9.17] — Make the custom song-form editor visible and labelled

- **Fix (user session): the modular section-list editor was there but its combo
  boxes drew no text**, so the section-type dropdowns looked like empty boxes and
  the editor read as "the old 3-choice combo only". The custom look-and-feel's
  `drawComboBox` now draws the selected text (it previously replaced the base
  implementation without drawing the label). This also fixes the value text for
  the Genre / Bass octave / Song form combos.
- Added a "Song sections (custom)" heading above the section list so the modular
  editor (add / remove / move-up / move-down / per-section type + bars) is
  clearly separate from the preset combo. The 3 presets remain as starting points.
- v0.9.17 (versioned build per workflow).

## [0.9.16] — Slimmer editor UI (make room for the section list)

- **UI cleanup (user session):** removed the tempo knob (rotary BPM), the
  "Inference: …" label, and the "MIDI outputs on drum/bass …" help caption from
  the editor so the Phase 2 section-list editor has room. Tempo still comes from
  the DAW transport (the `bpm` parameter remains as the standalone fallback);
  the inference backend name/error count and the routing note are still available
  via `getActiveInferenceName()`/`getOnnxErrorCount()`, just no longer shown.
- v0.9.16 (versioned build per workflow).

## [0.9.15] — Fix crash on opening the plugin editor

- **Fix (user session): REAPER crashed when pressing the track's FX button.** The
  Phase 2 section-list editor is a `std::unique_ptr` created part-way through the
  `AccompanimentEditor` constructor, but `setSize()` at the top of the
  constructor fires `resized()` immediately — before the member existed — so
  `sectionListEditor->getHeightHint()` dereferenced null. `resized()` now guards
  the pointer. Added an `[editor]` construction smoke test so this class of
  regression is caught by the integration suite.
- v0.9.15 (versioned build per workflow). Suite: 167 unit + 30 integration/e2e green.

## [0.9.14] — Groove lock rework, golden-signal tests, editable song form

### P0 — Groove-lock rework (the "changes too quickly" fix)

- **R1 — the bass note is frozen while locked.** Removed `PhraseLearner::retuneToLiveRoot()` and the live-root retune call in the processor: the learned riff's recorded pitches are now authoritative for the whole hold, so changing chord/root mid-hold no longer yanks the bass. The live-mirror is dropped once `Locked` (the learned pattern drives the bass alone); `StablePitchTracker` still feeds the fallback root when *not* locked.
- **R2 — fixed hold.** `lockBars` is now a fixed window; returning to the riff no longer extends it (`justMatchedRiff()` extension removed).
- **R4 — smooth release.** On hold expiry the audio thread arms a crash + a `Release` transition fill at the bar boundary and hands pattern selection back to the listener (which may re-lock a *new* riff).
- **Full-riff capture (option A).** Lock now waits for ≥8 attacks and scans pattern lengths *descending* (longest first), and `lockPattern()` replicates the riff across the bar-aligned loop — so the learned playback is dense note-for-note instead of a 2-note slice. Removed the phase-jumping pattern-growth re-lock.

### Golden-signal test harness

- New `tests/fixtures/` (4×10 s real guitar excerpts: palm-mute chug, thrash chug, open-chord, single-note — documented in `tests/fixtures/README.md`) and `tests/test_golden_signal.cpp`, which runs the exact 0.1 s RMS window + `PitchEstimator` + `PhraseLearner` on real audio and asserts lock time + note density. Registered in CMake (the "unregistered tests" trap from bug #1).

### Phase 2 — editable/persistent song form + reactive fills

- `StructureSequencer::serializeForm()` / `parseFormString()` (form ↔ `"VERSE:8,CHORUS:8,…"`).
- The song form persists as a session property, handed to the audio thread lock-free via `setCustomSongForm()` (message thread) + a version counter (audio thread). The old preset-index polling was replaced.
- New section-list editor in the UI (add / remove / move / per-section type + bar count); the preset combo seeds it.
- `chooseTransitionFillKind()` is now energy-aware (impact vs build-up vs subtle).

### Phase 3 — ONNX normalization fix (C5 prereq, training-side)

- The spectral-centroid std was clamped to `1e-8` (`merge_datasets.py`, `pattern/structure/bass_model.py`), making live inference saturate (audit §8.1/F3). Added a per-feature std floor (centroid ≈ 1 kHz, others 1e-3) and bumped the backstop epsilon to `1e-3`. **Requires re-running `merge_datasets.py` + re-exporting the ONNX models** to affect the shipped model.

- v0.9.14 (versioned build per workflow). Suite: 163 unit + 29 integration/e2e green.

## [0.9.13] — Follow-mode bass mirrors while the groove is locked

- **Fix (user session): bass mirrored in Play mode but not in follow mode.** In
  follow mode the groove lock engages on riff repeat, and the previous fix
  blanket-suppressed the live mirror while held — so once the lock engaged, the
  bass dropped back to looping the skeletal learned pattern instead of
  mirroring your playing. Play mode never engages the lock, which is why it
  sounded fine there.
- **The mirror is now suppressed only for solo licks.** While the groove lock
  holds the riff, riff-matching attacks (the guitarist playing the riff) are
  still mirrored densely; non-matching attacks (a solo over the groove) are not
  mirrored — the learned loop sustains. Outside the hold, every attack is
  mirrored as before.
- **Pattern growth:** the first lock still captures a skeletal slice (len=2),
  but as more matching attacks accumulate the pattern is re-captured with the
  longest matching length (up to 16 notes) — the groove-lock sustain loops the
  full riff instead of a 2-note blip.
- Verified on synthetic follow-mode sessions: held + playing riff → 12 notes/s
  (mirror); held + solo → learned riff loops at the grown density, solo not
  mirrored; released + solo → follows again. Suite: 159 unit + 25 integration
  green.
- v0.9.13 (versioned build per workflow).

## [0.9.12] — Bass stays dense while you play (live mirror through the lock)

- **Fix (user session): the bass still played sparse staccato notes while
  riffing.** Diagnosis from your real recordings: the immediate mirror only ran
  *before* the pattern lock; once locked, the learned-pattern playback took over
  — and the lock fires on the first repeat, so the pattern is a skeletal 2-note
  slice that plays a couple of short notes per 4-beat loop. That was the sparse
  staccato.
- **The live riff mirror now runs in the Locked state too:** while you are
  playing, the bass follows every detected attack (measured on your recorded
  takes: ~1.7 notes/s → **10–17 notes/s during active playing**, matching the
  chug density). The learned loop only sustains when you stop, and the groove
  lock still holds the riff through a solo (the mirror is suppressed only while
  the groove lock is holding).
- **Legato note length:** bass notes are ~0.9 beat instead of 0.4 — the bass
  sustains through dense chugs instead of staccato blips.
- v0.9.12 (versioned build per workflow). Also: installs now strip quarantine
  xattrs (`xattr -cr`) — the iCloud-synced build folder was flagging fresh
  builds so macOS refused to load them ("library load disallowed by system
  policy").

## [0.9.11] — Bass mirrors riffs as you play them (immediate mirror)

- **Fix (user session): the bass still didn't mirror riffs — grounded in your
  real recordings.** Ran the actual pipeline (analyser RMS + YIN pitch + learner)
  over `data/raw/` takes: the RMS detector fires ~9×/s on real chugging, but the
  **pitch-confidence gate starved the learner** — YIN's confidence on distorted
  palm-mute guitar is bimodal (≈0 at most attack moments), so only a handful of
  attacks were recorded, the mirror locked after 2–6 s with a skeletal 2-note
  pattern, and the sparse beat-1 fallback was all you heard.
- **Attacks are now gated on the RMS transient only — never on pitch
  confidence.** The riff mirror is a *rhythm* mirror; the note value comes from
  the held/confident pitch and is corrected later by the live-root retune. Lock
  time on your real recordings dropped ~2.7×.
- **New: immediate riff mirror.** While you're actively riffing (≥2 attacks in
  the last 2 bars), the bass plays **every detected attack as it happens** with
  the held pitch — no 2–6 s learning wait, no beat-1 drone. The beat-grid
  fallback returns only when riffing stops (a lone accent doesn't count as
  riffing). Once the pattern locks, the learned riff (with live-root retune)
  takes over, and the groove lock still freezes drums+bass on riff repeat.
- **Tests:** realistic-chug regression now feeds **zero pitch confidence** (the
  distorted-guitar case) and must lock; new test asserts immediate-mirror
  triggers fire before the lock. Suite: 159 unit + 25 integration cases green.
- v0.9.11 (versioned build per workflow).

## [0.9.10] — Bass actually mirrors your riffs again (attack-detector fix)

- **Fix (user session): the bass stopped mirroring riffs.** The 0.9.8 attack
  detector (`rms > prev × 1.2`) never fired on real playing: the analyser's
  100 ms RMS window smooths palm-muted chugs into a few-% block-to-block swings,
  so the learner recorded ~1 attack per 6 s, never locked, and the bass sat on
  the sparse beat-1 fallback. New detector: an attack is a **sharp rise that
  follows a recent decay** (a real note pulse), which fires on real chugging
  (verified: a realistic 16th-note palm-mute chug locks in ~0.2 s) while still
  rejecting constant tones, slow swells, and the analyser warm-up ramp.
- **Fix: fallback bass density.** INTRO/OUTRO/BREAKDOWN fallback was a whole
  note on beat 1 (read as "the bass isn't opening up"); now a minimum half-note
  pulse everywhere.
- **Tests:** new regression — "locks on a realistic palm-muted 16th chug"
  (feeds the analyser-style windowed RMS, the profile the old detector failed
  on). Suite: 158 unit + 25 integration cases green.
- v0.9.10 (versioned build per workflow).

## [0.9.9] — Generative groove lock (riff → drums+bass lock in)

- **New: the groove auto-locks onto your riff.** In generative mode (Play off),
  when the PhraseLearner detects your riff repeating, the **drums freeze on the
  current pattern** and the **bass keeps looping the learned riff** while you
  expand/solo over it — then it listens again. Behavior (per product spec):
  - **Engage:** riff repeats once → lock (drums + bass hold the groove).
  - **Hold:** `lockBars` bars (default 16, UI slider 4–64) after the last moment
    you were playing the riff; **returning to the riff extends the hold**.
  - **Release:** silence, Play-on, or the hold elapsing without riff activity.
    After expiry the lock re-engages only if the riff is still being played
    (2-bar freshness grace) — a stale learner never re-freezes the listener.
  - **Bass while locked:** keeps retuning to your live root **only when you're
    demonstrably playing the riff** (a riff-grid attack just landed); soloing or
    held chords freeze the riff note-for-note, per the "freeze if unreliable"
    decision. Intervals/contour preserved.
  - **UI:** "Groove: LOCKED (riff)" status indicator (green) + "Lock (bars)"
    slider. The lock status is also available to hosts via `isGrooveLocked()`.
- **Fix: phrase learner missed attacks at loud/quiet transitions.** YIN's
  confidence collapses to ~0 when its analysis ring mixes loud + quiet samples —
  exactly when note attacks fire — so riff *starts* (after a rest or quiet
  interlude) were dropped and the bass never learned them. The learner now holds
  the last confidently-estimated pitch and accepts transition attacks with it.
  This also makes the new auto-lock engage reliably on riff starts.
- **Fix: "following the riff" is now robust.** The drift check compares each
  attack against **any** note-to-note interval of the learned pattern (cycled),
  instead of a playback-position-coupled expectation that failed on sub-bar
  uniform chugs (whose bar-aligned pattern under-samples the bar).
- **Tests:** PhraseLearner hold/follow unit tests (hold suppresses drift-unlock,
  following fires while the riff plays) and a processor pipeline test (lock
  engages on a chug → committed pattern frozen through a 4 s re-eval window →
  hold expires after `lockBars` bars → stale learner does not re-engage →
  rejection takes effect once listening resumes → silence stays released).
  Suite: 157 unit + 25 integration cases green.
- v0.9.9 (versioned build per workflow).

## [0.9.8] — Bass actually follows the guitarist's root

- **Fix (user session): the bass root was a major third off.** `StablePitchTracker`
  computes the semitone offset anchored at C (`kBassRootPc = 0`, the drop-C root),
  but the processor folded it onto **E2 = 40** (`bassRoot = 40 + offset`). Net:
  guitar C → bass E, guitar E → bass G♯, guitar G → bass B₁. This is why the bass
  "sounded off" while tracking. The processor now folds the offset onto **C2 = 36**
  (the v0.9.6 intent), so the bass plays the guitarist's pitch class.
- **Register fix — no more octave-down folding:** the tracker previously wrapped
  the offset to ±6, which pushed roots at pc ≥ 7 an octave down (G → B₁ = 35,
  below the audible range of many bass VSTs). It now returns the pitch class in
  [0, 11], so every root lands in the C2–B2 octave (C→36, E→40, G→43, B→47).
- **PhraseLearner loop is bar-aligned:** the learned riff used to loop at
  `totalBeats + 0.5`, so a 4-beat riff looped at 4.5 beats and the bass drifted
  against the drum bar every loop. The loop now rounds to a whole number of bars
  (never shorter than the riff), and its phase is aligned to the riff's own cycle
  — the first bass note lands on the guitarist's next phrase start.
- **Locked bass follows the live root (A1 acceptance):** while the riff mirror is
  locked, the pattern is re-rooted to the guitarist's current pitch class every
  block (1/8-beat stability window), so a root change is audible at the next bass
  note instead of waiting for a drift-triggered re-learn. Riff intervals/contour
  are preserved.
- **Fix: no more false riff locks.** The phrase learner's attack detector fired on
  any positive RMS drift while loud, so it locked onto the warm-up ramp of any
  sustained tone — hold a chord and the bass mirrored the transient and flickered
  between mirror and fallback. Attacks now require a sharp single-block rise
  (≥ +20% vs the previous block); a constant tone, a slow swell, or the analyser
  warm-up never qualifies.
- **Tests:** the pitch-following path was untested — `test_stable_pitch_tracker.cpp`
  and `test_pitch_estimator.cpp` were never registered in CMake, and the tracker
  test still asserted the old E-anchored contract. Updated the tracker tests to
  the C-anchored [0, 11] contract, registered both files, and added
  `tests/test_phrase_learner.cpp` (lock, bar alignment, phase alignment, live-root
  retune, no false lock, silence reset). The "bass octave +12" pipeline test now
  passes — it asserted the intended v0.9.6 mapping that the buggy code never
  produced. Suite: 156 unit + 24 integration cases green.
- v0.9.8 (versioned build per workflow).

## [0.9.7] — Bass octave control + choice-param fix

- **New UI control — "Bass octave" (−12 / 0 / +12):** shifts the bass MIDI an
  octave (applies to the harmonic/pattern bass *and* phrase-learned notes).
  Motivated by the user's bass VST: for drop-C the plugin correctly outputs
  MIDI 36 = C2, but VSTs using the middle-C=C3 convention display it as "C1"
  and some can't sound below ~E2 — the control fits the bass to the instrument's
  range. Regression test: C2 input with +12 → bass around C3 (48).
- **Choice-parameter read fix:** the APVTS adapter's raw value for a choice
  parameter is already the *index*; a stray normalized multiply collapsed
  Punk/Metal/Sludge genres to Metal and broke +12 (regression test: Sludge
  routes SOFT low-energy to the metal half-time, not the rock set).
- v0.9.7 (versioned build per workflow).

## [0.9.6] — Drop-C pitch tracking

- **Fix (user session):** the bass always played low E regardless of the guitar's
  root. The YIN pitch estimator's low end was capped at 75 Hz (`sr/75`), but
  drop-C's low string is C2 = 65.4 Hz — below the range — so no pitch was ever
  detected, `StablePitchTracker` returned INT_MIN, and the processor fell back to
  bassRoot = 40 (E2).
- **Fix:** extended the estimator band to ~55 Hz (`sr/55`, ring holds ~5 periods
  at 55 Hz) and lowered the accept gate to 50 Hz. Drop-C (and lower drop tunings)
  are now detected; the pitch-class anchoring in `StablePitchTracker` maps C to
  C2 (36) on the bass. New tests: 65.4 Hz → MIDI 36 and 55 Hz → MIDI 33.
- v0.9.6 (versioned build per workflow).

## [0.9.5] — Solid SILENT under hot-input noise

- **Fix (user session):** with guitar volume up and not playing, the noise floor
  was RMS ~0.04–0.05 — above the v0.9.4 silent-threshold cap (0.03) — so the
  state hovered at the boundary and the bass "sometimes stopped, sometimes kept
  going."
- **Silent-threshold cap raised 0.03 → 0.06:** the learned noise floor can now
  push the silent threshold above hot-input noise (up to ~0.05 RMS → threshold
  0.06). Quiet playing is still protected: the threshold only rises to the cap
  when the floor itself is high.
- **Phrase learner gated on SILENT:** the learner's internal silence gate
  (rms < 0.002–0.003) was far below the noise floor, so during "silence" it
  locked onto the noise and mirrored it endlessly — the learned-bass path runs
  regardless of the Silent pattern. The processor now resets the learner and
  suppresses its triggers whenever the structure state is SILENT.
- **Faster floor adaptation** (release 0.001 → learns a hotter noise floor in
  ~15 s of quiet), creep gated to the quiet band so loud playing can't raise the
  displayed floor.
- Regression test: 15 s of noise-floor-level input → state SILENT, zero bass in
  the final 3 s. v0.9.5 (versioned build per workflow).

## [0.9.4] — Solid SILENT (adaptive noise floor)

- **Fix:** SILENT was not solid — guitar hum/hiss (centroid ~3100 Hz, HF flux ~0.9
  in the user's session) kept RMS hovering around the fixed `kSilentRms = 0.012`,
  so the state flickered SILENT/SOFT and the accompaniment kept playing over the
  noise floor.
- **Adaptive noise floor:** `StructureTagger` now tracks the quietest RMS seen
  (snaps down instantly, creeps up slowly with a ~20 s time constant) and sets
  the silent threshold at `max(kSilentRms, min(floor × 1.5, 0.03))`. The gate
  learns your actual noise floor instead of assuming a fixed one; the 0.03 cap
  guarantees quiet playing is never misread as silence.
- **Holds to SILENT reduced** for responsiveness: SOFT→SILENT 2.0→1.0 s,
  LOUD→SILENT 3.0→1.0 s (phrase-breath gaps ≤ 0.75 s still keep the groove).
- New UI readout **Noise floor** shows the learned floor so you can verify it
  tracks your input. New regression tests for hum-solid SILENT and floor reset.
- v0.9.4 (versioned build per workflow).

## [0.9.3] — Bass stops on silence

- **Fix:** when the guitarist stops, the state drops to SILENT and the pattern
  switches to 0 (Silent). Drums correctly went quiet, but the bass kept droning
  the last root note: pattern 0 has no authored `bassEvents`, so the harmonic
  fallback engine played on — and hum-level input kept the plugin "active"
  (`rms > 0.001`) so `structureSilent` never engaged. The bass engine is now
  suppressed whenever the active pattern is 0 — the Silent pattern means silence
  for the bass too. Regression test: switching to pattern 0 leaves bars 2-4
  bass-free.
- v0.9.3 (versioned build per workflow).

## [0.9.2] — Frozen-transport fix (jam with the DAW stopped)

- **Root cause of "no drums + harsh constant bass" in the DAW:** when the transport
  is stopped, `getTimeInSamples()` returns a constant position. The transport-jump
  detector saw a "jump" on every block, wiped the pending pattern change each
  block (`activePatternIndex` stuck on 0 = Silent → no drums), and anchored the
  beat clock to a fixed phase so the bass re-fired the same note at block rate —
  the harsh constant drone.
- **Fix:** `PatternPlayer` detects a frozen host position (`lastHostSample`) and
  runs its own internal beat clock, so patterns play in time with the transport
  stopped. Real seeks/loops still register as jumps and drop deferred state.
  Regression test: a frozen host position produces one bar of Verse Groove
  (~9 hits), not 188 block-rate hits.
- **Build versioning:** this is v0.9.2 — every build from now on bumps the patch
  version so the loaded binary is always identifiable in the plugin UI.

## [0.9.1] — Bass stuck-note fix

- **Fix:** multi-pitch bass (authored intervals + harmonic fallback) could leave a
  note stuck on: the single deferred note-off slot emitted `noteOff` for the last
  note played instead of the note whose note-off was due → a harsh constant drone.
  Bass is monophonic, so one slot is correct as long as it carries the right note
  number: `emitBassNote()` closes the previous note on each new note-on and
  `bassNoteOffMidi` tracks the pending note. Regression test: multi-pitch bass
  across small blocks leaves zero notes open.
- This build is **v0.9.1** so the fixed binary is distinguishable from the first
  v0.9.0 build in the plugin UI (top-right version label).

## [0.9.0] — Musicality & Rock Pivot (Workstream A: P0+P1)

**Implements the code phases of `docs/MUSICALITY_ROCK_PIVOT_PLAN.md`.** Fixes the
musical ceiling — the plugin was only *selecting* canned metal loops over a root-note
metronome bass with white-noise jitter; now it renders grooves and basslines with
drummer-like hierarchy, structured microtiming, dynamic contrast and real harmony.

### A1 — Bass engine (dead code revived + harmonic fallback)
- `PatternPlayer` now reads `MidiPattern.bassEvents` — the 20+ authored bass lines
  that were written but never emitted (Finding 1) now play, transposed to the
  guitarist's tracked root (`liveRoot + (authoredNote − 36)`), folded into the
  playable register.
- New harmonic fallback engine (`emitHarmonicBass`) for patterns without authored
  bass: root/fourth/fifth/octave movement per section (chorus walks root→fifth→
  octave→fourth; verse holds root with an occasional fourth; breakdown roots),
  beat-1 accents, ±5 humanisation, ~2 ms behind the kick for pocket, 85% gate kept
  as a named parameter.
- Beat-aligned root-metronome bass replaced; `bassNotesPerBar` still drives the
  harmonic fallback density. (A multi-pitch note-off bookkeeping bug found in UAT
  was fixed in **0.9.1** — see that entry.)

### A2 — Groove & humanisation
- `src/midi/GrooveTemplate.h`: per-16th-grid velocity hierarchy (downbeat >
  backbeat > 8th hats > off-16th) and structured microtiming (backbeat ~4 ms late,
  kick ~1 ms early, ghosts early) replacing the uniform ±10 velocity / ±2 ms timing
  white noise. Jitter is now a bounded gaussian (±2.5σ) around the structured offset,
  seeded deterministically (`setRandomSeed`).
- Swing knob (`swing` APVTS param, 0–100%): delays off-8th events by up to 1/6 beat
  (straight → triplet feel), mapped to a UI slider.

### A3 — Dynamic contrast & articulation
- Per-section velocity multipliers from the genre preset (verse 0.92 / chorus 1.06
  at Rock): chorus backbeat is ≥15 louder than verse backbeat (plan §7 metric).
  Reactive mode maps LOUD playing → CHORUS so dynamics track the guitarist.
- Ghost notes: authored ≤62-velocity snares are rendered in the 30–55 ghost band and
  slightly early; verse/breakdown sections additionally inject off-16th ghost snares
  (density from the genre preset) at cells not occupied by authored snares.

### A4.1 — Rock-first pattern set
- 6 new patterns (indices 22–27): Rock Backbeat, Rock Half-Time, Rock Shuffle,
  Punk D-Beat, Rock Ballad, Rock 6/8 Feel — each with authored drum **and** bass
  events. `MidiPatternLibrary::kPatternCount` (28) is the single source of truth;
  `PatternRules::kPatternCount` is synced and unit-tested.

### B1/B2 — Genre presets & tempo unification
- New `genre` APVTS param (Rock default / Hard Rock / Punk / Metal / Sludge): selects
  groove template, velocity profile, section dynamics, ghost density, swing default.
  Metal/Sludge keep the original metal routing; rock genres prefer rock patterns in
  play-mode pools (`sectionPatternPoolForGenre`) and the reactive path
  (`diversifyPatternForGenre`).
- Every BPM clamp site now agrees on [40, 300] (`PatternPlayer` previously allowed
  320; `PatternRules::adjustedBpm`, the APVTS param and the slider already clamped
  to 300).

### Known limitations / deferred (P2–P4 of the plan)
- Groove template numbers are musically-baked defaults; C1 (E-GMD velocity/timing
  stats) can replace them data-driven without touching rendering code.
- `bass_model.onnx` is still not in the live path (unchanged); the plan defers its
  integration/retirement decision.
- Human UAT (blind A/B: humanisation on vs off) remains the final acceptance gate.

## [0.7.0] — Creative Companion (Playability Pivot)

**Milestone M001.** ONNX-first inference that doesn't jitter: stable tempo, stable ML-driven structure detection, and musically distinct grooves — without replacing ML with manual controls.

### S01: Tempo Stability
- Soft-lock EMA (α=0.03): BPM updates via exponential moving average after tempo lock instead of hard-freeze, letting genuine tempo changes propagate over ~3–5 seconds while rejecting single-outlier IOI jitter.
- `resetTempoLock()` no longer touches currentBpm — BPM preservation is the caller's responsibility via `setSeedBpm()`, keeping reset single-purpose (clear IOI memory only).
- Onset history cap at 64 entries (was unbounded) preventing stale-IOI pollution.
- Minimum onset interval clamp for valid BPM range (40–320 BPM).

### S02: Structure Stability
- StructureTagger confidence gating with hysteresis: minimum 2-second hold time per state, minimum energy threshold for verse/chorus transitions.
- Centroid-based soft-state transitions: prevents classification flip-flop during palm-mute/full-chord boundaries.
- SILENT state entered immediately on dropout, exited only after sustained energy above threshold.
- Stability verified: 20-second soft → 20-second loud transitions settle within ≤2 bars.

### S03: Groove Vocabulary
- MidiPatternLibrary expanded from 7 to 11 patterns with 4 new musically distinct metal drum grooves: Half-Time (index 7, open hi-hat backbeat), Blast Beat (8, ride bell + alternating kick/snare 16ths), Sparse Breakdown (9, kick+snare only, no hats/ride), Thrash (10, double-kick 16ths with steady ride).
- `diversifyPattern()` deterministic routing function inserted after `selectPattern()` but before the 2-bar drum hold guard: SOFT low-energy → half-time, LOUD low-energy → sparse, LOUD high-BPM → blast/thrash, using bar-phase for variety.
- SOFT compatibility matrix expanded to include index 7 (half-time); LOUD expanded to 8–10 (blast/sparse/thrash).
- E2E test: simulated multi-section jam produces ≥3 distinct groove names ("Verse mid", "Half-Time", "Chorus mid").

### S04: Jam Verification & Polish
- Long-duration stability test: 312 seconds continuous processing (240s music), 15 SOFT/LOUD cycles, zero crashes, all invariants held.
- Performance benchmark: mean 0.39ms/block, P99 0.43ms/block at 256-sample buffer (~7.3% audio thread budget).
- Full regression: 169 CTest targets including 160 unit + 4 E2E + 2 integration + 1 perf + 1 stability + 1 benchmark — 100% pass on both standard and ONNX builds.
- `MA_ENABLE_ONNX` is default ON; ONNX path is the primary verified pipeline.

### Known Limitations
- BPM control through synthetic test signals is unreliable with the current 2048-sample FFT onset detector — test-only BPM injection API suggested for future ML integration tests.
- ONNX model still trained on pre-expansion 7-pattern labels — diversifyPattern() bridges the gap deterministically until retrain.
- Human UAT (5+ minute Reaper jam with clean DI guitar) is the final acceptance gate — automated verification supports but does not replace it.

## [0.6.7] — v0.6.0 ML Correctness & Evaluation (completed)

**Current milestone:** v0.6.0 (Phases 32–36). Plugin version 0.5.6 in `CMakeLists.txt`. `MA_ENABLE_ONNX` default is **ON**.

### v0.6.0 — ML Correctness & Evaluation
- **Phase 32 — Training Label Correction:** Rule-oracle labels replace quantile bins in `build_dataset.py` and `build_lakh_dataset.py`; `merge_datasets.py` rejects invalid labels; deterministic class-6 oversampling; retrained `accompaniment_model.onnx` promoted (LABEL-01–03, DATA-06).
- **Phase 33 — Model Quality Gates:** Bass training gate (MSE vs rule baseline, per-step metrics, `validation.json`); structure dual gate (macro-F1 vs rule agreement, norm stats JSON); `StructureOnnxExport` bakes normalization into ONNX graph; contract tests for structure model (QGATE-01–04).
- **Phase 34 — Domain Gap & Feature Capture:** Runtime `feature_capture.v1` JSONL capture with debug toggle; offline annotation evaluator (rule vs ONNX accuracy, confusion matrices, disagreement); captured-vs-proxy gap analyzer with quantitative `FEATURE_PROXY.md` verdicts (DOMAIN-01–03).
- **Phase 35 — Inference Path Consistency:** `OnnxInference` packs adjusted BPM into tensor column 0; compatible exclusion modulo walks to valid pattern instead of blind `(last+1)%7`; 3× intensity dataset expansion + retrain; `structureBlend` and adjusted-BPM docs (INFC-01–03).
- **Phase 36 — ONNX Evaluation & Default Readiness** (in progress): Committed readiness fixture, centroid proxy fix + retrain, CI accuracy gates, ORT latency CTest, `docs/ONNX_READINESS.md`, `MA_ENABLE_ONNX` default flip criteria (ONNXEVAL-01–03).

### v0.5.0 — Rhythmic Coherence (partial)
- **Phase 27 — Documentation:** `plugin/README.md` Pre-FX placement story; root/docs entry points (RHY-DOC-01–02).
- **Phase 31 — Architecture Deepening:** `PlaybackGate`, `StablePitchTracker`, `TempoStabiliser` extracted to `src/analysis/`; `PatternRules` shared header in `src/inference/`; `AccompanimentProcessor` thinned (ARCH-01–04).
- Phases 28–30 (beat tracker, runtime coordination, ML retrain) parked for v0.7.0.

## [v0.4.0] — ML Playability & Simplification (2026-04-29)

**Milestone:** v0.4.0 (Phases 21–26). Better-trained ML on GMD+Lakh merged data; 3-class structure; `[1,32]` bass piano-roll; simplified UI; ML rejection for Next Pattern.

- **Phase 21 — C++ Type Foundation:** `StructureState` → 3-class enum; `FeatureVector` and `IInference` contract updates.
- **Phase 22 — ONNX Contract + Stubs:** Updated tensor shapes; forward-compatible stubs.
- **Phase 23 — C++ Inference Layer:** `OnnxInference` updated to new contract; `OnnxStructureInference` reads normalization from graph.
- **Phase 24 — UI Simplification:** Genre APVTS removed; editor simplified.
- **Phase 25 — Training Data Pipeline:** Lakh MIDI dataset integration; `build_lakh_dataset.py`; `merge_datasets.py` producing merged train/val tensors.
- **Phase 26 — Retrain + Validate:** Pattern and structure models retrained on merged data; promoted to `assets/`; PVAL-01 DAW verification.

## [v0.3.0] — Real ML Training Pipeline (2026-04-19)

- **Phase 17 — Data Pipeline:** `training/download_gmd.py` (TFDS-pinned GMD + SHA-256 manifest); `training/FEATURE_PROXY.md`; `training/build_dataset.py` (train/val `.pt`, histogram gate); `training/tfds_compat.py`; `training/data/` gitignored (DATA-04–06).
- **Phase 18 — Pattern Model:** `PatternNet` (5→32→16→7 MLP); `PatternOnnxExport` with baked normalization; `train_gmd.py` → `pattern_trained.onnx`; contract validation (PMODEL-01–03).
- **Phase 19 — Bass Model:** `BassNet` (7→32→16→4); synthetic E2/A2/B1 training; `train_bass.py` → `bass_trained.onnx`; contract validation (BMODEL-01–02).
- **Phase 20 — Export & Promotion:** `scripts/install-model-local.sh`; `20-VERIFICATION.md` DAW smoke checklist (EXP-01–02).

## [v0.2.0] — ML + Generative (2026-04-17)

- Phase 9 (data & training strategy): `docs/DATASET_AUDIT.md`, `docs/TOKENIZATION.md`, `training/prep_midi.py` stub (DATA-01–03).
- Phase 10 (ONNX): `OnnxInference` loads `assets/accompaniment_model.onnx` when `MA_ENABLE_ONNX=ON`; `RuleBasedInference` fallback; `scripts/build_minimal_pattern_onnx.py`.
- Phase 11 (pitch & harmony): YIN pitch detection; bass root routing (PITCH-01–04).
- Phase 12 (ML structure): Structure ONNX path + rule fallback; integration tests (STRUC-01–03).
- Phase 13 (generative bass): Train/export; rank/select; degradation to static patterns (GBASS-01–03).
- Phase 14 (plugin UI): APVTS policy + editor (PUI-01–03).
- Phase 15 (Python training pipeline): `training/requirements.txt`; `train_pytr_stub.py`; `validate_onnx_contract.py`; `training/README.md` (PYTR-01–03).
- Phase 16 (Terraform model storage): `infra/` S3 + OIDC; `promote-model.sh` / `download-model.sh` (CLOUD-01–02).

## [v0.1.0-phase1] — Phase 1 rule-based MVP

**Summary:** Real-time guitar onset/tempo tracking, energy/structure classification, rule-based `IInference` → drum/bass MIDI via `PatternPlayer`. Phase 2 can replace inference with ONNX or other backends behind the same `IInference` interface.

**Highlights**

- JUCE 8 VST3 + AU; lock-free handoff from audio thread to background inference (~50 Hz)
- `RuleBasedInference` selects patterns from structure state and BPM; `OnnxInference` stub for Phase 2
- Unit tests for onset, structure tagger, pattern player, and rule inference

**Known limitations (Phase 1)**

- Bass pitch fixed (no real-time pitch tracking yet)
- Structure is threshold/rule-based, not ML-classified
- Plugin UI is debug-oriented; full parameter UI is Phase 2

### Tagging this release

After documentation and CI are green on `main`:

```bash
git tag -a v0.1.0-phase1 -m "Phase 1 rule-based MVP; see CHANGELOG.md"
git push origin v0.1.0-phase1
```

Then open GitHub Issues for Phase 2 work using `docs/PHASE2_GITHUB_ISSUES.md` as a checklist.
