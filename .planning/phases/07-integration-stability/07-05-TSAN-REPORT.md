# ThreadSanitizer (D-05)

**Date:** 2026-04-16  
**Build:** `build-tsan/` (Debug, `-fsanitize=thread`)  
**Binary:** `build-tsan/MetalAccompanimentTests`  
**Command:** `TSAN_OPTIONS=halt_on_error=1 ./build-tsan/MetalAccompanimentTests`

## Result

- All 7 Catch2 test cases completed; **no ThreadSanitizer reports** (exit code 0).
- Assertions: 96 across 7 test cases.

## Notes

- Automated coverage is unit tests only; full plugin / DAW threading is not exercised here.
