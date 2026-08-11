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
        g.fillRect (b);

        //  A wide, very low-contrast lift behind the middle. It is what stops
        //  the background reading as a flat rectangle without ever becoming a
        //  visible shape of its own.
        juce::ColourGradient glow (colour::shellGlow.withAlpha (0.55f),
                                   b.getCentreX(), b.getY() + b.getHeight() * 0.28f,
                                   colour::shellGlow.withAlpha (0.0f),
                                   b.getCentreX(), b.getY() + b.getHeight() * 1.15f, true);
        glow.isRadial = true;
        g.setGradientFill (glow);
        g.fillRect (b);
    }

    void dropShadow (juce::Graphics& g, juce::Rectangle<float> b, float corner, int depth)
    {
        for (int i = depth; i > 0; --i)
        {
            const float t = (float) i / (float) depth;
            g.setColour (juce::Colours::black.withAlpha (0.10f * (1.0f - t)));
            g.drawRoundedRectangle (b.expanded ((float) i), corner + (float) i, 1.6f);
        }
    }

    Look::Look()
    {
        setColour (juce::Slider::textBoxTextColourId, colour::text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Label::textColourId, colour::text);
        setColour (juce::ToggleButton::textColourId, colour::text);
        setColour (juce::TooltipWindow::backgroundColourId, colour::panelTop);
        setColour (juce::TooltipWindow::textColourId, colour::text);
        setColour (juce::CaretComponent::caretColourId, colour::textBright);
    }

    juce::Label* Look::createSliderTextBox (juce::Slider& s)
    {
        auto* l = juce::LookAndFeel_V4::createSliderTextBox (s);
        l->setJustificationType (juce::Justification::centred);
        l->setColour (juce::Label::outlineWhenEditingColourId, colour::textDim);
        l->setFont (juce::FontOptions (11.0f));
        return l;
    }

    void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h,
                                 float pos, float startAngle, float endAngle,
                                 juce::Slider& s)
    {
        const auto accent = juce::Colour ((juce::uint32) (int) s.getProperties()
                                              .getWithDefault ("accent", (int) colour::low.getARGB()));

        auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const float thickness = juce::jmax (2.5f, radius * 0.17f);
        const auto centre = bounds.getCentre();
        const float arcR = radius - thickness;
        const float angle = startAngle + pos * (endAngle - startAngle);

        //  Recessed body, so the knob sits IN the panel rather than on it.
        {
            juce::ColourGradient body (colour::panelBottom, centre.x, centre.y - radius,
                                       colour::shellBottom, centre.x, centre.y + radius, false);
            g.setGradientFill (body);
            g.fillEllipse (bounds.reduced (thickness * 0.4f));

            g.setColour (colour::panelHilite.withAlpha (0.35f));
            g.drawEllipse (bounds.reduced (thickness * 0.4f), 1.0f);
        }

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (colour::gridStrong);
        g.strokePath (track, { thickness, juce::PathStrokeType::curved,
                               juce::PathStrokeType::rounded });

        if (pos > 0.0015f)
        {
            juce::Path value;
            value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);

            //  Two passes: a wide, faint one is the glow, the crisp one is the
            //  value. Cheaper and better behaved than a real blur.
            g.setColour (accent.withAlpha (0.22f));
            g.strokePath (value, { thickness * 2.6f, juce::PathStrokeType::curved,
                                   juce::PathStrokeType::rounded });
            g.setColour (accent);
            g.strokePath (value, { thickness, juce::PathStrokeType::curved,
                                   juce::PathStrokeType::rounded });
        }

        //  Pointer: a short spoke that stops well short of the arc.
        const float tipR = arcR - thickness * 1.1f;
        const juce::Point<float> tip { centre.x + tipR * std::sin (angle),
                                       centre.y - tipR * std::cos (angle) };
        const juce::Point<float> root { centre.x + tipR * 0.30f * std::sin (angle),
                                        centre.y - tipR * 0.30f * std::cos (angle) };

        g.setColour (colour::textBright);
        g.drawLine ({ root, tip }, juce::jmax (1.6f, thickness * 0.5f));
    }

    void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b,
                                 bool highlighted, bool)
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.0f);
        const auto accent = juce::Colour ((juce::uint32) (int) b.getProperties()
                                              .getWithDefault ("accent", (int) colour::high.getARGB()));
        const bool on = b.getToggleState();
        const float corner = 5.0f;

        if (on)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.fillRoundedRectangle (r.expanded (2.0f), corner + 2.0f);
        }

        juce::ColourGradient face (on ? accent.withAlpha (0.30f) : colour::panelTop,
                                   r.getX(), r.getY(),
                                   on ? accent.withAlpha (0.14f) : colour::panelBottom,
                                   r.getX(), r.getBottom(), false);
        g.setGradientFill (face);
        g.fillRoundedRectangle (r, corner);

        g.setColour (on ? accent : (highlighted ? colour::panelHilite : colour::panelEdge));
        g.drawRoundedRectangle (r.reduced (0.5f), corner, 1.0f);

        g.setColour (on ? colour::textBright : (highlighted ? colour::text : colour::textDim));
        g.setFont (juce::FontOptions (r.getHeight() > 26.0f ? 12.0f : 11.0f).withStyle ("Bold"));
        g.drawText (b.getButtonText(), r, juce::Justification::centred, false);
    }
}

namespace edge::ui
{
    juce::Font Look::getComboBoxFont (juce::ComboBox&)
    {
        return juce::Font (juce::FontOptions (11.0f).withStyle ("Bold"));
    }

    void Look::drawComboBox (juce::Graphics& g, int w, int h, bool down,
                             int, int, int, int, juce::ComboBox& box)
    {
        const auto accent = juce::Colour ((juce::uint32) (int) box.getProperties()
                                              .getWithDefault ("accent", (int) colour::low.getARGB()));

        auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        const bool hot = down || box.isMouseOver();

        g.setColour (colour::shellBottom.withAlpha (0.80f));
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (accent.withAlpha (hot ? 0.85f : 0.40f));
        g.drawRoundedRectangle (r, 4.0f, 1.0f);

        //  A small chevron rather than JUCE's default triangle button.
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
        g.setGradientFill ({ colour::panelTop, 0.0f, 0.0f,
                             colour::panelBottom, 0.0f, (float) h, false });
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (colour::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }
}
