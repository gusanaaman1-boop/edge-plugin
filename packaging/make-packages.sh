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

# --- Windows source ---------------------------------------------------------
echo
echo "== Windows: source bundle =="
SRC="$DIST/stage-win/EDGE-$VERSION-src"
mkdir -p "$SRC"

# git archive gives exactly what is tracked - no build folders, no outputs, no
# 200 MB of JUCE.
git archive HEAD | tar -x -C "$SRC"

cp "$ROOT/packaging/BUILD-ME-FIRST.txt" "$SRC/"
cp "$ROOT/packaging/build-windows.bat" "$SRC/"

( cd "$DIST/stage-win" && zip -qr "$DIST/EDGE-$VERSION-windows-src.zip" "EDGE-$VERSION-src" )
rm -rf "$DIST/stage-win"

echo
echo "== done =="
ls -lh "$DIST" | tail -n +2
