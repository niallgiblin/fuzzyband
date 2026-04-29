# Phase 28: Beat tracker & bass sequencer - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.  
> Decisions are captured in `28-CONTEXT.md`.

**Date:** 2026-04-29  
**Phase:** 28 — Beat tracker & bass sequencer  
**Areas discussed:** Tempo engine boundary, IOI fallback, start gating, bass grid, Phase 28/29 API boundary, test strategy  

---

## Gray area selection

User selected **all** proposed areas: tempo integration, start gating, bass grid, API boundary (28 vs 29), tests/golden.

---

## Tempo engine — module boundary

| Option | Description | Selected |
|--------|-------------|----------|
| New class | New `BeatTracker` (or similar); `OnsetDetector` focused on onsets / minimal duties | |
| Extend `OnsetDetector` | Single place for BPM/phase; larger class | |
| Planner discretion | No strong preference — researcher/planner decide | ✓ |

**User's choice:** Planner discretion (implementation chooses boundary).  
**Notes:** Locked constraint: real-time safe, no heap on audio thread; see CONTEXT D-28-01.

---

## IOI-median policy (RHY-TEMPO-01)

| Option | Description | Selected |
|--------|-------------|----------|
| Fallback when low confidence | Beat primary; IOI-median explicit fallback + tests for both | ✓ |
| Retire from runtime | IOI only in tests / dev | |
| Parallel blend | Continuous blend of both BPM estimates | |

**User's choice:** Beat primary with explicit IOI fallback when beat confidence is low; both paths tested.

---

## Start / count-in gating

| Option | Description | Selected |
|--------|-------------|----------|
| Tracker lock | Replace 4-onset gate with beat-tracker-based gate (lock/confidence) | ✓ |
| Hybrid onsets | Keep N events but spaced by beat phase | |
| Keep current count-in | Defer superseding | |

**User's choice:** Tracker-based gate replaces count-in for Phase 28.

---

## Bass 16-step grid

| Option | Description | Selected |
|--------|-------------|----------|
| Bar phase | Steps at barStart + k×(bar/16); same clock as drums | ✓ |
| Beat clock only | Sub-bar grid without full bar policy in 28 | |
| Planner only | Minimal spec | |

**User's choice:** Bar-aligned grid per CONTEXT D-28-05/06.

---

## Phase 28 vs 29 — FeatureVector / queue

| Option | Description | Selected |
|--------|-------------|----------|
| Internal only | No FeatureVector changes in 28 | |
| Debug getters | Processor-level readouts for tests/UI; no queue payload until 29 | ✓ |
| Early FeatureVector fields | Risk cross-phase coupling | |

**User's choice:** Debug getters allowed; no `FeatureVector`/queue expansion in 28.

---

## RHY-TEMPO-02 — test strategy

| Option | Description | Selected |
|--------|-------------|----------|
| Synthetic primary | Clicks/metronome buffers; ±5 BPM vs known tempo | ✓ |
| + golden snippets | Add short clean-guitar WAVs | |
| Manual DAW only for real guitar | Synthetic automation only | |

**User's choice:** Synthetic primary; no required in-repo guitar golden files for closure.

---

## Claude's Discretion

- Exact class structure for beat tracker vs `OnsetDetector` (user: planner discretion).
- Confidence metrics, thresholds, hysteresis, and DSP details for 1-bar lock.

## Deferred Ideas

- Optional clean-guitar golden corpus — not part of Phase 28 acceptance.

---

*End of discussion log.*
