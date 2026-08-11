#include "PluginEditor.h"

using namespace edge;
using namespace edge::ui;

void EdgeAudioProcessorEditor::Control::attach (juce::Component& parent,
                                                juce::AudioProcessorValueTreeState& state,
                                                const juce::String& id,
                                                const juce::String& text,
                                                juce::Colour accent)
{
    slider.getProperties().set ("accent", (int) accent.getARGB());
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 14);
    parent.addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, colour::textDim);
    caption.setFont (juce::FontOptions (10.0f).withStyle ("Bold"));
    parent.addAndMakeVisible (caption);

    attachment = std::make_unique<SliderAttachment> (state, id, slider);

    //  Double click resets to the parameter's default, which is what the work
    //  order asks for and what a host's own "reset" does.
    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void EdgeAudioProcessorEditor::Control::setBounds (juce::Rectangle<int> r)
{
    caption.setBounds (r.removeFromTop (13));
    slider.setBounds (r);
}

// -----------------------------------------------------------------------------

EdgeAudioProcessorEditor::EdgeAudioProcessorEditor (EdgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), edgeProcessor (p), curve (p)
{
    setLookAndFeel (&look);

    title.setText ("EDGE", juce::dontSendNotification);
    title.setFont (juce::FontOptions (20.0f).withStyle ("Bold"));
    title.setColour (juce::Label::textColourId, colour::textBright);
    addAndMakeVisible (title);

    auto sideLabel = [this] (juce::Label& l, const juce::String& text, juce::Colour c)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (11.5f).withStyle ("Bold"));
        l.setColour (juce::Label::textColourId, c);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };

    sideLabel (lowLabel,  "LOW EDGE",  colour::low);
    sideLabel (highLabel, "HIGH EDGE", colour::high);
    lowLabel.setJustificationType (juce::Justification::centredLeft);
    highLabel.setJustificationType (juce::Justification::centredRight);

    addAndMakeVisible (curve);

    auto& state = edgeProcessor.getState();
    lowCurveCtl    .attach (*this, state, param::lowCurve,     "CURVE",    colour::low);
    lowShoulderCtl .attach (*this, state, param::lowShoulder,  "SHOULDER", colour::low);
    lowResCtl      .attach (*this, state, param::lowRes,       "RESO",     colour::low);
    highShoulderCtl.attach (*this, state, param::highShoulder, "SHOULDER", colour::high);
    highCurveCtl   .attach (*this, state, param::highCurve,    "CURVE",    colour::high);
    highResCtl     .attach (*this, state, param::highRes,      "RESO",     colour::high);
    focusCtl       .attach (*this, state, param::focus,        "FOCUS",    colour::textBright);
    outputCtl      .attach (*this, state, param::output,       "OUTPUT",   colour::textBright);

    linkButton.getProperties().set ("accent", (int) colour::textBright.getARGB());
    bypassButton.getProperties().set ("accent", (int) colour::high.getARGB());
    addAndMakeVisible (linkButton);
    addAndMakeVisible (bypassButton);
    linkAttachment   = std::make_unique<ButtonAttachment> (state, param::link, linkButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, param::bypass, bypassButton);

    installLinkCoupling();

    constrainer.setFixedAspectRatio ((double) metric::defaultWidth / metric::defaultHeight);
    constrainer.setSizeLimits (metric::minWidth,
                               (int) (metric::minWidth * metric::defaultHeight
                                          / (double) metric::defaultWidth),
                               metric::maxWidth,
                               (int) (metric::maxWidth * metric::defaultHeight
                                          / (double) metric::defaultWidth));
    setConstrainer (&constrainer);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
    addAndMakeVisible (*resizer);

    setResizable (true, false);
    setSize (edgeProcessor.editorWidth.load(), edgeProcessor.editorHeight.load());
}

EdgeAudioProcessorEditor::~EdgeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

//  LINK. Watching the two frequency parameters and moving the partner by the
//  same number of OCTAVES preserves the interval between the edges, which is
//  what "linked" means for a pair of filters; moving by the same number of Hz
//  would collapse the relationship in the bass.
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

    const bool linked = edgeProcessor.getState().getParameter (param::link)->getValue() > 0.5f;

    if (applyingLink || ! linked || lastLowFreq <= 0.0f || lastHighFreq <= 0.0f)
    {
        lastLowFreq = nowLow;
        lastHighFreq = nowHigh;
        return;
    }

    const juce::ScopedValueSetter<bool> guard (applyingLink, true);

    if (lowMoved)
    {
        const float ratio = nowLow / juce::jmax (1.0f, lastLowFreq);
        const float target = juce::jlimit (kHighFreqMin, kHighFreqMax, nowHigh * ratio);
        highP->setValueNotifyingHost (highP->convertTo0to1 (target));
    }
    else
    {
        const float ratio = nowHigh / juce::jmax (1.0f, lastHighFreq);
        const float target = juce::jlimit (kLowFreqMin, kLowFreqMax, nowLow * ratio);
        lowP->setValueNotifyingHost (lowP->convertTo0to1 (target));
    }

    lastLowFreq  = lowP->convertFrom0to1 (lowP->getValue());
    lastHighFreq = highP->convertFrom0to1 (highP->getValue());
}

void EdgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    paintShell (g, b);

    auto header = b.removeFromTop (40.0f);

    //  Header sits on a slightly lifted band with a hairline under it.
    g.setGradientFill ({ colour::panelTop.withAlpha (0.75f), 0.0f, header.getY(),
                         juce::Colours::transparentBlack, 0.0f, header.getBottom(), false });
    g.fillRect (header);
    g.setColour (colour::panelEdge);
    g.drawHorizontalLine ((int) header.getBottom(), 0.0f, (float) getWidth());

    //  A short two-accent rule under the wordmark - the only ornament in the
    //  window, and it still only uses the two product colours.
    g.setGradientFill ({ colour::low, 16.0f, 0.0f, colour::high, 96.0f, 0.0f, false });
    g.fillRoundedRectangle (16.0f, header.getBottom() - 7.0f, 80.0f, 2.0f, 1.0f);

    //  The two accents as a legend, not as decoration.
    const int legendRight = getWidth() - 20;
    struct Item { const char* label; juce::Colour c; };
    const Item items[] = { { "LOW", colour::low }, { "HIGH", colour::high } };

    for (int i = 0; i < 2; ++i)
    {
        const int x = legendRight - (2 - i) * 58;
        g.setColour (items[(size_t) i].c);
        g.fillRoundedRectangle ((float) x, 17.0f, 20.0f, 3.0f, 1.5f);
        g.setColour (items[(size_t) i].c.withAlpha (0.25f));
        g.fillRoundedRectangle ((float) x - 1.0f, 16.0f, 22.0f, 5.0f, 2.5f);

        g.setColour (colour::textDim);
        g.setFont (juce::FontOptions (9.0f).withStyle ("Bold"));
        g.drawText (items[(size_t) i].label, x - 4, 23, 28, 10,
                    juce::Justification::centred, false);
    }

    //  A quiet groove behind the control strip, so the knobs have a surface.
    auto strip = getLocalBounds().toFloat().removeFromBottom ((float) controlStripHeight());
    g.setGradientFill ({ juce::Colours::transparentBlack, 0.0f, strip.getY(),
                         colour::shellBottom.withAlpha (0.6f), 0.0f, strip.getBottom(), false });
    g.fillRect (strip);
    g.setColour (colour::panelEdge.withAlpha (0.6f));
    g.drawHorizontalLine ((int) strip.getY(), 12.0f, (float) getWidth() - 12.0f);
}

int EdgeAudioProcessorEditor::controlStripHeight() const noexcept
{
    return juce::jlimit (96, 150, getHeight() / 4);
}

void EdgeAudioProcessorEditor::resized()
{
    edgeProcessor.editorWidth.store (getWidth());
    edgeProcessor.editorHeight.store (getHeight());

    auto r = getLocalBounds();

    auto header = r.removeFromTop (40);
    title.setBounds (header.removeFromLeft (130).reduced (16, 6));

    auto bottom = r.removeFromBottom (controlStripHeight());
    curve.setBounds (r.reduced (12, 10));

    //  LOW's three knobs on the left, HIGH's on the right, the shared pair in
    //  the middle, so the strip reads the way the spectrum does.
    bottom.reduce (14, 8);

    const int knobWidth = juce::jlimit (54, 92, (bottom.getWidth() - 120) / 9);
    const int gap = 6;
    const int sideWidth = knobWidth * 3 + gap * 2;

    auto lowArea  = bottom.removeFromLeft (sideWidth);
    auto highArea = bottom.removeFromRight (sideWidth);

    //  Section titles are justified OUTWARD and given their own row with a
    //  gap: centred, they landed directly on the middle knob's caption.
    lowLabel.setBounds (lowArea.removeFromTop (14).withTrimmedLeft (2));
    highLabel.setBounds (highArea.removeFromTop (14).withTrimmedRight (2));
    lowArea.removeFromTop (3);
    highArea.removeFromTop (3);

    auto place = [knobWidth, gap] (juce::Rectangle<int>& area, Control& c)
    {
        c.setBounds (area.removeFromLeft (knobWidth));
        area.removeFromLeft (gap);
    };

    place (lowArea, lowCurveCtl);
    place (lowArea, lowShoulderCtl);
    place (lowArea, lowResCtl);

    place (highArea, highShoulderCtl);
    place (highArea, highCurveCtl);
    place (highArea, highResCtl);

    bottom.removeFromTop (17);

    const int buttonWidth = juce::jlimit (72, 104, knobWidth + 16);
    auto centre = bottom.withSizeKeepingCentre (
        juce::jmin (bottom.getWidth(), knobWidth * 2 + gap * 2 + buttonWidth),
        bottom.getHeight());

    focusCtl.setBounds (centre.removeFromLeft (knobWidth));
    centre.removeFromLeft (gap);
    outputCtl.setBounds (centre.removeFromLeft (knobWidth));
    centre.removeFromLeft (gap);

    auto buttons = centre.reduced (0, 7);
    const int half = buttons.getHeight() / 2;
    linkButton.setBounds (buttons.removeFromTop (half).reduced (1, 2));
    bypassButton.setBounds (buttons.reduced (1, 2));

    resizer->setBounds (getWidth() - 15, getHeight() - 15, 15, 15);
}
