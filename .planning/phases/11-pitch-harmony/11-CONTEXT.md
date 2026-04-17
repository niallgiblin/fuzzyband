# Phase 11: Pitch & harmony - Context

**Gathered:** 2026-04-17
**Status:** Ready for planning

<domain>
## Phase Boundary

Replace **fixed bass root** (E2 / MIDI 40 in authored patterns) with **pitch-informed bass routing** when the pitch path is active and confident, per **PITCH-01–04**. Does not include full plugin UI polish (Phase 14) or ML structure (Phase 12); may add minimal internal parameters or stubs if needed for safe defaults.

**Roadmap goal:** Pitch estimate stable enough for metal guitar in test harness; bass follows policy from detected root within documented latency; automated tests on synthetic / known ground truth.

</domain>

<decisions>
## Implementation Decisions

### Pitch estimator (PITCH-01)
- **D-11-01:** **Hybrid stack** — **YIN** as the always-available baseline real-time pitch path; **optional ONNX-hosted pitch model** (e.g. CREPE-class) when a model is present, reusing the same “optional model + fallback” story as Phase 10. Phase 11 delivers the integration points; exact ONNX graph packaging can align with `OnnxInference` / CMake optional deps.

### Harmony hints (PITCH-02)
- **D-11-02:** **Root only** — expose a **stable MIDI root per analysis window** plus **confidence** (and timestamps as needed for the existing feature queue). **No** chord spelling, progression tracking, or mode flags in Phase 11 unless a follow-up phase expands scope.

### Bass routing (PITCH-03)
- **D-11-03:** **Transpose at playback** — patterns stay authored with nominal bass at **MIDI 40 (E2)**; when the pitch path is active, apply a **semitone offset** from nominal root to **detected root** in the playback path (e.g. `PatternPlayer` or a thin helper) so drum events are unchanged and bass follows the guitarist. *(User selected “Claude’s discretion” for mechanism; this is the chosen approach for maintainability vs duplicating patterns per key.)*

### Activation & fallback
- **D-11-04:** When confidence is **low**, **hold the last good root** rather than snapping to E2 on every noisy frame. **Snap to fixed E2** (legacy behavior) on **cold start** and when **silence** (or equivalent “no pitch” state) is detected — exact silence detection should reuse existing energy/structure signals where possible.

### Testing (PITCH-04)
- **D-11-05:** **Unit tests** on **synthetic** inputs (e.g. sine at known frequency / controlled harness); **document regression thresholds** next to tests or in a short testing note. **Extend the existing macOS CI workflow** to **run these tests** (same runner family as the current build job).

### Claude's Discretion
- Exact confidence thresholds, window sizes, and queue fields on `FeatureVector` (names, optional smoothing) — planner/researcher to propose within PITCH latency goals and threading rules.
- Whether ONNX pitch model ships in-repo as a sample or is documented-only for Phase 11 — align with Phase 10 artifact story.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & roadmap
- `.planning/ROADMAP.md` — Phase 11 goal, success criteria, phase ordering vs 10/12/14
- `.planning/REQUIREMENTS.md` — **PITCH-01–04**

### Binding prior milestone context
- `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md` — policy / hybrid mindset (optional ML with safe fallback)
- `.planning/phases/09-data-training-strategy/09-CONTEXT.md` — `FeatureVector` → policy direction (Phase 9)

### Code integration points
- `src/analysis/FeatureVector.h` — extend with pitch/root/confidence (design in plan)
- `src/inference/IInference.h` — pattern selection may later use pitch; Phase 11 may only consume features in `AccompanimentProcessor` / playback unless inference needs pitch for routing
- `src/midi/MidiPatternLibrary.cpp` — nominal `kBassRoot = 40` authoring baseline
- `src/midi/PatternPlayer.cpp` / `PatternPlayer.h` — bass MIDI emission; transpose hook

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **MidiPattern / MidiEvent** — bass notes are data-driven; transposition can be applied at emit time without regenerating pattern tables for all keys.
- **FeatureVector + lock-free queue** — established handoff from audio to background thread; new pitch fields should follow the same snapshot model.

### Established Patterns
- **RuleBasedInference** — structure + BPM only today; pitch may feed **bass** first while pattern index still comes from rules/ML later.
- **Fixed E2** — single constant `kBassRoot`; Phase 11 replaces “always 40” with “40 + offset” when active.

### Integration Points
- **AccompanimentProcessor** — wires analysis → inference → pattern index; pitch likely computed on analysis path and copied into `FeatureVector` for the same cadence as other features (~50 Hz story per Phase 1 docs).

</code_context>

<specifics>
## Specific Ideas

No external reference tracks — user chose **hybrid YIN + optional ONNX**, **root-only harmony**, **hold-last-root** fallback, **CI-backed unit tests**.

</specifics>

<deferred>
## Deferred Ideas

**None from discussion** — scope stayed within PITCH-01–04.

</deferred>

---

*Phase: 11-pitch-harmony*
*Context gathered: 2026-04-17*
