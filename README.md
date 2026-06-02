# Fuzzyband — Metal Accompaniment

JUCE **8** **VST3 / AU** plugin: listens to guitar audio and outputs **drum + bass MIDI** in real time. Ships with a **rule-based** pipeline (onset detection, energy/structure tagging, tempo tracking) and a trained **ONNX** pattern/structure/bass pipeline behind `MA_ENABLE_ONNX`. Default builds use rule-based inference; ONNX path is gated behind a readiness checklist ([`docs/ONNX_READINESS.md`](docs/ONNX_READINESS.md)). Version **0.5.5** — milestone **v0.6.0 ML Correctness & Evaluation** in progress.

## Plugin hosting (insert order)

Put the plug-in on your **dry guitar track *before*** amp/cab sims and heavy FX (**guitar → Metal Accompaniment → FX**) so onset/tempo/analysis match what this build assumes. Quick reference: **[`plugin/README.md`](plugin/README.md)** · contributor entry **[`docs/PLUGIN_HOSTING.md`](docs/PLUGIN_HOSTING.md)**.

## Learning the codebase

If you are new to C++/JUCE/audio plugins, start with **[`docs/DOCS_INDEX.md`](docs/DOCS_INDEX.md)**. It links a plain-English codebase walkthrough, runtime architecture map, C++/JUCE/audio-thread audit, and modularity/bloat review.

## Version & roadmap

| What | Where |
|---|---|
| GSD milestone & phase (current: v0.6.0, Phase 36) | [`.planning/STATE.md`](.planning/STATE.md) and [`.planning/ROADMAP.md`](.planning/ROADMAP.md) |
| Plugin version string in the editor | **`CMakeLists.txt`** → `project(MetalAccompaniment VERSION 0.5.5)` — rebuild after changing |
| Archived milestone narratives | [`.planning/milestones/`](.planning/milestones/) (v0.1.0 through v0.4.0) |

**Milestone snapshot:**

| Milestone | Phases | Status |
|---|---|---|
| **v0.1.0** Rule MVP | 1–8 | ✅ Shipped |
| **v0.2.0** ML + Generative | 9–16 | ✅ Shipped |
| **v0.3.0** Real ML Training Pipeline | 17–20 | ✅ Shipped |
| **v0.4.0** ML Playability & Simplification | 21–26 | ✅ Shipped |
| **v0.5.0** Rhythmic Coherence | 27–31 | 🚧 Partial (27, 31 done; 28–30 parked) |
| **v0.6.0** ML Correctness & Evaluation | 32–36 | 🔜 In progress (32–35 done; 36 executing) |
| **v0.7.0** Creative Companion | TBD | 📋 Planned (brief at `.planning/milestones/v0.7.0-CONTEXT.md`) |

## ML pipeline

The plugin ships three trained ONNX models in `assets/`. They are active when built with `-DMA_ENABLE_ONNX=ON`; otherwise the rule-based pipeline runs. See [`docs/ONNX_READINESS.md`](docs/ONNX_READINESS.md) for the criteria gating the `MA_ENABLE_ONNX=ON` default flip.

| Model | File | Tensor contract |
|---|---|---|
| Pattern selector | `assets/accompaniment_model.onnx` | [`docs/ONNX_IO.md`](docs/ONNX_IO.md) |
| Structure classifier | `assets/structure_model.onnx` | [`docs/ONNX_IO.md`](docs/ONNX_IO.md) |
| Generative bass | `assets/bass_model.onnx` | [`docs/BASS_ONNX_IO.md`](docs/BASS_ONNX_IO.md) |

**Training pipeline:** Python venv with pinned deps — see [`training/README.md`](training/README.md) for dataset prep (GMD + Lakh MIDI), model training, ONNX export, contract validation, and feature-capture evaluation.

**v0.6.0 ML correctness work (Phases 32–36):**
- **Label correction (Phase 32):** Training labels derived from rule oracle, not quantile bins
- **Quality gates (Phase 33):** Bass and structure models have hard export gates; structure normalization baked into ONNX graph
- **Domain gap (Phase 34):** Runtime `FeatureVector` capture, offline evaluation, captured-vs-proxy gap docs
- **Inference consistency (Phase 35):** Adjusted-BPM ONNX input, compatible exclusion logic, `structureBlend` docs
- **Evaluation (Phase 36):** Latency benchmarks, A/B comparison, ONNX readiness checklist (in progress)

## Architecture & learning the codebase

- **[`docs/DOCS_INDEX.md`](docs/DOCS_INDEX.md)** — recommended reading order (codebase walkthrough, runtime architecture, best-practices audit, modularity review)
- **[`ARCHITECTURE.md`](ARCHITECTURE.md)** — component boundaries, threading model, data flow
- **[`docs/PLUGIN_GUIDE.md`](docs/PLUGIN_GUIDE.md)** — practical reference for tuning and extending the plugin
- **[`docs/FEATURE_CAPTURE.md`](docs/FEATURE_CAPTURE.md)** — runtime feature capture workflow and JSONL schema

### Extracted modules (v0.5.0 Phase 31)

`AccompanimentProcessor` was thinned by extracting four deep modules into `src/analysis/`:
- **`PlaybackGate`** — silence, phrase-breath holds, confidence gating, deferred beat snaps
- **`StablePitchTracker`** — pitch-class stability window and semitone mapping
- **`TempoStabiliser`** — BPM hysteresis and alias folding
- **`PatternRules`** (in `src/inference/`) — shared rule logic between `RuleBasedInference` and `OnnxInference`

## Pattern MIDI export (offline)

After building the `MetalAccompanimentExportPatterns` target, run:

```bash
./build/MetalAccompanimentExportPatterns
```

This writes `metal_accompaniment_patterns.mid` next to the executable (useful for checking patterns in Reaper before relying on the live plugin).

## Plugin parameters

### User parameters (saved with project)

These are exposed through the custom editor and the host’s generic parameter list (where applicable):

- `**outputGain**` — Guitar pass-through level at the plugin output (does not scale MIDI).
- `**intensity**` — Shifts effective BPM tier on the rule-based path (`RuleBasedInference`). Pattern ONNX inputs follow [`docs/ONNX_IO.md`](docs/ONNX_IO.md); `FeatureVector::policyIntensity` is copied from this parameter on the audio thread before enqueue.
- `**structureBlend**` — When ML structure is valid, values **≥ 0.5** use the ML shadow structure for pattern selection; **below 0.5** follow rule structure (`AccompanimentProcessor::drainFeatureQueueAndRunInference`).
- `**generativeBassMode`** — **`Auto`** / **`On`** / **`Off`** for generative bass when ONNX bass loads.

### On-screen diagnostics (not separate automation targets)

Live analysis readouts: **BPM**, **State**, **Pattern**, **RMS**, **Centroid**, **HF Flux**.

### Debug

**Next pattern (preview 5s)** — advances the pattern index and plays a short preview window.

The host may still show **Output Gain** and other parameters even if they are not drawn in the custom editor.

## Build (macOS)

Requirements: **CMake 3.22+**, **Xcode** or **Ninja**, **Git** (for FetchContent).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Artifacts (typical paths):

- VST3: `build/MetalAccompaniment_artefacts/Release/VST3/Metal Accompaniment.vst3`
- AU: `build/MetalAccompaniment_artefacts/Release/AU/Metal Accompaniment.component`

**Reaper does not scan your project `build/` folder by default.** After each build, install the VST3 into the user plug-ins folder (or add your build path under Reaper’s VST paths):

```bash
mkdir -p ~/Library/Audio/Plug-Ins/VST3
cp -R "build/MetalAccompaniment_artefacts/Debug/VST3/Metal Accompaniment.vst3" ~/Library/Audio/Plug-Ins/VST3/
# Use Release instead of Debug if you built Release.
```

Then in **Reaper**: **Options → Preferences → Plug-ins → VST → Re-scan** (or **Clear cache and re-scan VST paths**).

### If it still “doesn’t show” in the FX window

Reaper often lists this plugin under **category Tools**, not under EQ/Dynamics/Reverb. The scanned name is `**Metal Accompaniment (Niall Giblin)`**.

1. **Add FX** (or **FX** on a track) → click the **search** field and type `**Metal`**, `**Accompaniment**`, or `**Niall**`.
2. Or open the **Tools** category in the tree and look for **Metal Accompaniment**.
3. Turn **off** any browser filters (e.g. “favorites only”, “JS only”, or a developer whitelist).
4. Confirm **VST3** is enabled: **Preferences → Plug-ins → VST** — VST3 scanning should be on; **VST plug-in paths** should list `**~/Library/Audio/Plug-Ins/VST3`** as a **folder** (not the `.vst3` bundle path — that can confuse the scanner).

**AU fallback (macOS):** copy the built `**Metal Accompaniment.component`** to `**~/Library/Audio/Plug-Ins/**` and choose **AU** in the FX browser (search the same name).

**Still missing?** Ensure **Reaper is Apple Silicon native** if your build is `arm64` (Activity Monitor → Kind column). An Intel-only Reaper will not load an arm64 VST3.

## Tests

### Quick run (all tests)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

### Run by tier

```bash
# Unit tests only (fast, no plugin link)
ctest --test-dir build --output-on-failure -L unit

# Integration tests (full processor pipeline + shadow replay)
ctest --test-dir build --output-on-failure -L integration

# E2E tests (synthesised audio through the live plugin)
ctest --test-dir build --output-on-failure -L e2e
```

### Python tests (training pipeline)

```bash
pip install -r training/requirements.txt pytest
cd training && python3 -m pytest tests/ -v
```

### Generate WAV fixtures for manual inspection

```bash
pip install numpy soundfile
python3 scripts/generate_test_fixtures.py        # writes to test/fixtures/
```

### Regenerate E2E structure-transition golden file

Run the test binary with the env var set — it writes the golden and then fails; unset and re-run to verify:

```bash
MA_WRITE_E2E_GOLDEN=1 ./build/MetalAccompanimentIntegrationTests "[e2e][transitions]"
```

## Reaper setup

1. Install the plugin as above, then **re-scan VST3**. Insert **Metal Accompaniment** on your **guitar** track.
2. Add a **new track** with Superior Drummer, EZdrummer, or another drum instrument.
3. Route **MIDI** from the guitar/plugin track to the drum track: use Reaper’s track routing so the drum track receives MIDI sent by the plugin (e.g. **MIDI Hardware Output** / virtual routing, or record-arm the drum track with input from the guitar track — exact clicks depend on your Reaper version).
4. Play: the plugin outputs **channel 10** (drums) and **channel 2** (bass); point your drum instrument at the appropriate input.

See **[`ARCHITECTURE.md`](ARCHITECTURE.md)** for threading and components; **[`.planning/ROADMAP.md`](.planning/ROADMAP.md)** for current phased work.

```bash
cd /Users/ng/Desktop/fuzzyband

export ONNXRUNTIME_ROOT="/Users/ng/Downloads/onnxruntime-osx-arm64-1.24.4"

cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release \
  -DMA_BUILD_STANDALONE=ON \
  -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_ROOT"

cmake --build build-onnx --config Release --parallel
```

```bash
DEST="$HOME/Desktop/MetalAccompaniment-onnx-build"
rm -rf "$DEST"
/Users/ng/Desktop/fuzzyband/scripts/stage-and-sign-macos-plugins.sh "$DEST"

mkdir -p "$HOME/Library/Audio/Plug-Ins/VST3"
cp -R "$DEST/Metal Accompaniment.vst3" "$HOME/Library/Audio/Plug-Ins/VST3/"
```
