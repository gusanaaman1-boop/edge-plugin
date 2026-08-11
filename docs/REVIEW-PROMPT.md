# EDGE — review prompt (v0.6)

Paste everything below the line into the review chat, then attach
`outputs/EDGE-SOURCE-APPENDIX.md` — the real source, unabridged.

---

You are a senior audio DSP engineer and commercial plug-in developer. Review a
working VST3/AU/Standalone filter plug-in and return a **phased work order**,
not advice.

## What EDGE is

A two-sided musical filter for electronic music. JUCE 9, C++20, macOS universal,
targeting Cubase 15. It builds, `auval` passes, **93 measurement checks pass** in
Release and under ASan+UBSan with zero sanitiser diagnostics, there are **zero
heap allocations in 10,000 audio blocks**, and it runs at **264× realtime**
stereo with the analyser feeding.

The idea the whole thing is built on: **each spectral edge moves continuously
from a gentle shelf, through deeper attenuation, to a real cut — and it is the
same filter the whole way.** One large control drives that transition from both
sides, and the signal itself can drive that control.

## Every control, and what it does

**The performance row — what you play**

| control | what it does |
|---|---|
| **MODE** | `LP` low edge becomes an identity, high edge is the low-pass. `HP` the mirror. `BAND` both active. `FREE` both active, corners do **not** travel with EDGE, the band is draggable as a unit, and FOLLOW moves its centre instead of EDGE's position. |
| **EDGE** | 0–100 %. The master morph. At 0 the plug-in is **bit-exact and zero-latency**, whatever else is set. Opening it walks both corners geometrically from the range boundaries to their targets, and scales Depth, Shoulder, Resonance and the MID bell's gain from nothing to their targets. |
| **FOLLOW** | −100…+100 %. A stereo-linked envelope follower on the input, modulating EDGE's position (or, in FREE, the band's centre by up to ±2 octaves). Positive drives further into the cut, negative opens up. |
| **SPREAD** | −100…+100 %. Offsets the two channels' corners in opposite directions, in octaves, **both corners of a channel together** so each keeps its bandwidth. ±12 semitones total at full. |
| **BITE** | 0–100 %, default 35. How much hidden colour. `drive = maxDrive(bite) · activity^gamma(bite)`. |
| **CHARACTER** | `WARM` / `IRON`. Which hidden engine BITE drives. Nothing else about them is exposed. |
| **OUTPUT**, **BYPASS** | trim; bypass is a bit-exact dry path. |

**The targets — inside SHAPE**

| control | what it does |
|---|---|
| **Frequency** (low, high) | where that edge is going. Low 20–8 k, high 200–20 k, log. |
| **Depth** | 0 dB → −132 dB (`CUT`). A shelf whose gain degenerates into a cut. |
| **Curve** | knee width over its soft half, pole count over its tight half. The display's combo snaps it to SOFT / 12 / 24 / 36 / 48 / 72 dB/oct. |
| **Shoulder** | 0 → −12 dB. A second, gentler shelf **six octaves into the passband**, so the whole passing side leans down towards the corner and the lean travels with the cutoff. |
| **Resonance** | lowers Q on the first section only. Silent at Depth 0 by construction. |
| **MID Freq / Gain / Reso** | a movable bell inside whatever the edges let through. ±18 dB, 60 Hz – 12 kHz. Stays where it is put; EDGE scales only its gain. |
| **Follow Sens / Attack / Release** | the detector's operating level and timing. |

24 host parameters, state version 2, with a migration from v0.1.

## Architecture — do not reverse-engineer it

**One section type does almost everything.** A TPT/ZDF state-variable section
gives `low + k·band + high == x` exactly, and EDGE recombines those:

```
edge section:   y = x + (G−1)·low   + (√G−1)·(k·band)      [low side]
mid  section:   y = x + (G−1)·(k·band)                     [bell]
```

Three consequences carry the product:

* **`G = 1` is a bit-exact wire.** Depth 0, EDGE 0, an inactive MODE edge, a
  Curve setting that does not use a section, and MID at 0 dB are all *the same
  thing*: a section at unity. Nothing is ever switched in or out, so none of
  them can click, and none of them needs a bypass branch.
* **`G = 0` is a textbook second-order cut.** The shelf→cut morph is algebra,
  not a crossfade.
* With `b = √G` and `k ≥ √2` the edge response is **provably monotonic** — the
  "no unplanned peak" requirement is discharged analytically.

**Curve** distributes the depth *in dB* across six always-running sections by
smoothstep weights (`Σw = 1`, so the total depth is exactly what was asked for
at every Curve setting; Curve only changes how many octaves it takes).

**The gain multipliers interpolate per sample.** `y = x + (G−1)·low` means a
step in G is a step in the output; updating once per 32-sample chunk measured
−43 dBFS of discontinuity on a mode switch, and two multiply-adds per sample
bought −100 dBFS.

**The colour** is FOUR COLOR's engines, vendored byte-identical, one instance
per channel per character (their engage smoother is a single shared member —
processing L then R on one instance measured 0.014 of stereo divergence).
Pre-filter, at 1×, zero latency.

## Already measured — do not spend the answer rediscovering it

| | |
|---|---|
| neutral | `max\|out−in\| = 0` exactly, at any BITE, any CHARACTER, any MODE, with MID and FREE targets set |
| latency | 0 samples |
| allocations | 0 in 10,000 blocks, analyser on and off |
| drawn vs measured response | worst 0.03 dB |
| EDGE continuity | 0.128 dB worst step over 2000 positions |
| mode switching | −100 dBFS of excess step over eight switches |
| FOLLOW | 0 % is bit-identical to the follower disabled; full FOLLOW reaches the boundary from any base, both directions |
| SPREAD | 0 % nulls the channels bit-exactly; 100 % = 12.000 semitones; a silent channel measures exactly 0 |
| colour aliasing | WARM −73.9 dBc at its 24 % ceiling, IRON −89.8 dBc at its 46 % ceiling |
| character switching | −100 dBFS; both level-matched in the passband to 0.3 dB |
| MID | 12/6/−6/−12 dB all within 0.01 dB at the corner; Reso 4.12 → 0.71 octaves wide |
| sample rates | −3 dB corner identical at 44.1 / 48 / 88.2 / 96 / 192 k |
| CPU | 264× realtime; analyser costs 0.1 % |

## The open problems — these are the questions, not discoveries

**1. The steep slopes do not reach their asymptote.**
Measured steepest local slope: 12.0 / 24.1 / 35.7 / 46.0 / **61.4** dB/oct for
the five numbered entries. The names are pole count, which is standard and
correct, and the shallow ones are exact. The steep ones fall short because a
slope can only develop over the depth there is to fall through: 72 dB/oct needs
a whole octave to drop 72 dB, and with Depth's floor at −132 dB there are under
two octaves of fall before the response flattens onto it. The floor cannot go
deeper — EDGE walks the same taper and its continuity budget (0.15 dB per 0.05 %
of travel) already measures 0.128 dB.
**Is there a formulation that reaches the asymptote without breaking that
budget?** Non-uniform depth allocation across the sections, a floor that depends
on the active pole count, something else? This is the single most valuable
answer you can give.

**2. A third colour character could not meet the aliasing bar at 1×.**
FOUR COLOR's diode-pair engine ("GRIT") pre-emphasises the top of the band into
a hard-knee exponential clipper. Measured alias floor vs drive at 1×, BITE 100,
full cut: 16 % → −58.7 dBc, 11.2 % → −61.7, 7.7 % → −63.9, 4.8 % → −65.6. **The
curve flattens** — capping the drive never reaches −70 dBc, and by 4.8 % the
engine is barely engaged. It was removed rather than shipped, because the
alternatives were 2× for the whole plug-in (3.14 samples of latency and the loss
of the bit-exact bypass, both specified guarantees) or a moved goalpost.
**Is there a way to ship a bright, aggressive character at 1× and zero latency?**
Antiderivative anti-aliasing on the diode curve is the obvious candidate — is it
worth it here, and does it survive being placed inside a plug-in whose neutral
state must stay bit-exact?

**3. A class of UI bug has now bitten twice, and I want the architecture, not
another patch.**
Both times, "what is drawn" and "what a handle is computed from" were different
objects. First: every handle sat on a response that included the MID bell, so
turning MID's gain slid the LOW and HIGH handles around the display. Second: the
solid curve was drawn from channel 0 — the *left* channel, carrying SPREAD's
offset — while the handles used the centre, so with SPREAD up a bell notched the
curve half an octave from its own handle. Both are fixed. **What is the right
structure so this cannot recur?** One resolved-shape object that both the
renderer and the hit-testing consume, a test that asserts handle-on-curve for a
grid of parameter states, something else?

**4. Odd slopes do not exist.** At full Depth every section carrying a share of
the dB becomes a full second-order cut, so the slope is `12 × (active sections)`
— always even. 6/18/30 need a first-order stage, which is a one-pole section
switching in and out with the parity of the order: a topology change on an
automatable control, which nothing else in EDGE does. **Is there a continuous
formulation?**

**5. Resonance weakens as Curve tightens** — +7.8 dB at SOFT, +2.8 dB at TIGHT,
because the other sections keep falling through the peak. How would you make it
constant without compounding Q across sections and without breaking the
monotonicity proof?

**6. What is missing for a commercial release** — presets, metering, sizing,
host quirks, anything a paying user notices in the first ten minutes.

## Rules that are not open to review

Say so in one line at the end if you disagree, then move on.

1. No LFO, no sequencer, no drawable modulation, no randomisation. One envelope
   follower is allowed and exists.
2. Neutral must stay **bit-exact and zero-latency**.
3. BITE and CHARACTER are the only public colour controls. No drive, bias, mix
   or oversampling.
4. No hidden compressor, dynamic EQ, AGC or loudness rider.
5. **Do not propose edits to any file under `Source/Vendor/`.** That is another
   product's engine, vendored byte-identical. Propose changes to how EDGE *uses*
   it instead.

## The answer I want

One row per item, ordered by value:

```
PHASE n — <title>

  PROBLEM     what is wrong or missing, in one or two sentences
  CHANGE      the specific change, naming files and functions
  ACCEPTANCE  a NUMBER a test can assert, and how to measure it
  RISK        what this could break, and what would catch it
  EFFORT      S / M / L
```

* Every acceptance criterion must be a number, not "sounds better".
* If you think one of the open problems is wrong, say so and show the reasoning.
* Do not restate the architecture back to me. Assume I wrote it.
* If you find nothing wrong in an area, say "nothing found" rather than
  inventing work.

The full source follows as an attachment.
