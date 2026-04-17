# Metal Accompaniment

## What This Is

A JUCE 8 VST3/AU plugin for macOS that listens to a guitarist's audio input and outputs rhythmically appropriate drum and bass MIDI in real time. Phase 1 uses a rule-based signal analysis pipeline (onset detection, tempo tracking, energy/structure classification) with no ML. The interface is designed so Phase 2 can drop in an ONNX ML model without changing the threading model or plugin architecture.

## Core Value

A guitarist can play into the plugin and hear a musically reactive metal drum groove fire in time — with zero manual tempo tapping or pattern selection.

## Current state

**v0.1.0** (Phase 1 rule-based MVP) closed 2026-04-17: JUCE 8 VST3/AU, rule-based onset/tempo/structure → `IInference` → `PatternPlayer`, lock-free handoff, macOS CI, Doxygen + CONTRIBUTING + CHANGELOG handoff, Phase 2 issue checklist in `docs/PHASE2_GITHUB_ISSUES.md`. Next: define Milestone 2 (`/gsd-new-milestone`); optional ops — `git push origin v0.1.0-phase1`, open Phase 2 issues on GitHub.

## Next milestone goals

- Run `/gsd-new-milestone` to author a fresh `.planning/REQUIREMENTS.md` and roadmap for Phase 2 (ML + generative / cloud inference — direction TBD).
- Promote or schedule backlog **999.1** using `PHASE2_ML_DATA_STRATEGY.md` when opening Milestone 2.
- Complete operational handoff: push `v0.1.0-phase1`, create GitHub issues from the Phase 2 checklist.

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

### Active

- [ ] Push `v0.1.0-phase1` to GitHub (`git push origin v0.1.0-phase1`)
- [ ] Open Phase 2 GitHub issues from `docs/PHASE2_GITHUB_ISSUES.md`
- [ ] (Optional) Confirm plugin + drum VSTi routing in Reaper for your setup

### Out of Scope

- ML-based inference (Phase 1) — deliberately deferred; IInference interface keeps Phase 2 as a drop-in replacement
- Pitch/chord detection for root note tracking — Phase 2
- Generative bass lines — Phase 2
- Plugin UI with parameter controls — Phase 2
- Python training pipeline — Phase 2
- Windows support — macOS first; Windows after Phase 1 stable
- Generative or adaptive patterns — rule-based selection only in Phase 1

## Context

- **Solo dev, part-time** — 8–10 week timeline for Phase 1
- **Target DAW**: Reaper primary, Ableton + Logic secondary
- **Platform**: macOS first (Apple Silicon + Intel universal); Windows later
- **JUCE version**: 8 (required for macOS 15+ / juceaide build; roadmap originally wrote JUCE 7 but JUCE 8 was used)
- **Codebase state**: **v0.1.0 shipped** (2026-04-17) — Phases 1–8 complete; rule-based MVP + docs handoff. See `.planning/MILESTONES.md`.
- **Next:** Define Milestone 2 with `/gsd-new-milestone`; backlog 999.1 holds ML data strategy memo.

## Constraints

- **Performance**: End-to-end audio→MIDI latency < 30ms; CPU < 15% on M-series at 256-sample buffer
- **Threading**: Audio thread must never block — lock-free queue + atomics only
- **Stability**: 20-minute session with no crashes or xruns
- **Accuracy**: Tempo within ±5 BPM; pattern switching within ~200ms of energy change
- **Interface**: IInference must remain stable — Phase 2 ONNX model is a drop-in

## Key Decisions

| Decision | Rationale | Outcome |
|----------|-----------|---------|
| JUCE 8 over JUCE 7 | Required for macOS 15+ / juceaide build compatibility | ✓ Good |
| Rule-based Phase 1, ONNX Phase 2 | Ships a working product fast; IInference interface abstracts the swap | ✓ Good (Phase 1 shipped; swap still Phase 2) |
| Lock-free inference pipeline (atomic + ReaderWriterQueue) | Audio thread must never block | ✓ Good |
| Bass fixed to root E2 until pitch tracking | Pitch detection deferred to Phase 2; avoids wrong notes | — Pending |
| constexpr pattern arrays (no file I/O) | Plugin must not do file I/O in the audio path | ✓ Good |

---
*Last updated: 2026-04-17 after v0.1.0 milestone close*

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
