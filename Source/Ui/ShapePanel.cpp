#include "ShapePanel.h"

#include "../Core/ParameterIds.h"

namespace edge::ui
{
    void ShapePanel::Control::attach (juce::Component& parent,
                                      juce::AudioProcessorValueTreeState& state,
                                      const juce::String& id, const juce::String& text,
                                      juce::Colour accent)
    {
        slider.getProperties().set ("accent", (int) accent.getARGB());
        slider.getProperties().set ("ticks", true);
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 78, metric::pillRow);
        parent.addAndMakeVisible (slider);

        caption.setText (text, juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, colour::textDim);
        caption.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        parent.addAndMakeVisible (caption);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            state, id, slider);

        if (auto* p = state.getParameter (id))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    //  The same rhythm the main strip uses - knob, pill, caption - so the two
    //  halves of the plug-in line up with each other.
    void ShapePanel::Control::setBounds (juce::Rectangle<int> r)
    {
        caption.setBounds (r.removeFromBottom (metric::captionRow));
        r.removeFromBottom (metric::rowGap);
        slider.setBounds (r);
    }

    ShapePanel::ShapePanel (juce::AudioProcessorValueTreeState& s) : state (s)
    {
        auto title = [this] (juce::Label& l, const juce::String& text, juce::Colour c,
                             juce::Justification j)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (juce::FontOptions (font::title).withStyle ("Bold"));
            l.setColour (juce::Label::textColourId, c);
            l.setJustificationType (j);
            addAndMakeVisible (l);
        };

        title (lowTitle,    "LOW TARGET",  colour::low,  juce::Justification::centredLeft);
        title (highTitle,   "HIGH TARGET", colour::high, juce::Justification::centredRight);
        title (followTitle, "FOLLOW",      colour::text, juce::Justification::centred);

        lowDepth   .attach (*this, state, param::lowDepth,    "DEPTH",    colour::low);
        lowCurve   .attach (*this, state, param::lowCurve,    "CURVE",    colour::low);
        lowShoulder.attach (*this, state, param::lowShoulder, "SHOULDER", colour::low);
        lowReso    .attach (*this, state, param::lowReso,     "RESO",     colour::low);

        highDepth   .attach (*this, state, param::highDepth,    "DEPTH",    colour::high);
        highCurve   .attach (*this, state, param::highCurve,    "CURVE",    colour::high);
        highShoulder.attach (*this, state, param::highShoulder, "SHOULDER", colour::high);
        highReso    .attach (*this, state, param::highReso,     "RESO",     colour::high);

        followSens   .attach (*this, state, param::followSens,    "SENS",    colour::text);
        followAttack .attach (*this, state, param::followAttack,  "ATTACK",  colour::text);
        followRelease.attach (*this, state, param::followRelease, "RELEASE", colour::text);

        linkButton.getProperties().set ("accent", (int) colour::textBright.getARGB());
        linkButton.onClick = [this] { if (onLinkChanged) onLinkChanged(); };
        addAndMakeVisible (linkButton);
    }

    void ShapePanel::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        paintWell (g, r, 8.0f);

        //  A hairline between the target columns and the follower's row, so the
        //  panel reads as two ideas rather than eleven knobs.
        const float y = r.getBottom() - 78.0f;
        g.setColour (colour::panelEdge.withAlpha (0.7f));
        g.drawHorizontalLine ((int) y, r.getX() + 14.0f, r.getRight() - 14.0f);
    }

    void ShapePanel::resized()
    {
        auto r = getLocalBounds().reduced (14, 10);

        auto followRow = r.removeFromBottom (74);
        r.removeFromBottom (6);

        //  --- the two targets --------------------------------------------------
        auto titles = r.removeFromTop (14);
        lowTitle.setBounds (titles.removeFromLeft (titles.getWidth() / 2));
        highTitle.setBounds (titles);

        const int knob = juce::jlimit (52, 82, r.getWidth() / 9);
        const int gap = 5;

        auto lowArea  = r.removeFromLeft (knob * 4 + gap * 3);
        auto highArea = r.removeFromRight (knob * 4 + gap * 3);

        auto place = [knob, gap] (juce::Rectangle<int>& area, Control& c)
        {
            c.setBounds (area.removeFromLeft (knob));
            area.removeFromLeft (gap);
        };

        place (lowArea, lowDepth);
        place (lowArea, lowCurve);
        place (lowArea, lowShoulder);
        place (lowArea, lowReso);

        place (highArea, highDepth);
        place (highArea, highCurve);
        place (highArea, highShoulder);
        place (highArea, highReso);

        //  LINK sits between the two columns, where its meaning is obvious.
        linkButton.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 76), 24));

        //  --- the follower -----------------------------------------------------
        followTitle.setBounds (followRow.removeFromTop (13));

        auto centre = followRow.withSizeKeepingCentre (knob * 3 + gap * 2, followRow.getHeight());
        place (centre, followSens);
        place (centre, followAttack);
        place (centre, followRelease);
    }
}
