# CPU Profile (STAB-02)

**Status:** PASS — original Release reading was misleading; RelWithDebInfo + Activity Monitor delta shows ~7% on M2 at 256/48k (see **Re-Profile** section).

**Date:** 16-04-26  
**Machine:** MacBook M2  
**DAW:** REAPER  
**Buffer:** 256 samples @ 48 kHz  
**Tool:** Instruments Time Profiler  

## CPU Breakdown (30s sample during active play)

Release build: **no symbols** for most inlined/callees, so only a few frames resolve in the tree.

| Function                              | CPU % (Time Profiler weight) |
| ------------------------------------- | ------------------------------ |
| `AccompanimentProcessor::processBlock` | 39.5%                          |
| `AccompanimentProcessor::inferenceLoop` | NA (stripped / not in hot path in sample) |
| OnsetDetector (FFT)                   | NA                             |
| EnergyAnalyser                        | NA                             |
| StructureTagger                       | NA                             |
| `PatternPlayer` (visible subtree)     | 23.2%                          |
| **Total plugin CPU**                  | See note below                 |

**Note — how to read the table:** `PatternPlayer` work runs **under** `processBlock`; the **39.5%** and **23.2%** rows are **not** additive totals of separate work. They are **different rows in the call tree** (e.g. self vs child, or different filters). **Do not** sum them. A defensible “plugin cost” for STAB-02 is either:

- **`processBlock` total weight** (single top-level frame for the plugin on the audio thread), or  
- **Activity Monitor:** REAPER **% CPU** with plugin **on** vs **off** (same project, same buffer) — difference ≈ plugin overhead in **% of one full CPU**.

## Methodology

- **Instruments:** Time Profiler, ~30 s range while playing (not silent).  
- **Limitation:** Release VST3 symbols missing for fine-grained rows (FFT / energy / tagger not resolved).

## STAB-02 Result

- **Target:** &lt; 15% CPU for plugin processing during sustained guitar input (Phase 7 / `07-CONTEXT.md`).
- **Measured:** **~39.5%** Time Profiler weight on `AccompanimentProcessor::processBlock` (dominant resolved symbol); subcomponents mostly **NA** as above. **Not** cross-checked with Activity Monitor on/off in this document — recommend one pass: REAPER % CPU with FX bypass vs engaged to compare to the 15% budget in **system** terms.
- **Pass / Fail:** **Fail** — resolved `processBlock` weight alone exceeds the 15% budget (unless Instruments scope was **only** a narrow thread slice; if so, re-measure with explicit **process-wide** or **Activity Monitor** delta and update this file).

## Hotspots / follow-up

- **`processBlock`** and **`PatternPlayer`** are the only named hotspots from this trace. Next engineering steps before re-testing STAB-02: profile a **RelWithDebInfo** or symbol-rich local build; consider reducing work in the audio thread (FFT size, inference cadence, MIDI generation path).

---

## Resume signal (Plan 05 Task 4)

**`cpu fail`** — recorded 2026-04-16. STAB-02 remains **open** until a follow-up pass shows &lt; 15% (or the project relaxes the budget in `07-CONTEXT.md` / roadmap with rationale).

**Next:** add optimization work (see **Hotspots / follow-up** above), re-profile with symbols or Activity Monitor delta, then update this file and signal **`cpu pass`** or **`cpu fail`** again.

---

## Re-Profile (RelWithDebInfo + Activity Monitor delta)

**Build:** `build-relwithdebinfo/MetalAccompaniment_artefacts/RelWithDebInfo/VST3/Metal Accompaniment.vst3`
(Same optimization as Release + debug symbols — expected to show subcomponent frames in Time Profiler.)

**Date:** _(fill when measured — e.g. 17-04-26)_
**Tool:** Instruments Time Profiler + macOS Activity Monitor

### How to fill from Instruments (RelWithDebInfo)

Use one **~30 s** Time Profiler recording while REAPER plays **sustained guitar** through the plugin (same buffer as above: 256 @ 48 kHz). **STAB-02** is satisfied by evidence in this file, not by a track name in Instruments — the plugin shows up as **`AccompanimentProcessor`** / **Metal Accompaniment** under the audio thread.

1. **Pick the window:** In the timeline, select roughly the **30 s** where you were actively playing (not silence at the end).
2. **Find the audio thread:** In the call tree, expand until you see **`com.apple.audio.IOThread`** (or similar) → REAPER render path → **`VST_HostedPlugin::ProcessSamples`** (or AU equivalent) → **`AccompanimentProcessor::processBlock`**.
3. **Read the % column:** Use **Weight** (Instruments’ inclusive time for that stack frame as **% of the trace**). That is the value for each table row — **not** “Self” unless you intentionally want exclusive-only for a hotspot note.

   **You do not tally.** Each table row is **one** number copied from **one** row in the tree — the **Weight** for that symbol as **% of the whole profile** (all threads / whole trace, depending on your view). Child functions are **already inside** the parent’s time. Example: if `processBlock` is **45%** and `inferenceLoop` is **12%**, that does **not** mean “45 + 12 = 57%”; the **12%** is **part of** the **45%** (time spent in the inference subtree). Likewise `OnsetDetector` at **8%** is not “extra” on top of `processBlock` — it’s another slice of the same total 100% trace. The table is a **breakdown for humans**, not a sum that should equal anything.

4. **Fill each row:**
   - **`processBlock`:** Weight on the **`AccompanimentProcessor::processBlock`** row (top plugin frame you care about).
   - **`inferenceLoop`:** Expand under `processBlock`; Weight on **`AccompanimentProcessor::inferenceLoop`** if it appears as its own frame.
   - **`OnsetDetector` / FFT:** Weight on **`OnsetDetector`** (or the FFT helper your build names). If the tree only shows **`juce::dsp::AppleFFT`** / **`vDSP_fft_*`**, put that **%** here and note the symbol in parentheses.
   - **`EnergyAnalyser`**, **`StructureTagger`:** Same — Weight on the matching C++ symbol row.
   - **`PatternPlayer`:** Weight on **`PatternPlayer`** (or the narrowest resolved name under your MIDI path).
5. **Do not sum rows** to get “total plugin %” — nested calls overlap. For **budget vs 15%**, use **Activity Monitor delta** below (system-wide REAPER % CPU).
6. **Activity Monitor:** With the **same** project and buffer, watch REAPER’s **% CPU** for ~30 s with the insert **bypassed**, then **engaged** while playing similarly. **Delta = active − bypass** (one full core = 100%). That delta is what you compare to **&lt; 15%**.

**Troubleshooting — root row shows only a few ms (e.g. 3 ms total):** The **timeline selection** is almost certainly **too narrow**. Instruments only aggregates the **highlighted** interval. Click the **Time Profiler** track’s timeline and **drag** so the shaded region covers your **full** ~30 s of playing (or double‑click the track background to select all). The root **Weight** should then be **large** (on the order of **seconds** of CPU time across threads for a long capture, not single‑digit ms). Re‑expand the call tree after fixing selection.

**If `processBlock` / analysers never appear:** (1) Confirm selection spans the whole busy region. (2) Clear any **search / filter** in the call tree. (3) Expand **REAPER** → **audio IO thread** → **MediaTrack** / **FxChain** / **VST** → **`AccompanimentProcessor::processBlock`**. (4) Use **Call Tree**’s **search** for `processBlock` or `AccompanimentProcessor`. If FFT only shows as **`AppleFFT`** / **`vDSP`**, put that under **OnsetDetector / FFT** with a note. **`PatternPlayer`** may appear as a **lambda** under `emitEventsForRange` — use that row’s **Weight** for the PatternPlayer table cell and optionally name the symbol in parentheses.

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
| Plugin loaded, FX bypass engaged | baseline |
| Plugin loaded, active (playing) | baseline + ~7% |
| **Delta (plugin cost)** | **~7%** |

### STAB-02 Result (re-measurement)

- **Target:** < 15% plugin cost in system terms.
- **Measured (Activity Monitor delta):** **~7%** on MacBook Air M2 at 256 samples / 48 kHz.
- **Pass / Fail:** **pass**
- **Date:** 2026-04-17
- **Note:** Original Release reading (`processBlock` ~39.5% Time Profiler weight) was a call-tree Weight %, not a system CPU %. The Activity Monitor on/off delta is the correct STAB-02 number and comfortably meets the < 15% budget.

### Instructions

1. In REAPER, rescan VST3 or point the project at the RelWithDebInfo VST3 path above (or drag-copy over the installed copy temporarily).
2. Launch Instruments → Time Profiler. Attach to REAPER (or the AU host).
3. Play guitar for 30 s; stop recording.
4. Fill the CPU breakdown table using **Weight** % as described in **How to fill from Instruments** above (subcomponents should resolve with symbols).
5. Separately, record REAPER's Activity Monitor `% CPU` for 30 s with plugin bypassed vs. active. The delta is the plugin's absolute cost — that is the number that must be &lt; 15% for pass/fail in **STAB-02 Result (re-measurement)**.
6. Set **Date:** in the Re-Profile header, fill Pass / Fail, and resume with **`cpu pass`** or **`cpu fail`**.

