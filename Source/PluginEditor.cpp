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

    //  Accessibility and the 500 ms tooltip: the name, and the value the
    //  formatter already produces.
    slider.setTitle (text);
    slider.setTooltip (text);

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

    //  Dark glass, not titanium chassis: #171C27 -> #0D111A, radius 12, a 1 px
    //  top-inner highlight, contact + ambient shadows, no border.
    {
        juce::Path sh;
        sh.addRoundedRectangle (b.translated (0.0f, 2.0f), 12.0f);
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillPath (sh);
        juce::Path sh2;
        sh2.addRoundedRectangle (b.translated (0.0f, 6.0f), 12.0f);
        g.setColour (juce::Colours::black.withAlpha (0.20f));
        g.fillPath (sh2);
    }

    g.setGradientFill ({ colour::headerTop, 0.0f, b.getY(),
                         colour::headerBottom, 0.0f, b.getBottom(), false });
    g.fillRoundedRectangle (b, 12.0f);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.drawLine (b.getX() + 12.0f, b.getY() + 1.0f, b.getRight() - 12.0f, b.getY() + 1.0f, 1.0f);

    //  The one reactive element: the hairline takes the active mode's colour.
    g.setColour (hairline.withAlpha (0.6f));
    g.drawLine (b.getX() + 12.0f, b.getBottom() - 1.0f,
                b.getRight() - 12.0f, b.getBottom() - 1.0f, 1.0f);

    drawWordmark (g, b, juce::Colour (0xffEEF2FA));
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

    {
        juce::ColourGradient grad (colour::deckTop, 0.0f, b.getY(),
                                   colour::deckBottom, 0.0f, b.getBottom(), false);
        g.setGradientFill (grad);
        g.fillPath (card);
    }
    g.setColour (juce::Colours::white.withAlpha (0.08f));
    g.drawLine (b.getX() + 20.0f, b.getY() + 2.5f, b.getRight() - 28.0f, b.getY() + 2.5f, 1.0f);

    //  The violet-blue seam along the top: 14 % idle, up to 24 % when the
    //  REAL follow envelope pushes. Never animated on its own.
    g.setColour (colour::movement.withAlpha (0.14f + 0.10f * juce::jlimit (0.0f, 1.0f, seamEnergy)));
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
    presetTitle.setColour (juce::Label::textColourId, colour::tertiary);
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

    //  BITE is champagne - never LOW's amber.
    biteKnob.attach (*this, state, param::bite, "BITE", colour::champagne);
    biteKnob.slider.getProperties().set ("valueInside", false);
    biteKnob.caption.setColour (juce::Label::textColourId, colour::textDim);

    bypassButton.getProperties().set ("accent", (int) colour::low.getARGB());
    bypassButton.getProperties().set ("lamp", true);
    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (state, param::bypass, bypassButton);

    //  CHARACTER: WARM | IRON as two segments. Champagne accent for WARM,
    //  cold titanium for IRON - violet is forbidden here.
    for (auto* b : { &warmButton, &ironButton })
    {
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    warmButton.setConnectedEdges (juce::Button::ConnectedOnRight);
    ironButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
    warmButton.getProperties().set ("accent", (int) colour::champagne.getARGB());
    ironButton.getProperties().set ("accent", (int) colour::titanBright.getARGB());

    if (auto* cp = state.getParameter (param::character))
    {
        auto write = [cp] (int c)
        {
            cp->beginChangeGesture();
            cp->setValueNotifyingHost (cp->convertTo0to1 ((float) c));
            cp->endChangeGesture();
        };
        warmButton.onClick = [write] { write ((int) Character::warm); };
        ironButton.onClick = [write] { write ((int) Character::iron); };

        characterAttachment = std::make_unique<juce::ParameterAttachment> (
            *cp,
            [this] (float v)
            {
                const int c = (int) std::lround (v);
                warmButton.setToggleState (c == (int) Character::warm, juce::dontSendNotification);
                ironButton.setToggleState (c == (int) Character::iron, juce::dontSendNotification);
            },
            nullptr);
        characterAttachment->sendInitialUpdate();
    }

    //  OUTPUT: the existing parameter finally gets a panel control - a 30 px
    //  neutral header knob. Presentation only; the parameter is unchanged.
    outputKnob.attach (*this, state, param::output, "OUT", colour::text);
    outputKnob.slider.getProperties().set ("bipolar", true);
    outputKnob.slider.getProperties().set ("valueInside", false);
    outputKnob.caption.setColour (juce::Label::textColourId, colour::textDim);

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
                                                                 : colour::titanDark;
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

    //  SPREAD: the existing parameter gets its direct deck control back -
    //  48 px, bipolar, neutral-violet. Presentation only.
    spreadKnob.attach (*this, state, param::spread, "SPREAD", colour::textDim);
    spreadKnob.slider.getProperties().set ("bipolar", true);
    spreadKnob.slider.getProperties().set ("valueSize", 12.0);

    for (auto* k : { &lowKnob, &highKnob, &followKnob })
        k->slider.getProperties().set ("valueSize", 15.0);

    edgeKnob.getProperties().set ("accent",  (int) colour::low.getARGB());
    edgeKnob.getProperties().set ("accent2", (int) colour::high.getARGB());
    edgeKnob.getProperties().set ("valueInside", true);
    edgeKnob.getProperties().set ("valueSize", 24.0);   // the largest number on the UI
    edgeKnob.getProperties().set ("ledOrbit", true);     // 36 violet dots, lit by travel
    edgeKnob.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f,
                                  juce::MathConstants<float>::pi * 2.75f, true);
    edgeKnob.setTitle ("EDGE");
    edgeKnob.setTooltip ("EDGE");
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

    //  The dock's seam brightens with the REAL follow envelope only.
    if (std::abs (snap.followEnv01 - dockPanel.seamEnergy) > 0.02f)
    {
        dockPanel.seamEnergy = snap.followEnv01;
        dockPanel.repaint (0, 0, dockPanel.getWidth(), 4);
    }

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

    curve.setBounds (getLocalBounds());

    //  --- header (10, 10, W-20, 48) ---------------------------------------------
    headerPanel.setBounds (10, 10, getWidth() - 20, 48);
    {
        auto header = headerPanel.getBounds();

        //  Preset browser: < [field 160] > with 8 px gaps.
        auto left = header.withTrimmedLeft (16);
        presetTitle.setBounds (left.removeFromLeft (50).withSizeKeepingCentre (50, 20));
        prevPreset.setBounds (left.removeFromLeft (28).withSizeKeepingCentre (28, 28));
        left.removeFromLeft (8);
        const int fieldW = juce::jmin (160, getWidth() / 5);
        presetBox.setBounds (left.removeFromLeft (fieldW).withSizeKeepingCentre (fieldW, 28));
        left.removeFromLeft (8);
        nextPreset.setBounds (left.removeFromLeft (28).withSizeKeepingCentre (28, 28));

        //  Right side: BITE, WARM|IRON, OUTPUT, BYPASS.
        auto right = header.withTrimmedRight (16);
        bypassButton.setBounds (right.removeFromRight (70).withSizeKeepingCentre (70, 26));
        right.removeFromRight (10);

        auto out = right.removeFromRight (66);
        outputKnob.caption.setBounds (out.removeFromLeft (28).withSizeKeepingCentre (28, 20));
        outputKnob.slider.setBounds (out.withSizeKeepingCentre (30, 30));
        right.removeFromRight (10);

        auto character = right.removeFromRight (82).withSizeKeepingCentre (82, 26);
        warmButton.setBounds (character.removeFromLeft (41));
        ironButton.setBounds (character);
        right.removeFromRight (10);

        auto bite = right.removeFromRight (66);
        biteKnob.caption.setBounds (bite.removeFromLeft (30).withSizeKeepingCentre (30, 20));
        biteKnob.slider.setBounds (bite.withSizeKeepingCentre (30, 30));
    }

    //  --- performance deck (10, H-170, W-20, 160) ---------------------------------
    dockPanel.setBounds (10, getHeight() - 170, getWidth() - 20, 160);
    {
        const auto dock = dockPanel.getBounds();
        const float fx = (float) dock.getWidth() / 880.0f;

        //  Reference centres, scaled horizontally with the dock.
        auto centreAt = [&] (float refX, float refY) -> juce::Point<int>
        {
            return { dock.getX() + (int) ((refX - 10.0f) * fx),
                     dock.getY() + (int) (refY - 390.0f) };
        };

        auto placeKnob = [&] (DeckKnob& k, float refX, float refY, int d)
        {
            const auto c = centreAt (refX, refY);
            k.slider.setBounds (c.x - d / 2, c.y - d / 2, d, d);
            k.caption.setBounds (c.x - 48, c.y - d / 2 - 17, 96, 14);
        };

        placeKnob (spreadKnob, 78.0f, 478.0f, 48);
        placeKnob (lowKnob,   216.0f, 474.0f, 82);
        placeKnob (highKnob,  660.0f, 474.0f, 82);
        placeKnob (followKnob, 820.0f, 478.0f, 74);

        const auto ec = centreAt (450.0f, 466.0f);
        edgeKnob.setBounds (ec.x - 58, ec.y - 58, 116, 116);

        const auto dockPos = dockPanel.getPosition();
        dockPanel.edgeLabel = juce::Rectangle<int> (ec.x - 40, ec.y - 58 - 18, 80, 14) - dockPos;
        dockPanel.cleanRect = juce::Rectangle<int> (ec.x - 58 - 48, ec.y + 42, 44, 12) - dockPos;
        dockPanel.cutRect   = juce::Rectangle<int> (ec.x + 58 + 4,  ec.y + 42, 44, 12) - dockPos;
    }

    //  --- MODE (326, 80, 248, 28), centred to the window --------------------------
    {
        const int segW = 248, segH = 28;
        auto seg = juce::Rectangle<int> (getWidth() / 2 - segW / 2, 80, segW, segH);
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
