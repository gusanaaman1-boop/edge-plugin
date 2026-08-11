# EDGE

A two-sided musical filter for electronic music production. VST3 / AU /
Standalone, JUCE 9, targeting Cubase 15.

One idea: **each edge of the spectrum moves continuously from a gentle shelf all
the way to a real cut**, and it is the same filter the whole way — not a shelf
that gets swapped for a high-pass at some threshold. You are never forced to
delete your low end to shape it.

```
LOW EDGE            HIGH EDGE           SHARED
  Frequency           Frequency           Link
  Depth               Depth               Focus
  Curve               Curve               Output
  Shoulder            Shoulder            Bypass
  Resonance           Resonance
```

Each edge also carries a **slope selector on the display** (SOFT / 12 / 24 /
36 dB/oct) that snaps the Curve control to a whole slope, and a **Shoulder** that
leans the entire passband down towards the corner and travels with it.

No LFO. No sequencer. No envelope follower. No saturation panel. The colour is
inside the filter's character and has no controls, because it is not a
distortion unit.

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

79 checks. Every one prints the number it measured. Exit code 0 means all
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
  Core/          parameter IDs, control laws, the APVTS layout
  Dsp/
    MorphSvf.h   the section: one TPT SVF recombined into shelf-or-cut
    EdgeUnit.h   three sections = one edge, and Curve's distribution law
    ColorStage   the hidden colour, wrapping the vendored engine
    EdgeEngine   the whole path, smoothing, Focus, response evaluation
  Ui/            Theme (the only place colours live), CurveView
  Tools/         test_dsp.cpp, screenshot_tool.cpp
  Vendor/        FOUR COLOR's saturation engine, byte-identical - DO NOT EDIT
docs/
  DSP-TOPOLOGY.md      why this filter and not a biquad, with the proofs
  SATURATION-AUDIT.md  what the existing engine is and how to use it safely
  COLOUR-PLACEMENT.md  pre vs post vs feedback, measured
  PARAMETERS.md        the frozen IDs and every control law
```

## Status

MVP complete and measured on macOS. Not yet verified in Cubase, and not yet
built on Windows — see the "remaining" section of the session report.
