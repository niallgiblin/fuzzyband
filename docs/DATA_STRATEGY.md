# Data Improvement Strategy — Metal Accompaniment

**Status:** Planning memo (major plan). Supersedes the data-strategy portions of
`docs/MUSICALITY_ROCK_PIVOT_PLAN.md` §5 (C1–C6) and `docs/HANDOFF_PLAN.md` §3.
**Snapshot:** v0.9.14 (`CMakeLists.txt:4`).
**Production model decision (confirmed):** the **22-class mel-CNN** (`assets/metal_groove.onnx`)
is the single production inference path. The legacy scalar/oracle model
(`assets/accompaniment_model.onnx`) is **retired**.

---

## 0. TL;DR

The plugin's data/ML weaknesses are not primarily "not enough data." They are:
**circular labels**, a **half-fixed normalization bug**, **broken validation**, **two
competing ML stacks**, and **repo/test hygiene** problems that have repeatedly produced
"all green but broken" regressions. This strategy fixes correctness and provenance
**first**, retires the dead pipeline, and only then improves data — with the human
labeling effort deferred to the very last step and scoped to what a guitarist can
*honestly* label.

### The labeling insight that shapes everything

Split perception from arrangement:

| Layer | Learns | Label source | Why |
|---|---|---|---|
| **Perception** | guitar audio → playing-style / intensity (palm-mute, open-chord, single-note, sustain, silence; soft/loud) | **Your honest self-labels** on real captures | The audio physically contains this evidence; you can truthfully tag it. This is the circularity break, and it is in-domain to your gear/tone. |
| **Arrangement / selection** | style + intensity + section → drum/bass pattern (incl. verse/chorus/breakdown feel) | **Robust external datasets** (E-GMD, Lakh subsets, DadaGP) + energy-based `StructureTagger` | Section identity is *not* determinable from a 370 ms guitar window — the same riff can be a verse or a chorus. Self-labeling it would be circular and unlearnable. |

**You self-label only what your playing actually determines (articulation/dynamics).
Structural/arrangement knowledge comes from real datasets, never from self-annotation.**

### The two runtime modes (both must be served)

One engine, two user-facing modes — the data strategy has to improve both:

- **Follow / listen mode** (`playOn == false`): *reactive*. Audio-thread analysis + the
  mel-CNN pick patterns from your playing; `PhraseLearner` detects a repeated riff and the
  **groove lock** freezes drums + bass onto it for a set number of measures (the `lockBars`
  slider), then transitions out and re-locks on whatever you play next. Leans hardest on the
  **perception layer** (articulation/intensity of *your* playing).
- **Play mode** (`playOn == true`): *scripted*. `StructureSequencer` plays a whole song form
  (INTRO/VERSE/CHORUS/BREAKDOWN/…), drawing patterns from per-section pools with fills/crashes
  at boundaries. Leans hardest on the **arrangement layer** (section → pattern-pool priors).

How the layers/datasets map onto the modes:

| Data work | Follow mode | Play mode |
|---|---|---|
| **Perception self-labels** (§5.2) + **C3** articulation | Primary — drives reactive selection + what the riff-lock latches onto | Secondary — refines section feel |
| **C1** groove feel (E-GMD) | Both — every emitted pattern is humanized the same way | Both |
| **C2** selection/section priors (Lakh) | Section priors sharpen reactive transitions | Primary — fills the per-section pools + typical section orderings |
| **C4** generative (gated) | Could generate a groove that tracks your riff's accents instead of picking the nearest loop | Could generate section-appropriate variation |

---

## 1. Issues this strategy addresses

Derived from a full source review (three explorations of the ML/data pipeline, the
real-time engine, and build/repo hygiene).

| # | Issue | Severity | Resolved in |
|---|---|---|---|
| 1 | **Circular labeling** — legacy labels are a Python port of `PatternRules::rulePatternForState()` (`training/build_dataset.py:98-158`, passed through by `merge_datasets.py:204-209`). Model can never exceed the rulebook. | High | Phase 1 (root-removed) + Phase 3 (honest self-labels) |
| 2 | **Half-fixed normalization** — centroid-std collapse fixed in code but shipped `training/data/processed/norm_stats.json:19` still has `spectralCentroid` std = `1e-08`; the bundled legacy ONNX still bakes it in. | Critical (legacy only) | Phase 1 (legacy retired ⇒ bug gone) |
| 3 | **Broken validation / dead classes** — legacy val had 4/7 classes with zero examples; mel-CNN marks *everything* `split:"train"` (`build_mel_groove_dataset.py:216`), so reported ~0.96 F1 is train-on-train. | High | Phase 3.1 |
| 4 | **Two parallel ML stacks** — mel-CNN (prod) + legacy scalar (fallback), plus orphaned `train_bass.py`/`train_structure.py`, unused `groove_embedding` output, randomly-initialized style head. | High (maintenance) | Phase 1 |
| 5 | **Tiny corpus + unused captures** — ~60 WAVs / 302 MB feed 22 classes (2–3 clips/class, heavy augmentation ⇒ overfit); 13k+ `FeatureCapture` frames exist but are **evaluation-only**. | High (generalization) | Phase 3.3–3.5 + Phase 5 |
| 6 | **Committed build artifacts** — `.gitignore` lists `build-onnx/` but 31 files are tracked, incl. a ~13 MB test binary and a standalone `.app`. | Medium | Phase 0.1 |
| 7 | **Test integrity gaps** — `test_feature_capture.cpp` and `test_structure_shadow_integration.cpp` unregistered (latter has a broken `test/` vs `tests/` fixture path + missing WAV); golden fixtures untracked in git. | Medium (recurring) | Phase 0.2 + Phase 2.2–2.3 |
| 8 | **Build-flag inconsistency** — `MA_ENABLE_ONNX` defaults **ON** (`CMakeLists.txt:16`) but CI (`ci.yml:35`) and `CONTRIBUTING.md:42` assume **OFF**. | Medium | Phase 0.3 |
| 9 | **Doc drift** — version truth is 0.9.14; README says 0.9.7, HANDOFF 0.9.13, CLAUDE/AGENTS 0.3.1; ARCHITECTURE references removed `OnsetDetector`. | Low | Phase 0.4 |
| 10 | **RT smell + dead code** — `melScratch.resize(22050)` can heap-alloc on the audio thread (`AccompanimentProcessor.cpp:414`); dead `State::Confirming`, `GrooveCommit` bass fields. (`FeatureVector.subBassRatio` is *not* dead — the signal is computed and used live by `StructureTagger`, but the copy on `FeatureVector` is never populated before hand-off.) | Medium | Phase 1.4 + Phase 2.1 |

---

## 2. Phase 0 — Repo hygiene & truth-in-docs *(fixes #6, #8, #9)*

Cheap, mechanical, high-leverage. Do first so all later work lands on a clean,
reproducible base with quiet diffs.

1. **Untrack committed build artifacts** (#6): `git rm -r --cached build-onnx/`. Files
   remain on disk (still gitignored); they just leave the index. Kills artifact noise
   in every diff.
2. **Commit the golden fixtures** (#7): `tests/fixtures/*.wav` + `tests/fixtures/README.md`
   provenance, so CI/clones can actually run the golden-signal tests.
3. **Resolve the ONNX build-flag inconsistency** (#8): keep `MA_ENABLE_ONNX=ON`
   (production is ONNX); update `.github/workflows/ci.yml` to provide `ONNXRUNTIME_ROOT`
   and fix `CONTRIBUTING.md` to match. Verify CI configures cleanly.
4. **Doc-drift sweep** (#9): README 0.9.7→0.9.14; update `CLAUDE.md`/`AGENTS.md`
   version-bump rule (0.3.1→current) and stale "v0.6.0" milestone; remove `OnsetDetector`
   references from `ARCHITECTURE.md`.

## 3. Phase 1 — Retire the legacy ML pipeline *(fixes #4; also removes #1 & #2 at the root)*

Make the mel-CNN the sole production model; delete the parallel stack so no data work
targets dead code.

1. **Remove the legacy fallback** in `makeInference()` (`AccompanimentProcessor.cpp:17-30`):
   chain becomes mel-CNN → `RuleBasedInference` only.
2. **Delete legacy assets & code**: `assets/accompaniment_model.onnx`,
   `src/inference/OnnxInference.{h,cpp}`, its CMake bundling + `test_onnx_inference`
   load test, and orphaned trainers `training/train_bass.py`, `train_structure.py`,
   `models/bass_model.py`, `models/structure_model.py`.
3. **Quarantine the oracle-label MIDI pipeline** under `training/legacy/` with a README
   marking it retired: `build_dataset.py`, `build_lakh_dataset.py`, `train_gmd.py`,
   `merge_datasets.py`, `models/pattern_model.py`, `norm_stats.json`. This is where the
   **circular labeling (#1)** and the **broken `norm_stats.json` (#2)** live — retiring
   the path resolves both at the root instead of patching them. (The mel path never had
   the centroid-std bug; it normalizes per-window in `build_mel_groove_dataset.py:142-146`,
   matching the C++ extractor.)
4. **Prune dead runtime paths + wire `subBassRatio`** (#10 partial): remove
   `MetalGrooveInference`'s unused `groove_embedding` plumbing,
   `PhraseLearner::State::Confirming`, and the `GrooveCommit` generative-bass fields.
   **Resolved:** populate `FeatureVector.subBassRatio` before hand-off (it's already
   computed by `EnergyAnalyser` and used live by `StructureTagger`) — it is the palm-mute
   vs open-chord signal the perception layer (§5.2) depends on, so it must reach any
   feature consumer, not just the audio-thread tagger.

## 4. Phase 2 — RT-safety & test integrity *(fixes #7, #10 remainder)*

1. **Move `melScratch` allocation to `prepareToPlay`** (#10): pre-size the 22050-sample
   buffer so the first mel window can't heap-allocate on the audio thread.
2. **Register or remove orphan tests** (#7): wire `tests/test_feature_capture.cpp` into the
   unit target (link `src/capture/FeatureCapture.cpp`); fix or delete
   `tests/test_structure_shadow_integration.cpp` (broken fixture path + missing WAV). This
   closes the exact "all-green-but-broken" trap the CHANGELOG keeps re-learning.
3. **Add a CMake orphan-test guard** (recommended): configure-time check that every
   `tests/*.cpp` is referenced by a target; fail loudly on the next orphan.

## 5. Phase 3 — Honest data foundation *(fixes #3, #5; the core of the strategy)*

All tooling here is built and smoke-tested against the **existing** ~60 WAVs, so it is
proven end-to-end before you record anything new.

### 5.1 Fix validation methodology (#3)
`build_mel_groove_dataset.py` marks everything `split:"train"` (line 216) — there is no
held-out set, so ~0.96 F1 is train-on-train. Change to:
- **Grouped split by source recording** — augmented variants of a take must never straddle
  train/val (prevents leakage that inflates F1).
- **Every class present in val**; emit honest macro-F1 + per-class confusion matrix.
- Fail the training quality gate on any dead (zero-recall) class.

### 5.2 Two-layer label taxonomy (the confirmed decomposition)
- **Perception (self-labeled):** `palm_mute`, `open_chord`, `single_note`, `sustain`,
  `silence` (+ intensity `soft`/`loud`, already derivable from the energy analyser). These
  are physically grounded in your playing.
- **Arrangement (dataset/rule-sourced, NOT self-labeled):** section identity
  (verse/chorus/breakdown), groove feel, pattern selection. Sourced from robust datasets
  (§5.6) and the energy-based `StructureTagger` over time.
- Document the explicit `style + intensity + section → pattern-pool` mapping (reuse
  `src/inference/pattern_rules.h` pools) so the two layers compose transparently.

### 5.3 Capture → training bridge (#5)
The 13k+ `FeatureCapture` frames are currently evaluation-only. Build the bridge that turns
your annotated real playing into **mel training data** (not the retired scalar path):
- Reframe/replace `evaluate_feature_capture.py` (currently hard-wired to the legacy 7-class
  `PATTERN_NAMES` + `_rule_pattern_for_state`) around the new perception taxonomy.
- Captures record audio-aligned timestamps; pair with the annotation CSV (§5.4) to slice
  labeled audio.

### 5.4 Annotation + slicing tooling
- Simple annotation format: `start_seconds,end_seconds,label` over a recorded take.
- A slicing script that ingests `WAV + CSV` and emits per-class clips under
  `data/raw/<style_label>/`, ready for `build_mel_groove_dataset.py`.
- Seed and smoke-test against audio already in `data/raw/`.

### 5.5 Data provenance / versioning (#5 loose end)
Settle the messy layout (tracked `data/processed/`, gitignored `training/data/`,
uncommitted fixtures) into a documented convention:
- `data/MANIFEST` describing each source, its license, and the exact regenerate command.
- Clear split: raw audio (committed if small / documented if large), processed tensors
  (gitignored, regenerable), fixtures (committed).

### 5.6 Robust datasets → see Phase 4
The arrangement layer is fed by external datasets (E-GMD, Lakh, DadaGP, Slakh/MoisesDB).
These are substantial, self-contained efforts, so they are broken out into **Phase 4**
below. Phase 3 only needs to define the interfaces they plug into (the `GrooveTemplate.h`
struct for C1, the pattern-pool/selection tables for C2, the perception taxonomy for C3,
and the multitrack supervision format for C4).

## 6. Phase 4 — Robust dataset integration (arrangement layer) *(C1–C3 DONE; C4 gated)*

**Status (Phase 4 complete for C1–C3):** implemented against the cached datasets.
- **C1 ✅** `training/build_groove_template.py` → `data/groove_templates.json` +
  generated `src/midi/GrooveTemplateData.h`, baked into `Groove::rock/metal/punk`
  (204 GMD rock takes, 115k hits; metal/punk are documented tightening transforms).
- **C2 ✅** `training/download_msd_genre.py` + `training/build_lakh_priors.py` →
  `data/lakh_priors.json` + generated `src/inference/PatternPriors.h`, wired into
  `pattern_rules.h` via `orderPoolByPriors` / `orderedSectionPatternPoolForGenre`
  (additive, build-time; RT selection path unchanged). Content-derived tempo.
- **C3 ✅ (tooling)** `training/build_dadagp_articulation.py` maps DadaGP articulation
  tokens onto the perception taxonomy → `data/dadagp_articulation.json`. DadaGP is
  access-gated, so the extractor ships with a synthetic-fixture test; run it against
  a local token set (`--tokens-dir`) to produce the corroborating prior.
- **C4 ⏸ gated** — untouched (§6.4).

These datasets supply what you *cannot* honestly self-label: how patterns are *played*
(groove feel), which patterns/sections co-occur (selection priors), and riff/articulation
grammar. **Decision (confirmed): adopt C1–C3 now. The project stays on *selection +
humanized feel* — it does NOT commit to a generative end-state.** C4 (generative
supervision) is deferred to an **optional, gated milestone** (§6.4): only explored if, after
C1–C3 + Phase 3, an A/B shows the drums still don't track your rhythm well enough. The stable
`IInference` interface means generation can be added later with no rework. Verify each
license before redistribution (`docs/MUSICALITY_ROCK_PIVOT_PLAN.md` §C6 checklist). C1–C3
need no user input; they run before Phase 5.

Priority order = C1 → C2 → C3 (drum feel first, then selection, then articulation).
C4 is out of the committed scope — see the gate in §6.4.

### 6.1 C1 — E-GMD (Expanded Groove MIDI) → data-derived groove template
- **Source:** ~444 h audio / ~43 h aligned drum MIDI (human + synthesized). GMD is CC-BY 4.0;
  confirm E-GMD terms on the Magenta/Zenodo page.
- **Use:** learn per-grid-cell **velocity hierarchy** and **microtiming distributions**,
  and bake them into `src/midi/GrooveTemplate.h` — replacing the hand-authored
  `rock()/metal()/punk()` templates with statistics from real human drumming.
- **Deliverable:** an offline script that reduces E-GMD to per-genre groove-template structs
  (velocity multipliers + timing offsets per 16th) + a regeneration entry in `data/MANIFEST`.
- **Guardrail:** output must remain a *fixed-size* template struct consumed on the audio
  thread — no model inference added to the RT path.

### 6.2 C2 — Lakh MIDI genre subsets → selection & section priors
- **Source:** `lmd_matched` ≈ 45k files with Million Song Dataset genre tags (CC-BY via LMD).
- **Use:** replace blind channel-10 + header-BPM filtering with **genre-weighted rock
  subsets** (MSD tags: rock, alternative, punk, metal, hard-rock). Derive which
  patterns/sections co-occur and typical section orderings → priors for the arrangement
  layer and `StructureSequencer` pools.
- **Fix:** use **content-derived tempo**, not header BPM (unreliable — flagged in the prior
  audit).
- **Deliverable:** a rock-weighted subset builder + extracted selection/section-transition
  priors, wired into `src/inference/pattern_rules.h` pool weighting (not the RT path — this
  informs the pools/tables at build time).

### 6.3 C3 — DadaGP → riff/articulation grammar (feeds the perception taxonomy)
- **Source:** 26k GuitarPro songs, rock/metal-heavy (confirm CC-BY 4.0). Optional complement:
  ProgGP (173 prog-metal tabs) for a stronger metal-preset story.
- **Use:** **not** for accompaniment directly — for learning palm-mute vs open-chord vs
  single-note **articulation and riff rhythm** grammar. This is the symbolic reference that
  corroborates and augments the human-labeled perception taxonomy (§5.2), improving
  palm-mute/open-chord discrimination where distortion collapses the centroid.
- **Deliverable:** an articulation-token extraction pass + a mapping onto the perception
  label set; used to augment/validate the perception classifier, never as the sole label
  source (keep human labels authoritative for real audio).

### 6.4 C4 — Slakh2100 / MoisesDB → generative accompaniment *(GATED — not in committed scope)*
**Deferred.** Do not start this unless the gate below is met. Selection + humanized feel
(C1–C3) already makes the engine adaptive — it varies *how* patterns are played and (in
Follow mode) mirrors your riff on bass. C4 only changes *what* drum rhythm is produced, is
the largest and riskiest build, and is hard to evaluate (no ground-truth "correct" generated
groove). Rationale for deferring is in the chat discussion and `MUSICALITY_ROCK_PIVOT_PLAN.md`
P3 ("architectural bet; only after P0–P2 validate").

- **Gate to open C4:** after C1–C3 + Phase 3 ship, run a blind A/B on real playing. Only if
  the drums still feel like they *pick the nearest loop* rather than *respond to your
  rhythm/accents* is generation worth pursuing. Otherwise C4 stays closed.
- **If opened — source:** Slakh2100 = 2,100 synthesized multitracks with clean stems (confirm
  terms); MoisesDB = 45 real multitracks, 12 genres (research/non-commercial likely — check
  before shipping). The only route to simultaneous drum + bass + guitar supervision.
- **If opened — approach:** start with a single-groove generative **spike** behind
  `IInference`; it must satisfy the RT budget (<30 ms, <15% CPU) and win a musical A/B before
  any wider commitment. Never replace the curated library wholesale on the first pass.

## 7. Phase 5 — Your step: record & self-label *(the only human-effort task, last)*

After Phases 0–4 land, the pipeline is complete and proven on existing audio + external
datasets. You then:
1. Record 30–60 min of real playing, labeling only **articulation/dynamics** (what your
   playing honestly determines) — either as folder-organized clips or longer takes + a
   `start,end,label` CSV.
2. Run one command: dataset build → train → **honest** validate → export centroids/ONNX.
3. Review the per-class confusion matrix; record a bit more of any weak class and re-run.

The arrangement layer (section feel, pattern selection) is *already* handled by the rules +
Phase 4 datasets across both modes, so your labeling stays scoped to what is truthful and
learnable — which is exactly what breaks the circularity for the perception layer.

---

## 8. Sequencing rationale

Phase 0 is cheap and unblocks clean diffs/CI. Phase 1 makes issues **#1 and #2 vanish**
(their only home is the retired legacy path) and shrinks surface area before any data work.
Phase 2 hardens the test safety-net so later changes are trustworthy. Phase 3 is the
perception payload: honest validation + capture wiring + labeling tooling, all testable on
existing audio, with the two-layer taxonomy keeping self-labels truthful. Phase 4 integrates
the robust external datasets (C1→C3) for the arrangement layer — code+data work that needs
no user input — while keeping the engine on selection + humanized feel and leaving generation
(C4) gated. Phase 5 (your recording/self-labeling) is the only step that needs you, and it is
last.

## 9. Open decisions

1. **~~`subBassRatio`~~** — *Resolved:* wire through to inference (§3, Phase 1.4).
2. **~~Robust dataset scope~~** — *Resolved:* adopt **C1–C3** (§6). Engine stays on
   selection + humanized feel; **no generative-end-state commitment**.
3. **~~Generative (C4)~~** — *Resolved:* **deferred/gated** (§6.4). Revisit only if a blind
   A/B after C1–C3 + Phase 3 shows the drums still don't track your rhythm.

## 10. Success criteria

- **Correctness:** single production ML path; no baked-normalization bug anywhere; every
  `tests/*.cpp` registered; CI configures and runs golden tests on a clean clone.
- **Honest metrics:** grouped-split macro-F1 on a held-out set with **all** perception
  classes non-zero (no dead classes); reported number is not train-on-train.
- **Circularity broken:** perception model trained on human self-labeled real audio, not on
  `rulePatternForState()` output.
- **Groove realism (C1):** `GrooveTemplate.h` values are E-GMD-derived; blind A/B favors the
  data-derived feel over the hand-authored one.
- **Selection in-distribution (C2):** pattern/section priors come from rock-weighted Lakh
  subsets with content-derived tempo.
- **Articulation (C3):** palm-mute/open-chord/single-note discrimination improves vs the
  human-labels-only baseline.
- **Both modes improved:** Follow-mode reactive selection tracks your articulation/intensity;
  Play-mode section pools are Lakh-derived — measured on real playing, not the rule oracle.
- **Generative viability (C4, only if the §6.4 gate opens):** the spike produces guitar-conditioned drum+bass within the
  RT budget and beats the pattern-selection baseline in an A/B before full commitment.
- **Provenance:** `data/MANIFEST` lets any machine regenerate every processed dataset from
  documented sources.
- **No RT regressions:** no audio-thread allocation; latency/CPU within existing constraints
  (<30 ms, <15% CPU).
