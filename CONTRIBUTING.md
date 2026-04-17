# Contributing to Metal Accompaniment

JUCE 8 VST3/AU plugin (macOS primary). Stack details: `CLAUDE.md` and `ARCHITECTURE.md`.

## Prerequisites (macOS)

- CMake **3.22+**
- Xcode Command Line Tools (or full Xcode)
- Git

## Configure and build (matches CI)

From the repository root:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_STANDALONE=ON
cmake --build build --config Release --parallel
```

## Tests

```bash
ctest --test-dir build --output-on-failure --config Release
```

## Build options

| CMake option | Default | Notes |
|--------------|---------|--------|
| `MA_BUILD_TESTS` | ON | Unit tests (`MetalAccompanimentTests`) |
| `MA_BUILD_STANDALONE` | ON | Standalone app target |
| `MA_ENABLE_ONNX` | OFF | Phase 2 — ONNX Runtime |
| `ONNXRUNTIME_ROOT` | — | Required when `MA_ENABLE_ONNX=ON` (path to ONNX Runtime root with `include/` and `lib/`) |

### ONNX Runtime (optional — Phase 10)

1. Download a **macOS** ONNX Runtime archive from [onnxruntime releases](https://github.com/microsoft/onnxruntime/releases) (CPU, matching your architecture).
2. Extract and set `ONNXRUNTIME_ROOT` to the folder that contains `include/onnxruntime_cxx_api.h` and `lib/libonnxruntime.dylib`.
3. The bundled models are `assets/accompaniment_model.onnx` (minimal pattern stub) and `assets/structure_model.onnx` (Phase 12 structure head). Regenerate with:
   ```bash
   training/.venv/bin/python scripts/build_minimal_pattern_onnx.py
   training/.venv/bin/python scripts/build_minimal_structure_onnx.py
   ```
   JUCE `BinaryData` exposes `accompaniment_model_onnx` / `structure_model_onnx` (plus `*Size` length symbols) when `MA_ENABLE_ONNX=ON`.
4. Configure and build:

```bash
cmake -B build-onnx -DCMAKE_BUILD_TYPE=Release \
  -DMA_ENABLE_ONNX=ON \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime-osx-arm64-1.20.1
cmake --build build-onnx --parallel
```

If `tryLoadModel()` fails at runtime, the processor falls back to `RuleBasedInference` (same as `MA_ENABLE_ONNX=OFF`).

Example with tests off:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_TESTS=OFF
cmake --build build --config Release --parallel
```

## API documentation (optional)

If [Doxygen](https://www.doxygen.nl/) is installed:

```bash
cmake --build build --target doxygen-docs
```

HTML output is written to `docs/doxygen/html/` (ignored by git). You can also run `doxygen Doxyfile` from the repo root.

## Linux (unsupported / best-effort)

Use the **same three commands as CI** (not regularly validated on Linux):

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -DMA_BUILD_STANDALONE=ON
cmake --build build --config Release --parallel
ctest --test-dir build --output-on-failure --config Release
```

Expect to install distro packages for ALSA, X11, and OpenGL as required by JUCE. Phase 1 ONNX (`MA_ENABLE_ONNX`) remains off by default; set `ONNXRUNTIME_ROOT` when you enable it for Phase 2 work.

## Windows

Not part of Phase 1 contributor workflow; see project roadmap for later milestones.
