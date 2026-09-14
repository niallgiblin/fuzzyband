> **PARTIALLY STALE (audited 2026-09-14).** Written against v0.9.62 and not refreshed for the
> 1.0.x bass-mirror changes; some expected results no longer match. Useful as a playtest script.


# Fuzzyband — End-User Stress Test

**Plugin:** Metal Accompaniment / Fuzzyband
**Written against:** v1.0.0 (`CMakeLists.txt` version string, top-right of the UI)
**Purpose:** Play through every user-facing mode, style, genre, section, and drum pattern. Tick what you hear. Record the session. Hand the recording + this filled log to an agent to double-check.

Phase 9 automated contracts for Stations A–H are in [`docs/archive/PHASE9_ACCEPTANCE-1.0.0-rc.md`](archive/PHASE9_ACCEPTANCE-1.0.0-rc.md) (T9.4). This file is still the human DAW pass.

If a pattern or style **never appears** after you follow the trigger recipe, that is a product bug or a coverage gap — log it as **MISS**, do not skip it.

---

## 0. How to use this document

1. Print or keep this file open next to the DAW.
2. Confirm the UI version matches the header above. If it does not, stop — you are testing the wrong binary.
3. Do **Setup** once (~5 min).
4. Run the **Speed run** (Stations A–H). At 120 BPM with shortened lock/transition sliders this is ~45–60 minutes of playing, because there are **13 genres**. A half-hour pass is Station A–B + Metal/Rock Play + style hunt only (Section 10).
5. Fill every `Result` / `Heard` / `Pattern #` box as you go. Use the Pattern Atlas (Section 4) as an ear-training cheat sheet — the UI shows **index numbers**, not names.
6. Anything that fails, flickers, or never appears goes in the **Miss / bug log** (Section 8).
7. Record the session as described in Section 2 and paste the **Agent review prompt** (Section 9) with the files.

**Pass rule:** a feature passes only if you *heard or saw* the expected behaviour, not because you played the tab. “I played it but nothing happened” is a fail.

### Notation used here

```
Tuning (written): Drop C  —  C G C F A D   (low → high)
PM = palm mute    ○ = let ring    x = scratch / muted dead note
h = hammer-on     p = pull-off    / = slide up    \ = slide down
~ = sustain / vibrato
Count in 4/4:  1 e & a  2 e & a  3 e & a  4 e & a
```

Drop C matches the plugin’s bass root (C2). If you are in **standard E**, play the same shapes on the low E string. The plugin tracks **pitch class**, not absolute tuning — the bass should still follow the note name (E instead of C). Write your tuning in the session header.

Every riff is 1–4 bars. Loop it. Do not write a song. The point is to poke the engine.

---

## 1. Setup (do this once)

### 1.1 DAW graph (required)

Fuzzyband listens to **guitar audio** and emits **MIDI only** (drums ch 10, bass ch 2). Put it **before** amp/cab/saturation so analysis sees a dry-ish signal.

```
Guitar DI ──► Fuzzyband (this plugin) ──► (optional) dry monitor / mute
                      │
                      ├── MIDI ch 10 ──► drum VST (GM kit)
                      └── MIDI ch  2 ──► bass VST
```

- Drum kit must be **General MIDI**: kick 36, snare 38, closed hat 42, open hat 46, ride 51, ride bell 53, crash 49, china 52, splash 55, hi tom 48, mid tom 45, lo tom 41.
- Click during Record riff: kick 36 on beat 1, sidestick 37 on 2/3/4. If your kit has no 37, you will still hear the 1.
- Plugin **Output Gain** only scales the guitar passthrough, not MIDI. Set drums/bass levels on those tracks.

### 1.2 Transport

- Start the **DAW transport**. Tempo comes from the host playhead. If the host has no tempo, the plugin falls back to its internal BPM parameter (default 120).
- Buffer: 256 samples if you can. This is the production target.
- Confirm the UI **BPM:** readout matches the DAW tempo (±1).

### 1.3 Test sliders (speed-run values)

Set these **before** Station A so lock/transition cycles are short enough to finish:

| Control             | Speed-run                                         | Why                                                                                                               |
| ------------------- | ------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Genre               | start**Metal**, then sweep all **13** | Pools differ Rock-family vs Metal-family; each preset also changes velocity, ghosts, swing default, and BPM range |
| Swing               | 0.00                                              | Isolate groove; Station G turns it up                                                                             |
| LOCK (BARS)         | **4**                                       | Minimum. Default 16 is too slow for a full sweep                                                                  |
| TRANSITION (BARS)   | **4**                                       | Minimum is 2; 4 bars is enough to hear a section                                                                  |
| TRANSITION SECTIONS | **2**                                       | A → B →**A** → C → A (each contrast returns to the riff; **not** A-B-C-A)                         |
| Sections list       | see 1.4                                           | Default form is missing BREAKDOWN and SOLO                                                                        |

Restore LOCK=16 after the test if that is how you normally play.

### 1.4 Sections list (Play mode)

The default form is `INTRO:4, VERSE:8, CHORUS:8, VERSE:8, CHORUS:8, OUTRO:4`. That **never plays BREAKDOWN or SOLO**.

For this test, edit the Sections list to:

```
INTRO:4
VERSE:8
CHORUS:8
BREAKDOWN:8
SOLO:8
OUTRO:4
```

That is 40 bars. At 120 BPM ≈ 1:20 per Play pass. Play **stops after OUTRO** — it does not loop. You will run this once per genre (13 passes) in the full test.

### 1.5 Session header (fill before you play)

```
Date:              ________
Plugin version:    v______   (must match UI top-right)
DAW / buffer:      ________ / _____ samples
Host tempo:        ______ BPM
Tuning:            Drop C / Standard E / other: ________
Interface / DI:    ________
Drum VST:          ________     Bass VST: ________
ONNX path:         MetalGrooveInference visible?  Y / N   (if the UI ever prints the engine name)
Notes:             ________
```

Idle status on load must read: `Groove: idle - press Play or Record riff`. If drums are already playing, that is a fail (engine must stay silent until armed).

---

## 2. Recording for yourself and for an agent

Record **all four** of these if you can. Two is the minimum (UI + drums).

| Stem         | What                                   | Why the agent needs it                                       |
| ------------ | -------------------------------------- | ------------------------------------------------------------ |
| A. Plugin UI | Screen capture of the Fuzzyband window | BPM, State, Style, Pattern index, Groove / Transition status |
| B. Guitar DI | The audio Fuzzyband actually heard     | Confirms what you played vs what the engine classified       |
| C. Drums     | MIDI ch 10 rendered, or the kit audio  | Pattern identity, fills, crashes, click                      |
| D. Bass      | MIDI ch 2 rendered, or the bass audio  | Root tracking, lock vs live-mirror, transition harmony       |

Also keep this markdown with your ticks filled in. Filename suggestion:

```
fuzzyband-uat-YYYYMMDD-v0.9.48/
  ui.mov
  guitar.wav
  drums.wav
  bass.wav
  END_USER_STRESS_TEST.md   (this file, filled)
```

Talk while you play: “Station C, Metal, entering BREAKDOWN.” The agent can sync that to the timeline.

---

## 3. UI legend — what to watch every bar

| Readout               | Values you will see                                                                                                                    | Meaning                                                                                   |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------- |
| **BPM**         | 40–300                                                                                                                                | Host tempo. Must track DAW tempo changes within a bar or two                              |
| **State**       | SILENT / SOFT / LOUD                                                                                                                   | Energy machine. Hold times: SOFT→LOUD ~0.4 s, LOUD→SOFT ~2 s, to SILENT ~1 s            |
| **Style**       | Palm Mute / Open Chord / Single Note / Sustain / Silence                                                                               | Perception head. Must match what you are physically doing                                 |
| **Pattern**     | integer**0–27**                                                                                                                 | Index into the atlas below. Write the number, then look up the name                       |
| **Groove:**     | `idle` / `COUNT-IN` / `RECORDING bar n/4` / `LOCKED - bar n/N` / `transition` / `PLAYING`                                  | Arming + lock + song-form                                                                 |
| **Transition:** | `Transition - bar X/Y - N left` (SECTIONS=1) or `Transition B/C - bar X/Y - N left` (SECTIONS>1); Play shows `Section: INTRO/…` | Contrast after a riff lock, or current Play section. No CHORUS/SOLO suffix in Record riff |
| **Scope**       | bar-aligned waveform, notches 1-2-3-4, downbeat at left                                                                                | Must line up with audible beat 1                                                          |

**Pattern index is the ground truth for “did this groove fire.”** Memorise nothing — use Section 4.

---

## 4. Pattern atlas (ear training)

When Pattern shows `N`, this is what the kit should be doing. Tick `Heard` the first time you are sure.

GM map: **K**=kick 36  **S**=snare 38  **h**=closed hat  **H**=open hat  **R**=ride  **B**=ride bell  **C**=crash  **X**=china  **T**=toms.

| #  | Name               | Length | What you should hear                                                          | Heard |
| -- | ------------------ | ------ | ----------------------------------------------------------------------------- | ----- |
| 0  | Silent             | 1      | Nothing. No kick, no bass                                                     | ☐    |
| 1  | Verse Groove       | 2      | Backbeat S on 2/4, closed hats, ride-bell accent on beat 3 of bar 1           | ☐    |
| 2  | Verse Half-Time    | 1      | Snare on**beat 3 only**, sparse kicks, ride on 4                        | ☐    |
| 3  | Verse Fast         | 1      | Double-kick 16ths, S on 2/4, closed-hat timekeeping                           | ☐    |
| 4  | Chorus Mid         | 2      | **Crash on 1**, open hats, ride bell bar 2. Bigger than verse           | ☐    |
| 5  | Chorus Fast        | 1      | China on 1, constant double-kick, open hats, S on 2/4                         | ☐    |
| 6  | Breakdown          | 2      | Sparse, china, clustered ghost snares, crushing kicks                         | ☐    |
| 7  | Half-Time          | 1      | Straight half-time: K on 1, S on 2**and** 4 (not the sludge snare-on-3) | ☐    |
| 8  | Blast Beat         | 1      | Kick/snare**alternating 16ths**, ride quarters, crash on 1              | ☐    |
| 9  | Sparse Breakdown   | 2      | Almost nothing: china+kick on 1, then kick+snare on beat 3 of bar 2           | ☐    |
| 10 | Thrash             | 1      | Double-kick on 1 and 3, S on 2/4, driving closed hats                         | ☐    |
| 11 | Intro Build        | 4      | Bar 1 splash+ride taps → kicks enter → ghosts → tom fill into crash        | ☐    |
| 12 | Intro Full         | 2      | Full groove + crash, tom fill into crash at end of bar 2                      | ☐    |
| 13 | Pre-Chorus Rise    | 2      | Floor-tom + ride tension, tom build into crash                                | ☐    |
| 14 | Chorus Open Groove | 2      | Big crash + open hats + ride bell, china on bar 2                             | ☐    |
| 15 | Breakdown Full     | 2      | Heavier 6: more ghost snares, china, crash at the end                         | ☐    |
| 16 | Outro Decay        | 4      | Groove simplifies each bar, final crash on bar 4                              | ☐    |
| 17 | Fill Short         | 1      | Normal groove beats 1–3,**tom burst on beat 4** into crash             | ☐    |
| 18 | Fill Medium        | 1      | Groove 1–2,**toms from beat 3**                                        | ☐    |
| 19 | Fill Big           | 1      | **Full-bar tom roll** + crash. Loudest fill                             | ☐    |
| 20 | Verse Ghost        | 1      | Closed-hat 16ths with**ghost snares** on the e’s and a’s              | ☐    |
| 21 | Chorus Blast       | 1      | Double-kick + snare blast, ride bell, crashes                                 | ☐    |
| 22 | Rock Backbeat      | 1      | Classic rock: K 1/3, S 2/4, closed-hat 8ths                                   | ☐    |
| 23 | Rock Half-Time     | 1      | Snare on 3, sparse kicks, ride in bar 2 feel                                  | ☐    |
| 24 | Rock Shuffle       | 1      | Shuffle/triplet 8ths (Swing knob exaggerates this)                            | ☐    |
| 25 | Punk D-Beat        | 1      | Straight 8th kicks, S on 2/4, ride quarters. D-beat motorik                   | ☐    |
| 26 | Rock Ballad        | 2      | Soft ride + backbeats, bar 2 goes half-time                                   | ☐    |
| 27 | Rock 6/8 Feel      | 1      | Compound 8ths inside 4/4: kick / hats on triplets, snare on 3                 | ☐    |

If you hear a groove that matches a row but the index is different, write **both** — the renderer may be ornamenting (ghosts, micro-fills, swing) on top of a named pattern.

---

## 5. Coverage map — how every thing is supposed to fire

This is the contract. Use it when something does not appear.

### 5.1 Two user-facing modes (v0.9.30+)

The engine is **idle and silent** until you arm it.

| Mode                           | How you arm                               | What the drums do                                                                                                                                                                                                                                                                                                                          | What the bass does                                                                                                          |
| ------------------------------ | ----------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------------- |
| **Idle**                 | Load plugin, or press Forget after a take | Silence. Pattern 0. Style → Silence                                                                                                                                                                                                                                                                                                       | Nothing                                                                                                                     |
| **Play**                 | PLAY button                               | Walks the Sections list**once**. Pool rotation every 2 bars (4 for INTRO/BREAKDOWN/OUTRO). Last bar of each section = fill 17/18/19 **replacing** the groove for that window (same swing / velocity feel as the bar). Crash into the next section. **Stops and returns to idle after the last section** — it does not wrap to INTRO                                                                            | **Mirrors your live riff** — one bass note per detected attack, at the played pitch class. The authored/harmonic line is the **fallback**: it plays only in the gaps between mirrored notes (and before you start picking). Pickup into section changes               |
| **Record riff**          | Record riff                               | 1 bar click count-in, then 4 bars of click while you play, then**LOCK** that take for LOCK bars. Drums freeze on the captured groove                                                                                                                                                                                                 | Note-for-note learned riff for the whole lock. Root does**not** chase mid-lock                                        |
| **Post-lock transition** | Automatic after lock expires              | One contrast (B, then later C…) for TRANSITION BARS. Crash + build-up fill on entry. Status`Transition` (SECTIONS=1) or `Transition B` (SECTIONS>1) plus bar countdown — **not** `Transition B - CHORUS`. **Then back to A.** SECTIONS=1 is **A-B-A-B** (same B). SECTIONS=2 is **A-B-A-C-A**, not A-B-C-A | Leaves the riff; follows you / new section harmony. Replaying the recorded riff **cuts** that contrast at the **next bar** and re-locks |

Play and Record riff fight: starting Play **cancels** a capture. Do not overlap them.

### 5.2 Genres (dropdown — 13 presets)

Two **families** share pattern pools. Every preset still has its own velocity, ghost density, swing default, half-time bias, and BPM clamp — so you must hear all 13, not just one Metal and one Rock.

**Metal family** (metal/shared pools: 1–21 in Play; no 22–27 in Play)

| id | Genre        | Groove slot | Ghosts | BPM range | Suggested host | What you are listening for vs Metal                         |
| -- | ------------ | ----------- | ------ | --------- | -------------- | ----------------------------------------------------------- |
| 3  | Metal        | Metal       | 0.10   | 40–300   | 120            | Reference: chorus can produce**21 Blast**; tight grid |
| 4  | Sludge       | Sludge      | 0.05   | 40–220   | 70             | Lazier, fewer ghosts, half-time bias, do not exceed 220     |
| 5  | Thrash Metal | Metal       | 0.20   | 90–300   | 180            | Hotter, busier, min 90 — do not sit at 70                  |
| 6  | Death Metal  | Metal       | 0.25   | 80–300   | 180            | Even hotter velocities, dense                               |
| 7  | Black Metal  | Metal       | 0.30   | 100–300  | 180            | Fast floor (min 100), more ornaments than Metal             |
| 8  | Doom Metal   | Sludge      | 0.08   | 40–160   | 60             | Slow ceiling**160** — must not run away at 200       |
| 9  | Djent        | Metal       | 0.15   | 60–240   | 140            | Slight default swing 0.05, precise, max 240                 |

**Rock family** (rock-first pools: 22–26 in verse/chorus/outro)

| id | Genre        | Groove slot | Ghosts | BPM range | Suggested host | What you are listening for vs Rock                                                           |
| -- | ------------ | ----------- | ------ | --------- | -------------- | -------------------------------------------------------------------------------------------- |
| 0  | Rock         | Rock        | 0.35   | 40–300   | 120            | Reference: verse**22/23**, chorus **24 Shuffle / 25 D-Beat**, outro **26** |
| 1  | Hard Rock    | Hard Rock   | 0.30   | 40–300   | 120            | Hotter backbeats; preset swing 0.10 (confirm the**Swing slider** is the live control)  |
| 2  | Punk         | Punk        | 0.15   | 80–300   | 180            | Tight, near-grid, min 80, D-beat/shuffle in chorus                                           |
| 10 | Classic Rock | Rock        | 0.30   | 60–200   | 100            | Looser, swing 0.10, max 200                                                                  |
| 11 | Alternative  | Rock        | 0.35   | 60–220   | 120            | Slight swing 0.05, same pools as Rock                                                        |
| 12 | Grunge       | Rock        | 0.25   | 50–200   | 90             | Heavier half-time bias, max 200                                                              |

The 28-class ONNX model still has **five feel slots** (Rock / Hard Rock / Punk / Metal / Sludge). Broader presets map onto the closest slot — Thrash/Death/Black/Djent → Metal slot, Doom → Sludge slot, Classic/Alternative/Grunge → Rock slot. You are still testing the **preset** (velocity, ghosts, BPM clamp), not a new model class.

**Metal-family Play must be able to produce Blast (21). Rock-family Play must be able to produce Shuffle (24) and D-Beat (25).** Confirm that on the family representatives (Metal, Rock); subgenres use the same pools so the feel check is the extra work.

### 5.3 Play-mode section → pattern pools (guaranteed if you wait)

Phrasing: VERSE / CHORUS / SOLO hold a groove **2 bars** then pick a different pool member. INTRO / BREAKDOWN / OUTRO hold **4 bars**. Consecutive phrases must not repeat the same index.

**Metal family** (Metal, Sludge, Thrash, Death, Black, Doom, Djent)

| Section   | Pool (indices) | Names                                       |
| --------- | -------------- | ------------------------------------------- |
| INTRO     | 11, 12         | Intro Build, Intro Full                     |
| VERSE     | 1, 2, 3        | Verse Groove, Verse Half-Time, Verse Fast   |
| CHORUS    | 4, 14, 21      | Chorus Mid, Chorus Open, Chorus Blast       |
| BREAKDOWN | 6, 15, 9       | Breakdown, Breakdown Full, Sparse Breakdown |
| SOLO      | 4, 14          | Chorus Mid, Chorus Open                     |
| OUTRO     | 16             | Outro Decay                                 |

**Rock family** (Rock, Hard Rock, Punk, Classic Rock, Alternative, Grunge)

| Section   | Pool (indices) | Names                                                        |
| --------- | -------------- | ------------------------------------------------------------ |
| INTRO     | 11, 12         | Intro Build, Intro Full                                      |
| VERSE     | 22, 23, 1, 2   | Rock Backbeat, Rock Half-Time, Verse Groove, Verse Half-Time |
| CHORUS    | 4, 24, 25, 14  | Chorus Mid, Rock Shuffle, Punk D-Beat, Chorus Open           |
| BREAKDOWN | 6, 15, 9       | Breakdown, Breakdown Full, Sparse Breakdown                  |
| SOLO      | 4, 14, 24      | Chorus Mid, Chorus Open, Rock Shuffle                        |
| OUTRO     | 16, 26         | Outro Decay, Rock Ballad                                     |

Last bar of **every** section: fill **17 / 18 / 19** chosen by how loud you are (quiet → Short, mid → Medium, loud → Big). The fill **replaces** the groove from its window start and inherits that bar's swing / velocity. Play loud at the end of CHORUS to force Fill Big; play barely-there at the end of INTRO to force Fill Short. Record A/B section ends use a varying seed so Fill Big is reachable. The fill is armed on the last-bar downbeat (host grid), not a `beatInBar` fudge, so large buffers still land it in the outgoing bar.

### 5.4 Playing styles (perception head) → groove family

Style must show the label **while you are playing that articulation**. After it is stable, follow/transition selection is steered into this pool (filtered to SOFT vs LOUD so a style cannot force a structurally wrong groove).

**Metal family**

| Style       | Play this                | Pool                                            |
| ----------- | ------------------------ | ----------------------------------------------- |
| Palm Mute   | Tight chugs, lots of sub | 7 Half-Time, 1 Verse Groove, 9 Sparse Breakdown |
| Open Chord  | Ringing power chords     | 4 Chorus Mid, 6 Breakdown, 14 Chorus Open       |
| Single Note | Fast single-note runs    | 3 Verse Fast, 10 Thrash, 2 Verse Half-Time      |
| Sustain     | Held notes / drones      | 6 Breakdown, 9 Sparse, 7 Half-Time              |
| Silence     | Stop                     | 0 Silent                                        |

**Rock family**

| Style       | Play this         | Pool                                           |
| ----------- | ----------------- | ---------------------------------------------- |
| Palm Mute   | Tight chugs       | 23 Rock Half-Time, 22 Rock Backbeat, 9 Sparse  |
| Open Chord  | Open/power chords | 22 Rock Backbeat, 4 Chorus Mid, 14 Chorus Open |
| Single Note | Single-note lines | 3 Verse Fast, 24 Shuffle, 25 D-Beat            |
| Sustain     | Held notes        | 26 Ballad, 9 Sparse, 7 Half-Time               |
| Silence     | Stop              | 0 Silent                                       |

Style steering is the **only designed path** for several metal-extreme indices (7, 8, 10) and for 5 / 13 / 20 / 27. Those are **not** in Play-mode pools. Station D exists to force them during a post-lock transition (Play off, lock expired, drums still audible).

### 5.5 Patterns with no Play-mode pool (must hunt in Station D)

If these never appear after Station D, log **MISS** — do not excuse them.

| #  | Name            | Intended trigger                                                                                                                                                          |
| -- | --------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 5  | Chorus Fast     | Metal + LOUD + fast host tempo (≥160) + open/aggressive playing during transition                                                                                        |
| 7  | Half-Time       | Metal + Palm Mute (SOFT) or Sustain                                                                                                                                       |
| 8  | Blast Beat      | Metal + LOUD + BPM ≥ 160 + bright/open (high centroid) + dense 16th picking                                                                                              |
| 10 | Thrash          | Metal + Single Note + dense picking (≥8th-note chugs) + LOUD                                                                                                             |
| 13 | Pre-Chorus Rise | Metal + LOUD; ONNX class 13. Easy to miss — hunt it, log MISS if absent                                                                                                  |
| 20 | Verse Ghost     | Metal + SOFT + lighter ghost-note style playing                                                                                                                           |
| 27 | Rock 6/8 Feel   | Rock + SOFT + compound 6/8 riff.**Not in any style or section pool** — only if the classifier picks 27 and style does not override. Treat a miss as a coverage gap |

### 5.6 Fills, crashes, ornaments (always on)

These are not pattern indices you select; they layer on top.

| Feature                      | When                                                                                                                                                                                 |
| ---------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Fill 17/18/19                | Last bar of a Play section; energy picks the size                                                                                                                                    |
| Transition crash (49)        | Section change in Play; lock → transition; transition → next section; return to riff                                                                                               |
| Build-up fill                | Entry into a post-lock contrast section                                                                                                                                              |
| Micro-fill (two tom pickups) | End of a 4-bar phrase, not on a fill pattern                                                                                                                                         |
| Ghost notes                  | Genre ghost density: Rock 0.35, Alternative 0.35, Hard Rock / Classic Rock 0.30, Black 0.30, Death / Grunge 0.25, Thrash 0.20, Punk / Djent 0.15, Metal 0.10, Doom 0.08, Sludge 0.05 |
| Swing                        | Swing knob; Rock Shuffle (24) shows it most                                                                                                                                          |
| Guitar-energy swell          | Hit harder → drums/bass a bit louder (not a pattern change)                                                                                                                         |
| Click track                  | Only during Record riff count-in + 4-bar take                                                                                                                                        |

---

## 6. The playthrough

Tempo suggestions assume Drop C, metronome on, you playing **with** the DAW click. Change the **host** tempo, not a plugin knob (the BPM parameter is a fallback only).

Before each station: glance at Groove status so you know which mode you are in.

---

### Station A — Idle, arming, scope, silence (2 min)

Host tempo: **120**. Genre: **Metal**. Do **not** press Play or Record yet.

**A1. Idle**

- Do nothing for 4 bars, then play anything.

Tab (play this while idle — drums must stay dead):

```
  PM................................
C |0-0-0-0-0-0-0-0|0-0-0-0-0-0-0-0-|
  |1 e & a 2 e & a 3 e & a 4 e & a
```

| Check         | Expected                                                                             | Result | Notes                                                                                                                                    |
| ------------- | ------------------------------------------------------------------------------------ | ------ | ---------------------------------------------------------------------------------------------------------------------------------------- |
| Groove status | `idle - press Play or Record riff`                                                 | pass   |                                                                                                                                          |
| Pattern       | 0                                                                                    | pass   | Pattern changes while I play as expected though. 0 on silence                                                                            |
| Style         | Silence (until you play, then it may still not arm)                                  | pass   | Upon initial load of plugin style has weird symbols like $@ or something, presumeably to represent nothing but let's clean up the symbol |
| Drums / bass  | **Complete silence** even while you chug                                       | pass   |                                                                                                                                          |
| Scope         | Waveform appears when you play; downbeat notch 1 at left; playhead fills left→right | pass   |                                                                                                                                          |

**A2. Arm via Play, then stop**

- Press PLAY. Drums must start on (or within one bar of) beat 1, with a crash into INTRO.
- Press PLAY again to stop, **or let the form finish** — it must return to idle/silence after OUTRO (no wrap to INTRO).

| Check     | Expected                                                            | Result | Notes |
| --------- | ------------------------------------------------------------------- | ------ | ----- |
| Status    | `Groove: PLAYING` then back to idle                               | pass   |       |
| Section   | `Section: INTRO` on start                                         | pass   |       |
| Entry     | Crash / fill into first groove, not a random pickup mid-bar         | pass   |       |
| Completes | If you let the form run, it stops after the last section — no wrap | pass   |       |

---

### Station B — Record riff, lock, bass freeze, transition, return (8–10 min)

Stay at **120 BPM**. LOCK=4, TRANSITION BARS=4, TRANSITION SECTIONS=2. Genre **Metal** first. Two transition sections means **A-B-A-C-A** (return to the riff after every contrast), not A-B-C-A.

**B1. Count-in + capture**

Press **Record riff**. Status must go `COUNT-IN - play on 1`, then `RECORDING bar 1/4` … `4/4`. Click: kick on 1, stick on 2/3/4.

Play this **exact** 4-bar riff on the click (palm-mute C chug, accent on 1 and 3):

```
Drop C — “Lock Riff A”   PM throughout    ○ = let the 3-chord ring a 16th

Bar 1–2
C |--0-0-0-0-0-0-0-0-|--0-0-0-0-0-0-0-0-|
  |1 . & . 2 . & . 3 . & . 4 . & .

Bar 3 (move to Eb, still muted)
C |--3-3-3-3-3-3-3-3-|
Bar 4 (back to C, open the last hit)
C |--0-0-0-0-0-0-0-0○|
```

If you flub, press **Forget**, then Record again. Capture auto-locks when bar 4 ends if it heard ≥2 hits.

| Check             | Expected                                                                                                                    | Result | Pattern # | Notes |
| ----------------- | --------------------------------------------------------------------------------------------------------------------------- | ------ | --------- | ----- |
| Count-in          | 1 bar of click, no groove yet                                                                                               | pass   |           |       |
| Recording         | Button`Rec n/4 - N` with N climbing                                                                                       | pass   |           |       |
| Lock              | `Groove: LOCKED - bar 1/4 - 3 left…`                                                                                     | pass   |           |       |
| Drums during lock | **Frozen** — same groove all 4 bars, does not chase your dynamics                                                    | pass   |           |       |
| Bass during lock  | Loops the captured rhythm**and pitches** (C then Eb then C). Changing chord mid-lock must **not** yank the bass | pass   |           |       |
| Style during take | Palm Mute                                                                                                                   | pass   |           |       |

**B2. Mid-lock freedom (drums stay, bass stays)**

While still LOCKED, solo over the riff — the drums must not switch.

```
Single-note run (do NOT let this change the drums)

D |--------------------|--10-8----------|
A |--------------8-10--|---------10-8---|
F |------7-8-10--------|----------------|
C |--8-10--------------|----------------|
```

| Check | Expected                                      | Result                                                                        | Notes                                                                                      |
| ----- | --------------------------------------------- | ----------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------ |
| Drums | Unchanged for the rest of the lock            | pass                                                                          | The 'LOCK' refers to the first time the riff gets accompanied? then this works as expected |
| Style | May flip to Single Note — drums still frozen | Did not flip at all even when I changed style. Works outside record riff fine |                                                                                            |
| Bass  | Still the locked riff, not the solo notes     | FAIL. LOCKED RIFF DOESN'T PERSIST                                             |                                                                                            |

**B3. Lock expires → B, back to A, then C, back to A**

When the lock hits 0, you should hear a **crash + build-up fill**, status `Groove: transition` and `Section: Transition B - bar 1/4 - 3 left` (this station uses SECTIONS=2, so the letter is shown). With SECTIONS=1 the letter is omitted (`Section: Transition - bar …`) and the **same** contrast returns every time — it must not flip CHORUS then SOLO.

During B, play **open chorus chords** (so the bass can leave the riff):

```
Open power chords — “Transition B”

C |--0-------0-------|--3-------3-------|
G |--0-------0-------|--3-------3-------|
C |--0-------0-------|--3-------3-------|
  |  1       3          1       3
```

**Do not play Lock Riff A during B.** After 4 bars of B the drums must **crash back into the locked riff (A)** — `Groove: LOCKED` again. They must **not** go straight into C.

Stay off the lock riff during that second A as well (or you will just extend the lock). When that lock expires, C should start (`Transition: C - <other section>`), a **different** contrast from B. After 4 bars of C, crash back to A again.

The full shape with TRANSITION SECTIONS = 2 is **A-B-A-C-A**. If you hear A-B-C-A (C immediately after B, no riff in between), that is a **fail**.

| Check                             | Expected                                                     | Result | Section name | Pattern#s heard | Notes |
| --------------------------------- | ------------------------------------------------------------ | ------ | ------------ | --------------- | ----- |
| Entry                             | Crash + fill, not a splice                                   | ☐     |              |                 |       |
| B is a contrast                   | Not the riff’s own family                                   | ☐     |              |                 |       |
| Bass during B                     | Leaves the recorded riff; follows live chords / new harmony  | ☐     |              |                 |       |
| Return to A after B               | After 4 bars of B,`LOCKED` again, riff drums+bass back     | ☐     |              |                 |       |
| C appears after the second A      | New contrast,**not** the same name as B, another crash | ☐     |              |                 |       |
| Return to A after C               | Locked riff again                                            | ☐     |              |                 |       |
| Consecutive grooves in a contrast | Pattern index changes every 2–4 bars, no immediate repeat   | ☐     |              |                 |       |

**B4. Cut-on-riff**

Expire the next lock, and **as soon as B (or C) starts**, play Lock Riff A again. That contrast must **cut short at the next bar** and re-lock. A *different* riff must not cut the contrast. The other contrast in the pair should still be reachable on a later expiry (you already heard both in B3).

| Check             | Expected                                     | Result | Notes |
| ----------------- | -------------------------------------------- | ------ | ----- |
| Natural A-B-A-C-A | Heard in B3                                  | ☐     |       |
| Cut on riff       | Playing A during B or C re-locks at the next bar | ☐     |       |

**B5. Forget**

Press **Forget**. Must go idle, drums die, Record riff re-enabled.

| Check  | Expected                   | Result | Notes |
| ------ | -------------------------- | ------ | ----- |
| Forget | Idle + silence + Pattern 0 | ☐     |       |

---

### Station C — Play mode: every section × every genre (~20–30 min)

This is the **guaranteed** pattern sweep. Host tempo **120** unless a row says otherwise. Sections list from 1.4. Swing 0.

For **Metal** and **Rock**: press PLAY, play the matching tab through the whole form, write every Pattern index you see, and **let it stop by itself after OUTRO**. Then next genre.

For the **other 11 genres**: play the same form once at the suggested tempo, confirm the family pools, the feel notes in 5.2, and that Play **stops** after OUTRO. You do not have to fill every index cell again, but you do have to hear the pass.

**Play louder in CHORUS / SOLO** (open chords) and **quieter / tighter in VERSE** so State can go LOUD vs SOFT even though Play ignores State for pattern choice — you still want the Style and State readouts honest, and last-bar fill size follows RMS.

#### Tabs for the form (loop each section’s tab until the UI section changes)

**INTRO — sparse, let it build (4 bars)**

```
Single notes, quiet, lots of air

C |--0-----------0---|--------------0---|
  |  1           3    1             4
```

**VERSE — palm-mute chug (8 bars)**

```
PM................
C |--0-0-0-0-3-3-0-0-|--0-0-0-0-5-5-3-3-|
  |1 . & . 2 . & . 3 . & . 4 . & .
```

**CHORUS — open power chords, hit hard (8 bars)**

```
○ let ring
C |--0-------0-------|--8-------7-------|
G |--0-------0-------|--8-------7-------|
C |--0-------0-------|--8-------7-------|
  |  1       3          1       3
```

**BREAKDOWN — slow chugs, space, heavy (8 bars)**

```
PM          PM
C |--0-------0-0-----|--0-------0-------|
  |  1       3 &        1       3
```

**SOLO — single-note line over the form (8 bars)**

```
F |--------------5-7-|--8-7-5-----------|
C |--0-0-5-7-8-------|----------8-7-5-0-|
```

**OUTRO — decaying, then stop on last bar (4 bars)**

```
C |--0---------------|--0---------------|
  |  1                  1
   (bars 3–4: play nothing — test decay + final crash)
```

On the **last bar of each section**, listen for a tom fill. Dig in on CHORUS last bar (want **19 Fill Big**). Ghost the last bar of INTRO (want **17 Fill Short**).

#### C-Metal (genre Metal) — log

| Section   | Must-see indices (any of) | Indices you saw | Fill # on last bar | State | Style | Pass? |
| --------- | ------------------------- | --------------- | ------------------ | ----- | ----- | ----- |
| INTRO     | 11, 12                    |                 |                    |       |       | ☐    |
| VERSE     | 1, 2, 3                   |                 |                    |       |       | ☐    |
| CHORUS    | 4, 14, 21                 |                 |                    |       |       | ☐    |
| BREAKDOWN | 6, 15, 9                  |                 |                    |       |       | ☐    |
| SOLO      | 4, 14                     |                 |                    |       |       | ☐    |
| OUTRO     | 16                        |                 |                    |       |       | ☐    |

Chorus **must** be able to produce **21 Chorus Blast** within 8 bars (pool rotates every 2 bars, 3 members — wait the full 8). If 21 never appears, MISS.

Breakdown **must** rotate through 6, 15, **and** 9 (4-bar hold, 8-bar section = 2 phrases — you may need **two Play passes** or lengthen BREAKDOWN to 12 to hear all three). If a pool member never appears after two passes, MISS.

After OUTRO, Play **must stop** (idle, silence). A second INTRO without pressing PLAY is a fail. A second Play press is allowed if you need another pass for 21 / 9.

#### C-Sludge (genre Sludge) — same tabs, play **slower and heavier**

Set host tempo **70**. Same form. Pools are the metal pools; feel should be lazier, fewer ghosts, less chorus lift.

| Check             | Expected                    | Result | Notes |
| ----------------- | --------------------------- | ------ | ----- |
| Tempo             | UI BPM ≈ 70                | ☐     |       |
| Pools             | Same indices as Metal table | ☐     |       |
| Feel vs Metal     | Darker, less busy ornaments | ☐     |       |
| Kit still in time | No rushing at 70            | ☐     |       |
| Stops after OUTRO | Idle, not a second INTRO    | ☐     |       |

#### C-Rock (genre Rock) — host **120**, same form, rock tabs if you want more “bar band”

Verse alternative (open-ish 8ths, not all muted):

```
C |--0-0-0-0-0-0-0-0-|--0-0-3-3-5-5-0-0-|
```

Chorus alternative (big open):

```
C |--0-0-0-0-0-0-0-0-|--8-8-8-8-7-7-7-7-|
G |--0-0-0-0-0-0-0-0-|--8-8-8-8-7-7-7-7-|
```

| Section   | Must-see indices | Indices you saw | Fill # | Pass? |
| --------- | ---------------- | --------------- | ------ | ----- |
| INTRO     | 11, 12           |                 |        | ☐    |
| VERSE     | 22, 23, 1, 2     |                 |        | ☐    |
| CHORUS    | 4, 24, 25, 14    |                 |        | ☐    |
| BREAKDOWN | 6, 15, 9         |                 |        | ☐    |
| SOLO      | 4, 14, 24        |                 |        | ☐    |
| OUTRO     | 16, 26           |                 |        | ☐    |

Rock chorus **must** be able to show **24 Shuffle** and **25 D-Beat**. Rock verse **must** show **22** and **23**. Outro **must** be able to show **26 Ballad** (2 members × 4-bar hold = 8 bars needed — bump OUTRO to 8 if 26 never appears in 4). After OUTRO, Play **must stop**.

#### C-Hard Rock (genre Hard Rock)

Same pools as Rock. You are listening for **hotter velocities** and a hint of default swing (preset swing 0.10 even with the knob at 0? Confirm: the **Swing slider** is the live control; genre default may apply on genre change — watch whether hats lilt when you switch genre without touching Swing).

| Check             | Expected                                       | Result | Notes |
| ----------------- | ---------------------------------------------- | ------ | ----- |
| Pools             | Same as Rock table                             | ☐     |       |
| Vs Rock           | Louder backbeats / slightly different hat lilt | ☐     |       |
| Swing slider      | Still 0 unless you moved it                    | ☐     |       |
| Stops after OUTRO | Idle, not a second INTRO                       | ☐     |       |

#### C-Punk (genre Punk) — host **180**, downstrokes

```
Punk verse (fast 8ths, muted)
C |--0-0-0-0-0-0-0-0-|--0-0-0-0-3-3-3-3-|   (downstrokes)

Punk chorus (open, still fast)
C |--0-0-0-0-0-0-0-0-|--0-0-0-0-0-0-0-0-|
G |--0-0-0-0-0-0-0-0-|--0-0-0-0-0-0-0-0-|
```

| Check             | Expected                                                                        | Result | Notes |
| ----------------- | ------------------------------------------------------------------------------- | ------ | ----- |
| BPM               | UI ≈ 180                                                                       | ☐     |       |
| Chorus            | 25 D-Beat and/or 24 Shuffle appear                                              | ☐     |       |
| Kit               | Tight, near-grid, fewer ghosts than Rock                                        | ☐     |       |
| Min BPM           | Punk preset min is 80 — at 180 this is N/A; do not drop below 80 in this genre | ☐     |       |
| Stops after OUTRO | Idle, not a second INTRO                                                        | ☐     |       |

#### C-Thrash Metal — host **180**, metal pools, aggressive downstrokes

Same metal tabs as C-Metal, faster. Do not sit below 90.

| Check             | Expected                                   | Result | Notes |
| ----------------- | ------------------------------------------ | ------ | ----- |
| BPM               | UI ≈ 180                                  | ☐     |       |
| Pools             | Same indices as Metal table                | ☐     |       |
| Feel vs Metal     | Hotter, busier, more ghosts (0.20 vs 0.10) | ☐     |       |
| Stops after OUTRO | Idle                                       | ☐     |       |

#### C-Death Metal — host **180**

| Check             | Expected                       | Result | Notes |
| ----------------- | ------------------------------ | ------ | ----- |
| BPM               | UI ≈ 180                      | ☐     |       |
| Pools             | Metal table                    | ☐     |       |
| Feel vs Metal     | Even hotter velocities, denser | ☐     |       |
| Stops after OUTRO | Idle                           | ☐     |       |

#### C-Black Metal — host **180** (do not drop below 100)

| Check             | Expected                                 | Result | Notes |
| ----------------- | ---------------------------------------- | ------ | ----- |
| BPM               | UI ≈ 180                                | ☐     |       |
| Pools             | Metal table                              | ☐     |       |
| Feel vs Metal     | Fast floor, more ornaments (ghosts 0.30) | ☐     |       |
| Stops after OUTRO | Idle                                     | ☐     |       |

#### C-Doom Metal — host **60** (ceiling 160 — a fail if it runs away at 200)

Same slow heavy tabs as Sludge.

| Check             | Expected                                    | Result | Notes |
| ----------------- | ------------------------------------------- | ------ | ----- |
| BPM               | UI ≈ 60                                    | ☐     |       |
| Pools             | Metal table                                 | ☐     |       |
| Feel vs Sludge    | Similar dark/half-time, even slower ceiling | ☐     |       |
| Stops after OUTRO | Idle                                        | ☐     |       |

#### C-Djent — host **140**

Palm-mute C chug, tight. Slight preset swing 0.05.

| Check             | Expected                         | Result | Notes |
| ----------------- | -------------------------------- | ------ | ----- |
| BPM               | UI ≈ 140                        | ☐     |       |
| Pools             | Metal table                      | ☐     |       |
| Feel vs Metal     | Precise, a hint of lilt, max 240 | ☐     |       |
| Swing slider      | Still 0 unless you moved it      | ☐     |       |
| Stops after OUTRO | Idle                             | ☐     |       |

#### C-Classic Rock — host **100**, rock tabs

| Check             | Expected                           | Result | Notes |
| ----------------- | ---------------------------------- | ------ | ----- |
| BPM               | UI ≈ 100                          | ☐     |       |
| Pools             | Same as Rock table                 | ☐     |       |
| Feel vs Rock      | Looser, preset swing 0.10, max 200 | ☐     |       |
| Stops after OUTRO | Idle                               | ☐     |       |

#### C-Alternative — host **120**, rock tabs

| Check             | Expected                          | Result | Notes |
| ----------------- | --------------------------------- | ------ | ----- |
| Pools             | Same as Rock table                | ☐     |       |
| Feel vs Rock      | Similar ghosts, slight swing 0.05 | ☐     |       |
| Stops after OUTRO | Idle                              | ☐     |       |

#### C-Grunge — host **90**, heavier rock tabs

| Check             | Expected                                             | Result | Notes |
| ----------------- | ---------------------------------------------------- | ------ | ----- |
| BPM               | UI ≈ 90                                             | ☐     |       |
| Pools             | Same as Rock table                                   | ☐     |       |
| Feel vs Rock      | Heavier half-time bias, fewer ghosts (0.25), max 200 | ☐     |       |
| Stops after OUTRO | Idle                                                 | ☐     |       |

#### C — Play-mode cross-checks (all genres)

| Check                      | Expected                                                                                       | Result | Notes |
| -------------------------- | ---------------------------------------------------------------------------------------------- | ------ | ----- |
| Phrase rotation            | Groove holds 2 bars (4 in INTRO/BREAKDOWN/OUTRO) then changes                                  | ☐     |       |
| No immediate repeat        | Next phrase ≠ previous index                                                                  | ☐     |       |
| Last-bar fill              | Every section, a 17/18/19 instead of the pool groove                                           | ☐     |       |
| Crash on section change    | Audible 49 into the new section                                                                | ☐     |       |
| Bass follows root          | Verse C chug → bass around C; chorus F/Eb chords → bass moves                                | ☐     |       |
| Bass not the recorded riff | Play-mode bass is harmonic, not Station B’s riff                                              | ☐     |       |
| Bass lead-in               | Last bar of a section, bass anticipates the next                                               | ☐     |       |
| Custom form                | Adding/removing a section in the list changes what Play does on the next start                 | ☐     |       |
| Stops after OUTRO          | Form does**not** wrap to INTRO. Groove → idle, drums die. Next PLAY restarts from INTRO | ☐     |       |
| Stop mid-form              | PLAY off → silence, next PLAY restarts from INTRO                                             | ☐     |       |

**Custom-form poke (30 s):** set Sections to `BREAKDOWN:4` only, PLAY, confirm you land in breakdown pools immediately. Then restore the full test form.

---

### Station D — Style + leftover patterns (the hunt) (8 min)

Goal: trigger Style labels **and** the patterns that Play mode cannot guarantee (5, 7, 8, 10, 13, 20, 27).

Method: Genre **Metal**, LOCK=4, **TRANSITION BARS=8** (the hunt needs more than 4 bars per contrast). Record a 4-bar palm-mute C chug (same as B1). Let it lock. **When transition B starts, do not play the lock riff** (so it does not cut). Play the style tabs below. Remember B returns to A after TRANSITION BARS — if A comes back, wait for the next lock expiry (C) or Forget and recapture. Do **not** expect B to chain straight into C.

Then Forget, switch Genre **Rock**, repeat the capture, hunt 22–27 / 24 / 25 / 26 / 27 during the Rock transition.

#### D1. Style classifier (Metal, during transition)

Play each articulations **in isolation** (4 bars, then 2 bars of silence to reset Style toward Silence).

**Palm Mute (must show Style: Palm Mute)**

```
Tight, near the bridge, no ring
C |--0-0-0-0-0-0-0-0-|--0-0-0-0-0-0-0-0-|
```

**Open Chord (Style: Open Chord)**

```
Full power chords, no mute, lots of bloom
C |--0-------0-------|--0-------0-------|
G |--0-------0-------|--0-------0-------|
C |--0-------0-------|--0-------0-------|
```

**Single Note (Style: Single Note)**

```
One string, no chords, 8th notes
C |--0-3-5-3-0-3-5-3-|--0-5-7-5-0-5-7-5-|
```

**Sustain (Style: Sustain)**

```
Hit once, hold, feedback/vibrato is fine
C |--0~~~~~~~~~~~~~~~|--~~~~~~~~~~~~~~~~|
G |--0~~~~~~~~~~~~~~~|--~~~~~~~~~~~~~~~~|
```

**Silence (Style: Silence, State: SILENT, Pattern: 0)**

Put the guitar down for **3+ seconds**. LOUD→SILENT hold is ~1 s, SOFT→SILENT ~1 s.

| Style       | UI showed it? | Pattern#s while it was showing | Pass? | Notes                                |
| ----------- | ------------- | ------------------------------ | ----- | ------------------------------------ |
| Palm Mute   | ☐            |                                | ☐    | Want 7 / 1 / 9                       |
| Open Chord  | ☐            |                                | ☐    | Want 4 / 6 / 14                      |
| Single Note | ☐            |                                | ☐    | Want 3 / 10 / 2                      |
| Sustain     | ☐            |                                | ☐    | Want 6 / 9 / 7                       |
| Silence     | ☐            | 0                              | ☐    | Drums should drop out if energy dies |

#### D2. Density / blast / thrash hunt (Metal, host **180**, LOUD)

During a fresh transition at 180 BPM, play **16th chugs**, pick hard, open the notes slightly so they are bright (high centroid helps blast):

```
16th metal chug — “Blast bait”
PM optional; keep it bright and loud
C |--0000000000000000|--0000000000000000-|  (16ths)
  |1e&a2e&a3e&a4e&a
```

Then a **single-note 16th run** (Thrash bait):

```
C |--0-0-3-3-5-5-3-3-|--0-0-5-5-7-7-5-5-|  (16ths)
```

| Target       | Index | Heard? | What you were playing | Notes |
| ------------ | ----- | ------ | --------------------- | ----- |
| Blast Beat   | 8     | ☐     |                       |       |
| Chorus Fast  | 5     | ☐     |                       |       |
| Chorus Blast | 21    | ☐     |                       |       |
| Thrash       | 10    | ☐     |                       |       |
| Verse Fast   | 3     | ☐     |                       |       |

If 8 and 10 never appear at 180 with dense picking, that is a **MISS** (rhythm-density rule is supposed to steer LOUD dense playing to thrash/blast).

#### D3. Half-time / sparse / ghost (Metal, host **80**, quieter)

```
Slow sludge chug
C |--0-------0-------|--0-------0-0-----|
```

```
Nearly nothing — one hit per bar (sparse / sustain)
C |--0---------------|------------------|
```

```
Soft 16th ghosts on one note (verse ghost bait)
C |--0-x-0-x-0-x-0-x-|--0-x-0-x-0-x-0-x-|
```

| Target           | Index | Heard? | Notes |
| ---------------- | ----- | ------ | ----- |
| Half-Time        | 7     | ☐     |       |
| Verse Half-Time  | 2     | ☐     |       |
| Sparse Breakdown | 9     | ☐     |       |
| Verse Ghost      | 20    | ☐     |       |
| Pre-Chorus Rise  | 13    | ☐     |       |

#### D4. Rock leftovers (Genre Rock, host **90**, then **140**)

At 90, play a **6/8 riff** (count 1-2-3-4-5-6, two groups per 4/4 bar):

```
6/8 feel in 4/4  (accents on 1 and 4 of the six)
Count: 1 a 2 a 3 a | 1 a 2 a 3 a     written as 4/4 triplets

C |--0-----0-----0---|----0-----0-----0-|
  |  1     &     3        &     4
     (compound: kick with 1 and the “and” of 2 in 6/8)
```

At 140, single-note punk line (shuffle / d-beat bait):

```
C |--0-0-0-0-0-0-0-0-|--3-3-3-3-5-5-5-5-|
```

Ballad bait (host **70**, big ringing chords, slow):

```
C |--0---------------|--7---------------|
G |--0---------------|--7---------------|
C |--0---------------|--7---------------|
```

| Target         | Index | Heard? | Notes                                    |
| -------------- | ----- | ------ | ---------------------------------------- |
| Rock Backbeat  | 22    | ☐     | Also guaranteed in Play verse            |
| Rock Half-Time | 23    | ☐     |                                          |
| Rock Shuffle   | 24    | ☐     |                                          |
| Punk D-Beat    | 25    | ☐     |                                          |
| Rock Ballad    | 26    | ☐     |                                          |
| Rock 6/8 Feel  | 27    | ☐     | **Likely gap if absent — log it** |

---

### Station E — Tempo, energy, State machine (4 min)

Genre **Metal**. Press PLAY so drums are audible. Change **host tempo** while playing the verse chug.

| Step | Host BPM | Play                             | Expected                                                                                                           | Result | Notes |
| ---- | -------- | -------------------------------- | ------------------------------------------------------------------------------------------------------------------ | ------ | ----- |
| E1   | 60       | slow chugs                       | UI BPM ≈ 60, kit in time, no double-time runaway                                                                  | ☐     |       |
| E2   | 120      | same riff                        | Smooth catch-up within ~2 bars                                                                                     | ☐     |       |
| E3   | 200      | 8th/16th chugs                   | UI ≈ 200, kit still even (Punk/Metal max 300; Sludge max 220 — do not use Sludge here)                           | ☐     |       |
| E4   | 120      | verse mute → slam chorus chords | State SOFT → LOUD within ~0.5 s                                                                                   | ☐     |       |
| E5   | 120      | chorus → back to mute           | LOUD → SOFT takes ~2 s (must**not** flicker every hit)                                                      | ☐     |       |
| E6   | 120      | stop playing                     | SILENT after ~1 s; Play mode**keeps drums going** (song form). Confirm drums continue in Play despite SILENT | ☐     |       |
| E7   | 120      | Play OFF, stop                   | After Forget: SILENT + Pattern 0 + no MIDI                                                                         | ☐     |       |

Phrase breath (Play off, armed via a lock/transition): pause 1 beat then resume. Should **not** fully reset; a crash on re-entry is allowed.

---

### Station F — Bass pitch tracking (3 min)

Play mode ON, Metal, 120 BPM, verse section. Play each root for 2 bars. Bass should sit on that pitch class in the C1–C3-ish register (library root C2).

```
C  |--0-0-0-0-0-0-0-0-|     bass → C
C# |--1-1-1-1-1-1-1-1-|     bass → C# / Db
D  |--2-2-2-2-2-2-2-2-|     bass → D
Eb |--3-3-3-3-3-3-3-3-|     bass → Eb
E  |--4-4-4-4-4-4-4-4-|     bass → E
F  |--5-5-5-5-5-5-5-5-|     bass → F
G  |--7-7-7-7-7-7-7-7-|     bass → G
Bb |--10-10-10-10----|      bass → Bb
```

Octave jump on the guitar (same pitch class):

```
C |--0---------------| then  |--12--------------|   (12th fret C)
```

Bass should stay in its register (not leap an octave with you).

| Check                | Expected                                       | Result | Notes                                     |
| -------------------- | ---------------------------------------------- | ------ | ----------------------------------------- |
| Roots C–G           | Bass pitch class matches within ~1 bar         | ☐     | Distortion can lag YIN — note delay      |
| Bb / other chromatic | Still tracks                                   | ☐     |                                           |
| Octave jump          | Same bass note, not 12 semitones up            | ☐     |                                           |
| Play vs Lock         | Play: live root. Lock: frozen captured pitches | ☐     | Already covered in B; confirm they differ |

---

### Station G — Swing, fills, ornaments, scope, energy (3 min)

| Step | What you do                                   | Expected                                     | Result | Notes |
| ---- | --------------------------------------------- | -------------------------------------------- | ------ | ----- |
| G1   | Genre Rock, Play, CHORUS, Swing**0.00** | Straight 8ths (unless pattern 24)            | ☐     |       |
| G2   | Same, Swing**0.50**                     | Off-beats late; Shuffle (24) becomes obvious | ☐     |       |
| G3   | Swing**1.00**                           | Hard shuffle / dotted                        | ☐     |       |
| G4   | Swing back to 0                               | Straight again next bar                      | ☐     |       |
| G5   | Last bar of CHORUS, strum as hard as you can  | Fill**19** (full-bar toms)             | ☐     |       |
| G6   | Last bar of INTRO, barely touch the strings   | Fill**17** (toms only on 4)            | ☐     |       |
| G7   | Medium last bar                               | Fill**18**                             | ☐     |       |
| G8   | Listen across 4-bar phrases (not a fill bar)  | Tiny two-tom pickup into the downbeat        | ☐     |       |
| G9   | Dig in vs ease off, same riff                 | Kit velocity swells, pattern may stay        | ☐     |       |
| G10  | Scope vs kick                                 | Kick on 1 lines up with notch**1**     | ☐     |       |

---

### Station H — UI / session hygiene (2 min)

| Step                         | Expected                                                                                     | Result | Notes |
| ---------------------------- | -------------------------------------------------------------------------------------------- | ------ | ----- |
| Genre change mid-Play        | Next phrase uses the new genre’s pool (Rock chorus starts producing 24/25; Metal chorus 21) | ☐     |       |
| Record riff while Play is on | Capture cancelled / ignored — Play wins                                                     | ☐     |       |
| Forget while locked          | Immediate idle                                                                               | ☐     |       |
| Forget while capturing       | Abort, no lock                                                                               | ☐     |       |
| Resize the window            | Controls remain usable; no clipped Sections list                                             | ☐     |       |
| Close / reopen editor        | Params (genre, swing, lock, sections) persist with the session                               | ☐     |       |
| Bypass / disable the plugin  | MIDI stops; no stuck crash/bass note                                                         | ☐     |       |
| Stuck notes                  | After SILENT, no hanging crash or bass                                                       | ☐     |       |
| Version string               | Still the build you meant to test                                                            | ☐     |       |

---

## 7. Master sighting log — every pattern must be ticked

Copy indices from Stations C–D. A blank row at the end of the session is a **MISS**.

| #  | Name               | First heard in station | Genre | Mode (Play / Lock / Transition) | Tick |
| -- | ------------------ | ---------------------- | ----- | ------------------------------- | ---- |
| 0  | Silent             |                        |       |                                 | ☐   |
| 1  | Verse Groove       |                        |       |                                 | ☐   |
| 2  | Verse Half-Time    |                        |       |                                 | ☐   |
| 3  | Verse Fast         |                        |       |                                 | ☐   |
| 4  | Chorus Mid         |                        |       |                                 | ☐   |
| 5  | Chorus Fast        |                        |       |                                 | ☐   |
| 6  | Breakdown          |                        |       |                                 | ☐   |
| 7  | Half-Time          |                        |       |                                 | ☐   |
| 8  | Blast Beat         |                        |       |                                 | ☐   |
| 9  | Sparse Breakdown   |                        |       |                                 | ☐   |
| 10 | Thrash             |                        |       |                                 | ☐   |
| 11 | Intro Build        |                        |       |                                 | ☐   |
| 12 | Intro Full         |                        |       |                                 | ☐   |
| 13 | Pre-Chorus Rise    |                        |       |                                 | ☐   |
| 14 | Chorus Open Groove |                        |       |                                 | ☐   |
| 15 | Breakdown Full     |                        |       |                                 | ☐   |
| 16 | Outro Decay        |                        |       |                                 | ☐   |
| 17 | Fill Short         |                        |       |                                 | ☐   |
| 18 | Fill Medium        |                        |       |                                 | ☐   |
| 19 | Fill Big           |                        |       |                                 | ☐   |
| 20 | Verse Ghost        |                        |       |                                 | ☐   |
| 21 | Chorus Blast       |                        |       |                                 | ☐   |
| 22 | Rock Backbeat      |                        |       |                                 | ☐   |
| 23 | Rock Half-Time     |                        |       |                                 | ☐   |
| 24 | Rock Shuffle       |                        |       |                                 | ☐   |
| 25 | Punk D-Beat        |                        |       |                                 | ☐   |
| 26 | Rock Ballad        |                        |       |                                 | ☐   |
| 27 | Rock 6/8 Feel      |                        |       |                                 | ☐   |

### Style / genre / section ticks

| Item                                                | Tick | Notes |
| --------------------------------------------------- | ---- | ----- |
| Style: Palm Mute                                    | ☐   |       |
| Style: Open Chord                                   | ☐   |       |
| Style: Single Note                                  | ☐   |       |
| Style: Sustain                                      | ☐   |       |
| Style: Silence                                      | ☐   |       |
| State: SILENT                                       | ☐   |       |
| State: SOFT                                         | ☐   |       |
| State: LOUD                                         | ☐   |       |
| Genre: Rock                                         | ☐   |       |
| Genre: Hard Rock                                    | ☐   |       |
| Genre: Punk                                         | ☐   |       |
| Genre: Metal                                        | ☐   |       |
| Genre: Sludge                                       | ☐   |       |
| Genre: Thrash Metal                                 | ☐   |       |
| Genre: Death Metal                                  | ☐   |       |
| Genre: Black Metal                                  | ☐   |       |
| Genre: Doom Metal                                   | ☐   |       |
| Genre: Djent                                        | ☐   |       |
| Genre: Classic Rock                                 | ☐   |       |
| Genre: Alternative                                  | ☐   |       |
| Genre: Grunge                                       | ☐   |       |
| Section: INTRO                                      | ☐   |       |
| Section: VERSE                                      | ☐   |       |
| Section: CHORUS                                     | ☐   |       |
| Section: BREAKDOWN                                  | ☐   |       |
| Section: SOLO                                       | ☐   |       |
| Section: OUTRO                                      | ☐   |       |
| Idle until arm                                      | ☐   |       |
| Record count-in click                               | ☐   |       |
| Groove lock freeze                                  | ☐   |       |
| Bass frozen in lock                                 | ☐   |       |
| Transition B contrast                               | ☐   |       |
| Return to A after B                                 | ☐   |       |
| Transition C second contrast (after A, not after B) | ☐   |       |
| Return to A after C                                 | ☐   |       |
| Cut transition by replaying riff                    | ☐   |       |
| Forget → idle                                      | ☐   |       |
| Play form walk                                      | ☐   |       |
| Play stops after last section                       | ☐   |       |
| Last-bar fill                                       | ☐   |       |
| Section-change crash                                | ☐   |       |
| Swing knob                                          | ☐   |       |
| Bass root tracking                                  | ☐   |       |
| Scope downbeat = beat 1                             | ☐   |       |
| Host tempo follow                                   | ☐   |       |

---

## 8. Miss / bug log

Every fail, flicker, or empty atlas row goes here. One line per issue. This is what the agent will triage.

```
ID | Station | Time on recording | What you played | UI (BPM/State/Style/Pattern/Groove) | What should have happened | What actually happened | Severity (block / annoy / miss)
---|---|---|---|---|---|---|---
1  |        |                   |                 |                                    |                           |                        |
2  |        |                   |                 |                                    |                           |                        |
3  |        |                   |                 |                                    |                           |                        |
```

**Severity guide**

- **block** — silence when armed, stuck notes, crash, drums on idle, Play ignores Sections, lock never engages, bass in the wrong octave and unusable
- **annoy** — late transitions, wrong style label with the right family of drums, fill a bit early, scope off by a 16th
- **miss** — a specific pattern index never appeared after its trigger recipe (especially 5, 7, 8, 10, 13, 20, 27)

Known-hard indices (do not pre-excuse them, but tell the agent they have no Play-mode pool): **5, 7, 8, 10, 13, 20, 27**.

---

## 9. Agent review prompt (paste with the recordings)

Copy everything below the line into a new chat, attach the stems + this filled file.

---

```
You are reviewing a human playtest of Fuzzyband (Metal Accompaniment), a JUCE guitar→MIDI drum/bass plugin.

Read docs/END_USER_STRESS_TEST.md (filled) plus the recordings:
- ui.mov     plugin window (BPM, State, Style, Pattern index, Groove/Transition status)
- guitar.wav dry guitar the plugin heard
- drums.wav  GM kit from MIDI ch 10
- bass.wav   bass from MIDI ch 2

Contract (v0.9.48):
- Idle until Play or Record riff. Idle + guitar must be silent MIDI.
- MIDI drums ch 10, bass ch 2. GM: 36 kick, 38 snare, 42/46 hats, 51/53 ride, 49 crash, 52 china, 55 splash, 48/45/41 toms. Record click: 36 on 1, 37 on 2/3/4.
- Pattern UI is an integer 0–27. Names are in Section 4 of the test doc.
- **13 genres** in two families. Metal family: Metal, Sludge, Thrash, Death, Black, Doom, Djent. Rock family: Rock, Hard Rock, Punk, Classic Rock, Alternative, Grunge. Play-mode pools are per family; feel (velocity, ghosts, swing, BPM clamp) is per preset.
- Play mode uses section pools (Metal-family vs Rock-family) and last-bar fills 17/18/19. It does NOT use the style head for pattern choice. **Play walks the Sections list once and stops after the last section.** It must not wrap to INTRO.
- Record riff: 1-bar count-in, 4-bar capture, lock for lockBars. Bass is note-for-note frozen. Drums frozen. After lock: contrast B for transitionBars, **then back to A**, then (if TRANSITION SECTIONS ≥ 2) contrast C, **then back to A**. Shape is **A-B-A-C-A**, not A-B-C-A. Replaying the riff cuts the current contrast. Crash in; bass leaves the riff during B/C.
- Style head: Palm Mute / Open Chord / Single Note / Sustain / Silence. Must match articulation.
- State: SILENT / SOFT / LOUD with hysteresis (SOFT→LOUD fast, LOUD→SOFT ~2s).
- Tempo is host tempo.
- Patterns with no Play pool (must be hunted in transition): 5, 7, 8, 10, 13, 20, 27.

Do this:
1. Cross-check every tick in Sections 7–8 against the audio/video. Call out ticks that the recording does not support.
2. Identify every pattern index that is audible, with timestamp.
3. List MISSes: patterns/styles/sections/genres the tester tried to trigger and did not get.
4. Flag correctness bugs (idle not silent, lock not frozen, bass yanking during lock, Play bass still playing a recorded riff, transition not contrasting, crash/fill missing, wrong style label for a long stretch, tempo not following host, stuck MIDI notes, scope not on beat 1).
5. Separate “engine never selected this class” from “tester didn’t play the trigger long enough / wrong genre / still in Play mode”.
6. Return a short triage table: bug | evidence timestamp | likely subsystem (inference / pattern pool / phrase lock / bass / gate / UI) | suggested next fix.

Do not excuse a missing pattern because it is hard. If the trigger recipe was followed and the index never appeared, it is a coverage gap.
```

---

## 10. Speed-run clock

If you only have half an hour, skip the 11 subgenre Play passes (do Metal + Rock full forms, 20-second feel checks on the rest):

| Min    | Station                                                                                              | Skip if needed                     |
| ------ | ---------------------------------------------------------------------------------------------------- | ---------------------------------- |
| 0–2   | A idle + one Play arm (confirm it**stops** after a 4-bar custom form if you are short on time) | —                                 |
| 2–12  | B Metal lock → B → A → C → A → forget                                                           | Skip B2 solo                       |
| 12–22 | C Metal + Rock full forms (add BREAKDOWN+SOLO). Confirm Play**stops** after OUTRO              | Subgenres as 20-second feel checks |
| 22–28 | D style five-pack + 180 BPM blast bait + 6/8 bait (TRANSITION BARS=8)                                | —                                 |
| 28–32 | E tempo jumps, F three roots (C/E/G), G swing + one Fill Big                                         | —                                 |
| 32–35 | H Forget / genre swap (Metal → Thrash → Rock → Grunge) / stuck notes                              | —                                 |

Full session (all 13 Play passes) is ~45–60 min. Then fill Section 7 honestly. Empty boxes are data, not failure of the test.

---

## Appendix — GM MIDI cheat (for piano-roll inspection)

When you dump the MIDI, this is how you confirm a pattern without guessing from the kit mix:

| Pattern | Signature in the piano roll                                |
| ------- | ---------------------------------------------------------- |
| 0       | no note-ons                                                |
| 2 / 23  | snare (38) on beat 3, not on 2                             |
| 8       | 36 and 38 alternating every 16th                           |
| 9       | two bars, only a handful of hits, china 52 on 1            |
| 10      | double 36 on the downbeat 16ths, 38 on 2 and 4             |
| 17      | toms 48/45/41 clustered on beat 4                          |
| 19      | toms all bar long                                          |
| 24      | 8ths with swing when knob > 0                              |
| 25      | 8th-note 36s, 38 on 2/4, 51 quarters                       |
| 27      | hats/kicks at 0.666 / 1.333 / 2.666 / 3.333 (triplet grid) |

Bass is channel 2, typically 28–55. During lock it repeats the captured contour. During Play/transition it should move when you change chord and stay in-key with the section.
