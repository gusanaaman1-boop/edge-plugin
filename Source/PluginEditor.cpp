#include "PluginEditor.h"

using namespace edge;
using namespace edge::ui;

void EdgeAudioProcessorEditor::Knob::attach (juce::Component& parent,
                                             juce::AudioProcessorValueTreeState& state,
                                             const juce::String& id, const juce::String& text,
                                             juce::Colour accent, bool bipolar)
{
    slider.getProperties().set ("accent", (int) accent.getARGB());
    slider.getProperties().set ("ticks", true);
    if (bipolar)
        slider.getProperties().set ("bipolar", true);

    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 14);
    parent.addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, colour::text);
    caption.setFont (juce::FontOptions (10.5f).withStyle ("Bold"));
    parent.addAndMakeVisible (caption);

    attachment = std::make_unique<SliderAttachment> (state, id, slider);

    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void EdgeAudioProcessorEditor::Knob::setBounds (juce::Rectangle<int> r)
{
    caption.setBounds (r.removeFromBottom (13));
    slider.setBounds (r);
}

// -----------------------------------------------------------------------------

EdgeAudioProcessorEditor::EdgeAudioProcessorEditor (EdgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), edgeProcessor (p), curve (p),
      shape (p.getState())
{
    setLookAndFeel (&look);

    addAndMakeVisible (curve);
    addChildComponent (shape);

    auto& state = edgeProcessor.getState();

    //  --- EDGE -------------------------------------------------------------
    //  The only control with two accents: it drives both edges at once, and the
    //  ring says so.
    edgeKnob.getProperties().set ("accent",  (int) colour::low.getARGB());
    edgeKnob.getProperties().set ("accent2", (int) colour::high.getARGB());
    edgeKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (edgeKnob);
    edgeAttachment = std::make_unique<SliderAttachment> (state, param::edge, edgeKnob);

    if (auto* ep = state.getParameter (param::edge))
        edgeKnob.setDoubleClickReturnValue (true, ep->convertFrom0to1 (ep->getDefaultValue()));

    followKnob.attach (*this, state, param::follow, "FOLLOW", colour::text,       true);
    spreadKnob.attach (*this, state, param::spread, "SPREAD", colour::text,       true);
    biteKnob  .attach (*this, state, param::bite,   "BITE",   colour::low,        false);
    outputKnob.attach (*this, state, param::output, "OUTPUT", colour::textBright, true);

    //  --- MODE --------------------------------------------------------------
    for (auto* b : { &lpButton, &bandButton, &hpButton })
    {
        b->setClickingTogglesState (false);
        b->getProperties().set ("accent", (int) colour::low.getARGB());
        addAndMakeVisible (*b);
    }

    if (auto* modeParam = state.getParameter (param::mode))
    {
        auto write = [modeParam] (int index)
        {
            modeParam->beginChangeGesture();
            modeParam->setValueNotifyingHost (modeParam->convertTo0to1 ((float) index));
            modeParam->endChangeGesture();
        };

        lpButton.onClick   = [write] { write ((int) Mode::lowPass); };
        bandButton.onClick = [write] { write ((int) Mode::band); };
        hpButton.onClick   = [write] { write ((int) Mode::highPass); };

        modeAttachment = std::make_unique<juce::ParameterAttachment> (
            *modeParam,
            [this] (float v)
            {
                const int m = (int) std::lround (v);
                lpButton.setToggleState   (m == (int) Mode::lowPass,  juce::dontSendNotification);
                bandButton.setToggleState (m == (int) Mode::band,     juce::dontSendNotification);
                hpButton.setToggleState   (m == (int) Mode::highPass, juce::dontSendNotification);
            },
            nullptr);

        modeAttachment->sendInitialUpdate();
    }

    //  --- SHAPE + BYPASS -----------------------------------------------------
    shapeButton.setClickingTogglesState (false);
    shapeButton.getProperties().set ("accent", (int) colour::text.getARGB());
    shapeButton.onClick = [this] { setShapeOpen (! shape.isVisible()); };
    addAndMakeVisible (shapeButton);

    bypassButton.getProperties().set ("accent", (int) colour::low.getARGB());
    bypassButton.getProperties().set ("lamp", true);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, param::bypass, bypassButton);

    shape.onLinkChanged = [] {};
    installLinkCoupling();

    //  --- window -------------------------------------------------------------
    constrainer.setSizeLimits (metric::minWidth, 420, metric::maxWidth, 1400);
    setConstrainer (&constrainer);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
    addAndMakeVisible (*resizer);
    setResizable (true, false);

    const bool open = edgeProcessor.shapeOpen.load();
    shape.setVisible (open);
    shapeButton.setToggleState (open, juce::dontSendNotification);

    setSize (edgeProcessor.editorWidth.load(),
             edgeProcessor.editorHeight.load() + (open ? metric::shapeHeight : 0));

    startTimerHz (30);
}

EdgeAudioProcessorEditor::~EdgeAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void EdgeAudioProcessorEditor::setShapeOpen (bool shouldBeOpen)
{
    if (shape.isVisible() == shouldBeOpen)
        return;

    const int base = getHeight() - (shape.isVisible() ? metric::shapeHeight : 0);

    shape.setVisible (shouldBeOpen);
    shapeButton.setToggleState (shouldBeOpen, juce::dontSendNotification);
    edgeProcessor.shapeOpen.store (shouldBeOpen);

    setSize (getWidth(), base + (shouldBeOpen ? metric::shapeHeight : 0));
}

void EdgeAudioProcessorEditor::timerCallback()
{
    auto& engine = edgeProcessor.getEngine();

    //  The knob shows where the parameter is; the marker shows where FOLLOW has
    //  actually put it.
    const float live = engine.getLiveEdge01();
    const float shown = (float) edgeKnob.getProperties().getWithDefault ("live", -1.0f);

    if (std::abs (live - shown) > 0.002f)
    {
        edgeKnob.getProperties().set ("live", live);
        edgeKnob.repaint();
    }

    const bool warm = engine.isColourEngaged();
    if (warm != warmLit)
    {
        warmLit = warm;
        repaint (warmLampArea);
    }
}

//  LINK. Moving one frequency moves the partner by the same number of OCTAVES,
//  which is what "linked" means for a pair of filters; moving by the same
//  number of Hz would collapse the relationship in the bass. It is applied to
//  editor gestures only - a processor that writes parameters back to the host
//  turns one automated lane into two lanes fighting each other.
void EdgeAudioProcessorEditor::installLinkCoupling()
{
    auto* lowP  = edgeProcessor.getState().getParameter (param::lowFreq);
    auto* highP = edgeProcessor.getState().getParameter (param::highFreq);

    if (lowP == nullptr || highP == nullptr)
        return;

    lastLowFreq  = lowP->convertFrom0to1 (lowP->getValue());
    lastHighFreq = highP->convertFrom0to1 (highP->getValue());

    freqWatcherLow = std::make_unique<juce::ParameterAttachment> (
        *lowP, [this] (float) { applyLink (true); }, nullptr);

    freqWatcherHigh = std::make_unique<juce::ParameterAttachment> (
        *highP, [this] (float) { applyLink (false); }, nullptr);
}

void EdgeAudioProcessorEditor::applyLink (bool lowMoved)
{
    auto* lowP  = edgeProcessor.getState().getParameter (param::lowFreq);
    auto* highP = edgeProcessor.getState().getParameter (param::highFreq);
    if (lowP == nullptr || highP == nullptr)
        return;

    const float nowLow  = lowP->convertFrom0to1 (lowP->getValue());
    const float nowHigh = highP->convertFrom0to1 (highP->getValue());

    if (applyingLink || ! shape.isLinkEnabled() || lastLowFreq <= 0.0f || lastHighFreq <= 0.0f)
    {
        lastLowFreq = nowLow;
        lastHighFreq = nowHigh;
        return;
    }

    const juce::ScopedValueSetter<bool> guard (applyingLink, true);

    if (lowMoved)
    {
        const float ratio = nowLow / juce::jmax (1.0f, lastLowFreq);
        highP->setValueNotifyingHost (highP->convertTo0to1 (
            juce::jlimit (kHighFreqMin, kHighFreqMax, nowHigh * ratio)));
    }
    else
    {
        const float ratio = nowHigh / juce::jmax (1.0f, lastHighFreq);
        lowP->setValueNotifyingHost (lowP->convertTo0to1 (
            juce::jlimit (kLowFreqMin, kLowFreqMax, nowLow * ratio)));
    }

    lastLowFreq  = lowP->convertFrom0to1 (lowP->getValue());
    lastHighFreq = highP->convertFrom0to1 (highP->getValue());
}

int EdgeAudioProcessorEditor::controlStripHeight() const noexcept
{
    const int usable = getHeight() - (shape.isVisible() ? metric::shapeHeight : 0);
    return juce::jlimit (150, 210, usable / 3);
}

void EdgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0b0c0d));
    paintShell (g, getLocalBounds().toFloat().reduced (3.0f));

    //  WARM lamp, over the BITE knob. It reads the ENGINE's engage factor, not
    //  "BITE > 0": a colour that is switched on but not yet doing anything
    //  should not claim otherwise.
    if (! warmLampArea.isEmpty())
    {
        const auto c = warmLampArea.toFloat();
        drawLamp (g, { c.getX() + 5.0f, c.getCentreY() }, 3.5f, colour::low, warmLit);

        g.setColour (warmLit ? colour::low : colour::textDim);
        g.setFont (juce::FontOptions (9.5f).withStyle ("Bold"));
        g.drawText ("WARM", warmLampArea.withTrimmedLeft (14),
                    juce::Justification::centredLeft, false);
    }

    //  Bipolar knobs get a centre mark and end signs, as in the mockup.
    auto marks = [&g] (const juce::Slider& s)
    {
        const auto b = s.getBounds();
        g.setColour (colour::textDim);
        g.setFont (juce::FontOptions (9.0f));
        g.drawText ("0", b.getX(), b.getY() - 12, b.getWidth(), 11,
                    juce::Justification::centred, false);
        g.drawText ("-", b.getX() - 2, b.getBottom() - 15, 12, 11,
                    juce::Justification::centred, false);
        g.drawText ("+", b.getRight() - 10, b.getBottom() - 15, 12, 11,
                    juce::Justification::centred, false);
    };

    marks (followKnob.slider);
    marks (spreadKnob.slider);

    //  The wordmark, under the big knob.
    const auto e = edgeKnob.getBounds();
    g.setColour (colour::textBright);
    g.setFont (juce::FontOptions ((float) juce::jlimit (18, 30, e.getWidth() / 5))
                   .withStyle ("Bold").withHorizontalScale (1.0f));
    g.drawText (juce::String::fromUTF8 ("E D G E"),
                e.getX() - 20, e.getBottom() + 2, e.getWidth() + 40, 26,
                juce::Justification::centred, false);
}

void EdgeAudioProcessorEditor::resized()
{
    const bool open = shape.isVisible();

    edgeProcessor.editorWidth.store (getWidth());
    edgeProcessor.editorHeight.store (getHeight() - (open ? metric::shapeHeight : 0));

    auto r = getLocalBounds().reduced (10);

    if (open)
        shape.setBounds (r.removeFromBottom (metric::shapeHeight - 6));

    auto strip = r.removeFromBottom (controlStripHeight());
    curve.setBounds (r.reduced (2, 2));

    strip.reduce (6, 4);

    //  --- left column: MODE over SHAPE ---------------------------------------
    auto left = strip.removeFromLeft (juce::jlimit (150, 220, strip.getWidth() / 5));
    {
        auto modeRow = left.removeFromTop (30);
        const int w = modeRow.getWidth() / 3;
        lpButton.setBounds   (modeRow.removeFromLeft (w).reduced (2));
        bandButton.setBounds (modeRow.removeFromLeft (w).reduced (2));
        hpButton.setBounds   (modeRow.reduced (2));

        left.removeFromTop (8);
        shapeButton.setBounds (left.removeFromTop (30).reduced (2, 0)
                                   .withTrimmedRight (left.getWidth() / 3));
    }

    //  --- right column: OUTPUT and BYPASS ------------------------------------
    auto right = strip.removeFromRight (juce::jlimit (150, 210, strip.getWidth() / 4));
    {
        bypassButton.setBounds (right.removeFromRight (78).withSizeKeepingCentre (78, 40));
        right.removeFromRight (10);
        outputKnob.setBounds (right.removeFromRight (juce::jmin (66, right.getWidth()))
                                  .withSizeKeepingCentre (66, juce::jmin (86, right.getHeight())));
    }

    //  --- centre: EDGE, then the three performance knobs ---------------------
    const int knobSize = juce::jlimit (58, 84, strip.getWidth() / 7);
    auto perf = strip.removeFromRight (knobSize * 3 + 16);

    auto place = [knobSize] (juce::Rectangle<int>& area, Knob& k)
    {
        k.setBounds (area.removeFromLeft (knobSize)
                          .withSizeKeepingCentre (knobSize, juce::jmin (100, area.getHeight())));
        area.removeFromLeft (8);
    };

    place (perf, followKnob);
    place (perf, spreadKnob);
    place (perf, biteKnob);

    //  The lamp sits just above BITE, where the mockup puts it.
    warmLampArea = juce::Rectangle<int> (biteKnob.slider.getX() + 4,
                                         biteKnob.slider.getY() - 15, 62, 12);

    const int edgeSize = juce::jlimit (86, 150,
                                       juce::jmin (strip.getWidth() - 20, strip.getHeight() - 24));
    edgeKnob.setBounds (strip.withSizeKeepingCentre (edgeSize, edgeSize)
                             .translated (0, -10));

    resizer->setBounds (getWidth() - 15, getHeight() - 15, 15, 15);
}
