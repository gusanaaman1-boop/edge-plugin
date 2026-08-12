#include <memory>

#include "ShapePanel.h"

#include "../Core/ParameterIds.h"

namespace edge::ui
{
    int ShapePanel::attachmentCount = 0;

    namespace
    {
        //  Inspector geometry, v0.13: a one-row instrument strip, not a modal.
        constexpr int kPadding = 12;
        constexpr int kGap = 8;
        constexpr int kHeaderRow = 18;
        constexpr int kNotchW = 10, kNotchH = 6;
        constexpr int kCellW = 59;
        constexpr int kMiniKnob = 30;
        constexpr int kStripH = 92;
    }

    void ShapePanel::Knob::init (juce::Component& parent,
                                 juce::AudioProcessorValueTreeState& apvts,
                                 const char* id, const juce::String& text,
                                 juce::Colour accent)
    {
        paramID = id;

        slider.getProperties().set ("accent", (int) accent.getARGB());
        slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        parent.addAndMakeVisible (slider);

        caption.setText (text.toUpperCase(), juce::dontSendNotification);
        caption.setJustificationType (juce::Justification::centred);
        caption.setColour (juce::Label::textColourId, colour::textDim);
        caption.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
        parent.addAndMakeVisible (caption);

        value.setJustificationType (juce::Justification::centred);
        value.setColour (juce::Label::textColourId, colour::text);
        value.setFont (juce::FontOptions (10.0f));
        parent.addAndMakeVisible (value);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            apvts, paramID, slider);
        ++attachmentCount;

        auto* sliderPtr = &slider;
        auto* valuePtr = &value;
        slider.onValueChange = [sliderPtr, valuePtr]
        {
            valuePtr->setText (sliderPtr->getTextFromValue (sliderPtr->getValue()),
                               juce::dontSendNotification);
        };
        slider.onValueChange();

        if (auto* p = apvts.getParameter (paramID))
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
    }

    //  One horizontal CELL per control: 9 px label above a 30 px mini-knob,
    //  the exact value under it. Four cells in a row read as an instrument
    //  strip; four tall rows read as a dialog.
    void ShapePanel::Knob::setBounds (juce::Rectangle<int> r)
    {
        caption.setBounds (r.removeFromTop (10));
        value.setBounds (r.removeFromBottom (12));
        slider.setBounds (r.withSizeKeepingCentre (kMiniKnob, juce::jmin (r.getHeight(), kMiniKnob)));
    }

    void ShapePanel::ContextPanel::resized()
    {
        auto r = getLocalBounds();
        const int used = (int) knobs.size();
        if (used == 0)
            return;

        auto row = r.withSizeKeepingCentre (kCellW * used + kGap * (used - 1), r.getHeight());

        for (auto& k : knobs)
        {
            k->setBounds (row.removeFromLeft (kCellW));
            row.removeFromLeft (kGap);
        }
    }

    ShapePanel::ShapePanel (juce::AudioProcessorValueTreeState& s) : state (s)
    {
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

        //  Contents per context, from the spec. Frequencies live on the graph
        //  handles and the deck knobs, not here.
        buildPanel (SelectedControl::low,
                    { { param::lowDepth, "DEPTH" }, { param::lowCurve, "CURVE" },
                      { param::lowReso, "RESO" }, { param::lowShoulder, "SHOULDER" } },
                    colour::low);

        buildPanel (SelectedControl::high,
                    { { param::highDepth, "DEPTH" }, { param::highCurve, "CURVE" },
                      { param::highReso, "RESO" }, { param::highShoulder, "SHOULDER" } },
                    colour::high);

        buildPanel (SelectedControl::mid,
                    { { param::midFreq, "FREQUENCY" }, { param::midGain, "GAIN" },
                      { param::midReso, "WIDTH" } },
                    colour::text);

        //  FOLLOW is movement, and movement is violet - the one place the
        //  colour is allowed.
        buildPanel (SelectedControl::follow,
                    { { param::follow, "AMOUNT" }, { param::followAttack, "ATTACK" },
                      { param::followRelease, "RELEASE" }, { param::followSens, "SENS" } },
                    colour::movement);

        setContext (SelectedControl::low, "LOW");
    }

    juce::Point<int> ShapePanel::preferredSize() const noexcept
    {
        //  Width follows the cell count; the height is one strip everywhere.
        const int cells = (int) panels[(size_t) selected].knobs.size();
        const int w = kPadding * 2 + kCellW * cells + kGap * (cells - 1);

        return { juce::jmax (w, 200), kStripH + kNotchH };
    }

    void ShapePanel::setContext (SelectedControl which, const juce::String& headerText)
    {
        selected = which;
        header = headerText;

        headerColour = which == SelectedControl::low    ? colour::low
                     : which == SelectedControl::high   ? colour::high
                     : which == SelectedControl::follow ? colour::movement
                                                        : colour::text;

        //  Visibility only. The acceptance test counts attachment
        //  constructions across context switches and expects zero.
        for (int i = 0; i < kNumSelectable; ++i)
            panels[(size_t) i].setVisible (i == (int) which);

        resized();
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
        //  The notch flips: below the card when the card sits above its
        //  anchor, above it when the placement algorithm put the card
        //  underneath.
        const bool notchBelow = getAnchor().y >= getBounds().getCentreY();
        auto body = getLocalBounds().toFloat();
        body = notchBelow ? body.withTrimmedBottom ((float) kNotchH)
                          : body.withTrimmedTop ((float) kNotchH);

        //  Shadow y 6 at 28 %, then the card at 94 %.
        {
            juce::Path shape;
            shape.addRoundedRectangle (body.translated (0.0f, 6.0f), 14.0f);
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillPath (shape);
        }

        juce::Path card;
        card.addRoundedRectangle (body, 14.0f);

        {
            const float ax = juce::jlimit (body.getX() + 14.0f + kNotchW,
                                           body.getRight() - 14.0f - kNotchW,
                                           (float) (getAnchor().x - getX()));
            juce::Path notch;
            if (notchBelow)
                notch.addTriangle (ax - kNotchW * 0.5f, body.getBottom(),
                                   ax + kNotchW * 0.5f, body.getBottom(),
                                   ax, body.getBottom() + (float) kNotchH);
            else
                notch.addTriangle (ax - kNotchW * 0.5f, body.getY(),
                                   ax + kNotchW * 0.5f, body.getY(),
                                   ax, body.getY() - (float) kNotchH);
            card.addPath (notch);
        }

        g.setColour (colour::raised.withAlpha (0.94f));
        g.fillPath (card);
        g.setColour (colour::text.withAlpha (0.24f));
        g.strokePath (card, juce::PathStrokeType (1.0f));

        //  Semantic header: "LP", never "HIGH EDGE".
        g.setColour (headerColour);
        g.setFont (juce::FontOptions (font::title).withStyle ("Bold"));
        g.drawText (header, (int) body.getX() + kPadding, (int) body.getY() + 4,
                    (int) body.getWidth() - kPadding * 2, kHeaderRow - 4,
                    juce::Justification::centredLeft, false);
    }

    void ShapePanel::resized()
    {
        const bool notchBelow = getAnchor().y >= getBounds().getCentreY();
        auto r = getLocalBounds();
        r = notchBelow ? r.withTrimmedBottom (kNotchH) : r.withTrimmedTop (kNotchH);
        r = r.reduced (kPadding, 0).withTrimmedTop (kHeaderRow).withTrimmedBottom (6);

        for (auto& panel : panels)
            panel.setBounds (r);
    }
}
