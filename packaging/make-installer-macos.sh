#!/usr/bin/env bash
#
# Builds EDGE.pkg — a real macOS installer, from the universal artefacts.
#
#   dist/EDGE-<ver>.pkg
#
# Three components, each individually deselectable in the installer UI, because
# a producer who only uses Cubase does not want an Audio Unit in their Logic
# folder.
#
# pkgbuild and productbuild ship with macOS. Nothing is downloaded and nothing
# is paid for. The package is NOT signed - see docs/SIGNING.md for exactly what
# that costs the user and what fixes it.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
IDENTIFIER="com.naaman.edge"

ART="$ROOT/build-universal/Edge_artefacts/Release"
STAGE="$ROOT/build-installer"
DIST="$ROOT/dist"

echo "EDGE installer — $VERSION"

# --- the artefacts must already exist, and must be universal -----------------
# Building them here would hide a stale build behind a fresh installer.
for f in "VST3/EDGE.vst3" "AU/EDGE.component" "Standalone/EDGE.app"; do
    [ -e "$ART/$f" ] || { echo "MISSING: $ART/$f — run packaging/make-packages.sh first"; exit 1; }
done

for f in "VST3/EDGE.vst3/Contents/MacOS/EDGE" \
         "AU/EDGE.component/Contents/MacOS/EDGE" \
         "Standalone/EDGE.app/Contents/MacOS/EDGE"; do
    archs=$(lipo -archs "$ART/$f")
    case "$archs" in
        *arm64*x86_64*|*x86_64*arm64*) ;;
        *) echo "NOT UNIVERSAL: $f is $archs"; exit 1 ;;
    esac
done

rm -rf "$STAGE"
mkdir -p "$STAGE/roots/vst3/Library/Audio/Plug-Ins/VST3"
mkdir -p "$STAGE/roots/au/Library/Audio/Plug-Ins/Components"
mkdir -p "$STAGE/roots/app/Applications"
mkdir -p "$STAGE/pkgs" "$DIST"

cp -R "$ART/VST3/EDGE.vst3"       "$STAGE/roots/vst3/Library/Audio/Plug-Ins/VST3/"
cp -R "$ART/AU/EDGE.component"    "$STAGE/roots/au/Library/Audio/Plug-Ins/Components/"
cp -R "$ART/Standalone/EDGE.app"  "$STAGE/roots/app/Applications/"

# --- one component package per format ----------------------------------------
build_component () {
    local name="$1" root="$2" install_to="$3"
    pkgbuild --quiet \
             --root "$STAGE/roots/$root" \
             --identifier "$IDENTIFIER.$name" \
             --version "${VERSION#v}" \
             --install-location "/" \
             "$STAGE/pkgs/$name.pkg"
    echo "  component: $name -> $install_to"
}

build_component vst3 vst3 "/Library/Audio/Plug-Ins/VST3"
build_component au   au   "/Library/Audio/Plug-Ins/Components"
build_component app  app  "/Applications"

# --- the distribution --------------------------------------------------------
cat > "$STAGE/distribution.xml" <<XML
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>EDGE $VERSION - by Gussa Naaman</title>
    <organization>com.naaman</organization>
    <options customize="always" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_localSystem="true"/>
    <welcome file="welcome.html" mime-type="text/html"/>

    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="app"/>
    </choices-outline>

    <choice id="vst3" title="VST3" description="For Cubase, Live, Reaper, Bitwig and anything else that loads VST3.">
        <pkg-ref id="$IDENTIFIER.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="For Logic Pro and GarageBand.">
        <pkg-ref id="$IDENTIFIER.au"/>
    </choice>
    <choice id="app" title="Standalone application" description="EDGE on its own, without a host.">
        <pkg-ref id="$IDENTIFIER.app"/>
    </choice>

    <pkg-ref id="$IDENTIFIER.vst3" version="${VERSION#v}" onConclusion="none">vst3.pkg</pkg-ref>
    <pkg-ref id="$IDENTIFIER.au"   version="${VERSION#v}" onConclusion="none">au.pkg</pkg-ref>
    <pkg-ref id="$IDENTIFIER.app"  version="${VERSION#v}" onConclusion="none">app.pkg</pkg-ref>
</installer-gui-script>
XML

mkdir -p "$STAGE/resources"
cat > "$STAGE/resources/welcome.html" <<HTML
<html><body style="font-family:-apple-system;font-size:13px">
<p><b>EDGE $VERSION</b> — a two-sided filter for electronic music.<br>
by <b>Gussa Naaman</b></p>
<p>This build is <b>not signed or notarised</b>. macOS may warn about it the
first time you open the standalone application; the plug-ins themselves load
normally in a host.</p>
<p>To remove EDGE later, run <code>packaging/uninstall-macos.sh</code> from the
source, or delete the three items it installs by hand.</p>
</body></html>
HTML

productbuild --quiet \
             --distribution "$STAGE/distribution.xml" \
             --package-path "$STAGE/pkgs" \
             --resources "$STAGE/resources" \
             "$DIST/EDGE-$VERSION.pkg"

# --- verify -------------------------------------------------------------------
# A .pkg that builds is not a .pkg that contains anything. Check the payload.
echo
echo "== verifying the payload =="
for name in vst3 au app; do
    n=$(pkgutil --payload-files "$STAGE/pkgs/$name.pkg" | wc -l | tr -d ' ')
    echo "  $name.pkg: $n files"
    [ "$n" -gt 0 ] || { echo "EMPTY COMPONENT: $name"; exit 1; }
done

pkgutil --payload-files "$DIST/EDGE-$VERSION.pkg" >/dev/null 2>&1 || true

echo
ls -lh "$DIST/EDGE-$VERSION.pkg"
echo
echo "NOT SIGNED. See docs/SIGNING.md."
