# Roadmap: Metal Accompaniment

## Milestones

- ✅ **v0.1.0 — Phase 1 rule-based MVP** — Phases 1–8 (shipped 2026-04-17)
- ✅ **v0.2.0 — Phase 2 ML + Generative** — Phases 9–16 (shipped 2026-04-17)
- ✅ **v0.3.0 — Real ML Training Pipeline** — Phases 17–20 (shipped 2026-04-19; `EXP-02` human Reaper smoke still tracked — see `.planning/milestones/v0.3.0-REQUIREMENTS.md`)
- 🚧 **v0.4.0 — ML Playability & Simplification** — Phases 21–26 (in progress)

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

### v0.4.0 — ML Playability & Simplification (Phases 21–26)

**Milestone Goal:** Make the plugin musical and self-contained — better-trained ML drives all pattern decisions from a richer dataset, the UI gets out of the way, and the structure model is simplified to energy states.

**Dependency note:** Phases 21 → 22 → 23 must run in strict sequence (type foundation unlocks contract; contract unlocks inference). Phase 24 (UI) depends only on Phase 23. Phase 25 (data) depends on Phase 21 (3-class label encoding) and can run in parallel with Phases 22–24. Phase 26 (retrain) depends on both Phase 23 and Phase 25.

- [ ] **Phase 21: C++ Type Foundation** — Reduce StructureState to 3 values and remove genre field; compiler enumerates all breakage
- [x] **Phase 22: ONNX Contract + Stubs** — Update I/O contract docs and regenerate stubs; unblocks CI and C++ inference work
- [ ] **Phase 23: C++ Inference Layer** — Update OnnxInference packing for 3-class state and piano-roll bass; wire rejection signal
- [ ] **Phase 24: UI Simplification** — Remove genre widget atomically; rewire Next Pattern to rejection signal
- [ ] **Phase 25: Training Data Pipeline** — Download and merge Lakh MIDI with GMD into joint train/val tensors
- [ ] **Phase 26: Retrain + Validate** — Retrain all three models against new contracts; human Reaper jam verification

### Phase 21: C++ Type Foundation
**Goal**: All C++ code compiles cleanly with a 3-value StructureState and no genre field — the compiler surfaces every stale reference before any ONNX or Python work begins
**Depends on**: Phase 20 (last shipped phase)
**Requirements**: TYPE-01, TYPE-02
**Success Criteria** (what must be TRUE):
  1. Plugin compiles in Release and Debug with `StructureState` values `SILENT`, `SOFT`, `LOUD` only — no reference to `VERSE`, `CHORUS`, or `BREAKDOWN` in any `src/` file
  2. All unit tests pass (`MetalAccompanimentTests`) with 3-state tagger logic
  3. `FeatureVector` contains no `policyGenreIndex` field; all ONNX packing sites updated; zero compiler errors or warnings about removed identifiers
  4. CI green on macOS with `MA_ENABLE_ONNX=OFF` (rule-based path unbroken after enum change)
**Plans**: 2 plans

Plans:
- [ ] 21-01-PLAN.md — Redefine StructureState enum (SILENT/SOFT/LOUD), update StructureTagger, StructureHoldSmoother, FeatureVector, PolicyPatternMapper, RuleBasedInference, OnnxStructureInference, AccompanimentEditor
- [ ] 21-02-PLAN.md — Update all tests for 3-state enum, bump version to 0.4.0, build and pass test suite

### Phase 22: ONNX Contract + Stubs
**Goal**: The ONNX I/O contract document and stub model generators reflect the 3-class state input and piano-roll bass output — CI can validate contract shapes before any real model is trained
**Depends on**: Phase 21
**Requirements**: ONNX-04, ONNX-05
**Success Criteria** (what must be TRUE):
  1. `docs/ONNX_IO.md` documents `X[state]` as 3-class one-hot and `Y_struct` as `[1,3]`; `Y_bass` documented as `[1,32]` piano-roll (16-step × pitch offset + velocity per 16th note)
  2. `build_minimal_structure_onnx.py` regenerated stub passes `validate_onnx_contract.py --structure` with new `[1,3]` output shape
  3. `validate_onnx_contract.py --bass` passes against a `[1,32]` stub output — old `[1,4]` stub fails the check
  4. CI smoke (`MA_ENABLE_ONNX=ON` with updated stub models) passes without shape assertion errors
**Plans**: 1 plan

Plans:
- [x] 22-01-PLAN.md — Lock ONNX docs/contracts, regenerate deterministic stubs, enforce validator + ONNX CI smoke gate

### Phase 23: C++ Inference Layer
**Goal**: `OnnxInference` asserts correct shapes at load time, packs 3-class state correctly, decodes `[1,32]` piano-roll bass output, and exposes a rejection signal that the message thread can trigger
**Depends on**: Phase 22
**Requirements**: INF-01, INF-02
**Success Criteria** (what must be TRUE):
  1. Loading an ONNX model with wrong `Y_bass` shape (e.g. old `[1,4]`) raises a hard assertion at plugin startup — no silent catch/fallback to rule-based on shape mismatch; rule-based fallback triggers only when model file is absent
  2. `IInference::selectPattern` accepts `excludeIndex` parameter; `patternRejectionCount` atomic is written by the message thread and bypasses the 2-bar hold guard for exactly one inference cycle
  3. Plugin loads with updated stub models and `MA_ENABLE_ONNX=ON`; pattern display shows ML path output
  4. Unit test: calling the rejection path with `excludeIndex=N` never returns pattern N on the immediately following `selectPattern` call
**Plans**: 2 plans

Plans:
- [x] 23-01-PLAN.md — Pattern [1,7] one-hot packing + bass [1,32] piano-roll decoding + load-time contract assertions (INF-01)
- [x] 23-02-PLAN.md — IInference excludeIndex + patternRejectionCount atomic + single-shot hold-guard bypass + unit tests (INF-02)

### Phase 24: UI Simplification
**Goal**: Genre selection is gone from the plugin across all four affected files atomically; Next Pattern button drives the ML rejection signal instead of hardcoded index cycling
**Depends on**: Phase 23
**Requirements**: UI-01, UI-02
**Success Criteria** (what must be TRUE):
  1. Genre APVTS parameter absent from plugin; genre widget absent from editor; `PolicyPatternMapper` contains no genre branching — all four files changed in one commit
  2. A session XML saved by a pre-v0.4.0 build loads in v0.4.0 without crash (unknown `genre` attribute ignored gracefully by APVTS)
  3. "Next Pattern" button increments `patternRejectionCount` on the processor; ML inference loop responds by excluding the current pattern on its next cycle
  4. Variation slider absent from editor; `structureBlend` slider retained
**Plans**: 2 plans
**UI hint**: yes

Plans:
- [ ] 24-01-PLAN.md — Atomic removal: genre, variation, PolicyPatternMapper; resized() reflow (UI-01, UI-02)
- [ ] 24-02-PLAN.md — Session backward-compat test, version bump 0.4.1, build verify (UI-01, UI-02)

### Phase 25: Training Data Pipeline
**Goal**: A merged GMD + Lakh MIDI tensor dataset with source-stratified train/val splits is on disk and passes a domain compatibility check — no model retraining may begin until this gate clears
**Depends on**: Phase 21 (3-class state encoding used in label oracle)
**Requirements**: DATA-07, DATA-08, DATA-09
**Success Criteria** (what must be TRUE):
  1. `download_lakh.py` downloads `lmd_matched` subset; filtered file count (channel-10 drum track present, BPM in [80, 220]) is logged and meets minimum threshold
  2. `build_lakh_dataset.py` produces tensors in GMD-compatible format; domain compatibility check comparing Lakh vs GMD feature distributions passes before merge is permitted
  3. `merge_datasets.py` produces merged `.pt` shards with joint quantile label recomputation; GMD-only validation fold preserved in a separate shard for cross-corpus comparison
  4. Class histogram over merged labels shows ≥50 examples per class across all 7 pattern indices
**Plans**: TBD

### Phase 26: Retrain + Validate
**Goal**: All three ONNX models are retrained against updated contracts and a human Reaper jam confirms that ML-driven pattern selection is audible with no rule-based fallback
**Depends on**: Phase 23 (updated inference layer), Phase 25 (merged dataset)
**Requirements**: PMODEL-04, BMODEL-03, STRUC-04, PVAL-01
**Success Criteria** (what must be TRUE):
  1. `validate_onnx_contract.py --pattern` passes for `PatternNet` retrained on merged GMD+Lakh with 3-class state encoding; macro-F1 early-stop gate satisfied
  2. `validate_onnx_contract.py --bass` passes for `BassNet` with `[1,32]` piano-roll output; exported interval vocabulary demonstrably includes P5, m3, tritone, and chromatic approach offsets
  3. `validate_onnx_contract.py --structure` passes for structure classifier with 3-class `SILENT/SOFT/LOUD` output
  4. Reaper jam (5-minute session): ≥3 distinct drum patterns audible; bass lines demonstrate within-bar variation; plugin pattern display shows ML-chosen indices with no silent fallback to rule-based default
**Plans**: 3 plans

Plans:
- [x] 26-01-PLAN.md — Training data prep (bass + structure) + C++ WIP integration + inference name UI label
- [x] 26-02-PLAN.md — Retrain all three models (PatternNet, BassNet, StructureNet) with quality gates
- [x] 26-03-PLAN.md — ONNX promotion + Reaper jam verification + phase summary

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
| 24. UI Simplification | v0.4.0 | 0/0 | Not started | - |
| 25. Training Data Pipeline | v0.4.0 | 0/0 | Not started | - |
| 26. Retrain + Validate | v0.4.0 | 3/3 | Complete | 2026-04-29 |

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

---

## Backlog

### Phase 999.1: ML training data feasibility research (BACKLOG)

**Goal:** Identify and license-audit candidate training datasets for a fine-tuned metal drum/bass accompaniment model (M2 precursor). Produce a go/no-go feasibility memo recommending the primary dataset path.

> **Superseded for execution:** Core themes shipped as **Phase 9** (DATA-*) in v0.2.0. Traceability: `.planning/milestones/v0.2.0-REQUIREMENTS.md`.

**Plans:** 1/1 plans complete

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
