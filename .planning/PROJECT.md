# Metal Accompaniment

## What This Is

A JUCE 8 VST3/AU plugin for macOS that listens to a guitarist's audio input and outputs rhythmically appropriate drum and bass MIDI in real time. **v0.1.0** shipped a rule-based analysis pipeline; **v0.2.0** adds optional ONNX inference (patterns and generative bass), ML structure, pitch-aware bass, APVTS UI, training/export tooling, and Terraform model storage — still on the same `IInference` contract and non-blocking audio path. **v0.3.0** adds a Groove-MIDI-based training pipeline (`download_gmd` → `build_dataset` → `train_gmd` / `train_bass`), ONNX exports with contract validation, and a local install script into `assets/*.onnx` without C++ changes. **Plugin build version** (editor UI): `CMakeLists.txt` `project(VERSION)`; **phase/milestone status**: `.planning/STATE.md` / `.planning/ROADMAP.md`.

## Core Value

A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.

## Shipped: v0.3.0 — Real ML Training Pipeline

**Closed:** 2026-04-19 — Phases 17–20. GMD download + tensor pipeline + class histogram gate; `PatternNet` + `train_gmd.py` + `pattern_trained.onnx`; `BassNet` + `train_bass.py` + `bass_trained.onnx`; `install-model-local.sh` + Phase 20 README; Reaper verification procedure in `20-VERIFICATION.md` (human EXP-02).

**Archived requirements:** `.planning/milestones/v0.3.0-REQUIREMENTS.md`  
**Archived roadmap:** `.planning/milestones/v0.3.0-ROADMAP.md`

## Shipped: v0.2.0 — Phase 2 (ML + Generative)

**Closed:** 2026-04-17 — Phases 9–16. ONNX optional path + frozen `docs/ONNX_IO.md` contract; YIN pitch and adaptive bass root; ML structure with rule fallback; generative bass with validation and degradation; APVTS/editor policy UI; Python stub training + ONNX export validation; Terraform S3 + promotion runbook.

**Archived requirements:** `.planning/milestones/v0.2.0-REQUIREMENTS.md`  
**Archived roadmap:** `.planning/milestones/v0.2.0-ROADMAP.md`

## Next milestone

**Not defined.** Run `/gsd-new-milestone` to set version, requirements, and roadmap. `.planning/REQUIREMENTS.md` is reset to a placeholder until then.

## Prior milestones (shipped)

**v0.1.0** (Phase 1 rule-based MVP) — 2026-04-17: JUCE 8 VST3/AU, rule-based pipeline → `IInference` → `PatternPlayer`, lock-free handoff, macOS CI, docs handoff. `.planning/milestones/v0.1.0-ROADMAP.md`, `.planning/milestones/v0.1.0-REQUIREMENTS.md`.

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
- ✓ PYTR-01–03: pinned `training/requirements.txt`, stub train + `metrics.jsonl`, ONNX export + `scripts/validate_onnx_contract.py` — v0.2.0 Phase 15
- ✓ ONNX-01–03: optional ORT build, `OnnxInference` + rule fallback, `docs/ONNX_IO.md` + audio-thread CI guard — v0.2.0 Phase 10
- ✓ STRUC-01–03: ML structure path + fallback + bar-quantized transitions — v0.2.0 Phase 12
- ✓ GBASS-01–03: generative bass ONNX path, `BassMidiValidator`, rank/select + degradation docs — v0.2.0 Phase 13
- ✓ BMODEL-01/02: `BassNet` + `train_bass.py` synthetic pipeline, `bass_trained.onnx`, `validate_onnx_contract.py --bass` — v0.3.0 Phase 19
- ✓ DATA-04/05/06: GMD download, `FEATURE_PROXY.md`, `build_dataset.py` + histogram gate — v0.3.0 Phase 17
- ✓ PMODEL-01/02/03: `PatternNet`, `train_gmd.py`, contract `--pattern` — v0.3.0 Phase 18
- ✓ EXP-01: `scripts/install-model-local.sh` + `training/README.md` Phase 20 section — v0.3.0 Phase 20

### Active (release / ops)

**Operational (carryover):**

- [ ] Push `v0.1.0-phase1` to GitHub (`git push origin v0.1.0-phase1`)
- [ ] Push `v0.2.0` tag to GitHub (`git push origin v0.2.0`)
- [ ] Push `v0.3.0` tag to GitHub after optional EXP-02 human verification (`git push origin v0.3.0`)
- [ ] Run EXP-02 Reaper checklist (`.planning/phases/20-export-promotion/20-VERIFICATION.md`) if gating the release tag on in-DAW behavior
- [ ] Open Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md`
- [ ] (Optional) Confirm plugin + drum VSTi routing in Reaper for your setup

### Out of Scope

- Waveform music generation (does not produce MIDI; out of product contract)
- Windows plugin build (macOS first; revisit in a future milestone)

## Context

- **Solo dev, part-time** — 8–10 week timeline for Phase 1
- **Target DAW**: Reaper primary, Ableton + Logic secondary
- **Platform**: macOS first (Apple Silicon + Intel universal); Windows later
- **JUCE version**: 8 (required for macOS 15+ / juceaide build; roadmap originally wrote JUCE 7 but JUCE 8 was used)
- **Codebase state**: **v0.1.0**, **v0.2.0**, and **v0.3.0** shipped — planning history in `.planning/milestones/`; active roadmap `.planning/ROADMAP.md` (next milestone TBD). Backlog **999.1** strategy memo: `.planning/phases/999.1-ml-phase-2-data-feasibility-research/`.

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
| Bass fixed to root E2 until pitch tracking | Pitch detection deferred to Phase 2; avoids wrong notes | ✓ Good — v0.2.0 PITCH path + tests |
| constexpr pattern arrays (no file I/O) | Plugin must not do file I/O in the audio path | ✓ Good |
| GMD + synthetic bass training before cloud/Lakh | Ship a reproducible local training loop; defer larger corpora | ✓ Good — v0.3.0 |

---
*Last updated: 2026-04-19 after v0.3.0 milestone close (`/gsd-complete-milestone`)*

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
