# Documentation Index

**Last rewritten:** 2026-09-14, at v1.0.3 (`d1fc62a`).

This index tells you which document is authoritative for what. Read it before
opening anything else at random — this repository has 577 markdown files, and a
large fraction of them are stale, duplicated, vendored, or archived.

---

## 1. Start here

| If you want to… | Read |
|---|---|
| Understand modes, controls, and decisions in plain English | [`../RULES.md`](../RULES.md) |
| Understand the project and fix a bug | [`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md) |
| Avoid the traps that keep recurring | [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md) |
| Work on the bass mirroring bug | [`BASS_MIRRORING.md`](BASS_MIRRORING.md) (early §§ stale vs v1.0.19; prefer `RULES.md` §7) |
| Know what was already tried and failed | [`PROJECT_TIMELINE.md`](PROJECT_TIMELINE.md) |
| Know what the tests cover | [`TEST_AUDIT.md`](TEST_AUDIT.md) |
| Understand the runtime architecture | [`/ARCHITECTURE.md`](../ARCHITECTURE.md), evidence in [`ARCHITECTURE_DETAIL.md`](ARCHITECTURE_DETAIL.md) |
| Get the coding-agent rules | [`/AGENTS.md`](../AGENTS.md) |

---

## 2. The canonical set (treat these as current)

| Doc | Status | What it is |
|---|---|---|
| [`../RULES.md`](../RULES.md) | **NEW** (v1.0.19) | Domain/rules guide for non-experts: modes, controls, decision flow, bass pitch/beat contract, musicality research |
| [`CONTEXT_HANDOFF.md`](CONTEXT_HANDOFF.md) | **NEW** | Self-contained briefing: what the project is, real architecture, build/test, the active bug, what is *not* wired |
| [`PITFALLS_AND_INVARIANTS.md`](PITFALLS_AND_INVARIANTS.md) | **NEW** | Cross-wiring causes, recurring engineering traps, the ten invariants, a pre-flight checklist |
| [`BASS_MIRRORING.md`](BASS_MIRRORING.md) | **NEW** | The mirroring contract, exact call chain, all nine fix attempts, ranked hypotheses, debug recipe |
| [`PROJECT_TIMELINE.md`](PROJECT_TIMELINE.md) | **NEW** | 12 eras, 13-row repeated-regression table, failed approaches, source conflicts |
| [`TEST_AUDIT.md`](TEST_AUDIT.md) | **NEW** | Execution results, per-file inventory, six coverage gaps, tests that encode bugs, CI holes |
| [`ARCHITECTURE_DETAIL.md`](ARCHITECTURE_DETAIL.md) | **NEW** | Source-verified architecture with `file:line` citations, ~200 doc-vs-code contradictions |
| [`../ARCHITECTURE.md`](../ARCHITECTURE.md) | **REWRITTEN** | Short, correct architecture; the previous version had material errors |
| [`../README.md`](../README.md) | **UPDATED** | Public face; was 74 versions stale and listed controls that do not exist |
| [`../AGENTS.md`](../AGENTS.md) | **CORRECTED** | The one canonical agent-instruction file (banner + phantom-class fixes) |
| [`../CLAUDE.md`](../CLAUDE.md) | **REPLACED** | Now a pointer to `AGENTS.md`; it was a stale near-duplicate |
| [`../CHANGELOG.md`](../CHANGELOG.md) | current | Narrative history. **Not** a reliable version-order record — see `PROJECT_TIMELINE.md` §0.3 |
| [`RELEASING.md`](RELEASING.md) | current | How to cut a release; the download site self-populates |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | mostly current | Source build and tests |

---

## 3. Specialist docs (current-ish, read with care)

| Doc | Status | Note |
|---|---|---|
| [`MUSICALITY_ROCK_PIVOT_PLAN.md`](MUSICALITY_ROCK_PIVOT_PLAN.md) | mostly current | Its own header says "Date: 2025" — a typo; the work is 2026 |
| [`TIER1_GROOVE_MODEL_CONTRACT.md`](TIER1_GROOVE_MODEL_CONTRACT.md) | mostly current | **Caveat:** the Tier-1 `GrooveRenderer` it specifies is fully implemented but **never instantiated** in the live path |
| [`TIER0_ORNAMENTATION_SPEC.md`](TIER0_ORNAMENTATION_SPEC.md) | mostly current | Ornaments are now behind the `humanize` parameter |
| [`ROCK_PATTERN_RENDER.md`](ROCK_PATTERN_RENDER.md) | mostly current | Rock-first pattern routing |
| [`LABEL_TAXONOMY.md`](LABEL_TAXONOMY.md) | mostly current | Training label taxonomy |
| [`TOKENIZATION.md`](TOKENIZATION.md) | mostly current | MIDI tokenization for training |
| [`SINGLE_NOTE_DETECTION_PLAN.md`](SINGLE_NOTE_DETECTION_PLAN.md) | mostly current | Plan document |
| [`FEATURE_CAPTURE.md`](FEATURE_CAPTURE.md) | mostly current | **Caveat:** `FeatureCapture` is compiled **only into the test binary**, not the plugin |
| [`DATA_STRATEGY.md`](DATA_STRATEGY.md) | **partially stale** | Says 22 model classes; the live count is **28** |
| [`ONNX_IO.md`](ONNX_IO.md) | **stale** | Contract for a path that is not wired into the plugin |
| [`PLUGIN_GUIDE.md`](PLUGIN_GUIDE.md) | **partially stale** | Tempo / onset-tracking sections describe a deleted pipeline |
| [`PLUGIN_HOSTING.md`](PLUGIN_HOSTING.md) | not re-verified | Short DAW routing guidance |
| [`END_USER_STRESS_TEST.md`](END_USER_STRESS_TEST.md) | **partially stale** | Written for v0.9.62; still useful as a playtest script |
| [`../training/README.md`](../training/README.md) | mostly current | Training pipeline |
| [`../tests/fixtures/README.md`](../tests/fixtures/README.md) | **partially stale** | Describes the 0.1 s RMS window; the engine now uses 0.02 s |
| [`../data/MANIFEST.md`](../data/MANIFEST.md) | mostly current | Data corpus manifest |

---

## 4. Archived — superseded, do not cite

Moved to [`archive/`](archive/) on 2026-09-14, each with a dated banner stating
what replaced it. Kept for history only.

| Archived file | Why |
|---|---|
| [`archive/IMPLEMENTATION_PLAN-phases-0-9.md`](archive/IMPLEMENTATION_PLAN-phases-0-9.md) | Phases 0–9 shipped in v0.9.68–v1.0.0-rc; the plan is complete |
| [`archive/PLAYABILITY_REVIEW-v0.9.67.md`](archive/PLAYABILITY_REVIEW-v0.9.67.md) | Its 23 defects were remediated in Phases 0–9 |
| [`archive/PHASE9_ACCEPTANCE-1.0.0-rc.md`](archive/PHASE9_ACCEPTANCE-1.0.0-rc.md) | Acceptance report for the rc; suite has since grown |
| [`archive/SECTION_TRANSITIONS-design.md`](archive/SECTION_TRANSITIONS-design.md) | Superseded by the implemented transition cycle |
| [`archive/BASS_ONNX_IO-retired.md`](archive/BASS_ONNX_IO-retired.md) | The generative bass ONNX path is not wired |
| [`archive/RUNTIME_ARCHITECTURE-stale.md`](archive/RUNTIME_ARCHITECTURE-stale.md) | Describes non-existent subsystems and an inverted ONNX default |
| [`archive/CODEBASE_WALKTHROUGH-stale.md`](archive/CODEBASE_WALKTHROUGH-stale.md) | Written for the pre-1.0 rule-based codebase |
| [`archive/ONNX_READINESS-stale.md`](archive/ONNX_READINESS-stale.md) | A PASS gate whose evidence artifact is missing |
| [`archive/ARCHITECTURE-pre-1.0.3.md`](archive/ARCHITECTURE-pre-1.0.3.md) | Claimed a `StructureState` that does not exist |
| [`archive/CLAUDE-pre-1.0.3.md`](archive/CLAUDE-pre-1.0.3.md) | Stale near-duplicate of `AGENTS.md` |

---

## 5. Trees you should know about before searching

| Tree | Files | What it is | Trust it? |
|---|---|---|---|
| `docs/` | 24 | Project documentation | Yes — this index |
| `docs/archive/` | 10 | Superseded docs with banners | History only |
| `.planning/` | 410 | **The live planning tree** (phases, plans, summaries, debug logs, research) | Yes for *dated history*; **no file mentions v1.0.x** — it stops at v0.9.67 |
| `.cursor/get-shit-done/` | 173 | **Vendored third-party agent framework** | Not project documentation. Exclude from searches |
| `.MDignore/` | 13 | Deliberately parked pre-2026 planning | History only; the only record of the v0.7.x/v0.8.0 prototype era |
| `.gsd/` | ~326 | **Stale symlink** to `~/.gsd/projects/002bd9a3f840` | **NO.** Its `STATE.md` says the project is blocked on a milestone that finished months ago |
| `build/` | — | Build artefacts | No |

Two warnings that matter:

1. **`.planning/` is gitignored** (`.gitignore:27`) and only ~149 of 417 files are
   force-added. Most planning docs have no commit provenance, and anything you
   write there may not survive a clone. **Durable documentation belongs in `docs/`.**
2. **`grep -r "*.md"` will mostly return vendored framework text.** Scope searches
   to `docs/`, `*.md` at the root, and `ARCHITECTURE.md`.

---

## 6. Known documentation defects still open

These were found in the 2026-09-14 audit and are **not yet fixed**:

- "22 model classes" appears in `DATA_STRATEGY.md`, `README.md`, `AGENTS.md`,
  `ONNX_IO.md`, `END_USER_STRESS_TEST.md`, `training/README.md`,
  `data/MANIFEST.md`, `CONTRIBUTING.md`. The real count is **28**
  (`MidiPatternLibrary.h:40`, `pattern_embeddings.h:19`).
- `.planning/` has missed every release since v0.9.67 and still names v0.9.67 as
  the newest version.
- `.gsd/DECISIONS.md` D005/D006 are architecturally live and have no `.planning/`
  counterpart — migrate them before removing the `.gsd` symlink.
- `tests/fixtures/README.md` still documents the pre-1.0.3 0.1 s RMS window.
- The generated `GSD:*` sections in `AGENTS.md` will be overwritten if
  `generate-*-profile` is re-run; the banner at the top of that file explains what
  to re-check.
