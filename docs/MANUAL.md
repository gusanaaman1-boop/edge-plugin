# EDGE — user manual

A two-sided filter for electronic music. VST3, AU and Standalone.

---

## The one idea

Every filter you have used has a shelf mode and a cut mode, and a switch between
them. EDGE has neither. **Each spectral edge moves continuously from a gentle
shelf, through deeper attenuation, to a real cut — and it is the same filter the
whole way.** Nothing switches in, nothing crossfades, so nothing clicks.

One large control, **EDGE**, drives that journey from both sides at once. And the
signal itself can drive it, through **FOLLOW**.

At **EDGE 0 the plug-in is a bit-exact wire** — the output is the input, sample
for sample, whatever else is set. It also adds **zero latency**, so you can leave
it on a track and forget about it.

---

## Ten minutes with it

1. Load EDGE. Turn **EDGE** up slowly. Both corners walk inwards from the edges
   of the spectrum and the band closes around your sound.
2. Press **SHAPE**. Set **Low Depth** to about −6 dB. Turn EDGE up again — now
   the low side leans instead of cutting. That is the same filter.
3. Set **High Curve** to `24 dB/oct` with the combo on the display. Turn
   **High Reso** up. Now it is the filter you expected.
4. Put **FOLLOW** at +70. Play something with transients. The filter opens and
   closes with the music.
5. Press **FREE**. Drag the band on the display. It travels as one shape and
   keeps its width.
6. Turn **BITE** up. The colour arrives with the depth, not with the knob.

---

## The front panel

### EDGE

**0–100 %.** The master control, and the one you play.

At 0 the plug-in does nothing at all, bit for bit. Opening it walks both corners
geometrically from the boundaries of their ranges to the frequencies you set in
SHAPE, and scales Depth, Shoulder, Resonance and the MID bell's gain from
nothing to their targets. One gesture, both sides, everything.

In **FREE** mode EDGE stops moving the corners and becomes purely *how deep*.

### MODE — LP / BAND / HP / FREE

| | |
|---|---|
| **LP** | The low edge becomes an identity; the high edge is your low-pass. |
| **HP** | The mirror. |
| **BAND** | Both edges active. |
| **FREE** | Both active, but the corners do **not** travel with EDGE. The band is draggable as a unit on the display, and FOLLOW moves its **centre** by up to ±2 octaves instead of driving EDGE's position. |

Switching modes is silent by construction: a disabled edge has its gains driven
to 1, which is already a bit-exact wire, so there is nothing to fade.

### FOLLOW

**−100 … +100 %.** An envelope follower on the input, modulating EDGE's position
— or, in FREE, the band's centre.

Positive drives further into the cut when the signal gets loud; negative opens
up. It scales by the headroom in the direction of travel, so ±100 % feel
balanced and the control can never clamp against an end.

At exactly 0 it is bit-identical to the follower being switched off.

Its **Sensitivity**, **Attack** and **Release** live in SHAPE.

### SPREAD

**−100 … +100 %.** Offsets the two channels' corners in opposite directions,
in octaves. **Both corners of a channel move together**, so each channel keeps
its bandwidth — this widens, it does not detune the shape. ±12 semitones total
at full.

At 0 the two channels null bit-exactly.

### BITE and CHARACTER

**BITE 0–100 %, default 35.** How much hidden colour. There is no drive, no mix,
no bias and no oversampling control, because the amount of colour is not a
separate decision from how hard the filter is working:

```
drive = maxDrive(BITE) · activity ^ gamma(BITE)
```

`activity` is what the filter is actually doing. A shelf gets a little, a full
cut gets the ceiling. The lamp beside CHARACTER lights when the engine is
genuinely engaged — not merely when BITE is above zero.

**CHARACTER — WARM or IRON.**

* **WARM** — a soft saturator with a slow sag envelope. Round, fat, gently
  compressing.
* **IRON** — the same saturator inside a feedback loop whose return is
  low-passed, so what it does depends on what just happened. Denser,
  transformer-ish.

Their ceilings differ (24 % and 46 %) because each one's ceiling is set by its
own measured aliasing, not by taste. Switching is a 20 ms crossfade between two
engines that are both always running.

### OUTPUT and BYPASS

Trim, ±24 dB. BYPASS is a bit-exact dry path, not a muted wet one.

---

## SHAPE — where the edges are going

Press **SHAPE**. Nothing in here is played; it is set once per sound.

**There is one set of knobs, not four.** Pick **LOW**, **MID**, **HIGH** or
**FOLLOW** and the row underneath drives that band. Touching a handle on the
display picks it too — and opens SHAPE if it was closed — so the thing you just
grabbed is the thing the knobs are controlling. The selected handle is filled
rather than merely outlined, so one glance answers "what am I editing?".

It used to show sixteen knobs in four labelled blocks. Nobody needs DEPTH for
the low edge and DEPTH for the high edge on screen at the same time; they need
DEPTH for the edge they are working on. The panel is now half the height, and
the display keeps the difference.

### Frequency

Where that edge is heading. Low 20 Hz – 8 kHz, high 200 Hz – 20 kHz, both
logarithmic. Drag the handle on the display instead if you prefer — it is the
same control.

### Depth — 0 dB → CUT

How far down that side goes. This is the control that makes EDGE what it is:
a shelf whose gain degenerates smoothly into a cut. There is no mode change
anywhere along it.

### Curve

Two things in one control, split at the middle:

* **the soft half** widens the knee — gentler, more "hi-fi";
* **the tight half** adds pole pairs — steeper, more "synth".

The combo on the display snaps it to a named slope. The names are **pole
counts**, which is what the structure actually has:

| combo | poles | measured steepest slope |
|---|---|---|
| SOFT | — | a knee, not a slope |
| 12 dB/oct | 2 | 12.0 dB/oct |
| 24 dB/oct | 4 | 24.1 |
| 36 dB/oct | 6 | 35.7 |
| 48 dB/oct | 8 | 46.0 |
| 72 dB/oct | 12 | 61.4 |

The steep entries fall short of their asymptote and that is structural, not a
defect: a slope can only develop over the depth there is to fall through, and
72 dB/oct needs a whole octave to drop 72 dB. The measured numbers are published
rather than rounded up.

Between two entries Curve is a voicing, and reads as a percentage rather than
claiming a slope it does not deliver.

### Shoulder — OFF → −12 dB

A second, much gentler shelf **six octaves into the passband** from the cut's
corner. It leans the whole passing side down towards the corner, and the lean
**travels with the cutoff**.

This is what makes a high-pass at 9 kHz also thin out everything below it,
instead of leaving the passband flat up to the corner.

### Resonance

Lowers the damping of the first section only, so it lifts the corner without
compounding through the cascade. It is silent at Depth 0 by construction — there
is nothing to resonate when the section is a wire.

It weakens as Curve tightens (+7.8 dB at SOFT, +2.8 dB at the tight end),
because the other sections keep falling through the peak.

### MID Freq / Gain / Reso

A movable bell inside whatever the edges let through. ±18 dB, 60 Hz – 12 kHz,
from a broad 4.1-octave tilt down to a 0.7-octave formant.

**It stays where you put it.** EDGE scales its gain and leaves its frequency
alone — a control you place has to stay placed. It travels only with SPREAD and
FREE, which move everything at once so the shape stays one shape.

At 0 dB it is a bit-exact wire and costs nothing.

### Follow Sens / Attack / Release

The detector's operating level and timing. Sensitivity is the input level at
which FOLLOW reaches full modulation; it reaches zero 24 dB below that.

---

## The display

* **The solid curve** is what the plug-in is doing right now.
* **The dashed curve** is where EDGE at 100 % would take it.
* **The two faint traces** appear when SPREAD is up: the left and right
  channels.
* **The handles** are TARGET handles. Drag them. A handle grows a tick on the
  solid curve when the two have pulled apart, and the **selected** one — the
  band SHAPE's knobs are driving — is filled in.
* **An edge that MODE has turned into an identity has no handle at all.** In LP
  the low edge is a wire, so there is nothing there to drag. A dimmed handle
  sitting at 0.0 dB only invited the question "why does moving this do
  nothing?".
* **The read-out** appears in the top-left of the plot while you touch a
  handle, not floating over the handle itself where it used to cover the label.
* **The analyser** behind everything is the plug-in's output.
* **The combo** snaps Curve to a named slope.

The solid curve and the handles are drawn from the same shape, always — so a
handle is never somewhere the curve is not.

---

## Presets

23 factory presets, exposed as host programs, so your DAW's own preset menu
lists them. **Program 0 is DEFAULT** and is exactly the plug-in's neutral state.

No preset ever touches **Bypass** — auditioning presets on a bypassed insert
will not silently un-bypass it.

See [PARAMETER-TABLE.md](PARAMETER-TABLE.md) for the full list, generated from
the plug-in itself.

---

## Installing

**macOS.** The zip contains three folders. Copy:

| | |
|---|---|
| `EDGE.vst3` | `/Library/Audio/Plug-Ins/VST3/` |
| `EDGE.component` | `/Library/Audio/Plug-Ins/Components/` |
| `EDGE.app` | `/Applications/` |

The build is **not signed or notarised yet**, so macOS will refuse it the first
time. Right-click → Open, or System Settings → Privacy & Security → "Open
Anyway".

**Windows.** The download is source plus a build script, because there is no
Windows machine here to build on and shipping an untested binary would be worse
than shipping none. Install CMake, Git and Visual Studio with the "Desktop
development with C++" workload, then run `build-windows.bat`. It clones JUCE at
a pinned commit, builds, **runs both test suites**, and only then offers to
install. If any check fails it stops and installs nothing.

In Cubase: Studio → VST Plug-in Manager → rescan. EDGE appears under **Filter**.

---

## If something goes wrong

The bottom right of the window shows the exact build. EDGE also appends one line
per session to a log file — version, build, host, sample rate, block size:

* macOS: `~/Library/Logs/EDGE/EDGE.log`
* Windows: `%APPDATA%\EDGE\EDGE.log`

Nothing is sent anywhere. The file exists so you can paste it.

---

## What EDGE deliberately is not

No LFO, no step sequencer, no drawable modulation, no randomiser. One envelope
follower, and it is FOLLOW. No hidden compressor, dynamic EQ or loudness rider.
No exposed saturation controls beyond BITE and CHARACTER.

It is a filter you play.
