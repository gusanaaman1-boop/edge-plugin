# Stage 1 — audit of the project's existing saturation engine

## Where it is

The workspace holds several nonlinearities. Only one is a general-purpose,
reusable saturation **engine**; the rest are inline shapers owned by their
plug-ins.

| Location | What it is | Reusable? |
|---|---|---|
| **`FourColor/Source/Dsp/ColorEngine.{h,cpp}`** | **four polymorphic engine classes behind one base with a documented lifecycle** | **yes — this is the engine** |
| `FourColor/Source/Dsp/NonlinearStage.{h,cpp}` | oversampling / quality / crossfade wrapper around the above | partially, see below |
| `TRIX/Source/Dsp/Modules.h` (`struct Drive`) | five static `shapeSample` curves + a 2× IIR wrapper | no — a `switch` on an int, no state contract |
| `CoreColor/Source/DSP/NonlinearMixer.h` | one `tanh(g·x)/g^0.85` inside a synth voice | no |
| `CoreColor/Source/DSP/Effects/ColorEngine.h` | six *bass-specific* colour engines, split at 130 Hz | no — assumes a bass source |
| `BuildMe2/FxChain.cpp`, `Riser/FxChain.h`, `DualSpace/SmartComp.cpp`, `TRIX/ReverbV3.h` | one-line `tanh` limiters inside other effects | no |

EDGE uses **`fourcolor::ColorEngine`**, specifically the **WARM** engine.
It is vendored byte-identical into `Source/Vendor/FourColor/` — see
`Source/Vendor/VENDOR.md` for why a copy rather than an include path.

## How it works

`ColorEngine` is an abstract base with a fixed contract, and four concrete
structures (`WARM`, `IRON`, `BITE`, `FUZZ`) that are genuinely different
topologies rather than one waveshaper with four constant sets.

### Inputs and outputs

```cpp
void  prepare (double sampleRate, int numChannels);   // allocates, not RT-safe
void  reset();
void  setDrive (float drivePercent);                  // 0..100, block rate, RT-safe
void  setContext (const ColorContext&);               // block rate, RT-safe, never resets
void  processBlock (float* data, int numSamples, int channel,
                    const float* preGainMod  = nullptr,
                    const float* residualMod = nullptr) noexcept;   // IN PLACE
float getCompensationGain() const;
float getEngageTarget() const;
```

* **In place, one channel per call**, `float*`, mono buffer plus a channel index
  that selects which internal state block to use.
* `ColorContext` carries `bandIndex`, `oversampledRate`, `bandLowHz`,
  `bandHighHz`, `centreHz`. It is *internal state, not a parameter* — no ID, no
  automation, nothing saved. `WarmEngine` does not override `contextChanged()`,
  so it ignores everything except the rate; EDGE fills it in truthfully anyway.
* The two optional modulation curves are per-sample factors around 1.0. EDGE
  passes `nullptr` for both: those exist for FOUR COLOR's Behavior detector,
  which is exactly the "auto drive that follows an envelope" EDGE is forbidden
  to have.

### Stateful or memoryless

**Stateful, all four of them.** `WarmEngine` specifically carries, per channel:

* a **sag envelope** — a one-pole at 1.3 Hz (~120 ms) tracking `|x·g|`, which
  eases the drive on sustained loud material by up to `0.35 · drive01`;
* a **DC blocker** — `fourcolor::dsp::DcBlocker`, a first-order high-pass at
  10 Hz;
* a **drive-engage smoother** — `juce::SmoothedValue` over 10 ms.

Two consequences EDGE had to design around:

1. The sag is program-dependent gain. At EDGE's maximum drive (10 %) its depth
   is `0.35 × 0.10 = 3.5 %`, i.e. 0.3 dB moving over 120 ms. That is below the
   "no pumping" bar, and it is the reason EDGE caps the drive rather than
   trusting the engine to stay polite.
2. **`engageSmoothed` is one member shared by every channel**, advanced once per
   sample inside `blend()`. Processing L then R through one instance therefore
   hands the two channels different points on the engage ramp. Measured on
   identical L and R input while Depth moved: **0.0142 of divergence** — a
   wandering stereo image. EDGE gives each channel **its own engine instance**
   rather than modifying the vendored file. (`ColorStage.h` documents this at
   the member declaration.)

### Does it create DC

**Yes.** `WarmEngine` is deliberately asymmetric: `bias = 0.12 · drive01` is
added before the shaper and `rationalSoft(bias)` subtracted after, which cancels
the DC at zero signal but not with a signal present. **It already contains its
own DC blocker**, applied inside the wet leg of `blend()`, so EDGE adds nothing.

That blocker is not invisible: once the engine engages, the whole signal passes
a 10 Hz first-order high-pass — **−0.46 dB at 30 Hz, −0.97 dB at 20 Hz**. EDGE
models it in the drawn response curve rather than pretending the curve is flat
down there (`EdgeShape::colourEngage` / `colourGain`, verified by test 2 at
0.007–0.114 dB agreement between drawn and measured).

### Does it already oversample

**No.** `ColorEngine` runs at whatever rate `prepare()` is given and derives
every time constant from it. Oversampling lives in the *wrapper*,
`NonlinearStage`, which pre-allocates four `juce::dsp::Oversampling` instances
(1×/2×/4×/8× equiripple FIR) to serve a user-facing Quality control, and reports
a **constant 65 samples** of latency so switching between them is safe.

EDGE has no Quality control and is specified as minimum-latency, so it does not
use `NonlinearStage`. It wraps `ColorEngine` in its own ~40-line `ColorStage`
with a *switchable* 1×/2× factor, and let the aliasing measurement decide:

| | alias floor | latency |
|---|---|---|
| **1× (shipped)** | **−76.7 dBc** | **0 samples** |
| 2× polyphase IIR | −85.2 dBc | 3.14 samples |

8.5 dB is far too small a gap for genuine aliasing from a 2.4 dB soft
saturator — oversampling would buy 20–40 dB. Most of the −76.7 dBc is WARM's sag
envelope amplitude-modulating the tone, which oversampling cannot touch. 1× is
therefore the right answer, and it is what keeps the neutral state and the
bypass path **bit-exact**.

### The property EDGE is actually built on

```cpp
float blend (float x, float wet, float residualGain = 1.0f)
{
    const float e = engageSmoothed.getNextValue();
    return x + e * residualGain * (wet * compensation - x);
}
```

At `drive == 0` this returns `x` **bit for bit** — no residual, no gate, no DC
filtering — and from 0 to 5 % drive the engine fades in on a smoothstep with
zero derivative at both ends. That contract is the reason EDGE's neutral state
needs no bypass branch of its own, and it is why WARM was chosen over the other
three (IRON's feedback loop, BITE's diode pair and FUZZ's gate are all wrong for
a filter's internal colour).

### The one thing that had to be corrected, not by editing the engine

`ColorEngine::updateCompensation()` matches the **RMS of a −12 dBFS sine**
through the curve. That is the right normalisation for a saturator with a Drive
knob, and the wrong one for a hidden colour: it leaves the small-signal gain
well above unity, so engaging Depth would quietly make the track louder.

EDGE measures the engine's own gain at `prepare()` — a probe instance, a 1 kHz
sine at **−18 dBFS** (the working level the work order names), 33 drive points —
and applies the reciprocal as part of its per-sample output gain. Measured
result across a 42 dB input-level range at full filter activity: **0.95 dB**
worst deviation, and **exactly 0.00 dB** at the calibration level. The engine
file is untouched.

## The safe way to integrate it

1. **One instance per channel.** Call `processBlock(data, n, 0)` on each.
2. `prepare()` on the audio setup path only; it allocates.
3. `setDrive()` from a **smoothed** value at block or sub-block rate. The curves
   are continuous in drive, so 32-sample steps stay click-free (asserted).
4. Never call `reset()` in response to a parameter change — it empties the sag
   envelope and the DC blocker, which is a dropout.
5. Do not add a DC blocker; it has one.
6. Do not use `preGainMod` / `residualMod` unless an envelope-following colour
   is actually wanted.
7. Drive it from **parameter state only** if the product forbids pumping.
8. Put it **before** the filters. Measured penalty for putting it after:
   **+46.6 dB** of intermodulation product below a 300 Hz cut, **+23.3 dB** of
   harmonic above a 3 kHz cut (`docs/COLOUR-PLACEMENT.md`).
