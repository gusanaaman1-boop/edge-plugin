# EDGE — host parameters

**These IDs are frozen.** Automation lanes and saved projects reference the
strings, not the names. New parameters may only be appended.

14 host parameters. Nothing about the internal colour engine appears here, by
design — no drive, no model, no bias, no mix, no oversampling.

| # | ID | Name | Range | Default | Reads out as |
|---|---|---|---|---|---|
| 1 | `lowFreq` | Low Freq | 20 – 8000 Hz, log (knob centre 120 Hz) | 20 Hz | `48.0 Hz` / `1.20 kHz` |
| 2 | `lowDepth` | Low Depth | 0 – 100 % | 0 | `-6.0 dB`, `CUT` at 100 |
| 3 | `lowCurve` | Low Curve | 0 – 100 % | 50 | `SOFT` / `24 dB/oct` / `TIGHT` |
| 4 | `lowRes` | Low Reso | 0 – 100 % | 0 | `35 %` |
| 5 | `highFreq` | High Freq | 200 – 20000 Hz, log (knob centre 3 kHz) | 20 kHz | `6.50 kHz` |
| 6 | `highDepth` | High Depth | 0 – 100 % | 0 | `-12.0 dB`, `CUT` |
| 7 | `highCurve` | High Curve | 0 – 100 % | 50 | as `lowCurve` |
| 8 | `highRes` | High Reso | 0 – 100 % | 0 | `20 %` |
| 9 | `link` | Link | on / off | off | |
| 10 | `focus` | Focus | −100 … +100 | 0 | `+40  narrow` |
| 11 | `output` | Output | −24 … +24 dB | 0 | `2.0 dB` |
| 12 | `bypass` | Bypass | on / off | off | |
| 13 | `lowShoulder` | Low Shoulder | 0 – 100 % | 0 | `OFF` / `-7.2 dB` |
| 14 | `highShoulder` | High Shoulder | 0 – 100 % | 0 | `OFF` / `-7.2 dB` |

Parameters 13 and 14 were **appended** after v0.1. The first twelve IDs and their
order are unchanged, so a project saved with v0.1 loads with its automation
lanes intact and simply gets Shoulder at 0.

## Depth's taper

Piecewise linear **in dB**, through the work order's own perceptual table, so
the labelled values land exactly on their control positions:

| % | 0 | 25 | 40 | 58 | 78 | 92 | 100 |
|---|---|----|----|----|----|----|-----|
| dB | 0 | −3 | −6 | −12 | −24 | −48 | −120 (`CUT`) |

Measured DC gain matches the label to better than 0.05 dB at every breakpoint.
Six octaves below the corner the real filter is within 0.5 dB of it; closer to
the corner a second-order shelf is still on its way down, which is the shelf
being a shelf, not an error.

`CUT` is −120 dB rather than −∞, deliberately — see `DSP-TOPOLOGY.md` §3.

## Shoulder

A second, much gentler shelf sitting **six octaves into the passband** from the
cut's corner, so it leans the *whole* side that passes down towards the corner
rather than only the region next to it. Under a 9 kHz cut it pulls 0 – 9 kHz
down; and because its corner is defined relative to the cut's, the lean travels
with the cutoff when that is automated.

0 – 100 % maps linearly to 0 … −12 dB. Measured with a 200 Hz low cut at full
Depth and Shoulder at 100 %:

| 400 Hz | 1.6 kHz | 10 kHz |
|---|---|---|
| −12.3 dB | −11.7 dB | −6.1 dB |

(The first version reached three octaves and left 1.6 kHz at −4.4 dB. That was
a lean next to the corner; this is a lean across the passband.)

It is the same `MorphSection` the cut is built from, with a wide knee
(damping 2.2), so:

* at 0 dB it is a **bit-exact wire** — the control is free when unused, and the
  plug-in's neutral state stays bit-exact;
* the cascade stays **monotonic by the same proof** (`k > √2`, `b = √G`),
  verified over the whole Depth × Curve × Shoulder grid;
* it counts as filter activity, so it engages the hidden colour like anything
  else does.

Curve does not affect it. The shoulder is deliberately one fixed, gentle shape:
a second slope control there would make the pair of them a graphic EQ.

## Curve, and the slope selector

Curve is continuous. 0 → 50 widens the knee at constant order; 50 → 100 adds
order at constant knee. Both halves meet at the same point, so the control is
continuous end to end (measured worst step over 2000 positions: 0.083 dB at full
cut, 0.003 dB at −6 dB).

The **combo box on the display** snaps it to a whole slope. It writes the same
`lowCurve` / `highCurve` parameter the knob does — there is no second parameter
and no second source of truth, and turning the knob off a snap point clears the
combo's selection and shows the percentage instead.

| entry | Curve % | measured slope at full cut |
|---|---|---|
| SOFT | 0 | wide knee, ~10.5 dB/oct |
| 12 dB/oct | 50 | 11.8 |
| 24 dB/oct | 75 | 23.6 |
| 36 dB/oct | 100 | 35.1 |

**Why there is no 6, 18 or 30 dB/oct.** At full Depth every section that carries
a share of the dB becomes a full *second-order* cut, so the asymptotic slope is
12 × (number of active sections) — an integer. Curve = 62.5 % does not give
18 dB/oct; it gives two active sections carrying unequal shares, and it measures
**23.5 dB/oct**. Odd slopes need an extra first-order stage in the cascade,
which is a real feature but also a one-pole section switching in and out with
the parity of the order — a topology change on an automatable control. Left for
a follow-up rather than shipped as a label that lies.

## Link

`link` saves and restores, and appears in the host's parameter list, but the
coupling itself is an **editor gesture**: moving one frequency in the UI moves
the other by the same number of octaves. It is deliberately *not* applied to
incoming host automation — a processor that writes parameters back to the host
turns one automated lane into two lanes fighting each other.

## Focus

A macro over the two frequencies, in **octaves**, ±2 octaves at ±100. Positive
brings the edges together. Near a range boundary it is soft-limited
(`h·tanh(x/h)`) so it slows down instead of stopping dead: measured worst jump
over 400 positions is 12 cents, and the ends stay inside the ranges.

The two effective corners are kept at least 1.05 apart (under a fifth of a
semitone) by a soft maximum, so the pair cannot collapse to a zero-width
passband.

## What Output does and does not compensate

Output is the user's trim, plus exactly two static, parameter-derived
corrections, both smoothed per sample:

* **the resonance make-up**, `−1.5 dB × res × (1 − G_section0)` — self-gating, so
  it is exactly 0 dB whenever Depth is 0;
* **the colour engine's measured small-signal level change**, cancelled so that
  engaging Depth does not quietly change the level of passband material.

It never compensates the attenuation the user asked for with Depth, and there is
no dynamic AGC anywhere in the plug-in.
