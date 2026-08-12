#include <memory>

#include "ShapePanel.h"

#include "../Core/ParameterIds.h"

namespace edge::ui
{
    void ShapePanel::Knob::init (juce::Component& parent,
                                 juce::AudioProcessorValueTreeState& apvts,
                                 const char* id, const juce::String& text,
                                 juce::Colour accent)
    {
        paramID = id;

        slider.getProperties().set ("ticks", true);
        slider.getProperties().set ("accent", (int) accent.getARGB());
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, metric::pillRow);
        parent.addAndMakeVisible (slider);

        caption.setText (text, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, colour::textDim);
        caption.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        parent.addAndMakeVisible (caption);

        //  Built once, for the life of the editor. APVTS stays the single
        //  source of truth; the attachment is only the wire.
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, paramID, slider);

        if (auto* p = apvts.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    //  The same rhythm the main strip uses - knob, pill, caption - so the two
    //  halves of the plug-in line up with each other.
    void ShapePanel::Knob::setBounds (juce::Rectangle<int> r)
    {
        caption.setBounds (r.removeFromBottom (metric::captionRow));
        r.removeFromBottom (metric::rowGap);
        slider.setBounds (r);
    }

    void ShapePanel::BandPanel::resized()
    {
        auto r = getLocalBounds();

        //  Sized from the HEIGHT so these knobs match the main strip's, and
        //  centred so a three-knob panel does not leave a hole on the right.
        const int forHeight = r.getHeight() - metric::pillRow - metric::rowGap
                                            - metric::captionRow;
        const int knob = juce::jlimit (46, 78,
                                       juce::jmin (forHeight, r.getWidth() / 6));
        constexpr int gap = 14;

        const int used = (int) knobs.size();
        const int rowHeight = knob + metric::pillRow + metric::rowGap + metric::captionRow;

        auto row = r.withSizeKeepingCentre (knob * used + gap * juce::jmax (0, used - 1),
                                            juce::jmin (r.getHeight(), rowHeight));

        for (auto& k : knobs)
        {
            k->setBounds (row.removeFromLeft (knob));
            row.removeFromLeft (gap);
        }
    }

    ShapePanel::ShapePanel (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        //  All four panels and every attachment exist from here on. Selection
        //  is pure visibility.
        struct Entry { const char* id; const char* caption; };

        auto buildPanel = [this] (SelectedControl which,
                                  std::initializer_list<Entry> entries, juce::Colour accent)
        {
            auto& panel = panelFor (which);

            for (const auto& e : entries)
            {
                auto k = std::make_unique<Knob>();
                k->init (panel, state, e.id, e.caption, accent);
                panel.knobs.push_back (std::move (k));
            }

            addChildComponent (panel);
        };

        buildPanel (SelectedControl::low,
                    { { param::lowFreq, "FREQ" }, { param::lowDepth, "DEPTH" },
                      { param::lowCurve, "CURVE" }, { param::lowShoulder, "SHOULDER" },
                      { param::lowReso, "RESO" } },
                    colour::low);

        buildPanel (SelectedControl::high,
                    { { param::highFreq, "FREQ" }, { param::highDepth, "DEPTH" },
                      { param::highCurve, "CURVE" }, { param::highShoulder, "SHOULDER" },
                      { param::highReso, "RESO" } },
                    colour::high);

        //  The MID band is neither edge, so it is drawn in the neutral colour:
        //  a third accent would stop "orange means low, cyan means high" from
        //  being true. The follower likewise.
        buildPanel (SelectedControl::mid,
                    { { param::midFreq, "FREQ" }, { param::midGain, "GAIN" },
                      { param::midReso, "RESO" } },
                    colour::textBright);

        buildPanel (SelectedControl::follow,
                    { { param::followSens, "SENS" }, { param::followAttack, "ATTACK" },
                      { param::followRelease, "RELEASE" } },
                    colour::text);

        struct Tab { juce::TextButton* button; SelectedControl which; juce::Colour accent; };
        const Tab tabs[] = {
            { &lowButton,    SelectedControl::low,    colour::low },
            { &midButton,    SelectedControl::mid,    colour::textBright },
            { &highButton,   SelectedControl::high,   colour::high },
            { &followButton, SelectedControl::follow, colour::text },
        };

        for (const auto& t : tabs)
        {
            t.button->setClickingTogglesState (false);
            t.button->getProperties().set ("accent", (int) t.accent.getARGB());

            const auto which = t.which;
            t.button->onClick = [this, which]
            {
                setSelected (which);
                if (onSelectionChanged)
                    onSelectionChanged (which);
            };

            addAndMakeVisible (*t.button);
        }

        linkButton.getProperties().set ("accent", (int) colour::textBright.getARGB());
        linkButton.onClick = [this] { if (onLinkChanged) onLinkChanged(); };
        addAndMakeVisible (linkButton);

        setSelected (SelectedControl::low);
        panelFor (SelectedControl::low).setVisible (true);
    }

    void ShapePanel::setSelected (SelectedControl which)
    {
        //  Visibility only. Nothing is destroyed, nothing is allocated, and a
        //  selection change in the middle of a drag cannot touch the gesture.
        selected = which;

        for (int i = 0; i < kNumSelectable; ++i)
            panels[(size_t) i].setVisible (i == (int) which);

        lowButton.setToggleState    (which == SelectedControl::low,    juce::dontSendNotification);
        midButton.setToggleState    (which == SelectedControl::mid,    juce::dontSendNotification);
        highButton.setToggleState   (which == SelectedControl::high,   juce::dontSendNotification);
        followButton.setToggleState (which == SelectedControl::follow, juce::dontSendNotification);

        //  LINK couples the two edge frequencies. It means nothing on MID or on
        //  the follower, so it is not offered there.
        linkButton.setVisible (which == SelectedControl::low || which == SelectedControl::high);

        repaint();
    }

    juce::Slider* ShapePanel::sliderFor (const juce::String& paramID) noexcept
    {
        for (auto& panel : panels)
            for (auto& k : panel.knobs)
                if (k->paramID == paramID)
                    return &k->slider;

        return nullptr;
    }

    void ShapePanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        paintWell (g, r, 8.0f);
    }

    void ShapePanel::resized()
    {
        auto r = getLocalBounds().reduced (14, 8);

        //  --- the selector, left, on its own line -------------------------------
        auto top = r.removeFromTop (24);

        auto tabs = top.removeFromLeft (juce::jmin (top.getWidth() - 90, 4 * 74));
        const int tabWidth = tabs.getWidth() / 4;
        lowButton.setBounds    (tabs.removeFromLeft (tabWidth).reduced (2, 0));
        midButton.setBounds    (tabs.removeFromLeft (tabWidth).reduced (2, 0));
        highButton.setBounds   (tabs.removeFromLeft (tabWidth).reduced (2, 0));
        followButton.setBounds (tabs.reduced (2, 0));

        linkButton.setBounds (top.removeFromRight (juce::jmin (top.getWidth(), 78))
                                 .withSizeKeepingCentre (78, 22));

        r.removeFromTop (6);

        //  All four panels share the same rectangle; only one is visible.
        for (auto& panel : panels)
            panel.setBounds (r);
    }
}
