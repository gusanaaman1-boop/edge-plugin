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
    caption.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold"))
                         .withExtraKerningFactor (0.08f));
    parent.addAndMakeVisible (caption);

    attachment = std::make_unique<SliderAttachment> (state, id, slider);

    if (auto* p = state.getParameter (id))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void EdgeAudioProcessorEditor::DeckKnob::place (juce::Rectangle<int> area, int diameter)
{
    //  Caption and knob as ONE block, centred, sized for the 116 px dock.
    auto block = area.withSizeKeepingCentre (juce::jmax (diameter, 96), 14 + 2 + diameter);
    caption.setBounds (block.removeFromTop (14));
    block.removeFromTop (2);
    slider.setBounds (block.withSizeKeepingCentre (diameter, diameter));
}

// -----------------------------------------------------------------------------

void EdgeAudioProcessorEditor::HeaderPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    //  Floating titanium glass: #DDE1E5 at 94 %, radius 15, no outer border,
    //  a 1 px inner highlight, and its shadow onto the graph below.
    {
        juce::Path sh;
        sh.addRoundedRectangle (b.translated (0.0f, 2.0f), 15.0f);
        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.fillPath (sh);
    }

    g.setColour (juce::Colour (0xffDDE1E5).withAlpha (0.94f));
    g.fillRoundedRectangle (b, 15.0f);
    g.setColour (juce::Colours::white.withAlpha (0.38f));
    g.drawRoundedRectangle (b.reduced (1.0f), 14.0f, 1.0f);

    //  The one reactive element: the hairline along the header's bottom edge
    //  takes the active filter function's colour.
    g.setColour (hairline.withAlpha (0.75f));
    g.drawLine (b.getX() + 15.0f, b.getBottom() - 1.5f,
                b.getRight() - 15.0f, b.getBottom() - 1.5f, 1.0f);

    drawWordmark (g, b, colour::textOnLight);
}

void EdgeAudioProcessorEditor::DockPanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();

    //  The dark glass dock: #151A21 at 88 %, radius 20, no border, the EDGE
    //  CUT on its top-right corner - the motif's approved deck location.
    auto card = edgeCutPanel (b, 20.0f, 26.0f, 19.0f);

    {
        juce::Path sh (card);
        sh.applyTransform (juce::AffineTransform::translation (0.0f, 2.0f));
        g.setColour (juce::Colours::black.withAlpha (0.34f));
        g.fillPath (sh);
    }
    {
        juce::Path sh (card);
        sh.applyTransform (juce::AffineTransform::translation (0.0f, 10.0f));
        g.setColour (juce::Colours::black.withAlpha (0.24f));
        g.fillPath (sh);
    }

    g.setColour (juce::Colour (0xff151A21).withAlpha (0.88f));
    g.fillPath (card);
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawLine (b.getX() + 20.0f, b.getY() + 1.0f, b.getRight() - 28.0f, b.getY() + 1.0f, 1.0f);

    //  EDGE's label and the CLEAN / CUT endpoints - drawn HERE because the
    //  dock paints above the editor, and anything the editor painted at these
    //  coordinates would be underneath the glass.
    g.setColour (colour::text);
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold"))
                   .withExtraKerningFactor (0.08f));
    g.drawText ("EDGE", edgeLabel, juce::Justification::centred, false);

    g.setColour (colour::textDim);
    g.setFont (juce::Font (juce::FontOptions (10.0f)).withExtraKerningFactor (0.06f));
    g.drawText ("CLEAN", cleanRect, juce::Justification::centredRight, false);
    g.drawText ("CUT", cutRect, juce::Justification::centredLeft, false);

   #if JUCE_DEBUG
    g.setColour (colour::textDim.withAlpha (0.5f));
    g.setFont (juce::FontOptions (font::tiny));
    g.drawText (versionText, getLocalBounds().reduced (12, 4),
                juce::Justification::bottomRight, false);
   #endif
}

EdgeAudioProcessorEditor::EdgeAudioProcessorEditor (EdgeAudioProcessor& p)
    : juce::AudioProcessorEditor (&p), edgeProcessor (p), curve (p),
      inspector (p.getState())
{
    setLookAndFeel (&look);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (curve);
    addAndMakeVisible (headerPanel);
    addAndMakeVisible (dockPanel);
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
                headerPanel.hairline = m == (int) Mode::lowPass  ? colour::high
                                     : m == (int) Mode::highPass ? colour::low
                                                                 : colour::shellShadow;
                headerPanel.repaint();

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

    for (auto* k : { &lowKnob, &highKnob, &followKnob })
        k->slider.getProperties().set ("valueSize", 16.0);

    edgeKnob.getProperties().set ("accent",  (int) colour::low.getARGB());
    edgeKnob.getProperties().set ("accent2", (int) colour::high.getARGB());
    edgeKnob.getProperties().set ("valueInside", true);
    edgeKnob.getProperties().set ("valueSize", 24.0);   // the largest number on the UI
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
    dockPanel.versionText = versionText;

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
    const auto graph = curve.plotBounds().translated (curve.getX(), curve.getY());

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
    //  Full-bleed: the graph view covers the window, the floating panels
    //  paint their own glass. This fill only shows for the first frame.
    g.fillAll (colour::graph);

}

void EdgeAudioProcessorEditor::resized()
{
    edgeProcessor.editorWidth.store (getWidth());
    edgeProcessor.editorHeight.store (getHeight());

    //  The graph is the window.
    curve.setBounds (getLocalBounds());

    //  --- floating header: x 16, y 12, height 48 --------------------------------
    headerPanel.setBounds (16, 12, getWidth() - 32, 48);
    {
        auto header = headerPanel.getBounds();

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

    //  --- floating macro dock: x 24, bottom inset 18, height 116 -----------------
    dockPanel.setBounds (24, getHeight() - 18 - 116, getWidth() - 48, 116);
    {
        auto dock = dockPanel.getBounds().reduced (10, 4);
        const int quarter = dock.getWidth() / 4;

        auto lowArea    = dock.removeFromLeft (quarter);
        auto edgeArea   = dock.removeFromLeft (quarter);
        auto highArea   = dock.removeFromLeft (quarter);
        auto followArea = dock;

        lowKnob.place  (lowArea, 64);
        highKnob.place (highArea, 64);
        followKnob.place (followArea, 60);

        //  EDGE: dominant, its caption row shared with the others.
        auto e = edgeArea.withSizeKeepingCentre (140, 14 + 2 + 92);
        edgeLabelArea = e.removeFromTop (14);
        e.removeFromTop (2);
        const auto knob = e.withSizeKeepingCentre (92, 92);
        edgeKnob.setBounds (knob);

        const auto dockPos = dockPanel.getPosition();
        dockPanel.edgeLabel = edgeLabelArea - dockPos;
        dockPanel.cleanRect = juce::Rectangle<int> (knob.getX() - 48, knob.getBottom() - 16,
                                                    44, 12) - dockPos;
        dockPanel.cutRect = juce::Rectangle<int> (knob.getRight() + 4, knob.getBottom() - 16,
                                                  44, 12) - dockPos;
    }

    //  --- MODE, floating inside the graph content ---------------------------------
    {
        const int segW = 250, segH = 28;
        auto seg = juce::Rectangle<int> (getWidth() / 2 - segW / 2, 68 + 8, segW, segH);
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
