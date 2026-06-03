# Roadmap: Metal Accompaniment

## Milestones

- ✅ **v0.1.0 — Phase 1 rule-based MVP** — Phases 1–8 (shipped 2026-04-17)
- ✅ **v0.2.0 — Phase 2 ML + Generative** — Phases 9–16 (shipped 2026-04-17)
- ✅ **v0.3.0 — Real ML Training Pipeline** — Phases 17–20 (shipped 2026-04-19; `EXP-02` human Reaper smoke still tracked — see `.planning/milestones/v0.3.0-REQUIREMENTS.md`)
- ✅ **v0.4.0 — ML Playability & Simplification** — Phases 21–26 (shipped 2026-04-29)
- ⏸️ **v0.5.0 — Rhythmic Coherence** — Phases 27–28 paused (superseded by v0.6.0 ONNX priority); Phase 31 (Architecture Deepening) shipped as v0.5.0
- ✅ **v0.6.0 — ML Correctness & Evaluation** — Phases 32–36 (shipped 2026-06-02; ONNX default enablement; archive: `.planning/milestones/v0.6.0-ROADMAP.md`)
- 🔜 **v0.7.0 — Creative Companion** — planning (brief: `.planning/milestones/v0.7.0-CONTEXT.md`)

## Phases

<details>
<summary>✅ v0.1.0 Phase 1 rule-based MVP (Phases 1–8) — SHIPPED 2026-04-17</summary>

| # | Phase | Goal | Requirements | Status |
|---|-------|------|--------------|--------|
| 1 | Project Scaffold | Buildable, loadable plugin with CMake + CI | SCAF-01–05 | ✓ Complete |
| 2 | Onset & Tempo | Detect guitar attacks; derive stable BPM | ONSET-01–04 | ✓ Complete |
| 3 | Energy & Structure | Classify guitar state (SILENT/VERSE/CHORUS/BREAKDOWN) | ENERGY-01–03 | ✓ Complete |
| 4 | MIDI Pattern Library | Author drum + bass patterns; offline audition tool | MIDI-01–05 | ✓ Complete |
| 5 | MIDI Output & DAW Routing | Wire PatternPlayer to MidiBuffer; verify in Reaper | OUT-01–05 | ✓ Complete* |
| 6 | Feature→Pattern Inference | Connect analysis to pattern selector via IInference | INFER-01–04 | ✓ Complete |
| 7 | Integration & Stability | Profile, tune, fix edge cases; 20-min session clean | STAB-01–04 | ✓ Complete |
| 8 | Docs & Phase 2 Handoff | Doxygen, CONTRIBUTING.md, v0.1.0 tag, Phase 2 issues | DOCS-01–05 | ✓ Complete 2026-04-17 |

\*Phase 5: routing code complete; optional Reaper confirmation. Full roadmap text at milestone close: `.planning/milestones/v0.1.0-ROADMAP.md`.

### Phase details (historical)

See archived roadmap for full phase narratives, success criteria, and plan lists.

</details>

<details>
<summary>✅ v0.2.0 Phase 2 ML + Generative (Phases 9–16) — SHIPPED 2026-04-17</summary>

| # | Phase | Goal | Requirements | Status |
|---|-------|------|--------------|--------|
| 9 | Data & training strategy | Dataset audit, tokenization, data-prep stub; absorbs backlog **999.1** themes | DATA-01–03 | ✓ Complete 2026-04-17 |
| 10 | ONNX runtime & IInference | Optional ORT; frozen I/O contract; audio-thread guardrails | ONNX-01–03 | ✓ Complete 2026-04-17 |
| 11 | Pitch & harmony | YIN + bass root routing; tests + pitch testing doc | PITCH-01–04 | ✓ Complete 2026-04-17 |
| 12 | ML structure | ML path + rule fallback; integration tests | STRUC-01–03 | ✓ Complete 2026-04-17 |
| 13 | Generative bass | Train/export; rank/select; validation + degradation | GBASS-01–03 | ✓ Complete 2026-04-17 |
| 14 | Plugin UI | APVTS policy + editor; README/CONTRIBUTING | PUI-01–03 | ✓ Complete 2026-04-17 |
| 15 | Python training pipeline | Pinned deps; stub train; ONNX export + contract validation | PYTR-01–03 | ✓ Complete 2026-04-17 |
| 16 | Terraform model storage | Versioned S3 + OIDC; promote/download scripts; runbook | CLOUD-01–02 | ✓ Complete 2026-04-17 |

**Phase numbering note:** The folder `.planning/phases/15-onset-robustness/` is **not** this table's Phase 15. It documents a **sidecar** iteration (distortion / adaptive onset robustness). **Roadmap Phase 15** is **Python training pipeline** (PYTR-01–03).

**Operational follow-ups:** Push `v0.1.0-phase1` if pending; GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md`; optional Reaper routing smoke (OUT-05).

**Strategy context:** `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md` (hybrid rank/select, ~1 bar reaction, Groove/GMD vs E-GMD).

### Phase details (historical)

Full narratives and success criteria: `.planning/milestones/v0.2.0-ROADMAP.md`.

</details>

<details>
<summary>✅ v0.3.0 Real ML Training Pipeline (Phases 17–20) — SHIPPED 2026-04-19</summary>

**Milestone Goal:** Replace synthetic stub with a real data pipeline and trained models using Groove MIDI Dataset. The C++ plugin is not modified — trained artifacts slot directly into `assets/*.onnx`.

**Note:** Implementation and install path are complete; **EXP-02** (non-degenerate pattern selection in a DAW) is verified via `.planning/phases/20-export-promotion/20-VERIFICATION.md` when an operator runs the checklist — requirement checkbox may remain open until that human pass.

### Phase 17: Data Pipeline
**Goal**: Real labeled training tensors exist and pass class coverage audit
**Depends on**: Phase 16 (training infrastructure from v0.2.0)
**Requirements**: DATA-04, DATA-05, DATA-06
**Success Criteria** (what must be TRUE):
  1. `training/download_gmd.py` runs idempotently; SHA-256 verification passes and corpus lands in gitignored `training/data/`
  2. `training/FEATURE_PROXY.md` exists and documents all heuristic mappings (rmsEnergy, spectralCentroid, hfFlux proxies and label oracle logic) before any model code is written
  3. `training/build_dataset.py` produces `.pt` tensors and the class histogram audit confirms ≥50 examples per pattern class (0–6)
**Plans**: `17-01-PLAN.md` (DATA-04), `17-02-PLAN.md` (DATA-05), `17-03-PLAN.md` (DATA-06)

### Phase 18: Pattern Model
**Goal**: A trained 3-layer MLP pattern classifier is exported to ONNX with normalization baked in and passes the frozen contract
**Depends on**: Phase 17
**Requirements**: PMODEL-01, PMODEL-02, PMODEL-03
**Success Criteria** (what must be TRUE):
  1. `training/models/pattern_model.py` defines `PatternNet` (5→32→16→7, BatchNorm, Dropout) and `PatternOnnxExport` wrapper with normalization embedded in the ONNX graph
  2. `training/train_gmd.py` runs to completion on real GMD tensors, logs per-class metrics to `metrics.jsonl`, and emits `pattern_trained.onnx` at opset 17
  3. `scripts/validate_onnx_contract.py --pattern` exits 0 for the trained artifact
**Plans**: `18-01-PLAN.md` (PMODEL-01), `18-02-PLAN.md` (PMODEL-02), `18-03-PLAN.md` (PMODEL-03)

### Phase 19: Bass Model
**Goal**: A trained bass model is exported to ONNX and passes the frozen contract
**Depends on**: Phase 17
**Requirements**: BMODEL-01, BMODEL-02
**Success Criteria** (what must be TRUE):
  1. `training/models/bass_model.py` defines `BassNet` (7→32→16→4) trained on synthetic E2/A2/B1 metal-key pitch distributions; training script runs without external corpus dependency
  2. `scripts/validate_onnx_contract.py --bass` exits 0 for the trained artifact
**Plans**: 2 plans

Plans:
- [x] 19-01-PLAN.md — Define BassNet + BassOnnxExport model classes
- [x] 19-02-PLAN.md — Synthetic training, ONNX export, contract validation + README

**Note**: Can run in parallel with Phase 18 — both depend on Phase 17 tensors only

### Phase 20: Export & Promotion
**Goal**: Trained models are installed into the plugin and produce musically sensible output in a DAW
**Depends on**: Phase 18, Phase 19
**Requirements**: EXP-01, EXP-02
**Success Criteria** (what must be TRUE):
  1. `scripts/install-model-local.sh` copies trained artifacts to `assets/*.onnx` and a subsequent `cmake --build` with `MA_ENABLE_ONNX=ON` succeeds without modification to any `src/` C++ file
  2. Plugin loaded in a DAW with `MA_ENABLE_ONNX=ON` selects non-degenerate pattern indices (not always the same index) across at least three distinct playing intensities
  3. `scripts/validate_onnx_contract.py` (both `--pattern` and `--bass` flags) passes on the final installed artifacts
**Plans**: 2 plans

Plans:
- [x] 20-01-PLAN.md — `install-model-local.sh` + `training/README.md` (EXP-01)
- [x] 20-02-PLAN.md — `20-VERIFICATION.md` Reaper DAW smoke (EXP-02)

**Artifacts:** Phase directories `17-data-pipeline` … `20-export-promotion` under `.planning/phases/`; requirements snapshot `.planning/milestones/v0.3.0-REQUIREMENTS.md`.

</details>

<details>
<summary>✅ v0.4.0 ML Playability & Simplification (Phases 21–26) — SHIPPED 2026-04-29</summary>

**Milestone goal:** Better-trained ML on GMD+Lakh merged data; 3-class structure; `[1,32]` bass piano-roll; simplified UI (no genre); ML rejection for “Next Pattern”; human Reaper verification (PVAL-01).

**Full narratives, success criteria, and plan lists:** `.planning/milestones/v0.4.0-ROADMAP.md`  
**Requirements snapshot:** `.planning/milestones/v0.4.0-REQUIREMENTS.md`

| Phase | Name | Plans | Status |
|-------|------|-------|--------|
| 21 | C++ Type Foundation | 2/2 | Complete 2026-04-28 |
| 22 | ONNX Contract + Stubs | 1/1 | Complete 2026-04-28 |
| 23 | C++ Inference Layer | 2/2 | Complete 2026-04-28 |
| 24 | UI Simplification | 2/2 | Complete 2026-04-29 |
| 25 | Training Data Pipeline | 3/3 | Complete 2026-04-29 |
| 26 | Retrain + Validate | 3/3 | Complete 2026-04-29 |

**Phase artifacts:** `.planning/phases/21-cpp-type-foundation/` … `26-retrain-validate/` (e.g. `26-03-SUMMARY.md`).

</details>

### v0.5.0 — Rhythmic Coherence (Phases 27–30)

**Milestone goal:** Make the plugin reliably join the player in time — fix tempo tracking on clean signal, eliminate drum/bass disconnection, and add musical transitions between sections.

**Research / design anchor:** `.planning/research/rhythmic-coherence/README.md` indexes v0.5.0 design/research; optional deeper artifacts may be added per clone.

**Target capabilities (summary)**

| Theme | Delivery |
|-------|-----------|
| Host workflow | Pre-FX placement documented and recommended in `plugin/README` (and discoverable from root docs) |
| Tempo | Beat tracker (autocorrelation + DP, 1-bar lock) replaces IOI-median engine; count-in gate superseded |
| Bass | Bar-aligned bass step sequencer — fixes “only step 0 plays” |
| Coupling | Unified drum/bass update on bar boundaries |
| Features | `FeatureVector` +5: beat phase sin/cos, tempo confidence, prev pattern, prev bass density |
| Transitions | Four fills driven by section direction to avoid abrupt cuts |
| ML | Separate phase: retrain on 12-feature input; contract + promotion |

**Dependency note:** **27** (docs) can run early. **28** (beat tracker + bass sequencer) before **29** (C++ coordination, enriched features, fills). **30** (retrain) after **29** locks feature semantics and ONNX I/O.

- [x] **Phase 27: Rhythmic coherence documentation** — Pre-FX workflow, milestone-facing README updates, pointers to research
- [x] **Phase 28: Beat tracker & bass sequencer** — IMPLEMENTED (2/2 plans, 2026-04-29) but UAT rejected (1/4 pass). Code (BeatTracker, bass-sequencer) exists in tree. BeatTracker refocused to learn-mode + gate in v0.7.0 S01 Tempo Stability; continuous-chase path deprecated per D001 (session-first tempo).
- [x] **Phase 29: Runtime coordination (C++)** — COMPLETE (3/3 plans, 2026-06-03). Review fixes resolved; UAT passed 8/8; security verified with 0 open threats.
- [ ] ~~**Phase 30: ML retrain (12 features)**~~ — **SUPERSEDED** by v0.7.0. Phase 30's 12-feature ONNX retrain assumed reactive tempo/phase semantics that v0.7.0 discards (D001: session-first tempo). Retrain will happen on v0.7.0's frozen feature semantics per `.planning/milestones/v0.7.0-CONTEXT.md`.

#### Phase 27: Rhythmic coherence documentation
**Goal**: Hosts and users have a single, authoritative pre-FX placement story; research decisions are discoverable from the roadmap.
**Depends on**: None (can start in parallel with late v0.4 work)
**Requirements**: RHY-DOC-01, RHY-DOC-02
**Success criteria** (what must be TRUE):
  1. `plugin/README.md` states insert order (guitar → plugin → FX/amp), failure modes when post-FX, and link to rhythmic-coherence research folder
  2. Root or `docs/` entry points link to the same workflow so contributors do not miss it
**Plans**: `27-01-PLAN.md` (wave 1 — RHY-DOC-01), `27-02-PLAN.md` (wave 2 — RHY-DOC-02); see `.planning/phases/27-rhythmic-coherence-documentation/`

#### Phase 28: Beat tracker & bass sequencer
**Goal**: Tempo follows musical pulse on clean input; bass MIDI reflects every active 16th in the bar-aligned piano-roll path.
**Depends on**: Phase 27 optional for README polish; no hard blocker
**Requirements**: RHY-TEMPO-01, RHY-TEMPO-02, RHY-BASSSEQ-01
**Success criteria** (what must be TRUE):
  1. New beat-tracking path is the primary BPM/phase source (IOI-median path retired or demoted to fallback with explicit tests)
  2. Controlled tests or golden audio snippets show BPM stability within ±5 BPM where project expects it
  3. Bass sequencer regression test: multiple steps in a bar produce note-ons (not only step 0)
**Plans**: `28-01-PLAN.md` (RHY-TEMPO-01/02), `28-02-PLAN.md` (RHY-BASSSEQ-01) in `.planning/phases/28-beat-tracker-bass-sequencer/`

#### Phase 29: Runtime coordination (C++)
**Goal**: Drums and bass share one musical clock; inference sees enriched features; section changes trigger directional fills instead of hard cuts.
**Depends on**: Phase 28
**Requirements**: RHY-SYNC-01, RHY-FEAT-01, RHY-FILL-01
**Success criteria** (what must be TRUE):
  1. Drum pattern changes and bass updates align to the same bar boundary policy (documented + unit/integration coverage)
  2. `FeatureVector` (and lock-free queue payload) includes the five new fields populated on the audio thread without allocation
  3. At least four fill patterns mapped by section-direction; audible overlap or fill bar before new groove (exact behavior per research docs)
**Plans**: 3 plans in `.planning/phases/29-runtime-coordination-c/`

Plans:
- [x] 29-01-PLAN.md — Rescope audit, RHY-FEAT-01 supersession, and shared commit policy documentation
- [x] 29-02-PLAN.md — Shared fixed-size pending groove commit for drum pattern plus generated bass activation
- [x] 29-03-PLAN.md — Four minimal directional transition fills through the shared commit path

#### Phase 30: ML retrain (12-feature input) — SUPERSEDED by v0.7.0
**Original goal**: ONNX models and training code match the expanded v0.5.0 feature vector.
**Supersession rationale**: v0.7.0's D001 (session-first tempo) discards the reactive tempo/phase semantics that Phase 30's 12-feature tensor assumed. Retrain will execute on v0.7.0's frozen feature semantics in a future ML refresh phase. See `.planning/milestones/v0.7.0-CONTEXT.md`.
**Plans**: None (superseded before planning)

#### Phase 31: Architecture Deepening
**Goal**: Four shallow or leaky modules extracted into deep, independently-testable units; `AccompanimentProcessor` reduced to a thin coordinator.
**Depends on**: None (pure refactor — no behaviour changes required; can run before or after Phase 28 execution)
**Requirements**: ARCH-01, ARCH-02, ARCH-03, ARCH-04
**Success criteria** (what must be TRUE):
  1. `PlaybackGate` class exists in `src/analysis/`; `AccompanimentProcessor` loses 8 gate-related private fields; new unit tests cover phrase-breath/beat-snap interaction directly
  2. `StablePitchTracker` class exists in `src/analysis/`; `AccompanimentProcessor` loses 5 pitch-stability private fields (`heldPitchRootMidi`, `heldPitchConfidence`, `pitchHoldValid`, `pitchStableCounterSamples`, `lastStablePitchMidi`; `lastBassUpdateSample` retained — inference-thread bass hold guard) and the ~40-line stability block; tests cover pitch-class stability window and semitone mapping
  3. Shared `pattern_rules.h` (or equivalent) in `src/inference/`; both `RuleBasedInference.cpp` and `OnnxInference.cpp` depend on it; duplication eliminated
  4. `TempoStabiliser` class exists in `src/analysis/`; `AccompanimentProcessor` loses 3 BPM-hysteresis fields; unit tests drive the stabilizer with synthetic BPM candidate sequences
**Plans**: 4 plans in 2 waves

Plans:
- [x] 31-01-PLAN.md — PatternSelectionRules header-only extraction (ARCH-03, Wave 1)
- [x] 31-02-PLAN.md — TempoStabiliser extraction (ARCH-04, Wave 1)
- [x] 31-03-PLAN.md — PlaybackGate extraction (ARCH-01, Wave 2)
- [x] 31-04-PLAN.md — StablePitchTracker extraction + version bump to 0.5.0 (ARCH-02, Wave 2)

---

## Progress

| Phase | Milestone | Plans Complete | Status | Completed |
|-------|-----------|----------------|--------|-----------|
| 1. Project Scaffold | v0.1.0 | — | Complete | 2026-04-17 |
| 2. Onset & Tempo | v0.1.0 | — | Complete | 2026-04-17 |
| 3. Energy & Structure | v0.1.0 | — | Complete | 2026-04-17 |
| 4. MIDI Pattern Library | v0.1.0 | — | Complete | 2026-04-17 |
| 5. MIDI Output & DAW Routing | v0.1.0 | — | Complete | 2026-04-17 |
| 6. Feature→Pattern Inference | v0.1.0 | — | Complete | 2026-04-17 |
| 7. Integration & Stability | v0.1.0 | — | Complete | 2026-04-17 |
| 8. Docs & Phase 2 Handoff | v0.1.0 | — | Complete | 2026-04-17 |
| 9. Data & training strategy | v0.2.0 | — | Complete | 2026-04-17 |
| 10. ONNX runtime & IInference | v0.2.0 | — | Complete | 2026-04-17 |
| 11. Pitch & harmony | v0.2.0 | — | Complete | 2026-04-17 |
| 12. ML structure | v0.2.0 | — | Complete | 2026-04-17 |
| 13. Generative bass | v0.2.0 | — | Complete | 2026-04-17 |
| 14. Plugin UI | v0.2.0 | — | Complete | 2026-04-17 |
| 15. Python training pipeline | v0.2.0 | — | Complete | 2026-04-17 |
| 16. Terraform model storage | v0.2.0 | — | Complete | 2026-04-17 |
| 17. Data Pipeline | v0.3.0 | 3/3 | Complete | 2026-04-18 |
| 18. Pattern Model | v0.3.0 | 3/3 | Complete | 2026-04-19 |
| 19. Bass Model | v0.3.0 | 2/2 | Complete | 2026-04-19 |
| 20. Export & Promotion | v0.3.0 | 2/2 | Complete | 2026-04-19 |
| 21. C++ Type Foundation | v0.4.0 | 2/2 | Complete | 2026-04-28 |
| 22. ONNX Contract + Stubs | v0.4.0 | 1/1 | Complete   | 2026-04-28 |
| 23. C++ Inference Layer | v0.4.0 | 2/2 | Complete | 2026-04-28 |
| 24. UI Simplification | v0.4.0 | 2/2 | Complete | 2026-04-29 |
| 25. Training Data Pipeline | v0.4.0 | 3/3 | Complete | 2026-04-29 |
| 26. Retrain + Validate | v0.4.0 | 3/3 | Complete | 2026-04-29 |
| 27. Rhythmic coherence documentation | v0.5.0 | 2/2 | Complete | 2026-04-29 |
| 28. Beat tracker & bass sequencer | v0.5.0 | 2/2 | Code exists; UAT rejected (1/4 pass) | 2026-04-29 |
| 29. Runtime coordination (C++) | v0.5.0 | 3/3 | Complete    | 2026-06-03 |
| 30. ML retrain (12 features) | v0.5.0 | 0/0 | Superseded by v0.7.0 | - |
| 31. Architecture Deepening | v0.5.0 | 4/4 | Complete   | 2026-05-03 |
| 32. Training Label Correction | v0.6.0 | 3/3 | Complete   | 2026-05-12 |
| 33. Model Quality Gates | v0.6.0 | 3/3 | Complete   | 2026-05-13 |
| 34. Domain Gap and Feature Capture | v0.6.0 | 3/3 | Complete   | 2026-05-25 |
| 35. Inference Path Consistency | v0.6.0 | 3/3 | Complete    | 2026-05-25 |
| 36. ONNX Evaluation and Default Readiness | v0.6.0 | 4/4 | Complete | 2026-06-02 |

---

## Risk Register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Onset detector fires on noise/silence | Medium | Noise gate threshold + minimum energy floor |
| Tempo tracker unstable on slow riffs | Medium | Clamp IOI range, median filter, fallback to last stable BPM |
| Palm mute misclassified as SILENT | Medium | Spectral centroid distinguishes mute from silence |
| Pattern transitions sound jarring | Low-Medium | Quantise transitions to bar boundaries |
| Plugin crashes on buffer size change | Low | Reset all state in `prepareToPlay()` |
| Latency too high | Low | Background thread inference keeps audio thread unblocked |
| ONNX model too heavy for realtime | Medium | Quantization, smaller graphs, or hybrid rank/select only |
| Training data license unclear | Medium | DATA-01 audit before fine-tune; prefer GMD/Lakh per strategy memo |
| Generative bass musically unstable | Medium | Constraints + GBASS degradation to static patterns |
| Label class collapse (VERSE >> BREAKDOWN) | High | Class histogram audit gate in Phase 17; CrossEntropyLoss with per-class weights |
| Normalization outside ONNX graph | High | Bake normalization into PatternOnnxExport before first training run |
| ORT version skew (training vs C++ runtime) | Medium | Pin opset 17; match onnxruntime==1.20.1 to CMakeLists.txt ORT version |
| StructureState enum change breaks 3 ONNX models silently | High | Phase 21 + 22 must complete before any model retrain; shape assertions in INF-01 prevent silent fallback |
| Genre APVTS removal partial crash on session load | High | All 4 files changed atomically in Phase 24; session XML round-trip test gates the phase |
| Beat tracker false locks on polyrhythms / weak onsets | Medium | Hold 1-bar lock; fallback path; tune DP on real clips from research set |
| Feature-count skew (train vs plugin) | High | Phase 30 gated on frozen `FeatureVector` layout + single validator for ONNX inputs |
| Transition fills misfire on noisy structure | Medium | Hysteresis + direction from smoothed energy; fill length capped per RHY-FILL-01 tests |

### v0.6.0 — ML Correctness & Evaluation (Phases 32–36)

**Milestone goal:** Fix training label semantics so the pattern model is a learned approximation of the rule path (not an activity-quantile ranker); add quality gates to bass and structure models; close the MIDI-to-guitar domain gap with a real audio capture pipeline; align rule and ONNX inference paths on adjusted BPM and compatible exclusion logic; and establish a repeatable evaluation baseline that gates `MA_ENABLE_ONNX=ON` becoming the recommended default.

**Source design document:** `CHANGES_PLAN.md` (Areas 1–5)

**Dependency note:** Phases **32**, **33**, **34** can execute in parallel. Phase **35** depends on **32** (adjusted-BPM change requires a coordinated retrain). Phase **36** depends on all four preceding phases.

#### Phase 32: Training Label Correction
**Goal**: Pattern model training labels are derived from the rule oracle (`PatternRules`) rather than ordinal quantile bins; `build_dataset.py` and `build_lakh_dataset.py` updated; model retrained and promoted to `assets/`.
**Depends on**: None (independent of v0.5.0 phases; can run in parallel with 33 and 34)
**Requirements**: LABEL-01, LABEL-02, LABEL-03
**Success criteria** (what must be TRUE):
  1. `build_dataset.py` generates labels Y∈[0,6] by calling `PatternRules::rulePatternForState` on each proxy feature row; quantile-bin scoring removed
  2. `training/FEATURE_PROXY.md` updated to document rule-oracle labeling approach; quantile-bin description retired
  3. Retrained `accompaniment_model.onnx` promoted to `assets/`; `scripts/validate_onnx_contract.py --pattern` exits 0; DATA-06 histogram gate still passes
**Plans**: 3 plans

Plans:
- [x] 32-01-PLAN.md — Add rule oracle + Breakdown heuristic to build_dataset.py; unit tests (LABEL-01, Wave 1)
- [x] 32-02-PLAN.md — Oracle labeling for build_lakh_dataset.py; merge_datasets.py label passthrough; FEATURE_PROXY.md update (LABEL-01, LABEL-02, Wave 1)
- [x] 32-03-PLAN.md — Run full pipeline, retrain, validate ONNX contract, promote to assets/ (LABEL-03, Wave 2)

#### Phase 33: Model Quality Gates
**Goal**: Bass and structure models have hard quality gates that block export on poor convergence; structure model ONNX graph embeds its own normalization; per-step bass validation metrics logged.
**Depends on**: None (can run in parallel with 32 and 34; coordinate retrain with Phase 35)
**Requirements**: QGATE-01, QGATE-02, QGATE-03, QGATE-04
**Success criteria** (what must be TRUE):
  1. `train_bass.py` exits non-zero if `best_val_mse` exceeds a computed rule-baseline MSE threshold; no ONNX export produced on failure; per-step velocity-error and pitch-offset distribution written to `validation.json`
  2. `train_structure.py` exits non-zero if val macro-F1 < rule-path agreement rate on the same split; `StructureOnnxExport` wrapper bakes mean/std normalisation into the ONNX graph (mirrors `PatternOnnxExport` pattern)
  3. `OnnxStructureInference.cpp` reads normalisation from ONNX graph constants (external normalisation step removed)
  4. `training/tests/test_onnx_contract.py` validates structure model I/O shape and dtype alongside pattern and bass model checks
**Plans**: 3 plans in 2 waves

Plans:
**Wave 1**
- [x] 33-01-PLAN.md — Bass quality gate: baseline MSE, per-step metrics, validation.json (QGATE-01, Wave 1)
- [x] 33-02-PLAN.md — Structure dual gate + structure_norm_stats.json output (QGATE-02, Wave 1)

**Wave 2** *(blocked on Wave 1 completion)*
- [x] 33-03-PLAN.md — StructureOnnxExport baked normalization + contract tests + promote asset (QGATE-02, QGATE-03, QGATE-04, Wave 2)

#### Phase 34: Domain Gap and Feature Capture
**Goal**: A `FeatureVector` capture utility records real guitar audio features to disk; an offline evaluation harness measures rule-path vs ONNX accuracy on that data; the residual domain gap is quantified and documented.
**Depends on**: None (capture pipeline is foundational for Phase 36 real-audio evaluation)
**Requirements**: DOMAIN-01, DOMAIN-02, DOMAIN-03
**Success criteria** (what must be TRUE):
  1. Standalone utility (or plugin debug mode toggle) records `FeatureVector` values to a JSONL file from a live guitar session; output format documented
  2. Offline evaluation script accepts a JSONL + human-annotated pattern labels; prints rule-path accuracy vs ONNX accuracy and per-class breakdown side-by-side
  3. `training/FEATURE_PROXY.md` updated with quantitative distribution-shift estimates per feature (bpm, rms, centroid, hf_flux at minimum); residual domain gap documented with concrete numbers; worst-case divergence for spectral centroid proxy identified and proxy improved or explicitly accepted
**Plans**: 3 plans in 3 waves

Plans:
**Wave 1**
- [x] 34-01-PLAN.md — Runtime FeatureVector capture component, plugin debug toggle, JSONL schema docs (DOMAIN-01, Wave 1)

**Wave 2** *(blocked on Wave 1 completion)*
- [x] 34-02-PLAN.md — Offline capture evaluator with annotation CSV, rule/ONNX accuracy, confusion matrices, disagreement output (DOMAIN-02, Wave 2)

**Wave 3** *(blocked on Waves 1-2 completion)*
- [x] 34-03-PLAN.md — Captured-vs-proxy gap analyzer and FEATURE_PROXY.md quantitative verdicts (DOMAIN-03, Wave 3)

#### Phase 35: Inference Path Consistency
**Goal**: Rule path and ONNX path use the same intensity-adjusted BPM input; exclusion modulo respects state compatibility (no accidental Breakdown on non-transition inputs); `structureBlend` semantics documented.
**Depends on**: Phase 32 (adjusted-BPM tensor change requires coordinated model retrain with corrected labels)
**Requirements**: INFC-01, INFC-02, INFC-03
**Success criteria** (what must be TRUE):
  1. `OnnxInference.cpp` packs `PatternRules::adjustedBpm(f)` (not raw `f.bpm`) into tensor column 0; pattern model retrained with adjusted BPM in the proxy column; `docs/ONNX_IO.md` and `training/FEATURE_PROXY.md` updated to document column 0 is adjusted BPM
  2. D-23-04 exclusion logic walks forward modulo 7 until `isPatternCompatibleWithState` passes, rather than blind `(last+1)%7`; `test_rule_based_inference.cpp` asserts exclusion never returns an incompatible pattern for any state
  3. `structureBlend` semantics documented (inline comment or `docs/` entry): rule state is authoritative for silence detection and gate; ONNX shadow only shapes non-silent pattern decisions at `blend > 0.5`
**Plans**: 3 plans in 3 waves

Plans:
- [x] 35-01-PLAN.md — PatternRules compatible exclusion, OnnxInference adjusted BPM X[0], Catch2 matrix tests (INFC-01 partial, INFC-02, Wave 1)
- [x] 35-02-PLAN.md — 3× intensity dataset expansion, merge, retrain, promote accompaniment_model.onnx (INFC-01 complete, Wave 2)
- [x] 35-03-PLAN.md — structureBlend + adjusted BPM documentation in ONNX_IO, FEATURE_PROXY, processor comment (INFC-01 docs, INFC-03, Wave 3)

#### Phase 36: ONNX Evaluation and Default Readiness
**Goal**: Repeatable ONNX latency benchmark exists and runs in CI; A/B comparison tool works on captured feature logs; `docs/ONNX_READINESS.md` documents exact flip criteria; all six readiness conditions are met or have a clear tracked path.
**Depends on**: Phases 32, 33, 34, 35
**Requirements**: ONNXEVAL-01, ONNXEVAL-02, ONNXEVAL-03
**Success criteria** (what must be TRUE):
  1. CTest or standalone benchmark runs 10,000 inference calls per model (all three); all complete a single call in < 5 ms on reference M-series hardware; benchmark added to CI and fails if any model exceeds the gate
  2. A/B comparison script accepts a JSONL of `FeatureVector` values and prints `RuleBasedInference` vs `OnnxInference` pattern selections side-by-side with agreement rate summary
  3. `docs/ONNX_READINESS.md` exists with the six flip criteria (pattern macro-F1 ≥ 0.65 on real-audio set, ≥ 80% rule agreement, latency < 5 ms, bass gate passing, structure normalisation baked, ONNX contract tests passing); current pass/fail status tracked per criterion; `MA_ENABLE_ONNX` default flipped to ON when all criteria pass
**Plans**: 4 plans in 4 waves

Plans:
- [ ] 36-01-PLAN.md — Phase 35 carryover verification: clamp/Breakdown boundary tests + D-10 regression (Wave 1)
- [ ] 36-02-PLAN.md — Spectral-centroid proxy fix, retrain/promote accompaniment_model.onnx, FEATURE_PROXY.md (Wave 2)
- [ ] 36-03-PLAN.md — Committed readiness fixture, macro-F1/agreement gates, --replay, CI eval (ONNXEVAL-02, Wave 3)
- [ ] 36-04-PLAN.md — ORT latency CTest, ONNX_READINESS.md, MA_ENABLE_ONNX default flip (ONNXEVAL-01/03, Wave 4)

---

## Backlog

### Phase 999.1: ML training data feasibility research (BACKLOG)

**Goal:** Identify and license-audit candidate training datasets for a fine-tuned metal drum/bass accompaniment model (M2 precursor). Produce a go/no-go feasibility memo recommending the primary dataset path.

> **Superseded for execution:** Core themes shipped as **Phase 9** (DATA-*) in v0.2.0. Traceability: `.planning/milestones/v0.2.0-REQUIREMENTS.md`.

**Plans:** 4/4 plans complete

Plans:
- [x] Executed under Phase 9 / v0.2.0 (see phase 9 plans and `docs/DATASET_AUDIT.md`)

**Captured context (from discussion 2026-04-16):**

- **Base model candidate:** Anticipatory Music Transformer (Thickstun et al., Stanford) — pretrained on Lakh MIDI, designed for symbolic accompaniment / infilling with partial-track conditioning. Fine-tuning on a metal-weighted subset looks tractable on a single A100.
- **Deployment target:** cloud GPU inference (on-device not a requirement). Rate-limit cloud calls to 2–5 Hz without perceptible UX loss.
- **Dataset shortlist to evaluate:**
  - Lakh MIDI Dataset (~170k files, free, clean licensing, AMT's pretraining set)
  - GuitarPro tab scrapes (huge volume, legally gray — TOS violations common)
  - Groove MIDI Dataset (Magenta — paired drum performances, not metal but teaches groove)
  - Slakh2100 (synthetic multi-track from MIDI, genre-limited)
  - Commissioned recordings (20–50 hrs of drummers accompanying guitar tracks — expensive but cleanest signal)
- **Experience decisions already made:**
  - Pure audio-reactive + optional **session seed** (preset dropdown for subgenre/tempo); no in-session chatbot
  - Reactive **fill-cue detection** on a 2-bar rolling window, not long-horizon structure prediction (real drummers sense the windup, don't predict sections)
  - **Session-adaptive style embedding** first (typical IOI, RMS envelope, chord-root histogram); persisted per-user LoRA deferred
  - Avoid audio-generative models (MusicGen, Riffusion, Suno) — they emit waveforms, not MIDI, and don't fit the `IInference` contract
- **Remaining M2 scope** (context for planning, *not* this phase): preprocessing pipeline (MIDI → AMT token format), fine-tuning pipeline + eval harness, cloud inference service (latency budget, rate-limiting, cost model), plugin `CloudInference` class implementing `IInference` with graceful fallback to rule-based, eval & iteration (listening tests, fill-cue accuracy, seed adherence, per-user style embedding).
- **Open questions for this phase:** which datasets clear license audit, how much paired guitar→drums coverage exists vs. needs commissioning, tokenization compatibility with AMT's vocabulary.

**Strategy memo (2026-04-17):** Consolidated ML data + integration direction (Groove/GMD vs E-GMD, rank vs replace vs hybrid, guitar bridge, ~1-bar reaction / anticipation) lives in `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`. Use it when promoting 999.1 or opening the next milestone.
