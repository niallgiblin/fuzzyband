# Retrospective — Metal Accompaniment

Living document; append new milestone sections at the top of the milestone history.

---

## Milestone: v0.4.0 — ML Playability & Simplification

**Shipped:** 2026-04-29  
**Phases:** 6 (numbered 21–26) | **Plans:** 13

### What was built

Three-class energy structure end-to-end; ONNX contract + stubs for structure `[1,3]` and bass `[1,32]`; load-time shape assertions and ML rejection (`excludeIndex`, `patternRejectionCount`); genre-free UI; GMD+Lakh `merge_datasets.py` pipeline; retrained pattern/bass/structure ONNX; promoted `assets/*.onnx`; PVAL-01 Reaper jam recorded in `26-03-SUMMARY.md`.

### What worked

- Compiler-driven enum migration (Phase 21) before contract and data work reduced silent drift between C++ and training labels.
- Single `validate_onnx_contract.py` tri-flag gate stayed the spine from stubs through promoted artifacts.

### What was inefficient

- `audit-open` flagged quick tasks whose summaries used prefixed filenames instead of `SUMMARY.md` — fixed at milestone close.
- Living `REQUIREMENTS.md` checkboxes lagged traceability until archival reconciliation.

### Patterns established

- Merged corpus with preserved `val_gmd.pt` for cross-corpus comparison.
- “Next Pattern” as inference-visible rejection rather than APVTS-driven pattern index.

### Key lessons

- Resolve `audit-open` gates before `complete-milestone`, or batch-fix artifact naming (`SUMMARY.md`) once.

### Cost observations

Not tracked for this milestone.

---

## Milestone: v0.3.0 — Real ML Training Pipeline

**Shipped:** 2026-04-19  
**Phases:** 4 (numbered 17–20) | **Plans with summaries:** 10

### What was built

- GMD download + tensor dataset with histogram gate; `PatternNet` / `train_gmd.py` / `pattern_trained.onnx`; `BassNet` / `train_bass.py` / `bass_trained.onnx`; `install-model-local.sh` + README; Reaper verification procedure for EXP-02.

### What worked

- `validate_onnx_contract.py` as a single gate across pattern and bass artifacts kept Python export aligned with frozen docs.
- Phases 18 and 19 could proceed in parallel after Phase 17 tensors existed.

### What was inefficient

- `gsd-sdk query milestone.complete` is not the Homebrew `gsd-sdk`; milestone archival used `node .cursor/get-shit-done/bin/gsd-tools.cjs milestone complete` instead.
- SUMMARY frontmatter sometimes lacked a clean `one-liner`, so `milestone complete` picked noisy “Outcome:” lines for MILESTONES.md until hand-edited.

### Patterns established

- Validate-then-copy shell installer for local `assets/*.onnx` without touching `src/`.
- Human DAW checklist as the acceptance path for “musically sensible” behavior (EXP-02).

### Key lessons

- Reconcile `REQUIREMENTS.md` checkboxes against phase SUMMARYs before close — DATA-04/05/06 were implemented but boxes had lagged until `requirements mark-complete`.

### Cost observations

Not tracked for this milestone.

---

## Milestone: v0.2.0 — Phase 2 ML + Generative

**Shipped:** 2026-04-17  
**Phases:** 8 (numbered 9–16) | **Plans with summaries:** 18

### What was built

- DATA/ONNX groundwork (`DATASET_AUDIT`, `TOKENIZATION`, `prep_midi`, `ONNX_IO`, threading guardrails); pitch + ML structure + generative bass paths; APVTS UI; Python training stub + ONNX validation; Terraform model bucket and promotion scripts.

### What worked

- Reusing `IInference` kept one contract across rule-based, ONNX, and generative bass.
- Frozen tensor contract (`docs/ONNX_IO.md`) aligned C++ and Python export.
- Phases 9–16 produced parallel SUMMARY artifacts for traceability.

### What was inefficient

- `gsd-sdk` / `milestone.complete` CLI not available locally — milestone close done manually.
- Sidecar `15-onset-robustness` vs roadmap Phase 15 (PYTR) naming collision required explicit roadmap notes.

### Patterns established

- Shell-based CI guard to keep ORT off the audio thread without downloading ORT in default CI.
- Pointer manifest + checksum promotion for model artifacts.

### Key lessons

- Archive living `REQUIREMENTS.md` at milestone boundary so the next milestone starts with a clean sheet.

### Cost observations

Not tracked for this milestone.

---

## Milestone: v0.1.0 — Phase 1 rule-based MVP

**Shipped:** 2026-04-17  
**Phases:** 8 (numbered 1–8) | **Plans with summaries:** 6 (Phase 7 × 5, Phase 8 × 1)

### What was built

- JUCE 8 VST3/AU scaffold with CI; onset/tempo + energy/structure pipeline; MIDI pattern library and `PatternPlayer`; `IInference` + `RuleBasedInference` with lock-free queues; integration hardening and stability evidence (Phase 7); Doxygen, CONTRIBUTING, CHANGELOG, Phase 2 issue checklist (Phase 8).

### What worked

- Clear `IInference` boundary preserved ONNX/ML for Phase 2 without blocking Phase 1 delivery.
- Phase plans with SUMMARY artifacts (7–8) gave traceable evidence for STAB/DOCS closure.
- GSD planning imported mid-project without rewriting shipped code.

### What was inefficient

- Early phases (1–6) lack per-plan phase directories in `.planning/phases/`, so `roadmap analyze` under-reported disk status until later phases.
- Living `REQUIREMENTS.md` lagged phase summaries until milestone close reconciliation.

### Patterns established

- Lock-free handoff (audio → background inference) as a non-negotiable architecture constraint.
- `constexpr` pattern data — no file I/O on the audio path.

### Key lessons

- Close the requirements doc against SUMMARY evidence at phase end to avoid drift.
- Operational release steps (git push, GitHub issues) belong in **Deferred Items** when automation is not in-repo.

### Cost observations

Not tracked for this milestone.

---

## Cross-Milestone Trends

| Milestone | Duration (calendar) | Phases | Notes |
|-----------|---------------------|--------|-------|
| v0.4.0 | 2026-04-28 — 2026-04-29 | 6 (21–26) | Type→contract→inference→UI→data→retrain; PVAL-01 closes ML playable loop |
| v0.3.0 | 2026-04-18 — 2026-04-19 | 4 (17–20) | Real training pipeline + export; EXP-02 human verification deferred |
| v0.2.0 | 2026-04-16 — 2026-04-17 | 8 (9–16) | Phase 2 ML + generative stack; 18 plan summaries |
| v0.1.0 | 2026-04-16 — 2026-04-17 | 8 (1–8) | Import + close in two calendar days of recorded GSD history |

---

*Started at v0.1.0 milestone close.*
