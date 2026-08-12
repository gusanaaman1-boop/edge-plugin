# EDGE — v0.17.1 verification prompt

Paste everything below the line into the review chat. Attach: the approved
mockup, plus `hero-lp-follow`, `inspector-lp-high`, `inspector-follow`,
`state-iron` and `size-min` from `outputs/ui/`.

---

## What this is

The corrective work order (fixes 1–9) has been implemented in full, in the
mandated sequence, with the checkpoint honoured after steps 1–3. This is the
verification pass: judge each fix against the approved mockup and return a
verdict per item — no new design directions, no re-litigation of decided
questions.

The functional lock held throughout: hash `0x7826a61a1db9aa41`, 24 parameters,
neutral bit-exact (measured 0.000000000000), latency 0, 93 DSP + 133
host-contract checks green in Release and under ASan+UBSan.

## What was implemented, per fix

**FIX 1 — curved EDGE PATH.** The horizontal rails are deleted. The route is
sampled from the actual cached target-response path at 1 px arc-length steps;
dots sit every 9 px of accumulated path distance, every dot's y from the curve.
LP descends right, HP left, BAND both, FREE none. No dot inside the puck's
core + 3 px. The dashed target hands over to the dotted journey on the cut
side — drawing both stacked meant neither was readable.

**FIX 2 — destination diamond.** On the target response at −54 dB or 12 px
above the floor, whichever comes first — the visible end of the cut, a
distinct object from the frequency handle.

**FIX 3 — analyzer height.** Root cause found: per-band pink energy for a
−18 dBFS total is ≈ −38 dBFS per 96th-octave band, and the old y-anchors put
that at 33 % of graph height. New anchors (0 → 8 %, −18 → 30 %, −36 → 55 %,
−66 → 94 %) put the same signal at ~45 %. Band values untouched — the tone
placement and level tests pass unchanged. Steel palette applied exactly
(#405A70/#263D51/#101B27 fill, #63809A line at 38–46 %).

**FIX 4 (order §5) — EDGE knob.** Violet base arc #9A7BFF, displacement arc
#B5A4FF at 5 px between base and live, ice-white base endpoint, violet live
endpoint, 36-LED orbit on a widened 140 px component. Amber/cyan split: gone.
Deck micro-orbits: 28/28/24/20 dots, value-lit, never animated. Semantic value
colours everywhere per the table (amber/cyan/violet/#B29CFF/#C6CCDA/champagne).

**FIX 5 (order §6) — wordmark.** The falling tail is dead. Four outlined
glyphs, 142 × 28, cap 25, stroke 2.25, segmented E arms with 2 px spine
breaks, continuous D bowl, G with a 7 px inward terminal, and the 14 × 10
EDGE CUT on the final E's top arm — inside the bounds. Three-stroke light
(8 px / 4 %, 4 px / 14 %, core).

**FIX 6 (order §7) — inspector density.** 420 × 104, centred 24 px title
strip + 1 px separator, 34 px mini-knobs, −/+ end marks, value capsules
#080C13 at 90 % radius 6, slope selector 156 × 28 with dB/oct outside the
segments and selected digits in graph-black.

**FIX 7 (order §8) — label solver.** Four candidates per handle scored
against placed labels (+6), handles (+8), the readout, the mode selector and
the open inspector (avoid-rects fed from the editor), response curve (+4) as
tiebreaker. All-fail suppresses an unselected label only. The LP/MID collision
near 1 kHz is gone.

**FIX 8 (order §9) — knee priority.** Inside the puck's 24 px exclusion:
route dots capped 16 %, trail 26 %, FOLLOW halo 28 %; the drawing order is the
specified stack; no dot inside core + 3 px.

## Honest deviations to rule on

1. **The trail is nearly invisible in still frames** — 180 ms decay barely
   survives a screenshot. Live it reads. If stills matter for marketing,
   say whether the decay budget moves (it is a display-only constant).
2. **Overall luminosity is still below the mockup.** I applied the specified
   stroke/glow numbers literally; the mockup image glows more than its own
   numbers. If the numbers should change, give new ones — I will not
   freelance them again.
3. **The G and D letterforms** are my geometry from your written spec. If the
   silhouette isn't right, correct with coordinates, not adjectives.
4. **BITE endpoint light from `colourEngage01` (0–18 %)** is still not wired.
   Small, honest gap; say if it makes this round or the next.

## Verdict format

```
FIX n — ACCEPT | REJECT
  if REJECT: the exact numeric correction, one line per change
```

Plus, if anything in the hero frame still diverges from the mockup in a way
not covered by fixes 1–9: name it with coordinates and numbers. The next pass
is polish-only; structural questions are closed.
