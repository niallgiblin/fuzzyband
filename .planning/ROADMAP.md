# Roadmap: Metal Accompaniment

## Milestones

- ✅ **v0.1.0 — Phase 1 rule-based MVP** — Phases 1–8 (shipped 2026-04-17)
- ✅ **v0.2.0 — Phase 2 ML + Generative** — Phases 9–16 (shipped 2026-04-17)
- 📋 **Next milestone** — TBD (start with `/gsd-new-milestone`)

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

**Phase numbering note:** The folder `.planning/phases/15-onset-robustness/` is **not** this table’s Phase 15. It documents a **sidecar** iteration (distortion / adaptive onset robustness). **Roadmap Phase 15** is **Python training pipeline** (PYTR-01–03).

**Operational follow-ups:** Push `v0.1.0-phase1` if pending; GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md`; optional Reaper routing smoke (OUT-05).

**Strategy context:** `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md` (hybrid rank/select, ~1 bar reaction, Groove/GMD vs E-GMD).

### Phase details (historical)

Full narratives and success criteria: `.planning/milestones/v0.2.0-ROADMAP.md`.

</details>

## Current focus

No active GSD phase. Define the next milestone (requirements + roadmap) with `/gsd-new-milestone`.

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

> **Superseded for execution:** Core themes shipped as **Phase 9** (DATA-*) in v0.2.0. Traceability: `.planning/milestones/v0.2.0-REQUIREMENTS.md`.

**Plans:** 3/3 plans complete

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
