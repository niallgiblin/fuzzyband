# Retrospective — Metal Accompaniment

Living document; append new milestone sections at the top of the milestone history.

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
| v0.2.0 | 2026-04-16 — 2026-04-17 | 8 (9–16) | Phase 2 ML + generative stack; 18 plan summaries |
| v0.1.0 | 2026-04-16 — 2026-04-17 | 8 (1–8) | Import + close in two calendar days of recorded GSD history |

---

*Started at v0.1.0 milestone close.*
