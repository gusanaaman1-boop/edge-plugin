#!/usr/bin/env bash
#
# Builds the universal macOS binaries and assembles the two delivery zips.
#
#   dist/EDGE-<ver>-macOS.zip     VST3 + AU + Standalone, universal, ready to use
#   dist/EDGE-<ver>-windows-src.zip  the source, with a .bat that builds it
#
# There is no cross-compiler on this machine and pretending otherwise wastes a
# round trip, so Windows ships as source that builds itself.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
DIST="$ROOT/dist"

echo "EDGE packaging — version $VERSION"

# The Windows bundle is produced with `git archive HEAD`, so anything not
# committed is silently absent from it - the macOS binaries would be built from
# the working tree while the Windows source shipped the last commit. That
# mismatch is exactly the kind of thing that only surfaces on someone else's
# machine, so refuse rather than ship it.
if ! git diff-index --quiet HEAD -- 2>/dev/null; then
    echo
    echo "REFUSING: the working tree has uncommitted changes."
    echo "The Windows bundle is built from HEAD, so those changes would NOT ship"
    echo "while the macOS binaries WOULD contain them. Commit first."
    echo
    git status --short
    exit 1
fi
rm -rf "$DIST"
mkdir -p "$DIST"

# --- macOS ------------------------------------------------------------------
echo
echo "== macOS: universal build =="
cmake -B build-universal -DCMAKE_BUILD_TYPE=Release -DEDGE_COPY_AFTER_BUILD=OFF >/dev/null
cmake --build build-universal --target Edge_All -j8 >/dev/null

ART="$ROOT/build-universal/Edge_artefacts/Release"
for f in "VST3/EDGE.vst3" "AU/EDGE.component" "Standalone/EDGE.app"; do
    [ -e "$ART/$f" ] || { echo "MISSING: $ART/$f"; exit 1; }
done

# Every slice must actually be there - a "universal" build that is arm64 only
# looks identical until someone opens it under Rosetta.
for f in "VST3/EDGE.vst3/Contents/MacOS/EDGE" \
         "AU/EDGE.component/Contents/MacOS/EDGE" \
         "Standalone/EDGE.app/Contents/MacOS/EDGE"; do
    archs=$(lipo -archs "$ART/$f")
    echo "  $(basename "$(dirname "$(dirname "$f")")"): $archs"
    case "$archs" in
        *x86_64*arm64*|*arm64*x86_64*) ;;
        *) echo "NOT UNIVERSAL: $f ($archs)"; exit 1;;
    esac
done

# The parameter table is generated FROM the binary being packaged. A table
# copied from the repository could describe a different build.
#
# Built explicitly first: a stale EdgeShot took "--parameter-table" for an
# output directory and rendered seven PNGs into a folder of that name.
cmake --build build-universal --target EdgeShot -j8 >/dev/null
"$ROOT/build-universal/EdgeShot_artefacts/Release/EdgeShot" --parameter-table docs/PARAMETER-TABLE.md

STAGE="$DIST/stage-mac/EDGE-$VERSION"
mkdir -p "$STAGE"
cp -R "$ART/VST3/EDGE.vst3" "$STAGE/"
cp -R "$ART/AU/EDGE.component" "$STAGE/"
cp -R "$ART/Standalone/EDGE.app" "$STAGE/"
cp "$ROOT/README.md" "$STAGE/"
cp "$ROOT/docs/MANUAL.md" "$STAGE/"
cp "$ROOT/docs/PARAMETER-TABLE.md" "$STAGE/"
cp "$ROOT/docs/SIGNING.md" "$STAGE/"
cp "$ROOT/packaging/uninstall-macos.sh" "$STAGE/"

cat > "$STAGE/INSTALL-macOS.txt" <<'TXT'
EDGE — macOS
============

There is an installer: EDGE-<version>.pkg, alongside this zip. It puts each
format where it belongs and lets you deselect the ones you do not want.

If you would rather copy by hand, copy:

  EDGE.vst3       ->  ~/Library/Audio/Plug-Ins/VST3/
  EDGE.component  ->  ~/Library/Audio/Plug-Ins/Components/

EDGE.app is the standalone; it needs no installation, just double-click it.

To remove everything again: ./uninstall-macos.sh --remove

These are universal binaries (Apple Silicon and Intel), so they load in hosts
running natively and under Rosetta.

They are NOT signed or notarised. The first time you open the standalone,
macOS will refuse it: right-click the app, choose Open, then Open again in the
dialog. Plug-ins loaded by a DAW are not affected by this. SIGNING.md says what
that costs and what fixes it.

MANUAL.md is the manual. PARAMETER-TABLE.md lists every control and preset,
generated from the plug-in itself.

In Cubase: Studio > VST Plug-in Manager > rescan. EDGE is filed under Filter.
TXT

( cd "$DIST/stage-mac" && zip -qr "$DIST/EDGE-$VERSION-macOS.zip" "EDGE-$VERSION" )
rm -rf "$DIST/stage-mac"

# --- macOS installer ---------------------------------------------------------
echo
echo "== macOS: installer =="
"$ROOT/packaging/make-installer-macos.sh" "$VERSION"

# --- macOS disk image --------------------------------------------------------
# The one file a Mac user should be handed: the installer, the manual and an
# uninstaller, in the window that opens when they double-click it.
echo
echo "== macOS: disk image =="
"$ROOT/packaging/make-dmg-macos.sh" "$VERSION"

# --- Windows source ---------------------------------------------------------
echo
echo "== Windows: source bundle =="
SRC="$DIST/stage-win/EDGE-$VERSION-src"
mkdir -p "$SRC"

# git archive gives exactly what is tracked - no build folders, no outputs, no
# 200 MB of JUCE.
git archive HEAD | tar -x -C "$SRC"

cp "$ROOT/packaging/BUILD-ME-FIRST.txt" "$SRC/"
cp "$ROOT/packaging/INSTALL-EDGE.bat" "$SRC/"
cp "$ROOT/packaging/MAKE-INSTALLER.bat" "$SRC/"
cp "$ROOT/packaging/RUN-EDGE-TESTS.bat" "$SRC/"
cp "$ROOT/packaging/UNINSTALL-EDGE.bat" "$SRC/"

# The four scripts must be at the bundle root, and MAKE-INSTALLER.bat needs the
# two files it feeds to Inno Setup. git archive carries packaging/ already, but
# checking beats assuming - the bundle is only ever opened on another machine.
for f in "INSTALL-EDGE.bat" "MAKE-INSTALLER.bat" "RUN-EDGE-TESTS.bat" \
         "UNINSTALL-EDGE.bat" "BUILD-ME-FIRST.txt" \
         "packaging/EDGE.iss" "packaging/INFO-BEFORE.txt" "CMakeLists.txt"; do
    [ -e "$SRC/$f" ] || { echo "WINDOWS BUNDLE INCOMPLETE: $f"; exit 1; }
done

( cd "$DIST/stage-win" && zip -qr "$DIST/EDGE-$VERSION-windows-src.zip" "EDGE-$VERSION-src" )
rm -rf "$DIST/stage-win"

# --- what is what -------------------------------------------------------------
# Four files in a folder with no note is a guessing game. This one says who
# each file is for, in the folder itself, where the guessing would happen.
cat > "$DIST/README-FIRST.txt" <<TXT
EDGE $VERSION — what to send, and to whom
by Gussa Naaman


ON A MAC
--------

  EDGE-$VERSION.dmg            <- send THIS

    Double-click, then double-click the .pkg inside. Choose VST3, Audio
    Unit and/or Standalone. Universal — Apple Silicon and Intel. The manual
    and a double-clickable uninstaller are in the same window.

  EDGE-$VERSION.pkg            the bare installer, if you would rather not
                              send a disk image
  EDGE-$VERSION-macOS.zip      the raw .vst3 / .component / .app, for
                              someone who wants to place them by hand


ON WINDOWS
----------

  EDGE-$VERSION-windows-src.zip

    This is the SOURCE. Whoever runs it needs Visual Studio 2022 (free).
    Extract it, then right-click INSTALL-EDGE.bat and Run as administrator.

    There is no Windows compiler on the Mac these packages were built on, so
    a ready-made Windows installer cannot be produced here. It can be
    produced ONCE on any Windows machine:

        extract the ZIP  ->  double-click MAKE-INSTALLER.bat

    That yields a single file — dist\\EDGE-<version>-windows.exe, whose exact
    name it prints when it finishes — and that file needs NO tools at all on
    the machines you then send it to.

    Do that once, keep the .exe, and Windows becomes as simple as the Mac.


NOT SIGNED
----------

  Neither platform's package is code-signed.

    macOS    the standalone needs right-click, Open, the first time.
             Plug-ins inside a DAW are unaffected.
    Windows  SmartScreen may show a blue panel: More info, then Run anyway.

  docs/SIGNING.md says exactly what certificates cost and what they fix.
TXT

echo
echo "== done =="
ls -lh "$DIST" | tail -n +2
