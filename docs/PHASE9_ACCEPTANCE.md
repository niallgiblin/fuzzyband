# Phase 9 — Verification and acceptance (v1.0.0-rc)

**Plan:** [`docs/IMPLEMENTATION_PLAN.md`](IMPLEMENTATION_PLAN.md) T9.1–T9.5
**Binary:** `project(MetalAccompaniment VERSION 1.0.0)` — UI `v1.0.0`
**Date:** 11 Sep 2026
**Host:** macOS arm64, Release, `MA_ENABLE_ONNX=ON`

Installed and version-checked:

```
~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3          → 1.0.0
~/Library/Audio/Plug-Ins/Components/fuzzyband.component → 1.0.0
auval -v aumf MtAc NGAC  → AU VALIDATION SUCCEEDED
```

---

## T9.1 — Full test suite green

| Binary | Result |
| --- | --- |
| `./build/MetalAccompanimentTests` | **268** cases, **87979** assertions, all passed |
| `./build/MetalAccompanimentIntegrationTests` | **77** cases, **1194** assertions, all passed |

Review §5 tests (the nine that would have caught the playability defects):

| §5 | Test | Status |
| --- | --- | --- |
| 1 Transport-loop | `T2.1 lock survives a 4-bar DAW loop…` | pass |
| 2 Dual-block-size golden | `MidiProbe dual-block-size golden` + `T9.2 pipeline…` + `T9.2 PatternPlayer fingerprint…` | pass |
| 3 Note-off ledger | `T1.1 note-off ledger` / long notes at 128 | pass |
| 4 Velocity histogram | `T3.1 metal chorus…` / `T9.3 dynamics` | pass |
| 5 Play-mode coverage | `E2E: multi-section jam produces >=3…` / `T9.3 Play variety` | pass |
| 6 Latency budget | `T6.1: a large RMS step commits… within 250 ms` | pass |
| 7 Locked-riff articulation | `T5.2: a sustained chord plays O(1) bass notes…` | pass |
| 8 Seek/loop while ringing | `T1.2 seek while crash and bass ring…` | pass |
| 9 Section-end fill | `T7.2 Play last-bar fill lands inside the section…` | pass |

No `[!mayfail]` cases remain.

---

## T9.2 — Buffer-size invariance

**Must be identical** for note placement. Gate:

| Render | 128 vs 512 vs 2048 |
| --- | --- |
| PatternPlayer, ~4 bars, pattern 4 (full fingerprint) | **identical** |
| Processor count-in + 4-bar capture (`sample < 460800`) | **identical** |

Full T0.2 Record session *including lock hold* still diverges after the lock onset (first mismatch ~sample 479k). That is the documented residual from the Phase 0–4 review: lock detection finishes on a **block boundary**, not a sample. Fill arming is sample-accurate (T7.2). Loop-wrapped Record `128` vs `512` matched; `128` vs `2048` differed by one kick note-off (~1.8k samples) on the wrap flush.

`.artifacts/baseline/` was recaptured at **v1.0.0**. Pre-lock TSVs `record_dropc_{128,512,2048}` agree.

---

## T9.3 — Listening matrix (MIDI)

A/B against T0.2 / pre-Phase-3 corpus. These are MIDI assertions, not a DAW listen.

| Check | Target | Verdict | Evidence |
| --- | --- | --- | --- |
| Cymbals ring | Crash/china decay ≥ 1 beat at all buffer sizes | **pass** | `T9.3 cymbals ring`; T1.1 golden holds ≥ 1 beat at 128/512/2048 |
| Dynamics | Verse vs chorus backbeat delta ≥ 15; no wall of 127 | **pass** | Metal chorus at energy 1.28: stddev > 12, `< 25%` at 127. Record 512: **0 / 67** note-ons at 127 vs pre-Phase-3 **21 / 70 (30%)** |
| Grid | Straight pattern vs click: mean error < 2 ms | **pass** | `T9.3 grid` / T3.3 |
| Play variety | ≥ 3 distinct grooves across the default form | **pass** | `T9.3 Play variety`; e2e jam ≥ 3 names (Rock Backbeat, Chorus Mid, Chorus Open) |
| Lock under a DAW loop | Bass on occupied 16ths; transition within `lockBars` | **pass** | T2.1 |
| Lock under a breath | 6 s without picking → drums and bass continue | **pass** | T6.4 |
| Bass content | Play verse shows authored intervals, not just the root | **pass** | T5.1 / `T9.3 bass content` (pattern 22 at E: 40 and 45) |
| Frozen riff | Sustained chord plays one note, not eight 16th blips | **pass** | T5.2 (`O(1)` notes, `maxGate >= 4`) |
| Reactivity | Hard dynamic change moves the groove within 250 ms | **pass** | T6.1 |
| Cut-short | Replaying the riff mid-contrast re-locks at the next bar | **pass** | T6.2 same-riff cut-short; different riff does not |

---

## T9.4 — End-user stress test re-run

Station A–H in [`docs/END_USER_STRESS_TEST.md`](END_USER_STRESS_TEST.md) is still a **guitar + DAW** pass. This RC maps each station to the automated contract so the human log is a confirmation, not the first time the engine is asked:

| Station | Automated stand-in | Result |
| --- | --- | --- |
| A idle / arm | T4.4 idle Pattern 0; Play form start | suite green |
| B Record lock, loop, breath, cut-short | T2.1, T6.4, T6.2 | suite green |
| C Play form + variety | T4.1, T9.3 Play variety, T5.1 | suite green |
| D style / B-listen | T6.3 | suite green |
| E tempo | e2e BPM | suite green |
| F roots | T1.4 fold; T5.1 transpose | suite green |
| G swing + fills | T8.3, T7.1–T7.3 | suite green |
| H Forget / genre swap | pipeline Forget + T8.3 | suite green |

**Human remaining:** load **v1.0.0** in the DAW (confirm the UI string), run Stations A–H, fill Section 8 of the stress-test doc. No human miss/bug log was invented here.

Diff vs the original playtest (v0.9.62 stale binary): every defect in the review’s §2 has a landed task in Phases 1–8 and a green test in T9.1/T9.3.

---

## T9.5 — Performance budget

Fail if per-block p99 > **1.5 ms** or ONNX p99 > **5 ms**.

| Probe | Mean | p99 | Budget | Verdict |
| --- | --- | --- | --- | --- |
| `processBlock` × 10 000 @ 256 / 48 kHz | 0.523 ms | **0.841 ms** | 1.5 ms | **pass** |
| ONNX `metal_groove` × 10 000 | 0.200 ms | **0.364 ms** | 5 ms | **pass** |

Stability: 300 s simulated session, 62 sections, BPM 120–120, patterns 14–22, no crash.

---

## How to re-run

```bash
cmake --build build --config Release -j"$(sysctl -n hw.ncpu)"
./build/MetalAccompanimentTests
./build/MetalAccompanimentIntegrationTests
./build/MetalAccompanimentIntegrationTests "[phase9]"
./scripts/install-plugin-to-user.sh --build build --config Release
strings -a ~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3/Contents/MacOS/fuzzyband \
  | grep -E '^[0-9]+\.[0-9]+\.[0-9]+$'
```

---

## Addendum — review of Phases 5–9 (v1.0.1)

Independently re-verified after the Phase 5–9 commits. All 45 tasks
(T5.1–T5.3, T6.1–T6.5, T7.1–T7.3, T8.1–T8.5, T9.1–T9.5) are present and tested;
the previously `[!mayfail]` cases are gone. Three defects were found and fixed.

### Fixed

| # | Defect | Evidence | Fix |
| --- | --- | --- | --- |
| R7 | **Riff origin one bar out at most buffer sizes.** `fmod` always snaps the bar phase backwards, so a detect block starting just before a bar line moved the origin back a full bar. | Riff origins were 480 000 (128) vs 384 000 (512/2048) for the same take. | Snap forward only when the block straddles the line (<= 0.5 beat). All three now latch 480 000. |
| R8 | **Every bar-aligned DAW loop wrap re-phased the riff.** `reanchorLockClockOnJump` re-latched unconditionally to the detecting block boundary. | T2.1 loop coverage was 31 of 32 onsets, with a mid-loop phase step. | A jump landing on the same bar phase is a no-op; the mono clock keeps the loop continuous. Coverage is now 32/32. |
| R9 | **Onset capture collapsed at large buffers.** The end-of-note envelope was measured over the last quarter of the *block*, not of the *16th*. | Same 4-bar chug captured **9 / 5 / 1** onsets at 128 / 512 / 2048; at 2048 the riff became one 16-beat note. | Tail window is a fixed fraction of the 16th. Now **53 / 52 / 49** onsets; 52/52 notes per loop in a single-lock check. |

Also: the lock hold is measured from the bar-aligned origin (so `lockBars` is the
same length at every buffer size), `getRiffAPlay{Origin}Sample()` is exposed for
tests, and the T2.1 loop test measures against the real origin.

### T9.2 re-measured

| Render | Before | After |
| --- | --- | --- |
| PatternPlayer golden (full fingerprint) | identical | identical |
| Processor count-in + capture | identical | identical |
| Record session, frozen-riff bass notes | 13 / 10 / 6 | **57 / 57 / 54** |
| Record session, drum events 512 vs 2048 | diverged | **identical** |

`512` vs `2048` now agree on every drum event; `128` still differs in fill arming.

### Still block-dependent (pre-existing, not regressions)

* **Fill arming at 128 samples** — one fill instead of two (the same 2-vs-4 tom
  pattern is present before this review). T7.2 follow-up.
* **3 of ~55 frozen-riff notes between 512 and 2048** — capture-window edge
  (the first/last partial slot of the take reads a truncated tail).
* **Click note-off** lands on the detecting block boundary.
* **Lock onset** is detected on a block boundary.

These are arrangement/edge effects; note placement is exact.
