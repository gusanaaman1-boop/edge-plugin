# EDGE v0.13 — product specification

*2026-08-12 · build `v0.13` · every number below was measured by the test
suites, not estimated.*

---

## Identity

**An autofilter with a destination.** LOW and HIGH define where the filter is
going; **EDGE** travels there from CLEAN to CUT; **FOLLOW → EDGE** lets the
signal itself push it along the same path. Each spectral edge morphs
continuously from a gentle shelf, through deeper attenuation, into a real cut —
one filter the whole way, nothing switched, nothing crossfaded.

At EDGE 0 the plug-in is a **bit-exact wire** with **zero latency**.

---

## Formats and requirements

| | |
|---|---|
| Formats | VST3, Audio Unit, Standalone |
| macOS | universal binary (Apple Silicon + Intel), ad-hoc signed |
| Windows | VST3 + Standalone, built from the source bundle (`build-windows.bat` builds, runs both test suites, and only then installs) |
| Host target | Cubase 15 (filed under *Filter*); any VST3/AU host |
| Sample rates | 44.1 / 48 / 88.2 / 96 / 192 kHz — corner identical at all five |
| Channels | mono or stereo |
| Latency | **0 samples**, all settings |
| Framework | JUCE 9, C++20, drawn entirely in code — no image assets |

---

## Signal path

```
input ──┬── analyzer tap (pre-filter) ── envelope follower
        │
        ├── hidden colour (WARM / IRON, drive from BITE × filter activity)
        ├── MID bell
        ├── LOW edge  (6 morphing TPT sections + shoulder)
        ├── HIGH edge (6 morphing TPT sections + shoulder)
        ├── output trim (+ measured colour level compensation)
        └── bit-exact bypass / dry path
```

One section type does everything: a TPT/ZDF state-variable section recombined
as `y = x + (G−1)·low + (√G−1)·(k·band)`. `G = 1` is a bit-exact wire — so an
inactive mode edge, Depth 0, an unused Curve section and MID at 0 dB are all
*the same thing* and none of them can click. `G = 0` is a textbook second-order
cut. Monotonicity (no unplanned peak) is proven analytically for `k ≥ √2`.

---

## Controls

### Header
| control | function |
|---|---|
| PRESET | 23 factory programs, browser + prev/next |
| BITE | 0–100 %: hidden colour amount, `drive = maxDrive(BITE) · activity^γ(BITE)` |
| WARM / IRON | the colour voicing (soft sag saturator / feedback core-loss loop) |
| BYPASS | bit-exact dry path |

### Graph (the visual centre)
| element | function |
|---|---|
| MODE | LP / BAND / HP / FREE segmented control |
| handles | LOW (amber), HIGH (cyan), MID (neutral) — drag = frequency ± depth/gain |
| EDGE PATH | rail from the neutral boundary to the target diamond; the filled puck rides the **live** corner from the engine snapshot |
| readout | persistent semantic identity, e.g. `LP • 3.2 kHz` — never disappears |
| analyzer | the **incoming** signal, 96 log bands, behind everything |
| inspector | one floating strip; opens on handle/FOLLOW touch, closes on empty space or Escape |

### Macro deck
| control | size | function |
|---|---|---|
| LOW | 82 px | low edge destination, 20 Hz – 8 kHz |
| **EDGE** | 116 px | CLEAN → CUT master travel; violet outer marker shows the live position after FOLLOW |
| HIGH | 82 px | high edge destination, 200 Hz – 20 kHz |
| FOLLOW → EDGE | 74 px | −100…+100 %, the signal driving EDGE (band centre in FREE) |

### Inspector contexts
| context | contents |
|---|---|
| LOW / HP | DEPTH · CURVE · RESO · SHOULDER |
| HIGH / LP | DEPTH · CURVE · RESO · SHOULDER |
| MID | FREQUENCY · GAIN · WIDTH |
| FOLLOW | AMOUNT · ATTACK · RELEASE · SENS |

24 host parameters, state version 2 with migration from v0.1; the parameter
contract (IDs, ranges, defaults) is hashed and asserted in CI:
`0x7826a61a1db9aa41`. Full table: [PARAMETER-TABLE.md](PARAMETER-TABLE.md).

*Note: SPREAD and OUTPUT currently have no panel control (not in the approved
layout); both remain fully host-automatable and preset-settable.*

---

## Measured performance (v0.13 universal build)

| | measured |
|---|---|
| neutral path | `max|out−in| = 0` exactly, any BITE/CHARACTER/MODE |
| bypass | bit-exact |
| latency | 0 samples |
| audio-thread allocations | 0 in 10,000 blocks |
| throughput | ~253× realtime stereo, analyzer feeding |
| drawn vs measured response | worst 0.03 dB |
| EDGE continuity | 0.128 dB worst step over 2,000 positions |
| mode switching | −100 dBFS excess step |
| slopes (measured steepest) | 12.0 / 24.1 / 35.7 / 46.0 / 61.4 dB/oct |
| Depth floor | −132 dB |
| colour aliasing ceiling | WARM −73.9 dBc, IRON −89.8 dBc, at 1× |
| SPREAD 0 | channels null bit-exactly |
| render determinism | two offline renders null exactly; 256 vs 512-sample blocks null exactly |

## Measured display correctness

| | measured |
|---|---|
| EDGE travel, LP | live cutoff monotonic left, 860 → 637 px |
| EDGE travel, HP / BAND | monotonic right / both inward |
| FREE corner drift | 0.000 px |
| target drift while EDGE moves | 0.000 px |
| handle x vs parameter | 0.000 px, same frame as the gesture |
| refresh | 60 Hz interacting, 30 Hz idle; analyzer on its own 30 Hz clock |
| mode-label dropouts over 80 switches | 0 |
| inspector placement (4 contexts × 5 freqs) | 0 clipped, 0 handle/selector/readout overlaps |
| inspector attachment constructions over 100 switches | 0 |
| analyzer tone placement (100 Hz / 1 k / 10 k) | correct band ±1 |
| analyzer level accuracy | −18.30 / −18.01 / −18.00 dBFS for a −18 dBFS tone |
| analyzer gating | −84 dBFS in: 0 bands lit; 1.5 s silence: 0 lit |
| layout audit | 9 size × scale combinations, 0 clipping, 0 overlap |
| palette | 84.2 L* shell/graph separation, text contrast ≥ 12.7:1 |

## Verification

93 DSP checks + 124 host-contract checks, green in Release **and** under
ASan+UBSan with zero sanitiser diagnostics. `auval` passes. The MID
stale-display defect, the EDGE-travel defect and the LP-label defect are each
reproduced by a regression test that failed before its fix.

**Verified on this machine:** macOS universal, auval, both suites, installed
to `~/Library/Audio/Plug-Ins` (VST3 + AU, v0.13, signatures valid).
**Not yet verified:** Cubase 15 session behaviour (v0.1 was the last version
tried in the host), Windows/MSVC build, pluginval, signing/notarisation.

---

## Install (this machine — already done)

| what | where |
|---|---|
| `EDGE.vst3` v0.13 | `~/Library/Audio/Plug-Ins/VST3/` |
| `EDGE.component` v0.13 | `~/Library/Audio/Plug-Ins/Components/` |

In Cubase: **Studio → VST Plug-in Manager → rescan**. EDGE appears under
*Filter*. If Cubase was open during the install, restart it.
