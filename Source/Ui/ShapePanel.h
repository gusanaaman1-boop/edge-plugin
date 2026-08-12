// SHAPE: a contextual inspector. One row of knobs on screen, four prebuilt
// panels behind it.
//
// The selection - LOW, HIGH, MID or FOLLOW - decides which panel is visible.
// Every panel's sliders and their APVTS attachments are built ONCE, in the
// constructor, and switching selection only flips visibility. The previous
// version destroyed and recreated attachments on every switch, which meant a
// selection change during a gesture reallocated inside the gesture.

#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace edge::ui
{
    //  The one selection shared by the graph and the inspector. The editor owns
    //  the current value; both views are told about changes and neither keeps
    //  its own copy of any parameter.
    enum class SelectedControl { low = 0, high, mid, follow };
    inline constexpr int kNumSelectable = 4;

    class ShapePanel : public juce::Component
    {
    public:
        explicit ShapePanel (juce::AudioProcessorValueTreeState&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void setSelected (SelectedControl);
        SelectedControl getSelected() const noexcept { return selected; }

        //  Fired when the user picks a band HERE, so the editor can tell the
        //  display to highlight the same handle.
        std::function<void (SelectedControl)> onSelectionChanged;

        //  UI-only gesture linking, exactly as before: it is NOT a parameter,
        //  so a host automating one frequency never finds the plug-in writing
        //  the other one back at it.
        bool isLinkEnabled() const noexcept { return linkButton.getToggleState(); }
        std::function<void()> onLinkChanged;

        static constexpr int preferredHeight = 156;

        //  Test support: the slider attached to `paramID`, or null if no panel
        //  owns that parameter. The control matrix uses this to assert that a
        //  host write actually reaches the knob on screen.
        juce::Slider* sliderFor (const juce::String& paramID) noexcept;

    private:
        struct Knob
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                                  juce::Slider::TextBoxBelow };
            juce::Label caption;
            juce::String paramID;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

            void init (juce::Component& parent, juce::AudioProcessorValueTreeState&,
                       const char* id, const juce::String& text, juce::Colour accent);
            void setBounds (juce::Rectangle<int>);
        };

        //  One band's whole row. Built once; shown or hidden, never rebuilt.
        struct BandPanel : public juce::Component
        {
            std::vector<std::unique_ptr<Knob>> knobs;
            void resized() override;
        };

        juce::AudioProcessorValueTreeState& state;
        SelectedControl selected = SelectedControl::low;

        std::array<BandPanel, (size_t) kNumSelectable> panels;
        juce::TextButton lowButton { "LOW" }, midButton { "MID" },
                         highButton { "HIGH" }, followButton { "FOLLOW" };
        juce::ToggleButton linkButton { "LINK" };

        BandPanel& panelFor (SelectedControl c) noexcept { return panels[(size_t) c]; }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapePanel)
    };
}
