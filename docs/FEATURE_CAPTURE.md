# Feature Capture

Phase 34 adds a debug capture path for real plugin `FeatureVector` snapshots and runtime pattern predictions.

## Location

The plugin writes JSONL captures to a fixed local debug directory. It prefers:

`~/Documents/MetalAccompaniment/captures/`

If the host process cannot write there, it falls back to the JUCE user application data directory and then the system temp directory, preserving the same `MetalAccompaniment/captures` suffix.

The UI toggle is `Capture features`. There is no manual path selector and the path is not stored in host state.

## Real-Time Safety

file I/O does not run on the audio thread. `processBlock()` creates the canonical `FeatureVector` and pushes it through the existing lock-free queue. The capture row is built on the background inference thread after pattern selection, then handed to a bounded capture queue drained by a writer thread.

If the capture queue fills, rows are dropped and the dropped-row counter is shown in the UI. Capture also stops automatically at the safety cap: `30000` rows or `600` seconds, whichever comes first.

## JSONL Schema

Schema version: `feature_capture.v1`

The first row is a header:

```json
{"type":"header","schema_version":"feature_capture.v1","plugin_version":"0.5.2","sample_rate":48000.0,"block_size":256,"capture_start_utc":"2026-05-13T10:00:00Z","model_mode":"RuleBasedInference","max_rows":30000,"max_capture_seconds":600.0,"columns":["sample_timestamp","elapsed_seconds","bpm","rms_energy","spectral_centroid","high_freq_flux","state","state_name","pitch_root_midi","pitch_confidence","policy_intensity","rms_delta","rule_pattern_index","rule_pattern_name","active_pattern_index","active_pattern_name","onnx_pattern_index","onnx_pattern_name","onnx_available","model_mode"]}
```

Data rows have `type="feature"` and the same `schema_version`.

| JSON field | Source |
|------------|--------|
| `sample_timestamp` | `FeatureVector::sampleTimestamp` |
| `elapsed_seconds` | `sampleTimestamp / sample_rate` |
| `bpm` | `FeatureVector::bpm` |
| `rms_energy` | `FeatureVector::rmsEnergy` |
| `spectral_centroid` | `FeatureVector::spectralCentroid` |
| `high_freq_flux` | `FeatureVector::highFreqFlux` |
| `state`, `state_name` | effective `StructureState` for pattern selection |
| `pitch_root_midi` | `FeatureVector::pitchRootMidi` |
| `pitch_confidence` | `FeatureVector::pitchConfidence` |
| `policy_intensity` | `FeatureVector::policyIntensity` |
| `rms_delta` | `FeatureVector::rmsDelta` |
| `rule_pattern_index`, `rule_pattern_name` | rule-path prediction |
| `active_pattern_index`, `active_pattern_name` | active backend prediction |
| `onnx_pattern_index`, `onnx_pattern_name` | ONNX prediction, or `null` when unavailable |
| `onnx_available` | `true` only when the active pattern backend is ONNX |
| `model_mode` | active inference backend name |

## Annotation CSV

Use a sidecar CSV for human labels:

```csv
start_seconds,end_seconds,label_index,label_name
0.0,12.5,2,Verse mid
12.5,28.0,4,Chorus mid
```

Rules:

- `start_seconds` must be lower than `end_seconds`.
- `label_index` must be in `0..6`.
- `label_name` must match the canonical pattern name for the index.
- Ranges must not overlap.
- Captured rows outside ranges are ignored and reported as annotation coverage loss.

## Evaluator Handoff

Phase 34 evaluator input is the capture JSONL plus the annotation CSV. The evaluator reports rule accuracy, ONNX accuracy when available, rule-vs-ONNX agreement, per-class accuracy, confusion matrices, annotation coverage, and top disagreement rows.

The default ONNX threshold is passed as `--min-onnx-accuracy 0.65`.
