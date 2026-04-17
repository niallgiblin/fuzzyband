# Retrospective — Metal Accompaniment

Living document; append new milestone sections at the top of the milestone history.

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
| v0.1.0 | 2026-04-16 — 2026-04-17 | 8 | Import + close in two calendar days of recorded GSD history |

---

*Started at v0.1.0 milestone close.*
