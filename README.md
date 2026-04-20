# Metal Accompaniment

JUCE **8** **VST3 / AU** plugin: listens to guitar audio and outputs **drum + bass MIDI** in real time. **Default builds** use rule-based tempo, energy, and structure plus **optional ONNX** pattern/structure/bass when built with `MA_ENABLE_ONNX=ON` and models in `assets/` (see [CONTRIBUTING.md](CONTRIBUTING.md)). Pitch-aware bass routing is in the tree; generative bass needs ONNX + `bass_model.onnx`.

## Version & roadmap (source of truth)


| What                                                                  | Where                                                                                                                                              |
| --------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------- |
| **GSD milestone & phase** (e.g. v0.3.0, Phase 17 done, Phase 18 next) | `[.planning/STATE.md](.planning/STATE.md)` and `[.planning/ROADMAP.md](.planning/ROADMAP.md)`                                                      |
| **Shipped v0.2.0 narrative** (Phases 9–16, archived)                  | `[.planning/milestones/v0.2.0-ROADMAP.md](.planning/milestones/v0.2.0-ROADMAP.md)`                                                                 |
| **Plugin version string** in the editor (e.g. `v0.3.0`)               | `CMakeLists.txt` → `project(MetalAccompaniment VERSION …)` — rebuild `**MetalAccompaniment_VST3`** / `**MetalAccompaniment_AU**` after changing it |


**Milestone snapshot:** **v0.1.0** = Phases 1–8 (rule MVP). **v0.2.0** = Phases 9–16 (ML + generative stack, ONNX optional). **v0.3.0** = Phases 17–20 (real GMD pipeline + trained models → `assets/*.onnx`); **Phase 17** (data pipeline) is complete; **18–20** are next.

## Phase 2 data & training (prep)

- **[Dataset audit & go/no-go](docs/DATASET_AUDIT.md)** — symbolic corpus shortlist and license checklist
- **[Tokenization](docs/TOKENIZATION.md)** — event JSON/JSONL contract for offline prep
- **[Training / prep tooling](training/README.md)** — Python venv, `prep_midi.py` CLI

## ONNX inference (Phase 10)

- **[ONNX I/O contract](docs/ONNX_IO.md)** — frozen tensor names / shapes consumed by the plugin; Phase 15 export must match
- Optional ONNX Runtime builds are enabled with `-DMA_ENABLE_ONNX=ON` and `-DONNXRUNTIME_ROOT=/path/to/onnxruntime` — full setup is in **[CONTRIBUTING.md](CONTRIBUTING.md)** (§ONNX Runtime)
- Default builds (and CI) leave `MA_ENABLE_ONNX=OFF`; the plugin falls back to `RuleBasedInference` when ONNX is disabled or model load fails

## Generative bass (Phase 13)

When `MA_ENABLE_ONNX=ON` and `bass_model.onnx` loads successfully, the background inference thread runs an **additional** ONNX `Run` on each feature drain (same ~50 Hz cadence as pattern/structure inference — order of magnitude, not a hard guarantee). **CPU / latency:** expect roughly one extra small-session inference per drain versus ONNX builds without the bass head; for exact numbers, profile the plugin locally with your buffer size and DAW.

**Degradation:** If the bass model is missing, fails to load, or the proposal’s confidence is below `kMinGenerativeConfidence` in `AccompanimentProcessor.cpp`, the plugin falls back to **static pattern bass only** (no generative rank/select). See `docs/BASS_ONNX_IO.md` for the tensor contract.

Contributor setup for the bass asset is in **[CONTRIBUTING.md](CONTRIBUTING.md)** (§ONNX Runtime).

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
- `**genre**` — Style preset (`Metal`, `Metalcore`, `Death`, `Progressive`, `Black`) for pattern policy remapping.
- `**intensity**` — Shifts effective BPM tier on the **rule-based** path ( ONNX pattern tensor still uses analyzed BPM).
- `**variation`** — Small pattern-index nudge after genre mapping (shared rule/ONNX post-processing).
- `**structureBlend**` — When ML structure is valid, values **≥ 0.5** use the ML shadow structure for pattern selection; values **below 0.5** follow rule structure.
- `**generativeBassMode`** — `**Auto**` (score vs library), `**On**` (force when proposal validates), `**Off**` (library bass only).

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

See `**ARCHITECTURE.md`** for threading and components. Historical Phase 1-only narrative: `**ROADMAP_PHASE_1.md**`. **Current** phased work: `**.planning/ROADMAP.md`**.

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

