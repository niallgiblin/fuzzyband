# Learning and Quality Docs

These docs are for learning the project from the inside out. They explain what each subsystem does, how data moves through the plugin, and where the main quality risks are.

## Recommended Reading Order

1. [Codebase Walkthrough](CODEBASE_WALKTHROUGH.md)  
   Start here if you are new to C++, JUCE, or this repository. It explains the main classes and the "one audio block at a time" mental model.

2. [Runtime Architecture](RUNTIME_ARCHITECTURE.md)  
   Use this when you want to understand how audio, MIDI, inference, UI, and capture threads connect.

3. [C++ / JUCE / Audio Best Practices Audit](CPP_JUCE_AUDIO_BEST_PRACTICES_AUDIT.md)  
   The quality and safety review: allocations, real-time thread rules, atomics, locking, ONNX, state save/restore, and user-experience risks.

4. [Modularity and Bloat Review](MODULARITY_AND_BLOAT_REVIEW.md)  
   A maintainability review of what is clean, what is getting too large, and what can be trimmed or modularized.

## Existing Specialist Docs

- [Plugin guide](PLUGIN_GUIDE.md): practical behavior guide, but some older sections may describe behavior that has since been refactored.
- [Plugin hosting](PLUGIN_HOSTING.md): DAW insert-order and routing guidance.
- [Feature capture](FEATURE_CAPTURE.md): runtime capture workflow and JSONL schema.
- [ONNX I/O](ONNX_IO.md): pattern model tensor contract.
- [Bass ONNX I/O](BASS_ONNX_IO.md): generative bass tensor contract.
- [Section transitions](SECTION_TRANSITIONS.md): design thinking around phrase transitions.

## Most Important Source Files

| Area | Files |
|---|---|
| Plugin coordinator | `src/AccompanimentProcessor.h`, `src/AccompanimentProcessor.cpp` |
| UI | `src/AccompanimentEditor.h`, `src/AccompanimentEditor.cpp` |
| Audio analysis | `src/analysis/` |
| Pattern and structure inference | `src/inference/` |
| MIDI output | `src/midi/` |
| Feature capture | `src/capture/` |
| Build and tests | `CMakeLists.txt`, `tests/` |

