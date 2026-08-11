#include "CurveView.h"

#include "../PluginProcessor.h"

namespace edge::ui
{
    namespace
    {
        constexpr float kHandleRadius = 8.0f;
    }

    CurveView::CurveView (EdgeAudioProcessor& p) : processor (p)
    {
        setOpaque (false);
        buildSlopeBox (lowSlope,  param::lowCurve,  colour::low);
        buildSlopeBox (highSlope, param::highCurve, colour::high);

        //  Sync once up front. Waiting for the first timer tick left the boxes
        //  blank in any context that paints before the timer runs - which is
        //  every offline render, and the first frame a host shows.
        refreshSlopeBoxes();
        startTimerHz (30);
    }

    void CurveView::buildSlopeBox (juce::ComboBox& box, const char* paramId,
                                   juce::Colour accent)
    {
        box.getProperties().set ("accent", (int) accent.getARGB());

        for (int i = 0; i < kNumSlopeChoices; ++i)
            box.addItem (kSlopeChoices[i].name, i + 1);

        box.setColour (juce::ComboBox::textColourId, accent);
        box.setColour (juce::PopupMenu::textColourId, colour::text);
        box.setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.25f));
        box.setColour (juce::PopupMenu::highlightedTextColourId, colour::textBright);
        box.setTooltip ("Slope");

        box.onChange = [this, &box, paramId]
        {
            const int id = box.getSelectedId();
            if (id <= 0)
                return;

            if (auto* p = processor.getState().getParameter (paramId))
            {
                const float want = kSlopeChoices[id - 1].curvePercent;

                //  Only write when it actually differs, or refreshing the box
                //  from the parameter would write the parameter back.
                if (std::abs (p->convertFrom0to1 (p->getValue()) - want) > 0.05f)
                {
                    p->beginChangeGesture();
                    p->setValueNotifyingHost (p->convertTo0to1 (want));
                    p->endChangeGesture();
                }
            }
        };

        addAndMakeVisible (box);
    }

    //  The combos follow the Curve knobs, not the other way round: turning a
    //  knob to a value between two slopes clears the selection and shows the
    //  live slope as placeholder text.
    void CurveView::refreshSlopeBoxes()
    {
        auto sync = [this] (juce::ComboBox& box, const char* paramId)
        {
            auto* p = processor.getState().getParameter (paramId);
            if (p == nullptr)
                return;

            const float percent = p->convertFrom0to1 (p->getValue());
            const int index = slopeIndexFor (percent);

            box.setTextWhenNothingSelected (slopeTextFor (percent));

            if (box.getSelectedId() != index + 1)
                box.setSelectedId (index >= 0 ? index + 1 : 0, juce::dontSendNotification);
        };

        sync (lowSlope,  param::lowCurve);
        sync (highSlope, param::highCurve);
    }

    CurveView::~CurveView() { stopTimer(); }

    void CurveView::resized()
    {
        auto boxes = getLocalBounds().reduced (10, 8);
        const int boxWidth = juce::jlimit (78, 104, boxes.getWidth() / 9);
        lowSlope.setBounds (boxes.removeFromLeft (boxWidth).removeFromTop (20));
        highSlope.setBounds (boxes.removeFromRight (boxWidth).removeFromTop (20));
        refreshSlopeBoxes();

        //  A dedicated gutter for the frequency scale. Drawing it inside the
        //  plot let a steep cut run straight through the numbers.
        auto r = getLocalBounds().toFloat().reduced (2.0f);
        axisGutter = r.removeFromBottom (14.0f);
        plot = r;
        lastShapeHash = 0.0f;
    }

    float CurveView::xForHz (float hz) const noexcept
    {
        const float t = std::log2 (juce::jlimit (metric::displayMinHz, metric::displayMaxHz, hz)
                                       / metric::displayMinHz)
                      / std::log2 (metric::displayMaxHz / metric::displayMinHz);
        return plot.getX() + t * plot.getWidth();
    }

    float CurveView::hzForX (float x) const noexcept
    {
        const float t = juce::jlimit (0.0f, 1.0f, (x - plot.getX()) / plot.getWidth());
        return metric::displayMinHz
             * std::pow (metric::displayMaxHz / metric::displayMinHz, t);
    }

    float CurveView::yForDb (float db) const noexcept
    {
        const float t = (metric::displayTopDb - db)
                      / (metric::displayTopDb - metric::displayBottomDb);
        return plot.getY() + juce::jlimit (-0.05f, 1.05f, t) * plot.getHeight();
    }

    void CurveView::timerCallback()
    {
        const auto s = processor.getEngine().getDisplayShape();

        //  Cheap change detector: repainting a 300-point curve 30 times a second
        //  when nothing moved is most of the plug-in's idle CPU.
        const float h = s.lowHz + s.highHz * 3.0f + s.lowDepthDb * 7.0f
                      + s.highDepthDb * 11.0f + s.lowCurve01 * 13.0f
                      + s.highCurve01 * 17.0f + s.lowRes01 * 19.0f
                      + s.highRes01 * 23.0f + s.outputDb * 29.0f
                      + s.lowShoulderDb * 37.0f + s.highShoulderDb * 41.0f
                      + processor.getEngine().getColourTrimDb() * 31.0f;

        refreshSlopeBoxes();

        if (std::abs (h - lastShapeHash) > 1.0e-6f)
        {
            lastShapeHash = h;
            curvePath.clear();
            repaint();
        }
    }

    juce::Point<float> CurveView::handlePosition (Grab which) const noexcept
    {
        const auto s = processor.getEngine().getDisplayShape();
        const float hz = which == Grab::low ? s.lowHz : s.highHz;

        const double db = magnitudeDb (s, processor.getSampleRate() > 0.0
                                              ? processor.getSampleRate() : 48000.0, hz)
                        + (double) processor.getEngine().getColourTrimDb();

        return { xForHz (hz), yForDb ((float) db) };
    }

    CurveView::Grab CurveView::grabAt (juce::Point<float> p) const noexcept
    {
        if (lowSlope.getBounds().contains (p.toInt())
            || highSlope.getBounds().contains (p.toInt()))
            return Grab::none;

        const float r = kHandleRadius * 2.4f;

        //  Frequency proximity decides, not distance in pixels: the two handles
        //  can sit on top of each other vertically when both edges are deep.
        const float dLow  = std::abs (p.x - handlePosition (Grab::low).x);
        const float dHigh = std::abs (p.x - handlePosition (Grab::high).x);

        if (juce::jmin (dLow, dHigh) > r)
            return Grab::none;

        return dLow <= dHigh ? Grab::low : Grab::high;
    }

    juce::RangedAudioParameter* CurveView::freqParam (Grab g) const noexcept
    {
        return processor.getState().getParameter (g == Grab::low ? param::lowFreq
                                                                 : param::highFreq);
    }

    juce::RangedAudioParameter* CurveView::depthParam (Grab g) const noexcept
    {
        return processor.getState().getParameter (g == Grab::low ? param::lowDepth
                                                                 : param::highDepth);
    }

    void CurveView::mouseMove (const juce::MouseEvent& e)
    {
        const auto h = grabAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            setMouseCursor (h == Grab::none ? juce::MouseCursor::NormalCursor
                                            : juce::MouseCursor::DraggingHandCursor);
            repaint();
        }
    }

    void CurveView::mouseExit (const juce::MouseEvent&)
    {
        if (hovered != Grab::none) { hovered = Grab::none; repaint(); }
    }

    void CurveView::mouseDown (const juce::MouseEvent& e)
    {
        dragging = grabAt (e.position);
        if (dragging == Grab::none)
            return;

        dragStartY = e.position.y;
        if (auto* d = depthParam (dragging))
            dragStartDepth = d->convertFrom0to1 (d->getValue());

        freqParam (dragging)->beginChangeGesture();
        depthParam (dragging)->beginChangeGesture();
    }

    void CurveView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging == Grab::none)
            return;

        if (auto* fp = freqParam (dragging))
            fp->setValueNotifyingHost (fp->convertTo0to1 (hzForX (e.position.x)));

        if (auto* dp = depthParam (dragging))
        {
            //  A full-height drag covers the whole Depth range, which puts the
            //  shelf-to-cut transition roughly a fifth of the way up from the
            //  bottom - the same place it sits on the knob.
            const float delta = (e.position.y - dragStartY) / juce::jmax (1.0f, plot.getHeight())
                              * 100.0f;
            dp->setValueNotifyingHost (dp->convertTo0to1 (juce::jlimit (0.0f, 100.0f,
                                                                        dragStartDepth + delta)));
        }
    }

    void CurveView::mouseUp (const juce::MouseEvent&)
    {
        if (dragging == Grab::none)
            return;

        freqParam (dragging)->endChangeGesture();
        depthParam (dragging)->endChangeGesture();
        dragging = Grab::none;
    }

    void CurveView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto g = grabAt (e.position);
        if (g == Grab::none)
            return;

        //  Depth back to 0 - the neutral state, which is the useful reset here.
        //  The frequency is left where it is: a double click that also threw the
        //  corner back to 20 Hz would destroy more than it fixed.
        if (auto* dp = depthParam (g))
        {
            dp->beginChangeGesture();
            dp->setValueNotifyingHost (dp->convertTo0to1 (0.0f));
            dp->endChangeGesture();
        }
    }

    void CurveView::paint (juce::Graphics& g)
    {
        auto frame = getLocalBounds().toFloat().reduced (3.0f);

        dropShadow (g, frame, 8.0f);

        //  Recessed display: graded face, a hairline that is lighter along the
        //  top edge, so the panel reads as sunk into the shell.
        g.setGradientFill ({ colour::panelTop, frame.getX(), frame.getY(),
                             colour::panelBottom, frame.getX(), frame.getBottom(), false });
        g.fillRoundedRectangle (frame, 8.0f);

        // --- grid ------------------------------------------------------------
        g.setFont (juce::FontOptions (10.0f));

        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                          2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForHz (hz);
            const bool decade = hz == 20.0f || hz == 100.0f || hz == 1000.0f
                             || hz == 10000.0f || hz == 20000.0f;
            g.setColour (decade ? colour::gridStrong.withAlpha (0.7f) : colour::grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (colour::textDim);
            g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k"
                                      : juce::String (hz, 0),
                        (int) x - 18, (int) axisGutter.getY(), 36, 12,
                        juce::Justification::centred, false);
        }

        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f })
        {
            const float y = yForDb (db);
            g.setColour (db == 0.0f ? colour::gridStrong : colour::grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (colour::textDim);
            g.drawText (juce::String ((int) db), (int) plot.getX() + 3, (int) y - 12, 30, 12,
                        juce::Justification::left, false);
        }

        //  Each edge's own territory, tinted very faintly, so it is obvious at
        //  a glance which half of the display belongs to which accent.
        {
            const auto sh = processor.getEngine().getDisplayShape();
            const float xLow = xForHz (sh.lowHz);
            const float xHigh = xForHz (sh.highHz);

            g.setGradientFill ({ colour::low.withAlpha (0.07f), plot.getX(), 0.0f,
                                 colour::low.withAlpha (0.0f), xLow, 0.0f, false });
            g.fillRect (plot.withRight (juce::jmax (plot.getX(), xLow)));

            g.setGradientFill ({ colour::high.withAlpha (0.0f), xHigh, 0.0f,
                                 colour::high.withAlpha (0.07f), plot.getRight(), 0.0f, false });
            g.fillRect (plot.withLeft (juce::jmin (plot.getRight(), xHigh)));
        }

        // --- the response ----------------------------------------------------
        const auto shape = processor.getEngine().getDisplayShape();
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        const double colourDb = (double) processor.getEngine().getColourTrimDb();

        if (curvePath.isEmpty())
        {
            const int points = juce::jmax (64, (int) plot.getWidth());

            for (int i = 0; i <= points; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) points;
                const auto db = magnitudeDb (shape, sr, hzForX (x)) + colourDb;
                const float y = yForDb ((float) db);

                if (i == 0) curvePath.startNewSubPath (x, y);
                else        curvePath.lineTo (x, y);
            }
        }

        //  Fill under the curve: strong just beneath the line, gone by the
        //  bottom of the plot, so the shape reads without flooding the panel.
        {
            juce::Path fill (curvePath);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();

            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (fill);

            juce::ColourGradient vertical (colour::textBright.withAlpha (0.13f),
                                           plot.getCentreX(), plot.getY(),
                                           colour::textBright.withAlpha (0.0f),
                                           plot.getCentreX(), plot.getBottom(), false);
            g.setGradientFill (vertical);
            g.fillRect (plot);

            juce::ColourGradient tint (colour::low.withAlpha (0.20f), plot.getX(), 0.0f,
                                       colour::high.withAlpha (0.20f), plot.getRight(), 0.0f,
                                       false);
            g.setGradientFill (tint);
            g.fillRect (plot);
        }

        //  Glow then line. Two strokes of the same path, wide and faint under
        //  narrow and bright.
        juce::ColourGradient stroke (colour::low, plot.getX(), 0.0f,
                                     colour::high, plot.getRight(), 0.0f, false);

        juce::ColourGradient halo (colour::low.withAlpha (0.28f), plot.getX(), 0.0f,
                                   colour::high.withAlpha (0.28f), plot.getRight(), 0.0f, false);
        g.setGradientFill (halo);
        g.strokePath (curvePath, { 6.0f, juce::PathStrokeType::curved });

        g.setGradientFill (stroke);
        g.strokePath (curvePath, { 2.2f, juce::PathStrokeType::curved });

        // --- handles ---------------------------------------------------------
        auto drawHandle = [&] (Grab which, juce::Colour accent, const juce::String& name)
        {
            const auto pos = handlePosition (which);
            const bool active = (dragging == which) || (dragging == Grab::none && hovered == which);
            const float r = kHandleRadius * (active ? 1.2f : 1.0f);

            g.setColour (accent.withAlpha (active ? 0.40f : 0.20f));
            g.drawVerticalLine ((int) pos.x, plot.getY(), plot.getBottom());

            if (active)
            {
                g.setColour (accent.withAlpha (0.20f));
                g.fillEllipse (pos.x - r * 2.0f, pos.y - r * 2.0f, r * 4.0f, r * 4.0f);
            }

            g.setColour (colour::panelBottom);
            g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
            g.setColour (accent.withAlpha (0.35f));
            g.fillEllipse (pos.x - r * 0.42f, pos.y - r * 0.42f, r * 0.84f, r * 0.84f);
            g.setColour (accent);
            g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 2.2f);

            if (! active)
                return;

            const float hz = which == Grab::low ? shape.lowHz : shape.highHz;
            const float depthDb = which == Grab::low ? shape.lowDepthDb : shape.highDepthDb;

            const juce::String label = name + "   " + formatHz (hz) + "   "
                + (depthDb <= kDepthFloorDb + 0.5f ? juce::String ("CUT")
                                                   : juce::String (depthDb, 1) + " dB");

            g.setFont (juce::FontOptions (11.0f).withStyle ("Bold"));
            const int w = juce::jmax (140, (int) std::ceil (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label)) + 16);
            //  Below the combo row, so the two never collide.
            auto box = juce::Rectangle<int> ((int) pos.x - w / 2, (int) plot.getY() + 30, w, 18);
            box = box.constrainedWithin (plot.toNearestInt());

            g.setColour (colour::shellBottom.withAlpha (0.92f));
            g.fillRoundedRectangle (box.toFloat(), 4.0f);
            g.setColour (accent.withAlpha (0.45f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (accent);
            g.drawText (label, box, juce::Justification::centred, false);
        };

        drawHandle (Grab::low,  colour::low,  "LOW");
        drawHandle (Grab::high, colour::high, "HIGH");

        g.setColour (colour::panelEdge);
        g.drawRoundedRectangle (frame.reduced (0.5f), 8.0f, 1.0f);
        g.setColour (colour::panelHilite.withAlpha (0.30f));
        g.drawLine (frame.getX() + 8.0f, frame.getY() + 1.0f,
                    frame.getRight() - 8.0f, frame.getY() + 1.0f, 1.0f);
    }
}
