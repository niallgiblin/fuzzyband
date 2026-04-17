# Dataset audit (Phase 2 symbolic drum pretrain)

**Status:** Planning memo — not legal advice. Verify current terms on each dataset’s **official** page before redistribution or commercial use.

## Go / no-go (primary path)

**Go/no-go summary:** **Go** on using **Groove MIDI Dataset (GMD)** plus **Lakh MIDI** (drum-filtered) as the primary symbolic pretraining path for drum language and breadth, with **E-GMD** reserved for a **later** audio+aligned-MIDI phase only (not co-primary with GMD/Lakh).

## Ranked shortlist

| Rank | Dataset | Role | Notes |
|------|---------|------|--------|
| 1 | **Groove MIDI Dataset (GMD)** — Magenta | Primary drum *language* (timing, velocity, fills) | Symbolic-only drum performances; aligned with “GMD first” in project strategy. |
| 2 | **Lakh MIDI Dataset** | Scale / breadth; **drum-track filtering** | Large corpus; use filtered subsets for drum-focused pretrain; genre tags are weak — metal flavor via curation, not a single “metal corpus.” |
| 3 | **Slakh2100** | Optional multi-track research | Synthetic multi-track; genre-limited — secondary reference only for this product path. |
| 4 | **E-GMD** (Enriched Groove MIDI) | **Deferred** unless a phase needs **audio + aligned MIDI** | **Not** co-primary with GMD/Lakh for the current symbolic-first story. |

## Exclusions (explicit)

- **GuitarPro / tab scrapes and similar grey-area aggregations** — inconsistent licensing; often TOS violations; excluded from primary path.
- **Waveform-only generators** (e.g. MusicGen-class outputs) — not symbolic MIDI; incompatible with the plugin’s MIDI-centric `IInference` contract.
- **E-GMD as co-primary** — strategy keeps symbolic GMD + Lakh first; E-GMD is for audio-conditioned workflows later, not parallel primary with the symbolic pair above.
- **Licensed commercial MIDI pack catalogs** — not a v0.2.0 milestone line item; prefer filter/curate from public corpora first.

## License checklist (light)

Use this as a **solo-dev gate**, not counsel-ready due diligence. For each dataset you actually download or redistribute derivatives of, confirm on the **canonical** site:

| Check | Groove MIDI (Magenta) | Lakh MIDI |
|-------|----------------------|-----------|
| Intended use (research / commercial) | Confirm on Magenta / TensorFlow hub page | Confirm on Magenta / project page |
| Attribution | Note required credit lines | Note required credit lines |
| Redistribution of **original** files | Often restricted — prefer links + hashes | Often restricted — prefer links + hashes |
| Derivatives (tokenized JSON, checkpoints) | Re-check terms for your deployment model | Re-check terms for your deployment model |

**Disclaimer:** This memo summarizes project direction only. It is **not** legal advice.

## Official references (verify before use)

- **Groove MIDI Dataset:** [TensorFlow Datasets — groove](https://www.tensorflow.org/datasets/catalog/groove) and Magenta [Groove](https://magenta.tensorflow.org/datasets/groove) documentation.
- **Lakh MIDI Dataset:** [Magenta Lakh MIDI](https://magenta.tensorflow.org/datasets/lakh) (follow links to the dataset distribution and terms you actually use).

---

*Aligned with `.planning/phases/999.1-ml-phase-2-data-feasibility-research/999.1-CONTEXT.md` (GMD + Lakh primary; E-GMD not co-primary).*
