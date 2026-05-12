---
phase: "32"
plan: "02"
subsystem: training-pipeline
tags: [training, labeling, oracle, lakh, merge, dataset, ml]
dependency_graph:
  requires: [32-01]
  provides: [lakh-oracle-labels, merge-label-passthrough, feature-proxy-docs]
  affects: [training/build_lakh_dataset.py, training/merge_datasets.py, training/FEATURE_PROXY.md]
tech_stack:
  added: []
  patterns: [rule-oracle-labeling, oracle-label-passthrough, no-quantile-recomputation]
key_files:
  created: []
  modified:
    - training/build_lakh_dataset.py
    - training/merge_datasets.py
    - training/FEATURE_PROXY.md
decisions:
  - "Oracle functions copied verbatim from plan interfaces section (build_dataset.py runs in parallel wave)"
  - "build_lakh_dataset.py histogram print added without sys.exit (gate is in merge_datasets.py)"
  - "merge_datasets.py quantile block replaced atomically with three-line passthrough"
  - "FEATURE_PROXY.md Seed similarity section deleted entirely; oracle section replaces Hybrid oracle"
metrics:
  duration: "~20 minutes"
  completed: "2026-05-12"
  tasks: 3
  files: 3
---

# Phase 32 Plan 02: Lakh Oracle Labeling + Merge Label Passthrough Summary

Apply rule-oracle + Breakdown heuristic labeling to `build_lakh_dataset.py`; change tensor output key from `"scores"` to `"Y"` (int64); remove joint quantile recomputation from `merge_datasets.py` replacing it with a direct oracle passthrough; retire quantile-bin documentation in `FEATURE_PROXY.md` and document the new labeling approach.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Apply oracle labeling to build_lakh_dataset.py and change tensor format to Y key | eaa2144 | training/build_lakh_dataset.py |
| 2 | Remove quantile recomputation from merge_datasets.py and update label passthrough | 8db085a | training/merge_datasets.py |
| 3 | Update FEATURE_PROXY.md to retire quantile-bin description and document rule oracle | 86bb339 | training/FEATURE_PROXY.md |

## What Was Built

**Task 1 — build_lakh_dataset.py oracle labeling:**
- Deleted `_activity_score` function (quantile-bin scorer)
- Added `_K_SOFT_MID_BPM = 120.0` and `_K_SOFT_LOUD_BPM = 160.0` constants (mirror `pattern_rules.h`)
- Added `_rule_pattern_for_state(bpm, state_float, policy_intensity=0.5)` — Python port of C++ rule path
- Added `_oracle_label(bpm, state_float, events)` — applies rule oracle then Breakdown heuristic override
- Changed `scores: list[float] = []` to `Y_list: list[int] = []`
- Updated per-file loop: calls `_oracle_label(bpm_clamped, state_float, events)` instead of `_activity_score`
- Changed `torch.save` dict key from `"scores"` (float64) to `"Y"` (int64 via `np.asarray(Y_list, dtype=np.int64)`)
- Added advisory histogram print for Lakh Y distribution (no `sys.exit`, gate is in merge_datasets.py)

**Task 2 — merge_datasets.py oracle passthrough:**
- Deleted `_activity_score_from_row` function (dead code)
- Replaced `scores_lakh = lakh_data["scores"].numpy()` with `Y_lakh = lakh_data["Y"].numpy()`
- Added clarifying comments to `Y_gmd_train_old` and `Y_gmd_val_old` load lines
- Replaced full quantile recomputation block (17 lines: `scores_gmd_train`, `scores_gmd_val`, `scores_merged`, `qt`, three `searchsorted` calls) with 5-line oracle passthrough
- Deleted `Quantile edges:` print line (`qt` no longer exists; would NameError at runtime)
- Updated module docstring to reflect Phase 32 oracle passthrough approach

**Task 3 — FEATURE_PROXY.md documentation:**
- Replaced "## Hybrid oracle — overview" + "Implementation note" section with "## Label oracle — Y ∈ [0, 6]"
- New section documents: policyIntensity=0.5 assumption, rule oracle lookup table (6 conditions), GMD state=3.0 clamp note, Breakdown heuristic conditions
- Updated "Heuristic class guess h (coarse)" bullet to reference Label oracle section
- Renamed "Tie-breaks and failure modes" to "## Failure modes"; removed argmax/ambiguous bullets
- Deleted "## Seed similarity" section entirely (cosine similarity fully retired)

## Verification Results

All plan verification checks passed:

1. Quantile code removed from merge_datasets.py: `grep -c "searchsorted|quantile.*edges|_activity_score"` → **0** (expected 0)
2. Lakh tensor format: `"Y": torch.from_numpy(Y_all)` present, `"scores"` absent
3. FEATURE_PROXY.md oracle section: `grep -c "Label oracle"` → **2** (expected >= 1)
4. merge_datasets.py reads `lakh_data["Y"]` at line 105

Both Python files pass `py_compile` syntax check.

## Deviations from Plan

**None** — plan executed exactly as written.

The oracle functions were taken verbatim from the plan's `<interfaces>` section (since `build_dataset.py` runs in a parallel worktree). The plan anticipated this and pre-provided the function bodies, so no deviation was needed.

## Threat Model Verification

| Threat | Status |
|--------|--------|
| T-32-03 — lakh_tensors.pt tensor dict key contract | Mitigated: both sides updated atomically in this plan; "scores" key removed, "Y" key added |
| T-32-04 — merge_datasets.py quantile block removal | Mitigated: `np.searchsorted` and `qt` patterns absent; runtime KeyError impossible |
| T-32-05 — training data in gitignored data/ | Accepted: data/ gitignored, no PII |

## Threat Flags

None — no new network endpoints, auth paths, file access patterns, or schema changes at trust boundaries introduced.

## Known Stubs

None — all changes are functional code modifications with deterministic behavior.

## Self-Check: PASSED

- training/build_lakh_dataset.py exists: FOUND
- training/merge_datasets.py exists: FOUND
- training/FEATURE_PROXY.md exists: FOUND
- Commit eaa2144 exists: FOUND
- Commit 8db085a exists: FOUND
- Commit 86bb339 exists: FOUND
