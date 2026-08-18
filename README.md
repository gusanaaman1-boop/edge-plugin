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

On Windows, `INSTALL-EDGE.bat` does all of the above in one step — it finds
CMake inside Visual Studio rather than demanding it on PATH, names the
generator explicitly, and uses the shared `%USERPROFILE%\JUCE`.
`RUN-EDGE-TESTS.bat` runs both suites; `MAKE-INSTALLER.bat` packs the build
into a redistributable `.exe`.

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

## Installing

The delivery is **two files**, one per platform.

| | |
|---|---|
| `EDGE-<ver>-macOS.zip` | `.pkg` installer (VST3 / AU / Standalone, each optional), manual, parameter table, double-clickable uninstaller |
| `EDGE-<ver>-Windows-Setup.zip` | Inno Setup `.exe`, the raw `EDGE.vst3` for hand placement, manual |

macOS binaries are universal (Apple Silicon + Intel). Neither package is
code-signed: on macOS the standalone needs right-click → Open once, and on
Windows SmartScreen wants *More info* → *Run anyway*. Plug-ins loaded by a DAW
are unaffected on both.

In Cubase: Studio → VST Plug-in Manager → Update. EDGE is under **Naaman**, in
the **Filter** category.

Full instructions: [docs/MANUAL.md](docs/MANUAL.md).

## Packaging

```bash
./packaging/fetch-windows-ci.sh     # Windows binaries from the last green CI run
./packaging/make-packages.sh        # both zips, then verified by re-opening them
```

There is no Windows compiler on the Mac this is developed on, so the Windows
half is built by [GitHub Actions](.github/workflows/windows.yml) with Visual
Studio 2022 — all 226 checks must pass with MSVC before the installer is
allowed to exist. `fetch-windows-ci.sh` refuses binaries from a run that built
different code than the tree.

`make-packages.sh` verifies what it produced rather than trusting it: it
re-opens both zips, checks all three macOS components carry a payload and
install to the path their format is loaded from, checks the Windows installer
is not an empty wizard, and asserts `dist/` holds exactly two files.

## Status

**v0.18, complete and measured.**

| | |
|---|---|
| checks | 93 signal + 133 host-contract, green on macOS/clang **and** Windows/MSVC |
| sanitisers | ASan + UBSan clean, zero diagnostics |
| `auval` | PASS |
| pluginval | SUCCESS at strictness 10, VST3 and AU |
| Windows installer | installed and uninstalled on a real Windows machine by CI |
| neutral path | `max|out−in| = 0` exactly |
| latency | 0 samples |
| audio-thread allocations | 0 in 10,000 blocks |
| throughput | ~255× realtime stereo with the analyser feeding |
| presets | 23 factory programs |
| hosts | confirmed working in Cubase 15 by the owner |

Not done: **code signing and notarisation**, which need paid certificates on
both platforms. The only consequence is one extra click the first time —
right-click → Open on macOS, *More info* → *Run anyway* on Windows — and
plug-ins loaded inside a DAW are unaffected either way.
