#include <memory>

#include "ShapePanel.h"

#include "../Core/ParameterIds.h"

namespace edge::ui
{
    void ShapePanel::Slot::create (juce::Component& parent)
    {
        slider.getProperties().set ("ticks", true);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, metric::pillRow);
        parent.addAndMakeVisible (slider);

        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, colour::textDim);
        caption.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        parent.addAndMakeVisible (caption);
    }

    void ShapePanel::Slot::point (juce::AudioProcessorValueTreeState& apvts, const char* id,
                                  const juce::String& text, juce::Colour accent)
    {
        //  Destroy the old attachment FIRST. Two attachments alive on one
        //  slider both write to it, and the second one to fire wins - so the
        //  knob would jump back to the previous band's value on any move.
        attachment.reset();

        const bool used = id != nullptr;
        slider.setVisible (used);
        caption.setVisible (used);

        if (! used)
            return;

        slider.getProperties().set ("accent", (int) accent.getARGB());
        caption.setText (text, juce::dontSendNotification);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, id, slider);

        if (auto* p = apvts.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    //  The same rhythm the main strip uses - knob, pill, caption - so the two
    //  halves of the plug-in line up with each other.
    void ShapePanel::Slot::setBounds (juce::Rectangle<int> r)
    {
        caption.setBounds (r.removeFromBottom (metric::captionRow));
        r.removeFromBottom (metric::rowGap);
        slider.setBounds (r);
    }

    ShapePanel::ShapePanel (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        for (auto& slot : slots)
            slot.create (*this);

        struct { juce::TextButton* button; Band band; juce::Colour accent; } tabs[] = {
            { &lowButton,    Band::low,    colour::low },
            { &midButton,    Band::mid,    colour::textBright },
            { &highButton,   Band::high,   colour::high },
            { &followButton, Band::follow, colour::text },
        };

        for (auto& t : tabs)
        {
            t.button->setClickingTogglesState (false);
            t.button->getProperties().set ("accent", (int) t.accent.getARGB());

            const auto b = t.band;
            t.button->onClick = [this, b]
            {
                setBand (b);
                if (onBandChanged)
                    onBandChanged (b);
            };

            addAndMakeVisible (*t.button);
        }

        linkButton.getProperties().set ("accent", (int) colour::textBright.getARGB());
        linkButton.onClick = [this] { if (onLinkChanged) onLinkChanged(); };
        addAndMakeVisible (linkButton);

        rebuildRow();
    }

    void ShapePanel::setBand (Band b)
    {
        if (b == band)
            return;

        band = b;
        rebuildRow();
        resized();
        repaint();
    }

    void ShapePanel::rebuildRow()
    {
        //  Five slots, and each band fills as many as it has. LOW and HIGH take
        //  the frequency with them, which they never had before - there was
        //  nowhere to put it, and it had to be dragged on the display.
        struct Entry { const char* id; const char* caption; };
        Entry row[numSlots] = {};
        juce::Colour accent = colour::text;

        switch (band)
        {
            case Band::low:
                row[0] = { param::lowFreq,     "FREQ" };
                row[1] = { param::lowDepth,    "DEPTH" };
                row[2] = { param::lowCurve,    "CURVE" };
                row[3] = { param::lowShoulder, "SHOULDER" };
                row[4] = { param::lowReso,     "RESO" };
                accent = colour::low;
                break;

            case Band::high:
                row[0] = { param::highFreq,     "FREQ" };
                row[1] = { param::highDepth,    "DEPTH" };
                row[2] = { param::highCurve,    "CURVE" };
                row[3] = { param::highShoulder, "SHOULDER" };
                row[4] = { param::highReso,     "RESO" };
                accent = colour::high;
                break;

            //  The MID band is neither edge, so it is drawn in the neutral
            //  colour: a third accent would stop "orange means low, cyan means
            //  high" from being true.
            case Band::mid:
                row[0] = { param::midFreq, "FREQ" };
                row[1] = { param::midGain, "GAIN" };
                row[2] = { param::midReso, "RESO" };
                accent = colour::textBright;
                break;

            case Band::follow:
                row[0] = { param::followSens,    "SENS" };
                row[1] = { param::followAttack,  "ATTACK" };
                row[2] = { param::followRelease, "RELEASE" };
                accent = colour::text;
                break;
        }

        for (int i = 0; i < numSlots; ++i)
            slots[i].point (state, row[i].id, row[i].caption, accent);

        lowButton.setToggleState    (band == Band::low,    juce::dontSendNotification);
        midButton.setToggleState    (band == Band::mid,    juce::dontSendNotification);
        highButton.setToggleState   (band == Band::high,   juce::dontSendNotification);
        followButton.setToggleState (band == Band::follow, juce::dontSendNotification);

        //  LINK couples the two edge frequencies. It means nothing on MID or on
        //  the follower, so it is not offered there.
        linkButton.setVisible (band == Band::low || band == Band::high);
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

        //  --- the shared row ----------------------------------------------------
        //  Centred on the panel rather than left-packed, so switching from a
        //  five-knob band to a three-knob one does not leave a hole on the right.
        int used = 0;
        for (auto& slot : slots)
            if (slot.slider.isVisible())
                ++used;

        //  Sized from the HEIGHT, not the width. The row is five knobs wide at
        //  most and the panel is far wider than that, so a width-derived size
        //  produced knobs floating in the middle of a tall empty box - which is
        //  exactly what it looked like. This makes them the same size as the
        //  ones in the main strip, which is the point of a shared rhythm.
        const int forHeight = r.getHeight() - metric::pillRow - metric::rowGap
                                            - metric::captionRow;
        const int knob = juce::jlimit (46, 78,
                                       juce::jmin (forHeight,
                                                   r.getWidth() / (numSlots + 1)));
        constexpr int gap = 14;

        const int rowHeight = knob + metric::pillRow + metric::rowGap + metric::captionRow;
        auto row = r.withSizeKeepingCentre (knob * used + gap * juce::jmax (0, used - 1),
                                            juce::jmin (r.getHeight(), rowHeight));

        for (auto& slot : slots)
        {
            if (! slot.slider.isVisible())
                continue;

            slot.setBounds (row.removeFromLeft (knob));
            row.removeFromLeft (gap);
        }
    }
}
