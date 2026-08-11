# Release phases 1–4

The work order from the review has ten phases. This records what phases 1–4
actually did, what was measured, and — just as important — what is still
**unverified** because this machine cannot verify it.

Nothing below is claimed unless a suite printed it.

---

## Phase 1 — Windows / MSVC build

**Status: prepared, NOT verified.** There is no Windows machine and no MSVC
here, and hosted CI is unavailable. Everything that can be fixed by inspection
has been; the compile itself is still the user's step.

### What was wrong

**Implicit standard headers.** Seventeen files named `std::` facilities without
including the header that provides them — `std::map` in `Core/StateMigration.h`,
`std::function` in `Ui/ShapePanel.h`, `std::printf`/`std::malloc`/`std::bad_alloc`
in the two tools, and so on. libc++ hands these over transitively; MSVC's STL
does not, so every one of them is a Windows-only compile error that no amount of
testing on macOS can find. All now included explicitly.

**No `/utf-8`.** MSVC reads source in the machine's ANSI codepage unless told
otherwise. The tree is pure ASCII today, so nothing was broken — but the first
non-ASCII character typed into a comment would have become a Windows-only build
failure. `/utf-8` is now in `EDGE_WARNING_FLAGS`.

**No local equivalent of MSVC's `/W4`.** Two `/W4` warnings fire on DSP code and
nothing on this machine was looking for them: C4244 (`double` → `float`) and
C4267 (`size_t` → `int`). The macOS build now carries `-Wfloat-conversion` and
`-Wshorten-64-to-32`, which is as close as this platform gets to standing in for
the other one.

**Three real warnings**, all now fixed: a parameter in `ShapePanel::Control::attach`
shadowing the panel's own `state` member, an unnecessary lambda capture, and a
dead private field (`dragStartMidGain`) left over from the MID anchoring fix.

### Measured here

| | |
|---|---|
| `-Werror` build of every EDGE target, with the MSVC-equivalent warnings on | **0 warnings, 0 errors** |
| DSP suite on that build | **93 checks, 0 failed** |
| non-ASCII bytes in EDGE's own sources | **0** |
| compiler-specific constructs (`__attribute__`, `typeof`, VLAs, `alloca`) | **0** |

### Still unverified

The actual MSVC compile, and the numbers the suites print on Windows.
`packaging/build-windows.bat` now builds `EdgeTests` and `EdgeHostTests`
alongside the VST3 and **runs both**, refusing to offer the install step if
either fails — so the Windows run reports its own numbers instead of inheriting
these.

---

## Phase 2 — host validation

**Status: pluginval-equivalent coverage implemented and green; pluginval itself
not run.** pluginval is not installed here and fetching it needs the user's
say-so. What it checks, however, is mostly assertable directly against the
`AudioProcessor`, and that is now a suite: `Source/Tools/test_host.cpp`,
target `EdgeHostTests`.

### What it found

**A NaN or Inf sample from upstream was permanent.** Every filter in EDGE is
recursive, so one non-finite sample reached the state and stayed there: the
plug-in output NaN for ever afterwards, and reloading it was the only cure.
Measured before the fix — one poisoned sample, then ten blocks of ordinary
audio, still non-finite. `EdgeEngine::process` now replaces non-finite input
samples with zero before anything recursive sees them. That is identity on any
finite input, so the bit-exact neutral path is untouched.

**Two controls parsed typed text as a different quantity.** Depth reads out in
dB and Curve reads out as a slope name, but both are 0–100 % parameters, and
JUCE's default parser read the printed number as the percentage. Typing
`-12 dB` into Depth set it to **0 %**; typing `24 dB/oct` into Curve set it to
**24 %**, a gentler filter than the read-out named. Shoulder had the same
problem, and `Bypass` parsed its own word `On` as 0. All four now have explicit
inverses — `depthDbToPercent`, `shoulderDbToPercent`, `curvePercentForText`, and
a bool parser — and Depth's inverse reads the *same* anchor table as the forward
law, so the two cannot drift apart.

### Measured here

`EdgeHostTests`: **35 checks, 0 failed.**

| | |
|---|---|
| parameter contract | 24 exposed, all automatable, all named, all unique, no name over 31 chars |
| value → text → value | worst **0.333 %** of range |
| 5 sample rates × 10 block sizes × 4 lengths | **200 cases**, 0 non-finite |
| mono layout | accepted, processed, finite |
| silence in | **exactly** silence out |
| NaN, Inf, 1e30, denormal input | recovers in every case |
| 20 editor open/close cycles with audio running | no crash, no leak |
| min / default / SHAPE-open / max sizes | all 4 lay out and paint |

### Still unverified

pluginval itself, at strictness 10, against the built VST3 and AU — which
exercises the SDK wrapper rather than the processor directly. **Deferred by
decision on 2026-08-11**: it is not installed here and was not fetched. The gap
it leaves is the wrapper layer — VST3 bus/parameter negotiation and the AU
property interface — not the processor, which the 35 checks above cover.

---

## Phase 3 — automation and render determinism

**Status: done, and it found a real bug.**

### What was wrong

**The output trim was applied at block granularity.** `outputGain` carries the
colour's measured level trim and the Resonance make-up, both recomputed every
32-sample chunk — but the per-sample output loop ran *after* the chunk loop, so
the whole block was scaled using the **last** chunk's target. The same project
rendered at 256 samples and at 512 samples therefore did not match: measured
**−48.5 dBFS** of difference. A host is free to change its buffer size between
two renders of the same project, so this was a real "the bounce sounds different
today" bug. The output loop now runs inside the chunk loop.

### Measured here

| | |
|---|---|
| two offline renders of the same automation (300 blocks, MODE switches, full EDGE sweep, FOLLOW, SPREAD, MID) | **null exactly** |
| same project at 256 vs 512 samples per block | **null exactly** (was −48.5 dBFS) |
| a parameter write is visible to the next block | 50.000 → 51.000 |

### Still unverified

Cubase 15 itself: writing automation on all 24 lanes, reloading the project, and
comparing two offline renders inside the host.

---

## Phase 4 — state compatibility

**Status: done, and it found a real bug.**

### What was wrong

**A state from a newer build reset the session to defaults.** `replaceState` was
handed the whole foreign tree, so parameters it did not recognise arrived and
the ones it did recognise vanished — the loudest possible way to lose someone's
work. `setStateInformation` now detects `stateVersion > kStateVersion`, takes
across every parameter it recognises, and leaves the rest exactly as they were.

### Measured here

| | |
|---|---|
| all 24 parameters through save → load | worst error **0.000000000** |
| editor size and SHAPE state through save → load | 1180 × 760, shape open |
| a recalled project rendered against the same settings set live | **null exactly** |
| v1 state detected and flagged | yes |
| v1 corner frequencies carried across | 400.0 Hz / 4000.0 Hz, unchanged |
| v1 EDGE / MODE / MID | 100 %, BAND, 0.0000 dB |
| v1 BITE against the documented migration | 28.63 %, expected 28.63 % |
| empty, corrupt and future states | survivable, and the parameters are left alone |

**The parameter contract hash** — ID, range, interval, default and step count of
all 24 parameters, FNV-1a over the sorted list — is recorded as
`0x7826a61a1db9aa41` and asserted. Changing any of them is now a deliberate act
that fails a test until the state version is bumped and a migration written,
rather than something a customer discovers when their session reopens wrong.

---

## CPU

Two of these changes are in the audio path — the non-finite input guard and the
output loop moving inside the chunk loop — so the throughput was re-measured
rather than assumed.

| | |
|---|---|
| before any of this work, idle machine | **259.335×** realtime |
| after, idle machine | **259.344×** realtime |

No measurable cost.

Mid-session the same check read **96×**, which was a browser using 190 % of the
CPU and a video decoding alongside it; the *pre-change* binary measured **97.6×**
under that same load. Throughput numbers from this suite mean nothing unless the
machine is quiet.
