# Milestones — Metal Accompaniment

## v0.2.0 — Phase 2 ML + Generative

**Shipped:** 2026-04-17  
**Phases:** 9–16 (8 phases)  
**Plans with summaries:** 18

### Summary

Shipped optional ONNX inference with a frozen I/O contract and CI guardrails, YIN-based pitch and adaptive bass routing, ML structure with rule fallback, generative bass with validation and rank/select, APVTS-backed plugin UI, a reproducible Python training stub with ONNX export validation, and Terraform + scripts for versioned model storage and promotion.

### Key accomplishments

1. **Data & ONNX foundation** — Dataset audit and tokenization docs; `training/prep_midi.py`; `docs/ONNX_IO.md` and audio-thread checks (Phase 9–10).
2. **Realtime intelligence** — Pitch/harmony, ML structure, generative bass integration without blocking the audio thread (Phase 11–13).
3. **Product surface** — Policy parameters and editor layout with documentation (Phase 14).
4. **Train → ship loop** — Pinned Python deps, stub training, dual ONNX export, contract validation script (Phase 15).
5. **Ops** — S3 + GitHub OIDC Terraform, promote/download scripts, optional CI model fetch (Phase 16).

### Stats (approximate)

- **Window:** 2026-04-16 — 2026-04-17 (GSD-recorded execution)
- **Plans:** 18 with summaries (phases 9–16)

### Pre-flight note

No `.planning/v0.2.0-MILESTONE-AUDIT.md` on file — optional `/gsd-audit-milestone` next time for a formal audit trail. Open-artifact audit (`audit-open`) was clear at close.

### Known deferred items at close

Same operational follow-ups as v0.1.0 where still pending: push legacy tag, GitHub issues checklist, optional DAW smoke. See `STATE.md` → **Deferred Items**.

---

## v0.1.0 — Phase 1 rule-based MVP

**Shipped:** 2026-04-17  
**Phases:** 1–8 (8 development phases)  
**Plans with summaries:** 6 (Phase 7: five plans; Phase 8: one plan)

### Summary

Shipped a JUCE 8 VST3/AU plugin that listens to guitar audio and drives drum and bass MIDI in real time via rule-based onset/tempo/structure analysis and `IInference` → `PatternPlayer`, with lock-free threading to the audio path, CI on macOS, Doxygen/CONTRIBUTING/CHANGELOG handoff, and Phase 2 issue checklist.

### Key accomplishments

1. **Scaffold & analysis** — CMake VST3/AU, CI, onset/tempo tracker with unit test, energy/structure classification with debug UI.
2. **MIDI core** — Pattern library, offline export, `PatternPlayer` with humanisation and channel routing.
3. **Inference boundary** — `RuleBasedInference` + background thread; `IInference` ready for Phase 2 ONNX drop-in.
4. **Integration & stability** — Phase 7 plans: tagger tuning, BPM-adaptive durations, processor hardening, debug metrics, release/TSan/stability/CPU evidence (see `.planning/phases/07-integration-stability/`).
5. **Documentation handoff** — Doxygen target, CONTRIBUTING, CHANGELOG tagging instructions, `docs/PHASE2_GITHUB_ISSUES.md` checklist.

### Stats (approximate)

- **Git range:** `0c306ca` → `HEAD` (first import through milestone close)
- **Commits on `main`:** 21+ (milestone window)
- **Diff (first commit..HEAD):** ~104 files, +12k LOC (per `git diff --stat`)

### Known deferred items at close

Operational follow-ups (not blocking code archive):

| Item | Notes |
|------|--------|
| Push tag | `git push origin v0.1.0-phase1` may still be pending |
| GitHub issues | Create issues from `docs/PHASE2_GITHUB_ISSUES.md` |
| Optional DAW smoke | Reaper session confirmation for OUT-05 if desired |

See `STATE.md` → **Deferred Items** for the canonical list.

---

*Milestone entry created at v0.1.0 close (`/gsd-complete-milestone`).*
