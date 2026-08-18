# EDGE — user manual

*For v0.18. An autofilter with a destination.*

**EDGE — draw the destination.** By Gussa Naaman.

---

## The one idea

**Draw the destination. EDGE travels there. Your sound pushes it.**

LOW and HIGH define where the filter is going. The big **EDGE** knob travels
from CLEAN towards that destination — each spectral edge moving continuously
from a gentle shelf, through deeper attenuation, into a real cut. It is the
same filter the whole way: nothing switches, nothing crossfades, nothing
clicks. And **FOLLOW → EDGE** lets the signal itself drive that travel.

At **EDGE 0 the plug-in is a bit-exact wire** — output equals input, sample for
sample, whatever else is set — and it adds **zero latency**. Leave it on a
track and forget it is there.

---

## Ten minutes with it

1. Load EDGE on a loop. Turn **EDGE** up slowly and watch the puck ride the
   **EDGE PATH** towards the target diamond. That line is the whole product.
2. Grab the **HIGH** handle on the graph and drag it where you want the cut.
   The inspector opens at the bottom of the graph with that edge's controls.
3. Pick a slope: **12 | 24 | 36 | 48 | 72** dB/oct. Turn **RESO** up.
4. Turn **FOLLOW → EDGE** to +60 and play something with transients. The
   filter opens and closes with the music — the violet marker on the EDGE knob
   shows where the sound is pushing it.
5. Press **FREE**. Drag the band as one shape across the spectrum.
6. Raise **BITE**. The colour arrives with the depth, not with the knob.

---

## The window

```
[ PRESET browser ]        E D G E        [ BITE ] [ WARM ] [ BYPASS ]
[                    the graph                                      ]
[   LOW        EDGE (CLEAN <-> CUT)        HIGH      FOLLOW -> EDGE ]
```

### Header

| control | function |
|---|---|
| **PRESET** | 23 factory programs, with prev/next arrows |
| **BITE** | 0–100 %. Hidden colour. `drive = maxDrive(BITE) · activity^γ` — a shelf gets a little, a full cut gets the ceiling |
| **WARM / IRON** | the colour voicing: a soft sag saturator, or the same inside a feedback core-loss loop — denser, transformer-ish |
| **BYPASS** | a bit-exact dry path, not a muted wet one |

### The graph

The visual centre of the product. Everything on it is live engine state:

* **MODE** — `LP | BAND | HP | FREE`, floating at the top.
* **Handles** — amber LOW, cyan HIGH, neutral MID. Drag horizontally for
  frequency; vertically for depth (edges) or gain (MID). In LP the low handle
  is gone entirely — that edge is a wire, and a control that provably does
  nothing is not shown.
* **EDGE PATH** — the rail from the neutral boundary to the target diamond.
  The filled puck rides the **live** corner: turn EDGE, watch it travel; use
  FOLLOW, watch the signal move it.
* **The readout** — bottom left, always present: `LP • 3.2 kHz`, `LOW • 140 Hz
  HIGH • 6 kHz`. The identity you chose never disappears.
* **The analyzer** — the **incoming** signal, before the filter touches it, on
  its own dBFS scale. What you see is what you are shaping, even through a
  closed cut. It fades out below −66 dBFS and is gone by −84.

### The inspector

One strip, one fixed place: centred, near the bottom of the graph.

* Touch **LOW**, **HIGH** or **MID** on the graph — or the **FOLLOW → EDGE**
  knob — and the strip shows that context's controls.
* Click empty graph space, or press **Escape**, and it goes away.
* The graph stays fully draggable while it is open.

| context | controls |
|---|---|
| **LOW / HP** | DEPTH · **SLOPE** · RESO · SHOULDER |
| **HIGH / LP** | DEPTH · **SLOPE** · RESO · SHOULDER |
| **MID** | FREQUENCY · GAIN · WIDTH |
| **FOLLOW → EDGE** | AMOUNT · ATTACK · RELEASE · SENS |

In LP the strip's label says **LP** — the plug-in never shows you an internal
name.

### The macro deck

| control | function |
|---|---|
| **LOW** | the low edge's destination, 20 Hz – 8 kHz |
| **EDGE** | CLEAN → CUT. The pointer is your setting; the violet outer marker is where FOLLOW has actually pushed it right now |
| **HIGH** | the high edge's destination, 200 Hz – 20 kHz |
| **FOLLOW → EDGE** | −100…+100 %. Positive: louder = deeper. Negative: louder = opens up. At exactly 0 it is bit-identical to off |

---

## The controls in depth

### DEPTH — 0 dB → CUT

How far down that side goes at full EDGE: a shelf whose gain degenerates
smoothly into a cut. There is no mode switch anywhere along it.

### SLOPE — 12 | 24 | 36 | 48 | 72 dB/oct

Five musical choices; click, scroll or use the arrow keys. Each activates
exactly that many filter sections (12 = one second-order section … 72 = six).

Honesty note: the steepest choices cannot fully develop before the response
reaches its −132 dB floor — a slope needs an octave of room to fall 72 dB. The
measured steepest slopes are 12.0 / 24.1 / 35.7 / 46.0 / 61.4 dB/oct. The six
sections behind the 72 choice are real; the finite-band measurement is what it
is, and we publish it rather than round it up.

Old projects saved with in-between values keep their exact sound; the strip
just highlights the nearest choice until you deliberately pick one.

### RESO

Lifts the corner without compounding through the cascade (first section only).
Silent at DEPTH 0 by construction.

### SHOULDER — OFF → −12 dB

A second, much gentler shelf six octaves into the passband, so the whole
passing side leans towards the cut — and the lean travels with the cutoff.
This is what makes an LP at 9 kHz also calm everything below it.

### MID — FREQUENCY · GAIN · WIDTH

A movable bell inside whatever the edges allow. ±18 dB, 60 Hz – 12 kHz, from a
broad tilt to a formant. **It stays where you put it** — EDGE scales its gain
but never moves its frequency. At 0 dB it is a bit-exact wire.

### FOLLOW setup — AMOUNT · ATTACK · RELEASE · SENS

The detector reaches full modulation at the SENS level and zero 24 dB below
it. In FREE mode, FOLLOW moves the band's **centre** (±2 octaves) instead of
the depth.

### SPREAD and OUTPUT

**SPREAD** (stereo width, ±12 semitones of per-channel corner offset) sits at
the far left of the deck; **OUTPUT** (±24 dB trim) lives in the header next to
BYPASS. Both are also host-automatable and set by several factory presets.

---

## Presets

23 programs in the header browser. **Program 0 is DEFAULT** — exactly the
neutral state. No preset ever touches BYPASS.

---

## Installing

The whole delivery is two files. Take the one for your machine.

### macOS — `EDGE-<version>-macOS.zip`

Unzip it and double-click `EDGE-<version>-macOS.pkg`. It asks which formats
you want — VST3, Audio Unit, Standalone — and each can be unticked. Universal
binaries, so they load natively on Apple Silicon and Intel and under Rosetta.

Not notarised: the first time you open the standalone, right-click it and
choose Open, then Open again. Plug-ins loaded by a DAW are unaffected.

`Uninstall EDGE.command` in the same zip removes everything, on a double-click.

### Windows — `EDGE-<version>-Windows-Setup.zip`

Close your DAW, unzip, and double-click `EDGE-<version>-windows.exe`. Tick
VST3 and/or the standalone. It installs to `C:\Program Files\Common
Files\VST3` and `C:\Program Files\Naaman\EDGE`, and it appears in Windows'
Apps list so it uninstalls like any other program.

Not code-signed: SmartScreen may show a blue panel — *More info*, then *Run
anyway*.

The same zip carries the raw `EDGE.vst3` folder, if you would rather copy it
into `C:\Program Files\Common Files\VST3\` by hand than run an installer.

That Windows build is made by GitHub Actions on a machine with Visual Studio
2022, and all 226 measurement checks must pass with Microsoft's compiler
before the installer is allowed to exist.

### Either way

In Cubase: Studio → VST Plug-in Manager → Update. EDGE is under **Naaman**, in
the **Filter** category.

### Building it yourself

The source is at <https://github.com/gusanaaman1-boop/edge-plugin>.
`INSTALL-EDGE.bat` builds and installs on Windows, `RUN-EDGE-TESTS.bat` runs
the 226 checks with your own compiler, and `MAKE-INSTALLER.bat` rebuilds the
installer locally. All three need Visual Studio 2022 (free).

---

## If something goes wrong

EDGE writes one line per session — version, build, host, sample rate, block
size — to a local log you can paste into an email. Nothing is ever sent
anywhere.

* macOS: `~/Library/Logs/EDGE/EDGE.log`
* Windows: `%APPDATA%\EDGE\EDGE.log`

---

## What EDGE deliberately is not

No LFO, no step sequencer, no drawable modulation, no randomiser. One envelope
follower — FOLLOW — and that is the whole modulation story. No hidden
compressor or loudness rider. No exposed saturation controls beyond BITE and
the WARM/IRON switch.

It is a filter you play.
