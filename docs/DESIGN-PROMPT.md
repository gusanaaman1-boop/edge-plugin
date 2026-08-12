# EDGE — design prompt (v0.9)

Paste everything below the line into the review chat, and attach the four
screenshots from `outputs/ui/`.

---

You are a senior plug-in **product and interface designer** — the person who
decides what a plug-in *is* and what it looks like, not the person who writes
its DSP. I need a design specification, not advice.

## Read this first: what is NOT being asked

The DSP is finished and measured. **Do not redesign it, and do not propose DSP
work.** For context only, all of this is already true and tested:

* Each spectral edge morphs continuously shelf → cut inside **one** filter.
  At EDGE 0 the plug-in is **bit-exact** and **zero-latency**.
* 24 host parameters, 23 factory presets, state migration, a lock-free
  analyser, hidden saturation with two characters.
* 93 DSP checks and 49 host-contract checks pass in Release and under
  ASan+UBSan with zero sanitiser diagnostics. `auval` passes. 251× realtime.
* macOS `.pkg` installer, a manual, a generated parameter table.

**The sound is not the problem. The product is.**

## The actual brief, in the owner's words

> *"It is not good at all. The display is not professional. The angles are not
> professional. None of the buttons are professional. Put simply — we need a
> god. An autofilter with a twist."*

Two things are being asked for, and they are one thing:

1. **A visual design that looks like a product someone pays for.** Right now it
   looks like a competent engineer's layout, because that is what it is.
2. **An identity.** "The god of autofilters, with a twist." Not "a filter that
   also has an envelope follower" — *the* autofilter, and something about it
   that nobody else has.

## What it looks like today

Four screenshots are attached: default, SHAPE open, LP mode, and a MID-heavy
setting. The current design system, so you are correcting a real thing rather
than an imagined one:

| | |
|---|---|
| chassis | `#1C1D20`, shell gradient `#232427` → `#141517` |
| display well | `#0D0E10` → `#141518`, grid `#232529` / `#32353B` |
| text | `#C8CDD4`, dim `#787F89`, bright `#F4F7FA` |
| accents | **exactly two** — low `#F2A03C` (amber), high `#31C6E8` (cyan) |
| type | one scale, 9 / 10 / 11 px, system sans, Bold for captions |
| layout | display on top, one control strip, SHAPE panel below (156 px) |
| default size | 900 × 560, resizable 720 × 420 → 1800 × 1400 |
| knobs | one renderer: dark disc, value arc in the accent, tick ring outside |
| buttons | rounded rects, accent fill when active |
| read-outs | dark recessed pills under each knob |

**Controls.** MODE (LP/BAND/HP/FREE), one large EDGE knob, FOLLOW, SPREAD,
BITE, CHARACTER (WARM/IRON), OUTPUT, BYPASS. Inside SHAPE: a
`LOW | MID | HIGH | FOLLOW` selector and **one shared row of knobs** that is
repointed by whichever band is selected — touching a handle on the display
selects it too.

## The specific criticisms, and what I think is behind them

Take these as symptoms. Tell me if the diagnosis is wrong.

**"The display is not professional."** It is a correct plot: log-frequency
axis, dB grid, response curve, ghost target curve, analyser trace behind. It is
also flat, evenly weighted and grey — there is no focal point, the analyser and
the curve compete, the grid is as visually loud as the data, and the filled
area under the curve is a gradient wash that says nothing. It looks like a
measurement instrument, not like an instrument.

**"The angles are not professional."** I read this as: corner radii, bevels and
the knob geometry do not agree with each other. There are 4 px radii on pills,
8 px on the well, 10 px on the shell; the knob arc runs 1.25π → 2.75π with a
tick ring; buttons are rounded rectangles at yet another radius. Nothing was
derived from anything — each number was chosen where it was typed.

**"None of the buttons are professional."** Nine near-identical rounded
rectangles (four MODE, SHAPE, BYPASS, CHARACTER, LINK, four band tabs) with no
hierarchy between "this changes what the filter IS" and "this opens a panel".

## What I want back

### Part 1 — the identity (this is the important one)

**What is the twist?** EDGE has one already and it may not be the right one: the
shelf→cut morph is continuous, so the filter never switches modes. That is a
real engineering fact and probably a weak *product* idea, because a user cannot
hear an absence of clicks.

Answer with:

* **One sentence** a shop page would lead with.
* **The single gesture** that sells it in ten seconds in a video.
* **What the twist should be** — either argue that the continuous morph is it
  and tell me how to make it visible and audible, or name a better one that the
  existing DSP can already do. The DSP cannot change, but nothing stops us from
  *presenting* it completely differently.
* **What must be cut.** Assume the panel is 30 % too busy. Name what goes.

### Part 2 — the visual specification

Not a mood board. Numbers a developer can type:

* **Grid**: base unit, column structure, the vertical rhythm every control sits
  on, and the margins.
* **Geometry**: **one** corner-radius scale derived from the base unit, and
  which element gets which. This is the "angles" complaint — answer it
  explicitly.
* **Knob**: diameter steps, arc start/end angles, arc weight, pointer style,
  tick treatment (or its removal), and the three states (idle / hover / drag).
* **Buttons**: the hierarchy — primary, secondary, toggle, tab — with sizes,
  radii and state treatments for each.
* **Type**: family, the scale in px, weights, letter-spacing, and what is
  uppercase.
* **Colour**: whether two accents is right. If a third is needed, say what it is
  *for*. Give hex values and where each is allowed.
* **The display**: the single most valuable thing you can specify. How does the
  curve become the focal point? What happens to the analyser, the grid, the
  fill, the axis labels, the handles? What is the visual weight order?
* **Depth**: shadows, insets, bevels — how much, or none at all, and why.

### Part 3 — the work order

```
PHASE n — <title>

  PROBLEM     what is wrong, in one or two sentences
  CHANGE      the specific change, with numbers
  ACCEPTANCE  something a screenshot test can assert
  RISK        what this could break
  EFFORT      S / M / L
```

Ordered by how much each phase moves the plug-in towards "a product someone
pays for". I would rather have five phases that change everything than fifteen
that polish.

## Constraints that are not open to review

Say so in one line at the end if you disagree, then move on.

1. **No DSP changes.** Neutral stays bit-exact, latency stays zero, the
   vendored saturation engine is untouchable.
2. **The 24 parameters stay.** Controls may be hidden, regrouped, renamed or
   moved onto the display, but a saved project must still load. Anything that
   changes a parameter's ID, range or default breaks a shipped state hash.
3. **JUCE**, drawn in code. No image assets, no web view, no external font that
   has to be licensed.
4. It must stay legible and clickable from **720 × 420** to **1800 × 1400**, at
   100 %, 150 % and 200 % display scaling.
5. **No LFO, no sequencer, no drawable modulation, no randomiser.** One envelope
   follower exists and that is the whole modulation story.

## How to answer

* Every measurement is a number, not "a bit tighter".
* If you think a criticism is wrong, say so and show the reasoning.
* Do not restate what EDGE does back to me. Assume I wrote it.
* If an area is genuinely fine, say "nothing found" rather than inventing work.

**Part 1 first.** If the identity is wrong, the pixels do not matter.
