#include <memory>

#include "ShapePanel.h"

#include "../Core/ParameterIds.h"
#include "../Dsp/EdgeEngine.h"

namespace edge::ui
{
    int ShapePanel::attachmentCount = 0;

    namespace
    {
        //  Inspector geometry, v0.14: one FIXED strip, centred in the graph.
        constexpr int kPadding = 12;
        constexpr int kGap = 8;
        constexpr int kMiniKnob = 30;
        constexpr int kStripH = 88;
        constexpr int kStripW = 420;
        constexpr int kStripWMid = 330;
        constexpr int kSlopeW = 150, kSlopeH = 26;
    }

    // -------------------------------------------------------------------------
    //  SlopeSelector: five choices, one gesture per write.
    // -------------------------------------------------------------------------
    SlopeSelector::SlopeSelector (juce::RangedAudioParameter& param, juce::Colour accentIn)
        : attachment (param,
                      [this] (float newValue)
                      {
                          //  FOLLOWS the value - old projects and moving host
                          //  automation highlight the nearest choice. Nothing
                          //  is ever written back from here.
                          nominal = nominalSlopeForCurveValue (newValue);
                          repaint();
                      },
                      nullptr),
          accent (accentIn)
    {
        setWantsKeyboardFocus (true);
        attachment.sendInitialUpdate();
    }

    void SlopeSelector::choose (int index)
    {
        //  The one place a calibrated value is written, and it only runs on a
        //  deliberate gesture: click, wheel or key.
        const int slope = kNominalSlopes[juce::jlimit (0, kNumNominalSlopes - 1, index)];
        attachment.setValueAsCompleteGesture (curveValueForNominalSlope (slope));
    }

    void SlopeSelector::mouseDown (const juce::MouseEvent& e)
    {
        const int seg = (int) ((e.position.x / (float) getWidth()) * kNumNominalSlopes);
        choose (seg);
    }

    void SlopeSelector::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        int index = 0;
        for (int i = 0; i < kNumNominalSlopes; ++i)
            if (kNominalSlopes[i] == nominal)
                index = i;

        choose (index + (wheel.deltaY < 0 ? 1 : -1));
    }

    bool SlopeSelector::keyPressed (const juce::KeyPress& key)
    {
        int index = 0;
        for (int i = 0; i < kNumNominalSlopes; ++i)
            if (kNominalSlopes[i] == nominal)
                index = i;

        if (key == juce::KeyPress::leftKey || key == juce::KeyPress::downKey)
        {
            choose (index - 1);
            return true;
        }
        if (key == juce::KeyPress::rightKey || key == juce::KeyPress::upKey)
        {
            choose (index + 1);
            return true;
        }

        return false;
    }

    void SlopeSelector::paint (juce::Graphics& g)
    {
        auto r = getLocalBounds().toFloat();
        const float segW = r.getWidth() / (float) kNumNominalSlopes;

        for (int i = 0; i < kNumNominalSlopes; ++i)
        {
            auto seg = juce::Rectangle<float> (r.getX() + segW * (float) i, r.getY(),
                                               segW, r.getHeight()).reduced (1.0f, 0.0f);
            const bool isSelected = kNominalSlopes[i] == nominal;

            g.setColour (isSelected ? accent : accent.withAlpha (0.10f));
            g.fillRoundedRectangle (seg, 7.0f);

            //  Selected text in the graph colour - dark digits on the lit
            //  segment - and secondary text everywhere else.
            g.setColour (isSelected ? colour::graph : colour::textDim);
            g.setFont (juce::FontOptions (10.0f).withStyle (isSelected ? "Bold" : "plain"));
            g.drawText (juce::String (kNominalSlopes[i]), seg.toNearestInt(),
                        juce::Justification::centred, false);
        }
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

        slider.setTitle (text);
        slider.setTooltip (text);

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
        //  Horizontal, equally spaced cells, at least 70 px each. A panel that
        //  hosts the slope selector reserves its 150 px in the middle; the
        //  selector itself is positioned by ShapePanel::resized.
        auto r = getLocalBounds();
        const int used = (int) knobs.size();
        if (used == 0)
            return;

        const int selectorSpan = slopeArea.isEmpty() ? 0 : kSlopeW + kGap;
        const int cellW = juce::jmax (70, (r.getWidth() - selectorSpan
                                             - kGap * (used - 1)) / used);

        int placed = 0;
        for (auto& k : knobs)
        {
            //  The selector sits after the first cell (DEPTH | SLOPE | ...).
            if (placed == 1 && selectorSpan > 0)
                r.removeFromLeft (selectorSpan);

            k->setBounds (r.removeFromLeft (cellW));
            r.removeFromLeft (kGap);
            ++placed;
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
        //  handles and the deck knobs, not here. CURVE is not a knob any more:
        //  it is the five-slope selector, inserted after DEPTH.
        buildPanel (SelectedControl::low,
                    { { param::lowDepth, "DEPTH" },
                      { param::lowReso, "RESO" }, { param::lowShoulder, "SHOULDER" } },
                    colour::low);

        buildPanel (SelectedControl::high,
                    { { param::highDepth, "DEPTH" },
                      { param::highReso, "RESO" }, { param::highShoulder, "SHOULDER" } },
                    colour::high);

        if (auto* p = state.getParameter (param::lowCurve))
        {
            lowSlope = std::make_unique<SlopeSelector> (*p, colour::low);
            panelFor (SelectedControl::low).addAndMakeVisible (*lowSlope);
            ++attachmentCount;
        }

        if (auto* p = state.getParameter (param::highCurve))
        {
            highSlope = std::make_unique<SlopeSelector> (*p, colour::high);
            panelFor (SelectedControl::high).addAndMakeVisible (*highSlope);
            ++attachmentCount;
        }

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
        //  Fixed sizes from the spec: 420 wide for LOW/HIGH/FOLLOW, 330 for
        //  MID, 88 tall for all.
        return { selected == SelectedControl::mid ? kStripWMid : kStripW, kStripH };
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

    SlopeSelector* ShapePanel::slopeSelectorFor (SelectedControl which) noexcept
    {
        if (which == SelectedControl::low)  return lowSlope.get();
        if (which == SelectedControl::high) return highSlope.get();
        return nullptr;
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
        auto body = getLocalBounds().toFloat();

        //  One fixed card: shadow y 6 at 28 %, surface at 94 %, NEUTRAL border
        //  in every context - only the label carries the context colour. The
        //  top-right corner is the EDGE CUT: motif place three of three.
        auto card = edgeCutPanel (body, 14.0f, 20.0f, 14.0f);

        {
            juce::Path sh (card);
            sh.applyTransform (juce::AffineTransform::translation (0.0f, 6.0f));
            g.setColour (juce::Colours::black.withAlpha (0.28f));
            g.fillPath (sh);
        }

        {
            juce::ColourGradient grad (colour::inspTop.withAlpha (0.94f), 0.0f, body.getY(),
                                       colour::inspBottom.withAlpha (0.94f), 0.0f, body.getBottom(),
                                       false);
            g.setGradientFill (grad);
            g.fillPath (card);
        }
        g.setColour (juce::Colours::white.withAlpha (0.09f));
        g.drawLine (body.getX() + 14.0f, body.getY() + 1.0f,
                    body.getRight() - 22.0f, body.getY() + 1.0f, 1.0f);

        //  The context label, upper-left, 11 px semibold, context-coloured.
        g.setColour (headerColour);
        g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
        g.drawText (header, (int) body.getX() + kPadding, (int) body.getY() + 6,
                    (int) body.getWidth() - kPadding * 2, 13,
                    juce::Justification::centredLeft, false);

        //  The slope selector's own caption and unit, drawn by the container
        //  so every context keeps the same label/knob/value rhythm.
        if ((selected == SelectedControl::low || selected == SelectedControl::high)
              && ! slopeArea.isEmpty())
        {
            g.setColour (colour::textDim);
            g.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
            g.drawText ("SLOPE", slopeArea.getX(), slopeArea.getY() - 11,
                        kSlopeW, 10, juce::Justification::centred, false);
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("dB/oct", slopeArea.getRight() + 4, slopeArea.getCentreY() - 5,
                        36, 10, juce::Justification::centredLeft, false);
        }
    }

    void ShapePanel::resized()
    {
        auto r = getLocalBounds().reduced (kPadding, 0)
                                 .withTrimmedTop (20)
                                 .withTrimmedBottom (6);

        for (auto& panel : panels)
            panel.setBounds (r);

        //  The selector: 150 x 26, after the DEPTH cell, vertically centred on
        //  the knob row. Stored in panel coordinates for the layout, mirrored
        //  here for the caption drawing.
        auto placeSelector = [this, &r] (SelectedControl which, SlopeSelector* sel)
        {
            auto& panel = panelFor (which);
            if (sel == nullptr || panel.knobs.empty())
                return;

            const int used = (int) panel.knobs.size();
            const int cellW = juce::jmax (70, (r.getWidth() - (kSlopeW + kGap)
                                                 - kGap * (used - 1)) / used);

            const int x = cellW + kGap;
            sel->setBounds (x, (panel.getHeight() - kSlopeH) / 2 + 4, kSlopeW, kSlopeH);

            panel.slopeArea = { x, (panel.getHeight() - kSlopeH) / 2 + 4, kSlopeW, kSlopeH };

            if (selected == which)
                slopeArea = panel.slopeArea.translated (panel.getX(), panel.getY());
        };

        slopeArea = {};
        placeSelector (SelectedControl::low, lowSlope.get());
        placeSelector (SelectedControl::high, highSlope.get());

        for (auto& panel : panels)
            panel.resized();
    }
}

