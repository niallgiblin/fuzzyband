# Phase 11: Pitch & harmony - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in `11-CONTEXT.md`.

**Date:** 2026-04-17
**Phase:** 11 — Pitch & harmony
**Areas discussed:** Pitch stack, Harmony depth, Bass transposition, Activation & fallback, Tests

---

## Pitch stack

| Option | Description | Selected |
|--------|-------------|----------|
| YIN first | Ship pitch→root without ONNX dependency first | |
| CREPE/ONNX main | Primary path via ONNX aligned with Phase 10 | |
| Hybrid | YIN baseline + optional ONNX pitch model when present | ✓ |

**User's choice:** Hybrid  
**Notes:** Aligns optional ONNX pitch with broader ML/ONNX degradation pattern.

---

## Harmony depth

| Option | Description | Selected |
|--------|-------------|----------|
| Root only | Stable MIDI pitch + confidence per window | ✓ |
| Root + mode | Optional major/minor if cheap | |
| Defer richer | Strict minimum; backlog for more | |

**User's choice:** Root only  

---

## Bass transposition

| Option | Description | Selected |
|--------|-------------|----------|
| Emit-time transpose | Delta vs nominal 40 in playback path | |
| Rewrite patterns | Per-key pattern data | |
| Claude's discretion | Agent picks best fit to codebase | ✓ |

**User's choice:** Claude's discretion → **resolved to emit-time transpose** (see CONTEXT D-11-03).

---

## Activation & fallback

| Option | Description | Selected |
|--------|-------------|----------|
| Fixed E2 below threshold | Always snap to E2 when uncertain | |
| Hold last good root | Hold while noisy; E2 on silence/cold start | ✓ |
| APVTS stub early | Internal bool before Phase 14 | |

**User's choice:** Hold last good root  

---

## Tests

| Option | Description | Selected |
|--------|-------------|----------|
| Unit only | Synthetic + documented thresholds | |
| Unit + CI | As above + macOS CI runs tests | ✓ |
| Minimal smoke | One test + manual checklist | |

**User's choice:** Unit + CI  

---

## Claude's Discretion

- **Bass transposition mechanism:** Emit-time transpose vs nominal MIDI 40 (see CONTEXT).

## Deferred Ideas

- None recorded.
