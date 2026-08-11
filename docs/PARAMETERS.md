# EDGE — host parameters

**State version 2.** Every ID below is namespaced and semantic. The v0.1 IDs
(`lowFreq`, `focus`, `link`, …) are gone; a v0.1 project is **migrated on load**
rather than reset — see `Core/StateMigration.h` and §"Migration" below.

24 host parameters: the 20 specified, plus CHARACTER and the three-control MID
band. Nothing else about the
internal colour engines appears — no drive, no bias, no mix, no oversampling.
BITE is how much; CHARACTER is which of two voicings.

## Targets — what EDGE is travelling towards

| ID | Name | Range | Default | Reads out as |
|---|---|---|---|---|
| `low.freq` | Low Freq | 20 – 8000 Hz, log (knob centre 120 Hz) | 250 Hz | `250 Hz` |
| `low.depth` | Low Depth | 0 – 100 % | 100 | `-6.0 dB`, `CUT` |
| `low.curve` | Low Curve | 0 – 100 % | 75 | `SOFT` / `24 dB/oct` |
| `low.shoulder` | Low Shoulder | 0 – 100 % | 0 | `OFF` / `-6.6 dB` |
| `low.reso` | Low Reso | 0 – 100 % | 0 | `35 %` |
| `high.freq` | High Freq | 200 – 20000 Hz, log (knob centre 3 kHz) | 6000 Hz | `6.00 kHz` |
| `high.depth` | High Depth | 0 – 100 % | 100 | as `low.depth` |
| `high.curve` | High Curve | 0 – 100 % | 75 | as `low.curve` |
| `high.shoulder` | High Shoulder | 0 – 100 % | 0 | as `low.shoulder` |
| `high.reso` | High Reso | 0 – 100 % | 0 | `20 %` |

Depth defaults to **CUT on both sides** so that selecting BAND and opening EDGE
immediately produces a real band-pass, as specified.

## MID — the movable bell

| ID | Name | Range | Default |
|---|---|---|---|
| `mid.freq` | Mid Freq | 60 – 12000 Hz, log (knob centre 800 Hz) | 1000 Hz |
| `mid.gain` | Mid Gain | −18 … +18 dB | 0 dB |
| `mid.reso` | Mid Reso | 0 – 100 % | 40 |

A peaking bell on the same TPT section the rest of the filter is built from:

```
low + k·band + high == x
y = x + (G−1)·(k·band)     →     H(s) = (s² + G·k·s + 1) / (s² + k·s + 1)
```

Unity at DC and at Nyquist, exactly G at the corner, and **`G = 1` is a
bit-exact wire** — which is what lets the MID band exist without costing
anything at 0 dB, and what keeps EDGE 0 bit-exact with a MID target set.

* Positive gain is a peak, negative is a notch. Measured: 12 / 6 / −6 / −12 dB
  all deliver within 0.01 dB at the corner and leave 60 Hz within 0.2 dB.
* Resonance narrows it: **4.12 octaves** wide at 0 %, **0.71 octaves** at 100 %.
* **EDGE scales its gain and leaves its frequency alone.** The first version
  swept the frequency too, from the bottom of the range up to the target, so
  that opening EDGE would walk the peak across the spectrum. In use that was
  wrong twice over: the bell was never at the frequency the knob said unless
  EDGE happened to sit at 100 %, and its handle therefore floated somewhere the
  curve was not. A control you place has to stay placed.
* It still travels — with **SPREAD** and with **FREE**'s centre move, both of
  which are octave offsets applied to everything at once, so the band and the
  bell inside it stay one shape. Measured: SPREAD 100 % moves it 12.000
  semitones, the same as the edges.
* It counts as filter activity, so a big bell engages the colour like anything
  else does.
* It is drawn in the neutral colour on the display, not a third accent — orange
  means low and cyan means high, and a third would stop that being true.

**A bell is deliberately not monotonic.** It is the one thing in the plug-in
allowed above 0 dB, and the shape-invariant test now isolates the two edges by
holding MID at unity rather than pretending the rule still covers everything.

## Performance — what you play

| ID | Name | Range | Default |
|---|---|---|---|
| `mode` | Mode | LP / BAND / HP / FREE | BAND |
| `edge` | Edge | 0 – 100 % | 0 |
| `follow` | Follow | −100 … +100 % | 0 |
| `spread` | Spread | −100 … +100 % | 0 |
| `bite` | Bite | 0 – 100 % | 35 |
| `output` | Output | −24 … +24 dB | 0 |
| `bypass` | Bypass | on / off | off |
| `character` | Character | WARM / IRON | WARM |

## Follow setup — inside SHAPE

| ID | Name | Range | Default |
|---|---|---|---|
| `follow.sens` | Follow Sens | −60 … 0 dBFS | −12 dB |
| `follow.attack` | Follow Attack | 0.1 – 200 ms, log | 10 ms |
| `follow.release` | Follow Release | 5 – 2000 ms, log | 150 ms |

**LINK is not a parameter.** It survives as an editor gesture inside SHAPE:
moving one frequency moves the other by the same number of octaves. It is
deliberately not applied to incoming host automation — a processor that writes
parameters back to the host turns one automated lane into two fighting ones.

**FOCUS is gone.** It had no equivalent in the new control set and nothing
inherits it.

## FREE — the band that travels

A fourth mode. Both edges are active as in BAND, and three things change:

* **the corners do not travel with EDGE.** They sit where you put them, and
  EDGE becomes purely "how deep". Measured: at EDGE 40 % and EDGE 100 % the
  corners are within 0.01 octaves of each other while the depth moves from
  −8.8 dB to −110 dB.
* **the band is draggable as a unit** on the display. Grab anywhere between the
  two handles and it slides, moving both corners by the same number of
  *octaves*, so the width is preserved. Moving them by the same number of Hz
  would squash the band in the bass and stretch it at the top. The shift is
  clamped as one number, not per corner, so hitting a range end stops the band
  rather than deforming it.
* **FOLLOW moves the band's CENTRE**, up to ±2 octaves, instead of driving
  EDGE's amount. Measured: 2.00 octaves of travel with the width unchanged at
  2.000 octaves.

EDGE 0 is still bit-exact in FREE, and switching in and out of it measures
−100 dBFS of discontinuity — it crossfades both the corner travel and FOLLOW's
destination rather than switching them.

## CHARACTER — the second voicing

| | |
|---|---|
| **WARM** | a memoryless rational soft saturator with a slow sag envelope and a drive-dependent bias. Round, fat, gently compressing, even-harmonic. |
| **IRON** | the same saturator inside a feedback loop whose return passes a one-pole "core-loss" filter, so the transfer depends on what just happened. Denser, transformer-ish, odd-harmonic. |

### A third voicing was built and then removed

FOUR COLOR's diode-pair engine ("GRIT") was implemented, measured, and taken
back out. It pre-emphasises the top of the band *into* a hard-knee exponential
clipper, so it makes harmonics out of content that is already high and they fold
straight away. Measured at 1×, BITE 100, full cut:

| drive | alias floor |
|---|---|
| 16.00 % | −58.7 dBc |
| 11.19 % | −61.7 dBc |
| 7.67 % | −63.9 dBc |
| 4.82 % | −65.6 dBc |

The curve **flattens**. Capping the drive — which is what the work order says to
do when a character cannot meet the limit at 1× — buys 7 dB across the whole
useful range and never reaches −70 dBc, and by 4.8 % the engine is barely
engaged anyway. The alternatives were 2× oversampling for the whole plug-in,
which costs 3.14 samples of latency and the bit-exact bypass, or moving the bar.

It becomes available the moment zero latency stops being a requirement;
`ColorStage::setOversamplingFactor(2)` is the only line that would change.

Both shipped characters are vendored from FOUR COLOR unmodified. They are voicings, not models: no
drive, bias, mix or oversampling is exposed for either, and BITE remains the
only amount control.

**They have different drive ceilings, and the difference is measured rather than
chosen.** IRON's feedback low-pass throws far less energy at Nyquist — at the
same drive it measures **−107 dBc** of aliasing against WARM's **−74**. Handing
that headroom back is what makes IRON a distinct voicing: at a shared 24 %
ceiling the two were only 1.4 dB apart in second-harmonic content. IRON's
ceiling is 46 %, and at that ceiling it still measures **−89.8 dBc**.

Measured harmonic profiles at BITE 100 with a full cut, relative to the
fundamental:

| | h2 | h3 | h4 | h5 |
|---|---|---|---|---|
| WARM | −37.5 | −19.9 | −45.1 | −31.4 |
| IRON | −68.3 | −15.1 | −40.7 | −27.5 |

Each character carries its own measured level trim, so swapping does not change
the level of passband material: WARM −0.03 dB, IRON +0.31 dB. Switching during
playback is a 20 ms equal-gain crossfade between two engines that are both
already running — measured at −100 dBFS of discontinuity.

## EDGE, and what it travels

| | at EDGE 0 | at EDGE 100 |
|---|---|---|
| Low corner | 20 Hz (the range floor) | `low.freq` |
| High corner | 20 kHz (the range ceiling) | `high.freq` |
| Depth | 0 dB | its target |
| Shoulder | 0 dB | its target |
| Resonance | 0 | its target |
| Curve | its target — a shape, not an amount | its target |
| Colour | fully disengaged | per BITE and activity |

Frequencies travel **geometrically** (linear in log2). Depth, Shoulder and
Resonance travel in the **control** domain, so EDGE stays on the same
perceptual taper the knobs use — interpolating dB linearly would put a −110 dB
target at −11 dB by EDGE 10 %, which is already a deep cut.

Measured continuity over 2000 positions: **0.105 dB** worst step at 60 Hz,
0.047 at 300 Hz, 0.004 at 2 kHz, 0.044 at 9 kHz.

At EDGE 0 the plug-in is **bit-exact and zero-latency, at any BITE**.

## Depth's taper

| % | 0 | 20 | 33 | 48 | 60 | 100 |
|---|---|----|----|----|----|-----|
| dB | 0 | −3 | −6 | −12 | −24 | −132 (`CUT`) |

The positions are chosen so the steepest segment is **2.7 dB per 1 % of
travel** — EDGE walks this taper, and anything steeper than 3 dB/% breaks its
continuity budget. The first version put −24 dB at 78 % and −48 dB at 92 %,
which is 9 dB/% at the top and measured 0.39 dB per EDGE step.

`CUT` is −132 dB rather than −∞: a literal zero makes the composite slope jump
by 12 dB/oct the instant Curve gives a section a non-zero share of the depth.

## Shoulder

A second, much gentler shelf **six octaves into the passband** from the cut's
corner, so it leans the *whole* passing side down towards the corner and the
lean travels with the cutoff. 0 – 100 % maps linearly to 0 … −12 dB.

Measured with a 200 Hz low cut at full Depth and Shoulder 100 %:
−12.3 dB at 400 Hz, −11.7 dB at 1.6 kHz, −6.1 dB at 10 kHz.

## Curve, and the slope selector

Curve is continuous: 0 → 50 widens the knee at constant order, 50 → 100 adds
order at constant knee. The display's combo snaps it to a whole slope and writes
the same parameter the knob does.

| entry | Curve % | poles | measured steepest slope |
|---|---|---|---|
| SOFT | 0 | 2, wide knee | ~10 dB/oct |
| 12 dB/oct | 50 | 2 | **12.0** |
| 24 dB/oct | 60 | 4 | **24.1** |
| 36 dB/oct | 70 | 6 | **35.7** |
| 48 dB/oct | 80 | 8 | **46.0** |
| 72 dB/oct | 100 | 12 | **61.4** |

The names describe **pole count**, which is what the cascade has and what the
industry labels. The shallow entries hit their number exactly. **The steep ones
fall short of their asymptote, for a structural reason worth stating plainly:**
a slope can only develop over the depth there is to fall through, and 72 dB/oct
needs a whole octave to drop 72 dB. With Depth's floor at −132 dB there are
under two octaves of fall before the response flattens onto it, so the asymptote
never fully arrives. Deepening the floor further would break EDGE's continuity
budget — a worse trade than a label named by pole count and documented by
measurement.

The figure quoted is the **steepest local slope anywhere in the transition**,
measured over a third of an octave. A fixed dB window cannot serve every entry:
the same −132 dB is split across however many sections Curve has engaged, so a
12-pole cascade flattens onto its own share far sooner than a 2-pole one does.
Probing at −20/−50 dB read 72 dB/oct as 49; at −12/−36 it read 24 dB/oct as 21.9.

**There is no 6, 18, 30 or 60 dB/oct entry.** At full Depth every section
carrying a share of the dB becomes a full *second-order* cut, so the slope is
`12 × (active sections)` — always even. Odd slopes need an extra first-order
stage, which is a topology change on an automatable control. 60 dB/oct exists
(5 pole pairs, Curve 90 %) and simply is not in the list, because the list is
the set you asked for.

## SPREAD

Bipolar. Both corners of a channel move together by the same number of octaves,
so each channel keeps its **bandwidth**. At ±100 % the total left-to-right
separation is **12.000 semitones** (measured). At 0 the two channels' coefficients
match exactly and identical input nulls bit-exactly. There is no inter-channel
state, no crosstalk: a silent channel measures exactly 0.

## FOLLOW

```
liveEdge = base + amount · env · (amount > 0 ? 1 − base : base)
```

Scaling by the headroom in the direction of travel is what makes the two
directions perceptually balanced: full FOLLOW reaches exactly the boundary from
any base, either way, and it can never need clamping. `amount == 0` returns the
base **bit-exactly**, which is what makes "FOLLOW 0 is identical to the follower
being disabled" true rather than nearly true.

The detector is one stereo-linked envelope follower on the input: a mono sum of
magnitudes, one attack/release pole, and a fixed dB window ending at Sensitivity.
It produces a 0–1 modulation value and nothing else — it never touches gain,
never compensates, never adapts a drive.

## BITE

```
drive = maxDrive(bite) · activity ^ gamma(bite)
maxDrive(b) = 24 · b^0.70          gamma(b) = 0.65 → 0.35
```

* `bite = 0` gives **exactly** zero drive at any activity, and zero activity
  gives exactly zero drive at any BITE. Both are asserted as exact equality.
* At BITE 35, a −4 dB shelf produces **5.97 %** drive — above the vendored
  engine's 5 % engage window. The v0.1 linear law gave 3.00 % there, which is
  why it engaged too late.
* `kBiteMaxDrive = 24` is a **ceiling set by the aliasing measurement**, not by
  taste: at BITE 100 with a full cut the alias floor is **−73.9 dBc** at 1×,
  inside the −70 dBc limit. Latency is never traded for it.

The WARM lamp reads the engine's own engage factor, not "BITE > 0".

## Migration from v0.1

| v0.1 | v0.2 |
|---|---|
| `lowFreq`, `lowDepth`, `lowCurve`, `lowShoulder`, `lowRes` | carried across unchanged |
| `highFreq` … `highRes` | carried across unchanged |
| `output`, `bypass` | carried across unchanged |
| `focus` | **dropped** — no equivalent; folding its octave offset into the frequencies would move a corner the user set by hand |
| `link` | **dropped as a parameter**; survives as editor behaviour |
| — | `mode` = BAND, the only mode in which both edges behave as they did |
| — | `edge` = **100 %**, because in v0.1 the targets *were* the sound |
| — | `follow` = 0, `spread` = 0 — v0.1's behaviour exactly |
| — | `bite` = **28.6 %**, the value at which the new law reproduces v0.1's 10 % drive at a full cut |

Migrating twice is a no-op. All of this is asserted in the test suite.
