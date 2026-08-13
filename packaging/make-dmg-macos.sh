#!/usr/bin/env bash
#
# Builds EDGE.dmg — the single file a macOS user should ever be handed.
#
#   dist/EDGE-<ver>.dmg
#
# A disk image is what a musician expects from a Mac plug-in: double-click it,
# a window opens, the installer is right there. A .zip beside a loose .pkg is
# two downloads and a decision, and the decision is the part that goes wrong.
#
# hdiutil ships with macOS. Nothing is downloaded and nothing is paid for.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
DIST="$ROOT/dist"
PKG="$DIST/EDGE-$VERSION.pkg"
STAGE="$ROOT/build-dmg"
VOL="EDGE $VERSION"

echo "EDGE disk image — $VERSION"

# The .pkg is the payload. Building it here would let a stale installer hide
# behind a fresh disk image, so it must already exist.
[ -f "$PKG" ] || { echo "MISSING: $PKG — run packaging/make-installer-macos.sh first"; exit 1; }

rm -rf "$STAGE"
mkdir -p "$STAGE"

cp "$PKG"                        "$STAGE/EDGE $VERSION.pkg"
cp "$ROOT/docs/MANUAL.md"        "$STAGE/"
cp "$ROOT/docs/PARAMETER-TABLE.md" "$STAGE/"

# A double-clickable uninstaller. The shell script works, but a musician does
# not open Terminal to remove a plug-in, and .command opens on double-click.
cat > "$STAGE/Uninstall EDGE.command" <<'CMD'
#!/usr/bin/env bash
#  Removes EDGE from this Mac. Double-click me.
cd "$(dirname "$0")"
echo
echo "This removes EDGE — the VST3, the Audio Unit and the standalone app."
echo "Your presets and window size are not touched."
echo
read -r -p "Remove EDGE? [y/N] " a
case "$a" in
    [yY]*) ;;
    *) echo "Nothing was removed."; exit 0 ;;
esac
echo
echo "Your Mac password may be asked for — the plug-ins live in /Library."
for t in "/Library/Audio/Plug-Ins/VST3/EDGE.vst3" \
         "/Library/Audio/Plug-Ins/Components/EDGE.component" \
         "/Applications/EDGE.app" \
         "$HOME/Library/Audio/Plug-Ins/VST3/EDGE.vst3" \
         "$HOME/Library/Audio/Plug-Ins/Components/EDGE.component" \
         "$HOME/Library/Logs/EDGE"; do
    if [ -e "$t" ]; then
        if [ -w "$(dirname "$t")" ]; then rm -rf "$t"; else sudo rm -rf "$t"; fi
        echo "  removed  $t"
    fi
done
echo
echo "Done. Rescan in your DAW."
read -r -p "Press return to close. "
CMD
chmod +x "$STAGE/Uninstall EDGE.command"

cat > "$STAGE/READ ME FIRST.txt" <<TXT
EDGE $VERSION — a two-sided musical filter
by Gussa Naaman


INSTALL

  Double-click   EDGE $VERSION.pkg

  It asks which formats you want. Untick anything you do not use:

    VST3          Cubase, Live, Reaper, Bitwig, Studio One
    Audio Unit    Logic Pro, GarageBand
    Standalone    EDGE on its own, no host needed

  These are universal binaries — Apple Silicon and Intel — so they load
  natively and under Rosetta.

  In Cubase afterwards: Studio menu, VST Plug-in Manager, then Update.
  EDGE appears under Naaman, in the Filter category.


THE FIRST TIME YOU OPEN THE STANDALONE

  This build is not notarised, so macOS refuses it once: right-click
  EDGE.app, choose Open, then Open again in the dialog. From then on it
  behaves normally.

  Plug-ins loaded by a DAW are NOT affected by this — only the standalone.


WHAT IT IS

  Draw the destination. EDGE travels there. Your sound pushes it.

  LOW and HIGH        where the two spectral edges are going
  EDGE                travels from CLEAN to CUT — one control, both sides
  FOLLOW to EDGE      lets the signal itself drive that travel
  SPREAD              moves the channels apart without narrowing either one
  BITE / WARM · IRON  hidden colour that arrives with the depth

  At EDGE 0 it is a bit-exact wire and adds zero latency.

  MANUAL.md is the manual. PARAMETER-TABLE.md lists every control and every
  preset, generated from the plug-in itself.


REMOVING IT

  Double-click "Uninstall EDGE.command".
TXT

# UDZO is the compressed read-only image every Mac can open with no helper.
rm -f "$DIST/EDGE-$VERSION.dmg"
hdiutil create -quiet \
        -volname "$VOL" \
        -srcfolder "$STAGE" \
        -ov -format UDZO \
        "$DIST/EDGE-$VERSION.dmg"

rm -rf "$STAGE"

# --- verify -------------------------------------------------------------------
# A .dmg that builds is not a .dmg that contains anything. Mount it and look.
echo
echo "== verifying the image =="
MNT=$(mktemp -d)
hdiutil attach -quiet -nobrowse -readonly -mountpoint "$MNT" "$DIST/EDGE-$VERSION.dmg"
missing=0
for f in "EDGE $VERSION.pkg" "READ ME FIRST.txt" "MANUAL.md" "Uninstall EDGE.command"; do
    if [ -e "$MNT/$f" ]; then echo "  ok  $f"; else echo "  MISSING  $f"; missing=1; fi
done
[ -x "$MNT/Uninstall EDGE.command" ] || { echo "  NOT EXECUTABLE  Uninstall EDGE.command"; missing=1; }
hdiutil detach -quiet "$MNT"
rmdir "$MNT" 2>/dev/null || true
[ "$missing" = "0" ] || { echo "the image is incomplete"; exit 1; }

echo
ls -lh "$DIST/EDGE-$VERSION.dmg"
