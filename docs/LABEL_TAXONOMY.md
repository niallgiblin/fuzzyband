# Label Taxonomy — Perception vs Arrangement

**Status:** Phase 3 deliverable of `docs/DATA_STRATEGY.md` (§5.2, §5.6).
This is the single reference for *what gets labeled by whom*, and where each label
source plugs into the code.

---

## 1. The two layers

The strategy's central insight (`DATA_STRATEGY.md` §0) is to **split perception
from arrangement** so that self-labeling stays honest and learnable.

| Layer | Learns | Label source | Self-labeled? |
|---|---|---|---|
| **Perception** | guitar audio → playing-style + intensity | your honest self-labels on real captures | **Yes** |
| **Arrangement** | style + intensity + section → drum/bass pattern | external datasets (Phase 4) + energy-based `StructureTagger` | **No** |

You self-label only what your playing *physically determines* (articulation and
dynamics). Structural/arrangement knowledge (verse vs chorus, groove feel,
selection priors) comes from datasets and rules — never from self-annotation,
because a 370 ms guitar window cannot tell a verse riff from a chorus riff.

---

## 2. Perception labels (self-labeled)

Canonical, machine-readable source of truth: **`training/perception_taxonomy.py`**.
Every offline tool imports it — do not re-declare the list anywhere else.

**Style classes** (the class index is authoritative — never reorder):

| idx | label | what it captures |
|---|---|---|
| 0 | `palm_mute` | muted chugs; distortion collapses centroid, `subBassRatio` is the tell |
| 1 | `open_chord` | ringing open/power chords |
| 2 | `single_note` | single-note lines / lead runs |
| 3 | `sustain` | held notes / drones |
| 4 | `silence` | not playing |

**Intensity** (secondary self-label, also derivable from the energy analyser):
`soft`, `loud`.

These are the labels you attach to a recorded take with `slice_annotations.py`
(see §4). They are physically grounded, so tagging them is truthful — this is the
circularity break.

---

## 3. Arrangement labels (dataset / rule-sourced — NOT self-labeled)

Section identity (`INTRO / VERSE / CHORUS / BREAKDOWN / SOLO / OUTRO`), groove feel,
and pattern selection are **never** self-labeled. They come from:

- the energy-based `StructureTagger` over time (`src/analysis/StructureTagger.*`), and
- external datasets integrated in **Phase 4** (E-GMD groove feel, Lakh selection
  priors, DadaGP articulation grammar).

---

## 4. How the layers compose: `style + intensity + section → pattern-pool`

The two layers meet in `src/inference/pattern_rules.h` (header-only, unit-tested),
which is the single source of truth for pool mappings:

- **Perception → pool:** `PatternRules::stylePatternPool(styleIndex)` maps a
  perception style (0–4, same order as §2) to a small pattern pool. E.g. index 0
  (`palm_mute`) → `{7 half-time, 1 verse groove, 9 sparse breakdown}`.
- **Section → pool:** `PatternRules::sectionPatternPool(name)` /
  `sectionPatternPoolForGenre(name, genreId)` map a section to its pattern pool for
  Play (song-form) mode.
- **Intensity/energy → variant:** `PatternRules::diversifyPattern` /
  `diversifyPatternForGenre` route within a pool using RMS/centroid/BPM/bar-phase.

The composition is transparent: perception picks the *pool*, section priors and
energy pick the *variant within the pool*.

---

## 5. Phase 4 interface plug points (§5.6)

Phase 3 only defines the interfaces the external datasets plug into; the datasets
themselves are Phase 4. Those interfaces already exist in the codebase:

| Phase 4 workstream | Feeds | Plug point (already in code) |
|---|---|---|
| **C1** E-GMD groove feel | velocity hierarchy + microtiming | `src/midi/GrooveTemplate.h` — `Groove::Template` (fixed-size struct read on the audio thread; C1 replaces the hand-authored `rock()/metal()/punk()` values with E-GMD stats, no rendering-code change) |
| **C2** Lakh selection/section priors | pattern-pool weighting + section orderings | `src/inference/pattern_rules.h` — `sectionPatternPool*` / `stylePatternPool` tables (weighted at build time, not the RT path) |
| **C3** DadaGP articulation grammar | augments/validates perception classifier | `training/perception_taxonomy.py` — the style label set (human labels stay authoritative) |
| **C4** Slakh/MoisesDB generative *(gated)* | simultaneous drum+bass+guitar supervision | behind the stable `IInference` interface (`src/inference/IInference.h`) — added later with no rework |

See `DATA_STRATEGY.md` §6 for the full Phase 4 plan and the C4 gate.
