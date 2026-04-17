# Phase 11 — Pitch testing notes

## Synthetic sine (Catch2)

- **Sample rate:** 48 kHz  
- **Buffer:** 4096 samples  
- **Signal:** sine at 440 Hz, amplitude 0.8  
- **Expected MIDI note:** A4 ≈ 69.0  
- **Allowed error:** ±0.25 semitone (see `tests/test_pitch_estimator.cpp`)

## CI

Tests run on macOS GitHub Actions via:

```bash
ctest --test-dir build --output-on-failure --config Release
```

## Future ONNX pitch

When `MA_ENABLE_ONNX=OFF`, Phase 11 CI uses the YIN baseline only. An optional ONNX pitch head would reuse the same `FeatureVector` pitch fields and fall back to YIN when the model is absent or low-confidence — not required for Phase 11 automated tests.
