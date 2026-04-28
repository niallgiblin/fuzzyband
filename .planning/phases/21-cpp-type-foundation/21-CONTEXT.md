# Phase 21: C++ Type Foundation - Context

**Gathered:** 2026-04-28
**Status:** Ready for planning

<domain>
## Phase Boundary

Reduce `StructureState` to 3 values (`SILENT`, `SOFT`, `LOUD`) and remove `policyGenreIndex` from `FeatureVector`. The compiler enumerates every stale reference. All `src/` C++ compiles cleanly in Release and Debug; unit tests pass; CI green with `MA_ENABLE_ONNX=OFF`. `MA_ENABLE_ONNX=ON` must also compile — `OnnxStructureInference` gets a correct 3-class stub mapping now; the full I/O contract and packing correctness is Phase 22–23's scope.

**Not in scope:** ONNX contract doc updates, Python training changes, model retraining, UI changes, piano-roll bass output, rejection signal.

</domain>

<decisions>
## Implementation Decisions

### StructureState collapse
- **D-21-01:** Old states map as follows — `VERSE` → `SOFT`, `CHORUS` → `LOUD`. `BREAKDOWN` is not pre-mapped to either: the concept is abolished entirely. The tagger threshold boundaries decide whether heavy-but-low-energy playing lands in `SOFT` or `LOUD` based on RMS alone.
- **D-21-02:** The `StructureState` enum becomes: `SILENT`, `SOFT`, `LOUD` — three values, in that order. All old enum references (`VERSE`, `CHORUS`, `BREAKDOWN`) are removed from every `src/` file; no migration comments.

### Threshold logic (StructureTagger)
- **D-21-03:** `computeDesiredState()` switches to **pure RMS** for the SOFT/LOUD boundary. Spectral centroid is **dropped as a classifier axis** for state transitions (it may remain in `FeatureVector` for downstream use). Logic: below silence-floor RMS → `SILENT`; above loud-threshold RMS → `LOUD`; in between → `SOFT`. The exact threshold values are Claude's discretion — set them to be musically reasonable for metal (suggest keeping `kSilentRms` as-is and deriving `kLoudRms` from the current `kVerseRms`/`kChorusRms` midpoint or prior calibration).

### Hold times
- **D-21-04:** Keep **per-state hold constants**: `kHoldSilentSec`, `kHoldSoftSec`, `kHoldLoudSec`. The old `kHoldVerseSec`, `kHoldChorusSec`, `kHoldBreakdownSec` are removed. Initial values are Claude's discretion — preserving the old verse/chorus hold values as SOFT/LOUD respectively is a reasonable starting point.

### FeatureVector / genre removal
- **D-21-05:** Remove `policyGenreIndex` field from `FeatureVector` entirely. `PolicyPatternMapper.cpp` line that uses it (`idx = (idx + fv.policyGenreIndex) % 7`) becomes `idx = idx % 7` (or equivalent no-op). No replacement mechanism; genre is gone.

### OnnxStructureInference stub
- **D-21-06:** Update `stateFromClassIndex()` in `OnnxStructureInference.cpp` to map `{0 → SILENT, 1 → SOFT, 2 → LOUD}`. This is a compile-clean stub; the model it runs against still has the old 4-class shape — Phase 22–23 will fix that. The 3-class mapping is the correct forward target and doesn't break anything when `MA_ENABLE_ONNX=OFF` (the CI gate for this phase).

### Claude's Discretion
- Exact numeric values for `kLoudRms` (SOFT/LOUD boundary) — pick from prior threshold calibration or set to a reasonable metal value
- Exact `kHoldSoftSec` and `kHoldLoudSec` values — reuse prior verse/chorus hold times as a starting point
- Whether to remove spectral centroid from `computeDesiredState()` signature or just stop using it inside the function (keeping signature compat for callers is fine either way)

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — **TYPE-01**, **TYPE-02** (the two requirements for this phase)
- `.planning/ROADMAP.md` — Phase 21 goal, success criteria, and dependency note

### Source files with enum/field breakage sites
- `src/analysis/StructureTagger.h` — enum definition to replace; hold constant declarations
- `src/analysis/StructureTagger.cpp` — `computeDesiredState()` and per-state hold logic
- `src/analysis/FeatureVector.h` — `policyGenreIndex` field to remove; `state` field type
- `src/analysis/StructureHoldSmoother.h` / `.cpp` — switch statements over `StructureState` values
- `src/inference/OnnxStructureInference.cpp` — `stateFromClassIndex()` to update
- `src/inference/PolicyPatternMapper.cpp` — `policyGenreIndex` usage to remove
- `src/inference/RuleBasedInference.cpp` — switch/match over `StructureState` values
- `src/AccompanimentEditor.cpp` — may display state label string
- `src/AccompanimentProcessor.cpp` — references to structure state

### Project state
- `.planning/STATE.md` — architectural constraints for v0.4.0 (TYPE-01/02 must complete before any model retrain)

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`kSilentRms` constant** in `StructureTagger` — keep as-is for the SILENT floor; only the upper thresholds need re-mapping
- **`StructureHoldSmoother`** — already switches over `StructureState` values; will need updating but the class interface stays the same

### Established Patterns
- All switch statements over `StructureState` are `src/`-internal; the compiler will enumerate every stale case after the enum change — use `-Werror` / `-Wswitch-default` if available
- `FeatureVector` is a plain struct (`src/analysis/FeatureVector.h`) with no serialization layer — field removal is a clean compile-break with no migration path needed
- `IInference::selectPattern` takes a `const FeatureVector&` — removing a field is ABI-local (no DAW plugin binary format concern)

### Integration Points
- **Rule-based path** (`RuleBasedInference`) — primary CI target; must work end-to-end after enum change with `MA_ENABLE_ONNX=OFF`
- **ONNX path** (`OnnxStructureInference`) — compile-only target for Phase 21; functional correctness gated on Phases 22–23

</code_context>

<specifics>
## Specific Ideas

- User explicitly wants BREAKDOWN as a concept abolished — no "BREAKDOWN maps to X" rule, just let thresholds fall where they fall. This is a clean energy-only model.
- User chose pure RMS over dual-axis: this is a deliberate simplification, not an oversight. Don't add centroid logic back in.

</specifics>

<deferred>
## Deferred Ideas

None — discussion stayed within phase scope.

</deferred>

---

*Phase: 21-cpp-type-foundation*
*Context gathered: 2026-04-28*
