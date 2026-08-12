#pragma once

#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "Ui/CurveView.h"
#include "Ui/ShapePanel.h"
#include "Ui/Theme.h"

//  The v0.12 layout:
//
//      [ header: preset | wordmark | BITE  BYPASS ]           48 px
//      [ graph, with the MODE segments floating inside ]      the rest
//      [ deck:  LOW      EDGE      HIGH    FOLLOW->EDGE ]     176 px
//
//  One floating inspector, opened by selection, closed by empty space or
//  Escape. No permanent bottom panel, no tabs.
class EdgeAudioProcessorEditor : public juce::AudioProcessorEditor,
                                 private juce::Timer
{
public:
    explicit EdgeAudioProcessorEditor (EdgeAudioProcessor&);
    ~EdgeAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    //  Test support: the suite drives the real view and the real inspector.
    edge::ui::CurveView& getCurveView() noexcept { return curve; }
    edge::ui::ShapePanel& getShapePanel() noexcept { return inspector; }
    bool isInspectorVisible() const noexcept { return inspector.isVisible(); }
    void openInspectorForTest (edge::ui::SelectedControl c) { openInspector (c); }
    void closeInspectorForTest() { closeInspector(); }
    juce::Rectangle<int> testModeSelectorBounds() const
    {
        return lpButton.getBounds().getUnion (freeButton.getBounds()).expanded (4);
    }

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    //  The two floating layers of the full-bleed construction. They sit above
    //  the graph in z-order and below every control; each paints its own
    //  glass and shadows. The header carries the wordmark and the reactive
    //  hairline; the dock carries the EDGE CUT corner.
    struct HeaderPanel : public juce::Component
    {
        juce::Colour hairline = edge::ui::colour::titanDark;
        void paint (juce::Graphics&) override;
    };

    struct DockPanel : public juce::Component
    {
        juce::String versionText;
        juce::Rectangle<int> edgeLabel, cleanRect, cutRect;   // dock-local
        float seamEnergy = 0.0f;      // real followEnv01, drives the seam only
        void paint (juce::Graphics&) override;
    };

    //  A deck knob: label above, value inside the knob itself.
    struct DeckKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag,
                              juce::Slider::NoTextBox };
        juce::Label caption;
        std::unique_ptr<SliderAttachment> attachment;

        void attach (juce::Component& parent, juce::AudioProcessorValueTreeState&,
                     const juce::String& id, const juce::String& text, juce::Colour accent);
        void place (juce::Rectangle<int> area, int diameter);
    };

    void timerCallback() override;
    void openInspector (edge::ui::SelectedControl);
    void closeInspector();
    void positionInspector();
    juce::String semanticHeaderFor (edge::ui::SelectedControl) const;
    void refreshPresetBox();

    EdgeAudioProcessor& edgeProcessor;
    edge::ui::Look look;
    edge::ui::CurveView curve;
    edge::ui::ShapePanel inspector;

    //  --- header ----------------------------------------------------------
    juce::Label presetTitle;
    juce::ComboBox presetBox;
    juce::TextButton prevPreset { "<" }, nextPreset { ">" };
    DeckKnob biteKnob;
    juce::ToggleButton bypassButton { "BYPASS" };
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    juce::TextButton characterButton { "WARM" };
    std::unique_ptr<juce::ParameterAttachment> characterAttachment;

    //  --- MODE, floating inside the graph -----------------------------------
    juce::TextButton lpButton { "LP" }, bandButton { "BAND" },
                     hpButton { "HP" }, freeButton { "FREE" };
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;

    //  --- deck --------------------------------------------------------------
    DeckKnob lowKnob, highKnob, followKnob, spreadKnob;
    DeckKnob outputKnob;                                    // header, 30 px
    juce::TextButton warmButton { "WARM" }, ironButton { "IRON" };
    juce::Slider edgeKnob { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
    std::unique_ptr<SliderAttachment> edgeAttachment;

    edge::ui::SelectedControl selected = edge::ui::SelectedControl::low;
    juce::Rectangle<int> edgeLabelArea;

    HeaderPanel headerPanel;
    DockPanel dockPanel;

    //  500 ms tooltips: parameter name and current value, nothing more.
    juce::TooltipWindow tooltipWindow { this, 500 };

    juce::ComponentBoundsConstrainer constrainer;
    juce::String versionText;
    std::unique_ptr<juce::ResizableCornerComponent> resizer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeAudioProcessorEditor)
};
