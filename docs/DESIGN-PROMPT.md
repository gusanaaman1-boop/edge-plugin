# EDGE — v0.17 divergence report and correction request

Paste everything below the line into the review chat. Attach BOTH: the approved
mockup image, and the current renders from `outputs/ui/` (hero-lp-follow,
inspector-low, state-iron, size-min).

---

## What happened

v0.17 was implemented in one pass as ordered, all 226 automated checks are
green, the functional lock held (hash `0x7826a61a1db9aa41`, neutral bit-exact,
latency 0). The owner's verdict on the result:

> *"Extremely disappointed. It looks nothing like the mockup I sent, and the
> display is full of glitches again."*

He is right that it diverged. I compared my render against the mockup
pixel-by-pixel and these are the gaps I can see myself — confirm, correct and
extend this list, then give ONE corrective work order with exact numbers.

## The divergences I can already name

**1. EDGE PATH follows the wrong geometry — the biggest miss.** In the mockup
the dotted path rides **along the response curve itself**: from the live puck
at the knee, the cyan dots run down the target curve's slope to the diamond
sitting at the *bottom* of the cut. Mine draws a **horizontal rail** at the
puck's height. The mockup's version is dramatically better — the journey IS
the curve the sound will take. Spec needed: the dotted path is sampled along
the TARGET response between the live corner's position and the target corner,
dots following the curve's y at each x.

**2. Values are not accent-coloured.** Mockup: `148 Hz` in amber inside the
LOW knob, `1.20 kHz` in cyan, `+70 %` and `55 %` in violet. Mine renders all
values ice-white. Spec needed: value colour per control, and which stay
neutral.

**3. The EDGE knob's arc is violet in the mockup** — movement colour for the
travel control — with the LED orbit around it. Mine kept the amber→cyan split
arc. Also every deck knob in the mockup carries a fine dotted micro-tick ring;
mine has none.

**4. The wordmark is wrong.** The mockup's mark is large (~25 px cap),
outlined/segmented geometric letters with a soft glow, reading instantly as a
logo. Mine is thin strokes with a falling tail that reads as a scribble at
small size. Needs a full letterform spec: outline weight, segment breaks,
spacing, glow (if any), and whether the falling-tail idea survives at all.

**5. Luminosity is an order of magnitude apart.** The mockup glows: puck halo,
LP segment glow, arc glows, analyzer with strong presence filling most of the
graph height. Mine is matte and dim by comparison — I applied the "localized
light" strokes conservatively. Spec needed: exact glow radii/opacities per
element, and which elements get none.

**6. Density/scale.** Mockup knobs are larger relative to the deck, labels sit
INSIDE the accent colour, +/− end marks under mini knobs, value capsules under
the inspector knobs (mine puts plain text). The inspector in the mockup has a
title bar strip with the context name centred.

## The glitches

The owner reports "the display is full of glitches" — the ones I can see in my
own render: the MID and LP handle name labels collide near 1 kHz; the dashed
target, dotted path, trail dots and the violet ring stack into visual noise
around the knee; the wordmark tail collides visually with nothing but reads as
a rendering error. **Ask him for the specific list with a screenshot and mark
each one**, then include every fix in the work order with an acceptance test.

## What is fixed and cannot move

The functional lock from the v0.17 order stays exactly as written: no DSP,
parameter, preset or state changes; hash `0x7826a61a1db9aa41`; engine-driven
motion only; JUCE code only, no assets; all current tests stay green.

## What I need back

ONE corrective work order, implementation-ready:

```
FIX n — <element>
  CURRENT     what my render does (from the screenshots)
  TARGET      what the mockup does
  SPEC        exact numbers: geometry, colours, opacities, sizes
  ACCEPTANCE  what the screenshot/pixel rig can assert
```

Ordered by visual impact. The EDGE PATH curve-following geometry is #1 unless
you disagree with reasons. Where the mockup and the motion rules conflict
(glow that implies activity while idle), say which wins and why.
