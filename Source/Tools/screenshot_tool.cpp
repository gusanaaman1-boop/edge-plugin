// Deterministic UI renderer.
//
//     build/EdgeShot_artefacts/<config>/EdgeShot ui-shots
//
// Builds the real editor over the real processor, sets parameters through the
// APVTS exactly as a host would, and renders the component to a PNG. No screen
// capture, no window server, no timing luck.

#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../Core/Presets.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    struct Shot
    {
        const char* name;
        int inspector;      // -1 none, else (int) edge::ui::SelectedControl
        int width, height;
        std::vector<std::pair<const char*, float>> values;
        bool feedAudio = true;
    };

    void setParam (EdgeAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    int render (const juce::File& outputDir)
    {
        const int W = edge::ui::metric::defaultWidth, H = edge::ui::metric::defaultHeight;

        //  The acceptance list from the v0.12 work order: LP and HP at EDGE 0,
        //  50 and 100; every inspector context; minimum, reference and maximum
        //  window sizes.
        const std::vector<Shot> shots =
        {
            { "lp-edge-000", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 0.0f } } },
            { "lp-edge-050", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 50.0f } } },
            { "lp-edge-100", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 100.0f } } },

            { "hp-edge-000", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::highPass },
                { edge::param::lowFreq, 300.0f }, { edge::param::edge, 0.0f } } },
            { "hp-edge-050", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::highPass },
                { edge::param::lowFreq, 300.0f }, { edge::param::edge, 50.0f } } },
            { "hp-edge-100", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::highPass },
                { edge::param::lowFreq, 300.0f }, { edge::param::edge, 100.0f } } },

            { "inspector-low", (int) edge::ui::SelectedControl::low, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "inspector-lp-high", (int) edge::ui::SelectedControl::high, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 3200.0f }, { edge::param::edge, 56.0f } } },
            { "inspector-mid", (int) edge::ui::SelectedControl::mid, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::midGain, 8.0f }, { edge::param::edge, 62.0f } } },
            { "inspector-follow", (int) edge::ui::SelectedControl::follow, W, H, {
                { edge::param::follow, 64.0f }, { edge::param::edge, 56.0f } } },

            { "size-min", -1, edge::ui::metric::minWidth, edge::ui::metric::minHeight, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "size-ref", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },
            { "size-max", -1, edge::ui::metric::maxWidth, edge::ui::metric::maxHeight, {
                { edge::param::mode, (float) (int) edge::Mode::band },
                { edge::param::edge, 62.0f } } },

            //  The two analyzer proofs: silence must show NOTHING (the muted
            //  host is expected to look like this), -18 dBFS noise must show
            //  the input spectrum through a closed filter.
            { "analyzer-muted", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 1200.0f }, { edge::param::edge, 70.0f } }, false },
            { "analyzer-pink", -1, W, H, {
                { edge::param::mode, (float) (int) edge::Mode::lowPass },
                { edge::param::highFreq, 1200.0f }, { edge::param::edge, 70.0f } }, true },
        };

        outputDir.createDirectory();

        for (const auto& shot : shots)
        {
            EdgeAudioProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            for (const auto& v : shot.values)
                setParam (processor, v.first, v.second);

            //  The editor has to exist BEFORE the audio runs: the analyser FIFO
            //  is only written while an editor is open, which is the whole point
            //  of that switch, so processing first left every shot with an empty
            //  spectrum.
            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            if (editor == nullptr)
            {
                std::fprintf (stderr, "no editor\n");
                return 1;
            }

            //  A few blocks of pink-ish noise, so the analyser trace and the
            //  follower's live position are real rather than empty.
            juce::Random rng (1234);
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            float lp = 0.0f;

            for (int block = 0; block < 40; ++block)
            {
                buf.clear();

                if (shot.feedAudio)
                {
                    for (int i = 0; i < 512; ++i)
                    {
                        const float white = rng.nextFloat() * 2.0f - 1.0f;
                        lp += 0.06f * (white - lp);
                        const float v = 0.35f * (lp * 2.2f + white * 0.35f)
                                      * 0.5f;                       // ~-18 dBFS
                        buf.setSample (0, i, v);
                        buf.setSample (1, i, v);
                    }
                }

                processor.processBlock (buf, midi);
            }

            //  Let the editor's 30 Hz timer actually run, so the analyser
            //  drains the FIFO and transforms it.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (250);

            editor->setSize (shot.width, shot.height);

            if (shot.inspector >= 0)
                if (auto* ed = dynamic_cast<EdgeAudioProcessorEditor*> (editor.get()))
                    ed->openInspectorForTest ((edge::ui::SelectedControl) shot.inspector);

            juce::Image image (juce::Image::ARGB, editor->getWidth(), editor->getHeight(), true);
            {
                juce::Graphics g (image);
                editor->paintEntireComponent (g, true);
            }

            const auto file = outputDir.getChildFile (juce::String (shot.name) + ".png");
            file.deleteFile();

            juce::FileOutputStream stream (file);
            if (! stream.openedOk() || ! juce::PNGImageFormat().writeImageToStream (image, stream))
            {
                std::fprintf (stderr, "could not write %s\n", file.getFullPathName().toRawUTF8());
                return 1;
            }

            std::printf ("wrote %s  (%d x %d)\n", file.getFullPathName().toRawUTF8(),
                         image.getWidth(), image.getHeight());
        }

        return 0;
    }
}

namespace
{
    //  The manual's parameter table, written FROM the plug-in rather than typed
    //  next to it. A table maintained by hand is a table that disagrees with the
    //  build within one release.
    int writeParameterTable (const juce::File& out)
    {
        EdgeAudioProcessor processor;

        juce::StringArray rows;
        rows.add ("| Control | ID | Range | Default |");
        rows.add ("|---|---|---|---|");

        for (auto* raw : processor.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr)
                continue;

            const auto& range = rp->getNormalisableRange();

            //  Shown the way the plug-in itself shows them, so the manual and
            //  the read-out cannot describe the same number differently.
            const auto low  = rp->getText (0.0f, 0);
            const auto high = rp->getText (1.0f, 0);
            const auto def  = rp->getText (rp->getDefaultValue(), 0);

            const bool discrete = rp->getNumSteps() > 0
                                    && rp->getNumSteps() < (int) juce::AudioProcessor::getDefaultNumParameterSteps();

            juce::String span = low + " … " + high;
            if (discrete && range.interval > 0.0f)
            {
                juce::StringArray items;
                for (int i = 0; i < rp->getNumSteps(); ++i)
                    items.add (rp->getText ((float) i / (float) juce::jmax (1, rp->getNumSteps() - 1), 0));

                items.removeDuplicates (false);
                span = items.joinIntoString (" / ");
            }

            rows.add ("| " + rp->getName (128) + " | `" + rp->paramID + "` | "
                        + span + " | " + def + " |");
        }

        rows.add ("");
        rows.add ("| # | Preset |");
        rows.add ("|---|---|");
        for (int i = 0; i < edge::kNumPresets; ++i)
            rows.add ("| " + juce::String (i) + " | " + edge::kPresets[i].name + " |");

        out.getParentDirectory().createDirectory();
        if (! out.replaceWithText (rows.joinIntoString ("\n") + "\n"))
        {
            std::fprintf (stderr, "could not write %s\n", out.getFullPathName().toRawUTF8());
            return 1;
        }

        std::printf ("wrote %s  (%d parameters, %d presets)\n",
                     out.getFullPathName().toRawUTF8(),
                     processor.getParameters().size(), edge::kNumPresets);
        return 0;
    }
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::StringArray args (argv + 1, juce::jmax (0, argc - 1));

    if (args.contains ("--parameter-table"))
    {
        const int i = args.indexOf ("--parameter-table");
        const juce::String path = i + 1 < args.size() ? args[i + 1]
                                                      : juce::String ("docs/PARAMETER-TABLE.md");
        return writeParameterTable (juce::File::getCurrentWorkingDirectory().getChildFile (path));
    }

    //  An unrecognised switch is a mistake, not a folder name. A stale build of
    //  this tool once took "--parameter-table" for an output directory and
    //  rendered seven PNGs into a folder of that name, which then went into a
    //  commit and a release zip.
    for (const auto& a : args)
    {
        if (a.startsWith ("--"))
        {
            std::fprintf (stderr, "unknown option: %s\n"
                                  "usage: EdgeShot [<output-dir>]\n"
                                  "       EdgeShot --parameter-table [<file.md>]\n",
                          a.toRawUTF8());
            return 2;
        }
    }

    const juce::File dir = args.size() > 0
        ? juce::File::getCurrentWorkingDirectory().getChildFile (args[0])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-shots");

    return render (dir);
}
