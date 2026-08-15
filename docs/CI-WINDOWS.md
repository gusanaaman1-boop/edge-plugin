# Building EDGE for Windows on GitHub

`.github/workflows/windows.yml` builds EDGE with MSVC, runs all 226 measurement
checks, packs the result into `EDGE-<version>-windows.exe` with Inno Setup, and
refuses to produce the installer if a single check fails.

Nothing starts by itself. A push to `main` triggers nothing. The workflow runs
only when you ask for it — **Actions → Windows → Run workflow** — or when a
`v*` tag is pushed.

---

## Status: working

`edge-plugin` was made **public** on 2026-08-15. Public repositories get
unmetered Actions, so hosted runs work here and cost nothing.

Run **31880792789** was the first fully green one: Visual Studio 2022, 93 DSP
checks and 133 host-contract checks under MSVC, installer packed and
size-checked, 4.8 MB. It took about twelve minutes.

That first compile also found two real defects — see the commit log for
`__has_feature` and for the CPU bar. EDGE had never met a Microsoft compiler
before; both are fixed.

> **Background.** Hosted runs are still blocked on this account's **private**
> repositories, and have been since 2026-08-06 — a run starts, executes zero
> steps, and stops after about two seconds citing billing. That is why this
> repository being public is what makes the workflow run. If EDGE is ever made
> private again, use Option A below.

---

## Option A — your own Windows desktop as a runner

A **self-hosted runner** is your Windows machine, registered with the repo.
GitHub sends it the job; your machine does the work. **It consumes no Actions
minutes on any plan**, so the billing block does not apply to it at all.

Set up once, about five minutes:

1. On GitHub: **Settings → Actions → Runners → New self-hosted runner →
   Windows x64**. The page shows a short block of PowerShell with a token in
   it — run exactly that on your Windows machine, in a folder like
   `C:\actions-runner`.
2. When it asks for labels, accept the defaults. When it asks to run as a
   service, say yes — then it starts with Windows and you never think about it
   again.
3. That machine needs, once:
   - **Visual Studio 2022** with *Desktop development with C++* (which also
     provides CMake)
   - **Inno Setup 6** — <https://jrsoftware.org/isdl.php>. The workflow
     installs it via Chocolatey if it is absent, but on your own machine it is
     cleaner to install it yourself.

Then, on GitHub: **Actions → Windows → Run workflow**, and set **Where to
build** to `self-hosted`.

Ten minutes later the installer is on the run's page under **Artifacts**,
tested with the same compiler that produced it.

**What this gets you that `MAKE-INSTALLER.bat` does not:** the tests are run,
by something other than you, before the installer exists. A local build you
forgot to test is indistinguishable from one you did.

---

## Option B — public repository (this is what EDGE does)

Public repositories get unmetered Actions and are unaffected by the spending
limit, so `windows-2022` runs immediately with no runner to install.

It publishes EDGE's entire source and its whole history — including
`Source/Vendor/FourColor/`, FOUR COLOR's saturation engine. That was raised
before the switch and accepted. Every other plug-in repository here is private.

---

## What a run does

| step | |
|---|---|
| checkout | EDGE, and JUCE 9 pinned to `857aab9c` into `./JUCE` |
| configure | Visual Studio 17 2022, x64, `EDGE_COPY_AFTER_BUILD=OFF` |
| build | `Edge_VST3`, `Edge_Standalone`, `EdgeTests`, `EdgeHostTests` |
| verify | the payload *inside* `EDGE.vst3`, not just the folder |
| test | 93 DSP checks, then 133 host-contract checks — either failure stops the run |
| pack | Inno Setup, version and artefact path passed in, never hand-edited |
| verify | the `.exe` is over 2 MB, so an empty package cannot pass as a full one |
| upload | the installer, and the raw `EDGE.vst3` for hand placement |
| release | on a `v*` tag, attached to the GitHub release |

---

## Tagging a release

```bash
git tag -a v0.18 -m "EDGE v0.18"
git push origin v0.18
```

On a working runner that builds the installer and attaches it to the release.
With hosted runs blocked and no self-hosted runner registered, the tag is
pushed and the job simply never starts — nothing breaks, nothing is lost, and
the tag stays ready for whenever a runner exists.

---

## Without any of this

`MAKE-INSTALLER.bat`, in the Windows source bundle, does the same build and the
same packing on your desktop by hand. `RUN-EDGE-TESTS.bat` runs the same 226
checks. CI's advantage is that it cannot forget the middle step.
