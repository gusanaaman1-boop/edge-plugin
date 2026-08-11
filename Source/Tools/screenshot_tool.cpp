// Deterministic UI renderer.
//
//     build/EdgeShot_artefacts/<config>/EdgeShot ui-shots
//
// Builds the real editor over the real processor, sets parameters through the
// APVTS exactly as a host would, and renders the component to a PNG. No
// screen capture, no window server, no timing luck.

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    struct Shot
    {
        const char* name;
        std::vector<std::pair<const char*, float>> values;
    };

    void setParam (EdgeAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    int render (const juce::File& outputDir)
    {
        const Shot shots[] =
        {
            { "01-default", {} },
            { "02-both-edges", {
                { edge::param::lowFreq,   90.0f },  { edge::param::lowDepth,  62.0f },
                { edge::param::lowCurve,  70.0f },  { edge::param::lowRes,    35.0f },
                { edge::param::highFreq, 6500.0f }, { edge::param::highDepth, 55.0f },
                { edge::param::highCurve, 30.0f },  { edge::param::highRes,   20.0f } } },
            { "03-full-cut", {
                { edge::param::lowFreq,  240.0f },  { edge::param::lowDepth, 100.0f },
                { edge::param::lowCurve, 100.0f },  { edge::param::lowRes,    70.0f },
                { edge::param::highFreq, 2400.0f }, { edge::param::highDepth, 100.0f },
                { edge::param::highCurve, 100.0f }, { edge::param::highRes,   55.0f },
                { edge::param::focus,     40.0f } } },
            { "05-shoulder", {
                { edge::param::highFreq, 9000.0f }, { edge::param::highDepth,   100.0f },
                { edge::param::highCurve,  75.0f }, { edge::param::highShoulder, 55.0f },
                { edge::param::lowFreq,     40.0f }, { edge::param::lowDepth,     45.0f },
                { edge::param::lowCurve,    50.0f } } },
            { "04-gentle-shelves", {
                { edge::param::lowFreq,  180.0f },  { edge::param::lowDepth,  28.0f },
                { edge::param::lowCurve,   0.0f },
                { edge::param::highFreq, 9000.0f }, { edge::param::highDepth, 25.0f },
                { edge::param::highCurve,  0.0f },
                { edge::param::output,     2.0f },
                { edge::param::link,       1.0f } } },
        };

        outputDir.createDirectory();

        for (const auto& shot : shots)
        {
            EdgeAudioProcessor processor;
            processor.prepareToPlay (48000.0, 512);

            for (const auto& v : shot.values)
                setParam (processor, v.first, v.second);

            //  One block, so the engine's display shape is the live one rather
            //  than whatever prepare() left behind.
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            buf.clear();
            processor.processBlock (buf, midi);

            std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
            if (editor == nullptr)
            {
                std::fprintf (stderr, "no editor\n");
                return 1;
            }

            editor->setSize (edge::ui::metric::defaultWidth, edge::ui::metric::defaultHeight);

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
