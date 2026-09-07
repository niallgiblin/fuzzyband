# Contributing to Metal Accompaniment

JUCE 8 VST3/AU plugin (macOS primary). Stack details: `CLAUDE.md` and `ARCHITECTURE.md`.

There is **one** local CMake tree: `build/`. ONNX Runtime, tests, and the standalone app are all on.

## Prerequisites (macOS)

- CMake **3.22+**
- Xcode Command Line Tools (or full Xcode)
- Git
- ONNX Runtime (`brew install onnxruntime`, or a release archive)

## Configure and build (matches CI)

`MA_ENABLE_ONNX` defaults to **ON**, so configure needs `ONNXRUNTIME_ROOT`. From the repository root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/opt/homebrew/opt/onnxruntime
cmake --build build --config Release --parallel
```

Install into the user plug-in folders with `./scripts/install-plugin-to-user.sh --build build --config Release`.

## Tests

```bash
ctest --test-dir build --output-on-failure --config Release
```

## Plugin development

### Plugin parameters (APVTS)

User-facing IDs match `AccompanimentProcessor::createParameterLayout()`:

`outputGain`, `intensity`, `structureBlend`, `generativeBassMode`.

On the audio thread, **`FeatureVector::policyIntensity`** is set from the **`intensity`** parameter before each feature is enqueued for the inference thread. **`FeatureVector::state`** carries the rule-based structure from analysis; in **`AccompanimentProcessor::drainFeatureQueueAndRunInference`**, when structure ONNX provides valid ML metrics, **`structureBlend`** selects the effective structure for pattern selection: if ML is invalid, rules win; otherwise **≥ 0.5** uses the smoothed ML shadow state, **below 0.5** keeps the rule state. Pattern selection then uses that effective state (see **`IInference::selectPattern`**, invoked from the inference thread — **`AccompanimentProcessor.cpp`**). Tensor contracts for ONNX pattern input are documented in **`docs/ONNX_IO.md`**.

## Build options

| CMake option | Default | Notes |
|--------------|---------|--------|
| `MA_BUILD_TESTS` | ON | Unit tests (`MetalAccompanimentTests`) |
| `MA_BUILD_STANDALONE` | ON | Standalone app target |
| `MA_ENABLE_ONNX` | ON | ONNX Runtime (production inference path) — requires `ONNXRUNTIME_ROOT` |
| `MA_BUNDLE_GROOVE_RENDERER` | ON | Bundles `assets/groove_renderer.onnx` |
| `ONNXRUNTIME_ROOT` | — | Path to ONNX Runtime root with `include/` and `lib/` |

### ONNX Runtime

1. Install via Homebrew (`brew install onnxruntime`) or download a **macOS** CPU archive from [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases).
2. Set `ONNXRUNTIME_ROOT` to the folder that contains `include/onnxruntime_cxx_api.h` and `lib/libonnxruntime.dylib` (Homebrew: `/opt/homebrew/opt/onnxruntime`).
3. Production models bundled via JUCE BinaryData: `assets/metal_groove.onnx`, `assets/style_cnn.onnx`, and `assets/groove_renderer.onnx`.

If `tryLoadModel()` fails at runtime, the processor falls back to `RuleBasedInference`.

Before running training scripts or export helpers, set up the **locked** Python environment in **`training/README.md`** (venv + `pip install -r training/requirements.txt`).

## API documentation (optional)

If [Doxygen](https://www.doxygen.nl/) is installed:

```bash
cmake --build build --target doxygen-docs
```

HTML output is written to `docs/doxygen/html/` (ignored by git). You can also run `doxygen Doxyfile` from the repo root.

## Linux (unsupported / best-effort)

Not regularly validated on Linux. Use the same ONNX-on `build/` path as macOS: download a Linux ONNX Runtime archive and pass `ONNXRUNTIME_ROOT` (`lib/libonnxruntime.so`). Expect to install distro packages for ALSA, X11, and OpenGL as required by JUCE.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure --config Release
```

## Windows

Not part of the primary contributor workflow; the release workflow builds Windows x64 VST3 + Standalone.
