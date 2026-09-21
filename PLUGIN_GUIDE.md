# fuzzyband — Plugin Guide

How to set fuzzyband up in a DAW, what the controls do, and what MIDI it emits.

---

## 1. Routing

fuzzyband is an **audio effect that produces MIDI**: one audio input (your guitar), no MIDI input,
and MIDI output on two channels.

```
Guitar (DI) ──► fuzzyband ──► dry guitar out
                         │
                         └── MIDI out ──► drum track  (ch 10, GM percussion)
                                       └► bass track  (ch 2, pitched)
```

Insert **fuzzyband on the guitar audio track**, first in the chain:

1. Guitar input (DI)
2. **fuzzyband**
3. Amp / cab / IR / FX

The guitar passes through unmodified; the plugin's **Output Gain** scales that audio only, never
MIDI velocity.

Then route the MIDI the plugin generates:

| Track | Instrument | MIDI source | Channel filter |
|---|---|---|---|
| Guitar | fuzzyband (audio effect) | — | — |
| Drums | GM drum kit / sampler | the guitar track | **10** only |
| Bass | bass synth / sampler | the guitar track | **2** only |

If both instruments share one MIDI bus, filter by channel on each so the kit does not play bass
notes and the bass does not play kicks.

**Reaper:** scan plugins and search *fuzzyband* or *Niall* (category **Tools**). On the drum and
bass tracks use *MIDI from* the guitar track, then set the channel filter.

**Logic:** load the **AU** build. It registers as a music effect, so it can emit MIDI from an audio
track.

> **Use a clean DI signal.** The analyser is trained on dry DI. Fuzz and distortion upstream will
> confuse energy, playing-style and pitch detection, so keep fuzzyband before your amp sim.

---

## 2. Tempo

**The DAW is the click.** While the transport runs, BPM comes from the host playhead and the beat
clock is anchored to the host sample position — so grooves stay on the project grid across play,
stop, seek and loop.

- You do **not** tap tempo. Set the project BPM and play *into* that grid.
- The `bpm` parameter is a fallback for hosts with no playhead (such as the standalone app). It has
  no panel widget, and is ignored when the host reports a valid tempo.
- If you play with the transport stopped, the plugin keeps time itself at the last valid tempo.
  That is for sketching only — MIDI will not line up with existing clips until you press play.

Change the project tempo and the accompaniment follows on the next blocks. Loop the timeline and
patterns stay locked to bar 1 of the loop.

---

## 3. Modes

### Follow (PLAY off)

The plugin listens and picks a groove from how you play:

- **Energy** → `SILENT` / `SOFT` / `LOUD` (verse-like vs chorus-like).
- **Playing style** — palm mute, open chord, single note, sustain — steers the groove family once
  it has been stable for roughly 150 ms: chugging leans half-time/breakdown, open chords lean
  chorus/breakdown, single-note runs lean fast/thrash, sustain leans sparse.

Pattern changes commit on **bar boundaries**, with a hold, so the kit does not flicker on every
strum. Follow changes *which* groove plays, never *how fast*.

### Play (PLAY on)

Scripted **song form** at the project tempo. You arrange `INTRO`, `VERSE`, `CHORUS`, `SOLO`,
`BREAKDOWN`, `OUTRO` sections in the **Sections** list and the sequencer walks the form. Grooves
rotate by musical phrase (2 bars for verse/chorus/solo, 4 for breakdown/intro/outro) so it does not
sit on one pattern. Enable **Loop** to repeat the form.

Play starts with a bar-aligned count-in (kick on 1, stick on 2–4).

### Record riff (riff lock)

Press **Record riff**: you get a 1-bar count-in (kick on 1, stick on 2–4), then play a **4-bar**
riff to the click. The plugin locks the drums and bass to that take. While locked, the panel shows
how many bars remain before a transition into contrast sections, then back to follow.

**Forget** clears the riff. **Lock** and **Transition** control how long the lock holds after you
leave the riff and how the contrast sections behave.

---

## 4. Controls

| Control | What it does |
|---|---|
| **Genre** | 13 presets: Rock *(default)*, Hard Rock, Punk, Metal, Sludge, Thrash Metal, Death Metal, Black Metal, Doom Metal, Djent, Classic Rock, Alternative, Grunge. Sets groove feel, velocity profile, default swing, half-time bias and the usable BPM range. |
| **Swing** | Delays off-8th drum events (0–100%). The genre supplies a default. |
| **Humanize** | Scales the per-bar ornament probabilities (open hat, extra ghost note, micro-fill, ride switch). At 0 the bar is played verbatim. |
| **Lock** | How long a riff lock holds after you stop playing the riff. |
| **Transition** (bars / sections) | After the lock expires: how long each contrast section lasts, and how many distinct contrast families rotate (`A→B→A→C→A…`). |
| **Play** | On = song-form playback. Off = follow. |
| **Record riff / Forget** | Capture or clear a riff lock. |
| **Sections** | The song form, editable in place — drag to reorder, set bars per section, or remove. |

**Parameters with no panel widget.** `bpm`, `outputGain`, `bassTranspose`, `songForm` and `loop`
exist as plugin parameters, so they are saved with the session and are automatable, but they have
no control in the panel. Of these, `outputGain` (dry-guitar level), `bassTranspose` (‑12 / 0 / +12
semitones on the bass) and `bpm` (fallback) are the ones you might want to automate.

### Live readout

One line under the header, e.g.

```
120.0 bpm · SILENT · P23 · Open Chord
```

BPM · structure state · pattern index · detected playing style.

---

## 5. MIDI map

### Drums — channel 10 (General MIDI percussion)

| Note | Instrument | | Note | Instrument |
|---|---|---|---|---|
| 36 | Kick | | 42 | Hat (closed) |
| 38 | Snare | | 46 | Hat (open) |
| 41 | Tom (low) | | 49 | Crash |
| 45 | Tom (mid) | | 51 | Ride |
| 48 | Tom (high) | | 53 | Ride bell |
| 37 | Stick *(count-in clicks only)* | | 52 | China |
| | | | 55 | Splash |

Any kit that maps GM percussion will work — Addictive Drums, Superior Drummer, Battery, or a
plain GM sampler.

### Bass — channel 2

Pitched notes transposed to the tracked guitar root, with library-authored parts rooted at C2
(MIDI 36) for drop tunings. Use a bass instrument, not a drum kit. If the bass sounds an octave
out, adjust the `bassTranspose` parameter (‑12 / 0 / +12).

---

## 6. Getting a good result

- **Set the project BPM first** and practise to it, exactly as you would with a click. The plugin
  accompanies you *at* that tempo rather than chasing you.
- **Keep the guitar part dry at the plugin.** Amp/cab/FX go after it.
- **256-sample buffers** are the sweet spot on Apple Silicon. 128 is fine if the session stays
  xrun-free.
- **Pick a genre preset before tweaking Swing or Humanize** — the preset sets sensible defaults for
  the style, then the two knobs move you away from them.
- **Follow mode is for improvising; Play mode is for takes.** If you want the arrangement to line up
  with the rest of the song, use Play and build the section list.

---

## 7. Troubleshooting

| Symptom | Things to check |
|---|---|
| **No drums or bass at all** | Is the transport running? Is the MIDI routed to the instrument tracks, filtered to channels 10 and 2? Did you press play (Follow still needs the host clock)? |
| **Drums play but the bass is silent** | Confirm the bass track listens on channel 2 and is a *pitched* instrument, not a drum kit. |
| **Groove feels late or early** | Check the host's reported latency and plugin delay compensation. |
| **Wrong groove for what you're playing** | Try a different genre preset; the playing-style head needs ~150 ms of stable playing before it changes family. |
| **Bass sounds an octave out** | Adjust `bassTranspose` (‑12 / 0 / +12). |
| **Patterns don't change when you change sections** | In Play mode pattern changes commit on bar boundaries; give it a bar. |
| **Nothing reacts to dynamics** | The `SILENT`/`SOFT`/`LOUD` thresholds expect a reasonably clean DI. Heavily compressed or distorted input flattens the difference. |

---

## 8. Uninstalling

Remove the plugin bundle and it is gone — fuzzyband writes no files outside its own settings:

```
~/Library/Audio/Plug-Ins/VST3/fuzzyband.vst3
~/Library/Audio/Plug-Ins/Components/fuzzyband.component
```
