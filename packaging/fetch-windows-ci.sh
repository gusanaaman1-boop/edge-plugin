#!/usr/bin/env bash
#
# Downloads the Windows binaries from the last GREEN CI run into build-win-ci/.
#
# There is no Windows compiler on this Mac. GitHub Actions builds EDGE with
# Visual Studio 2022, runs all 226 measurement checks with MSVC, and only then
# packs the installer - so these are binaries that were tested before they
# existed, which is more than a local build can say.
#
#   packaging/fetch-windows-ci.sh
#
# Then make-packages.sh folds them into the Windows delivery zip.

set -euo pipefail

cd "$(dirname "$0")/.."
ROOT="$PWD"
OUT="$ROOT/build-win-ci"

# The LAST SUCCESSFUL run, not the last run. A failed run has no artefacts and
# a red run's leftovers are exactly what must never reach a delivery zip.
RUN=$(gh run list --workflow=windows.yml --status=success --limit 1 \
        --json databaseId,headSha,createdAt \
        --jq '.[0] | "\(.databaseId) \(.headSha) \(.createdAt)"')
[ -n "$RUN" ] || { echo "no successful Windows run to download from"; exit 1; }

RUN_ID=${RUN%% *}
REST=${RUN#* }
SHA=${REST%% *}
WHEN=${REST#* }

echo "run     $RUN_ID"
echo "commit  ${SHA:0:9}"
echo "built   $WHEN"

# Building the delivery from a run of DIFFERENT code than the tree produces a
# package whose two halves disagree, and nothing downstream would notice.
HEAD_SHA=$(git rev-parse HEAD)
if [ "$SHA" != "$HEAD_SHA" ]; then
    echo
    echo "REFUSING: that run built ${SHA:0:9}, the tree is at ${HEAD_SHA:0:9}."
    echo "The Windows and macOS halves would be built from different code."
    echo "Push, run the workflow again, and re-run this."
    exit 1
fi

rm -rf "$OUT"
mkdir -p "$OUT"
gh run download "$RUN_ID" -D "$OUT" >/dev/null

EXE=$(find "$OUT" -name "EDGE-*-windows.exe" | head -1)
VST3=$(find "$OUT" -type d -name "*windows-VST3" | head -1)

[ -n "$EXE" ]  || { echo "the run produced no installer"; exit 1; }
[ -n "$VST3" ] || { echo "the run produced no VST3 bundle"; exit 1; }
[ -f "$VST3/Contents/x86_64-win/EDGE.vst3" ] || { echo "the VST3 bundle has no payload"; exit 1; }

echo
echo "  $(basename "$EXE")  $(du -h "$EXE" | cut -f1)"
echo "  EDGE.vst3           $(du -h "$VST3/Contents/x86_64-win/EDGE.vst3" | cut -f1)"
echo
echo "ready for make-packages.sh"
