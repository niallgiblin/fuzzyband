# Dataset audit — symbolic drum training (Phase 9)

**Status:** Go/no-go memo for v0.2.0 (DATA-01)  
**Aligned with:** `.planning/phases/999.1-ml-phase-2-data-feasibility-research/999.1-CONTEXT.md`

This document is a **developer checklist**, not legal advice. **Re-read the official license / terms** on each dataset’s canonical site before commercial use or redistribution of derived data.

---

## Light license checklist (per dataset)

Use before relying on a corpus for training or shipping derived artifacts:

| Check | Question |
|-------|------------|
| L1 | Does the license allow **your intended use** (research vs commercial product)? |
| L2 | Is **attribution** required? If yes, plan where it appears (docs / repo). |
| L3 | Are **redistribution** or **derived dataset** sharing restricted? |
| L4 | Any **non-commercial** or **academic-only** clauses that block your use case? |

If any answer is unclear, **stop** and verify on the official source or seek counsel.

---

## Ranked shortlist

| Rank | Dataset | Role | Notes |
|------|---------|------|--------|
| 1 | **Groove MIDI Dataset (GMD)** | Primary **drum language** (timing, velocity, fills) | Magenta; symbolic drum performances. |
| 2 | **Lakh MIDI** | **Scale / breadth**; drum-track–filtered slices | Large; weak genre labels — use filters, not as “metal ground truth.” |
| 3 | Slakh2100 | Optional multi-track / orchestration experiments | Synthetic; genre-limited — not primary for drum-only pretrain. |

---

## Primary recommendation (go)

**Go** on using **GMD + Lakh (drum-filtered)** as the **primary symbolic pretrain** path:

- **GMD** teaches drum *language* in MIDI.
- **Lakh** adds volume and diversity when drum tracks are isolated/filtered.
- **Metal flavor** for v0.2.0: **curate / filter** from these public sources per 999.1 — **no** paid MIDI packs in this milestone scope.

---

## Explicit exclusions (not primary)

| Item | Reason |
|------|--------|
| **GuitarPro / tab scrapes** | Often **TOS / legally gray** — do not use as a primary path. |
| **E-GMD as co-primary** | Use **only** if a **later** phase needs **audio + aligned MIDI**; symbolic-only path does not require it (see strategy memo). |
| **Waveform-only generative models** (e.g. MusicGen-class) | Emit **audio**, not symbolic MIDI — **out of product contract** (`IInference` / MIDI plugin). |
| **MusicCaps / AudioSet / genre tags as drum supervision** | Wrong label type for symbolic drum accompaniment; optional unrelated pretrain only. |

---

## Canonical links (verify before use)

- **Groove MIDI Dataset:** [TensorFlow Magenta — Groove](https://magenta.tensorflow.org/datasets/groove) (follow to current download + terms).
- **Lakh MIDI Dataset:** Search for the **official Lakh / MAESTRO** distribution you intend to use; confirm license on the **host** page.

---

## Go / no-go summary

| Decision | Result |
|----------|--------|
| Primary symbolic path | **GO** — GMD + Lakh (drum-filtered), with checklist above |
| E-GMD primary | **NO-GO** for symbolic-only milestone (revisit if audio-conditioned training is scheduled) |
| Metal-only open corpus | **NO-GO** as a single clean public set — use **filter/curation** from GMD/Lakh + later optional licensed/commissioned work **outside** v0.2.0 scope per 999.1 |
