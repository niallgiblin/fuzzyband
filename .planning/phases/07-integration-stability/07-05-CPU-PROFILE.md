# CPU Profile (STAB-02)

**Status:** PENDING — requires Instruments Time Profiler on an M-series Mac during live play in Reaper (256-sample buffer @ 48 kHz).

**Date:**  
**Machine:**  
**DAW:**  
**Buffer:** 256 samples @ 48 kHz  
**Tool:** Instruments Time Profiler  

## CPU Breakdown (30s sample during active play)

| Function | CPU % |
|---|---|
| AccompanimentProcessor::processBlock | |
| AccompanimentProcessor::inferenceLoop | |
| OnsetDetector (FFT) | |
| EnergyAnalyser | |
| StructureTagger | |
| PatternPlayer | |
| **Total plugin CPU** | |

## STAB-02 Result

- Target: **&lt; 15%** CPU for plugin process during sustained guitar input.
- Measured: _TBD_

---

_Fill after profiling per Plan 05 Task 4._
