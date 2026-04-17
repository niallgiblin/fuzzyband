# CPU Profile (STAB-02)


**Date:** 17-04-26  
**Status:** COMPLETE
**Machine:** MacBook M2  
**DAW:** REAPER  
**Buffer:** 256 samples @ 48 kHz  
**Tool:** Instruments Time Profiler  

## CPU Breakdown (30s sample during active play)


| Function                              | CPU % (Time Profiler weight) |
| ------------------------------------- | ------------------------------ |
| `AccompanimentProcessor::processBlock` | 47.9%                          |
| `AccompanimentProcessor::inferenceLoop` | NA (not sure this runs without ML? selectPattern done) |
| OnsetDetector (FFT)                   | 1.1%                             |
| EnergyAnalyser                        | NA                             |
| StructureTagger                       | NA                             |
| `PatternPlayer` (visible subtree)     | 23.2%                          |
| **Total plugin CPU**                  | See note below                 |



## Methodology

- **Instruments:** Time Profiler, ~30 s range while playing (not silent).  
- **Limitation:** Release VST3 symbols missing for fine-grained rows (FFT / energy / tagger not resolved).


**Date:** 17-04-26
**Tool:** Instruments Time Profiler + macOS Activity Monitor

### CPU Breakdown (30 s sample, symbols resolved)

| Function | CPU % (Time Profiler weight) |
|---|---|
| `AccompanimentProcessor::processBlock` | |
| `AccompanimentProcessor::inferenceLoop` | |
| `OnsetDetector` / FFT | |
| `EnergyAnalyser` | |
| `StructureTagger` | |
| `PatternPlayer` | |

### Activity Monitor Delta

| Config | REAPER % CPU (30 s avg) |
|---|---|
| Plugin loaded, FX bypass engaged | 27% |
| Plugin loaded, active (playing) | 34% |
| **Delta (plugin cost)** | 7% |

### STAB-02 Result (re-measurement)

- **Target:** < 15% plugin cost in system terms.
- **Measured (Activity Monitor delta):** 7%
- **Pass / Fail:** pass

### Instructions

1. In REAPER, rescan VST3 or point the project at the RelWithDebInfo VST3 path above (or drag-copy over the installed copy temporarily).
2. Launch Instruments → Time Profiler. Attach to REAPER (or the AU host).
3. Play guitar for 30 s; stop recording.
4. Fill the CPU breakdown table. With symbols present, subcomponent frames (OnsetDetector, PatternPlayer children) should resolve.
5. Separately, record REAPER's Activity Monitor `% CPU` for 30 s with plugin bypassed vs. active. The delta is the plugin's absolute cost. That is the number that must be < 15%.
6. Fill Pass / Fail. Resume signal: **`cpu pass`** or **`cpu fail`**.

