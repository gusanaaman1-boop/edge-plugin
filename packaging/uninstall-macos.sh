#!/usr/bin/env bash
#
# Removes everything EDGE's installer put on this machine, and nothing else.
#
# macOS .pkg installers have no uninstaller. Shipping one without this means
# telling a customer to go digging in /Library themselves.
#
#   packaging/uninstall-macos.sh            list what would be removed
#   packaging/uninstall-macos.sh --remove   actually remove it

set -euo pipefail

TARGETS=(
    "/Library/Audio/Plug-Ins/VST3/EDGE.vst3"
    "/Library/Audio/Plug-Ins/Components/EDGE.component"
    "/Applications/EDGE.app"
    "$HOME/Library/Audio/Plug-Ins/VST3/EDGE.vst3"
    "$HOME/Library/Audio/Plug-Ins/Components/EDGE.component"
)

# The log and the receipts. Preferences are NOT touched: a reinstall should not
# forget the window size someone chose.
EXTRAS=(
    "$HOME/Library/Logs/EDGE"
)

REMOVE=0
[ "${1:-}" = "--remove" ] && REMOVE=1

found=0
for t in "${TARGETS[@]}" "${EXTRAS[@]}"; do
    if [ -e "$t" ]; then
        found=$((found + 1))
        if [ "$REMOVE" = "1" ]; then
            #  System paths need root; say so rather than failing silently.
            if [ -w "$(dirname "$t")" ]; then
                rm -rf "$t"
                echo "  removed  $t"
            else
                sudo rm -rf "$t"
                echo "  removed  $t  (as root)"
            fi
        else
            echo "  would remove  $t"
        fi
    fi
done

if [ "$REMOVE" = "1" ]; then
    for id in com.naaman.edge.vst3 com.naaman.edge.au com.naaman.edge.app; do
        if pkgutil --pkg-info "$id" >/dev/null 2>&1; then
            sudo pkgutil --forget "$id" >/dev/null
            echo "  forgot receipt  $id"
        fi
    done
fi

echo
if [ "$found" = "0" ]; then
    echo "Nothing installed."
elif [ "$REMOVE" = "1" ]; then
    #  Verify, do not assume. An uninstaller that reports success while leaving
    #  a plug-in behind is worse than one that fails.
    left=0
    for t in "${TARGETS[@]}" "${EXTRAS[@]}"; do
        [ -e "$t" ] && { echo "STILL PRESENT: $t"; left=$((left + 1)); }
    done

    [ "$left" = "0" ] && echo "Removed. 0 files left." || { echo "$left left behind."; exit 1; }
else
    echo "$found item(s). Re-run with --remove to delete them."
fi
