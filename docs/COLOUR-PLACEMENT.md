# Stage 4 — where the hidden colour goes, measured

Three placements were on the table. One of them is not implementable, and the
other two were measured rather than argued about: `EdgeEngine` carries a
non-parameter `ColourPlacement` switch that only the test suite ever touches
(`Source/Tools/test_dsp.cpp`, section 16).

## 1. Inside the resonance / feedback path — rejected on the maths

The work order allows "a small amount inside the feedback path **only if the
engine is mathematically suited to it and stable**". It is not, for two
independent reasons:

* **`ColorEngine` is not memoryless.** `WarmEngine` carries a 120 ms sag
  envelope and a DC blocker; `IronEngine` carries `lastOut` and a one-pole. A
  TPT/ZDF section's stability and its cutoff-independent `Q` both come from
  solving *one instantaneous linear* feedback equation per sample. Dropping a
  stateful nonlinearity into that loop turns it into a delay-free loop with
  memory, which the SVF form cannot solve — it would need an iterative solver
  per sample, on the audio thread.
* Even with a solver, the loop gain would become **level-dependent**. Resonance
  would then change with input level and with cutoff, which is precisely the
  "Resonance must not jump in level when the cutoff moves" requirement. As
  built, that requirement measures at **0.111 dB of spread over 60 Hz – 3 kHz**.

Not implemented, and not a candidate for v2 either without a different engine.

## 2 and 3. Pre-filter vs post-filter — measured

### Low edge: intermodulation under the cut

Two tones at 1000 Hz and 1100 Hz, 0.35 each, well inside the passband of a
300 Hz cut at Tight curve and full Depth. Their difference product lands at
**100 Hz — two octaves below the corner**, where the plug-in has just promised
there is nothing.

| Placement | 100 Hz product |
|---|---|
| **Pre-filter** | **−87.6 dBc** |
| Post-filter | −41.0 dBc |

**Post-filter is 46.6 dB worse.** The colour manufactures low end underneath a
high-pass, which is the single most damaging thing a filter plug-in can do.

### High edge: harmonics over the cut

A 1.5 kHz tone under a 3 kHz cut at Tight curve and full Depth; its third
harmonic lands at 4.5 kHz, above the corner.

| Placement | 4.5 kHz harmonic |
|---|---|
| **Pre-filter** | **−43.5 dBc** |
| Post-filter | −20.2 dBc |

**Post-filter is 23.3 dB worse.** Pre-filter, the harmonics that the colour adds
are then shaped by the very filter that is supposed to be removing that region —
which is also *why* pre-filter sounds right: the colour tracks the filter
instead of fighting it.

### What ships

Pre-filter, wired in, with an assertion that it stays that way:

```
[PASS] shipping default is pre-filter        100 Hz product -87.6 dBc
```

## The amount

The colour's drive is a pure function of the parameter state — no envelope
follower, no level detector, so pumping is structurally impossible:

```
A_edge = max( 1 − √G_edge ,  resonanceMakeup_edge )
A      = max( A_low , A_high )
drive% = 10 · A                                  → ColorEngine::setDrive
```

* `1 − √G` reads 0 at Depth 0, 0.29 at −6 dB, 0.50 at −12 dB, 0.75 at −24 dB.
* `resonanceMakeup = res · (1 − G_section0)` and **not** the raw Resonance
  control. Raw Resonance was the first version and it was wrong: at Depth 0 a
  section is a wire whatever its Q is, so Resonance did nothing audible while
  still engaging the colour — and the colour's 10 Hz DC blocker then took
  **0.97 dB off 20 Hz** for no reason. Caught by the test
  "Resonance 100 % at Depth 0 changes nothing", which now measures 0.000000000.
* 10 % of WARM's 24 dB range is **2.4 dB of pre-gain at absolute maximum**.
  14 % was tried first; the saturator's own compression then moved the level by
  1.0 dB across a −60…−18 dBFS input range, which is too much for something with
  no control and no display. At 10 % it is 0.95 dB worst-case and 0.00 dB at the
  −18 dBFS calibration point.
* The engine's engage window (drive < 5 %) means colour only begins to appear
  above `A ≈ 0.5`, i.e. around **−12 dB of Depth**. Below that EDGE is linear.
* `A` is smoothed over 30 ms, so a fast Depth ramp fades the colour in.

## Oversampling

Measured with a bin-centred 1 kHz tone under a Blackman-Harris window, colour at
maximum, everything that is not a harmonic counted as aliasing:

| | alias floor | latency | neutral path |
|---|---|---|---|
| **1× (shipped)** | **−76.7 dBc** | **0 samples** | **bit-exact** |
| 2× polyphase IIR | −85.2 dBc | 3.14 samples | not bit-exact |

Genuine aliasing from a soft 2.4 dB saturator drops 20–40 dB when oversampled.
It dropped **8.5 dB**, so most of what is being measured is not aliasing at all —
it is WARM's sag envelope amplitude-modulating the tone, which oversampling
cannot help. Paying 3.14 samples of latency, a fractional dry-path delay, and
the loss of a bit-exact bypass for 8.5 dB of something that is mostly not
aliasing is a bad trade. **1× ships.**

`ColorStage::setOversamplingFactor(2)` remains, exercised by the test, so the
decision can be revisited with one line if a future engine needs it.
