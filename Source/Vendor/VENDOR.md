# Vendored sources — do not edit

## FourColor/ — the project's existing saturation engine

Copied **byte-identical** from `Make Music/FourColor/Source` on 2026-08-10.

| File | Origin | sha256 (at copy time) |
|---|---|---|
| `FourColor/Dsp/ColorEngine.h`   | `FourColor/Source/Dsp/ColorEngine.h`   | — |
| `FourColor/Dsp/ColorEngine.cpp` | `FourColor/Source/Dsp/ColorEngine.cpp` | `53ab1bb4915bcb469897ad4cddc6d1c7b05a81e34633e9446deae74540890a7b` |
| `FourColor/Dsp/TptFilters.h`    | `FourColor/Source/Dsp/TptFilters.h`    | — |
| `FourColor/Core/ParameterIds.h` | `FourColor/Source/Core/ParameterIds.h` | — |

### Why a copy and not an include path into `../FourColor`

FOUR COLOR is a live product at 70% with its own git repo and its own session.
Pointing EDGE's build at that tree would mean

* a change made for FOUR COLOR silently changes EDGE's sound, and
* EDGE's build breaks whenever that tree is mid-edit,

which is exactly the cross-plugin coupling this workspace avoids. The copy is
unmodified, so the engine's behaviour is identical, and the provenance is
recorded here.

### Rules

* **Never edit these files.** EDGE adapts to the engine's API, not the reverse.
* `NonlinearStage` (FOUR COLOR's wrapper around the engines) is deliberately
  **not** vendored — see `docs/DSP-TOPOLOGY.md` §6 for why EDGE wraps
  `ColorEngine` itself instead.
* If FOUR COLOR's engine is improved and EDGE should follow, re-copy the whole
  file and update the hash above. Do not cherry-pick lines.
