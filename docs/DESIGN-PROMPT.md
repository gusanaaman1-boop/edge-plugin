# EDGE — design prompt (v0.10)

Paste everything below the line into the review chat. Attach the seven
screenshots from `outputs/ui/`.

---

## The job

Make **EDGE** the best autofilter anyone has shipped, and give it the twist that
makes people buy it instead of the free one that came with their DAW.

It has to stand next to FabFilter Volcano, Soundtoys FilterFreak, Kilohearts
Filter and Xfer LFOTool and win — on the shop page, in a ten-second video, and
after an hour of use. Right now it would lose to all four, and not on sound.

The engine is finished and measured. The interaction layer was just repaired to
your own spec and every acceptance number passed. **What is left is the part
that was explicitly out of scope in that pass: the design.** That is the whole
job, and it is yours.

## What it is

A filter with two spectral edges. Each edge moves continuously from a gentle
shelf, through deeper attenuation, into a real cut — one control, no mode
switch, nothing crossfaded. One big knob, **EDGE**, drives both edges at once,
and an envelope follower can drive that knob from the signal.

Also: a movable bell between the edges, stereo spread, hidden saturation with
two voicings, four modes (LP / BAND / HP / FREE), 23 presets. 24 parameters.

At zero it is a bit-exact wire with zero latency.

## What changed since the last screenshots you saw

Your interaction work order was executed in full, and it holds:

* The display is driven by **one coherent snapshot** from the engine
  (seqlock, revision counter). The stale-MID defect you named is reproduced in
  a regression test and fixed; the incomplete float hash is gone.
* Response and spectrum have **separate invalidation**; 60 Hz while dragging,
  30 Hz idle; a drag moves its handle in the **same frame** (0.000 px measured).
* SHAPE is now a **contextual inspector**: LOW / MID / HIGH / FOLLOW tabs, one
  visible panel, attachments built once. Touching a graph handle selects its
  band and opens the panel. Clicking the main FOLLOW knob selects FOLLOW.
* The analyzer aggregates power per log band, smooths across bands, fades out
  below −66 dBFS and draws nothing at −84.
* 93 DSP + 87 host checks green, ASan/UBSan clean, auval passes, zero
  audio-thread allocations, 253× realtime.

So: the mechanics under any design you specify are already correct. Nothing you
propose has to work around a broken display.

## The owner's verdict, unchanged

> *"Not good at all. The display is not professional. The angles are not
> professional. None of the buttons are professional. Simply put — we need a
> god. An autofilter with a twist."*

Seven screenshots are attached. It reads as a careful engineer's layout:
correct, evenly weighted, unmemorable. Nothing on the panel says what this
plug-in is for.

## Answer in three parts

### 1. The twist — do this first

The current claim is "the morph is continuous, so it never clicks". That is
true and it is a **bad product idea**, because nobody can hear an absence.

Give me:

* **One sentence** for the top of the shop page.
* **The ten-second demo** — the exact gesture, on what material, that makes
  someone want it.
* **The twist itself.** Either argue the continuous morph is the twist and tell
  me how to make it *visible and audible*, or name a better one. The DSP cannot
  change — but everything about how it is presented, named, grouped, animated
  and defaulted can. Note what already exists to build on: the signal driving
  the filter (FOLLOW), a band that travels as one shape (FREE), colour that
  arrives with the depth (BITE), and a display whose refresh architecture can
  now animate anything you specify without lying about the state.
* **What gets cut.** Assume the panel is 30 % too busy. Name what goes.

If the identity is wrong, the pixels do not matter. Do not skip to part 2.

### 2. The look

Numbers a developer types, not adjectives.

**Today:** chassis `#1C1D20`, well `#0D0E10`, text `#C8CDD4`, two accents — amber
`#F2A03C` for the low edge, cyan `#31C6E8` for the high. Type at 9 / 10 / 11 px,
system sans. Corner radii of 4, 8 and 10 px, none derived from the others.
Knobs: dark disc, accent arc from 1.25π to 2.75π, tick ring. Rounded-rect
buttons: four MODE, SHAPE, BYPASS, CHARACTER, four inspector tabs, LINK.
900 × 560, resizable 720 × 420 → 1800 × 1400. The inspector adds 156 px.

Specify:

* **Grid** — base unit, columns, vertical rhythm, margins.
* **Geometry** — one radius scale derived from the base unit, and which element
  gets which. This is the "angles" complaint. Answer it explicitly.
* **Knobs** — diameters, arc angles, arc weight, pointer, ticks or no ticks, and
  idle / hover / drag.
* **Buttons** — the hierarchy. A button that changes *what the filter is* (MODE)
  must not look like a button that opens a panel (SHAPE) or a tab (LOW/MID/
  HIGH/FOLLOW).
* **Type** — family, sizes, weights, tracking, what is uppercase.
* **Colour** — is two accents right? If a third is needed, say what it is *for*.
* **The display** — the highest-value answer here. It is currently a
  measurement instrument: flat, grey, grid as loud as the data, a gradient fill
  that means nothing. Tell me the visual weight order and what happens to every
  layer: grid, axis labels, analyzer, L/R spread traces, ghost target curve,
  solid current curve, fill, handles, the selected handle, the readout, the
  slope combo.
* **The inspector** — it exists now and works. Does it stay a bottom panel?
  Tabs on the left? Attached to the selected handle? Its selection colour rules
  are: orange = LOW only, cyan = HIGH only, neutral = MID and FOLLOW.
* **Motion** — what moves, how fast, and what must never move. The display can
  now honestly animate at 60 Hz during interaction.

### 3. The work order

```
PHASE n — <title>

  PROBLEM     one or two sentences
  CHANGE      the specific change, with numbers
  ACCEPTANCE  something a screenshot test can assert
  RISK        what this breaks
  EFFORT      S / M / L
```

Ordered by how far each phase moves it towards "worth paying for". Five phases
that change everything beat fifteen that polish. For ACCEPTANCE, note what the
test rig can already assert: rendered PNGs at any size and scale, component
bounds (clipping / overlap / zero-size), path geometry, handle positions to
sub-pixel, per-parameter display response, timer rates.

## Fixed

One line at the end if you disagree, then move on.

1. **No DSP changes.** Bit-exact neutral and zero latency are guarantees.
2. The **24 parameter IDs, ranges and defaults** stay — saved projects depend on
   them, and their hash is asserted in CI. Regroup, rename, hide or move
   controls onto the display freely.
3. **JUCE, drawn in code.** No image assets, no web view, no licensed font.
4. Legible and clickable **720 × 420 → 1800 × 1400**, at 100 / 150 / 200 %.
5. **No LFO, no sequencer, no drawable modulation, no randomiser.** One envelope
   follower, and that is the whole modulation story.
6. **The interaction contract just shipped stays**: one snapshot drives the
   display, handles track their parameters same-frame, the inspector keeps its
   attachments alive, selection is owned by the editor. Design around it, not
   through it.

Every measurement is a number. If a criticism is wrong, say so and show why. If
an area is fine, say "nothing found" rather than inventing work.
