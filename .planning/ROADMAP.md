# Roadmap: Metal Accompaniment

## Milestones

- ✅ **v0.1.0 — Phase 1 rule-based MVP** — Phases 1–8 (shipped 2026-04-17)
- ✅ **v0.2.0 — Phase 2 ML + Generative** — Phases 9–16 (shipped 2026-04-17)
- ✅ **v0.3.0 — Real ML Training Pipeline** — Phases 17–20 (shipped 2026-04-19; `EXP-02` human Reaper smoke still tracked — see `.planning/milestones/v0.3.0-REQUIREMENTS.md`)
- ✅ **v0.4.0 — ML Playability & Simplification** — Phases 21–26 (shipped 2026-04-29)
- 🔜 **v0.5.0 — Rhythmic Coherence** — Phases 27–30 (in progress; requirements: `.planning/REQUIREMENTS.md`)

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
- [ ] **Phase 28: Beat tracker & bass sequencer** — Replace tempo core; bar-aligned bass steps; tests on clean guitar / synthetic signals
- [ ] **Phase 29: Runtime coordination (C++)** — Unified bar tick; extend `FeatureVector` and handoff to inference; transition fills
- [ ] **Phase 30: ML retrain (12 features)** — Update `docs/ONNX_IO.md`, training scripts, validators; retrain, export, promote, smoke + jam

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
**Plans**: TBD (`/gsd-plan-phase 29`)

#### Phase 30: ML retrain (12-feature input)
**Goal**: ONNX models and training code match the expanded feature vector; shipped artifacts pass contract validation and brief DAW smoke.
**Depends on**: Phase 29
**Requirements**: RHY-ML-01, RHY-ML-02
**Success criteria** (what must be TRUE):
  1. `docs/ONNX_IO.md`, `validate_onnx_contract.py`, and exporters reflect 12-dimensional (or documented) pattern/structure inputs as designed in research
  2. Retrained models promoted to `assets/`; plugin build with `MA_ENABLE_ONNX=ON`; jam checklist confirms no regression vs v0.4 behavior goals
**Plans**: TBD (`/gsd-plan-phase 30`)

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
| 28. Beat tracker & bass sequencer | v0.5.0 | 0/2 | Not started | - |
| 29. Runtime coordination (C++) | v0.5.0 | 0/0 | Not started | - |
| 30. ML retrain (12 features) | v0.5.0 | 0/0 | Not started | - |
| 31. Architecture Deepening | v0.5.0 | 4/4 | Complete   | 2026-05-03 |

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
