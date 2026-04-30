# Section Transitions — Feel, Timing, and Musical Satisfaction

This document is specifically about the experience of moving between riffs and sections while the plugin is running. It covers what happens now (honestly), why it feels wrong, what signals a guitarist actually uses to communicate transitions, and what needs to change to make this feel alive.

---

## What the plugin currently does when you change sections

There is no transition detection. The plugin only knows about *settled state*, not *the moment of change*. Every piece of the pipeline has a delay:

```
You hit the chorus chord
         │
         ▼
EnergyAnalyser RMS rises above kLoudRms (0.35)
         │
         ▼  ← StructureTagger hold: must stay LOUD for 2.5 seconds before state commits
         ▼
FeatureVector with state=LOUD enters the queue
         │
         ▼  ← background inference thread: wakes every 20ms, evaluates
         ▼
RuleBasedInference → new pattern index computed
         │
         ▼  ← 2-bar hold guard: blocks pattern change until 2 bars have elapsed at current BPM
            (at 100 BPM: 4.8 seconds; at 80 BPM: 6 seconds)
         ▼
latestPatternIndex updated
         │
         ▼  ← PatternPlayer pending change: waits until next bar boundary
         ▼
Pattern actually changes
```

**Realistic total latency from your chord to the drummer responding: 7–11 seconds at slow tempos.**

At 100 BPM you play a chorus and wait nearly 10 seconds for a pattern change. That is not a musical instrument.

### The 2-bar hold guard exists for a reason

Without it, the pattern would flicker every time your RMS crossed the SOFT/LOUD threshold mid-riff. It prevents the pattern from changing on every hard strum. The problem is it treats all changes the same — a casual energy variation and a genuine "I'm in a new section now" look identical to the plugin.

### The StructureTagger hold exists for a reason

Two seconds prevents flickering between states caused by a single loud note or a momentary mute. Necessary for stability, but at odds with fast transitions.

---

## Why transitions feel wrong

The specific failure modes, in order of how much they hurt:

**1. The drummer doesn't notice for several seconds.**
You've clearly moved to a new riff. The drums keep playing the old pattern. You spend the transition playing over the wrong groove, which breaks the illusion entirely.

**2. When the drummer does change, there's no event to mark it.**
No crash, no fill, nothing. The pattern just quietly becomes different. In a real band the crash on bar 1 is the drummer's signal that *they noticed and they're with you*. Without it, even a correctly-timed pattern change feels flat.

**3. The plugin can't distinguish "I'm transitioning" from "I'm varying my dynamics."**
Playing a slightly louder note mid-riff and genuinely entering a chorus look identical to a threshold crossing. The plugin needs to tell these apart.

**4. Brief silence is treated as song stop.**
When a SILENT block is entered, `playbackGateOpen` is killed and the BeatTracker resets. If you lift your strings for a beat to breathe before the new section — a completely natural guitar gesture — the plugin starts its entire count-in sequence again. The brief pause should signal "new section incoming," not "they've stopped playing."

**5. The drummer never initiates.**
A real drummer will push you into a chorus when the groove has been building for 8 bars, even without a cue from you. The plugin is purely reactive — it only changes when you change first.

---

## How guitarists actually communicate transitions

These are the physical gestures that carry musical meaning, roughly ranked by how clearly they signal "something is changing":

**The breath** — strings go quiet for 0.5–2 beats, then a strong downstroke into the new riff. The silence is the signal. Duration distinguishes it from a SILENT reset: 50–500ms of near-silence is a phrase breath; 2+ seconds is actually stopping.

**The intensity slam** — a sudden large RMS increase in a single block, well above the sustained RMS of the current section. A chorus chord hit after a palm-muted verse. The peak-to-sustained ratio is the tell, not the absolute level.

**The energy arc** — a gradual build over 2–4 bars toward a peak, then a decisive landing. Common in post-rock and sludge. The endpoint is telegraphed by the shape of the build.

**The hold before the drop** — playing a single sustained note for an extra beat before cutting, then re-entering at a new energy level. The rhythmic break is the signal (riff stops pulsing, single note rings).

**The riff-end resolution** — playing a characteristic ending figure (e.g., a palm-muted chug on beat 4 with a slight lift) that closes a phrase. Hard to detect in the abstract but correlates with a brief centroid drop + slightly lower RMS at end of bar.

**The pitch-class jump** — moving to a new root note. The pitch estimator already tracks this. A root change correlating with an energy change is a strong two-feature transition signal.

**Playing the same idea twice** (the "I mean it" signal) — before a big section change, guitarists often play a 1–2 bar figure twice to tell the band what's coming. Repeated rhythmic pattern with slowly rising energy.

---

## What "satisfying landing" means in drummer terms

The drummer's job at a transition is not just to play a different pattern — it's to mark the moment. These are the elements:

**Crash cymbal (MIDI 49) on beat 1 of the new section.** This is the single most important event. Without it, the transition is invisible. A crash at bar 1 of a new pattern communicates "I'm here, I heard you, we're in this together."

**The pattern change lands on bar 1.** Not mid-bar, not mid-beat. The new groove should start at a bar boundary. PatternPlayer already handles this for pending changes, but it only matters if the change was detected in time for the boundary to line up.

**BPM is already correct when the crash hits.** If the drummer is still settling into tempo when the new section starts, it sounds like hesitation. The `snapBpm()` fix addresses this at gate-open, but the same principle applies to tempo correction at every pattern change.

**Volume/intensity lands with the section.** The drummer should reinforce the energy shift: louder hits into a chorus, lighter touch into a breakdown.

---

## Implementation roadmap — ranked by impact

### 1. Crash cymbal on pattern change (highest impact, ~15 lines)

Every time `activePatternIndex` changes in `PatternPlayer::process()`, emit a crash (MIDI 49 on channel 10) at sample offset 0 of that block.

Add a flag to `PatternPlayer` private state:
```cpp
bool fireTransitionCrash = false;
```

In `setGenerativeBassSteps` or wherever `activePatternIndex` gets committed (currently `process()` at line ~423):
```cpp
if (atBar)
{
    activePatternIndex = pendingPatternIndex;
    pendingPatternIndex = -1;
    fireTransitionCrash = true;   // arm the crash for this block
}
```

At the top of the emission section in `process()`, before `emitEventsForRange`:
```cpp
if (fireTransitionCrash && !structureSilent)
{
    midi.addEvent(juce::MidiMessage::noteOn(kDrumChannel, 49, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(kDrumChannel, 49), 6000);
    fireTransitionCrash = false;
}
```

Suppress the crash when transitioning *to* silent (index 0) or *from* silent.

### 2. Energy-delta bypass of the 2-bar hold guard (high impact, ~10 lines)

Track a rolling short-window RMS alongside the existing RMS. When the ratio `|blockRms - prevShortRms| / prevShortRms` exceeds a threshold (e.g., 0.6 — energy rose or fell by 60% in one block), set a `transitionEvent` flag. When that flag is set in `drainFeatureQueueAndRunInference()`, bypass the `drumHoldExpired` check.

In `processBlock()`, add to `FeatureVector`:
```cpp
// FeatureVector.h — add field:
float rmsDelta = 0.0f;   // signed ratio: (rms - prevRms) / max(prevRms, 1e-6)
```

Compute it in `processBlock()`:
```cpp
fv.rmsDelta = (rms - prevBlockRms) / std::max(prevBlockRms, 1.0e-6f);
prevBlockRms = rms;   // new private float member
```

In `drainFeatureQueueAndRunInference()`, replace:
```cpp
if (drumHoldExpired || excludeParam >= 0)
```
with:
```cpp
const bool transitionEvent = std::abs(latest.rmsDelta) > 0.6f;
if (drumHoldExpired || excludeParam >= 0 || transitionEvent)
```

This makes genuine section slams bypass the guard while controlled riffing stays gated.

### 3. Mini-silence / "the breath" (medium impact, medium complexity)

Currently any entry into `SILENT` resets `playbackGateOpen`, `beatTracker`, and everything else. A brief string lift (100–400ms) obliterates the gate and forces a full re-count-in.

The fix: distinguish "long silence" (>1 beat at current BPM) from "phrase breath" (<1 beat).

Add to `AccompanimentProcessor` private state:
```cpp
int silenceSamples = 0;
bool inPhrasBreath = false;
```

In the SILENT branch of `processBlock()`, count samples in silence:
```cpp
silenceSamples += numSamples;
const int oneBeatSamples = static_cast<int>(
    (60.0f / bpmForPlayer) * static_cast<float>(cachedSampleRate.load(std::memory_order_relaxed)));

if (silenceSamples > oneBeatSamples)
{
    // True stop — reset everything as now
    onsetDetector.resetTempoLock();
    beatTracker.reset();
    playbackGateOpen = false;
    ...
}
else
{
    // Phrase breath — keep BeatTracker running, keep gate open, arm crash for re-entry
    inPhraseBreath = true;
    // Don't reset playbackGateOpen or beatTracker
}
```

When exiting SILENT back to SOFT/LOUD with `inPhraseBreath = true`, immediately fire a crash and skip the deferred-snap delay (gate was already open).

Reset `silenceSamples = 0` when SILENT exits.

### 4. Set-interval / drummer agency (medium impact, unique feel)

This gives the plugin a drummer who occasionally pushes you. Every N bars (configurable), the drummer re-evaluates and is willing to change even without a clear energy signal from you.

Track `barsSinceLastPatternChange` in the inference loop (accessible since `lastDrumPatternChangeSample` is already tracked). When `barsSinceLastPatternChange >= autoChangeIntervalBars`:

- If energy has been stable in SOFT and the riff has been going on for a long time, push toward LOUD patterns anyway ("drummer feels the build")
- Or simply allow a pattern change that would otherwise be blocked by the hold guard

User-facing control: an `autoChangeBars` parameter (4 / 8 / 16 / Off). Default: 8 bars.

This is what you mean by "at a set/generated interval." The drummer hears that you've been in the verse for 8 bars and decides it's time to push the chorus.

### 5. Transition fill (high impact, highest complexity)

A 1-bar fill fired in the last bar before a pattern change. This requires knowing the change is coming before it happens, which the current architecture doesn't support.

Two approaches:

**Pre-emptive fill (easier):** When `transitionEvent` fires (energy delta bypass), immediately switch to a fill pattern for 1 bar, then switch to the new groove. This adds 1 bar of delay between the transition signal and the new pattern, which is musically correct (fill → chorus, not silence → chorus).

**Lookahead fill (harder):** Detect that energy has been monotonically rising for 2 bars and arm a fill for bar N, expecting the chorus at bar N+1. Speculative — may misfire.

Start with the pre-emptive approach. Add a fill pattern (`buildFill()`) to `MidiPatternLibrary` — a 1-bar dense snare/kick run that leads into beat 1. When `transitionEvent` is true, set `pendingPatternIndex` to the fill index, and schedule the real new pattern for the bar after.

### 6. Tune the StructureTagger holds (low-hanging fruit, no code change)

`kHoldSoftSec = 2.0` means you need to play softly for 2 continuous seconds before the plugin exits LOUD. For fast section switches this is too conservative.

Realistic values for metal with hard cuts:
- `kHoldSoftSec = 0.75` — three-quarter second is enough to distinguish muted note from palm-muted verse
- `kHoldLoudSec = 1.5` — one and a half seconds to confirm chorus commitment

At 100 BPM, 0.75s = 1.25 beats, which is roughly one kick-snare pair. That's long enough to filter single loud notes, short enough to catch genuine section changes.

The 2-bar hold guard in the inference loop becomes the primary long-term guard once StructureTagger is more responsive.

---

## The "set/generated interval" — what it should feel like

This is distinct from reactive transitions. A real drummer doesn't just respond to you — they sometimes *lead*. After playing the same groove for 8 bars, a drummer starts building the fill whether or not you gave the cue, because they feel the phrase is ending.

For the plugin, this means:

```
barsSincePatternChange >= 8
         │
         ▼
Re-evaluate pattern selection with "urgency bias"
         │  ─ if SOFT for 8+ bars and BPM suggests energy: push toward Chorus
         │  ─ if LOUD for 8+ bars: allow drop to Breakdown or Verse
         │  ─ if pattern change is warranted: fire immediately (bypass hold guard)
         ▼
Arm crash for next bar boundary
```

The interval should ideally vary slightly — not exactly 8 bars every time. A small random jitter (±1 bar) makes the drummer feel less mechanical. The jitter range should be user-tunable or based on intensity: low intensity = longer, more relaxed intervals; high intensity = shorter, more energetic.

---

## Priority order for implementation

| What | Feel impact | Code effort |
|---|---|---|
| Crash on pattern change | Very high — marks every transition | Low — ~15 lines in PatternPlayer |
| Energy delta bypass of hold guard | Very high — makes fast cuts actually work | Low — ~10 lines |
| Reduce StructureTagger holds | High — whole pipeline responds faster | None — change two constants |
| Set-interval auto-change | High — adds drummer personality | Medium |
| Mini-silence / breath detection | Medium — fixes the phrase-breath problem | Medium |
| Fill before transition | Very high — most "real drummer" feel | High — requires fill pattern + scheduling |

The first three can be done in one session and together would dramatically change the feel of section switching. The fill is the most musically satisfying end state but requires the scaffolding of the first three to work well.

---

## What this should feel like when it works

You play the verse riff for 4–6 bars. You palm-mute, breathe for half a beat, and hit the chorus chord. The drummer fires a crash on beat 1 of the next bar, switches to the Chorus Mid pattern, and the bass follows. The whole thing lands together. You didn't tap a button or wait 10 seconds — you played the transition the way you'd signal it to a human bandmate, and it responded.

Going back to the verse: you let the energy drop, maybe play softer and slower for a bar. The drummer notices — a fill into the verse pattern. Or they push back a little and hold the chorus groove for one more bar before dropping. Either way feels intentional.

If you do nothing and just sit in the riff, after 8 bars the drummer throws a fill and pushes you toward something. You can respond or ignore it. That's the generative interval — the drummer having an opinion, not just listening.
