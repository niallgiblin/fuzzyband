---
phase: 21-cpp-type-foundation
plan: "02"
subsystem: tests
tags: [test-update, enum, structure-state, policy-pattern-mapper]
dependency_graph:
  requires: [StructureState-3-value, FeatureVector-no-genre]
  provides: [passing-test-suite-3-value, version-0.4.0]
  affects: [CI, test-binary]
tech_stack:
  added: []
  patterns: [catch2-enum-tests, rms-threshold-test-fixtures]
key_files:
  created: []
  modified:
    - tests/test_structure_tagger.cpp
    - tests/test_structure_tagger_extended.cpp
    - tests/test_structure_hold_smoother.cpp
    - tests/test_structure_hold_smoother_complete.cpp
    - tests/test_rule_based_inference.cpp
    - tests/PolicyPatternMapperTests.cpp
    - tests/test_policy_pattern_mapper_full.cpp
    - tests/test_e2e_structure_transitions.cpp
    - tests/test_bass_generative.cpp
    - tests/test_bass_validator_boundaries.cpp
    - tests/test_structure_rule_agreement.cpp
    - tests/test_processor_pipeline.cpp
    - CMakeLists.txt
decisions:
  - "VERSE/CHORUS/BREAKDOWN replaced with SOFT/LOUD in all test fixtures — centroid-based test inputs replaced with RMS-based inputs (kRmsSoft=0.2, kRmsLoud=0.5)"
  - "policyGenreIndex removed from all test fixtures — PolicyPatternMapper tests now verify identity (no genre shift)"
  - "E2E test amplitudes set to 0.3 (SOFT, RMS≈0.212) and 0.6 (LOUD, RMS≈0.424) to avoid kLoudRms=0.35 boundary ambiguity"
  - "Bass validator tests and structure rule agreement test fixed as Rule 3 blocking issues"
metrics:
  duration: "~15 minutes"
  completed: "2026-04-28T12:15:00Z"
  tasks_completed: 2
  files_changed: 13
---

# Phase 21 Plan 02: Test Suite Update for 3-Value StructureState Summary

**One-liner:** Updated all Catch2 test files to use SILENT/SOFT/LOUD enum values with pure RMS-based fixtures, removed policyGenreIndex from all test fixtures, bumped version to 0.4.0, and verified 81 tests / 294 assertions pass cleanly.

## Tasks Completed

| Task | Name | Commit | Files |
|------|------|--------|-------|
| 1 | Rewrite structure tagger and hold smoother tests | d9ba044 | test_structure_tagger.cpp, test_structure_tagger_extended.cpp, test_structure_hold_smoother.cpp, test_structure_hold_smoother_complete.cpp |
| 2 | Rewrite inference and policy tests, bump version, build and run | b6b7a2e | test_rule_based_inference.cpp, PolicyPatternMapperTests.cpp, test_policy_pattern_mapper_full.cpp, test_e2e_structure_transitions.cpp, test_bass_generative.cpp, test_bass_validator_boundaries.cpp, test_structure_rule_agreement.cpp, test_processor_pipeline.cpp, CMakeLists.txt |

## What Was Built

- All 4 structure tagger/smoother test files rewritten: VERSE/CHORUS/BREAKDOWN → SOFT/LOUD; centroid inputs eliminated; RMS-based fixtures (kRmsSoft=0.2, kRmsLoud=0.5) used throughout
- `test_structure_tagger.cpp`: new 2.0s SOFT hysteresis hold test (173 blocks at 512/44100)
- `test_structure_tagger_extended.cpp`: 6 tests covering all SILENT/SOFT/LOUD transitions
- `test_structure_hold_smoother.cpp`: SOFT→LOUD hold test (was VERSE→CHORUS)
- `test_structure_hold_smoother_complete.cpp`: complete 3-value smoother test suite with kHoldSoftSec/kHoldLoudSec constants
- `test_rule_based_inference.cpp`: maps SILENT→0, SOFT+bpm<120→1, SOFT+bpm<160→2, SOFT+bpm≥160→3, LOUD+bpm<160→4, LOUD+bpm≥160→5
- `PolicyPatternMapperTests.cpp`: identity tests with policyVariation=0.5, no policyGenreIndex
- `test_policy_pattern_mapper_full.cpp`: variation extreme tests preserved; genre loop/distinctness tests removed; range clamping test simplified
- `test_e2e_structure_transitions.cpp`: CHORUS→LOUD, comment/amplitude updates (0.3f → SOFT, 0.6f → LOUD)
- `CMakeLists.txt`: version bumped to 0.4.0 (milestone bump)
- Final test run: **All tests passed (294 assertions in 81 test cases)**

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed test_bass_generative.cpp — StructureState::VERSE**
- **Found during:** Task 2 pre-build grep
- **Issue:** `test_bass_generative.cpp` used `StructureState::VERSE` in 6 call sites — would not compile
- **Fix:** Replaced all 6 `StructureState::VERSE` → `StructureState::SOFT`
- **Files modified:** tests/test_bass_generative.cpp
- **Commit:** b6b7a2e

**2. [Rule 3 - Blocking] Fixed test_bass_validator_boundaries.cpp — VERSE/CHORUS/BREAKDOWN**
- **Found during:** Task 2 pre-build grep
- **Issue:** Multiple references to `StructureState::VERSE`, `::CHORUS`, `::BREAKDOWN` plus test case names referencing old enum names
- **Fix:** VERSE→SOFT, CHORUS→LOUD, BREAKDOWN→SOFT; deduplicated "BREAKDOWN/VERSE/CHORUS state" test cases to "SOFT state" and "LOUD state"
- **Files modified:** tests/test_bass_validator_boundaries.cpp
- **Commit:** b6b7a2e

**3. [Rule 3 - Blocking] Fixed test_structure_rule_agreement.cpp — StructureState::VERSE**
- **Found during:** Task 2 pre-build grep
- **Issue:** `StructureState::VERSE` in feature vector fixture
- **Fix:** `StructureState::VERSE` → `StructureState::SOFT`
- **Files modified:** tests/test_structure_rule_agreement.cpp
- **Commit:** b6b7a2e

**4. [Rule 1 - Comment] Updated comment in test_processor_pipeline.cpp**
- **Found during:** Task 2 pre-build grep (comment-only reference)
- **Issue:** Comment referenced "VERSE range" — stale after pure-RMS classification
- **Fix:** Updated comment to describe SOFT classification via RMS
- **Files modified:** tests/test_processor_pipeline.cpp
- **Commit:** b6b7a2e

## Verification Results

```
grep -rn "VERSE|CHORUS|BREAKDOWN|policyGenreIndex" src/ tests/ --include="*.cpp" --include="*.h"
```
Result: **zero matches**

```
grep "^project" CMakeLists.txt
```
Result: `project(MetalAccompaniment VERSION 0.4.0)`

```
./build/MetalAccompanimentTests
```
Result: `All tests passed (294 assertions in 81 test cases)`

## Known Stubs

None — all changes are test fixture rewrites. No hardcoded stubs or placeholder values introduced.

## Threat Surface Scan

No new network endpoints, auth paths, file access patterns, or schema changes. Changes are test-only updates plus CMakeLists.txt version bump.

Threat mitigations from plan applied:
- T-21-03: Compiler enforced — any remaining `policyGenreIndex` usage would cause compile error; build passed cleanly confirming zero remaining references
- T-21-04: E2E test uses 0.3f amplitude (RMS≈0.212) for SOFT and 0.6f (RMS≈0.424) for LOUD, safely away from kLoudRms=0.35 boundary

## Self-Check: PASSED

Files exist:
- tests/test_structure_tagger.cpp — FOUND
- tests/test_structure_tagger_extended.cpp — FOUND
- tests/test_structure_hold_smoother.cpp — FOUND
- tests/test_structure_hold_smoother_complete.cpp — FOUND
- tests/test_rule_based_inference.cpp — FOUND
- tests/PolicyPatternMapperTests.cpp — FOUND
- tests/test_policy_pattern_mapper_full.cpp — FOUND
- tests/test_e2e_structure_transitions.cpp — FOUND
- CMakeLists.txt — FOUND (VERSION 0.4.0)

Commits exist: d9ba044, b6b7a2e
