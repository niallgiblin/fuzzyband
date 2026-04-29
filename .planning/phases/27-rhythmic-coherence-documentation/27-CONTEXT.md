# Phase 27: Rhythmic documentation - Context

**Gathered:** 2026-04-29  
**Status:** Ready for planning

<domain>
## Phase Boundary

Documentation only: one authoritative **pre-FX** hosting story for the plugin (signal order, failure modes when post-FX), discoverable links from **root `README.md`** and **`docs/`**, and a resolvable pointer to **rhythmic-coherence research** under `.planning/research/`. Satisfies **RHY-DOC-01** and **RHY-DOC-02**. No DSP, ONNX, or `FeatureVector` changes in this phase.

</domain>

<decisions>
## Implementation Decisions

### Doc structure & information architecture
- **D-01:** **Canonical** user-facing hosting doc is **`plugin/README.md`** (new file). It holds insert order (guitar → plugin → FX/amp), post-FX failure modes, Reaper-oriented routing reminder, and links to research + roadmap.
- **D-02:** **Root `README.md`** gets a dedicated short section (near top, after intro or before “Version & roadmap”) — e.g. **“Plugin hosting (insert order)”** — that **only summarizes** pre-FX in 1–2 sentences and **links to `plugin/README.md`** for the full story (avoid duplicating long prose in two places).
- **D-03:** Add **`docs/PLUGIN_HOSTING.md`** as a **thin entry point** (~1 screen): same 1–2 sentence summary + bullet link to `plugin/README.md` + link to `.planning/research/rhythmic-coherence/` + `.planning/ROADMAP.md` (v0.5.0 block). Satisfies **RHY-DOC-02** for contributors who browse `docs/` first.

### Research anchor (empty folder in this clone)
- **D-04:** Create **`.planning/research/rhythmic-coherence/README.md`** as a **stub index**: states purpose (v0.5.0 rhythmic coherence milestone), points to **`.planning/ROADMAP.md`** § v0.5.0 / Phase 27–30, and notes that detailed artifacts may live elsewhere until synced — **no fake technical content**. `plugin/README.md` links here by relative path from repo root: `.planning/research/rhythmic-coherence/`.

### Audience & tone
- **D-05:** **`plugin/README.md`** uses a **dual layout**: (1) **Quick start** — plain language for guitarists (“where do I put this plugin?”); (2) **Why pre-FX** — short technical rationale (onset/tempo/feature fidelity); (3) **Contributors** — pointers to `CONTRIBUTING.md`, `ARCHITECTURE.md`, ONNX docs as needed. Reaper remains **primary** DAW example; keep other hosts **agnostic** unless a one-line note suffices.

### Root / contributor doc accuracy (explicit Phase 27 sweep)
- **D-06:** Phase 27 **includes** updating **stale parameter documentation** to match current **`AccompanimentProcessor::createParameterLayout()`**: `outputGain`, `intensity`, `structureBlend`, `generativeBassMode` only — **remove `genre` and `variation`** (and any **policyGenreIndex** / genre-mapping language) from **root `README.md`** and **`CONTRIBUTING.md`** where they no longer apply.
- **D-07:** Refresh root **`README.md` milestone snapshot** line so it reflects **current** milestone (e.g. v0.5.0 / Phase 27 next) instead of outdated “Phase 18–20 are next” style text — without rewriting the whole file.

### Claude's Discretion
- Exact section titles and paragraph order inside `plugin/README.md`; optional ASCII diagram of FX chain; exact wording of post-FX symptom list; whether to add one-line Logic/Ableton hints after Reaper section.

</decisions>

<canonical_refs>
## Canonical References

**Downstream agents MUST read these before planning or implementing.**

### Requirements & roadmap
- `.planning/REQUIREMENTS.md` — **RHY-DOC-01**, **RHY-DOC-02**
- `.planning/ROADMAP.md` — Phase 27 goal, success criteria, v0.5.0 dependency notes

### Product & constraints
- `.planning/PROJECT.md` — core value, performance/threading constraints (docs must not imply blocking audio thread or post-FX being “supported”)

### Code truth for parameter docs
- `src/AccompanimentProcessor.cpp` — `createParameterLayout()` (authoritative APVTS IDs)

### Existing docs to align or link
- `README.md` — entry point; add hosting section + accuracy pass per D-06, D-07
- `CONTRIBUTING.md` — APVTS subsection must match layout (D-06)
- `ARCHITECTURE.md` — optional link from `plugin/README.md` for threading overview

</canonical_refs>

<code_context>
## Existing Code Insights

### Reusable Assets
- No `plugin/README.md` yet — greenfield file for hosting story.
- Root `README.md` already documents build, Reaper scan, tests — extend with hosting link rather than replacing.

### Established Patterns
- Parameter IDs and UI are defined in **`AccompanimentProcessor::createParameterLayout()`**; docs historically lagged (genre/variation removed in code but still in README/CONTRIBUTING).

### Integration Points
- Future beat tracker / bass work (Phases 28–30) will reference the same pre-FX story; keep **`plugin/README.md`** stable as the human-facing anchor.

</code_context>

<specifics>
## Specific Ideas

- User chose to discuss **all** gray areas in one pass; decisions above lock structure, stub research index, dual audience, and explicit README/CONTRIBUTING accuracy sweep.

</specifics>

<deferred>
## Deferred Ideas

- Beat tracker, bass sequencer, `FeatureVector` +5, fills, ONNX retrain — **Phases 28–30** only.

None — discussion stayed within phase scope

</deferred>

---

*Phase: 27-rhythmic-coherence-documentation*  
*Context gathered: 2026-04-29*
