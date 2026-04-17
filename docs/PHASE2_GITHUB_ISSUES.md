# Phase 2 — GitHub Issues checklist (manual)

Use this list to open issues on GitHub after `v0.1.0-phase1`. Labels: suggest **`phase-2`** + **`enhancement`** (or **`feature`**). Create a milestone **Phase 2 — ML + Generative** and attach all issues.

Link **`ARCHITECTURE.md`** and **`src/inference/IInference.h`** on any inference-related ticket.

**Training data strategy (Groove/GMD vs E-GMD, rank vs hybrid, guitar bridge):** `.planning/phases/999.1-ml-phase-2-data-feasibility-research/PHASE2_ML_DATA_STRATEGY.md`

| # | Topic (from roadmap) | Goal summary | Notes |
|---|----------------------|--------------|--------|
| Meta | M2 scope vs backlog | Reconcile Milestone 2 Preview with **Backlog 999.1** (ML data feasibility, cloud vs ONNX). | Single umbrella issue; link `.planning/ROADMAP.md` Backlog section |
| 2.1 | ONNX Runtime + `OnnxInference` | Drop-in `IInference` implementation using ONNX. | Depends on ONNX CMake path (`MA_ENABLE_ONNX`, `ONNXRUNTIME_ROOT`) |
| 2.2 | Pitch / chord detection | YIN or CREPE via ONNX for bass root / harmony. | Replaces fixed bass root |
| 2.3 | ML structure classifier | Replace threshold state machine where appropriate. | |
| 2.4 | Generative bass line | Small transformer → ONNX. | |
| 2.5 | Plugin UI | Genre, intensity, pattern variation controls. | |
| 2.6 | Python training pipeline | Metal MIDI dataset + training tooling. | May tie to 999.1 dataset audit |

**999.1 reminder:** Open a separate **research** issue for *ML training data feasibility* (license audit, dataset shortlist) if that backlog item is promoted; it is not required to close Phase 8.
