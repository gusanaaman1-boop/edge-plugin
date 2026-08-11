#include <cmath>
#include <memory>

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
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, metric::pillRow);
    parent.addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, colour::text);
    caption.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
    parent.addAndMakeVisible (caption);

    attachment = std::make_unique<SliderAttachment> (state, id, slider);

    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

//  One vertical rhythm, shared with the SHAPE panel:
//
//      [ mark ] [ knob ] [ value pill ] [ caption ]
//
//  The mark row is always reserved, whether or not this control draws into it,
//  so every knob in a row has its circle at the same height. Before this the
//  bipolar knobs' "0" was drawn over whatever happened to be above them.
//  A knob column is exactly this tall, and every column in a row is given the
//  same rectangle, so the circles line up and so do the pills and the captions.
int EdgeAudioProcessorEditor::Knob::heightFor (int knobSize) noexcept
{
    return metric::markRow + knobSize + metric::pillRow
         + metric::rowGap + metric::captionRow;
}

void EdgeAudioProcessorEditor::Knob::setBounds (juce::Rectangle<int> r)
{
    markArea = r.removeFromTop (metric::markRow);
    caption.setBounds (r.removeFromBottom (metric::captionRow));
    r.removeFromBottom (metric::rowGap);

    //  The slider owns its own text box, so its rectangle has to be exactly
    //  circle + pill. Handing it the whole remaining column instead left the
    //  rotary floating in the middle of a tall box with its read-out stranded
    //  30 px below it.
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                            juce::jmin (86, r.getWidth()), metric::pillRow);
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
    for (auto* b : { &lpButton, &bandButton, &hpButton, &freeButton })
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
        freeButton.onClick = [write] { write ((int) Mode::freeBand); };

        modeAttachment = std::make_unique<juce::ParameterAttachment> (
            *modeParam,
            [this] (float v)
            {
                const int m = (int) std::lround (v);
                lpButton.setToggleState   (m == (int) Mode::lowPass,  juce::dontSendNotification);
                bandButton.setToggleState (m == (int) Mode::band,     juce::dontSendNotification);
                hpButton.setToggleState   (m == (int) Mode::highPass, juce::dontSendNotification);
                freeButton.setToggleState (m == (int) Mode::freeBand, juce::dontSendNotification);
                curve.setFreeMode (m == (int) Mode::freeBand);
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

    //  CHARACTER. One button that names what it is currently doing and swaps
    //  on click - two entries do not need a menu.
    characterButton.setClickingTogglesState (false);
    characterButton.getProperties().set ("accent", (int) colour::low.getARGB());
    addAndMakeVisible (characterButton);

    if (auto* cp = state.getParameter (param::character))
    {
        characterButton.onClick = [cp]
        {
            const int now = (int) std::lround (cp->convertFrom0to1 (cp->getValue()));
            const int next = (now + 1) % kNumCharacters;
            cp->beginChangeGesture();
            cp->setValueNotifyingHost (cp->convertTo0to1 ((float) next));
            cp->endChangeGesture();
        };

        characterAttachment = std::make_unique<juce::ParameterAttachment> (
            *cp,
            [this] (float v)
            {
                const int c = (int) std::lround (v);
                characterButton.setButtonText (characterName (c));
                characterButton.setToggleState (true, juce::dontSendNotification);
                repaint (warmLampArea);
            },
            nullptr);

        characterAttachment->sendInitialUpdate();
    }

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

    //  The activity lamp, beside the CHARACTER button. It reads the ENGINE's
    //  engage factor, not "BITE > 0": a colour that is switched on but not yet
    //  doing anything should not claim otherwise.
    if (! warmLampArea.isEmpty())
    {
        const auto c = warmLampArea.toFloat();
        drawLamp (g, { c.getCentreX(), c.getCentreY() }, 3.5f, colour::low, warmLit);
    }

    //  Bipolar knobs get a centre mark and end signs, inside the mark row that
    //  Knob::setBounds reserved for them. Nothing else is drawn there, so
    //  nothing can collide with them.
    auto marks = [&g] (const Knob& k)
    {
        g.setColour (colour::textDim);
        g.setFont (juce::FontOptions (font::tiny));
        g.drawText ("0", k.markArea, juce::Justification::centred, false);

        const auto b = k.slider.getBounds();
        const int y = b.getBottom() - metric::pillRow - 12;
        g.drawText ("-", b.getX() + 1, y, 10, 10, juce::Justification::centred, false);
        g.drawText ("+", b.getRight() - 11, y, 10, 10, juce::Justification::centred, false);
    };

    marks (followKnob);
    marks (spreadKnob);

    //  The wordmark, under the big knob, on the same baseline as the captions
    //  either side of it.
    const auto e = edgeKnob.getBounds();
    g.setColour (colour::textBright);
    g.setFont (juce::FontOptions ((float) juce::jlimit (17, 26, e.getWidth() / 5))
                   .withStyle ("Bold"));
    g.drawText (juce::String::fromUTF8 ("E D G E"),
                e.getX() - 24, e.getBottom() + 4, e.getWidth() + 48, 24,
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

    strip.reduce (8, 6);

    //  Every column in the strip is laid out from the SAME top and the same
    //  height, so the knob circles form one line and the captions form another.
    const int knobSize = juce::jlimit (54, 78, (strip.getWidth() - 300) / 6);
    const int rowH = Knob::heightFor (knobSize);
    const int rowTop = strip.getY() + (strip.getHeight() - rowH) / 2;
    const int gap = 8;

    //  Everything in the strip hangs off this one line.
    auto column = [rowTop, rowH] (juce::Rectangle<int> box)
    {
        return box.withY (rowTop).withHeight (rowH);
    };

    //  --- left: MODE over SHAPE ---------------------------------------------
    auto left = strip.removeFromLeft (juce::jlimit (188, 248, strip.getWidth() / 4));
    {
        left = left.withY (rowTop).withHeight (rowH);
        auto modeRow = left.removeFromTop (28);
        const int w = modeRow.getWidth() / 4;
        lpButton.setBounds   (modeRow.removeFromLeft (w).reduced (2, 0));
        bandButton.setBounds (modeRow.removeFromLeft (w).reduced (2, 0));
        hpButton.setBounds   (modeRow.removeFromLeft (w).reduced (2, 0));
        freeButton.setBounds (modeRow.reduced (2, 0));

        left.removeFromTop (8);
        shapeButton.setBounds (left.removeFromTop (28).removeFromLeft (
            juce::jmin (110, left.getWidth())).reduced (2, 0));
    }

    //  --- right: OUTPUT and BYPASS -------------------------------------------
    auto right = strip.removeFromRight (juce::jlimit (168, 226, strip.getWidth() / 4));
    {
        //  BYPASS is centred on the knob CIRCLES, not on the column, so it
        //  reads as part of the same row rather than floating above it.
        auto bypassCol = column (right.removeFromRight (84));
        bypassButton.setBounds (bypassCol.withHeight (34)
                                          .withY (rowTop + metric::markRow
                                                  + (knobSize - 34) / 2));

        right.removeFromRight (gap);
        outputKnob.setBounds (column (right.removeFromRight (
            juce::jmin (knobSize, right.getWidth()))));
    }

    //  --- the three performance knobs ----------------------------------------
    auto perf = strip.removeFromRight (knobSize * 3 + gap * 2);
    auto place = [knobSize, &column] (juce::Rectangle<int>& area, Knob& k)
    {
        k.setBounds (column (area.removeFromLeft (knobSize)));
        area.removeFromLeft (8);
    };

    place (perf, followKnob);
    place (perf, spreadKnob);
    place (perf, biteKnob);

    //  CHARACTER sits in BITE's mark row, with its lamp to the left of it -
    //  the row that every other column leaves empty.
    {
        auto row = biteKnob.markArea.withHeight (metric::markRow);
        warmLampArea = row.removeFromLeft (10);
        characterButton.setBounds (row.withTrimmedLeft (1).expanded (0, 3));
    }

    //  --- EDGE, filling what is left ------------------------------------------
    //  EDGE is the only control that does NOT sit on the knob row's grid: it
    //  takes the whole strip's height, because the spec asks for it to stay
    //  visually dominant and a control the same size as FOLLOW is not.
    const int edgeSize = juce::jlimit (92, 160,
                                       juce::jmin (strip.getWidth() - 20,
                                                   strip.getHeight() - metric::captionRow - 10));
    edgeKnob.setBounds (strip.withSizeKeepingCentre (edgeSize, edgeSize)
                             .withY (strip.getY()));

    resizer->setBounds (getWidth() - 15, getHeight() - 15, 15, 15);
}
