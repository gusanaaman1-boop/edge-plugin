#!/usr/bin/env bash
#
# Assembles the package for the outside review chat:
#
#   docs/REVIEW-PROMPT.md              the prompt (tracked, hand-written)
#   outputs/EDGE-SOURCE-APPENDIX.md    the real source (generated, ignored)
#
# The appendix is generated rather than kept, so it can never drift out of date
# relative to the code it is supposed to be showing.

set -euo pipefail
cd "$(dirname "$0")/.."

python3 - <<'PY'
import pathlib, datetime

root = pathlib.Path(".")
order = [
    ("Build", ["CMakeLists.txt"]),
    ("Core", ["Source/Core/ParameterIds.h", "Source/Core/Parameters.h",
              "Source/Core/Parameters.cpp", "Source/Core/StateMigration.h"]),
    ("DSP - the sections", ["Source/Dsp/MorphSvf.h", "Source/Dsp/EdgeUnit.h"]),
    ("DSP - FOLLOW", ["Source/Dsp/FollowDetector.h"]),
    ("DSP - the hidden colour", ["Source/Dsp/ColorStage.h", "Source/Dsp/ColorStage.cpp"]),
    ("DSP - the engine", ["Source/Dsp/EdgeEngine.h", "Source/Dsp/EdgeEngine.cpp"]),
    ("Plug-in wrapper", ["Source/PluginProcessor.h", "Source/PluginProcessor.cpp"]),
    ("UI", ["Source/Ui/Theme.h", "Source/Ui/Theme.cpp", "Source/Ui/CurveView.h",
            "Source/Ui/CurveView.cpp", "Source/Ui/ShapePanel.h", "Source/Ui/ShapePanel.cpp",
            "Source/PluginEditor.h", "Source/PluginEditor.cpp"]),
    ("Tools", ["Source/Tools/test_dsp.cpp"]),
    ("Measurements", ["docs/TEST-RESULTS.txt"]),
    ("VENDORED - another product's engine. DO NOT PROPOSE EDITS TO THESE",
        ["Source/Vendor/VENDOR.md",
         "Source/Vendor/FourColor/Dsp/TptFilters.h",
         "Source/Vendor/FourColor/Dsp/ColorEngine.h",
         "Source/Vendor/FourColor/Dsp/ColorEngine.cpp"]),
]

lang = {".h": "cpp", ".cpp": "cpp", ".txt": "text", ".md": "markdown"}
out = ["# EDGE - source appendix\n",
       f"Generated {datetime.date.today().isoformat()}. The real source, unabridged, "
       "plus the last full run of the measurement suite so every number in the prompt "
       "can be checked against its own output.\n"]

toc = ["- **{}** - {}".format(sec, ", ".join("`" + f.split("/")[-1] + "`" for f in files))
       for sec, files in order]
out.append("\n".join(toc) + "\n")

for section, files in order:
    out.append(f"\n---\n\n# {section}\n")
    for f in files:
        p = root / f
        if not p.exists():
            out.append(f"\n## `{f}` - MISSING\n")
            continue
        text = p.read_text()
        out.append(f"\n## `{f}`  ({len(text.splitlines())} lines)\n")
        out.append(f"```{lang.get(p.suffix, '')}\n{text}\n```\n")

pathlib.Path("outputs").mkdir(exist_ok=True)
doc = "".join(out)
pathlib.Path("outputs/EDGE-SOURCE-APPENDIX.md").write_text(doc)
print(f"outputs/EDGE-SOURCE-APPENDIX.md  ({len(doc)//1024} KB)")
PY

cp docs/REVIEW-PROMPT.md outputs/EDGE-REVIEW-PROMPT.md
echo "outputs/EDGE-REVIEW-PROMPT.md"
