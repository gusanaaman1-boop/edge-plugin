#include <memory>

#include "ShapePanel.h"

#include "../Core/ParameterIds.h"

namespace edge::ui
{
    int ShapePanel::attachmentCount = 0;

    namespace
    {
        //  Inspector geometry, from the v0.12 spec.
        constexpr int kPadding = 16;
        [[maybe_unused]] constexpr int kGap = 12;
        constexpr int kHeaderRow = 18;
        constexpr int kNotchW = 12, kNotchH = 7;
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
        caption.setJustificationType (juce::Justification::centredLeft);
        caption.setColour (juce::Label::textColourId, colour::textDim);
        caption.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        parent.addAndMakeVisible (caption);

        value.setJustificationType (juce::Justification::centredRight);
        value.setColour (juce::Label::textColourId, colour::text);
        value.setFont (juce::FontOptions (font::caption));
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

    //  The mockup's layout: one ROW per control - the name on the left, a
    //  small knob, the value on the right. Four rows fit 272 x 128 with room
    //  to breathe; four columns squeezed "SHOULDER" into "SHOULD...".
    void ShapePanel::Knob::setBounds (juce::Rectangle<int> r)
    {
        caption.setBounds (r.removeFromLeft (76));
        value.setBounds (r.removeFromRight (52));
        slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getHeight(), 24),
                                                   juce::jmin (r.getHeight(), 24)));
    }

    void ShapePanel::ContextPanel::resized()
    {
        auto r = getLocalBounds();
        const int used = (int) knobs.size();
        if (used == 0)
            return;

        const int rowH = r.getHeight() / used;

        for (auto& k : knobs)
            k->setBounds (r.removeFromTop (rowH));
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
        //  From the spec: LOW/HIGH 272 x 128, MID 240 x 116, FOLLOW 264 x 120,
        //  plus the notch below.
        if (selected == SelectedControl::mid)    return { 240, 116 + kNotchH };
        if (selected == SelectedControl::follow) return { 264, 120 + kNotchH };

        return { 272, 128 + kNotchH };
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
        auto body = getLocalBounds().toFloat().withTrimmedBottom ((float) kNotchH);

        //  Shadow: offset (0, 8), then the raised card at 96 % over the graph.
        {
            juce::Path shape;
            shape.addRoundedRectangle (body.translated (0.0f, 8.0f), metric::radiusLarge);
            g.setColour (juce::Colours::black.withAlpha (0.38f));
            g.fillPath (shape);
        }

        juce::Path card;
        card.addRoundedRectangle (body, metric::radiusLarge);

        //  Pointer notch, 12 x 7, aimed at the anchor's x.
        {
            const float ax = juce::jlimit (body.getX() + metric::radiusLarge + kNotchW,
                                           body.getRight() - metric::radiusLarge - kNotchW,
                                           (float) (getAnchor().x - getX()));
            juce::Path notch;
            notch.addTriangle (ax - kNotchW * 0.5f, body.getBottom(),
                               ax + kNotchW * 0.5f, body.getBottom(),
                               ax, body.getBottom() + (float) kNotchH);
            card.addPath (notch);
        }

        g.setColour (colour::raised.withAlpha (0.96f));
        g.fillPath (card);
        g.setColour (colour::text.withAlpha (0.24f));
        g.strokePath (card, juce::PathStrokeType (1.0f));

        //  Semantic header: "LP", never "HIGH EDGE".
        g.setColour (headerColour);
        g.setFont (juce::FontOptions (font::title).withStyle ("Bold"));
        g.drawText (header, (int) body.getX() + kPadding, (int) body.getY() + 8,
                    (int) body.getWidth() - kPadding * 2, kHeaderRow,
                    juce::Justification::centredLeft, false);
    }

    void ShapePanel::resized()
    {
        auto r = getLocalBounds().withTrimmedBottom (kNotchH)
                                 .reduced (kPadding, 0)
                                 .withTrimmedTop (8 + kHeaderRow)
                                 .withTrimmedBottom (10);

        for (auto& panel : panels)
            panel.setBounds (r);
    }
}
