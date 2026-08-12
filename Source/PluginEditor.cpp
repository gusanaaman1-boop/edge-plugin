#include <cmath>
#include <memory>

#include <EdgeVersion.h>

#include "PluginEditor.h"

using namespace edge;
using namespace edge::ui;

void EdgeAudioProcessorEditor::DeckKnob::attach (juce::Component& parent,
                                                 juce::AudioProcessorValueTreeState& state,
                                                 const juce::String& id, const juce::String& text,
                                                 juce::Colour accent)
{
    slider.getProperties().set ("accent", (int) accent.getARGB());
    slider.getProperties().set ("valueInside", true);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                juce::MathConstants<float>::pi * 2.75f, true);
    parent.addAndMakeVisible (slider);

    caption.setText (text, juce::dontSendNotification);
    caption.setJustificationType (juce::Justification::centred);
    caption.setColour (juce::Label::textColourId, accent);
    caption.setFont (juce::FontOptions (font::knobLabel).withStyle ("Bold"));
    parent.addAndMakeVisible (caption);

    attachment = std::make_unique<SliderAttachment> (state, id, slider);

    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void EdgeAudioProcessorEditor::DeckKnob::place (juce::Rectangle<int> area, int diameter)
{
    //  Caption and knob as ONE block, centred - not a caption pinned to the
    //  top of the cell with the knob floating somewhere below it.
    auto block = area.withSizeKeepingCentre (juce::jmax (diameter, 90), 16 + 4 + diameter);
    caption.setBounds (block.removeFromTop (16));
    block.removeFromTop (4);
    slider.setBounds (block.withSizeKeepingCentre (diameter, diameter));
}

// -----------------------------------------------------------------------------

EdgeAudioProcessorEditor::EdgeAudioProcessorEditor (EdgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), edgeProcessor (p), curve (p),
      inspector (p.getState())
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (curve);
    addChildComponent (inspector);      // hidden until something is selected

    auto& state = edgeProcessor.getState();

    //  --- header --------------------------------------------------------------
    presetTitle.setText ("PRESET", juce::dontSendNotification);
    presetTitle.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
    presetTitle.setColour (juce::Label::textColourId, colour::textOnLight.withAlpha (0.65f));
    addAndMakeVisible (presetTitle);

    refreshPresetBox();
    presetBox.onChange = [this]
    {
        const int program = presetBox.getSelectedId() - 1;
        if (program >= 0 && program != edgeProcessor.getCurrentProgram())
            edgeProcessor.setCurrentProgram (program);
    };
    addAndMakeVisible (presetBox);

    auto step = [this] (int delta)
    {
        const int n = edgeProcessor.getNumPrograms();
        const int next = (edgeProcessor.getCurrentProgram() + delta + n) % n;
        edgeProcessor.setCurrentProgram (next);
        presetBox.setSelectedId (next + 1, juce::dontSendNotification);
    };
    prevPreset.onClick = [step] { step (-1); };
    nextPreset.onClick = [step] { step (+1); };
    addAndMakeVisible (prevPreset);
    addAndMakeVisible (nextPreset);

    biteKnob.attach (*this, state, param::bite, "BITE", colour::low);
    biteKnob.slider.getProperties().set ("valueInside", false);
    biteKnob.caption.setColour (juce::Label::textColourId, colour::textOnLight.withAlpha (0.75f));

    bypassButton.getProperties().set ("accent", (int) colour::low.getARGB());
    bypassButton.getProperties().set ("lamp", true);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, param::bypass, bypassButton);

    //  CHARACTER stays reachable: it is BITE's voicing, and BITE without it is
    //  half a control. One compact toggle, not a toolbar.
    characterButton.setClickingTogglesState (false);
    characterButton.getProperties().set ("accent", (int) colour::low.getARGB());
    addAndMakeVisible (characterButton);

    if (auto* cp = state.getParameter (param::character))
    {
        characterButton.onClick = [cp]
        {
            const int now = (int) std::lround (cp->convertFrom0to1 (cp->getValue()));
            cp->beginChangeGesture();
            cp->setValueNotifyingHost (cp->convertTo0to1 ((float) ((now + 1) % kNumCharacters)));
            cp->endChangeGesture();
        };

        characterAttachment = std::make_unique<juce::ParameterAttachment> (
            *cp,
            [this] (float v)
            {
                characterButton.setButtonText (characterName ((int) std::lround (v)));
            },
            nullptr);
        characterAttachment->sendInitialUpdate();
    }

    //  --- MODE: a segmented control inside the graph ---------------------------
    for (auto* b : { &lpButton, &bandButton, &hpButton, &freeButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }

    lpButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    bandButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    hpButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    freeButton.setConnectedEdges (juce::Button::ConnectedOnLeft);

    //  Selected segment carries the colour of the active filter FUNCTION:
    //  LP cuts with the high edge (cyan); HP is the mirror (amber); BAND and
    //  FREE are both edges, so they stay neutral. Violet is never a band.
    lpButton.getProperties().set   ("accent", (int) colour::high.getARGB());
    hpButton.getProperties().set   ("accent", (int) colour::low.getARGB());
    bandButton.getProperties().set ("accent", (int) colour::text.getARGB());
    freeButton.getProperties().set ("accent", (int) colour::text.getARGB());

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
                repaint (0, 0, getWidth(), metric::headerHeight);

                //  A mode that removed the selected edge moves the selection to
                //  the edge that still exists - the inspector must never sit on
                //  a control that is a wire.
                if (m == (int) Mode::lowPass && selected == SelectedControl::low)
                    openInspector (SelectedControl::high);
                else if (m == (int) Mode::highPass && selected == SelectedControl::high)
                    openInspector (SelectedControl::low);
                else if (inspector.isVisible())
                    openInspector (selected);       // refresh the semantic header
            },
            nullptr);

        modeAttachment->sendInitialUpdate();
    }

    //  --- deck ------------------------------------------------------------------
    lowKnob.attach  (*this, state, param::lowFreq,  "LOW",  colour::low);
    highKnob.attach (*this, state, param::highFreq, "HIGH", colour::high);
    followKnob.attach (*this, state, param::follow,
                       juce::String::fromUTF8 ("FOLLOW \xe2\x86\x92 EDGE"), colour::movement);
    followKnob.slider.getProperties().set ("bipolar", true);

    edgeKnob.getProperties().set ("accent",  (int) colour::low.getARGB());
    edgeKnob.getProperties().set ("accent2", (int) colour::high.getARGB());
    edgeKnob.getProperties().set ("valueInside", true);
    edgeKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    addAndMakeVisible (edgeKnob);
    edgeAttachment = std::make_unique<SliderAttachment> (state, param::edge, edgeKnob);

    if (auto* ep = state.getParameter (param::edge))
        edgeKnob.setDoubleClickReturnValue (true, ep->convertFrom0to1 (ep->getDefaultValue()));

    //  --- selection ---------------------------------------------------------------
    curve.onSelectionChanged = [this] (SelectedControl c) { openInspector (c); };
    curve.onSelectionCleared = [this] { closeInspector(); };

    //  Touching the FOLLOW knob selects FOLLOW - the detector's setup lives in
    //  the inspector and this is how you reach it.
    followKnob.slider.onDragStart = [this] { openInspector (SelectedControl::follow); };

    //  --- window --------------------------------------------------------------
    versionText = juce::String (edge::kGitDescribe) == juce::String ("v") + edge::kVersion
                    ? juce::String ("v") + edge::kVersion
                    : juce::String ("v") + edge::kVersion + "  " + edge::kGitDescribe;

    constrainer.setSizeLimits (metric::minWidth, metric::minHeight,
                               metric::maxWidth, metric::maxHeight);
    setConstrainer (&constrainer);

    resizer = std::make_unique<juce::ResizableCornerComponent> (this, &constrainer);
    addAndMakeVisible (*resizer);
    setResizable (true, false);

    setSize (juce::jlimit (metric::minWidth, metric::maxWidth, edgeProcessor.editorWidth.load()),
             juce::jlimit (metric::minHeight, metric::maxHeight, edgeProcessor.editorHeight.load()));

    startTimerHz (30);
}

EdgeAudioProcessorEditor::~EdgeAudioProcessorEditor()
{
    setLookAndFeel (nullptr);
}

void EdgeAudioProcessorEditor::refreshPresetBox()
{
    presetBox.clear (juce::dontSendNotification);
    for (int i = 0; i < edgeProcessor.getNumPrograms(); ++i)
        presetBox.addItem (edgeProcessor.getProgramName (i), i + 1);

    presetBox.setSelectedId (edgeProcessor.getCurrentProgram() + 1, juce::dontSendNotification);
}

//  In LP the high edge IS the low-pass and the inspector header must say so.
juce::String EdgeAudioProcessorEditor::semanticHeaderFor (SelectedControl c) const
{
    const int m = (int) std::lround (
        edgeProcessor.getState().getParameter (param::mode)->convertFrom0to1 (
            edgeProcessor.getState().getParameter (param::mode)->getValue()));

    switch (c)
    {
        case SelectedControl::low:    return m == (int) Mode::highPass ? "HP" : "LOW";
        case SelectedControl::high:   return m == (int) Mode::lowPass ? "LP" : "HIGH";
        case SelectedControl::mid:    return "MID";
        case SelectedControl::follow: return juce::String::fromUTF8 ("FOLLOW \xe2\x86\x92 EDGE");
    }

    return {};
}

void EdgeAudioProcessorEditor::openInspector (SelectedControl c)
{
    const bool wasVisible = inspector.isVisible();

    selected = c;
    inspector.setContext (c, semanticHeaderFor (c));

    curve.setSelected (c == SelectedControl::low  ? CurveView::Grab::low
                     : c == SelectedControl::high ? CurveView::Grab::high
                     : c == SelectedControl::mid  ? CurveView::Grab::mid
                                                  : CurveView::Grab::none);

    positionInspector();

    //  Opacity only: 90 ms in on open, 70 ms content change. The POSITION
    //  never animates - it is fixed.
    if (! wasVisible)
    {
        inspector.setAlpha (0.0f);
        inspector.setVisible (true);
        juce::Desktop::getInstance().getAnimator().fadeIn (&inspector, 90);
    }
    else
    {
        inspector.setAlpha (0.55f);
        juce::Desktop::getInstance().getAnimator().fadeIn (&inspector, 70);
    }

    inspector.toFront (false);
}

void EdgeAudioProcessorEditor::closeInspector()
{
    if (inspector.isVisible())
        juce::Desktop::getInstance().getAnimator().fadeOut (&inspector, 70);
}

void EdgeAudioProcessorEditor::positionInspector()
{
    //  v0.14: ONE fixed position. Horizontally centred in the graph, bottom
    //  edge 18 px above the graph's bottom, at every window size. No anchor,
    //  no notch, no candidates - the inspector is part of the instrument, not
    //  a tooltip chasing the cursor.
    const auto size = inspector.preferredSize();
    const auto graph = curve.getBounds();

    inspector.setBounds (graph.getCentreX() - size.x / 2,
                         graph.getBottom() - 18 - size.y,
                         size.x, size.y);

    //  The readout keeps the bottom-left and must never run under the strip.
    //  The limit applies always - the strip's position is fixed, so the
    //  readout simply never grows into that region.
    curve.setReadoutRightLimit (inspector.getX() - curve.getX() - 8);
}

bool EdgeAudioProcessorEditor::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey && inspector.isVisible())
    {
        closeInspector();
        return true;
    }

    return false;
}

void EdgeAudioProcessorEditor::timerCallback()
{
    //  The outer marker on EDGE: where the control actually IS after FOLLOW.
    //  The pointer is the lane; this is the sound.
    const auto snap = edgeProcessor.getEngine().getDisplaySnapshot();
    edgeKnob.getProperties().set ("live", snap.liveEdge01);
    edgeKnob.repaint();

    //  The preset box follows the program the host selected.
    if (presetBox.getSelectedId() != edgeProcessor.getCurrentProgram() + 1)
        presetBox.setSelectedId (edgeProcessor.getCurrentProgram() + 1,
                                 juce::dontSendNotification);
}

void EdgeAudioProcessorEditor::paint (juce::Graphics& g)
{
    //  v0.13: a light titanium chassis with the dark instrument set into it.
    //  The flat all-charcoal build made every surface the same value.
    g.setGradientFill ({ colour::shellHilite, 0.0f, 0.0f,
                         colour::shellLight, 0.0f, (float) getHeight() * 0.25f, false });
    g.fillRect (getLocalBounds());

    //  Direction E, and all of it: ONE hairline responds to state. It takes
    //  the active filter function's colour - cyan in LP, amber in HP - and
    //  stays neutral in the two-edge modes. The rest of the shell is calm.
    {
        auto* modeParam = edgeProcessor.getState().getParameter (param::mode);
        const int m = (int) std::lround (modeParam->convertFrom0to1 (modeParam->getValue()));
        const auto tint = m == (int) Mode::lowPass  ? colour::high
                        : m == (int) Mode::highPass ? colour::low
                                                    : colour::shellShadow;

        g.setColour (tint.withAlpha (m == (int) Mode::lowPass
                                      || m == (int) Mode::highPass ? 0.75f : 0.5f));
        g.drawHorizontalLine (metric::headerHeight - 1, 0.0f, (float) getWidth());
    }

    //  Cards: graph shadow y 5 at 20 %, deck shadow y 3 at 16 %, then the deck
    //  surface itself (the graph paints its own).
    auto shadow = [&g] (juce::Rectangle<float> b, float dy, float alpha)
    {
        juce::Path path;
        path.addRoundedRectangle (b.translated (0.0f, dy), metric::radiusLarge);
        g.setColour (juce::Colours::black.withAlpha (alpha));
        g.fillPath (path);
    };

    shadow (curve.getBounds().toFloat(), 5.0f, 0.20f);

    if (! deckArea.isEmpty())
    {
        //  The deck carries the EDGE CUT on its top-right corner - motif
        //  place two of three.
        auto card = edgeCutPanel (deckArea.toFloat(), metric::radiusLarge, 26.0f, 19.0f);

        {
            juce::Path sh (card);
            sh.applyTransform (juce::AffineTransform::translation (0.0f, 3.0f));
            g.setColour (juce::Colours::black.withAlpha (0.16f));
            g.fillPath (sh);
        }

        g.setColour (colour::deck);
        g.fillPath (card);
        g.setColour (colour::shellShadow.withAlpha (0.6f));
        g.strokePath (card, juce::PathStrokeType (1.0f));
    }

    //  The wordmark: four stroked capitals whose last E falls into the cut.
    //  Drawn from Paths - no font, no asset. Motif place one of three.
    drawWordmark (g, getLocalBounds().removeFromTop (metric::headerHeight).toFloat(),
                  colour::textOnLight);

   #if JUCE_DEBUG
    //  The build identity is a development aid. Release builds keep it in the
    //  log and the manual, not painted on the product.
    g.setColour (colour::textOnLight.withAlpha (0.45f));
    g.setFont (juce::FontOptions (font::tiny));
    g.drawText (versionText,
                getLocalBounds().removeFromBottom (14).withTrimmedRight (22).withTrimmedLeft (12),
                juce::Justification::centredRight, false);
   #endif

    //  EDGE's own label, and the CLEAN / CUT endpoints beside the knob.
    if (! edgeKnob.getBounds().isEmpty())
    {
        const auto e = edgeKnob.getBounds();

        g.setColour (colour::text);
        g.setFont (juce::FontOptions (font::knobLabel).withStyle ("Bold"));
        g.drawText ("EDGE", edgeLabelArea, juce::Justification::centred, false);

        g.setColour (colour::textDim);
        g.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        g.drawText ("CLEAN", e.getX() - 48, e.getBottom() - 18, 44, 12,
                    juce::Justification::centredRight, false);
        g.drawText ("CUT", e.getRight() + 4, e.getBottom() - 18, 44, 12,
                    juce::Justification::centredLeft, false);
    }
}

void EdgeAudioProcessorEditor::resized()
{
    edgeProcessor.editorWidth.store (getWidth());
    edgeProcessor.editorHeight.store (getHeight());

    auto r = getLocalBounds();

    //  --- header: preset left, wordmark centred (painted), BITE + BYPASS right --
    auto header = r.removeFromTop (metric::headerHeight);
    {
        auto left = header.withTrimmedLeft (metric::margin);
        presetTitle.setBounds (left.removeFromLeft (52).withSizeKeepingCentre (52, 20));
        const int boxW = juce::jmin (190, getWidth() / 5);
        presetBox.setBounds (left.removeFromLeft (boxW).withSizeKeepingCentre (boxW, 26));
        left.removeFromLeft (4);
        prevPreset.setBounds (left.removeFromLeft (24).withSizeKeepingCentre (24, 26));
        nextPreset.setBounds (left.removeFromLeft (24).withSizeKeepingCentre (24, 26));

        auto right = header.withTrimmedRight (metric::margin);
        bypassButton.setBounds (right.removeFromRight (86).withSizeKeepingCentre (86, 24));
        right.removeFromRight (8);
        characterButton.setBounds (right.removeFromRight (58).withSizeKeepingCentre (58, 22));
        right.removeFromRight (8);

        auto bite = right.removeFromRight (76);
        biteKnob.caption.setBounds (bite.removeFromLeft (34).withSizeKeepingCentre (34, 20));
        biteKnob.slider.setBounds (bite.withSizeKeepingCentre (36, 36));
    }

    //  --- deck --------------------------------------------------------------------
    auto deck = r.removeFromBottom (metric::deckHeight + metric::margin)
                 .withTrimmedBottom (metric::margin)
                 .withTrimmedLeft (metric::margin).withTrimmedRight (metric::margin);
    deckArea = deck;

    {
        auto quarters = deck;
        const int quarter = quarters.getWidth() / 4;

        auto lowArea    = quarters.removeFromLeft (quarter);
        auto edgeArea   = quarters.removeFromLeft (quarter);
        auto highArea   = quarters.removeFromLeft (quarter);
        auto followArea = quarters;

        lowKnob.place  (lowArea.reduced (8), 82);
        highKnob.place (highArea.reduced (8), 82);
        followKnob.place (followArea.reduced (8), 74);

        //  EDGE: 116 px, dominant, endpoints painted either side. Same block
        //  construction as the others so all four captions sit on one line.
        auto e = edgeArea.reduced (8).withSizeKeepingCentre (140, 16 + 4 + 116);
        edgeLabelArea = e.removeFromTop (16);
        e.removeFromTop (4);
        edgeKnob.setBounds (e.withSizeKeepingCentre (116, 116));
    }

    //  --- graph, with MODE floating inside its top --------------------------------
    auto graphArea = r.withTrimmedLeft (metric::margin).withTrimmedRight (metric::margin)
                      .withTrimmedTop (metric::margin);
    curve.setBounds (graphArea);

    {
        const int segW = 250, segH = 28;
        auto seg = juce::Rectangle<int> (graphArea.getCentreX() - segW / 2,
                                         graphArea.getY() + 12, segW, segH);
        const int w = segW / 4;
        lpButton.setBounds   (seg.removeFromLeft (w));
        bandButton.setBounds (seg.removeFromLeft (w));
        hpButton.setBounds   (seg.removeFromLeft (w));
        freeButton.setBounds (seg);
    }

    if (inspector.isVisible())
        positionInspector();

    resizer->setBounds (getWidth() - 18, getHeight() - 18, 18, 18);
}
