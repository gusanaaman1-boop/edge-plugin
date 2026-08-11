#include "Theme.h"

namespace edge::ui
{
    juce::String formatHz (float hz)
    {
        if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 2) + " kHz";
        if (hz >= 100.0f)   return juce::String (hz, 0) + " Hz";
        return juce::String (hz, 1) + " Hz";
    }

    void paintShell (juce::Graphics& g, juce::Rectangle<float> b)
    {
        g.setGradientFill ({ colour::shellTop, b.getX(), b.getY(),
                             colour::shellBottom, b.getX(), b.getBottom(), false });
        g.fillRoundedRectangle (b, 10.0f);

        g.setColour (colour::panelEdge.withAlpha (0.6f));
        g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, 1.0f);
        g.setColour (colour::panelHilite.withAlpha (0.28f));
        g.drawLine (b.getX() + 12.0f, b.getY() + 1.0f, b.getRight() - 12.0f, b.getY() + 1.0f, 1.0f);
    }

    void paintWell (juce::Graphics& g, juce::Rectangle<float> b, float corner)
    {
        g.setGradientFill ({ colour::wellTop, b.getX(), b.getY(),
                             colour::wellBottom, b.getX(), b.getBottom(), false });
        g.fillRoundedRectangle (b, corner);

        //  Inner bevel: a dark top edge and a faint bottom one make the well
        //  read as cut into the chassis rather than laid on top of it.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.drawLine (b.getX() + corner, b.getY() + 0.5f, b.getRight() - corner, b.getY() + 0.5f, 1.4f);
        g.setColour (colour::panelHilite.withAlpha (0.22f));
        g.drawLine (b.getX() + corner, b.getBottom() - 0.5f,
                    b.getRight() - corner, b.getBottom() - 0.5f, 1.0f);
        g.setColour (colour::panelEdge);
        g.drawRoundedRectangle (b.reduced (0.5f), corner, 1.0f);
    }

    void dropShadow (juce::Graphics& g, juce::Rectangle<float> b, float corner, int depth)
    {
        for (int i = depth; i > 0; --i)
        {
            const float t = (float) i / (float) depth;
            g.setColour (juce::Colours::black.withAlpha (0.13f * (1.0f - t)));
            g.drawRoundedRectangle (b.expanded ((float) i), corner + (float) i, 1.6f);
        }
    }

    void drawLamp (juce::Graphics& g, juce::Point<float> c, float r, juce::Colour col, bool lit)
    {
        if (lit)
        {
            g.setColour (col.withAlpha (0.28f));
            g.fillEllipse (c.x - r * 2.6f, c.y - r * 2.6f, r * 5.2f, r * 5.2f);
        }

        g.setColour (lit ? col : col.withMultipliedBrightness (0.22f).withAlpha (0.55f));
        g.fillEllipse (c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    }

    Look::Look()
    {
        setColour (juce::Slider::textBoxTextColourId, colour::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, colour::text);
        setColour (juce::ToggleButton::textColourId, colour::text);
        setColour (juce::TooltipWindow::backgroundColourId, colour::chassis);
        setColour (juce::TooltipWindow::textColourId, colour::text);
        setColour (juce::CaretComponent::caretColourId, colour::textBright);
        setColour (juce::PopupMenu::backgroundColourId, colour::chassis);
        setColour (juce::PopupMenu::textColourId, colour::text);
    }

    juce::Label* Look::createSliderTextBox (juce::Slider& s)
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::outlineWhenEditingColourId, colour::textDim);
        l->setFont (juce::FontOptions (font::value));
        l->getProperties().set ("pill", true);
        return l;
    }

    void Look::drawLabel (juce::Graphics& g, juce::Label& l)
    {
        //  Detect the slider's own text box by its parent rather than by a
        //  property set in createSliderTextBox(): sliders that were constructed
        //  before this look-and-feel was installed - every member of the editor
        //  and of the SHAPE panel - already had their label made by the default
        //  one, and never got the property.
        const bool isSliderReadout = dynamic_cast<juce::Slider*> (l.getParentComponent()) != nullptr;

        if (isSliderReadout || (bool) l.getProperties().getWithDefault ("pill", false))
        {
            auto r = l.getLocalBounds().toFloat().reduced (0.5f, 1.0f);
            g.setColour (juce::Colour (0xff0a0b0c));
            g.fillRoundedRectangle (r, 3.0f);
            g.setColour (colour::panelEdge.withAlpha (0.65f));
            g.drawRoundedRectangle (r, 3.0f, 1.0f);
        }

        if (l.isBeingEdited())
        {
            juce::LookAndFeel_V4::drawLabel (g, l);
            return;
        }

        g.setColour (l.findColour (juce::Label::textColourId));
        g.setFont (l.getFont());
        g.drawFittedText (l.getText(), l.getLocalBounds().reduced (3, 0),
                          l.getJustificationType(), 1, 1.0f);
    }

    void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                 float pos, float startAngle, float endAngle,
                                 juce::Slider& s)
    {
        const auto& props = s.getProperties();
        const auto accent = juce::Colour ((juce::uint32) (int) props
                                              .getWithDefault ("accent", (int) colour::low.getARGB()));
        const bool hasSecond = props.contains ("accent2");
        const auto accent2 = hasSecond
            ? juce::Colour ((juce::uint32) (int) props["accent2"]) : accent;
        const bool bipolar = (bool) props.getWithDefault ("bipolar", false);
        const bool ticks   = (bool) props.getWithDefault ("ticks", false);

        auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (3.0f);
        const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();

        //  Tick ring, outside everything.
        if (ticks)
        {
            g.setColour (colour::textDim.withAlpha (0.45f));
            for (int i = 0; i <= 20; ++i)
            {
                const float a = startAngle + (endAngle - startAngle) * (float) i / 20.0f;
                const float r0 = outer - 0.5f, r1 = outer - 3.0f;
                g.drawLine (centre.x + r0 * std::sin (a), centre.y - r0 * std::cos (a),
                            centre.x + r1 * std::sin (a), centre.y - r1 * std::cos (a), 1.0f);
            }
        }

        const float arcR = outer - (ticks ? 7.0f : 2.0f);
        const float thickness = juce::jmax (2.5f, arcR * 0.13f);
        const float bodyR = arcR - thickness * 1.6f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        //  Track.
        {
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (colour::gridStrong);
            g.strokePath (track, { thickness, juce::PathStrokeType::curved,
                                   juce::PathStrokeType::rounded });
        }

        //  Value arc. Bipolar controls grow out of 12 o'clock so that "no
        //  modulation" reads as an empty ring rather than a half-full one.
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;

        if (std::abs (angle - from) > 0.004f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                 juce::jmin (from, angle), juce::jmax (from, angle), true);

            if (hasSecond)
                g.setGradientFill ({ accent, centre.x - arcR, centre.y,
                                     accent2, centre.x + arcR, centre.y, false });
            else
                g.setColour (accent.withAlpha (0.22f));

            if (! hasSecond)
            {
                g.strokePath (value, { thickness * 2.4f, juce::PathStrokeType::curved,
                                       juce::PathStrokeType::rounded });
                g.setColour (accent);
            }

            g.strokePath (value, { thickness, juce::PathStrokeType::curved,
                                   juce::PathStrokeType::rounded });
        }

        //  Body: a dark disc with a rim, lifted at the top.
        {
            juce::ColourGradient body (colour::shellTop, centre.x, centre.y - bodyR,
                                       juce::Colour (0xff0b0c0d), centre.x, centre.y + bodyR, false);
            g.setGradientFill (body);
            g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);

            g.setColour (juce::Colours::black.withAlpha (0.55f));
            g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.2f);
            g.setColour (colour::panelHilite.withAlpha (0.30f));
            g.drawEllipse (centre.x - bodyR + 1.2f, centre.y - bodyR + 1.2f,
                           bodyR * 2.0f - 2.4f, bodyR * 2.0f - 2.4f, 1.0f);
        }

        //  Pointer: a bright spoke from the middle of the body to its rim.
        const juce::Point<float> tip { centre.x + (bodyR - 3.0f) * std::sin (angle),
                                       centre.y - (bodyR - 3.0f) * std::cos (angle) };
        const juce::Point<float> root { centre.x + bodyR * 0.28f * std::sin (angle),
                                        centre.y - bodyR * 0.28f * std::cos (angle) };

        g.setColour (colour::textBright);
        g.drawLine ({ root, tip }, juce::jmax (1.5f, bodyR * 0.075f));

        //  "live" is where the control actually IS after modulation, as opposed
        //  to where the parameter is. FOLLOW moves EDGE without moving its
        //  automation lane, and a knob that shows only the lane is lying about
        //  what the filter is doing.
        if (props.contains ("live"))
        {
            const float liveAngle = startAngle
                + juce::jlimit (0.0f, 1.0f, (float) props["live"]) * (endAngle - startAngle);

            if (std::abs (liveAngle - angle) > 0.01f)
            {
                const float r0 = arcR + thickness * 0.75f;
                const float r1 = arcR - thickness * 0.75f;
                g.setColour (colour::textBright.withAlpha (0.85f));
                g.drawLine (centre.x + r0 * std::sin (liveAngle), centre.y - r0 * std::cos (liveAngle),
                            centre.x + r1 * std::sin (liveAngle), centre.y - r1 * std::cos (liveAngle),
                            2.0f);
            }
        }
    }

    void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                 bool highlighted, bool)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const auto accent = juce::Colour ((juce::uint32) (int) b.getProperties()
                                              .getWithDefault ("accent", (int) colour::high.getARGB()));
        const bool on = b.getToggleState();
        const bool lamp = (bool) b.getProperties().getWithDefault ("lamp", false);
        const float corner = 5.0f;

        juce::ColourGradient face (colour::shellTop, r.getX(), r.getY(),
                                   juce::Colour (0xff17181a), r.getX(), r.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (r, corner);

        g.setColour (on && ! lamp ? accent : colour::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        auto textArea = r;

        if (lamp)
        {
            drawLamp (g, { r.getX() + 12.0f, r.getY() + 11.0f }, 3.0f, accent, on);
            textArea = r.withTrimmedTop (6.0f);
        }

        g.setColour (on ? colour::textBright : (highlighted ? colour::text : colour::textDim));
        g.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        g.drawText (b.getButtonText(), textArea, juce::Justification::centred, false);
    }

    //  TextButtons are the segmented MODE selector and the SHAPE disclosure.
    void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b,
                                     const juce::Colour&, bool highlighted, bool down)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const auto accent = juce::Colour ((juce::uint32) (int) b.getProperties()
                                              .getWithDefault ("accent", (int) colour::low.getARGB()));
        const bool on = b.getToggleState();

        if (on)
        {
            g.setColour (accent.withAlpha (0.18f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (accent.withAlpha (0.75f));
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            return;
        }

        juce::ColourGradient face (colour::shellTop, r.getX(), r.getY(),
                                   juce::Colour (0xff17181a), r.getX(), r.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (down || highlighted ? colour::panelHilite : colour::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }

    void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                               bool highlighted, bool)
    {
        const auto accent = juce::Colour ((juce::uint32) (int) b.getProperties()
                                              .getWithDefault ("accent", (int) colour::low.getARGB()));

        g.setColour (b.getToggleState() ? accent
                                        : (highlighted ? colour::text : colour::textDim));
        g.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
        g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
    }

    juce::Font Look::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (font::value).withStyle ("Bold"));
    }

    void Look::drawComboBox (juce::Graphics& g, int w, int h, bool down,
                             int, int, int, int, juce::ComboBox& box)
    {
        const auto accent = juce::Colour ((juce::uint32) (int) box.getProperties()
                                              .getWithDefault ("accent", (int) colour::low.getARGB()));

        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        const bool hot = down || box.isMouseOver();

        g.setColour (juce::Colour (0xff0b0c0d).withAlpha (0.85f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (accent.withAlpha (hot ? 0.85f : 0.40f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        const float cx = r.getRight() - 9.0f;
        const float cy = r.getCentreY();
        juce::Path chevron;
        chevron.startNewSubPath (cx - 3.5f, cy - 1.5f);
        chevron.lineTo (cx, cy + 2.0f);
        chevron.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (accent.withAlpha (hot ? 1.0f : 0.7f));
        g.strokePath (chevron, { 1.4f, juce::PathStrokeType::curved,
                                 juce::PathStrokeType::rounded });
    }

    void Look::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
    {
        label.setBounds (8, 0, box.getWidth() - 22, box.getHeight());
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centredLeft);
    }

    void Look::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
    {
        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h);
        g.setGradientFill ({ colour::shellTop, 0.0f, 0.0f,
                             colour::shellBottom, 0.0f, (float) h, false });
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colour::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }
}
