// SHAPE: one set of knobs, pointed at whichever band you have selected.
//
// It used to be sixteen knobs in four labelled blocks - LOW TARGET, HIGH
// TARGET, MID, FOLLOW - all on screen at once. Nobody needs to see DEPTH for
// the low edge and DEPTH for the high edge at the same time; they need to see
// DEPTH for the edge they are working on.
//
// So there is ONE row now. Clicking LOW, MID, HIGH or FOLLOW - here, or on the
// handle on the display itself - repoints that row. Fewer than half the pixels,
// and the panel no longer competes with the curve for attention.

#pragma once

#include <functional>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "Theme.h"

namespace edge::ui
{
    //  Which set of parameters the shared row is currently driving.
    enum class Band { low = 0, mid, high, follow };

    class ShapePanel : public juce::Component
    {
    public:
        ShapePanel (juce::AudioProcessorValueTreeState&);

        void paint (juce::Graphics&) override;
        void resized() override;

        void setBand (Band);
        Band getBand() const noexcept { return band; }

        //  Fired when the user picks a band HERE, so the display can highlight
        //  the same handle.
        std::function<void (Band)> onBandChanged;

        //  UI-only gesture linking, exactly as before: it is NOT a parameter,
        //  so a host automating one frequency never finds the plug-in writing
        //  the other one back at it.
        bool isLinkEnabled() const noexcept { return linkButton.getToggleState(); }
        std::function<void()> onLinkChanged;

        //  The height this panel needs. One row instead of four blocks, so the
        //  display gets the difference back.
        static constexpr int preferredHeight = 156;

    private:
        //  A slot in the shared row. It owns no parameter of its own - point()
        //  re-aims it, which is the whole idea.
        struct Slot
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                                  juce::Slider::TextBoxBelow };
            juce::Label caption;
            std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

            void create (juce::Component& parent);

            //  A null id empties the slot: the row is five wide and MID only
            //  uses three of it.
            void point (juce::AudioProcessorValueTreeState&, const char* id,
                        const juce::String& text, juce::Colour accent);

            void setBounds (juce::Rectangle<int>);
        };

        static constexpr int numSlots = 5;

        juce::AudioProcessorValueTreeState& state;
        Band band = Band::low;

        Slot slots[numSlots];
        juce::TextButton lowButton { "LOW" }, midButton { "MID" },
                         highButton { "HIGH" }, followButton { "FOLLOW" };
        juce::ToggleButton linkButton { "LINK" };

        void rebuildRow();

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShapePanel)
    };
}
