---
phase: 10-onnx-runtime-iinference
plan: 01
subsystem: inference
tags:
  - onnx
  - inference
  - docs
  - ci
requires:
  - CONTRIBUTING.md §ONNX Runtime (already present)
  - src/inference/OnnxInference.cpp (already present)
provides:
  - docs/ONNX_IO.md (frozen X/Y tensor contract for Phase 15 export)
  - scripts/check_onnx_audio_thread.sh (ONNX-03 audio-thread guardrail)
  - CI step running the guardrail on macOS
affects:
  - README.md
  - ARCHITECTURE.md
  - .github/workflows/ci.yml
  - .planning/REQUIREMENTS.md (ONNX-01/02/03 checked)
tech-stack:
  added: []
  patterns:
    - Shell-based CI guardrail (grep forbidden symbols in audio-thread sources)
key-files:
  created:
    - docs/ONNX_IO.md
    - scripts/check_onnx_audio_thread.sh
  modified:
    - README.md
    - ARCHITECTURE.md
    - .github/workflows/ci.yml
    - .planning/REQUIREMENTS.md
decisions:
  - D-10-01 honored: default CI stays MA_ENABLE_ONNX=OFF; guardrail does not require ORT download
  - D-10-06 honored: tensor contract (X float32 [1,5]; Y int64 clamped 0–6) frozen for v0.2.0
metrics:
  duration_seconds: 142
  completed: 2026-04-17
  tasks_completed: 5
  files_changed: 6
requirements_closed:
  - ONNX-01
  - ONNX-02
  - ONNX-03
---

# Phase 10 Plan 01: ONNX runtime & IInference — documentation, discovery, and CI guardrail

**One-liner:** Freeze the ONNX I/O contract in `docs/ONNX_IO.md`, link it from README/ARCHITECTURE, and add a no-ORT-required shell + CI guardrail that fails if `onnxruntime_cxx_api` or `Ort::` leaks into `AccompanimentProcessor`.

## What shipped

- **`docs/ONNX_IO.md`** — frozen contract: input `X` float32 `[1,5]` with canonical feature order (`bpm`, `rmsEnergy`, `spectralCentroid`, `highFreqFlux`, `float(state)`); output `Y` int64 clamped 0–6; bundled asset path `assets/accompaniment_model.onnx`; regeneration via `scripts/build_minimal_pattern_onnx.py`.
- **README.md** — new "ONNX inference (Phase 10)" subsection linking `docs/ONNX_IO.md` and pointing to `CONTRIBUTING.md` for `MA_ENABLE_ONNX` / `ONNXRUNTIME_ROOT`.
- **ARCHITECTURE.md** — `OnnxInference` subsection now links the tensor contract.
- **`scripts/check_onnx_audio_thread.sh`** — bash (`set -euo pipefail`) guardrail; exits 0 when `src/AccompanimentProcessor.{cpp,h}` are clean, exits 1 with offending line on match. Header comment states ORT API must only appear under `src/inference/`.
- **`.github/workflows/ci.yml`** — new step "ONNX audio-thread guardrail (ONNX-03)" runs the script before `Configure`. No ONNX Runtime download added (D-10-01 preserved).
- **`.planning/REQUIREMENTS.md`** — ONNX-01, ONNX-02, ONNX-03 checked.

## Commits

| Task | Name | Commit |
|------|------|--------|
| 1 | docs/ONNX_IO.md — I/O contract | `43bf9b3` |
| 2 | README + ARCHITECTURE discovery | `f4e052a` |
| 3 | check_onnx_audio_thread.sh | `8f7604d` |
| 4 | CI guardrail step | `972017a` |
| 5 | REQUIREMENTS.md ONNX-01/02/03 | `bb3bc86` |

## Verification results

- `cmake --build build --target MetalAccompanimentTests` — built (no changes to C++ sources).
- `ctest --test-dir build --output-on-failure` — **10/10 passed** (0.10 s).
- `./scripts/check_onnx_audio_thread.sh` — exited 0: "OK (no ORT symbols in AccompanimentProcessor sources)".
- YAML sanity: `yaml.safe_load(...)` on `.github/workflows/ci.yml` parses without error.

## Deviations from Plan

None — plan executed exactly as written. One minor workflow note:

- Task 1 commit also swept in pre-existing unstaged edits to `.planning/STATE.md` and `.planning/ROADMAP.md` from the prior session (not part of this plan's task files). These are planning metadata; no functional impact.
- `.planning/` is now gitignored; Task 5 used `git add -f` to update the tracked `REQUIREMENTS.md` file.

## Threat Flags

None — only docs, a read-only shell script, CI YAML, and tracked planning metadata changed. No new runtime surface.

## Self-Check: PASSED

- `docs/ONNX_IO.md` — FOUND
- `scripts/check_onnx_audio_thread.sh` — FOUND (executable)
- README.md — contains `ONNX_IO.md`, `CONTRIBUTING.md`, `MA_ENABLE_ONNX`
- ARCHITECTURE.md — contains `ONNX_IO.md`
- `.github/workflows/ci.yml` — contains `check_onnx_audio_thread`
- `.planning/REQUIREMENTS.md` — `[x] **ONNX-01/02/03`
- Commits `43bf9b3`, `f4e052a`, `8f7604d`, `972017a`, `bb3bc86` — all present in `git log`
