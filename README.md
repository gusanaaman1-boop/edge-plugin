# EDGE

A two-sided musical filter for electronic music production. VST3 / AU /
Standalone, JUCE 9, targeting Cubase 15.

One idea: **each edge of the spectrum moves continuously from a gentle shelf all
the way to a real cut**, and it is the same filter the whole way — not a shelf
that gets swapped for a high-pass at some threshold. You are never forced to
delete your low end to shape it.

**EDGE morphs from shaping to cutting — and the sound itself can move it.**

```
PERFORMANCE                 TARGETS (inside SHAPE)
  Mode   LP/BAND/HP/FREE      Low / High  Frequency
  EDGE   the whole morph                  Depth
  FOLLOW the sound moves it               Curve
  SPREAD stereo movement                  Shoulder
  BITE   hidden colour macro               Resonance
  CHARACTER  WARM / IRON            MID   Freq / Gain / Reso
  Output, Bypass                          Follow Sens / Attack / Release
```

One large control drives both spectral edges from open, through shelf, to a real
cut. FOLLOW lets the signal drive it. SPREAD moves the two channels apart in
semitones without changing either one's bandwidth. FREE turns the pair into a
band you can drag across the spectrum, and points FOLLOW at its centre instead.
A movable MID bell rides inside whatever the two edges let through, and travels
with them.

No LFO. No sequencer. No drawable modulation. No randomisation. One envelope
follower, and one hidden colour engine whose only public control is BITE.

At EDGE 0 the plug-in is **bit-exact and zero-latency**, whatever else is set.

## Building

JUCE 9 is expected at `~/JUCE` (the same checkout the rest of this workspace
uses). CMake 3.22+, a C++20 compiler.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build --target Edge_All -j8
```

Artefacts land in `build/Edge_artefacts/Release/` as `VST3/EDGE.vst3`,
`AU/EDGE.component` and `Standalone/EDGE.app`. `EDGE_COPY_AFTER_BUILD` is ON by
default and installs the VST3 and AU into the user plug-in folders; pass
`-DEDGE_COPY_AFTER_BUILD=OFF` to keep them in the build tree.

macOS builds universal (arm64 + x86_64) by default, matching the rest of the
workspace. Add `-DCMAKE_OSX_ARCHITECTURES=arm64` for a faster local build.

### The measurement suite

```bash
cmake --build build --target EdgeTests -j8 && ./build/EdgeTests_artefacts/Release/EdgeTests
```

93 checks. Every one prints the number it measured. Exit code 0 means all
passed; the last run is kept in `docs/TEST-RESULTS.txt`.

### Sanitisers

```bash
cmake -B build-san -DEDGE_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug && cmake --build build-san --target EdgeTestsSan -j8 && ./build-san/EdgeTestsSan_artefacts/Debug/EdgeTestsSan
```

### UI screenshots

```bash
cmake --build build --target EdgeShot -j8 && ./build/EdgeShot_artefacts/Release/EdgeShot ui-shots
```

Renders the real editor over the real processor to PNG — no window server, no
screen capture, deterministic.

## Layout

```
Source/
  Core/          parameter IDs, control laws, the APVTS layout, v1->v2 migration
  Dsp/
    MorphSvf.h   the section: one TPT SVF recombined into shelf-or-cut
    EdgeUnit.h   three sections = one edge, and Curve's distribution law
    FollowDetector  the one envelope follower, and FOLLOW's mapping
    ColorStage   the hidden colour, wrapping the vendored engine
    EdgeEngine   the whole path, smoothing, Focus, response evaluation
  Ui/            Theme (the only place colours live), CurveView, ShapePanel
  Tools/         test_dsp.cpp, screenshot_tool.cpp
  Vendor/        FOUR COLOR's saturation engine, byte-identical - DO NOT EDIT
docs/
  DSP-TOPOLOGY.md      why this filter and not a biquad, with the proofs
  SATURATION-AUDIT.md  what the existing engine is and how to use it safely
  COLOUR-PLACEMENT.md  pre vs post vs feedback, measured
  PARAMETERS.md        the frozen IDs and every control law
```

## Packaging

```bash
./packaging/make-packages.sh v0.4
```

Produces `dist/EDGE-<ver>-macOS.zip` (universal VST3 + AU + Standalone, with
install notes) and `dist/EDGE-<ver>-windows-src.zip` (130 KB of source and a
`.bat` that clones JUCE at a pinned commit and builds the VST3 itself). There is
no cross-compiler here, so Windows ships as source that builds itself; the
script verifies every macOS binary is genuinely universal before zipping.

## Status

v0.5.1 complete and measured on macOS: 93 checks green, `auval` PASS, ASan+UBSan
clean, 0 heap allocations in 10,000 audio blocks, ~458x realtime with the
analyser feeding. v0.1 ran in Cubase; v0.2 has not been re-checked there yet,
and there is still no Windows build, no presets and no installer.
