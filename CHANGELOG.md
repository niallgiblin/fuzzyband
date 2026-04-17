# Changelog

All notable changes to this project are documented here. For architecture and threading, see [`ARCHITECTURE.md`](ARCHITECTURE.md).

## [v0.1.0-phase1] — Phase 1 rule-based MVP

**Summary:** Real-time guitar onset/tempo tracking, energy/structure classification, rule-based `IInference` → drum/bass MIDI via `PatternPlayer`. Phase 2 can replace inference with ONNX or other backends behind the same `IInference` interface.

**Highlights**

- JUCE 8 VST3 + AU; lock-free handoff from audio thread to background inference (~50 Hz)
- `RuleBasedInference` selects patterns from structure state and BPM; `OnnxInference` stub for Phase 2
- Unit tests for onset, structure tagger, pattern player, and rule inference

**Known limitations (Phase 1)**

- Bass pitch fixed (no real-time pitch tracking yet)
- Structure is threshold/rule-based, not ML-classified
- Plugin UI is debug-oriented; full parameter UI is Phase 2

### Tagging this release

After documentation and CI are green on `main`:

```bash
git tag -a v0.1.0-phase1 -m "Phase 1 rule-based MVP — IInference stable for Phase 2 swap-in"
git push origin v0.1.0-phase1
```

Then open GitHub Issues for Phase 2 work using `docs/PHASE2_GITHUB_ISSUES.md` as a checklist.
