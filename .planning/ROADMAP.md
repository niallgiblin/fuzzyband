# ROADMAP — Metal Accompaniment

> **Milestone 1**: Phase 1 — Rule-Based MVP  
> **Goal**: A playable JUCE VST3/AU plugin that listens to a guitarist and triggers rhythmically appropriate drum and bass MIDI in real time — no ML required.  
> **Status**: In progress — Phases 1–6 complete, Phase 7 in progress, Phase 8 pending.

---

## Phases

| # | Phase | Goal | Requirements | Status |
|---|-------|------|--------------|--------|
| 1 | Project Scaffold | Buildable, loadable plugin with CMake + CI | SCAF-01–05 | ✓ Complete |
| 2 | Onset & Tempo | Detect guitar attacks; derive stable BPM | ONSET-01–04 | ✓ Complete |
| 3 | Energy & Structure | Classify guitar state (SILENT/VERSE/CHORUS/BREAKDOWN) | ENERGY-01–03 | ✓ Complete |
| 4 | MIDI Pattern Library | Author drum + bass patterns; offline audition tool | MIDI-01–05 | ✓ Complete |
| 5 | MIDI Output & DAW Routing | Wire PatternPlayer to MidiBuffer; verify in Reaper | OUT-01–05 | ✓ Complete* |
| 6 | Feature→Pattern Inference | Connect analysis to pattern selector via IInference | INFER-01–04 | ✓ Complete |
| 7 | Integration & Stability | Profile, tune, fix edge cases; 20-min session clean | STAB-01–04 | 🔄 In Progress |
| 8 | Docs & Phase 2 Handoff | Doxygen, CONTRIBUTING.md, v0.1.0 tag, Phase 2 issues | DOCS-01–05 | ⬜ Pending |

*Phase 5 code complete; DAW session setup is user verification step.

---

## Phase Details

### Phase 1: Project Scaffold ✓
**Goal**: Get a buildable, loadable JUCE plugin with correct CMake structure.  
**Requirements**: SCAF-01, SCAF-02, SCAF-03, SCAF-04, SCAF-05  
**Success criteria**:
1. Plugin builds VST3 + AU targets via CMake on macOS
2. Plugin loads in Reaper, shows blank UI, processes silence without crashing
3. GitHub Actions CI passes on macOS runner

---

### Phase 2: Onset & Tempo ✓
**Goal**: Detect when the guitarist hits notes and derive a stable tempo estimate.  
**Requirements**: ONSET-01, ONSET-02, ONSET-03, ONSET-04  
**Success criteria**:
1. Plugin logs a stable BPM estimate while playing a steady down-picked riff
2. Unit test: synthetic click @ 160 BPM → tracker converges within 4 beats
3. BPM estimate stays within ±5 BPM of actual tempo

---

### Phase 3: Energy & Structure ✓
**Goal**: Classify the current guitar state so the pattern selector knows what section it's in.  
**Requirements**: ENERGY-01, ENERGY-02, ENERGY-03  
**Success criteria**:
1. Debug UI shows correct state (SILENT/VERSE/CHORUS/BREAKDOWN)
2. Quiet → heavy → quiet riff transitions the label correctly
3. No flickering (2s hysteresis holds)

---

### Phase 4: MIDI Pattern Library ✓
**Goal**: Author the drum and bass patterns that the analysis layer will trigger.  
**Requirements**: MIDI-01, MIDI-02, MIDI-03, MIDI-04, MIDI-05  
**Success criteria**:
1. At least 3 drum patterns per structural state (9+ total)
2. Corresponding bass patterns per state
3. Offline MIDI export produces auditionable `.mid` file
4. Patterns trigger correctly via debug button in UI

---

### Phase 5: MIDI Output & DAW Routing ✓*
**Goal**: Wire the pattern player into the JUCE MidiBuffer and confirm DAW routing works.  
**Requirements**: OUT-01, OUT-02, OUT-03, OUT-04, OUT-05  
**Success criteria**:
1. Plugin drives a drum VSTi in Reaper at correct rhythmic positions
2. Drum MIDI on channel 10, bass MIDI on channel 2
3. Pattern transitions are smooth (quantised, not abrupt)
4. Humanisation applied (velocity ±10, timing ±2ms)

---

### Phase 6: Feature→Pattern Inference ✓
**Goal**: Connect the analysis outputs to the pattern selector so the system reacts to the guitarist.  
**Requirements**: INFER-01, INFER-02, INFER-03, INFER-04  
**Success criteria**:
1. Switching from quiet picking to heavy riff changes drum pattern within ~200ms
2. SILENT state stops all MIDI output (note-off flush)
3. Inference runs on background thread at ~50Hz without audio thread blocking

---

### Phase 7: Integration & Stability 🔄
**Goal**: Full system running together — find and fix all rough edges.  
**Requirements**: STAB-01, STAB-02, STAB-03, STAB-04  
**Plans:** 5 plans

Plans:
- [x] 07-01-PLAN.md — StructureTagger per-state hysteresis hold times (D-06, D-07)
- [x] 07-02-PLAN.md — BPM-adaptive note durations + D-09 humanization/overlap tests
- [x] 07-03-PLAN.md — AccompanimentProcessor hardening: NaN guard, thread pause/resume, bypass flush, debug atomics
- [x] 07-04-PLAN.md — Editor debug UI: RMS / Centroid / HF Flux labels
- [x] 07-05-PLAN.md — Release build, jam session tuning, CPU profile, TSan, edge cases, 20-minute stability

**Success criteria**:
1. 10-minute metal jam session recorded and profiled
2. CPU < 15% on M-series Mac at 256-sample buffer
3. Bypass, sample rate change, buffer change, transport stop/start all handled gracefully
4. 20-minute session with no crashes and no xruns

---

### Phase 8: Docs & Phase 2 Handoff ⬜
**Goal**: Leave Phase 1 in a state that makes Phase 2 straightforward to begin.  
**Requirements**: DOCS-01 (done), DOCS-02, DOCS-03, DOCS-04, DOCS-05  
**Success criteria**:
1. Doxygen comments on all public class interfaces
2. CONTRIBUTING.md covers macOS build instructions
3. `v0.1.0-phase1` tag on GitHub
4. GitHub Issues opened for all Phase 2 work items

---

## Risk Register

| Risk | Likelihood | Mitigation |
|------|-----------|------------|
| Onset detector fires on noise/silence | Medium | Noise gate threshold + minimum energy floor |
| Tempo tracker unstable on slow riffs | Medium | Clamp IOI range, median filter, fallback to last stable BPM |
| Palm mute misclassified as SILENT | Medium | Spectral centroid distinguishes mute from silence |
| Pattern transitions sound jarring | Low-Medium | Quantise transitions to bar boundaries |
| Plugin crashes on buffer size change | Low | Reset all state in `prepareToPlay()` |
| Latency too high | Low | Background thread inference keeps audio thread unblocked |

---

## Milestone 2 Preview — Phase 2: ML + Generative

| Phase | Goal |
|-------|------|
| 2.1 | ONNX Runtime integration + OnnxInference drop-in |
| 2.2 | Pitch/chord detection (YIN or CREPE) |
| 2.3 | ML structure classifier |
| 2.4 | Generative bass line model (transformer → ONNX) |
| 2.5 | Plugin UI with genre/intensity controls |
| 2.6 | Python training pipeline + metal MIDI dataset |

> **Note (2026-04-16):** M2 direction is under revision — see Backlog 999.1. Likely shift from on-device ONNX to cloud inference with a fine-tuned Anticipatory Music Transformer. The table above will be re-scoped when M2 is opened.

---

## Backlog

### Phase 999.1: ML training data feasibility research (BACKLOG)

**Goal:** Identify and license-audit candidate training datasets for a fine-tuned metal drum/bass accompaniment model (M2 precursor). Produce a go/no-go feasibility memo recommending the primary dataset path.

**Requirements:** TBD

**Plans:** 0 plans

Plans:
- [ ] TBD (promote with `/gsd-review-backlog` when ready)

**Captured context (from discussion 2026-04-16):**

- **Base model candidate:** Anticipatory Music Transformer (Thickstun et al., Stanford) — pretrained on Lakh MIDI, designed for symbolic accompaniment / infilling with partial-track conditioning. Fine-tuning on a metal-weighted subset looks tractable on a single A100.
- **Deployment target:** cloud GPU inference (on-device not a requirement). Rate-limit cloud calls to 2–5 Hz without perceptible UX loss.
- **Dataset shortlist to evaluate:**
  - Lakh MIDI Dataset (~170k files, free, clean licensing, AMT's pretraining set)
  - GuitarPro tab scrapes (huge volume, legally gray — TOS violations common)
  - Groove MIDI Dataset (Magenta — paired drum performances, not metal but teaches groove)
  - Slakh2100 (synthetic multi-track from MIDI, genre-limited)
  - Commissioned recordings (20–50 hrs of drummers accompanying guitar tracks — expensive but cleanest signal)
- **Experience decisions already made:**
  - Pure audio-reactive + optional **session seed** (preset dropdown for subgenre/tempo); no in-session chatbot
  - Reactive **fill-cue detection** on a 2-bar rolling window, not long-horizon structure prediction (real drummers sense the windup, don't predict sections)
  - **Session-adaptive style embedding** first (typical IOI, RMS envelope, chord-root histogram); persisted per-user LoRA deferred
  - Avoid audio-generative models (MusicGen, Riffusion, Suno) — they emit waveforms, not MIDI, and don't fit the `IInference` contract
- **Remaining M2 scope** (context for planning, *not* this phase): preprocessing pipeline (MIDI → AMT token format), fine-tuning pipeline + eval harness, cloud inference service (latency budget, rate-limiting, cost model), plugin `CloudInference` class implementing `IInference` with graceful fallback to rule-based, eval & iteration (listening tests, fill-cue accuracy, seed adherence, per-user style embedding).
- **Open questions for this phase:** which datasets clear license audit, how much paired guitar→drums coverage exists vs. needs commissioning, tokenization compatibility with AMT's vocabulary.
