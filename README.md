# Metal Accompaniment

JUCE **8** **VST3 / AU** plugin: listens to guitar audio and outputs **drum + bass MIDI** in real time using rule-based tempo, energy, and structure detection (Phase 1 — no ML). Bass patterns use a **fixed root (E2)** until pitch tracking lands in a later phase.

## Phase 2 data & training (prep)

- **[Dataset audit & go/no-go](docs/DATASET_AUDIT.md)** — symbolic corpus shortlist and license checklist
- **[Tokenization](docs/TOKENIZATION.md)** — event JSON/JSONL contract for offline prep
- **[Training / prep tooling](training/README.md)** — Python venv, `prep_midi.py` CLI

## Pattern MIDI export (offline)

After building the `MetalAccompanimentExportPatterns` target, run:

```bash
./build/MetalAccompanimentExportPatterns
```

This writes `metal_accompaniment_patterns.mid` next to the executable (useful for checking patterns in Reaper before relying on the live plugin).

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

Reaper often lists this plugin under **category Tools**, not under EQ/Dynamics/Reverb. The scanned name is **`Metal Accompaniment (Niall Giblin)`**.

1. **Add FX** (or **FX** on a track) → click the **search** field and type **`Metal`**, **`Accompaniment`**, or **`Niall`**.
2. Or open the **Tools** category in the tree and look for **Metal Accompaniment**.
3. Turn **off** any browser filters (e.g. “favorites only”, “JS only”, or a developer whitelist).
4. Confirm **VST3** is enabled: **Preferences → Plug-ins → VST** — VST3 scanning should be on; **VST plug-in paths** should list **`~/Library/Audio/Plug-Ins/VST3`** as a **folder** (not the `.vst3` bundle path — that can confuse the scanner).

**AU fallback (macOS):** copy the built **`Metal Accompaniment.component`** to **`~/Library/Audio/Plug-Ins/`** and choose **AU** in the FX browser (search the same name).

**Still missing?** Ensure **Reaper is Apple Silicon native** if your build is `arm64` (Activity Monitor → Kind column). An Intel-only Reaper will not load an arm64 VST3.

## Tests

```bash
ctest --test-dir build --output-on-failure
```

## Reaper setup

1. Install the plugin as above, then **re-scan VST3**. Insert **Metal Accompaniment** on your **guitar** track.
2. Add a **new track** with Superior Drummer, EZdrummer, or another drum instrument.
3. Route **MIDI** from the guitar/plugin track to the drum track: use Reaper’s track routing so the drum track receives MIDI sent by the plugin (e.g. **MIDI Hardware Output** / virtual routing, or record-arm the drum track with input from the guitar track — exact clicks depend on your Reaper version).
4. Play: the plugin outputs **channel 10** (drums) and **channel 2** (bass); point your drum instrument at the appropriate input.

See **`ROADMAP_PHASE_1.md`** for milestones and **`ARCHITECTURE.md`** for threading and components.

## Phase 1 tracking

Progress is tracked in **`Phase 1 TODO.md`**.
