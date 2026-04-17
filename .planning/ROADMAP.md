# Roadmap: Metal Accompaniment

## Milestones

- ✅ **v0.1.0 — Phase 1 rule-based MVP** — Phases 1–8 (shipped 2026-04-17)
- 🚧 **v0.2.0 — Phase 2 ML + Generative** — Phases 9–16 (in progress)

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

### v0.2.0 — Phases 9–16 (active)

| # | Phase | Goal | Requirements | Status |
|---|-------|------|--------------|--------|
| 9 | Data & training strategy | Dataset audit, tokenization, data-prep stub; absorbs backlog **999.1** themes | DATA-01–03 | Not started |
| 10 | ONNX runtime & IInference | ONNX Runtime in CMake; `OnnxInference` + fallback | ONNX-01–03 | Not started |
| 11 | Pitch & harmony | 3/3 | Complete    | 2026-04-17 |
| 12 | ML structure | Classifier/head vs. `StructureTagger`; rule fallback | STRUC-01–03 | Not started |
| 13 | Generative bass | Train → ONNX; constrained MIDI bass | GBASS-01–03 | Not started |
| 14 | Plugin UI | Genre, intensity, variation via APVTS | PUI-01–03 | Not started |
| 15 | Python training pipeline | Repro env, dataset ingest, training export | PYTR-01–03 | Not started |
| 16 | Terraform model storage | Versioned artifacts + promotion runbook | CLOUD-01–02 | Not started |

**Next:** `/gsd-discuss-phase 9` or `/gsd-plan-phase 9`. Operational: push `v0.1.0-phase1`; GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md`.

**Strategy context:** `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md` (hybrid rank/select, ~1 bar reaction, Groove/GMD vs E-GMD).

---

## Phase details (v0.2.0)

### Phase 9: Data & training strategy

**Goal:** Close the planning gap between symbolic MIDI corpora and plugin `FeatureVector` policy. Deliver audit memo, tokenization choice, and reproducible prep stub.  
**Requirements:** DATA-01, DATA-02, DATA-03  
**Success criteria:**

1. Written go/no-go on primary training data path with license notes
2. Tokenization documented and referenced by prep code
3. One runnable prep path from representative MIDI to model input tensors (or agreed format)

---

### Phase 10: ONNX runtime & IInference

**Goal:** Load ONNX models off the audio thread; keep `RuleBasedInference` as fallback.  
**Requirements:** ONNX-01, ONNX-02, ONNX-03  
**Success criteria:**

1. CMake builds with ONNX optional; docs list env vars
2. Swapping `IInference` implementation selects ONNX model when present
3. Stress test: no audio-thread blocking; inference cadence ~50 Hz preserved

---

### Phase 11: Pitch & harmony

**Goal:** Replace fixed bass root with pitch-informed routing when enabled.  
**Requirements:** PITCH-01, PITCH-02, PITCH-03, PITCH-04  
**Success criteria:**

1. Pitch estimate stable enough for metal guitar input in test harness
2. Bass notes follow policy from detected root (within documented latency)
3. Automated tests on synthetic sine / known MIDI ground truth

---

### Phase 12: ML structure

**Goal:** ML-based section/energy classification with safe fallback to thresholds.  
**Requirements:** STRUC-01, STRUC-02, STRUC-03  
**Success criteria:**

1. Offline metric vs. reference labels or shadow comparison to `StructureTagger`
2. Fallback path verified when model missing
3. No regression in bar-quantized pattern switches in integration test

---

### Phase 13: Generative bass

**Goal:** Train a small model; export to runtime; constrain outputs to valid bass MIDI.  
**Requirements:** GBASS-01, GBASS-02, GBASS-03  
**Success criteria:**

1. Checkpoint/ONNX loads in plugin test build
2. Output MIDI passes validation (range, channel, timing bounds)
3. CPU/latency documented; degradation path tested

---

### Phase 14: Plugin UI

**Goal:** User-facing controls wired to policy/inference.  
**Requirements:** PUI-01, PUI-02, PUI-03  
**Success criteria:**

1. APVTS parameters created and saved with project
2. Editor shows controls; layout acceptable for Reaper/AU hosting smoke test
3. README or CONTRIBUTING section for parameter semantics

---

### Phase 15: Python training pipeline

**Goal:** Reproducible training on macOS; export for plugin.  
**Requirements:** PYTR-01, PYTR-02, PYTR-03  
**Success criteria:**

1. Clean install from lockfile succeeds on fresh machine
2. At least one training run with logged loss/metrics
3. Export artifact consumed by Phase 10 path

---

### Phase 16: Terraform model storage

**Goal:** Versioned cloud storage for models; clear promotion process.  
**Requirements:** CLOUD-01, CLOUD-02  
**Success criteria:**

1. `terraform apply` (or workspace) creates bucket/storage with versioning
2. Runbook: tag → checksum → path used by release/build

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

---

## Backlog

### Phase 999.1: ML training data feasibility research (BACKLOG)

**Goal:** Identify and license-audit candidate training datasets for a fine-tuned metal drum/bass accompaniment model (M2 precursor). Produce a go/no-go feasibility memo recommending the primary dataset path.

> **Superseded for execution:** Core themes are **Phase 9** (DATA-*) in v0.2.0. Keep this backlog entry for narrative context until Phase 9 ships, then archive or remove via `/gsd-review-backlog`.

**Requirements:** Covered by DATA-01–03 in `.planning/REQUIREMENTS.md`

**Plans:** 3/3 plans complete

Plans:
- [ ] TBD — use `/gsd-plan-phase 9` to create plans

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

**Strategy memo (2026-04-17):** Consolidated ML data + integration direction (Groove/GMD vs E-GMD, rank vs replace vs hybrid, guitar bridge, ~1-bar reaction / anticipation) lives in `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`. Use it when promoting 999.1 or opening Milestone 2.
