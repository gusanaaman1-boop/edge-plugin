#include <cmath>

#include "Theme.h"

namespace edge::ui
{
    juce::String formatHz (float hz)
    {
        if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 2) + " kHz";
        if (hz >= 100.0f)   return juce::String (juce::roundToInt (hz)) + " Hz";
        return juce::String (hz, 1) + " Hz";
    }

    juce::Path edgeCutPanel (juce::Rectangle<float> b, float radius, float cutW, float cutH)
    {
        //  Rounded on three corners; the top-right is the cut: in along the
        //  top, then a straight fall to the right edge. The fall's angle is
        //  the motif - the same 14:10 run/fall the wordmark's tail uses.
        juce::Path p;
        p.startNewSubPath (b.getX() + radius, b.getY());
        p.lineTo (b.getRight() - cutW, b.getY());
        p.lineTo (b.getRight(), b.getY() + cutH);
        p.lineTo (b.getRight(), b.getBottom() - radius);
        p.addArc (b.getRight() - radius * 2.0f, b.getBottom() - radius * 2.0f,
                  radius * 2.0f, radius * 2.0f, juce::MathConstants<float>::halfPi,
                  juce::MathConstants<float>::pi);
        p.lineTo (b.getX() + radius, b.getBottom());
        p.addArc (b.getX(), b.getBottom() - radius * 2.0f,
                  radius * 2.0f, radius * 2.0f, juce::MathConstants<float>::pi,
                  juce::MathConstants<float>::pi * 1.5f);
        p.lineTo (b.getX(), b.getY() + radius);
        p.addArc (b.getX(), b.getY(), radius * 2.0f, radius * 2.0f,
                  juce::MathConstants<float>::pi * 1.5f, juce::MathConstants<float>::twoPi);
        p.closeSubPath();
        return p;
    }

    void drawWordmark (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour ink)
    {
        //  Stroked geometric capitals: cap height 16, stroke 3, letter width
        //  11, gap 7 - all from the 8 px grid's halves. The final E's middle
        //  arm runs on 12 px flat and falls 9 px: the EDGE CUT, growing out of
        //  the name itself.
        const float capH = 16.0f, w = 11.0f, gap = 7.0f, stroke = 3.0f;
        const float tailRun = 12.0f, tailFall = 9.0f;

        const float totalW = 4.0f * w + 3.0f * gap + tailRun + 4.0f;
        const float x0 = area.getCentreX() - totalW * 0.5f;
        const float y0 = area.getCentreY() - capH * 0.5f;

        juce::Path m;

        auto letterE = [&] (float x, bool withTail)
        {
            m.startNewSubPath (x, y0);
            m.lineTo (x, y0 + capH);                        // spine
            m.startNewSubPath (x, y0);
            m.lineTo (x + w, y0);                           // top arm
            m.startNewSubPath (x, y0 + capH);
            m.lineTo (x + w, y0 + capH);                    // bottom arm
            m.startNewSubPath (x, y0 + capH * 0.5f);

            if (withTail)
            {
                //  The middle arm becomes the motif: flat, then the fall.
                m.lineTo (x + w + tailRun, y0 + capH * 0.5f);
                m.lineTo (x + w + tailRun + tailFall * 0.8f, y0 + capH * 0.5f + tailFall);
            }
            else
            {
                m.lineTo (x + w * 0.78f, y0 + capH * 0.5f);
            }
        };

        auto letterD = [&] (float x)
        {
            m.startNewSubPath (x, y0);
            m.lineTo (x, y0 + capH);
            m.startNewSubPath (x, y0);
            m.lineTo (x + w * 0.5f, y0);
            m.addArc (x - w * 0.1f, y0, w * 1.2f, capH,
                      0.0f, juce::MathConstants<float>::pi, false);
            m.lineTo (x, y0 + capH);
        };

        auto letterG = [&] (float x)
        {
            //  An open arc with the bar driving into the centre.
            m.addArc (x, y0, w * 1.15f, capH,
                      juce::MathConstants<float>::pi * 0.45f,
                      juce::MathConstants<float>::pi * 1.97f, true);
            m.startNewSubPath (x + w * 1.15f, y0 + capH * 0.58f);
            m.lineTo (x + w * 0.55f, y0 + capH * 0.58f);
        };

        letterE (x0, false);
        letterD (x0 + w + gap);
        letterG (x0 + 2.0f * (w + gap));
        letterE (x0 + 3.0f * (w + gap), true);

        g.setColour (ink);
        g.strokePath (m, { stroke, juce::PathStrokeType::curved,
                           juce::PathStrokeType::rounded });
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
        //  v0.17 universal knob: black-glass centre, 2 px track at 18 %,
        //  4 px value arc with a 6 px endpoint dot, a 1 px titanium ring -
        //  no pointer line, no rim, no ticks, no gloss.
        const auto& props = s.getProperties();
        auto accent = juce::Colour ((juce::uint32) (int) props
                                        .getWithDefault ("accent", (int) colour::text.getARGB()));
        const bool bipolar = (bool) props.getWithDefault ("bipolar", false);
        const bool hover = s.isMouseOverOrDragging();
        const bool down  = s.isMouseButtonDown();

        auto bounds = juce::Rectangle<int> (x, y, w, h).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float arcR = radius - 3.0f;
        const float angle = startAngle + pos * (endAngle - startAngle);

        if (down)
        {
            g.setColour (accent.withAlpha (0.16f));
            g.drawEllipse (centre.x - radius, centre.y - radius,
                           radius * 2.0f, radius * 2.0f, 2.0f);
        }

        //  Background track.
        {
            juce::Path track;
            track.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f,
                                 startAngle, endAngle, true);
            g.setColour (colour::text.withAlpha (0.18f));
            g.strokePath (track, { 2.0f, juce::PathStrokeType::curved,
                                   juce::PathStrokeType::rounded });
        }

        //  Centre: flat black glass with a faint titanium ring and a contact
        //  shadow below it.
        const float bodyR = arcR - 8.0f;
        {
            g.setColour (juce::Colours::black.withAlpha (0.30f));
            g.fillEllipse (centre.x - bodyR, centre.y - bodyR + 2.0f, bodyR * 2.0f, bodyR * 2.0f);
            g.setColour (hover ? colour::controlHover : colour::controlCentre);
            g.fillEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
            g.setColour (colour::titanBright.withAlpha (0.28f));
            g.drawEllipse (centre.x - bodyR, centre.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);
        }

        //  Value arc + endpoint dot. Bipolar grows out of 12 o'clock.
        const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
        const bool hasSecond = props.contains ("accent2");

        //  LED orbit (the hero EDGE knob): 36 dots outside the arc, violet -
        //  lit up to the LIVE position, dim beyond it. Travelled = movement.
        if ((bool) props.getWithDefault ("ledOrbit", false))
        {
            const float livePos = juce::jlimit (0.0f, 1.0f,
                (float) (double) props.getWithDefault ("live", (double) pos));
            const float dotR = radius + 1.5f;

            for (int i = 0; i < 36; ++i)
            {
                const float t = (float) i / 35.0f;
                const float a = startAngle + t * (endAngle - startAngle);
                const bool lit = t <= juce::jmax (pos, livePos) + 0.001f;

                g.setColour (colour::movement.withAlpha (lit ? 0.82f : 0.10f));
                const float d = lit ? 2.4f : 2.0f;
                g.fillEllipse (centre.x + dotR * std::sin (a) - d * 0.5f,
                               centre.y - dotR * std::cos (a) - d * 0.5f, d, d);
            }
        }

        if (std::abs (angle - from) > 0.004f || ! bipolar)
        {
            const float lo = juce::jmin (from, angle), hi = juce::jmax (from, angle);
            auto stroke = [&] (float a0, float a1, juce::Colour c, float width)
            {
                if (a1 - a0 < 0.004f) return;
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, a0, a1, true);
                g.setColour (c);
                g.strokePath (arc, { width, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded });
            };

            if (hasSecond)
            {
                const auto accent2 = juce::Colour ((juce::uint32) (int) props["accent2"]);
                const float mid = juce::jlimit (lo, hi, (startAngle + endAngle) * 0.5f);
                stroke (lo, mid, accent, 4.0f);
                stroke (mid, hi, accent2, 4.0f);
            }
            else
            {
                stroke (lo, hi, accent, 4.0f);
            }

            //  Endpoint dot: 6 px, 7 on hover, in the accent.
            const float dotD = hover ? 7.0f : 6.0f;
            g.setColour (accent);
            g.fillEllipse (centre.x + arcR * std::sin (angle) - dotD * 0.5f,
                           centre.y - arcR * std::cos (angle) - dotD * 0.5f, dotD, dotD);
        }

        //  Bipolar centre-zero mark on the track.
        if (bipolar)
        {
            const float za = (startAngle + endAngle) * 0.5f;
            g.setColour (colour::text.withAlpha (0.45f));
            g.drawLine (centre.x + (arcR + 3.0f) * std::sin (za),
                        centre.y - (arcR + 3.0f) * std::cos (za),
                        centre.x + (arcR - 3.0f) * std::sin (za),
                        centre.y - (arcR - 3.0f) * std::cos (za), 1.5f);
        }

        //  The value, centred inside. Wider box than the body: "20.00 kHz"
        //  overhangs a small circle on the flat dark deck rather than clip.
        if ((bool) props.getWithDefault ("valueInside", false))
        {
            const float valueSize = (float) (double) props.getWithDefault ("valueSize", 13.0);
            const float boxW = juce::jmax (bodyR * 2.0f + 12.0f, valueSize * 6.0f);
            g.setColour (hover ? colour::numeric : colour::text);
            g.setFont (juce::FontOptions (valueSize));
            g.drawText (s.getTextFromValue (s.getValue()),
                        juce::Rectangle<int> ((int) (centre.x - boxW * 0.5f),
                                              (int) (centre.y - valueSize * 0.5f + 2.0f),
                                              (int) boxW, (int) valueSize + 4),
                        juce::Justification::centred, false);
        }

        //  Live marker (the sound) and the violet push arc from the pointer
        //  (the lane) to it - EDGE only, driven by resolved engine state.
        if (props.contains ("live") && ! (bool) props.getWithDefault ("ledOrbit", false))
        {
            const float liveAngle = startAngle
                + juce::jlimit (0.0f, 1.0f, (float) props["live"]) * (endAngle - startAngle);

            if (std::abs (liveAngle - angle) > 0.01f)
            {
                juce::Path push;
                push.addCentredArc (centre.x, centre.y, arcR + 4.0f, arcR + 4.0f, 0.0f,
                                    juce::jmin (angle, liveAngle),
                                    juce::jmax (angle, liveAngle), true);
                g.setColour (colour::movement.withAlpha (0.85f));
                g.strokePath (push, { 2.5f, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded });
            }
        }

        if ((bool) props.getWithDefault ("ledOrbit", false) && props.contains ("live"))
        {
            //  EDGE's two endpoints: ice-white base (the parameter), violet
            //  live (after FOLLOW). They coincide exactly at FOLLOW 0.
            const float liveAngle = startAngle
                + juce::jlimit (0.0f, 1.0f, (float) props["live"]) * (endAngle - startAngle);

            g.setColour (colour::text);
            g.fillEllipse (centre.x + arcR * std::sin (angle) - 3.0f,
                           centre.y - arcR * std::cos (angle) - 3.0f, 6.0f, 6.0f);

            if (std::abs (liveAngle - angle) > 0.01f)
            {
                g.setColour (colour::movement);
                g.fillEllipse (centre.x + arcR * std::sin (liveAngle) - 3.5f,
                               centre.y - arcR * std::cos (liveAngle) - 3.5f, 7.0f, 7.0f);
            }
        }

        //  Keyboard focus: a 1 px ice-white ring 2 px outside the component.
        if (s.hasKeyboardFocus (false))
        {
            g.setColour (colour::text.withAlpha (0.60f));
            g.drawEllipse (centre.x - radius - 2.0f, centre.y - radius - 2.0f,
                           radius * 2.0f + 4.0f, radius * 2.0f + 4.0f, 1.0f);
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

    static bool drawArrowInstead (juce::Graphics& g, juce::TextButton& b)
    {
        const auto t = b.getButtonText();
        if (t != "<" && t != ">")
            return false;

        auto r = b.getLocalBounds().toFloat().reduced (9.0f);
        juce::Path arrow;
        if (t == "<")
            arrow.addTriangle (r.getRight(), r.getY(), r.getRight(), r.getBottom(),
                               r.getX(), r.getCentreY());
        else
            arrow.addTriangle (r.getX(), r.getY(), r.getX(), r.getBottom(),
                               r.getRight(), r.getCentreY());

        g.setColour (colour::textDim);
        g.fillPath (arrow);
        return true;
    }

    void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b,
                               bool highlighted, bool)
    {
        if (drawArrowInstead (g, b))
            return;

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
