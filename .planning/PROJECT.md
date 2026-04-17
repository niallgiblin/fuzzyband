# Metal Accompaniment

## What This Is

A JUCE 8 VST3/AU plugin for macOS that listens to a guitarist's audio input and outputs rhythmically appropriate drum and bass MIDI in real time. Phase 1 uses a rule-based signal analysis pipeline (onset detection, tempo tracking, energy/structure classification) with no ML. The interface is designed so Phase 2 can drop in an ONNX ML model without changing the threading model or plugin architecture.

## Core Value

A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.

## Current Milestone: v0.2.0 — Phase 2 (ML + Generative)

**Goal:** Evolve the Phase 1 plugin with ONNX-capable inference, real-time pitch/harmony features, ML-based structure, generative bass, user-facing controls, a Python training loop, and versioned cloud model storage — without breaking the `IInference` contract or blocking the audio thread.

**Target features:**

- Dataset/tokenization strategy and training data prep (closes themes from backlog **999.1**; see `PHASE2_ML_DATA_STRATEGY.md`)
- ONNX Runtime + `OnnxInference` with fallback to `RuleBasedInference`
- Pitch/chord path for adaptive bass root (replacing fixed E2 where active)
- ML structure classifier with rule-based fallback
- Generative bass model (train → export → runtime constraints)
- APVTS-backed UI: genre, intensity, pattern variation
- Python training pipeline + metal MIDI dataset ingestion
- Terraform for model artifact storage and promotion runbook

**Requirements:** Scoped in `.planning/REQUIREMENTS.md` (REQ-IDs **DATA-**, **ONNX-**, **PITCH-**, **STRUC-**, **GBASS-**, **PUI-**, **PYTR-**, **CLOUD-**).

## Prior milestone (shipped)

**v0.1.0** (Phase 1 rule-based MVP) closed 2026-04-17: JUCE 8 VST3/AU, rule-based pipeline → `IInference` → `PatternPlayer`, lock-free handoff, macOS CI, docs handoff. Details: `.planning/MILESTONES.md`, `.planning/milestones/v0.1.0-ROADMAP.md`.

## Requirements

### Validated

- ✓ JUCE 8 CMake build with VST3 + AU targets — Phase 1 Milestone 1
- ✓ AudioProcessorValueTreeState skeleton for parameter UI — Phase 1 Milestone 1
- ✓ GitHub Actions CI (build-only, macOS) — Phase 1 Milestone 1
- ✓ OnsetDetector (spectral flux, FFT, IOI median, BPM clamp 80–220 BPM) — Phase 1 Milestone 2
- ✓ Thread-safe `getCurrentBpm()` + onset flag per block — Phase 1 Milestone 2
- ✓ Unit test: synthetic click @ 160 BPM converges within 4 beats — Phase 1 Milestone 2
- ✓ EnergyAnalyser (RMS 100ms, spectral centroid, high-freq flux) — Phase 1 Milestone 3
- ✓ StructureTagger (SILENT/VERSE/CHORUS/BREAKDOWN, 2s hysteresis) — Phase 1 Milestone 3
- ✓ Debug UI showing state label + BPM — Phase 1 Milestone 3
- ✓ MidiPattern/MidiEvent design; 7 pattern indices — Phase 1 Milestone 4
- ✓ Drum + bass patterns as static C++ data — Phase 1 Milestone 4
- ✓ Offline MIDI export utility for DAW auditioning — Phase 1 Milestone 4
- ✓ PatternPlayer (beat clock, MidiBuffer, humanisation, ch 10 drums / ch 2 bass) — Phase 1 Milestone 5
- ✓ IInference interface + RuleBasedInference (future ONNX drop-in) — Phase 1 Milestone 6
- ✓ Background inference ~50Hz; lock-free feature queue; atomic pattern index — Phase 1 Milestone 6
- ✓ Integration & stability (jam profile, CPU, edge cases, 20-min session) — v0.1.0 Phase 7
- ✓ Doxygen + CONTRIBUTING + CHANGELOG/tag instructions + Phase 2 issue checklist — v0.1.0 Phase 8
- ✓ YIN pitch estimator; `FeatureVector` root + confidence; D-11-04 hold/snap; bass semitone offset at `PatternPlayer` emit; tests + `docs/PHASE11_PITCH_TESTING.md` — v0.2.0 Phase 11 (PITCH-01–04)
- ✓ DATA-01–03: dataset audit (`docs/DATASET_AUDIT.md`), event tokenization (`docs/TOKENIZATION.md`), reproducible MIDI prep stub (`training/prep_midi.py`) + CI smoke — v0.2.0 Phase 9
- ✓ PUI-01–03: APVTS policy (`genre`, `intensity`, `variation`, `structureBlend`, `generativeBassMode`), `PolicyPatternMapper`, editor attachments, README/CONTRIBUTING parameter docs — v0.2.0 Phase 14
- ✓ CLOUD-01–02: Terraform S3 + GitHub OIDC IAM (`infra/`), bootstrap tfstate script, `promote-model.sh` / `download-model.sh`, optional CI download — v0.2.0 Phase 16

### Active (v0.2.0)

See `.planning/REQUIREMENTS.md` for numbered acceptance criteria. High level: DATA ✓ → ONNX → PITCH → STRUC → GBASS → PUI ✓ → **PYTR** → **CLOUD ✓** (remaining: **PYTR** Phase 15).

**Operational (carryover from v0.1.0):**

- [ ] Push `v0.1.0-phase1` to GitHub (`git push origin v0.1.0-phase1`)
- [ ] Open Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md`
- [ ] (Optional) Confirm plugin + drum VSTi routing in Reaper for your setup

### Out of Scope

- Waveform music generation (does not produce MIDI; out of product contract)
- Windows plugin build in v0.2.0 (macOS first; revisit after Milestone 2 stabilizes)

## Context

- **Solo dev, part-time** — 8–10 week timeline for Phase 1
- **Target DAW**: Reaper primary, Ableton + Logic secondary
- **Platform**: macOS first (Apple Silicon + Intel universal); Windows later
- **JUCE version**: 8 (required for macOS 15+ / juceaide build; roadmap originally wrote JUCE 7 but JUCE 8 was used)
- **Codebase state**: **v0.1.0 shipped**; **v0.2.0 in progress** — requirements and roadmap in `.planning/REQUIREMENTS.md` / `.planning/ROADMAP.md`. Backlog **999.1** strategy memo remains under `.planning/phases/999.1-ml-phase-2-data-feasibility-research/`.

## Constraints

- **Performance**: End-to-end audio→MIDI latency < 30ms; CPU < 15% on M-series at 256-sample buffer
- **Threading**: Audio thread must never block — lock-free queue + atomics only
- **Stability**: 20-minute session with no crashes or xruns
- **Accuracy**: Tempo within ±5 BPM; pattern switching within ~200ms of energy change
- **Interface**: `IInference` remains stable — new backends (`OnnxInference`, future cloud) implement the same contract

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| JUCE 8 over JUCE 7 | Required for macOS 15+ / juceaide build compatibility | ✓ Good |
| Rule-based Phase 1, ONNX Phase 2 | Ships a working product fast; IInference interface abstracts the swap | ✓ Good — v0.2.0 implements ONNX path |
| Lock-free inference pipeline (atomic + ReaderWriterQueue) | Audio thread must never block | ✓ Good |
| Bass fixed to root E2 until pitch tracking | Pitch detection deferred to Phase 2; avoids wrong notes | ⚠️ Revisit in v0.2.0 (PITCH-*) |
| constexpr pattern arrays (no file I/O) | Plugin must not do file I/O in the audio path | ✓ Good |

---
*Last updated: 2026-04-17 after Phase 16 (Terraform model storage) execution*

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state
