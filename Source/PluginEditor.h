#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Ui/CurveView.h"
#include "Ui/ShapePanel.h"
#include "Ui/Theme.h"

class EdgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit EdgeAudioProcessorEditor (EdgeAudioProcessor&);
    ~EdgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::TextBoxBelow };
        juce::Label caption;
        std::unique_ptr<SliderAttachment> attachment;
        juce::Rectangle<int> markArea;

        void attach (juce::Component& parent, juce::AudioProcessorValueTreeState&,
                     const juce::String& id, const juce::String& text,
                     juce::Colour accent, bool bipolar);
        void setBounds (juce::Rectangle<int>);
        static int heightFor (int knobSize) noexcept;
    };

    void timerCallback() override;
    void setShapeOpen (bool shouldBeOpen);
    void applyLink (bool lowMoved);
    void installLinkCoupling();
    int  controlStripHeight() const noexcept;

    EdgeAudioProcessor& edgeProcessor;
    edge::ui::Look look;
    edge::ui::CurveView curve;
    edge::ui::ShapePanel shape;

    //  The one control the whole product is named after.
    juce::Slider edgeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<SliderAttachment> edgeAttachment;

    Knob followKnob, spreadKnob, biteKnob, outputKnob;

    juce::TextButton lpButton { "LP" }, bandButton { "BAND" },
                     hpButton { "HP" }, freeButton { "FREE" };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    juce::TextButton shapeButton { "SHAPE" };
    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;

    //  CHARACTER: one two-way switch, next to the lamp it lights.
    juce::TextButton characterButton { "WARM" };
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    juce::Rectangle<int> warmLampArea;
    bool warmLit = false;

    //  LINK is an editor gesture, not a parameter.
    float lastLowFreq = 0.0f, lastHighFreq = 0.0f;
    bool applyingLink = false;
    std::unique_ptr<juce::ParameterAttachment> freqWatcherLow, freqWatcherHigh;

    juce::ComponentBoundsConstrainer constrainer;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeAudioProcessorEditor)
};
