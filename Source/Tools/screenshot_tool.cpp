// Deterministic UI renderer.
//
//     build/EdgeShot_artefacts/<config>/EdgeShot ui-shots
//
// Builds the real editor over the real processor, sets parameters through the
// APVTS exactly as a host would, and renders the component to a PNG. No screen
// capture, no window server, no timing luck.

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    struct Shot
    {
        const char* name;
        bool shapeOpen;
        std::vector<std::pair<const char*, float>> values;
    };

    void setParam (EdgeAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    int render (const juce::File& outputDir)
    {
        const std::vector<Shot> shots =
        {
            { "01-neutral", false, {} },

            { "02-band-open", false, {
                { edge::param::edge,      100.0f },
                { edge::param::lowFreq,   180.0f }, { edge::param::highFreq, 7000.0f },
                { edge::param::lowCurve,   75.0f }, { edge::param::highCurve,  75.0f },
                { edge::param::lowReso,    30.0f }, { edge::param::highReso,   22.0f },
                { edge::param::bite,       45.0f } } },

            { "03-lowpass", false, {
                { edge::param::mode,      (float) (int) edge::Mode::lowPass },
                { edge::param::edge,       78.0f },
                { edge::param::highFreq, 2600.0f }, { edge::param::highCurve, 100.0f },
                { edge::param::highReso,   55.0f },
                { edge::param::highShoulder, 40.0f },
                { edge::param::bite,       60.0f } } },

            { "04-follow-spread", false, {
                { edge::param::edge,       55.0f },
                { edge::param::follow,     70.0f },
                { edge::param::spread,     60.0f },
                { edge::param::lowFreq,   260.0f }, { edge::param::highFreq, 4800.0f },
                { edge::param::bite,       35.0f } } },

            { "05-free-band", false, {
                { edge::param::mode,      (float) (int) edge::Mode::freeBand },
                { edge::param::edge,      100.0f },
                { edge::param::lowFreq,   700.0f }, { edge::param::highFreq, 2800.0f },
                { edge::param::follow,     60.0f },
                { edge::param::character, (float) (int) edge::Character::iron },
                { edge::param::bite,       70.0f } } },

            { "06-shape-open", true, {
                { edge::param::edge,       90.0f },
                { edge::param::lowFreq,   140.0f }, { edge::param::highFreq, 9000.0f },
                { edge::param::lowShoulder, 55.0f }, { edge::param::highShoulder, 70.0f },
                { edge::param::lowReso,    40.0f }, { edge::param::highReso,   35.0f },
                { edge::param::follow,    -45.0f },
                { edge::param::bite,       50.0f } } },
        };

        outputDir.createDirectory();

        for (const auto& shot : shots)
        {
            EdgeAudioProcessor processor;
            processor.shapeOpen.store (shot.shapeOpen);
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
                for (int i = 0; i < 512; ++i)
                {
                    const float white = rng.nextFloat() * 2.0f - 1.0f;
                    lp += 0.06f * (white - lp);
                    const float v = 0.35f * (lp * 2.2f + white * 0.35f);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                }

                processor.processBlock (buf, midi);
            }

            //  Let the editor's 30 Hz timer actually run, so the analyser
            //  drains the FIFO and transforms it.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (250);

            const int h = edge::ui::metric::defaultHeight
                        + (shot.shapeOpen ? edge::ui::metric::shapeHeight : 0);
            editor->setSize (edge::ui::metric::defaultWidth, h);

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

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI init;

    const juce::File dir = argc > 1
        ? juce::File::getCurrentWorkingDirectory().getChildFile (argv[1])
        : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-shots");

    return render (dir);
}
