// The floating inspector.
//
// There is exactly one, and it has no tabs: the selection decides its context.
// Touch a handle on the graph and it opens beside it with that band's
// controls; touch the FOLLOW knob and it shows the detector. Click empty
// graph space or press Escape and it goes away.
//
// Every context's sliders and APVTS attachments are built ONCE, in the
// constructor. Switching context flips child visibility and nothing else -
// the acceptance test counts attachment constructions across a hundred
// switches and expects zero.

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace edge::ui
{
    //  The five musical slopes, as a stepped selector. It NEVER shows a
    //  percentage: any continuous value the parameter holds highlights the
    //  nearest choice, and only a deliberate user gesture - click, wheel,
    //  arrow key - writes a calibrated value back.
    class SlopeSelector : public juce::Component
    {
    public:
        SlopeSelector (juce::RangedAudioParameter&, juce::Colour accent);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        int currentNominal() const noexcept { return nominal; }

    private:
        void choose (int index);       // a USER gesture: writes the parameter

        juce::ParameterAttachment attachment;
        juce::Colour accent;
        int nominal = 24;              // highlighted choice, follows the value

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlopeSelector)
    };

    //  The one selection shared by the graph and the inspector. The editor
    //  owns the current value; both views are told about changes and neither
    //  keeps its own copy of any parameter.
    enum class SelectedControl { low = 0, high, mid, follow };
    inline constexpr int kNumSelectable = 4;

    class ShapePanel : public juce::Component
    {
    public:
        explicit ShapePanel (juce::AudioProcessorValueTreeState&);

        void paint (juce::Graphics&) override;
        void resized() override;

        //  Context + semantic header. In LP the high edge IS the low-pass, and
        //  the header must say "LP" - the internal name never reaches the user.
        void setContext (SelectedControl, const juce::String& headerText);
        SelectedControl getSelected() const noexcept { return selected; }

        //  The size this context wants, from the v0.14 spec: one fixed strip,
        //  420 wide (330 for MID), 88 tall. No notch - the position is fixed,
        //  so there is nothing to point at.
        juce::Point<int> preferredSize() const noexcept;

        //  Test support -------------------------------------------------------
        juce::Slider* sliderFor (const juce::String& paramID) noexcept;
        SlopeSelector* slopeSelectorFor (SelectedControl) noexcept;
        static int attachmentConstructions() noexcept { return attachmentCount; }

    private:
        struct Knob
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                                  juce::Slider::NoTextBox };
            juce::Label caption, value;
            juce::String paramID;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

            void init (juce::Component& parent, juce::AudioProcessorValueTreeState&,
                       const char* id, const juce::String& text, juce::Colour accent);
            void setBounds (juce::Rectangle<int>);
        };

        struct ContextPanel : public juce::Component
        {
            std::vector<std::unique_ptr<Knob>> knobs;
            juce::Rectangle<int> slopeArea;       // reserved for the selector
            void paint (juce::Graphics&) override;
            void resized() override;
        };

        juce::AudioProcessorValueTreeState& state;
        SelectedControl selected = SelectedControl::low;
        juce::String header;
        juce::Colour headerColour = colour::text;

        std::array<ContextPanel, (size_t) kNumSelectable> panels;
        std::unique_ptr<SlopeSelector> lowSlope, highSlope;
        juce::Rectangle<int> slopeArea;           // active selector, panel coords

        static int attachmentCount;

        ContextPanel& panelFor (SelectedControl c) noexcept { return panels[(size_t) c]; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapePanel)
    };
}
