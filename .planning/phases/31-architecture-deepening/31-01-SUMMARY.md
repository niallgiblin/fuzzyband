---
phase: 31-architecture-deepening
plan: "01"
subsystem: inference
tags: [refactor, deduplication, pattern-rules, header-only, tdd]
dependency_graph:
  requires: []
  provides: [src/inference/pattern_rules.h]
  affects: [src/inference/RuleBasedInference.cpp, src/inference/OnnxInference.cpp]
tech_stack:
  added: ["PatternRules namespace (header-only inline, C++20)"]
  patterns: ["Header-only namespace for shared stateless logic", "TDD RED-GREEN cycle"]
key_files:
  created:
    - src/inference/pattern_rules.h
    - tests/test_pattern_rules.cpp
  modified:
    - src/inference/RuleBasedInference.cpp
    - src/inference/OnnxInference.cpp
    - CMakeLists.txt
decisions:
  - "Header-only inline functions used to avoid ODR issues and eliminate a compilation unit"
  - "kPatternCount=7 constant centralised in PatternRules to eliminate magic numbers"
  - "RuleBasedInference retains std::clamp around rulePatternForState result to preserve existing contract"
metrics:
  duration: "~20 minutes"
  completed: "2026-05-03T07:38:19Z"
  tasks_completed: 2
  files_changed: 5
---

# Phase 31 Plan 01: Pattern Rules Extraction Summary

**One-liner:** Extracted duplicated BPM-threshold + exclusion logic into a header-only `PatternRules` namespace shared by RuleBasedInference and OnnxInference — eliminating threshold drift risk (ARCH-03).

## What Was Built

`src/inference/pattern_rules.h` — header-only namespace with four inline functions:

- `PatternRules::adjustedBpm(f)` — apply policyIntensity ±20 BPM offset
- `PatternRules::rulePatternForState(f)` — map StructureState + BPM to pattern index 0-5
- `PatternRules::isPatternCompatibleWithState(idx, state)` — validate index vs state
- `PatternRules::applyExclusion(result, excludeIndex)` — D-23-04 single-shot exclusion via %7

Two constants centralised: `kSoftMidBpmThreshold = 120.0f`, `kSoftLoudBpmThreshold = 160.0f`.

`tests/test_pattern_rules.cpp` — 4 TEST_CASE blocks, 25 assertions, tagged `[pattern_rules]`.

Both inference files now delegate to `PatternRules::` — the 56-line anonymous-namespace duplicate block in OnnxInference.cpp is deleted.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 (RED) | Failing tests for PatternRules | 3a183a7 | tests/test_pattern_rules.cpp, CMakeLists.txt |
| 1 (GREEN) | Create pattern_rules.h | 1799def | src/inference/pattern_rules.h |
| 2 | Delegate both inference files to PatternRules:: | f8a1d61 | src/inference/RuleBasedInference.cpp, src/inference/OnnxInference.cpp |

## TDD Gate Compliance

- RED gate: `test(31-01)` commit `3a183a7` — build failed with `fatal error: 'inference/pattern_rules.h' file not found`
- GREEN gate: `feat(31-01)` commit `1799def` — 25/25 assertions passed
- REFACTOR: not needed — implementation was clean on first pass

## Verification Results

```
Full suite: All tests passed (332 assertions in 86 test cases)
[pattern_rules]: All tests passed (25 assertions in 4 test cases)
[inference]: All tests passed (54 assertions in 3 test cases)
```

## Deviations from Plan

None — plan executed exactly as written.

The verify step in the plan referenced a nonexistent `focused-grothendieck-5c6a3f/build` path. A fresh build was configured in the current worktree using cached FetchContent deps from the main repo's build directory. This is a deviation in infrastructure only — no plan logic changed.

## Known Stubs

None.

## Threat Flags

No new network endpoints, auth paths, file access patterns, or schema changes introduced. The header is stateless inline logic visible in compilation units only.

## Self-Check: PASSED

- `src/inference/pattern_rules.h`: FOUND
- `tests/test_pattern_rules.cpp`: FOUND
- `src/inference/RuleBasedInference.cpp` contains `#include "pattern_rules.h"`: FOUND
- `src/inference/OnnxInference.cpp` contains `#include "pattern_rules.h"`: FOUND
- Commits 3a183a7, 1799def, f8a1d61: all present in git log
- No anonymous-namespace duplicates remain in OnnxInference.cpp
- Full test suite: 332/332 assertions pass
