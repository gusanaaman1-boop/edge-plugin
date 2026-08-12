# EDGE — 2026 modernization prompt (v0.15)

Paste everything below the line into the review chat. Attach the screenshots
from `outputs/ui/` (hero-lp-follow, inspector-low, size-ref, size-min).

---

## The brief

The owner, after signing off the v0.15 signature work:

> *"Very nice, looks good. I want to upgrade the design to feel more 2026."*

Everything works and everything has a reason: the light titanium shell over a
dark instrument, the EDGE PATH with its live puck and trail, the violet
follow-push, the Path-drawn wordmark with the falling tail, the EDGE CUT
corner on exactly three surfaces, one reactive hairline. 227 automated checks
pass. That is the baseline — the question is purely: **what makes this read as
a 2026 product instead of a 2021 one, concretely, in numbers?**

You define what "2026" means. I am putting candidates on the table; commit to
keep/kill for each, and add what I missed.

## Candidates

**A. Full-bleed instrument.** Kill the card-in-shell construction: the graph
becomes the entire window surface, edge to edge, and the header and macro deck
float OVER it as elevated layers. One immersive object instead of three
stacked rectangles. This is the single biggest "period" marker in the current
build — the 2021 bento-card look versus the 2026 single-surface look.
JUCE constraint to respect: there is no real backdrop blur. A "glass" layer
here means: darkened translucent fill (raised colour at ~80 %), 1 px light
inner stroke, real drop shadow — honest glass, not fake gaussian.

**B. The grid becomes a dot lattice.** Line grids read as instrumentation;
dot lattices (1 px dots at the intersections, ~8 % opacity) read as current
and quiet the background further. Axis labels stay.

**C. Typography scale-up.** Values are 13 px in knobs and 10 px in cells —
correct but timid. 2026 interfaces let the ONE number that matters be big:
EDGE's value at ~20-24 px light-weight inside the big knob, deck values at
15-16 px, labels staying small and tracked. Numerals get room to breathe.

**D. Depth via elevation, not bevels.** Currently: borders + small shadows.
Modern stack: no borders at all on dark surfaces; separation comes from
elevation only — two shadow layers per floating element (tight 2 px contact
shadow + soft 16 px ambient), and surfaces get 2-3 % luminance steps by level.

**E. Colour temperature pass.** The dark surfaces are neutral-cool charcoal.
Candidates: shift the graph toward deep blue-black (#0A0E14 family) — already
close; push the shell warmer or cooler; introduce a very subtle vertical
luminance ramp on the graph so it is not one flat field. Numbers, if kept.

**F. Knob modernization.** The arcs are 4 px with a pointer. 2026 read:
thinner track (2 px at 20 %), value arc stays 4 px but with a rounded dot at
its end instead of a separate pointer line, no body rim. Or argue the current
construction is better and keep it.

**G. Idle-state cinema (evaluate against the motion rules).** When no audio
plays for >2 s, the analyzer region is empty. Candidates: nothing (calm is
the brand), or the EDGE PATH alone gains a slow breathing opacity on its
remaining-road segment (1.5 s period, ±6 %) as an invitation. The motion rule
says no decorative animation — if you keep this, justify why an affordance is
not decoration; killing it is a fine answer.

## Fixed — not open

1. The interaction contract: fixed inspector, selection model, EDGE PATH
   semantics, semantic labels, same-frame handle tracking.
2. Colour roles: amber = low edge, cyan = high edge, violet = movement only.
3. The EDGE CUT motif stays in its exactly-three places (it may be redrawn to
   fit the new construction, not multiplied).
4. All 24 parameter IDs, the DSP, bit-exact neutral, zero latency.
5. JUCE code only, no assets, no licensed fonts. No real backdrop blur exists;
   do not spec one.
6. Legible 720×420 → 1800×1400 at 100/150/200 %. Contrast ≥ 4.5:1 on text.
7. No LFO/sequencer/randomiser. Every animation driven by engine state
   (the one candidate exception is G — decide it).

## Answer format

```
CANDIDATE <letter> — KEEP or KILL

  RATIONALE   two sentences max
  SPEC        exact numbers: colours, sizes, opacities, shadow params
  ACCEPTANCE  something the pixel-probe / screenshot rig can assert
  EFFORT      S / M / L
```

Order the kept candidates by visual impact per effort. Then describe the
target hero frame (LP, EDGE 55 %, FOLLOW pushing, techno loop) in enough
detail to implement without a picture — the 2026 version of the shot that is
currently on the table.
