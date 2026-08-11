# EDGE — DSP topology decision record

Stage 2 of the work order. Written before any DSP code existed; the numbers
quoted as *measured* were filled in afterwards from `EdgeTests` and are marked
as such.

---

## 1. The problem that decides the architecture

Everything else in EDGE is ordinary. One requirement is not:

> Depth moves continuously from **0 dB** (nothing) through a **shelf** to
> **−∞** (a real cut), and the move must be continuous in level, phase, colour,
> cutoff feel and automation.

Two obvious implementations both fail it.

### Rejected A — dry/wet blend of a cut against the dry signal

`y = (1−m)·x + m·HP(x)`. This is what the work order warns about, and the
warning is correct. `HP` is minimum-phase, so at the corner it is roughly
`0.707∠+45°`. Mixing that against a dry `1∠0°` gives, at `m = 0.5`,
`|y| = 0.85` — a **+0.7 dB bump above unity** just above the corner and a
non-monotonic response below it. It also never sounds like a shelf, because a
shelf has one pole/zero pair and this has a pole and a summed dry path.

### Rejected B — switching topology at some Depth threshold

Shelf biquad below −20 dB, high-pass biquad above. Every discontinuity the
work order forbids, at one point in the control's travel.

### Chosen — a shelf whose gain *degenerates into* the cut

A first-order low shelf with low-frequency gain `G` and high-frequency gain 1 is

```
H(s) = (s + G·ω₀) / (s + ω₀)
```

At `G = 1` this is `1`. At `G = 0` it is `s/(s+ω₀)` — **exactly a high-pass**.
The cut is not a different filter, it is the same filter at the end of its own
gain range. Continuity in magnitude, phase and cutoff feel is then not
something to engineer, it is a property of the algebra.

EDGE uses the second-order form of this idea, built on the TPT/ZDF SVF.

---

## 2. The core section — "morphing shelf SVF"

One TPT state-variable section produces `low`, `band`, `high` from one state,
and in the project's existing `fourcolor::dsp::TptSvf` they satisfy

```
low + k·band + high = x        (exactly, by construction of the `high` output)
```

**Low Edge section** — take the three outputs with weights:

```
y = G·low + b·(k·band) + high
  = x + (G−1)·low + (b−1)·(k·band)          ← the form actually implemented
```

**High Edge section** — the mirror image:

```
y = low + b·(k·band) + G·high
  = x + (G−1)·high + (b−1)·(k·band)
```

with

* `G` — the section's shelf gain, `0 ≤ G ≤ 1` (linear)
* `b = √G` — the band weight
* `k = 1/Q` — damping, `k = √2` is Butterworth

### Why this is the right section

**Neutral is bit-exact.** At `G = 1` we get `b = 1`, so `y = x` with no
arithmetic left to do — not "−120 dB of error", literally the input. Depth = 0
therefore needs no bypass branch, and acceptance criterion "in neutral the
plug-in does not audibly change the signal" is satisfied by construction.

**Full cut is a textbook filter.** At `G = 0`, `b = 0`, the Low Edge section is
`high` — a second-order high-pass, Butterworth at `k = √2`.

**It is provably monotonic.** With `u = ω/ω₀`, `v = u²`:

```
|H|² = N(v)/D(v),  N = v² + (k²b² − 2G)v + G²,  D = v² + (k²−2)v + 1
```

`N/D` is monotonically increasing in `v` iff `N′D − ND′ ≥ 0`, and

```
N′D − ND′ = (q−p)v² + 2(1−G²)v + (p − G²q),   p = k²b² − 2G,  q = k² − 2
```

Substituting `b² = G`:

```
q − p     = (1−G)(k²−2)
p − G²q   = G(1−G)(k²−2)
```

So for **every** `G ∈ [0,1]` and **every** `k ≥ √2`, all three coefficients are
`≥ 0` and the response is monotonic. That is the whole of the "without
Resonance the response must stay monotonic, with no unplanned peak" requirement,
discharged analytically rather than by listening. `b = √G` is not a taste
choice — it is the value that makes the `v²` and constant terms vanish at
`k = √2`.

Below `k = √2` the section can peak. That is not a defect, that is Resonance.

---

## 3. Depth

`Depth` is a single continuous control, presented in dB of attenuation, mapping
to the total shelf gain `G_total`.

| Control | dB | Behaviour |
|---|---|---|
| 0% | 0 | pass-through, bit-exact |
| ~25% | −3 | gentle shaping |
| ~40% | −6 | clear |
| ~58% | −12 | significant |
| ~78% | −24 | nearly a cut |
| 100% | −120 | true cut |

The taper interpolates **linearly in dB** through exactly those breakpoints, so
each labelled value lands on its own control position rather than near it.
Measured DC gain matches the label to better than 0.05 dB at every one. The last
22 % of travel carries −24 → −120 dB, which is the "smooth, controlled transition
from a deep shelf to a high-pass" the work order asks for; measured worst step
over 2000 positions of the whole control is **0.0012 dB**.

### The −120 dB floor, and why it is not −∞

A literal `G = 0` would be a true zero at DC. It is *not* used, and the reason
is Curve, not numerics. See §4: at `G = 0` any section with a non-zero share of
the depth becomes a full high-pass, so the composite slope would jump by
12 dB/oct the instant Curve gives a section a non-zero share — a discontinuity
in Curve exactly at the top of Depth. Clamping `G` at 1e−6 removes it and makes
the whole (Depth × Curve) plane continuous.

The honest cost: below the corner the response falls at the Curve's slope until
it reaches −120 dB, then flattens. At 100 Hz / 36 dB/oct that flattening starts
at 10 Hz. −120 dB is 20 dB below a 16-bit dither floor. This is a real
compromise and it is the only one in the Depth path.

---

## 4. Curve

Three alternatives were on the table.

| | Mechanism | Range it can reach | Problem |
|---|---|---|---|
| **(a)** Curve = `Q` only | one section, `k` varies | 12 dB/oct always | cannot get *tight*; going tighter means `k < √2`, which breaks monotonicity — that is Resonance's job |
| **(b)** Curve = pole count only | 1–3 sections | 12/24/36 dB/oct | cannot get *soft*; and at shallow Depth adding sections barely changes the shape (measured: a −6 dB shelf reads −2.04 dB at the corner with 1 section, −2.67 dB with 3 — a 0.6 dB difference for a control that is supposed to be one of four) |
| **(c) chosen** | `Q` for the soft half, pole count for the tight half | 12 → 36 dB/oct plus a genuinely wide knee | none found; the two halves meet at the same point |

**The chosen mapping**, `c ∈ [0,1]`, Soft → Tight:

```
c ∈ [0, 0.5]:   P = 1,                k = lerp(3.6 → √2)
c ∈ [0.5, 1]:   P = 1 + 4(c − 0.5),   k = √2
```

`P` is a *continuous* pole-pair count in `[1,3]`. Both segments agree at
`c = 0.5` (`P = 1`, `k = √2`), so Curve is continuous and never crosses
`k < √2`, which keeps §2's monotonicity proof valid across its entire travel.

### How fractional `P` is realised without switching topologies

Three sections always run. Section `i` gets a share `w_i` of the **depth in dB**:

```
t_i = clamp(P − i, 0, 1)
w_i = smoothstep(t_i) / Σ smoothstep(t_j),      G_i = G_total ^ w_i
```

`smoothstep(t) = t²(3−2t)`. A plain linear `t_i / P` was the first version and it
measured badly: it has a kink in `dw/dP` at every integer `P`, and at full Depth
that kink is enormous — a section's share of −120 dB starts moving at 120 dB per
unit `P` the instant `P` leaves 1, which showed up as a **1.79 dB jump for a
0.5 % nudge of Curve**. Smoothstep makes `dw/dP` zero at both ends of each
section's window, so a section enters and leaves the cascade with zero rate of
change. Same measurement after the change: **0.083 dB, over 2000 positions**.

so `∏ G_i = G_total` — the total shelf depth the user asked for is exactly
delivered at every Curve setting, and Curve only changes how many octaves it
takes to get there.

The key property: **`w_i = 0` ⟹ `G_i = 1` ⟹ that section is `y = x`,
bit-exact** (§2). A section fades out of the cascade by becoming an identity,
not by being switched off, so there is no topology change and nothing to click.
Inactive sections keep running with `G = 1` so their state stays warm and
bounded, and fading one back in costs nothing.

`P = 1.5`, for example, gives `w = (0.667, 0.333, 0)`: two thirds of the dB in
section 0, one third in section 1 — a real intermediate slope, not a crossfade
between two filters. Measured slope at full cut: **10.5 dB/oct** at Soft,
**12.0** at Neutral, **31.9** at Tight.

### Damping across the cascade — Butterworth staggering was rejected

A true Butterworth of order `2P` needs `k_i = 2·cos(π(2i+1)/4P)`, which puts
`k = 0.765` and `k = 0.518` on the later sections. Those are below `√2`, so §2's
proof no longer covers them and each such section peaks on its own; the cascade
is flat only because the peaks cancel — which they do for a *cut*, but the whole
point here is that these sections spend most of their life as partial shelves,
where they do not cancel. It also means `k_i` would have to be interpolated
through a region where it is negative (`P → 1` sends section 1's Butterworth `k`
to −1.414), i.e. through instability, on a control the user is allowed to
automate.

EDGE therefore uses **the same `k` in every section** (Linkwitz-Riley-style
cascade rather than Butterworth). The composite corner is a little softer than a
true Butterworth of the same order, the response is monotonic for the entire
parameter space by proof rather than by measurement, and Resonance is the
control that puts a defined corner back.

---

## 4b. Shoulder (added after v0.1)

The cut answers "where does it stop". Shoulder answers "what happens to the
frequencies just before it stops" — a slow lean into the corner instead of a
flat passband that falls off a cliff.

It is **another `MorphSection` of the same side**, at a corner six octaves into
the passband (`Fc · 2^6` for LOW, `Fc / 2^6` for HIGH), with a deliberately wide
knee (`k = 2.2`) and a linear 0 … −12 dB gain.

Six octaves, not three. Three was the first version and it only leaned the
region immediately next to the corner (1.6 kHz still at −4.4 dB under a 200 Hz
cut). The control is meant to pull down the *whole* passband — "0 to 9 kHz under
a 9 kHz cut" — so that the lean travels with the cutoff when it is automated.
At six octaves the same measurement reads −11.7 dB.

Everything it needs was already proved:

* `G = 1` is a bit-exact wire, so the control is free at 0 and the plug-in's
  neutral state is untouched (asserted);
* `k = 2.2 > √2` and `b = √G`, so §2's monotonicity proof covers it, and the
  cascade of the shoulder with the cut is a product of monotone increasing
  functions — still monotone. Verified numerically over the whole
  Depth × Curve × Shoulder grid: worst dip 0.000000 dB, worst peak 0.000433 dB;
* it is inside `EdgeUnit`, so `activity()` picks it up and the hidden colour
  engages for it exactly as it does for Depth.

Curve deliberately does **not** reach it. Two independent slope controls on the
same edge is the beginning of a graphic EQ, which is what EDGE exists not to be.

## 5. Resonance

Resonance lowers `k` **on section 0 only**, `k₀ = lerp(k_curve → 0.35, res)`.

* It cannot compound: only one of the three sections is resonant, so the peak
  height does not depend on Curve's pole count.
* It is **automatically silent at Depth 0**, because at `G = 1` the section
  returns `x` regardless of `k`. No gating logic, no "unplanned peak" —
  Resonance's audibility scales with how much the filter is actually doing.
* `k = 0.35` is `Q ≈ 2.9`. Self-oscillation needs `k = 0`; the ZDF form is
  unconditionally stable for `k > 0` at any cutoff and any sample rate, so
  "stable at every sample rate" and "no self-oscillation in v1" are both
  structural. Measured: peak **+7.75 dB** at full cut, impulse tail after 0.5 s
  at −100 dBFS, peak height constant to **0.111 dB** over 60 Hz – 3 kHz of
  cutoff travel.
* **The one honest cost of putting it on section 0 only**: with a Tight curve
  the other two sections keep falling straight through the peak, so the emphasis
  reads as less. Measured at full cut and full Resonance —
  Soft 7.8 dB, Neutral 7.8 dB, 75 % 5.1 dB, Tight 2.8 dB. Audible everywhere,
  strongest where the corner is gentlest. Making it constant needs the
  resonance distributed across the active sections with a compensation for how
  many are active, which is a v2 item, not an MVP one.
* Because it is a TPT/ZDF section, the resonance does not gain-jump when the
  cutoff moves — see §7.

---

## 6. The saturation engine, and where it goes

The engine is FOUR COLOR's `fourcolor::ColorEngine`, vendored unmodified
(`Source/Vendor/VENDOR.md`). EDGE uses the **WARM** engine.

Full behavioural description in `docs/SATURATION-AUDIT.md`. The three properties
that drive the integration decision:

1. `WarmEngine` is **stateful** (a ~120 ms sag envelope) and **asymmetric** (a
   drive-dependent bias), so it does produce DC — and it already contains its
   own `DcBlocker`. No extra DC blocker is needed or wanted.
2. At `drive = 0` the engine's `blend()` returns the input **bit for bit**, and
   from 0 to 5% drive it fades in on a smoothstep with zero derivative at both
   ends. This is the property that lets EDGE's colour vanish completely in the
   neutral state without any bypass branch of its own.
3. It contains **no oversampling**. `NonlinearStage` supplies that in FOUR
   COLOR, at a cost of 65 samples of latency and four oversamplers per band.

### Why `NonlinearStage` is not reused

EDGE is specified as minimum-latency. `NonlinearStage`'s equiripple FIR
oversamplers report a constant 65 samples, and its quality-switching machinery
exists for a user-facing Quality control EDGE does not have. EDGE wraps
`ColorEngine` in its own ~40-line stage with a *switchable* oversampling factor,
and let the measurement decide: **1× ships**, at −76.7 dBc of alias floor and
**zero latency**, because 2× bought only 8.5 dB for 3.14 samples and the loss of
a bit-exact bypass. Full numbers in `COLOUR-PLACEMENT.md`. The engine itself is untouched and is driven only through
its published API (`prepare` / `reset` / `setDrive` / `setContext` /
`processBlock`).

### Placement — measured, not assumed

| Placement | Verdict |
|---|---|
| **Pre-filter (chosen)** | harmonics generated before the filter are then *shaped by* the filter, so a low-pass really removes the fizz it created. Colour tracks the filter. |
| Inside the resonance/feedback path | **rejected on maths, not taste.** The TPT/ZDF section's stability and its cutoff-independent `Q` come from solving one instantaneous linear feedback equation. `ColorEngine` is not memoryless — `IronEngine` carries `lastOut` and a one-pole, `WarmEngine` a sag envelope — so it cannot be placed inside a zero-delay loop without an iterative solve, and the loop gain would become level-dependent, i.e. Resonance would change with input level and with cutoff. That is precisely the "Resonance must not jump in level when cutoff changes" requirement. |
| Post-filter | rejected, and **measured**: a 300 Hz cut leaks an intermodulation product at 100 Hz that is **46.6 dB louder** post-filter than pre-filter, and a 3 kHz cut leaks a harmonic that is **23.3 dB louder**. See `docs/COLOUR-PLACEMENT.md`. |

### The colour amount law

Nothing about the colour is exposed. Its drive is a pure function of how hard
the *filter* is working:

```
A_edge = max( 1 − √G_edge ,  resonance_edge )        per edge, ∈ [0,1]
A      = max( A_low , A_high )
drive% = 10 · A                                       into ColorEngine::setDrive
```

* `1 − √G` is 0 at Depth 0, 0.29 at −6 dB, 0.50 at −12 dB, 0.75 at −24 dB,
  0.94 at −48 dB — a smooth, bounded, monotone read of "how much shelf".
* It is a function of the **parameter state only**. No envelope follower, no
  level detector, no auto-drive: pumping is impossible because there is nothing
  time-varying in the law except the parameters, which are already smoothed.
* The resonance terms are `EdgeUnit::resonanceMakeup()`, not the raw Resonance
  controls. Raw Resonance was wrong: at Depth 0 a section is a wire whatever its
  Q is, so Resonance did nothing audible while still engaging the colour — and
  the colour's internal DC blocker then took 0.97 dB off 20 Hz for no reason.
* 10 % of `WarmEngine`'s 24 dB range is **2.4 dB of pre-gain at absolute
  maximum**. This keeps EDGE a filter. 14 % was tried first and measured 1.0 dB
  of level movement across a −60…−18 dBFS input range; 10 % measures 0.95 dB
  worst case and 0.00 dB at the calibration level.
* The engine's own engage window (drive < 5 %) means colour only starts to
  appear above `A ≈ 0.5`, i.e. around −12 dB of Depth. Below that the plug-in is
  linear.
* A is smoothed at 30 ms so a fast Depth automation ramps the colour rather than
  stepping it.

Calibration: `ColorEngine::updateCompensation` already normalises the curve
around a −12 dBFS sine, which is the work order's "calibrate around a sensible
working level (~−18 dBFS) without assuming every signal arrives at that level" —
it is a *static* make-up derived from the curve, not a level measurement of the
incoming audio, so a hot input simply saturates more, as it should.

---

## 7. Why TPT/ZDF and not a biquad

Both would give the same steady-state magnitude. The difference is entirely
about what happens *while the cutoff moves*, which is EDGE's normal operating
condition — the work order requires fast frequency automation to sound good.

* A Direct-Form biquad's state variables are **past inputs and outputs**. Its
  coefficients only describe the intended response if they have been constant
  for the length of the filter's memory. Change them per block and the stored
  `y[n-1]`, `y[n-2]` are interpreted under the new coefficients, which produces
  a level/phase discontinuity at every block boundary — the classic "zipper on
  a filter sweep" — and at high `Q` and low cutoff can transiently blow up.
* A TPT SVF's state variables are **integrator states**, i.e. the physical state
  of the analogue prototype. They stay meaningful when `g` changes: the filter
  behaves like an analogue filter whose knob is being turned. Coefficient
  updates are safe at block rate and even per sample.
* TPT prewarps by construction (`g = tan(π f/fs)`), so the corner is exact at
  every sample rate with no bilinear-transform frequency error near Nyquist.
  A biquad shelf would need explicit prewarping plus coefficient smoothing plus
  a stability check over the whole parameter range — three extra mechanisms to
  get to the same place.
* The state is per channel and the coefficients are shared, which is exactly the
  stereo-linked / independent-state requirement, and is the seam where Mid/Side
  can be added later without touching the section.

Cutoff `g` is smoothed **in the log-frequency domain** at 12 ms so the sweep is
perceptually even, and `G`, `b`, `k` are smoothed alongside it.

EDGE is minimum-phase throughout. Latency is the oversampler's only.

---

## 8. Signal flow

```
in ─┬─────────────────────────────── dry (for the colour gate only) ───┐
    │                                                                  │
    └─▶ COLOUR  2× IIR half-band ─▶ WarmEngine ─▶ ↓2 ──────────────────┴─▶ gate ─┐
                                                                                 │
   ┌─────────────────────────────────────────────────────────────────────────────┘
   │
   ├─▶ LOW EDGE   section0(res) ─▶ section1 ─▶ section2      (shelf → HP)
   │
   ├─▶ HIGH EDGE  section0(res) ─▶ section1 ─▶ section2      (shelf → LP)
   │
   └─▶ Output trim ─▶ out
```

Bypass is a 10 ms equal-gain fade to a latency-matched dry path, so it cannot
click and cannot shift timing.


---

## 9. Corrections made during stage 3–4, and what caught them

Recorded because each was a real defect that reasoning alone had not found.

| Found | Symptom | Cause | Fix |
|---|---|---|---|
| test 10 | 0.0142 of L/R divergence on identical input | `ColorEngine::engageSmoothed` is one member shared by all channels, advanced once per sample inside `blend()` | one engine instance per channel; the vendored file is untouched |
| test 5 | 1.79 dB response jump for a 0.5 % nudge of Curve | linear section weights have a kink in `dw/dP` at integer `P`, scaled by the full −120 dB | smoothstep weights (§4) |
| test 11 | engaging Depth changed passband level by 1.0 dB | the engine's make-up is RMS-matched at −12 dBFS, not unity at small signal | measure the engine's own gain at `prepare()` through a probe instance and cancel it; calibrate at −18 dBFS |
| test 6 | Resonance at Depth 0 took 0.97 dB off 20 Hz | the colour law used raw Resonance, which engaged the engine's DC blocker while the filter was still a wire | law now uses `resonanceMakeup`, which is gated by the section's shelf gain |
| test 12 | bypass left a 6e−8 residue against the source | `wet + 1.0f*(dry-wet)` is not `dry` in float | assign the endpoints instead of interpolating to them |
| test 2 | drawn curve 0.45 dB off the measurement at 30 Hz | the colour engine's internal 10 Hz DC blocker is real and was not drawn | model it in `magnitudeDb`; agreement is now 0.007–0.114 dB |

Two of those were **measurement** errors rather than DSP errors, and both looked
exactly like DSP errors first: a "−66 dB cut" that was really −104 dB of
fundamental hiding under its own distortion products (fixed by measuring one
bin instead of broadband RMS), and a "Depth 92 % is 3 dB out" that was a
second-order shelf still on its way to its asymptote four octaves down.


---

## 10. The slope selector, and the slopes that do not exist

The display carries a combo box per edge that snaps Curve to a whole dB/oct. It
writes the same parameter the knob writes; there is no second parameter.

It offers **SOFT / 12 / 24 / 36 dB/oct** and nothing else, because those are the
only slopes the cascade actually delivers. At full Depth every section with a
non-zero share of the dB becomes a full second-order cut, so the asymptotic
slope is `12 × (active sections)` — an integer. Fractional `P` distributes the
*depth* between sections, not the *order*.

Measured, one octave below the corner (Fc/2 to Fc/4, chosen so 36 dB/oct has not
yet reached the −120 dB floor):

| Curve | claimed | measured |
|---|---|---|
| 50 % | 12 dB/oct | 11.8 |
| 62.5 % | *18 dB/oct* | **23.5** ← would have been a lie |
| 75 % | 24 dB/oct | 23.6 |
| 87.5 % | *30 dB/oct* | **32.6** ← same |
| 100 % | 36 dB/oct | 35.1 |

6, 18 and 30 dB/oct need one more section of **first** order in the cascade.
That is a small, real feature, and the reason it is not in this build is not the
maths but the control law: a one-pole stage that switches in and out with the
parity of the order is a topology change on a parameter that can be automated,
and everything else in EDGE is built so that no control can do that.

The first measurement of this test probed at 30–60 Hz and reported 36 dB/oct as
**1.8 dB/oct** — the response had already flattened onto the −120 dB floor
(at Fc = 1 kHz and 36 dB/oct that floor arrives at 99 Hz). Another instance of
the recurring trap: the number looked like a DSP finding and was the
measurement.
