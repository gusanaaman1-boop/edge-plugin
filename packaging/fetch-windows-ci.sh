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
#
# But comparing the two commits outright is too blunt: editing a packaging
# script or a note in docs/ would force a twelve-minute rebuild to produce a
# byte-identical binary, and a guard that cries wolf is a guard people learn to
# pass with --force. So compare only what actually ENDS UP in the Windows half.
HEAD_SHA=$(git rev-parse HEAD)
INPUTS=(Source CMakeLists.txt
        packaging/EDGE.iss packaging/INFO-BEFORE.txt
        .github/workflows/windows.yml
        docs/MANUAL.md docs/PARAMETER-TABLE.md)

if [ "$SHA" != "$HEAD_SHA" ]; then
    if git diff --quiet "$SHA" "$HEAD_SHA" -- "${INPUTS[@]}" 2>/dev/null; then
        echo
        echo "note: that run built ${SHA:0:9}, the tree is at ${HEAD_SHA:0:9},"
        echo "      but nothing the Windows half is made of changed between"
        echo "      them. These binaries are the ones this tree would produce."
    else
        echo
        echo "REFUSING: that run built ${SHA:0:9}, the tree is at ${HEAD_SHA:0:9},"
        echo "and these inputs to the Windows build differ:"
        git diff --name-only "$SHA" "$HEAD_SHA" -- "${INPUTS[@]}" | sed 's/^/  /'
        echo
        echo "Push, run the workflow again, and re-run this."
        exit 1
    fi
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
