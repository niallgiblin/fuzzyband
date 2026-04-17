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

## Linux (unsupported stretch)

The same CMake flags may work with a suitable compiler and JUCE’s Linux dependencies; this path is not regularly validated. Expect to install distro packages for ALSA, X11, and OpenGL as required by JUCE.

## Windows

Not part of Phase 1 contributor workflow; see project roadmap for later milestones.
