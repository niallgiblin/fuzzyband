# fuzzyband / MetalAccompaniment — Authoritative Development Timeline

**Purpose.** A chronological, evidence-cited history of the project, weighted toward **iterations**:
what was tried, what worked, what failed or was reverted, and what had to be attempted a second
(or fifth) time. Written so a future engineer does not repeat failed work.

**Canonical doc set:** [`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md) (start here) ·
[`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md) ·
[`BASS_MIRRORING.md`](BASS_MIRRORING.md) · [`TEST_AUDIT.md`](TEST_AUDIT.md) · [`DOCS_INDEX.md`](DOCS_INDEX.md)

**Repository:** `/Users/ng/projects/fuzzyband` · **Audited at HEAD:** `5d5f410` (2026-09-14, CMake
`VERSION 1.0.3`) · **Commits on `main`:** 326 · **Commit date span:** 2026-04-16 → 2026-09-14.

> **Read §0.3 before citing any version number.** The CHANGELOG was backfilled in bulk for early
> releases, `.planning/` is gitignored, and git history only begins at Phase 7. Several version
> numbers in the CHANGELOG never existed as builds.

---

## 0. Method, sources, and reliability rules

### 0.1 Sources read

| Source | Notes |
|---|---|
| `CHANGELOG.md` (1180 lines, read in full) | Primary narrative; **not** a reliable version-order record (see §0.3) |
| `.planning/PROJECT.md`, `MILESTONES.md`, `RETROSPECTIVE.md`, `ROADMAP.md`, `STATE.md`, `REQUIREMENTS.md` | Milestone/phase authority |
| `.planning/research/{PITFALLS,ARCHITECTURE,SUMMARY}.md` | v0.4.0-era planning (2026-04-28) and v0.3.0-era research (2026-04-18) |
| `.planning/research/rhythmic-coherence/*.md` (6 files) | v0.5.0 design research, undated, no commit hashes |
| `.planning/debug/*.md` (5 files) | The richest failure/re-fix records in the repo |
| `.planning/milestones/*.md` (12 files) | Per-milestone requirement/roadmap snapshots |
| `.planning/quick/**/{*-PLAN,*-SUMMARY,SUMMARY}.md` | Quick-task iterations, incl. the six "accompaniment wiring repair" slices |
| `.planning/v0.6.0-MILESTONE-AUDIT.md` | Claimed-vs-actual gap analysis |
| `.MDignore/*.md` (13 files) | Parked early planning/prototype docs (v0.7.x/v0.8.0 era) |
| `docs/PLAYABILITY_REVIEW.md` (853 lines) | The single most valuable failure document: 23 ranked defects + git archaeology v0.9.44→v0.9.67 |
| `docs/IMPLEMENTATION_PLAN.md` (1021 lines) | The remediation plan (T0.1–T9.5) + two post-implementation reviews |
| `docs/{MUSICALITY_ROCK_PIVOT_PLAN,PHASE9_ACCEPTANCE,END_USER_STRESS_TEST}.md` | Pivot plan, acceptance verdicts, human playtest script/log |
| `git log` (326 commits), `git log --all`, renames, `--grep` for revert/regression/chasing/still | Commit ground truth |

### 0.2 Ordering authority

1. **Git commit dates** (`%ad`, short) are ground truth for *when work landed*.
2. **CMakeLists.txt `project(MetalAccompaniment VERSION …)`** is ground truth for the *plugin
   build version visible in the UI* (`STATE.md:26`).
3. **CHANGELOG version headings** are ground truth for *intended release identity* only.

Where these disagree it is called out inline and collected in §6.

### 0.3 Provenance caveats (important — read before citing versions)

- **`.planning/` is gitignored** (`.gitignore:27` `# GSD planning (local only)` / `.planning/`).
  Only a subset of planning files is force-added (e.g. `.planning/phases/35-*/35-01-SUMMARY.md`,
  `35-03-SUMMARY.md` are tracked; `35-VERIFICATION.md` and `36-VERIFICATION.md` are **not**).
  Most `.planning` docs therefore have **no commit provenance and no datable commit**.
- **Git history starts at Phase 7.** The oldest commit is `0c306ca` (2026-04-16, "docs: map existing
  codebase"). Phases 1–6 have no commit history and no phase directories
  (`RETROSPECTIVE.md:127`: "Early phases (1–6) lack per-plan phase directories").
- **CHANGELOG versions 0.9.0–0.9.16 were written retrospectively in one commit**: `0f225f6`
  (2026-08-19, CMake `0.9.16`, message "Data Improvements P0"). Verified by
  `git log -S"## [0.9.N]" -- CHANGELOG.md`. Similarly `0.9.29`–`0.9.31` were introduced by `d74edb7`
  (2026-08-31, CMake `0.9.33`), and `0.9.45` by `773038a` (2026-09-02, CMake `0.9.47`).
  **There is no per-build commit for any 0.9.0–0.9.16 change**; those version numbers are a
  reconstruction of the v0.8.x-era work.
- **CHANGELOG has missing headings and gaps**; see §6.3.
- `docs/PLAYABILITY_REVIEW.md` was committed in `5385856` ("Musicality Phase 0", 2026-09-10) —
  i.e. the review and the remediation plan were both landed in the same commit as the fix work began.
- `.MDignore/PROTOTYPE_PLAN.md`, `TRIAGE.md`, `SIMPLIFY.md`, `CHANGES_PLAN.md`, `ML_REPORT.md` were
  moved out of the repo root into `.MDignore/` by `d74edb7` (2026-08-31) — the diff for that commit
  shows `CHANGES_PLAN.md | 146 -`, `SIMPLIFY.md | 1041 -`, `ML_REPORT.md | 704 -`,
  `PROTOTYPE_PLAN.md | 480 -`, `TRIAGE.md | 292 -` removed from the root.

---

## 1. Chronological timeline by era

### Era 0 — Pre-history: planning only (undated; before 2026-04-16)

**Goal.** `ROADMAP_PHASE_1.md:3-6`: "A playable JUCE VST3/AU plugin that listens to a guitarist and
triggers rhythmically appropriate drum and bass MIDI in real time — **no ML required yet**",
"8–10 weeks, solo dev, part-time". Scope fence `:16-17`: "**Is not:** ML-driven, generative, or
capable of understanding melody… That's Phase 2". Architectural bet `:19-21`: rule-based inference is
"a drop-in replacement for the ML model in Phase 2 — same interface, same threading model".

**Key early decisions.** 8 milestones (`ROADMAP_PHASE_1.md:42-221`); stack `:223-236` (JUCE 7, CMake,
`std::atomic` + `moodycamel::ReaderWriterQueue`, ch10 drums / ch2 bass); Phase 2 preview `:253-260`
(YIN/CREPE pitch, ML structure classifier, "generative bass line model (small transformer,
PyTorch → ONNX)", genre/intensity/variation UI, Terraform).

**First recorded failure.** `Phase 1 TODO.md:20` records the JUCE 7 → **JUCE 8** deviation ("JUCE 8
required for macOS 15+ / juceaide build"); `:75` records `STAB-02` (CPU < 15% M-series @ 256 samples)
as **fail 2026-04-16** citing `07-05-CPU-PROFILE.md`. This contradicts
`.planning/milestones/v0.1.0-REQUIREMENTS.md:57`, which marks STAB-02 complete. **Unreconciled (§6).**

**First ML prototype design (superseded).** `.MDignore/REAL_TIME_AUDIO_CLASSIFIER.md`: premise "The
rule-based engine (Phase 1) is too brittle to feel musical" (`:11-12`); 5 perception classes
PALM_MUTE / OPEN_CHORD / SINGLE_NOTE / SUSTAIN / SILENCE (`:15-24`); own-guitar dataset ~7,200 windows
(`:120`); CNN < 200k params (`:124-171`); 512 ms / 256 ms mel windows (`:70-101`); "ONNX Runtime
**never** runs on the audio thread" (`:334-335`). Its standalone `fuzzyband-classifier/` pipeline
(`:177-195`) was later superseded by in-repo `training/scripts/build_mel_dataset.py` +
`train_classifier.py` + `scripts/export_classifier_onnx.py`.

---

### Era 1 — v0.1.0 "Phase 1 rule-based MVP" (2026-04-16 → 2026-04-17)

- **Version:** CMake `0.1.0`, added in `de0304d` (2026-04-16, "phase 1.7"); surfaced in the UI by
  `e1bfffd` (2026-04-19, "Add version to UI") while still `0.1.0`.
- **Milestone record:** shipped 2026-04-17, phases 1–8, requirements SCAF/ONSET/ENERGY/MIDI/OUT/
  INFER/STAB/DOCS (`MILESTONES.md:101-124`; `ROADMAP.md:16-35`).
- **Commits (all Phase 7–8):** `0c306ca` map codebase, `3198b11` 07-01 per-state tagger hold times,
  `551678a` 07-02 BPM-adaptive durations, `dbac738` 07-03 processor hardening, `9c62cc3` 07-04 debug
  metrics, `b4ba1ee` 07-05 verification templates/TSan, `de0304d` version bump, `e45daa4` backlog
  999.1, `46213ea` jam-tuned thresholds, `6df67c7` STAB-01..04 evidence, `a6aba0b`, `de1c908`
  (CR-01 + WR-01..06 review fixes), `bed11a5`, `32547b3`, `55c62d9`, `18846e6`, `bd0d49e`, `7fc112b`,
  `caddac0`, `1c154a4`, `cf8abe5`, `377d651` (archive v0.1.0).

**Built.** JUCE 8 VST3 + AU CMake build; GitHub Actions macOS CI; `OnsetDetector` (spectral flux, IOI
median, BPM clamp 80–220); `EnergyAnalyser` (RMS 100 ms, spectral centroid, HF flux); `StructureTagger`
(SILENT/VERSE/CHORUS/BREAKDOWN, 2 s hysteresis); 7 MIDI patterns as `constexpr` data; `PatternPlayer`;
`IInference` + `RuleBasedInference`; background inference ~50 Hz over a lock-free queue
(`PROJECT.md:47-60`; `ROADMAP.md:20-27`).

**Failures / gaps.**
- STAB-02 CPU fail not reconciled with the "complete" checkbox (§6.1).
- **Tests written but never registered in CMake** — the "unregistered tests trap":
  `test_stable_pitch_tracker.cpp` and `test_pitch_estimator.cpp` were unregistered until v0.9.8
  (`CHANGELOG.md:901-903`). This is failure #1 in the project's own defect history
  (`CHANGELOG.md:733` calls it "bug #1").
- Phase 5 Reaper routing confirmation stayed optional (`ROADMAP.md:24,29`); OUT-05 remained "optional
  maintainer DAW smoke-test" (`MILESTONES.md:123`).
- Tags `v0.1.0`–`v0.4.0` never pushed to GitHub (`PROJECT.md:79-82`; `STATE.md:62`) — still open at
  HEAD.

---

### Era 2 — v0.2.0 "Phase 2 ML + Generative" (2026-04-16 → 2026-04-17)

- **Version:** **no `0.2.0` CMake version ever existed.** CMake went `0.1.0` (`de0304d`, 2026-04-16)
  → `0.3.0` (`01a7c14`, 2026-04-19). The milestone is dated 2026-04-17 in
  `PROJECT.md:34`, `MILESTONES.md:83`, `RETROSPECTIVE.md:77`.
- **Phases 9–16**, 18 plan summaries (`MILESTONES.md:85`).

**Built.** Optional ONNX runtime with frozen `docs/ONNX_IO.md` contract and an audio-thread CI guard
(`ONNX-01–03`); YIN pitch estimator + bass root routing (`PITCH-01–04`, `97fd2ac`, `2815a13`);
ML structure path + rule fallback (`STRUC-01–03`, `936a6e1`, `a1730bf`, `fcb835f`); generative bass
with `BassMidiValidator` + rank/select + degradation (`GBASS-01–03`, `521a01f`, `c9e092e`, `00df73f`,
`e54568f`, `3898394`, `d83e97a`, `738909d`, `cb89d54`); APVTS policy params (`genre`, `intensity`,
`variation`, `structureBlend`, `generativeBassMode`) + `PolicyPatternMapper` + editor (`PUI-01–03`,
`7a46e6d`, `ff40e87`); Python training stub + `validate_onnx_contract.py` (`PYTR-01–03`, `e18100d`,
`51a6df9`, `d1675eb`); Terraform S3 + GitHub OIDC + promote/download scripts (`CLOUD-01–02`,
`ea96d68`, `b145992`, `f7e6252`, `491f36c`, `98ebb70`).

**Failed / reversed approaches.**
- **Cloud pivot reversed.** `v0.1.0-ROADMAP.md:158`: "M2 direction is under revision… Likely shift from
  on-device ONNX to cloud inference with a fine-tuned Anticipatory Music Transformer." v0.2.0 actually
  shipped **on-device** ONNX, with the cloud used only for model *storage* (`CLOUD-01/02`). Backlog
  999.1 ("Anticipatory Music Transformer on Lakh, cloud GPU inference at 2–5 Hz",
  `ROADMAP.md:400-416`) was marked **"Superseded for execution"** and its themes absorbed into Phase 9.
- **Phase-numbering collision.** `.planning/phases/15-onset-robustness/` is a **sidecar** iteration,
  *not* roadmap Phase 15 (Python training pipeline) — required an explicit note in
  `ROADMAP.md:51`; recorded as an inefficiency in `RETROSPECTIVE.md:93`.
- **Onset robustness sidecar (2026-04-17, four review fixes).** `7862657` WR-01 instantaneous attack
  on the peak-RMS envelope tracker; `7212604` WR-02 evict-first circular buffer in the rolling-mean
  threshold; `99867ae` WR-03 wrap `ioiRingWrite` into `[0,15]` to prevent signed overflow; `8d2892f`
  WR-04 guard `makeBassInference()` with `MA_ENABLE_ONNX`. Feature work: `4610524` band-limited
  adaptive flux threshold + 16-slot IOI ring; `a5c47ca` relative SILENT threshold via peak-RMS
  envelope; `b14bdcd` 8-onset warmup tests + noise-immunity test. **These four WR fixes are themselves
  re-fixes of the "stale artefact / unbounded buffer" class that recurred in Era 11.**

**ONNX silent-fallback hazard identified here.** `PITFALLS.md:41` and `:173`: a shape/opset mismatch
throws at `Run()`, `catch(...)` "silently swallows", the plugin falls back to rule-based inference with
"no error in the log".

---

### Era 3 — v0.3.0 "Real ML Training Pipeline" (2026-04-17 → 2026-04-20)

- **Version:** `0.3.0` at `01a7c14` (2026-04-19, "v0.3.0").
- **Phases 17–20**, 10 plans with summaries (`MILESTONES.md:62-79`).

**Built.** `training/download_gmd.py` (TFDS-pinned GMD + SHA-256 manifest, `172d23f`); `FEATURE_PROXY.md`
(`e58511e`, `dfc0917`); `build_dataset.py` with grouped split + histogram gate (`6c2e1ba`, `0348b5c`);
`PatternNet` 5→32→16→7 MLP + `PatternOnnxExport` with baked normalisation (`8aa3002`, `5884e2a`);
`BassNet` 7→32→16→4 on synthetic E2/A2/B1 (`763b908`); `install-model-local.sh` (`30f49fc`);
`20-VERIFICATION.md` Reaper smoke checklist (`f906936`, `5ac7fbc`).

**Failures fixed inside the era (all env/training-shape fights).**
- `04ab7c5` add `importlib-resources` for TFDS; `94b7fb4` locate GMD zip under TFDS hashed downloads
  filename; `3b9ea68` patch TFDS `DatasetInfo` for protobuf 6+ upb `FieldDescriptor`.
- `5884e2a` WR-001: BatchNorm broken at batch size 1 → **LayerNorm in `PatternNet`**.
- `b3e3fb2` WR-002: weighted validation CE did not match training.
- `74dab70` WR-001: `BassOnnxExport` batch-1 guard; `fd5396d` skip the guard during JIT trace
  (TracerWarning).
- `f18cf1f`, `79216e8`, `4d3ab4e` code-review passes.

**Deliberate scaffold that became a liability.** `PITFALLS.md:150`: bass training set
`Y[:, 1] = X[:, 5]` — "the model is literally learning to echo back the input pitch root. This is
intentional for v0.3.0 (prove the pipeline works)"; `duration_beats` fixed at 1.0 and `margin` at 0.0.
`PITFALLS.md:154` predicted "always activate but produce no variety". Later confirmed dead:
`SIMPLIFY.md` lists `OnnxBassInference` as "Dead code (proposals overwritten by RuleBasedBass)".

**Known gap at close.** `EXP-02` (non-degenerate pattern selection in a DAW) documented but human pass
pending (`MILESTONES.md:79`; `ROADMAP.md:68`); later deemed satisfied by PVAL-01 in v0.4.0.

**A root-echo / no-variety bass pipeline was a *second* parallel ML path** alongside the proxy
pipeline — flagged as a correctness liability in `MUSICALITY_ROCK_PIVOT_PLAN.md:206`
("two competing ML paths").

---

### Era 4 — v0.4.0 "ML Playability & Simplification" (2026-04-28 → 2026-04-29)

- **Versions:** CMake `0.3.8` (`d9ba044`, 2026-04-28) → `0.4.0` (`4d946f0`) → … → `0.4.10`
  (`bbea3f8`, 2026-04-29) → `0.4.18` (`31a598c`, 2026-05-03).
- **Phases 21–26**, 13 plans, 15 requirements (`MILESTONES.md:36-58`).

**Built (phase by phase).**
- 21 C++ Type Foundation: `StructureState` → 3-value SILENT/SOFT/LOUD (`f71008f`); pure-RMS
  `computeDesiredState`; `StructureHoldSmoother` 3-state + `policyGenreIndex` removed from
  `FeatureVector` (`b6841e2`); inference/editor updates (`582bcdf`); tests rewritten (`d9ba044`);
  `b6b7a2e` blocking refs fixed + bump to v0.4.0.
- 22 ONNX Contract + Stubs: `4fb481b` regenerate stubs + validator; `32e8519` rewrite ONNX contract
  docs; `4d946f0` CI contract validation + enabled-build smoke.
- 23 C++ Inference Layer: `OnnxInference` on the new contract, `OnnxStructureInference` reads
  normalisation from the graph, `excludeIndex` / `patternRejectionCount` ML rejection.
- 24 UI Simplification: `6387e18` **remove genre/variation/`PolicyPatternMapper` atomically**;
  `1e99ed6` session backward-compat test + v0.4.4; `6623df2` plan.
- 25 Training Data Pipeline: `64ca72d` Lakh scripts + 3-class `FEATURE_PROXY.md` (DATA-07/08/09).
- 26 Retrain + Validate: `05f3b8b` bass/structure datasets + WIP absorption; `d52c006` absorb Phase 23
  WIP (INF-01/02 C++, contract stubs); `e2e9ea3` retrain three ONNX heads; `aa8a80d` rebuild v0.4.7
  with models + inference-name label; `ed2423c` PVAL-01 jam testimonial.

**Failures / reverts.**
- **Genre-param removal was the highest-risk change** and was explicitly guarded:
  `PITFALLS.md:64-83` — "if you *partially* remove it … the editor crashes on construction because
  `genreAttachment` tries to bind a ComboBox to a nonexistent parameter"; ROADMAP risk register
  (`ROADMAP.md:287`) rates "Genre APVTS removal partial crash on session load | High" with mitigation
  "All 4 files changed atomically in Phase 24; session XML round-trip test gates the phase". It was
  done atomically and did not crash.
- **`PolicyPatternMapper` deleted as dead code** (`.planning/research/ARCHITECTURE.md:20`); the
  `(idx + policyGenreIndex) % 7` offset had been corrupting ML evaluation (`PITFALLS.md:189-193`).
- **Deferred:** Lakh `lmd_full` expansion, autoregressive/LSTM bass, cloud promotion for new ONNX
  shapes, Windows build (`v0.4.0-REQUIREMENTS.md:49-54`).
- **Milestone close process gaps:** quick-task summaries used prefixed filenames instead of
  `SUMMARY.md`; living `REQUIREMENTS.md` checkboxes lagged traceability
  (`RETROSPECTIVE.md:22-24`).

---

### Era 5 — v0.5.0 "Rhythmic Coherence" (partial) + Phase 31 Architecture Deepening
**2026-04-29 → 2026-06-03**

- **Created** 2026-04-29 (`902363a` docs(research): rhythmic-coherence proposals for v0.5.0;
  `6ac5c1f` archive v0.4.0 milestone).
- **Never shipped.** Requirements RHY-TEMPO-01/02, RHY-BASSSEQ-01, RHY-SYNC-01, RHY-FEAT-01,
  RHY-FILL-01, RHY-ML-01/02 all remain unchecked; only RHY-DOC-01/02 done
  (`.planning/milestones/v0.5.0-REQUIREMENTS.md`). There is **no `v0.5.0-ROADMAP.md`**.

**Phase 27 — documentation** (2026-04-29): `a01acd4` context + research stub; `631f94a`;
`994e424` review (README ONNX wording, CONTRIBUTING `IInference` cite); `299e1e9`, `8f73b65`.
Pre-FX placement story: plugin must sit **before** fuzz/amp (`rhythmic-coherence/02-*.md:11-16`),
matching "Roland GK, BIAS, every commercial guitar-to-MIDI converter"; post-fuzz the failure modes were
"onset attack compressed / YIN odd-order harmonics confuse F0 / RMS 4–6 dB swing / centroid locked into
harmonic cloud / HF flux constant noise floor" (`02:25-31`), and post-fuzz "won't crash. Tempo lock
will be unreliable, pitch will drift, dynamics will be flat" (`02:62-64`).

**Phase 28 — beat tracker + bass sequencer** (`28-01/28-02`, 2026-04-29):
`1fe46c0` context; `ce0b006` WR-02 chronological IOI ring for tempo-lock consistency;
`bbea3f8` WR-01 sorted bounded defers for generative bass steps.

**Preceding quick tasks (2026-04-20 → 2026-04-27, before Phase 28's BeatTracker work).**
`1f30640`/`2a5e068`/`4a12185` on 2026-04-20 (quick 260420-421: octave fold in `medianIoiBpm`, EMA
smoothing on `PatternPlayer::setBpm`, pitch-stability gate + 2-bar bass root hold); `59ee3d9`/`09a92c4`
on 2026-04-27 (quick 260427-t42: `snapToBarStart`, 4-onset count-in gate, 2-bar drum hold, v0.3.4);
`1c02d3e`/`da6b2ad` on 2026-04-27 (quick 260427-t43: **BPM lock-in after 8 consistent IOIs, 5-BPM
quantization, 80 ms refractory**, v0.3.8 — the 5-BPM quantisation was reverted three days later, §2.5);
`64732d7` (quick 260427 bass-pitch-class tracking, v0.3.7).

**Phase 28 FAILED UAT (2026-04-30).** `.planning/phases/28-beat-tracker-bass-sequencer/28-UAT.md`:
`total: 4 / passed: 1 / issues: 3`, all `major`:
1. "No it doesn't mathc it's about 20bpm too fast" (BPM ±5 requirement failed).
2. "no it currently doesn't ever really play a full phrase or anything, it sort of seems like it's
   doing some small intro fill and then never plays the full phrase" (beat gate never fully opens).
3. "it sometimes plays 16ths but is mostly similar to the drums, only outputting a few things and
   never locking into a groove" (RHY-BASSSEQ-01).
Recorded verdict: "Phase 28 implemented but UAT-failed — folded into v0.7.0 S01 Tempo Stability
(BeatTracker refocused on learn-mode/gate)" (`MILESTONES.md:32`; `STATE.md:69-72`).
The root design defect was that `emitGenerativeBassSteps` fired only step 0 per block
(`rhythmic-coherence/04:25-32`: at 120 BPM / 256-sample block `stepLenSamples ≈ 5500` > 256 samples, so
only `step = 0` qualified → "sounds like a buzzy clicking on the root note").

**Three fix rounds for the Phase 28 fallout (.planning/debug/).**
- `drums-gate-tempo-regression.md` (**resolved**, v0.4.11): symptom "drums rarely/never come in; tempo
  wrong by >5 BPM"; "Regressed after phase 28 commits (`bbea3f8`, `ce0b006`) — BeatTracker + IOI ring
  changes". Root cause: `kPlaybackConfidenceStart=0.50` never reached with noisy flux;
  `onsetDetector.isTempoLocked()` existed but "is NOT used"; `medianIoiBpm()` rounded to the nearest
  5 BPM giving ±2.5 BPM error. Fix: lower to 0.25, add `|| onsetDetector.isTempoLocked()` at
  `AccompanimentProcessor.cpp:477`, and **remove** `bpm = std::round(bpm/5.0f)*5.0f` — i.e. the
  5-BPM quantisation added in `1c02d3e` (v0.3.8) was **reverted 3 days later**.
- `phase-28-uat-failures.md` (**resolved**, v0.4.12): explicitly names the prior fix as its base
  ("kPlaybackConfidenceStart 0.50→0.25, `isTempoLocked` fallback, remove 5-BPM rounding") — a repeat
  pass. New root cause: autocorrelation normalisation `fabs(acc)/(W-lag)` biases toward higher BPM
  (+15–25 BPM); no octave disambiguation; EMA α=0.15. Fix: octave disambiguation in
  `BeatTracker::recompute()` explicitly mirroring `OnsetDetector::medianIoiBpm():96-101`. Also records
  "generative bass path is dead code in current build" (`bassInference == nullptr`) and that
  `kPlaybackConfidenceStart` was "already lowered to 0.25 in prior session".
- `accompaniment-groove-stability.md` (**awaiting_human_verify**, created 2026-04-30): five successive
  releases in one day —
  v0.4.14 still stopped after 1–2 s and BPM far off → v0.4.15 groove lasted longer but **locked at
  200 BPM** → **v0.4.16 "No accompaniment at all; not a single hit" — a new regression introduced by
  the tempo-alias fix** → v0.4.17 restored playability but "tempo is sometimes wrong and changes too
  easily" → v0.4.18 added the 4-BPM deadband + 2 s BPM latch. Note `:39`: "No debug knowledge base
  file exists for prior matching sessions."

**Phase 29 — Runtime coordination (C++)** — 2026-06-02/03, CMake `0.6.0`–`0.6.3`:
`e48b44a` rescope audit; `1ed3e9e` / `4f1cb9b` / `bfe495c` shared `PatternPlayer::GrooveCommit`
(drum pattern + generated bass cross the boundary as one payload); `793e272` / `718397f` / `895ccf4`
four directional transition fills through the shared commit path; `6f52994` phase closed.
UAT passed 8/8, security verified 0 open threats (`STATE.md:80`).

**Phase 30 — ML retrain (12-feature input): SUPERSEDED, never started.**
"Never started. Original scope assumed v0.5.0's reactive tempo/phase semantics, which v0.7.0 discards"
(`STATE.md:74-76`; `ROADMAP.md:203-206`).

**Phase 31 — Architecture Deepening** (2026-05-03, CMake `0.5.0`):
`b3e4d8b` plan; `f8a1d61`/`1799def`/`3a183a7` `PatternRules` header-only extraction (ARCH-03);
`798848e`/`d8ba9a2` `TempoStabiliser` with deadband/hold hysteresis (ARCH-04);
`f8018ae`/`b0bfb65` `PlaybackGate` + `GateDecision` (ARCH-01); `55c8d55`/`055fbf2` `StablePitchTracker`
+ **version 0.5.0** (ARCH-02); `31a598c` "code cleanse".

**Phase 31 review fixes were committed to an unmerged branch.** Branch
`origin/claude/wonderful-payne-cfe667` contains `fe3387e`, `d30e61e` (WR-02 sync `stablePlaybackBpm`),
`73dc84a` (WR-03 `fullResetFired` one-shot flag), `2b10643` (WR-04 delete `prevStructureSilent` dead
state), `6719b07` + `1e21166` (WR-01 `applyExclusion` wraps within the state-compatible range), all
2026-05-03. **None are ancestors of `main`** (`git merge-base --is-ancestor` = NO for all six).
`fullResetFired` / `prevStructureSilent` do not exist in the current tree. The equivalent
`applyExclusion` behaviour did later land on `main` via Phase 35 (`src/inference/pattern_rules.h:230`)
— the same fix reached twice, once discarded.

---

### Era 6 — v0.6.0 "ML Correctness & Evaluation" (2026-05-11 → 2026-06-02)

- **Versions:** CMake `0.5.0` (`4abe0b0`) → `0.5.1` (`6199c3d`) → `0.5.2` (`04871ee`) → `0.5.3`
  (`13b73c2`) → `0.5.6` (audit time) → `0.6.0` (`e48b44a`, 2026-06-02).
- **Source design doc:** `.MDignore/CHANGES_PLAN.md` "Areas 1–5"; findings from
  `.MDignore/ML_REPORT.md` (**"Version: v0.5.0 / Date: 2026-05-11"**).
- **Phases 32–36**, 16 plans, 16+ requirements (`MILESTONES.md:3-27`).

**Built.**
- **32 Training Label Correction** (2026-05-12): `f118ef8` replace quantile-bin labels with a rule
  oracle + Breakdown heuristic; `eaa2144` apply oracle labelling to `build_lakh_dataset.py`, tensor key
  `scores` → `Y`; `8db085a` remove quantile recomputation from `merge_datasets.py`; `86bb339`
  `FEATURE_PROXY.md` retire quantile bins; `03bdc37` oracle unit tests; `1c9bbd8` retrain + promote
  `assets/accompaniment_model.onnx`; `fd9123a` exempt classes 0/6 from the DATA-06 gate; `97c3ff8`
  guard `class_f1` index bounds; `441e577`, `e387c1c`, `b2828d7`.
- **33 Model Quality Gates** (2026-05-13): `80cf287` bass MSE gate + per-step metrics + per-step
  velocity-error/pitch-offset `validation.json`; `2eb18a9` structure dual gate + `structure_norm_stats.json`;
  `bf68b80` `StructureOnnxExport` baked-normalisation wrapper; `4abe0b0` wire `train_structure.py`
  export through `from_norm_stats`; `fc70543` contract tests + promote; `80d2262`, `9cbcdf1`,
  `3f1f155`, `7209b69`.
- **34 Domain Gap & Feature Capture** (2026-05-13): `16dea44` runtime `feature_capture.v1` JSONL +
  debug toggle; `e5e5bfa` offline capture evaluator (rule vs ONNX accuracy, confusion matrices,
  disagreement); `9ca4051` captured-vs-proxy gap analyzer + quantitative `FEATURE_PROXY.md` verdicts;
  `6199c3d` merge.
- **35 Inference Path Consistency** and **36 ONNX Evaluation & Default Readiness**: **no phase-numbered
  commits exist in `git log`.** Related work landed in `04871ee` (2026-05-20, docs: ROADMAP/STATE,
  `ARCHITECTURE.md` +115, `docs/RUNTIME_ARCHITECTURE.md`, `docs/MODULARITY_AND_BLOAT_REVIEW.md`,
  `docs/CPP_JUCE_AUDIO_BEST_PRACTICES_AUDIT.md`) and `d3978d3` (2026-05-20, "Audit fixes":
  `AccompanimentProcessor`, `OnnxBassInference`, `OnnxInference`, `OnnxStructureInference`).
  `35-VERIFICATION.md` / `36-VERIFICATION.md` exist on disk but are **untracked** (`.planning/` is
  gitignored).
- `13b73c2` (2026-05-22) "Libraries Summary" adds `.MDignore/CPP_LIBRARIES.md`.

**Failures / gaps (from `.planning/v0.6.0-MILESTONE-AUDIT.md`, 2026-06-02).**
- Verdict `passed` **only after remediation**. Original gap list (`:80-102`):
  "⚠ Phase 36 model may differ from Phase 32 (Phase 36 retrains with centroid fix)";
  "⚠ Gate passes unverified"; "⚠ CI gate unverified — pipeline may silently skip benchmark";
  ROADMAP showed Phase 36 "0/4 | Planned"; `REQUIREMENTS.md` still at v0.5.0; `CMakeLists.txt` still
  `VERSION 0.5.6`; **"No Phase 33 or 36 verification was run."**; Phase 34 centroid gap "deferred to
  Phase 36 … **unverified**".
- Permanent accepted limitation: **class 0 (SILENT) excluded** from training data and the DATA-06 gate
  because MIDI corpora lack SILENT examples; rule passthrough handles it at runtime
  (`MILESTONES.md:24-26`).
- **D-09** "ONNX class-6 (Breakdown) returned at runtime — not silently discarded" implies an earlier
  class-discard defect.
- v0.1.0-era docs and `CHANGELOG.md` both reference `.gsd/STATE.md` / `.gsd/ROADMAP.md` (e.g.
  `CHANGELOG.md:3`) — `.gsd` is a symlink to a local GSD project store, not repo content.
- Post-close: `9d8ed74` (2026-06-09, CMake `0.7.8`) "remove tempo changing".

---

### Era 7 — CHANGELOG [0.7.0] "Creative Companion" / M001–M002 (2026-06-02 → 2026-06-09)

- **Versions:** `0.6.4`–`0.6.7` and `0.7.2` (all 2026-06-03), then `0.7.8` (2026-06-09).
- **Goal** (`CHANGELOG.md:1079`): "ONNX-first inference that doesn't jitter: stable tempo, stable
  ML-driven structure detection, and musically distinct grooves — without replacing ML with manual
  controls."

**Commits (all 2026-06-03 unless noted):** `e685806` `TempoStabiliser::warmStart(float bpm)` (0.6.4);
`101c70c` soft-lock EMA drift on `OnsetDetector` α=0.03 + remove hard freeze (0.6.5); `cc75389` BPM
save/restore through the silence-reset cascade (0.6.6); `d72683f` E2E groove-variety test ≥3 distinct
grooves (0.6.7); `afca7e4` integrate `diversifyPattern()`; `b276b44`, `e117fc8` S02 structure stability,
`c117c23` full suite 581 assertions/132 cases, `b0b1bf0` phrase-breath hold 2.0 s → 3.0 s,
`a1150a8` M002 S01–S03 (BPM foundation, structure expansion, sub-bass energy); `53166ea` M002 S04
half-time LOUD routing, widened SOFT window, 8 s gate hold (0.7.2); `9d8ed74` (2026-06-09) remove tempo
changing (0.7.8).

**Failures / inconsistencies.**
- **Milestone/commit label mismatch:** `STATE.md:3-4` says `milestone: M001`,
  `milestone_name: Creative Companion`, slices S01–S04 `pending` — while those exact commits say
  "M002 S01-S03"/"M002 S04". §6.
- **Known limitations admitted at close** (`CHANGELOG.md:1105-1108`): "BPM control through synthetic
  test signals is unreliable with the current 2048-sample FFT onset detector — test-only BPM injection
  API suggested for future ML integration tests"; "ONNX model still trained on pre-expansion 7-pattern
  labels — `diversifyPattern()` bridges the gap deterministically until retrain"; "Human UAT (5+ minute
  Reaper jam with clean DI guitar) is the final acceptance gate".
- `CHANGELOG.md:1112` (the [0.6.7] block) states "Plugin version 0.5.6 in `CMakeLists.txt`" while the
  same document has 0.9.x entries above it — evidence the changelog was written non-chronologically.

---

### Era 8 — v0.8.0–v0.8.4: the unified mel-CNN prototype (2026-06-28 → 2026-06-29)

- **Versions:** `0.8.0` (`04dd3bc`, "prototype") → `0.8.1` (`f6033db`) → `0.8.2` (`ea6042b`) →
  `0.8.3` (`30d7044`, `016a5b1`, `2478af3`, `6021df2`, `afe13bb`) → `0.8.4` (`4f905f8`).
- **Then a ~6.5-week commit gap** (2026-06-29 → 2026-08-14).

**Commits:** `516f769` "close to prototype", `04dd3bc` "prototype", `7257ddc` "simple prototype",
`fddf403` "tests", `1406c0c` "fix tests", `f6033db` "Wave 1+3: Training pipeline +
`MetalGrooveInference` ONNX integration", `45ee32b` "Wave 3: Mel queue integration — wired
end-to-end", `b99958d`, `ea6042b` "Wave 4: Docs + `MetalGrooveInference` unit tests", `5f0b42a`
"Cleanup — remove stale ONNX assets + old recordings", `016a5b1` "Fix ONNX Runtime rpath — absolute
dylib path for plugin loading", `2478af3` "Remove intensity slider, fix bass root to C2 (audible
range)", `6021df2` "Reactive bass + playing-style display", `afe13bb` "**RiffMirror** — learning bass
that mirrors your riff after one repeat", `30d7044` "Fix bass — immediate trigger, quarter-note
fallback", `4f905f8` "Fix bass note duration — **0.9 beat legato** (was 0.25 = 16th-note blip)".

**Parked planning docs driving this era (all later `.MDignore/`, no dates except SIMPLIFY):**
- `TRIAGE.md` ("v0.7.11 Prototype Fixes") — BeatTracker wired but never called: "`BeatTracker::pushFluxSample()`
  is **never called**"; BPM read only from APVTS; `PlaybackGate` hardcodes
  `isOnsetTempoLocked = true` → "**Drums always play at 120 BPM**". Style classifier trained on
  near-breakup amp-sim recordings while the plugin sits pre-amp on clean DI — rated **CRITICAL**, later
  "✅ RE-RECORDED". `stylePatternPool` ran unconditionally, so on non-ONNX builds
  `stylePatternPool(0)` permanently locked drums to `{1,2,7}` — "LOUD and BREAKDOWN patterns (4, 5, 6)
  are never selected". Structure hold times multiplied "×4–8 for 'sludge pacing'" produced a measured
  worst case: "6 s (AMBIENT→SOFT) + 4 s (2-bar hold) = **10 s lag**".
- `PROTOTYPE_PLAN.md` ("Fuzzyband v0.7.x", bumps `0.7.11 → 0.7.12`) — implements the tempo fix
  ("This is the most impactful single change"), the style re-record, and states the pattern ONNX model
  is "a noisy rule-oracle copy… macro-F1 0.64 … actively *worse* than the rule-based path", offering
  "Option A: Disable `OnnxInference`, keep everything else".
- `SIMPLIFY.md` (**"v0.8.0 / Created: 2026-06-28"**) — the third architecture proposal. Decisive
  reverts/deletions:
  - "**StructureTagger:** Revert to 3-state" and "Remove AMBIENT and BREAKDOWN from enum"; "FIXED BY
    REVERTING TO 3-STATE".
  - Delete `BeatTracker.h/.cpp` outright ("Tempo from manual BPM knob or DAW transport only").
  - Delete `PitchEstimator` + `StablePitchTracker`: "**YIN unreliable on distorted guitar**".
  - `OnnxStructureInference` "Broken (5-state vs 3-state index mismatch)"; `OnnxBassInference` "Dead
    code (proposals overwritten by RuleBasedBass)".
  - New proposal: single mel-CNN `metal_groove.onnx`, 22 patterns, embedding nearest-neighbour, LSTM
    fill head; two competing fill-export designs offered.

**What actually happened (code evidence).**
- `BeatTracker` **was** deleted — but not until `40cc331` (2026-08-14, CMake `0.8.12`), which removes
  `src/analysis/BeatTracker.cpp` (204 lines) and `BeatTracker.h` (67 lines) and adds
  `src/analysis/PhraseLearner.cpp` (286 lines).
- `PitchEstimator` and `PhraseLearner` **survive** in the tree (they are used at HEAD in
  `src/AccompanimentProcessor.cpp`), i.e. the "delete pitch tracking" part of `SIMPLIFY.md` was **not**
  executed and pitch tracking returned.
- `StructureState` remains 3-value SILENT/SOFT/LOUD — the **second** time 4-state was collapsed to 3
  (the first was v0.4.0 Phase 21).
- `016a5b1` fixed the ONNX Runtime rpath — the exact class of defect that produced **silent rule-based
  fallback** (`PITFALLS.md:41,173`).

---

### Era 9 — v0.8.12 → v0.9.44: retrofit changelog, data work, subgenres (2026-08-14 → 2026-09-02)

- **Versions:** `0.8.12` (`40cc331`) → `0.9.16`–`0.9.19` (2026-08-19) → `0.9.26` (`f54b058`) →
  `0.9.27` (`297a796`) → `0.9.33` (`d74edb7`) → `0.9.44` (`fb32a15`) → `0.9.47` (`773038a`) →
  `0.9.48` (`1ac82d2`).

| Commit | Date | CMake | What |
|---|---|---|---|
| `40cc331` | 2026-08-14 | 0.8.12 | "clean up": delete `BeatTracker.{h,cpp}`; add `PhraseLearner.cpp`; `AccompanimentProcessor` +144; `MelSpectrogramExtractor` |
| `0f225f6` | 2026-08-19 | 0.9.16 | "Data Improvements P0" — **also writes CHANGELOG 0.9.0–0.9.16 retrospectively** |
| `b118ad2` | 2026-08-19 | 0.9.17 | "Data Improvement P1" — writes CHANGELOG 0.9.17 |
| `cc7536f` / `f7d5644` | 2026-08-19 | 0.9.18 | "Data improvement P2/P3" (P3 writes the unnumbered "Data Improvement P3" section) |
| `90e41e8` | 2026-08-19 | 0.9.19 | "Data Improvement P4" |
| `f54b058` | 2026-08-21 | 0.9.26 | style-head training, click track, groove variety, riff-lock progress UI |
| `297a796` | 2026-08-26 | 0.9.27 | "Debugging mode state confusion" — `StructureTagger.h`, several tests, README +302 |
| `d74edb7` | 2026-08-31 | 0.9.33 | "rock retrain" — adds `assets/style_cnn.onnx`, retrains `metal_groove.onnx`; **moves 9 planning/teaching docs into `.MDignore/`**; writes CHANGELOG 0.9.29–0.9.31; adds `pages.yml`, `release.yml` |
| `fb32a15` | 2026-09-02 | 0.9.44 | "Diversify outputs" — adds `assets/groove_renderer.onnx` + fonts, release workflow, website |
| `773038a` | 2026-09-02 | 0.9.47 | "Add subgenres and stress test" — 13 genres; writes CHANGELOG 0.9.45 |
| `1ac82d2` | 2026-09-02 | 0.9.48 | "Bug fixes and stress test update" — writes CHANGELOG 0.9.48 |

**The retrospective narrative inside `CHANGELOG.md:911-1015` (versions 0.9.0–0.9.13) describes the
v0.8.x-era bass/tempo battle:** bass root a major third off (E2 vs C2), drop-C below the YIN band,
octave folding, false riff locks from a warm-up ramp, a pitch-confidence gate that starved the learner,
sparse staccato from a skeletal 2-note lock, the mirror being suppressed while held, and a `0.9-beat
legato` claim that is explicitly retracted at `CHANGELOG.md:785-789`: "There is no single 0.9-beat
legato on every bass path."

**Known defects recorded here:**
- `CHANGELOG.md:610-650`: the `0.9.26` block **has no `## [0.9.26]` heading** — its text sits directly
  under the 0.9.29 section and ends with the marker line "v0.9.26 (versioned build per workflow)."
- `CHANGELOG.md:742-744` (0.9.14 Phase 3): spectral-centroid std was clamped to `1e-8`
  (`merge_datasets.py`, `pattern/structure/bass_model.py`), "making live inference saturate" — requires
  re-running `merge_datasets.py` and re-exporting the ONNX models. Confirmed still open at
  `MUSICALITY_ROCK_PIVOT_PLAN.md:45,166`: "The `1e-8` centroid std makes live inference effectively
  random."
- `CHANGELOG.md:719` — an `[editor]` construction smoke test was added after the 0.9.15 crash
  (below).

---

### Era 10 — 0.9.48 → 0.9.67: human stress-test-driven iteration (2026-09-02 → 2026-09-09)

This is the densest iteration period: nine days, ~20 commits, at least four separate attempts at the
same bass behaviour, and one bundled quick task that was later judged a false pass.

**Commits and what each did.**

| Commit | Date | CMake | What |
|---|---|---|---|
| `1ac82d2` | 2026-09-02 | 0.9.48 | Record-mode **A-B-A-C-A** (was A-B-C-A); Play stops after the form |
| `1d36e47` | 2026-09-04 14:17 | 0.9.50 | "**Fix bass regression**" — unify every lock/transition/listen schedule onto `clockSample = patternPlayer.previewResolvedHostSample(rawHostPos, numSamples)` (comment: "This is the same bug class fixed for the scope playhead in v0.9.31"); add `riffLoopActive`; `PhraseLearner::rewindRiffToDownbeat()`; publish `sectionPhase/Bar/BarsTotal/BarsRemaining/Progress`; CHANGELOG 0.9.50 (deterministic record-riff loop + **new Stop button**) |
| `5fea0d8` | 2026-09-04 14:20 | 0.9.51 | "**Remove button**" — the Stop button added three minutes earlier is deleted ("It did the same thing as Forget … did not pause or resume") |
| `5db5028` | 2026-09-04 14:29 | 0.9.53 | "symbol fix" — renumbers the entry to 0.9.52 and adds 0.9.53 (ASCII `Style: -` placeholder) |
| `a898b06` | 2026-09-04 15:04 | 0.9.54 | "**Still chasing bass regression**" — `PhraseLearner` silence reset (`rms < 0.003f`) suppressed while `holdActive_`; fixes "LOCKED RIFF DOESN'T PERSIST" on a breath |
| `bb487e7` | 2026-09-07 | 0.9.56 | "cleanup" — delete `build-phase10/`, CI/CONTRIBUTING/README |
| `546ed38` | 2026-09-08 | 0.9.56 | add **failing** tests for pinned contrast slots (quick 260908-kk2; PLAN only, no SUMMARY) |
| `cdf4bac` | 2026-09-08 | 0.9.60 | "Fix record mode transition logic" — **removed** the RMS-delta `transitionEvent` and the 4-bar `autoChangeReady` timer |
| — | 2026-09-08 | 0.9.61 | quick 260908-m7z: DAW record clock handoff (frozen→rolling snaps to host timeline, no click/groove dump, ignore small position blips) |
| `7caacc5` (0.9.60), `cf88acd` (0.9.62) | 2026-09-09 | 0.9.60→0.9.62 | slice 1 (`i70`): exclusive `EnginePhase`; Record A is a 64-slot `riffA` piano roll; return-to-A loads the snapshot |
| `ddfaee8` (0.9.62), `ac576f8` (0.9.63) | 2026-09-09 | 0.9.62→0.9.63 | slice 2 (`itt`): unified listen bass mixer; **"Library `bassEvents` are not mixed into the live path"**; B/C holes gone; late picks emit now; guitar stop cuts locked bass |
| `e566f61`, `0680160` | 2026-09-09 | 0.9.64 | slice 3 (`jjx`): Play hybrid drums inside the section pool; argmax constrained to pool; sticky 2-bar hold; no hash rotation |
| `6f638fc`, `85a5e62` (0.9.64); `c3bfc21`, `07375c2` (0.9.65) | 2026-09-09 | 0.9.64→0.9.65 | slice 4 (`jtf`): on-grid fills 17/18/19 in beat time; crash only when armed; fills on the outgoing last bar |
| `85a697f`, `bbf1f9e` | 2026-09-09 | 0.9.66 | slice 5 (`kpq`): Record B contrast home; learn `riffB`; freeze on B; A never overwritten |
| `d90bc3e`, `d68c9d5` | 2026-09-09 | 0.9.67 | slice 6 (`l6u`): **disconnect `GrooveRenderer` from the live path**; template humanize only; renderer kept compiling for a later milestone |

**Failures and re-attempts in this era.**
- **The bundled quick task `260909-gxy` was a false pass.** `itt-PLAN.md:78`: "Previous quick
  `260909-gxy` **falsely marked slices 1–6 done by weakening tests**. Slice 1 was redone as
  `260909-i70`." `260909-gxy-SUMMARY.md:18` also says "Uncommitted. No git commit from this task" while
  claiming v0.9.62 with all six slices.
- **Slice 2 deficiency carried forward:** `i70-VERIFICATION.md` lists leftover slice-2/5 anti-patterns
  including `phraseLearner.loadPattern(riffB)` at `AccompanimentProcessor.cpp:1312`.
- **Slice 2 known timing flake:** `jjx-SUMMARY.md:77`: "Slice 2 `RiffBListen bass has grid on beats
  1/3` is timing-flaky (1/3 runs failed on this machine); not changed here."
- **Slice 4 self-inflicted regression:** `jtf-SUMMARY.md:76-78` — arming fill 17 at beat 3.989 expired
  immediately, producing "zero beat-4 toms"; fixed via `fromNextBar`.
- **`cdf4bac` deleted the two reactivity short-circuits and never restored them.** `PLAYABILITY_REVIEW.md:73-75`:
  "The two triggers that used to short-circuit the hold (`|rmsDelta| > 0.6` and a 4-bar auto-change
  timer) were deleted in `cdf4bac` (v0.9.57–v0.9.60) and never restored. `rmsDelta` is still computed
  and published (`AccompanimentProcessor.cpp:566,668`) but nothing reads it." This is the origin of the
  ~6.5 s response latency defect (§2.7 of the review).
- **Stale installed binary.** `PLAYABILITY_REVIEW.md:11-26`: the playtest that produced the review was
  run against an installed **v0.9.62** while source HEAD was **v0.9.67**; the repo's own
  `build/MetalAccompaniment_artefacts/Release/VST3/fuzzyband.vst3` contained only `Info.plist` and no
  binary. Fixing this became `T0.1` of the remediation plan.

---

### Era 11 — v0.9.67 → v1.0.0-rc: the Playability Review remediation (2026-09-10 → 2026-09-11)

- **Version cliff:** `0.9.67` → `0.9.68` → … → `0.9.76` → `1.0.0` (all within two days).
- **Trigger:** `docs/PLAYABILITY_REVIEW.md` (853 lines), written against `d68c9d5` (v0.9.67) using
  "full read of the MIDI/selection/hold/clock path, git archaeology across v0.9.44 → v0.9.67, both
  test binaries run, numerical simulation of the riff-lock clock and the velocity chain"
  (`PLAYABILITY_REVIEW.md:4-5`). Committed with the remediation plan in `5385856` ("Musicality
  Phase 0", 2026-09-10).

**The review's 23 ranked defects** (`PLAYABILITY_REVIEW.md:79-722`) — the failures, in its own priority
order: drum note-offs force-fitted to the block §2.1 (blocker); DAW loop wedges the riff lock §2.2
(blocker); velocity chain saturates §2.3 (blocker, musicality); no Play rotation §2.4; authored bass
dead **for the second time** §2.5; locked riff re-articulates every 16th §2.6; ~6 s latency §2.7;
uninterruptible transitions contradicting the shipped contract §2.8; guitar-stop gate kills the lock
§2.9; humanisation moved the grid and muted ghosts §2.10; per-bar ornaments rewrite the groove and
`guitarEnergy` is a constant §2.11; split-block time base §2.12; crash handling §2.13; seek/loop drops
note-offs §2.14; bass octave control wrong pitch classes §2.15; ringing mirror suppresses the downbeat
§2.16; attack detector drops fast chugs §2.17; fills unreachable/late/overlaying §2.18; swing slider
desync §2.19; idle readout §2.20; three groove templates are the same data §2.21; **tests enshrine the
regressions §2.22**; dead code/races/doc drift §2.23.

**Severity of §2.22 ("process risk")** — `PLAYABILITY_REVIEW.md:690-702`:
`tests/test_pattern_player.cpp:453-456` asserts the authored bass lines **must not** play;
`tests/test_e2e_groove_variety.cpp:147` asserts `>= 2` where its title says `>= 3`;
`tests/test_processor_pipeline.cpp:1138` pins "transition does not cut short"; `test_pattern_player.cpp:148`
renders one giant block so §2.1/§2.12 are invisible; no test ever moves the playhead backwards so §2.2
is invisible. "All 227 unit cases and 56 integration cases pass. **Green tests are currently not
evidence of playability.**"

**Implementation plan** (`docs/IMPLEMENTATION_PLAN.md`): decisions D1–D6 (`:34-44`), Phases 0–9
(T0.1–T9.5), "**Never weaken an assertion to make a fix pass.** Three existing tests currently encode
the regressions (T0.3)" (`:20-21`), rollback strategy ("Every phase is independently revertable … each
task is its own commit"; "Phase 2 is the only one where a partial revert is unsafe — revert T2.1 and
T2.2 together", `:905-909`), traceability matrix §2.1–§2.23 → tasks → tests (`:924-951`), risk register
(`:893-903`) and a release-slicing table (`:911-921`).

**Commits.**
| Commit | CMake | Content |
|---|---|---|
| `5385856` | 0.9.67 | "Musicality Phase 0" — lands `PLAYABILITY_REVIEW.md` + `IMPLEMENTATION_PLAN.md` |
| `3dd067a` | 0.9.68 | Phase 1 rendering correctness: T1.1 deferred drum note-offs, T1.2 seek/silence/bypass release, T1.3 mid-block sample base, T1.4 learned-bass octave fold, T1.5 ghost threshold 62, T1.6 armed crash on a beat |
| `d0bb9c5` | 0.9.69 | Phase 2: **T2.1** monotonic `hostSampleTime` lock schedules with bar phase latched once at engage; **T2.2** seek/loop-wrap re-anchor preserving remaining lock; T2.3 rename |
| `7ab05a9` | 0.9.70 | Phase 3: T3.1 velocity headroom + soft knee; T3.2 bidirectional `guitarEnergy`; T3.3 mean-centred groove templates; T3.4 deterministic humanisation (per-event hash on bar/grid16/voice/salt) |
| `a729e66` | 0.9.71 | Phase 4: T4.1 Play rotation via `pickPoolPattern`; T4.2 mel variety draw re-enabled; T4.3 `humanize` param default 0.35, `rideSwitch` default 0; T4.4 idle shows pattern 0 |
| `7984394` | 0.9.72 | **Post-implementation review of Phases 0–4** + defect fixes R1–R6 |
| `310fc7b` | 0.9.73 | Phase 5 bass musicality: T5.1 reconnect authored bass via `emitBassRange`; T5.2 frozen-riff note lengths (onset vs sustain per 16th, 90 % gate); T5.3 grid bass retrigger over a ringing mirror |
| `8e27a05` | 0.9.74 | Phase 6 reactivity: T6.1 1-bar hold + beat-level gesture fast path; T6.2 same-riff cut-short; T6.3 B-listen rotates and can lock; T6.4 guitar-stop no longer kills a lock; T6.5 any RMS decrease arms the next rise |
| `ee15307` | 0.9.75 | Phase 7 fills: T7.1 Record fill seed; T7.2 last-bar origin via `armBarFillAtBeat`; T7.3 fills replace the groove and inherit feel |
| `620d62b` | 0.9.76 | Phase 8: T8.1 dead code removal; T8.2 races (`lastLoopValue` per-instance member, triple-buffered UI snapshot, learned-bass queue drops newest); T8.3 swing notify; **T8.4 skipped** (optional, data-dependent); T8.5 docs; T8.6 deferred |
| `1217d70` | 1.0.0 | Phase 9 verification and acceptance |

**Failures the remediation itself produced or exposed.**
- **`T9.2` was documented as unmet and it was true.** `IMPLEMENTATION_PLAN.md:972` (R1): "the render was
  measurably buffer-dependent — **55 of 130 note-ons moved between 128 and 2048** (25 by ±1 sample from
  a `std::floor` on a block-relative beat difference, ~20 by 0.5–7 ms from the
  `jlimit(0, numSamples-1, …)` microtiming clamp)."
- R2 ornaments keyed to block-start bar; R3 a retrigger could beat a natural note end; R4 grid bass
  could anticipate its beat (dropped a BListen beat-1 root); R5 the first event of the timeline was
  dropped when microtiming placed it before sample 0; **R6 "No changelog entries for 0.9.68–0.9.71"**
  and the installed plugin was stale (0.9.70).
- **Deliberately still buffer-dependent after Phase 0–4**: lock onset, fill arming, transition start
  (`IMPLEMENTATION_PLAN.md:983-994`).
- **v1.0.0-rc residuals** (`IMPLEMENTATION_PLAN.md:1003-1021` and `docs/PHASE9_ACCEPTANCE.md`): full
  Record *after* lock onset still block-quantised (first mismatch ~sample 479 000); loop-wrapped Record
  128 vs 2048 differed by one kick note-off (~1.8 k samples) on the wrap flush; lock onset and
  transition start scheduled on the first block past the musical instant. "**No human miss/bug log was
  invented here**" — the human DAW pass remained outstanding.
- `docs/PHASE9_ACCEPTANCE.md` T9.4 then lists Stations A–H as "suite green" but requires a human pass
  on the installed v1.0.0 binary.

**v1.0.1 (`db31dd7`, 2026-09-11) — review of Phases 5–9, three real defects, one severe.**
- **R7 frozen-riff phase was one bar out at most buffer sizes**: `latchLockClock` used `fmod`, which
  always snaps *backwards*; a detect block starting just before a bar line moved the riff origin back a
  whole bar, so the locked riff entered on bar 2 not bar 1 — and the offset depended on host buffer
  size (128 → 0-slot offset, 512/2048 → 16-slot offset). Measured: 480 000 @128 vs 384 000 @512/2048.
- **R8 a bar-aligned DAW loop wrap re-phased the riff on every pass**: `reanchorLockClockOnJump`
  re-latched unconditionally, dropping one onset per loop and producing a mid-loop phase step
  (T2.1 coverage 31/32 → 32/32).
- **R9 onset capture collapsed at large buffers**: the per-slot end-of-note envelope was measured over
  the last quarter of the **block**, not of the **16th**; for the same 4-bar chug the capture produced
  **9 / 5 / 1** onsets at 128 / 512 / 2048 — "at 2048 the whole riff became a single 64-sixteenth note
  and the locked bass played one note". Fixed to a fixed fraction of the 16th (last 20 %): **53 / 52 /
  49** onsets, 52/52 note coverage.
- Re-measured frozen-riff bass notes rose from 13/10/6 to 57/57/54 and drum events at 512 vs 2048
  became identical.
- **Test corrections:** `getRiffAPlay{Origin}Sample()` exposed; T2.1 measures against the real
  bar-locked origin. Suites: 268 unit / 77 integration, no `[!mayfail]`.
- Residual, **pre-existing not regressions** (`CHANGELOG.md:139-143`): fill arming differs at 128
  samples (one fill instead of two), 3 of ~55 frozen-riff notes differ between 512 and 2048, the click
  note-off lands on the detecting block boundary, and the lock onset is detected on a block boundary.

---

### Era 12 — v1.0.2 → v1.0.3 (2026-09-14) — current HEAD

**`e463847` — CMake 1.0.2, "Responsiveness testing".** Two problem clusters:
1. **Editor did not fit the screen.** "a fixed 520×1100 window whose layout needed 968px, opened on a
   956px laptop desktop, and had no scrollbar of its own — so the bottom of the UI (scope,
   BPM/State/Pattern/Style readouts) was unreachable" (`CHANGELOG.md:52-56`). Fix: `ContentComponent`
   inside a vertical `Viewport`; `setScrollWheelEnabled(false)` on the section list and sliders;
   three metric tiers (full ≥751 px / scope collapsed 635–750 px / tightened below); resize floor
   900 → 460; `fitEditorToScreen()` asks 860 px but never more than `userArea` minus host chrome
   (opens at 826 px on a 922 px desktop); the same figure becomes the advertised maximum height so a
   VST3 host's `checkSizeConstraint` cannot re-open taller than the screen; `setResizable` enables the
   corner grip. Guarded by a new integration test (no scrolling at 800 px, scrolls at 460 px, nothing
   laid out past the panel bottom).
2. **UI "read as generated".** One status row replaces three widgets; outlines flattened; "CONTROLS"/
   "SECTIONS" headings removed; BPM/State/Pattern/Style collapsed to `120.0 bpm · SILENT · P23 · Open
   Chord`; copy shortened and casing unified.

**`5d5f410` — CMake 1.0.3, "Mirroring choices fix" (HEAD).** The bass mirror had **drifted back** to
sounding like a root/harmony line:
- Diagnosis (`CHANGELOG.md:7-12`): "It was not a pitch problem: the mirror was firing on every attack,
  but the authored/harmonic grid line was playing **on top of it** — in a 12-bar 8th-note test, 128
  guitar attacks produced **512** bass note-ons, three quarters of them the fixed harmony part. Because
  the bass voice is monophonic, that line kept retriggering and cutting the mirrored notes."
- Fix: `PatternPlayer` gates the grid line on the mirror's ring — a learned or mirrored note stamps
  `mirrorVoiceEndSample_` (onset + gate) and `emitBassRange` / `emitPatternBass` / `emitHarmonicBass`
  skip grid events before it; a seek or silence flushes the claim. Result: "120 notes for 128 attacks".
- Onset detection got its own **fast (0.02 s) envelope**: `EnergyAnalyser::getOnsetRmsEnergy()`
  alongside the 0.1 s structure RMS, fed to `PhraseLearner`. Rationale (`CHANGELOG.md:25-34`): "A 0.1 s
  window cannot resolve a 16th note at 120 BPM (125 ms), so the attack detector used to lean on a
  stale-state artefact: it only evaluated the envelope while the min-interval gate was open, which made
  the first block after the gate read as a rise. That fired ~4 times through a single sustained note
  (the 'bass machine-gun')." Now the envelope advances every block, a decay→rise edge is latched across
  the gate, the edge is consumed on the accepted attack, and a rise must clear the trough.
- Suites **269** unit / **79** integration all passing; perf mean 0.50 ms, p99 0.81 ms.
- **Still open, explicitly not changed** (`CHANGELOG.md:45-48`): "Play-mode drum selection rotates a
  precomputed per-section pool and only partly reacts to the guitarist, and the idle (unarmed) pattern
  readout is pinned to `P0` by design (T4.4)."

---

## 2. REPEATED REGRESSIONS — bugs fixed more than once

Every row below was fixed, regressed or was re-fixed at least twice. "Evidence" cites the specific
file:line or commit where the claim is verifiable.

### 2.1 Authored per-pattern bass lines are disconnected from the output — **three times**

| Attempt | What happened | Evidence |
|---|---|---|
| Pre-v0.9.0 (v0.8.x) | 20+ authored `MidiPattern.bassEvents` lines written but "`PatternPlayer` never reads `pattern.bassEvents`" — grep shows writes only | `MUSICALITY_ROCK_PIVOT_PLAN.md:32,34` |
| **Fix 1 — v0.9.0 A1** | `emitBassRange()` wired; "the 20+ authored bass lines that were written but never emitted (Finding 1) now play, transposed to the guitarist's tracked root" | `CHANGELOG.md:1023-1027` |
| **Regressed — v0.9.63** | `ac576f8`: "Library `bassEvents` are not mixed into the live path"; `emitBassRange` becomes declaration + definition with **no call site** | `CHANGELOG.md:361-366`; `PLAYABILITY_REVIEW.md:268-287` |
| Test enshrined it | `REQUIRE(bassNotes.count(45) == 0); REQUIRE(bassNotes.count(47) == 0);` — "library +5 must not leak" | `PLAYABILITY_REVIEW.md:289-296` (`tests/test_pattern_player.cpp:453-456`) |
| Documentation lied | `PatternPlayer.h:228-229` still claimed "A1.1: authored bass lines … play when present" | `PLAYABILITY_REVIEW.md:298` |
| **Fix 2 — v0.9.73 T5.1** | `310fc7b`: "authored bass lines play again… a pattern's `bassEvents` transpose to the live root instead of being skipped for a harmonic root drone" | `CHANGELOG.md:221-224` |
| **Fix 3 — v1.0.3** | `5d5f410`: harmony/grid bass gated behind `mirrorVoiceEndSample_` so it no longer retriggers over mirrored notes; 512 → 120 note-ons for 128 attacks | `CHANGELOG.md:5-24` |

### 2.2 Bass mirror / live-bass audibility — **at least eight separate fixes**

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.9.4–0.9.5 | (CHANGELOG written in `0f225f6`) | learner locked onto hum/noise and mirrored it endlessly; SILENT flicker | gate learner on SILENT; adaptive noise floor; raise silent cap 0.03→0.06 (`CHANGELOG.md:938-957`) |
| 0.9.6 | | Bass always low E — drop-C 65.4 Hz below the YIN band (`sr/75`) | extend estimator to ~55 Hz, accept gate 50 Hz (`CHANGELOG.md:925-935`) |
| 0.9.8 | | Bass root a major third off: offset folded onto E2=40, not C2=36; ±6 wrap pushed pc ≥ 7 down an octave; riff looped at `totalBeats + 0.5` | fold onto C2=36, return pc ∈ [0,11], bar-align the learned loop (`CHANGELOG.md:873-909`) |
| 0.9.10 | | The 0.9.8 detector `rms > prev × 1.2` "never fired on real playing" — palm-mute chugs smooth to a few-% swing over the 100 ms window | new detector: sharp rise following a recent decay (`CHANGELOG.md:819-835`) |
| 0.9.11 | | YIN confidence collapses to ≈0 on distorted palm-mute → pitch gate starved the learner; lock after 2–6 s with a skeletal 2-note pattern | gate attacks on RMS transient only, never pitch confidence; add immediate riff mirror (`CHANGELOG.md:795-817`) |
| 0.9.12 | | Sparse staccato: the lock fires on the first repeat, so the learned pattern is a 2-note slice | run the live mirror in the Locked state too (1.7 → 10–17 notes/s) (`CHANGELOG.md:771-793`) |
| 0.9.13 | | Follow-mode bass stopped mirroring once the groove lock engaged | mirror suppressed only for non-matching (solo) attacks; pattern growth to 16 notes (`CHANGELOG.md:748-769`) |
| 0.9.45 | | Locked-riff bass stuck on the recorded riff through a transition; Play played the riff note-for-note out of key | drop the hold on transition; snap to section harmony (`CHANGELOG.md:515-536`) |
| **0.9.50** | `1d36e47` | "Fix bass regression" — bass re-entry off the audible drum downbeat (clock source mismatch; comment: "the same bug class fixed for the scope playhead in v0.9.31") | one `clockSample` for every schedule + `rewindRiffToDownbeat()` |
| **0.9.54** | `a898b06` | "Still chasing bass regression" — "LOCKED RIFF DOESN'T PERSIST" (a ~2 s quiet window wiped the frozen riff mid-lock) | suppress the silence reset while `holdActive_` (`PhraseLearner.cpp:370-380`) |
| **0.9.59** | | Mirror ~one RMS-window behind the pick | quantize the mirror to the nearest 16th (`CHANGELOG.md:406-417`) |
| **0.9.63** | `ac576f8` | "unified listen bass mixer" — simultaneously fixed B/C holes and **broke the authored bass** (§2.1) | one engine, mirror first then grid root |
| **1.0.3** | `5d5f410` | Mirror buried under the fixed harmony line (512 note-ons for 128 attacks); "bass machine-gun" (~4 attacks per sustained note) | mirror owns the voice via `mirrorVoiceEndSample_`; 0.02 s onset envelope; latched edge + trough clear |

### 2.3 Bass note duration / stuck notes / retrigger-every-16th

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.8.3 | `30d7044` | Bass not immediately triggered; no fallback | immediate trigger + quarter-note fallback |
| 0.8.4 | `4f905f8` | Bass note was 0.25 beats = "16th-note blip" | 0.9-beat legato |
| 0.9.1 | | "multi-pitch bass … could leave a note stuck on: the single deferred note-off slot emitted `noteOff` for the last note played instead of the note whose note-off was due → a harsh constant drone" | monophonic `emitBassNote` closes the previous note; `bassNoteOffMidi` tracks the pending note (`CHANGELOG.md:1004-1012`) |
| 0.9.12 | | (retraction) "There is no single 0.9-beat legato on every bass path" | three different gates documented (`CHANGELOG.md:785-789`) |
| 0.9.73 | `310fc7b` | T5.2: a held chord was eight 16th retriggers | capture onset vs sustain per 16th; trigger only onsets with a 90 % gate (`CHANGELOG.md:225-228`) |
| **1.0.1** | `db31dd7` | R9: at 2048 samples "the whole riff became a single 64-sixteenth note and the locked bass played one note" (9/5/1 onsets) | tail window becomes the last 20 % of the 16th (53/52/49 onsets) |
| **1.0.3** | `5d5f410` | "bass machine-gun" — ~4 attacks per sustained note; a single pick mirrored as 2–3 notes | fast onset envelope + edge latched across the gate + `clearsFloor` trough test |

### 2.4 Drum timing / drum note-offs / choked cymbals

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.3.6 | `963ae81` | "fixed drum timing and bass blips" (no CHANGELOG entry; content is model/asset rebuild) | — |
| 0.9.68 | `3dd067a` | T1.1: `jmin(numSamples - 1, off + durSamps)` — "every drum note is released within its onset block (3–46 ms) rather than after its authored duration"; every cymbal choked; open hats inherit the closed-hat `dur = 0.25f`; "The delivered artefact changes with buffer size" | per-note deferred drum note-off table; open hats ≥ 1-beat gate (`CHANGELOG.md:301-303`; `PLAYABILITY_REVIEW.md:81-113`) |
| 0.9.72 | `7984394` | R3: "A retrigger could beat a natural note end depending on whether both landed in one block" | `scheduleDrumNoteOff` releases at the true sample when it falls at or before the re-trigger |
| 0.9.75 | `ee15307` | T7.3: pattern 21 + fill 18 "flams two kicks at beat 3.75" | fills mute the groove from window start and inherit feel (`CHANGELOG.md:189-193`) |
| **1.0.1** | `db31dd7` | Residual: "the click note-off lands on the detecting block boundary"; frozen-riff drum events 512 vs 2048 diverged (13/10/6 → 57/57/54 bass notes; drum events now identical) | bar-aligned lock origin; test corrections |

### 2.5 Tempo / BPM stability

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.3.8 | `1c02d3e` | (quick 260427-t43) tempo jitter | BPM lock-in after 8 consistent IOIs + **5-BPM grid quantization** + 80 ms refractory |
| 0.3.7-era | `1f30640` | octave error in `medianIoiBpm` (quick 260420-421) | octave fold |
| 0.3.7-era | `2a5e068` | BPM jumps | exponential smoothing on `PatternPlayer::setBpm` (α=0.1) |
| 0.4.11 | (no commit; `.planning/debug/drums-gate-tempo-regression.md`) | drums rarely/never come in; wrong by >5 BPM; "Regressed after phase 28 commits (`bbea3f8`, `ce0b006`)" | `kPlaybackConfidenceStart` 0.50→0.25; use `onsetDetector.isTempoLocked()`; **remove the 5-BPM rounding added in `1c02d3e`** |
| 0.4.12 | (`.planning/debug/phase-28-uat-failures.md`) | BPM ~20 too fast; autocorrelation normalisation `fabs(acc)/(W-lag)` biased +15–25 BPM; no octave disambiguation | octave disambiguation in `BeatTracker::recompute()` mirroring `OnsetDetector::medianIoiBpm():96-101` |
| 0.4.14–0.4.18 | (`.planning/debug/accompaniment-groove-stability.md`) | five releases in one day: 0.4.14 stopped after 1–2 s → **0.4.15 locked at 200 BPM** → **0.4.16 "No accompaniment at all; not a single hit" (self-inflicted regression from the tempo-alias fix)** → 0.4.17 playable but "tempo is sometimes wrong and changes too easily" → 0.4.18 4-BPM deadband + 2 s BPM latch |
| 0.6.4–0.6.6 | `e685806`, `101c70c`, `cc75389` | BPM not recovered after a reset; hard freeze rejected genuine changes | `TempoStabiliser::warmStart(bpm)`; soft-lock EMA α=0.03 (drift propagates over ~3–5 s); BPM save/restore through the silence-reset cascade |
| **Phase 28 UAT** | (`.planning/phases/28-*/28-UAT.md`, 2026-04-30) | "about 20bpm too fast"; beat gate never fully opens into a phrase | UAT rejected 1/4; BeatTracker **deprecated** and later deleted (`40cc331`) |
| 0.7.8 | `9d8ed74` | tempo changing | "remove tempo changing" (session/DAW tempo becomes authoritative) |
| 0.7.0 limitation | `CHANGELOG.md:1106` | "BPM control through synthetic test signals is unreliable with the current 2048-sample FFT onset detector" | accepted limitation |
| v0.9-era | `MUSICALITY_ROCK_PIVOT_PLAN.md:36` | the whole onset-detection BPM path removed | "The old onset-detection BPM path (`OnsetDetector`, `TempoStabiliser`) no longer exists in `src/analysis/`" |
| 1.0.x | `README.md:103` | — | "Tempo: the DAW is the drummer's click" — host tempo only |

### 2.6 Onset detection

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| — | `.planning/debug/*` (0.4.11/0.4.12) | flux threshold / normalisation / octave | see §2.5 |
| 0.9.10 | | `rms > prev × 1.2` "never fired on real playing … so the learner recorded ~1 attack per 6 s" | sharp rise following a recent decay (`CHANGELOG.md:819-828`) |
| 0.9.11 | | pitch-confidence gate starved the learner; YIN confidence bimodal on distorted palm-mute | gate attacks on RMS transient only (`CHANGELOG.md:799-807`) |
| 0.9.74 | `8e27a05` | T6.5: "Any RMS decrease arms the next rise (was a 3% drop), so a 200 BPM 16th pulse train records ~4 attacks/beat" | any decrease arms the rise (`CHANGELOG.md:215-217`) |
| **1.0.1** | `db31dd7` | R9: per-slot envelope measured over the last quarter of the **block**, not the 16th → 9/5/1 onsets at 128/512/2048 | last 20 % of the 16th → 53/52/49 |
| **1.0.3** | `5d5f410` | 0.1 s window cannot resolve a 125 ms 16th; detector leaned on a stale-state artefact and fired ~4×/note | dedicated 0.02 s `getOnsetRmsEnergy()`; edge latched across the gate; trough-clearing requirement |

### 2.7 Plugin UI crash / editor defects

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.9.15 | (CHANGELOG written in `0f225f6`) | "REAPER crashed when pressing the track's FX button" — `setSize()` in the constructor fires `resized()` before `sectionListEditor` exists → null deref on `getHeightHint()` | guard the pointer in `resized()`; add an `[editor]` construction smoke test "so this class of regression is caught" (`CHANGELOG.md:711-720`) |
| 0.9.17 | `b118ad2` | section-list combo boxes "drew no text" — the custom LookAndFeel's `drawComboBox` replaced the base implementation without drawing the label | draw the selected text (`CHANGELOG.md:688-698`) |
| 2026-08-19 | (`.planning/debug/reaper-vst3-ui-crash.md:29,50`) | "Historical SIGSEGV 2026-08-19 in `AccompanimentEditor::resized()`" — recorded as "the v0.9.15 class of bug" | the doc eliminates the null-deref hypothesis (`:35-37`) and warns of "a residual editor `resized()` crash once the plugin actually loads" |
| 2026-08-26 | same doc `:63-65` | VST3 editor **never opens** in REAPER — JUCE VST3 packaging rewrites `Contents/Resources/moduleinfo.json` **after** codesign, invalidating the ad-hoc seal; macOS refuses the load | re-sign install + build bundles in place; CMake `POST_BUILD` re-sign; dlopen OK; REAPER relaunch pending |
| 0.4.0 risk | `PITFALLS.md:64-83` | partial `genre` param removal crashes the editor on construction (`genreAttachment` binding a nonexistent parameter) | removed atomically across 4 files (Phase 24) |
| 1.0.2 | `e463847` | editor needed 968 px inside a 520×1100 window on a 956 px desktop with no scrollbar → bottom of UI unreachable | Viewport + metric tiers + `fitEditorToScreen()` + resize floor 900→460 |

### 2.8 ONNX model fallback (silent rule-based masquerade)

| Occurrence | Evidence | Mitigation |
|---|---|---|
| `Run()` throws; `catch(...)` "silently swallows" and falls back to rule-based inference with "no error in the log" | `PITFALLS.md:41`; repeated for opset drift `PITFALLS.md:173` | — |
| 0.8.3 rpath bug: `tryLoadModel()` fails silently when the ORT dylib path is stale | `016a5b1`; `IMPLEMENTATION_PLAN.md:67-68` | absolute dylib path |
| Phase 12 structure ONNX swallowed `Ort::Exception` | `fcb835f` "log `Ort::Exception` in `OnnxStructureInference`" | log it |
| T0.3/T0.1 in the remediation: "a silent rule-based fallback can never masquerade as working ML again"; stale installed binary (0.9.62 vs 0.9.67) | `IMPLEMENTATION_PLAN.md:96-98`, `:52-68` | ONNX-availability guard + version-string check + integration test on `getActiveInferenceName()` |
| Current code: `makeInference()` has `jassertfalse` on load failure and a comment that "a failed load used to fall through silently, so a stale ONNX Runtime dylib looked like a working ML build" | `src/AccompanimentProcessor.cpp:26-33` | jassert + integration assertion |
| ONNX error counters were only surfaced by quick task `260520-dy1` (v0.5.3), with the recorded limitation "The ONNX-specific branches were compiled only insofar as headers and disabled stubs participate in the default build" | `260520-dy1-SUMMARY.md:28` | editor error counter |
| `MA_ENABLE_ONNX` default was **OFF**; flipped **ON** only at v0.6.0 | `MUSICALITY`/`CHANGES_PLAN.md:3`; `MILESTONES.md:9`; `e48b44a` | six readiness criteria PASS |
| Two competing ML paths (`bass_model.onnx` + the proxy pipeline) | `MUSICALITY_ROCK_PIVOT_PLAN.md:206`; `PITFALLS.md:148-167` (root-echo `Y[:,1] = X[:,5]`) | `bass_model.onnx` integration/retirement decision stayed open at v0.9.0 (`MUSICALITY:4`, `:80`) |

### 2.9 DAW loop / transport-jump wedges the lock or drops bass

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.9.2 | | Frozen transport → transport-jump detector fired every block, wiping the pending pattern change (`activePatternIndex` stuck on 0) and re-firing the same bass note at block rate ("harsh constant drone") | detect a frozen host position and run an internal beat clock (`CHANGELOG.md:988-1002`) |
| 0.9.31 | | Scope playhead used the plugin's own counter while drums quantized to the DAW transport | compute the playhead from the resolved host clock (`CHANGELOG.md:538-545`) |
| 0.9.61 | `cdf4bac`-era, quick `260908-m7z` | hitting Record/Play in the DAW treated the first moving playhead sample as a seek → click/count-in state dumped, metronome left the bar grid | snap to the host timeline on frozen→rolling, ignore small blips (`CHANGELOG.md:385-395`) |
| 0.9.69 | `d0bb9c5` | "A DAW loop shorter than `lockBars` used to wedge the lock forever and silence the frozen bass for part of every pass" — the emitter forbade negative placement and expiry was measured in absolute samples | monotonic `hostSampleTime` frame, bar phase latched once at engage, seek/loop-wrap re-anchor (`CHANGELOG.md:289-297`; `PLAYABILITY_REVIEW.md:117-163`) |
| **1.0.1** | `db31dd7` | R8: "Every bar-aligned DAW loop wrap re-phased the riff" — `reanchorLockClockOnJump` re-latched unconditionally, dropping one onset per loop | a jump landing on the same bar phase is a no-op; only an off-grid seek re-latches |
| 1.0.1 residual | `CHANGELOG.md:139-143` | loop-wrapped Record 128 vs 2048 differed by one kick note-off (~1.8 k samples) on the wrap flush | pre-existing, not fixed |

### 2.10 Bass pitch / register (octave) errors

| Version | Commit | Symptom | Fix |
|---|---|---|---|
| 0.3.6→0.3.7 | `64732d7` (quick 260427) | Stability tracked in **absolute MIDI** space; YIN octave-flips reset the counter, and `setBassSemitoneOffset` "is never called"; `rounded - 40` put the bass in the wrong register | pitch-class comparison + ±6-semitone mapping |
| 0.9.6 | | Drop-C C2 = 65.4 Hz below the YIN low cap 75 Hz → no pitch ever detected → always E2 = 40 | extend band to ~55 Hz, accept gate 50 Hz, map C→36 |
| 0.9.7 | | APVTS raw choice value is the **index**; a stray normalized multiply collapsed Punk/Metal/Sludge to Metal and broke +12 | use the index directly |
| 0.9.8 | | Offset folded onto E2 = 40 instead of C2 = 36: guitar C → bass E, E → G♯, G → B₁; ±6 wrap pushed pc ≥ 7 an octave down | fold onto C2 = 36; return pc ∈ [0,11] |
| 0.9.68 | `3dd067a` | T1.4: a hard clamp collapsed C/C#/D/D# onto E1 with the ±12 transpose control | learned-bass octave fold |
| 1.0.x | `PLAYABILITY_REVIEW.md:545-568` | §2.15 "The bass octave control produces wrong pitch classes" | T1.4 / P2 item 12 |

### 2.11 Buffer-size-dependent rendering (a recurring *class*, fixed four times)

| Version | Commit | Measured defect | Fix |
|---|---|---|---|
| 0.9.68 | `3dd067a` | drum note-offs capped at the block boundary; delivered artefact changed with buffer size | deferred drum note-off table |
| 0.9.69 | `d0bb9c5` | lock/transition schedules measured in absolute host samples; buffer-dependent phase | monotonic clock + single bar-phase latch |
| **0.9.72** | `7984394` | R1: "55 of 130 note-ons moved between 128 and 2048 (25 by ±1 sample from a `std::floor` …, ~20 by 0.5–7 ms from the `jlimit(0, numSamples-1, …)` microtiming clamp)"; R2 ornaments keyed to block-start bar; R4 grid bass anticipating its beat; R5 first event dropped; R6 "No changelog entries for 0.9.68–0.9.71" | index-enumerated occurrences placed at absolute samples; per-event-bar ornaments; one-sided pocket clamp; `placeEvent` clamps to 0 only in the first block |
| **1.0.1** | `db31dd7` | R7 riff phase offset 0 slots @128 vs 16 slots @512/2048; R9 onset capture 9/5/1 | `fmod` → forward snap only when the block genuinely straddles the line; tail window = last 20 % of the 16th |
| **1.0.3** | `5d5f410` | `EnergyAnalyser` onset window had to be "fixed in time, so it does not change with the host buffer size" (`EnergyAnalyser.h`) | dedicated fixed 0.02 s onset window |
| Residual | `IMPLEMENTATION_PLAN.md:1019-1021`; `CHANGELOG.md:139-143` | lock onset + transition start still scheduled on the first block past the musical instant; fill arming differs at 128 | accepted residual |

### 2.12 Control-surface churn (added then removed, sometimes within minutes)

| Control | Added | Removed | Evidence |
|---|---|---|---|
| **Stop button** | v0.9.50, `1d36e47` (2026-09-04 14:17) | v0.9.51/0.9.52, `5fea0d8` (14:20) — "It did the same thing as Forget … did not pause or resume" | `CHANGELOG.md:495-497`, `:477-479` |
| Tempo knob (rotary BPM) | Phase 1 | v0.9.16 (`0f225f6`) | `CHANGELOG.md:703-708` |
| Bass-octave dropdown, song-form preset dropdown, Loop checkbox, RMS/Centroid/HF-flux/noise-floor readouts | Phases 7/14 | v0.9.30 (`773038a`-era) | `CHANGELOG.md:558-563` |
| Intensity slider | — | v0.8.3, `2478af3` | commit message |
| `genre` APVTS param, `variation`, `PolicyPatternMapper` | v0.2.0 Phase 14 (`7a46e6d`) | v0.4.0 Phase 24, `6387e18` | `CHANGELOG.md:1133` |

### 2.13 "Dead code that was written but never reached the live path" (recurring, ≥6 instances)

| Component | Status | Evidence |
|---|---|---|
| `emitBassRange` / `emitPatternBass` | no call site; test asserted they must not play | `PLAYABILITY_REVIEW.md:279-296` |
| `snapBassToSectionHarmony` | no call site | `PLAYABILITY_REVIEW.md:711` (removed in T8.1, `620d62b`) |
| `pickPoolPattern`, `barsPerGrooveForSection` | "never called" — the entire diversity engine unreachable in the play path | `PLAYABILITY_REVIEW.md:236-247`, `:715` (wired by T4.1, `a729e66`) |
| `diversifyPatternForGenre` / `diversifyPatternForStyle` | ran only when `playSectionIndex < 0`; in Play always ≥ 0 | `PLAYABILITY_REVIEW.md:236-240` |
| `rmsDelta` | "computed, published, never consumed" — the "react now" input | `PLAYABILITY_REVIEW.md:74-75`, `:716` |
| `setMirrorWhileHeld` / `releaseForTransition` | "no-op stubs, zero callers, still documented in CHANGELOG 0.9.59" | `PLAYABILITY_REVIEW.md:714` |
| `GrooveCommit::fillKind` / `TransitionFillKind` | set to `None`, never read | `PLAYABILITY_REVIEW.md:713` |
| `chooseTransitionFillKind` | dead; deleted in slice 4 | `jtf-PLAN.md:114` |
| `bassListenArmedUntilSample` | "never read as a mixer (dead), deleted" | `itt-PLAN.md:101` |
| `BeatTracker::pushFluxSample()` | "**never called**" in the v0.7.x prototype | `TRIAGE.md:20-35` |
| `OnnxBassInference` | "Dead code (proposals overwritten by `RuleBasedBass`)" | `SIMPLIFY.md:50-51` |
| `OnnxStructureInference` | "Broken (5-state vs 3-state index mismatch)" | `SIMPLIFY.md:50` |
| `static bool lastLoopValue` | function-local `static` mutated from the audio thread, **shared by all plugin instances** — a data race | `PLAYABILITY_REVIEW.md:717` (fixed T8.2, `620d62b`) |
| Non-atomic UI reads of lock state | UI read `riffA`/`riffB`/`enginePhase` while the audio thread wrote them → possible torn 64-slot snapshot | `PLAYABILITY_REVIEW.md:718` (fixed T8.2) |

---

## 3. "What works and must not be broken" (with evidence)

1. **The audio thread never blocks.** Lock-free queue + atomics only.
   Evidence: `PROJECT.md:102` ("Audio thread must never block — lock-free queue + atomics only");
   `ARCHITECTURE.md:424` ("Why not a mutex?"); `RETROSPECTIVE.md:132` ("Lock-free handoff (audio →
   background inference) as a non-negotiable architecture constraint");
   `rhythmic-coherence/01-CURRENT-BEHAVIOR.md:91` ("Threading model is sound; lock-free queue +
   atomics behave").
2. **Note placement is sample-exact and buffer-size invariant across 128 / 512 / 2048.**
   Evidence: `IMPLEMENTATION_PLAN.md:979-981` (`MidiProbe dual-block-size golden` asserts full
   fingerprint equality: sample, note, channel, velocity, on/off); `docs/PHASE9_ACCEPTANCE.md` T9.2;
   `CHANGELOG.md:126-130` (53/52/49 onsets, 52/52 note coverage); tests
   `tests/test_midi_probe.cpp`, `tests/test_phase9_acceptance.cpp`, `tests/test_phase1_rendering.cpp`.
   **Guardrail:** never let the lock onset or transition start drift back into note placement; those are
   state-machine timings and are the last known residuals.
3. **The DAW transport is the tempo source; the plugin does not chase BPM.**
   Evidence: `README.md:103` ("Tempo: the DAW is the drummer's click"); `ARCHITECTURE.md:102`
   ("Tempo source (DAW transport)"); `STATE.md:85` (D001 "ONNX-first inference path — rule-based is
   fallback only", session-first tempo); `MUSICALITY_ROCK_PIVOT_PLAN.md:36` (onset/tempo path removed);
   `CHANGELOG.md:1060-1068` (all BPM clamps agree on [40, 300]).
4. **ONNX load failure is loud, never a silent rule-based masquerade.**
   Evidence: `src/AccompanimentProcessor.cpp:26-33` (`jassertfalse` + integration assertion on
   `getActiveInferenceName()`); `IMPLEMENTATION_PLAN.md:96-98` (T0.3 guard); `PITFALLS.md:41,173`;
   `IMPLEMENTATION_PLAN.md:52-68` (version-string verification of the installed binary).
5. **The editor constructs and opens without crashing; the panel scrolls instead of clipping.**
   Evidence: `CHANGELOG.md:711-720` (0.9.15 fix + `[editor]` smoke test); `CHANGELOG.md:50-81`
   (1.0.2 Viewport + metric tiers + `fitEditorToScreen()` + `checkSizeConstraint`);
   `.planning/debug/reaper-vst3-ui-crash.md:63-65` (CMake `POST_BUILD` re-sign).
6. **Lock/transition schedules run on the monotonic clock with one bar-phase latch.**
   Evidence: `CHANGELOG.md:289-297` (T2.1/T2.2); commit `1d36e47` (single `clockSample` for every
   schedule; comment "the same bug class fixed for the scope playhead in v0.9.31"); `CHANGELOG.md:118-123`
   (R8 no-op re-anchor).
7. **Authored per-pattern bass lines reach the output, transposed to the live root.**
   Evidence: `CHANGELOG.md:221-224` (T5.1); test `tests/test_pattern_player.cpp` authored-interval case;
   `CHANGELOG.md:16-24` (1.0.3 gating so harmony cannot retrigger over the mirror).
8. **The mirror owns the monophonic bass voice while the guitarist is picking.**
   Evidence: `CHANGELOG.md:16-24` (`mirrorVoiceEndSample_`); `CHANGELOG.md:35-38` (per-note mirror tests:
   128 attacks → 120 notes, alternating pitch contour one note per attack); commit `5d5f410`.
9. **A locked/frozen riff stores note lengths, so a held chord is O(1) notes, not eight 16ths.**
   Evidence: `CHANGELOG.md:225-228` (T5.2 onset/sustain stamping, 90 % gate); `docs/PHASE9_ACCEPTANCE.md`
   ("frozen riff `O(1)` notes, `maxGate >= 4`"); `CHANGELOG.md:124-130` (1.0.1 onset-capture recovery).
10. **Deterministic humanisation.** Per-event hash keyed on (bar, grid16, voice, salt) — a bounce is
    reproducible and the feel does not change with block size.
    Evidence: `CHANGELOG.md:285-287` (T3.4); `IMPLEMENTATION_PLAN.md:748-750` (P2 item 11).
11. **Never weaken a test assertion to make a fix pass; never let a test enshrine a regression.**
    Evidence: `IMPLEMENTATION_PLAN.md:20-21` ("Never weaken an assertion to make a fix pass. Three
    existing tests currently encode the regressions (T0.3)"); `:86-94` (the three specific tests);
    `PLAYABILITY_REVIEW.md:690-702` ("Green tests are currently not evidence of playability");
    `.planning/debug/itt-PLAN.md:78` (a whole six-slice task had to be redone because tests were
    weakened).
12. **Test the wrap/seek/loop and the non-monotonic transport, not just the internal counter.**
    Evidence: `PLAYABILITY_REVIEW.md:175-178` ("No existing test uses a non-monotonic host clock —
    `tests/test_processor_pipeline.cpp` always drives the processor's own monotonic counter");
    `.planning/debug/play-end-style-stuck.md:40-43` (test amplitude too low so the learner never locked
    and the regression was never covered).
13. **Plugin UI text must be ASCII-only.** The bundled font cannot render em-dashes, ellipses,
    middle-dots, en-dashes or macrons.
    Evidence: `CHANGELOG.md:561-563` (0.9.30 mojibake fix); `CHANGELOG.md:477-479` (0.9.52 section
    separators); `CHANGELOG.md:469-473` (0.9.53 `Style: -`); `CHANGELOG.md:99` (the one-line status
    uses `·` in the changelog text but the shipped label is ASCII).
14. **A custom LookAndFeel must not silently drop base-class drawing.** `drawComboBox` replaced the
    base without drawing the selected text, so every combo looked empty.
    Evidence: `CHANGELOG.md:688-698` (0.9.17; also affected Genre / Bass octave / Song form).
15. **The audio thread is the only writer of display state; UI reads take a snapshot, never a lock.**
    Evidence: `CHANGELOG.md:274` (T4.4 idle display; "the audio thread is the only writer");
    `CHANGELOG.md:165-169` (T8.2 triple-buffered snapshot; `lastLoopValue` per-instance).
16. **One engine phase owns drums and bass — no independent overlapping flags.**
    Evidence: `CHANGELOG.md:377-379` (0.9.62 exclusive `EnginePhase`); quick `260909-gxy`/`i70`.
17. **Performance budget: `processBlock` p99 < 1.5 ms; ONNX `metal_groove` p99 < 5 ms.**
    Measured 0.523/0.841 ms and 0.200/0.364 ms (`docs/PHASE9_ACCEPTANCE.md` T9.5); 1.0.3 reports mean
    0.50 ms / p99 0.81 ms (`CHANGELOG.md:42-43`); 300 s stability run at BPM 120–120.
18. **The plugin is idle and silent until armed.**
    Evidence: `CHANGELOG.md:564-569` (0.9.30 explicit arm; auto-lock-by-listening removed; a new test
    asserts idle does **not** auto-lock).
19. **Pre-FX placement is the documented, recommended workflow.** The plugin sits before fuzz/amp;
    post-fuzz "won't crash. Tempo lock will be unreliable, pitch will drift, dynamics will be flat."
    Evidence: `rhythmic-coherence/02-*.md:15,62-64`; Phase 27 RHY-DOC-01/02.
20. **Combo/choice APVTS parameters use the raw index, not a normalized value.**
    Evidence: `CHANGELOG.md:919-922` (0.9.7 collapse of Punk/Metal/Sludge to Metal and broken +12).

---

## 4. Failed approaches that should not be repeated

1. **Beat tracking as a continuous in-plugin BPM chase.** Tried three times: v0.1.0 IOI median
   (`ONSET-01–04`), `BeatTracker` (autocorrelation + DP, Phase 28, `bbea3f8`/`ce0b006`), naive
   improvement of `BeatTracker` (0.4.11/0.4.12). UAT rejected Phase 28 1/4; `SIMPLIFY.md` proposed
   deleting `BeatTracker`; it was deleted in `40cc331`; `9d8ed74` "remove tempo changing"; the final
   design is host/DAW tempo with no chase (`README.md:103`). Model-dependent continuous BPM inference
   was also explicitly considered and rejected in favour of autocorrelation+DP
   (`rhythmic-coherence/03:121`).
2. **"Fix the tempo by quantizing to a 5-BPM grid."** Added in `1c02d3e` (v0.3.8) and **reverted three
   days later** because it introduced up to ±2.5 BPM error and then drift
   (`.planning/debug/drums-gate-tempo-regression.md:33,41-42`; confirmed removed in
   `phase-28-uat-failures.md:14,99`).
3. **Extreme structure hysteresis ("sludge pacing", hold times ×4–8).** Measured worst case
   "6 s (AMBIENT→SOFT) + 4 s (2-bar hold) = 10 s lag" (`TRIAGE.md:76-95`). Reverted to 3-state and
   shorter holds; the 3-state collapse was then done a second time in v0.4.0 Phase 21
   (`SIMPLIFY.md:441,863`).
4. **Structure as a 4/5-state classifier feeding a 3-state model.** `SIMPLIFY.md:50`:
   `OnnxStructureInference` "Broken (5-state vs 3-state index mismatch)". `PITFALLS.md:36-60` had
   already flagged a baked `X[4]` state enum as "the highest-risk single change in v0.4.0"; the
   silent-failure mode is `catch(...)` fallback. Do not change the enum without a coordinated contract
   bump and retrain (mitigated by `MIGRATION`-style sequencing in v0.4.0 Phases 21→22).
5. **Training the style classifier on near-breakup amp-sim audio while the plugin receives clean DI.**
   Rated CRITICAL in `TRIAGE.md:191` / `PROTOTYPE_PLAN.md:260-285`; had to be re-recorded
   ("✅ RE-RECORDED", `PROTOTYPE_PLAN.md:267`).
6. **Unconditional `stylePatternPool` selection.** On non-ONNX builds `stylePatternPool(0)` permanently
   locked drums to patterns `{1,2,7}`, so "LOUD and BREAKDOWN patterns (4, 5, 6) are never selected"
   (`TRIAGE.md:174-208`).
7. **Quantile-bin training labels.** Ordinal activity quantiles are "not a direct semantic mapping to
   pattern names" (`ML_REPORT.md:487-496`); replaced by the rule oracle in Phase 32 (`f118ef8`).
8. **Clamping the spectral-centroid std to `1e-8`.** Made "live inference effectively random"
   (`CHANGELOG.md:742-744`; `MUSICALITY_ROCK_PIVOT_PLAN.md:45,166`); required per-feature std floors
   (~1 kHz centroid, 1e-3 others) and a re-export. This is the canonical proxy/normalisation trap
   (`PITFALLS.md:116-140`: `norm_stats.json` "fit on the proxy features" then applied to audio
   features → "out-of-distribution by design").
9. **Root-echo bass model.** `Y[:, 1] = X[:, 5]`, `duration_beats` fixed 1.0, `margin` 0.0
   (`PITFALLS.md:148-167`) → no variety; later "Dead code (proposals overwritten by RuleBasedBass)".
10. **On-device ONNX vs cloud AMT as an either/or.** The cloud pivot in `v0.1.0-ROADMAP.md:158` was
    reversed; AMT/cloud inference remains out of scope at v0.7.0 (`v0.7.0-CONTEXT.md:192`).
11. **Audio-generative models (MusicGen / Riffusion / Suno).** Rejected repeatedly as "incompatible
    with `IInference` contract" — they emit waveforms, not MIDI (`v0.2.0-REQUIREMENTS.md:78`,
    `v0.1.0-ROADMAP.md:189`, `v0.4.0-REQUIREMENTS.md:60`, `ROADMAP.md:414`).
12. **Multi-tracker BPM merge as the default.** "Keep current multi-tracker merge — **rejected as
    default**" (`v0.7.0-CONTEXT.md:74`).
13. **YIN pitch tracking on distorted guitar.** "YIN unreliable on distorted guitar"
    (`SIMPLIFY.md:57`); the estimator's confidence is bimodal on palm-mute chugs
    (`CHANGELOG.md:799-803`). Pitch works only pre-FX on clean DI (`README.md:103` region;
    `rhythmic-coherence/02:25-31`).
14. **Velocity scaling as a transition device ("fade in" to the new section).** Rated wrong by
    drummers and rejected: "**Decision: fills only (option A), no velocity scaling (option B
    rejected)**" (`rhythmic-coherence/05:123,130-132`). Note: `05:164,172-174` still describes
    scaled-down first-bar velocity — an unresolved internal contradiction in that research doc.
15. **Reusing a single dB/page-wide `genre` parameter as the UI's whole navigation model.** Added
    v0.2.0, removed v0.4.0, re-added in a different form as 13 genre presets in v0.9.47 (`773038a`)
    and then removed from pattern-choice authority in Play (`e566f61`, 0.9.64: "Genre/style rewrite
    does not run in Play").
16. **Unmerged review-fix work on a side branch.** Phase 31's WR-01..WR-04 fixes landed only on
    `origin/claude/wonderful-payne-cfe667` (2026-05-03) and are not ancestors of `main`; the same
    `applyExclusion` behaviour had to be re-implemented in Phase 35. Do not park verified fixes on
    worktree branches.
17. **Marking a multi-slice task complete by weakening its tests.** `260909-gxy` "falsely marked slices
    1–6 done by weakening tests"; slice 1 had to be redone as `260909-i70`
    (`.planning/quick/260909-itt-*/260909-itt-PLAN.md:78`).

---

## 5. Version ↔ commit index (from CMakeLists, first appearance per version)

| Date | CMake | First commit | Message |
|---|---|---|---|
| 2026-04-16 | 0.1.0 | `de0304d` | phase 1.7 (also `e1bfffd` "Add version to UI", 2026-04-19) |
| 2026-04-19 | 0.3.0 | `01a7c14` | v0.3.0 |
| 2026-04-20 | 0.3.1 | `521c1d1` | bump version + version-bump rule in CLAUDE.md |
| 2026-04-20 | 0.3.2 | `d850e69` | fuzzyband rebrand + LookAndFeel |
| 2026-04-20 | (still 0.3.0) | `9090d56` | user testing |
| 2026-04-27 | 0.3.3 | `59ee3d9` | `snapToBarStart` + count-in/hold members |
| 2026-04-27 | 0.3.4 | `09a92c4` | wire count-in gate, 2-bar drum hold |
| 2026-04-27 | 0.3.5 | `27eeecb` | require beat-spaced onsets for count-in gate |
| 2026-04-27 | 0.3.6 | `963ae81` | fixed drum timing and bass blips |
| 2026-04-27 | 0.3.7 | `64732d7` | pitch-class bass root + register |
| 2026-04-28 | 0.3.8 | `d9ba044` | 3-value StructureState tests |
| 2026-04-28 | 0.4.0 | `4d946f0` | ONNX contract validation CI |
| 2026-04-28 | 0.4.1 | `6623df2` | Phase 24 plan |
| 2026-04-28 | 0.4.3 | `6387e18` | atomic genre/variation removal |
| 2026-04-29 | 0.4.4 | `d52c006` | absorb Phase 23 WIP |
| 2026-04-29 | 0.4.5 | `05f3b8b` | Phase 26-01 datasets |
| 2026-04-29 | 0.4.6 | `e2e9ea3` | retrain three ONNX heads |
| 2026-04-29 | 0.4.7 | `ce0b006` | chronological IOI ring (28 WR-02) |
| 2026-04-29 | 0.4.10 | `bbea3f8` | sorted bounded defers (28 WR-01) |
| 2026-05-03 | 0.4.18 | `31a598c` | code cleanse |
| 2026-05-13 | 0.5.0 | `4abe0b0` | Phase 33-03 baked normalization export |
| 2026-05-13 | 0.5.1 | `6199c3d` | executor worktree merge |
| 2026-05-20 | 0.5.2 | `04871ee` | docs |
| 2026-05-22 | 0.5.3 | `13b73c2` | Libraries Summary |
| 2026-06-02 | 0.6.0 | `e48b44a` | Phase 29-01 runtime rescope |
| 2026-06-02 | 0.6.1 | `1ed3e9e` | 29-02 shared groove commit |
| 2026-06-03 | 0.6.2–0.6.3 | `793e272`, `6f52994` | 29-03 directional fills; phase close |
| 2026-06-03 | 0.6.4 | `e685806` | `TempoStabiliser::warmStart` |
| 2026-06-03 | 0.6.5 | `101c70c` | soft-lock EMA α=0.03; remove hard freeze |
| 2026-06-03 | 0.6.6 | `cc75389` | BPM save/restore through silence reset |
| 2026-06-03 | 0.6.7 | `d72683f` | E2E groove-variety test |
| 2026-06-03 | 0.7.2 | `53166ea` | M002 S04 half-time LOUD routing |
| 2026-06-09 | 0.7.8 | `9d8ed74` | remove tempo changing |
| 2026-06-28 | 0.8.0 | `04dd3bc` | prototype |
| 2026-06-28 | 0.8.1 | `f6033db` | training pipeline + `MetalGrooveInference` ONNX |
| 2026-06-28 | 0.8.2 | `ea6042b` | docs + unit tests |
| 2026-06-29 | 0.8.3 | `30d7044`/`016a5b1`/`2478af3`/`6021df2`/`afe13bb` | rpath fix, C2 bass root, reactive bass, RiffMirror |
| 2026-06-29 | 0.8.4 | `4f905f8` | bass note duration 0.9-beat legato |
| 2026-08-14 | 0.8.12 | `40cc331` | clean up (delete `BeatTracker`, add `PhraseLearner.cpp`) |
| 2026-08-19 | 0.9.16 | `0f225f6` | Data Improvements P0 (**writes CHANGELOG 0.9.0–0.9.16**) |
| 2026-08-19 | 0.9.17 | `b118ad2` | Data Improvement P1 |
| 2026-08-19 | 0.9.18 | `cc7536f`, `f7d5644` | P2, P3 |
| 2026-08-19 | 0.9.19 | `90e41e8` | P4 |
| 2026-08-21 | 0.9.26 | `f54b058` | style-head training, click track, groove variety, riff-lock progress |
| 2026-08-26 | 0.9.27 | `297a796` | Debugging mode state confusion |
| 2026-08-31 | 0.9.33 | `d74edb7` | rock retrain (adds `style_cnn.onnx`; parks planning docs) |
| 2026-09-02 | 0.9.44 | `fb32a15` | Diversify outputs (`groove_renderer.onnx`, fonts, release CI) |
| 2026-09-02 | 0.9.47 | `773038a` | Add subgenres + stress test |
| 2026-09-02 | 0.9.48 | `1ac82d2` | Bug fixes + stress-test update (A-B-A-C-A) |
| 2026-09-04 | 0.9.50 | `1d36e47` | Fix bass regression (clock unify, Stop button) |
| 2026-09-04 | 0.9.51 | `5fea0d8` | Remove button |
| 2026-09-04 | 0.9.53 | `5db5028` | symbol fix (renumber to 0.9.52 + ASCII placeholder) |
| 2026-09-04 | 0.9.54 | `a898b06` | Still chasing bass regression |
| 2026-09-07 | 0.9.56 | `bb487e7` | cleanup |
| 2026-09-08 | 0.9.56 | `546ed38` | failing tests for pinned contrast slots |
| 2026-09-08 | 0.9.60 | `cdf4bac` | Fix record mode transition logic |
| 2026-09-09 | 0.9.60 → 0.9.62 | `7caacc5` (0.9.60), `cf88acd` (0.9.62) | slice 1: `EnginePhase`, `riffA` snapshot |
| 2026-09-09 | 0.9.62 → 0.9.63 | `ddfaee8` (0.9.62), `ac576f8` (0.9.63) | slice 2: unified listen bass mixer |
| 2026-09-09 | 0.9.64 | `e566f61`, `0680160` | slice 3: Play hybrid drums |
| 2026-09-09 | 0.9.64 → 0.9.65 | `6f638fc`, `85a5e62` (0.9.64); `c3bfc21`, `07375c2` (0.9.65) | slice 4: on-grid fills, crash only when armed |
| 2026-09-09 | 0.9.66 | `85a697f`, `bbf1f9e` | slice 5: Record B contrast + freeze |
| 2026-09-09 | 0.9.67 | `d90bc3e`, `d68c9d5` | slice 6: GrooveRenderer disconnected |
| 2026-09-10 | 0.9.67 | `5385856` | Musicality Phase 0 (review + remediation plan) |
| 2026-09-10 | 0.9.68–0.9.71 | `3dd067a`, `d0bb9c5`, `7ab05a9`, `a729e66` | Phases 1–4 |
| 2026-09-10 | 0.9.72 | `7984394` | review of Phases 0–4 (R1–R6) |
| 2026-09-11 | 0.9.73–0.9.76 | `310fc7b`, `8e27a05`, `ee15307`, `620d62b` | Phases 5–8 |
| 2026-09-11 | 1.0.0 | `1217d70` | Phase 9 acceptance |
| 2026-09-11 | 1.0.1 | `db31dd7` | review of Phases 5–9 (R7–R9) |
| 2026-09-14 | 1.0.2 | `e463847` | Responsiveness testing (editor fits screen, UI de-generated) |
| 2026-09-14 | 1.0.3 | `5d5f410` | **HEAD** — Mirroring choices fix |

---

## 6. Where sources disagree, or evidence is missing

### 6.1 Unreconciled contradictions

| # | Contradiction | Sources |
|---|---|---|
| 1 | `STAB-02` (CPU < 15% @ 256 samples) marked **fail 2026-04-16** vs marked **complete** | `.MDignore/Phase 1 TODO.md:75` vs `.planning/milestones/v0.1.0-REQUIREMENTS.md:57` (both cite `07-05-CPU-PROFILE.md`) |
| 2 | CHANGELOG `[1.0.0-rc]` heading vs CMake `VERSION 1.0.0` | `CHANGELOG.md:148` vs `1217d70:CMakeLists.txt:4` |
| 3 | `CHANGELOG.md:1112` says "Plugin version 0.5.6 in `CMakeLists.txt`" inside the `[0.6.7]` block, while the file's top entries are 1.0.x | `CHANGELOG.md:1110-1112` |
| 4 | Milestone label `M001` vs commit messages `M002 S01-S03` / `M002 S04` | `STATE.md:3-4` vs commits `a1150a8`, `53166ea` |
| 5 | `STATE.md` still says "Phase: S01 (tempo-stability) — ready to execute", all slices `pending`, while `CHANGELOG.md [0.7.0]` documents S01–S04 as implemented | `STATE.md:36-47` vs `CHANGELOG.md:1077-1108` |
| 6 | `docs/MUSICALITY_ROCK_PIVOT_PLAN.md:5` says "Date: **2025**" while its own status line says v0.9.0 and every other artifact is 2026 | `docs/MUSICALITY_ROCK_PIVOT_PLAN.md:4-5` |
| 7 | `README.md:1` says "fuzzyband … — **v0.9.29**" at HEAD 1.0.3 | `README.md:1` vs `CMakeLists.txt:4` |
| 8 | `CHANGELOG.md` entry numbers lag CMake versions repeatedly (e.g. `0.9.45` introduced by `773038a` which is CMake `0.9.47`; `0.9.29`–`0.9.31` introduced by `d74edb7` which is CMake `0.9.33`) | `git log --format` + git version map (§5) |
| 9 | `rhythmic-coherence/05-COORDINATION-AND-CONTEXT.md` both rejects velocity scaling (`:123`, `:194`) and describes scaled-down first-bar velocity (`:164`, `:172-174`) | that file |
| 10 | `drums-gate-tempo-regression.md` and `phase-28-uat-failures.md` are dated 2026-04-30 but their fixes are versioned 0.4.11/0.4.12, while CMake at the time was already **0.4.10** (`bbea3f8`, 2026-04-29) and jumped to **0.4.18** by 2026-05-03 — versions 0.4.11–0.4.17 have **no commits on `main`** | debug docs vs commit version map |
| 11 | The `accompaniment-groove-stability.md` evidence block is not chronological (15:49 entries appear after 16:06 entries) | `.planning/debug/accompaniment-groove-stability.md:73-108` |
| 12 | `Phase 1 TODO.md` records a `0.3.6→0.3.7` bump for the bass-pitch-class quick task, but the same day also has `0.3.4` (t42) and `0.3.8` (t43) — the ordering is unclear | `260427-bass-pitch-class-tracking/PLAN.md`, `260427-t42-*`, `260427-t43-*` |
| 13 | Phase 35 and 36 are marked complete in `ROADMAP.md:265-266` (2026-05-25 / 2026-06-02) but have **no phase-numbered commits** and no `v0.5.4`/`v0.5.5` CMake steps | `ROADMAP.md` vs `git log` |
| 14 | The `.MDignore` location implies `CHANGES_PLAN.md` / `ML_REPORT.md` / `PROTOTYPE_PLAN.md` / `TRIAGE.md` / `SIMPLIFY.md` are deliberately excluded from repo context, yet they are the only record of the v0.7.x/v0.8.0 prototype era | `d74edb7` diff; `.MDignore/` |

### 6.2 Unclear / unverified

- **What `0.8.5`–`0.8.11` were.** No commits exist between `4f905f8` (2026-06-29, `0.8.4`) and
  `40cc331` (2026-08-14, `0.8.12`). **Unclear.**
- **What `0.9.20`–`0.9.25`, `0.9.28`, `0.9.32`, `0.9.34`–`0.9.43`, `0.9.46`, `0.9.49`–`0.9.50`,
  `0.9.52`, `0.9.55`, `0.9.57`–`0.9.59` were.** Several are referenced in the CHANGELOG narrative but
  the CMake versions either jump over them or the numbers were re-assigned after the fact (see the
  `5db5028` renumbering of 0.9.51 → 0.9.52). **Unclear which build numbers existed.**
- **Whether v0.5.0 ever shipped.** No `v0.5.0-ROADMAP.md`, no close artifact, requirements unchecked —
  yet `ML_REPORT.md:3` labels the codebase "v0.5.0" on 2026-05-11 and `v0.7.0-CONTEXT.md:244` refers to
  a "baseline v0.5.3 recording". **Unclear.**
- **Whether v0.7.0 phases 37–40 were ever planned or executed.** `ROADMAP.md:11` says "planning";
  `.planning/milestones/v0.7.0-CONTEXT.md` status is "Ready for roadmap / phase planning". No 37–40
  directories exist. **Unclear / apparently never planned.**
- **Whether `SIMPLIFY.md` (v0.8.0, 2026-06-28) was executed.** `BeatTracker` deletion and the 3-state
  revert happened; `PitchEstimator`/`StablePitchTracker` deletion did not. **Partially executed.**
- **The provenance of `docs/PLAYABILITY_REVIEW.md`.** The review states it was "read-only — no source
  files were modified" and that "the review doc lives in the Desktop docs folder"
  (`IMPLEMENTATION_PLAN.md:5`), yet a copy is committed in `5385856`. Whether the two are identical is
  **unclear**.
- **`v0.9.51` vs `v0.9.52`/`v0.9.53`.** `5fea0d8` added a `[0.9.51]` entry; `5db5028` rewrote the top
  heading to `[0.9.53]` and moved the previous text to `[0.9.52]`. No `0.9.51` build is documented.
- **Test-suite counts** are inconsistent across documents (e.g. 227+56 in the review,
  268+77 at 1.0.0-rc, 269+79 at 1.0.3) — these are snapshots, not contradictions, but should not be
  compared as a trend without the version.
- **`docs/MUSICALITY_ROCK_PIVOT_PLAN.md` "Date: 2025"** is almost certainly a typo but is
  **unreconciled**.

### 6.3 CHANGELOG structural defects (verified)

- **Missing heading** for the 0.9.26 block: `CHANGELOG.md:610-650` sits directly under the 0.9.29
  section and ends "v0.9.26 (versioned build per workflow)."
- **Unnumbered section** "Data Improvement P3" at `CHANGELOG.md:652` (explicitly says "no version bump",
  but CMake did bump to `0.9.18` in the same batch).
- **Versions 0.9.0–0.9.16 are retrospective** (all introduced by `0f225f6`, 2026-08-19).
- **0.9.17 entry content** ("Slimmer editor UI") does not match its commit message ("Data Improvement
  P1"), and vice versa for `0f225f6` ("Data Improvements P0" writes the 0.9.0–0.9.16 editor/bass
  narrative).

---

## 7. Compact era summary

| Era | Dates | Plugin versions | Goal | Headline failure / revert |
|---|---|---|---|---|
| 0 Pre-history | undated → 2026-04-16 | none | Rule-based MVP plan, `IInference` seam | STAB-02 CPU fail unreconciled with the "complete" checkbox |
| 1 v0.1.0 | 2026-04-16 → 04-17 | 0.1.0 | Rule-based realtime guitar → MIDI | Tests written but never registered in CMake |
| 2 v0.2.0 | 2026-04-16 → 04-17 | (no 0.2.x) | ONNX optional, pitch, ML structure, generative bass, Terraform | Cloud/AMT pivot reversed; phase-numbering collision; ONNX silent-fallback hazard |
| 3 v0.3.0 | 2026-04-17 → 04-20 | 0.3.0 | Real GMD pipeline + PatternNet/BassNet | TFDS/protobuf fights; BatchNorm at batch 1; root-echo bass scaffold |
| 4 v0.4.0 | 2026-04-28 → 04-29 | 0.3.8 → 0.4.10 | 3-class structure, ONNX contracts, Lakh merge, genre removal | Genre removal high-risk (guarded); milestone-close process gaps |
| 5 v0.5.0 + 31 | 2026-04-29 → 06-03 | 0.4.11 → 0.5.3, 0.6.0–0.6.3 | Rhythmic coherence; architecture deepening | **Phase 28 UAT 1/4**; three tempo fix rounds; 0.4.16 self-inflicted no-accompaniment regression; Phase 30 superseded; Phase 31 review fixes left unmerged |
| 6 v0.6.0 | 2026-05-11 → 06-02 | 0.5.0 → 0.6.0 | Label correction, quality gates, domain gap, ONNX default ON | Audit first found "No Phase 33 or 36 verification was run", unverified gates, stale version artifacts |
| 7 Creative Companion | 2026-06-02 → 06-09 | 0.6.4 → 0.6.7, 0.7.2, 0.7.8 | Stable tempo/structure, groove vocabulary | M001/M002 label mismatch; synthetic-signal BPM unreliable (accepted) |
| 8 v0.8.x prototype | 2026-06-28 → 06-29 | 0.8.0 → 0.8.4 | Unified mel-CNN + RiffMirror | Wrong-signal style classifier; BeatTracker never called; 10 s structure lag; ORT rpath → silent fallback |
| 9 v0.8.12–0.9.44 | 2026-08-14 → 09-02 | 0.8.12 → 0.9.48 | Data honesty, subgenres, rock retrain | CHANGELOG 0.9.0–0.9.16 written retrospectively; missing headings; `1e-8` centroid std still random |
| 10 0.9.48–0.9.67 | 2026-09-02 → 09-09 | 0.9.48 → 0.9.67 | Stress-test-driven fixes; 6 wiring-repair slices | `260909-gxy` false pass (tests weakened); Stop added then removed in 3 minutes; `cdf4bac` deleted reactivity triggers; stale installed binary |
| 11 0.9.67 → 1.0.0-rc/1.0.1 | 2026-09-10 → 09-11 | 0.9.67 → 1.0.1 | Playability Review remediation (23 defects) | T9.2 unmet (55/130 notes moved); R1–R6 and R7–R9; 3 tests had enshrined regressions |
| 12 1.0.2–1.0.3 | 2026-09-14 | 1.0.2, 1.0.3 | Editor fits screen; mirror owns the bass voice | Mirror buried by harmony line (512 vs 128 note-ons); machine-gun bass; Play drum reactivity + idle P0 still open |

---

*Compiled from the repository at HEAD `5d5f410` (2026-09-14, CMake `VERSION 1.0.3`) on a clean working
tree. Every non-obvious claim above is traceable to a file:line or a commit hash; nothing has been
invented. Items that could not be established are marked **unclear** in §6.*
