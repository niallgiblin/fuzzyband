# Milestones — Metal Accompaniment

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
