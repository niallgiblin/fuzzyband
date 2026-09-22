# fuzzyband — User Manual

---

Download Fuzzyband here: [niallgiblin.github.io/fuzzyband](https://niallgiblin.github.io/fuzzyband/) and load it into you DAW.

## 1. Routing

fuzzyband is an audio effect that produces MIDI: one audio input (your guitar),
and MIDI output on two channels.

```
Guitar (DI) ──► fuzzyband ──► dry guitar out
                         │
                         └── MIDI out ──► drum track  (ch 10, GM percussion)
                                       └► bass track  (ch 2, pitched)
```

Insert fuzzyband on the guitar audio track, first in the chain:

1. Guitar input (DI)
2. fuzzyband
3. Any other FX you wish

Then route the MIDI the plugin generates:

| Track  | Instrument               | MIDI source      | Channel filter    |
| ------ | ------------------------ | ---------------- | ----------------- |
| Guitar | fuzzyband (audio effect) | —               | —                |
| Drums  | GM drum kit / sampler    | the guitar track | **10** only |
| Bass   | bass synth / sampler     | the guitar track | **2** only  |

---

## 2. Tempo

The plugin matches the tempo set in the DAW. Set the project BPM and play into that grid.

Change the project tempo and the accompaniment follows on the next blocks. Loop the timeline and
patterns stay locked to bar 1 of the loop.

---

## 3. Modes

### Play

Pre-defined song form. You arrange `INTRO`, `VERSE`, `CHORUS`, `SOLO`,
`BREAKDOWN`, `OUTRO` in the **Sections** list.  Play starts with a bar-aligned count-in (kick on 1, stick on 2–4).

### Record riff

Press **Record riff**: you get a 1-bar count-in (kick on 1, stick on 2–4), then play a 4-bar
riff to the click. The plugin locks the drums and bass to that take. While locked, the panel shows
how many bars remain before a transition into contrast sections, then back to the locked riff after each transition section.

**Forget** clears the riff. **Lock** and **Transition** control how long the lock holds after you
leave the riff and how the contrast sections behave. **Transition Sections** indicates the number of unique transition sections you'd like to loop through.

---

## 4. Controls

| Control                                | What it does                                                                                                                                                                                                                         |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Genre**                        | 13 presets: Rock, Hard Rock, Punk, Metal, Sludge, Thrash Metal, Death Metal, Black Metal, Doom Metal, Djent, Classic Rock, Alternative, Grunge. Sets groove feel, velocity profile, default swing, and half-time bias for the drums. |
| **Swing**                        | Delays off-8th drum events (0–100%). The genre supplies a default.                                                                                                                                                                  |
| **Humanize**                     | Scales the per-bar ornament probabilities (open hat, extra ghost note, micro-fill, ride switch). At 0 the bar is played verbatim.                                                                                                    |
| **Lock**                         | How long a riff lock holds after you stop playing the riff.                                                                                                                                                                          |
| **Transition** (bars / sections) | After the lock expires: how long each contrast section lasts, and how many distinct contrast families rotate (`A→B→A→C→A…`).                                                                                                  |
| **Play**                         | Song-form playback.                                                                                                                                                                                                                  |
| **Record riff / Forget**         | Capture or clear a riff lock.                                                                                                                                                                                                        |
| **Sections**                     | The song form, editable in place. Drag to reorder, set bars per section, or remove.                                                                                                                                                  |

### Live readout

One line under the header, e.g.

```
120.0 bpm · SILENT · P23 · Open Chord
```

BPM · structure state · pattern index · detected playing style.

---

## 5. MIDI map

### Drums — channel 10 

| Note | Instrument                    |  | Note | Instrument   |
| ---- | ----------------------------- | - | ---- | ------------ |
| 36   | Kick                          |  | 42   | Hat (closed) |
| 38   | Snare                         |  | 46   | Hat (open)   |
| 41   | Tom (low)                     |  | 49   | Crash        |
| 45   | Tom (mid)                     |  | 51   | Ride         |
| 48   | Tom (high)                    |  | 53   | Ride bell    |
| 37   | Stick*(count-in clicks only)* |  | 52   | China        |
|      |                               |  | 55   | Splash       |

Any kit that maps GM percussion will work.

### Bass — channel 2

Pitched notes mirroring the tracked guitar.
