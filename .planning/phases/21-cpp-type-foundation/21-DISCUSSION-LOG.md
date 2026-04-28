# Phase 21: C++ Type Foundation - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-28
**Phase:** 21-cpp-type-foundation
**Areas discussed:** BREAKDOWN mapping, 3-state thresholds, Hold times, OnnxStructureInference stub

---

## BREAKDOWN Mapping

| Option | Description | Selected |
|--------|-------------|----------|
| SOFT | BREAKDOWN in metal = chuggy, low centroid, lower energy than chorus — maps naturally to SOFT | |
| LOUD | Some breakdowns hit hard (high RMS swell) — treat as LOUD if RMS is high enough | |
| Let thresholds decide | Remove BREAKDOWN as a concept entirely; RMS+centroid boundaries decide whether heavy-but-quiet riffs land in SOFT or LOUD | ✓ |

**User's choice:** Let thresholds decide
**Notes:** BREAKDOWN is abolished as a named concept. The tagger threshold boundaries alone determine SOFT vs LOUD.

---

## 3-State Thresholds

| Option | Description | Selected |
|--------|-------------|----------|
| Pure RMS | SILENT/SOFT/LOUD split entirely on RMS level — simple, directly tied to how hard the guitarist plays | ✓ |
| RMS + centroid | Keep dual-axis: spectral centroid still contributes (low-centroid heavy riff = SOFT even at high RMS) | |
| You decide | Let researcher and planner pick the cleanest approach | |

**User's choice:** Pure RMS
**Notes:** Deliberate simplification. Spectral centroid dropped as a classifier axis for state transitions.

---

## Hold Times

| Option | Description | Selected |
|--------|-------------|----------|
| Keep per-state | SILENT, SOFT, LOUD each get their own hold constant — preserves tuning flexibility | ✓ |
| Single non-SILENT hold | SILENT keeps its own hold, SOFT and LOUD share one constant | |

**User's choice:** Keep per-state
**Notes:** `kHoldSoftSec` and `kHoldLoudSec` replace `kHoldVerseSec`/`kHoldChorusSec`/`kHoldBreakdownSec`.

---

## OnnxStructureInference Stub

| Option | Description | Selected |
|--------|-------------|----------|
| Map 3 classes now | Update `stateFromClassIndex` to {0→SILENT, 1→SOFT, 2→LOUD} now; Phase 22-23 owns full I/O contract | ✓ |
| TODO stub only | Replace with `static_assert(false)` or fallthrough-to-SILENT — breaks fast if ONNX path runs before Phase 22-23 | |

**User's choice:** Map 3 classes now
**Notes:** Compile-clean stub with correct forward target. Functional correctness gated on Phases 22–23.

---

## Claude's Discretion

- Exact numeric value for `kLoudRms` (SOFT/LOUD boundary)
- Exact `kHoldSoftSec` and `kHoldLoudSec` values
- Whether to remove spectral centroid from `computeDesiredState()` signature or just stop using it internally

## Deferred Ideas

None — discussion stayed within phase scope.
