# Phase 28: Beat tracker & bass sequencer - Context

**Gathered:** 2026-04-29  
**Status:** Ready for planning

<domain>
## Phase Boundary

**Tempo:** Introduce a **beat-tracking path** (roadmap: autocorrelation + dynamic programming flavor, **1-bar lock** as design target) as the **primary** BPM/beat-phase source on clean input. **IOI-median** may remain only as an **explicit fallback** when **beat confidence is low**, with **automated tests for both** paths (**RHY-TEMPO-01**).

**Evidence:** Controlled tests (primarily **synthetic** click/metronome-style signals) show BPM stability within **±5 BPM** vs ground truth where the project expects it (**RHY-TEMPO-02**).

**Bass:** Fix the **bar-aligned generative bass** path so **multiple 16th steps** in a bar produce note-ons — not only step 0 (**RHY-BASSSEQ-01**), by anchoring steps to **bar phase** and the **same BPM/beat clock** as drums.

**Out of scope for Phase 28:** `FeatureVector` layout changes, lock-free queue payload expansion, directional fills, unified bar-boundary policy with pattern changes (**Phase 29**), ONNX/training (**Phase 30**).

</domain>

<decisions>
## Implementation Decisions

### Beat tracker integration (module boundary)
- **D-28-01:** **Planner / implementation discretion** on whether beat tracking lives in a **new class** (e.g. `BeatTracker`) vs **inside `OnsetDetector`**. Constraint: **real-time safe**, **no audio-thread allocation**, call sites stay maintainable; **spectral-flux onsets** may still be needed for feature/analysis paths depending on planner research.

### IOI-median vs beat (RHY-TEMPO-01)
- **D-28-02:** **Beat tracker is primary** for BPM/phase when it is **usable**.
- **D-28-03:** **IOI-median remains an explicit fallback** when **beat confidence is low** (threshold and confidence definition left to planner). **Both paths must have automated test coverage** — no silent dead fallback.

### Start / count-in gating
- **D-28-04:** **Replace** the current **four-onset count-in gate** with a **beat-tracker-based gate** (e.g. require **lock** and/or **confidence threshold** before drums/bass are allowed to start). Exact threshold names and hysteresis are planner details; behavior goal: **join the player in time** on clean input without relying on IOI-only count-in.

### Bass 16-step grid (RHY-BASSSEQ-01)
- **D-28-05:** **Bar-aligned grid:** step `k` fires at **bar start + k × (bar duration / 16)** in the **shared musical clock** (samples derived from BPM and **bar phase**), **not** from **block-local** sample origins. Use the **same BPM / beat progression** as the drum path in this phase.
- **D-28-06:** Implement so a **regression test** can prove **multiple distinct steps** (e.g. steps 0 and 4 or 0 and 8) emit **note-ons** within one bar for a controlled pattern.

### Phase 28 vs 29 API boundary (RHY-FEAT-01 deferred)
- **D-28-07:** **No new fields** on `FeatureVector` and **no** lock-free queue payload expansion in Phase 28.
- **D-28-08:** **Allowed:** **processor-level debug readouts** (e.g. BPM, beat confidence, beat phase) for **tests and/or UI**, if they stay **cheap** (atomics / existing display paths — **no new audio-thread allocation**).

### Verification strategy (RHY-TEMPO-02)
- **D-28-09:** **Primary automated evidence:** **synthetic** click or metronome-style buffers (or tiny generated WAV fixtures), asserting **±5 BPM** (or project-defined tolerance) against known tempo.
- **D-28-10:** **Real clean-guitar golden files in repo** are **not required** for Phase 28 closure; optional manual Reaper checks can be documented separately if useful.

### Claude's Discretion
- **`BeatTracker` vs `OnsetDetector` merger** per D-28-01.
- Exact **confidence metric**, **fallback threshold**, and **1-bar lock** DSP details.
- Whether **debug getters** surface on the editor **BPM label** only vs dedicated debug fields.
- Specific **Catch2** test layout (new test file vs extending onset tests).

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & roadmap
- `.planning/ROADMAP.md` — Phase 28 goal, success criteria, v0.5.0 dependency order (28 → 29 → 30)
- `.planning/REQUIREMENTS.md` — **RHY-TEMPO-01**, **RHY-TEMPO-02**, **RHY-BASSSEQ-01**

### Product & constraints
- `.planning/PROJECT.md` — latency/threading/no audio-thread blocking
- `plugin/README.md` — pre-FX hosting (feature fidelity; Phase 27 canonical)

### Research / planning index
- `.planning/research/rhythmic-coherence/README.md` — milestone index; no extra algorithm spec in this clone

### Prior phase context
- `.planning/phases/27-rhythmic-coherence-documentation/27-CONTEXT.md` — doc architecture; DSP phases reference same hosting story

### Code touchpoints (planning truth)
- `src/analysis/OnsetDetector.h` / `src/analysis/OnsetDetector.cpp` — current IOI-median tempo + flux onsets
- `src/AccompanimentProcessor.cpp` — count-in gate, `FeatureVector` fill, `PatternPlayer` wiring
- `src/midi/PatternPlayer.h` / `src/midi/PatternPlayer.cpp` — beat clock, **generative 16-step bass** path
- `tests/` — extend or add coverage for tempo + bass regression

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- **`OnsetDetector`**: Spectral-flux onset + IOI ring + BPM quantize/lock — fallback or onset source for features.
- **`PatternPlayer`**: `beatPosition`, `sampleCounter`, `setGenerativeBassSteps`, 16-step emission loop — **timing should use global bar/beat phase**, not **block-anchored** step times.
- **`AccompanimentProcessor`**: Count-in (`countInOnsetCount` / `snapToBarStart()`), pushes BPM into `FeatureVector`, drives `PatternPlayer::process`.

### Established Patterns
- BPM clamp **80–220** in `OnsetDetector`; plugin-wide soft smoothing in `PatternPlayer::setBpm` (0.1 coupling) — beat tracker integration must remain **real-time safe**.

### Integration Points
- Replace or demote **count-in** path when **beat confidence / lock** is available.
- **`fv.bpm`** continues to reflect **authoritative display/processing** BPM in Phase 28 without expanding `FeatureVector` (D-28-07).

</code_context>

<specifics>
## Specific Ideas

- Roadmap narrative mentions **autocorrelation + DP** and **1-bar lock** as the **design target** for the new tracker — planner/researcher should map that to a concrete, testable implementation.
- User chose **synthetic-first** verification; human jam remains valuable but **not** the Phase 28 automated gate.

</specifics>

<deferred>
## Deferred Ideas

- **Committed clean-guitar golden WAVs** in-repo — optional future enhancement if synthetic tests leave gaps.
- **`FeatureVector` +5** and unified bar-boundary **fill** behavior — **Phase 29** per roadmap.

None — discussion stayed within phase scope

</deferred>

---
*Phase: 28-beat-tracker-bass-sequencer*  
*Context gathered: 2026-04-29*
