// SHAPE: everything that describes the TARGET, tucked behind a disclosure so
// the main view stays one gesture wide.
//
// The main view answers "how far, and how fast". SHAPE answers "into what".
// Nothing here belongs on the front panel: Depth, Curve, Shoulder and Resonance
// define where EDGE is travelling to, and you set them once per sound.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace edge::ui
{
    class ShapePanel : public juce::Component
    {
    public:
        ShapePanel (juce::AudioProcessorValueTreeState&);

        void paint (juce::Graphics&) override;
        void resized() override;

        //  UI-only gesture linking, exactly as before: it is NOT a parameter,
        //  so a host automating one frequency never finds the plug-in writing
        //  the other one back at it.
        bool isLinkEnabled() const noexcept { return linkButton.getToggleState(); }
        std::function<void()> onLinkChanged;

    private:
        struct Control
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                                  juce::Slider::TextBoxBelow };
            juce::Label caption;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

            void attach (juce::Component& parent, juce::AudioProcessorValueTreeState&,
                         const juce::String& id, const juce::String& text, juce::Colour accent);
            void setBounds (juce::Rectangle<int>);
        };

        juce::AudioProcessorValueTreeState& state;

        juce::Label lowTitle, highTitle, followTitle, midTitle;
        Control lowDepth, lowCurve, lowShoulder, lowReso;
        Control highDepth, highCurve, highShoulder, highReso;
        Control followSens, followAttack, followRelease;
        Control midFreq, midGain, midReso;

        juce::ToggleButton linkButton { "LINK" };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapePanel)
    };
}
