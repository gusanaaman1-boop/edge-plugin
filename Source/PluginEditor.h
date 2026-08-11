#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Ui/CurveView.h"
#include "Ui/Theme.h"

class EdgeAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit EdgeAudioProcessorEditor (EdgeAudioProcessor&);
    ~EdgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    //  A knob plus its caption, so the layout code never positions two things
    //  that have to stay together.
    struct Control
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
        juce::Label caption;
        std::unique_ptr<SliderAttachment> attachment;

        void attach (juce::Component& parent, juce::AudioProcessorValueTreeState& state,
                     const juce::String& id, const juce::String& text, juce::Colour accent);
        void setBounds (juce::Rectangle<int>);
    };

    EdgeAudioProcessor& edgeProcessor;
    edge::ui::Look look;
    edge::ui::CurveView curve;

    juce::Label title, lowLabel, highLabel;

    Control lowCurveCtl, lowShoulderCtl, lowResCtl,
            highCurveCtl, highShoulderCtl, highResCtl,
            focusCtl, outputCtl;

    juce::ToggleButton linkButton { "LINK" }, bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> linkAttachment, bypassAttachment;

    //  Link is a GESTURE coupling, applied when the user moves a handle or a
    //  frequency in the editor. It is deliberately not applied to incoming host
    //  automation: having the processor write parameters back to the host turns
    //  one automated lane into two fighting ones.
    int controlStripHeight() const noexcept;
    void installLinkCoupling();
    void applyLink (bool lowMoved);

    float lastLowFreq = 0.0f, lastHighFreq = 0.0f;
    bool applyingLink = false;
    std::unique_ptr<juce::ParameterAttachment> freqWatcherLow, freqWatcherHigh;

    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeAudioProcessorEditor)
};
