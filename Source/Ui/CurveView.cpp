#include "CurveView.h"

#include "../PluginProcessor.h"

namespace edge::ui
{
    namespace
    {
        constexpr float kHandleRadius = 9.0f;

        //  Display smoothing. Electronic music is dense and percussive; a fast
        //  rise with a slow fall reads as "what is there" rather than as a
        //  flickering hedge.
        constexpr float kRise = 0.55f;
        constexpr float kFall = 0.12f;
        constexpr float kFloorDb = -96.0f;
    }

    CurveView::CurveView (EdgeAudioProcessor& p) : processor (p)
    {
        setOpaque (false);

        ring.assign ((size_t) fftSize, 0.0f);
        scratch.assign ((size_t) fftSize * 2, 0.0f);
        window.resize ((size_t) fftSize);
        bandDb.assign ((size_t) numBands, kFloorDb);

        for (int i = 0; i < fftSize; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                             * (float) i / (float) (fftSize - 1));

        //  The audio thread only fills the FIFO while an editor is alive.
        processor.getEngine().setAnalyzerEnabled (true);
        startTimerHz (30);
    }

    CurveView::~CurveView()
    {
        stopTimer();
        processor.getEngine().setAnalyzerEnabled (false);
    }

    void CurveView::resized()
    {
        auto r = getLocalBounds().toFloat().reduced (4.0f);
        axisGutter = r.removeFromBottom (15.0f);
        plot = r;
        curvesDirty = true;
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
        return metric::displayMinHz * std::pow (metric::displayMaxHz / metric::displayMinHz, t);
    }

    float CurveView::yForDb (float db) const noexcept
    {
        const float t = (metric::displayTopDb - db)
                      / (metric::displayTopDb - metric::displayBottomDb);
        return plot.getY() + juce::jlimit (-0.05f, 1.08f, t) * plot.getHeight();
    }

    // -------------------------------------------------------------------------

    void CurveView::pullAudio()
    {
        //  Drain whatever the audio thread left. A fixed stack buffer, so the
        //  timer allocates nothing either.
        float temp[2048];

        for (;;)
        {
            const int got = processor.getEngine().readAnalyzerSamples (temp, (int) std::size (temp));
            if (got <= 0)
                break;

            for (int i = 0; i < got; ++i)
            {
                ring[(size_t) ringWrite] = temp[i];
                ringWrite = (ringWrite + 1) % fftSize;
            }

            haveSpectrum = true;
        }
    }

    void CurveView::updateSpectrum()
    {
        if (! haveSpectrum)
            return;

        //  Unwrap the ring into the FFT workspace, oldest first.
        for (int i = 0; i < fftSize; ++i)
            scratch[(size_t) i] = ring[(size_t) ((ringWrite + i) % fftSize)] * window[(size_t) i];

        std::fill (scratch.begin() + fftSize, scratch.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (scratch.data());

        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        const float binHz = (float) (sr / fftSize);
        const float norm = 2.0f / (float) fftSize;

        for (int b = 0; b < numBands; ++b)
        {
            //  Log-spaced bands. Each takes the peak of the bins it covers, so
            //  a narrow tone does not vanish into an average at the top end.
            const float f0 = metric::displayMinHz
                * std::pow (metric::displayMaxHz / metric::displayMinHz,
                            (float) b / (float) numBands);
            const float f1 = metric::displayMinHz
                * std::pow (metric::displayMaxHz / metric::displayMinHz,
                            (float) (b + 1) / (float) numBands);

            const int k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 / binHz));
            const int k1 = juce::jlimit (k0, fftSize / 2 - 1, (int) (f1 / binHz));

            float peak = 0.0f;
            for (int k = k0; k <= k1; ++k)
                peak = juce::jmax (peak, scratch[(size_t) k]);

            const float db = juce::Decibels::gainToDecibels (peak * norm, kFloorDb);
            auto& slot = bandDb[(size_t) b];
            slot += (db > slot ? kRise : kFall) * (db - slot);
        }
    }

    void CurveView::timerCallback()
    {
        pullAudio();
        updateSpectrum();

        auto& engine = processor.getEngine();
        const auto s = engine.getDisplayShape();
        const auto t = engine.getTargetShape();

        //  Cheap change detector: rebuilding four 300-point paths 30 times a
        //  second when nothing moved is most of the plug-in's idle CPU.
        const float h = s.lowHz + s.highHz * 3.0f + s.lowDepthDb * 7.0f
                      + s.highDepthDb * 11.0f + s.lowCurve01 * 13.0f
                      + s.highCurve01 * 17.0f + s.lowRes01 * 19.0f
                      + s.highRes01 * 23.0f + s.outputDb * 29.0f
                      + s.lowShoulderDb * 37.0f + s.highShoulderDb * 41.0f
                      + t.lowHz * 43.0f + t.highHz * 47.0f + t.lowDepthDb * 53.0f
                      + engine.getColourTrimDb() * 59.0f;

        if (std::abs (h - lastShapeHash) > 1.0e-6f)
        {
            lastShapeHash = h;
            curvesDirty = true;
        }

        repaint();
    }

    void CurveView::buildCurves()
    {
        auto& engine = processor.getEngine();
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        const double colourDb = (double) engine.getColourTrimDb();
        const int points = juce::jmax (64, (int) plot.getWidth());

        auto build = [&] (juce::Path& path, const EdgeShape& shape, bool withColour)
        {
            path.clear();

            for (int i = 0; i <= points; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * (float) i / (float) points;
                const auto db = magnitudeDb (shape, sr, hzForX (x)) + (withColour ? colourDb : 0.0);
                const float y = yForDb ((float) db);

                if (i == 0) path.startNewSubPath (x, y);
                else        path.lineTo (x, y);
            }
        };

        build (currentPath, engine.getDisplayShape(), true);
        build (targetPath,  engine.getTargetShape(),  false);

        if (engine.isSpreadActive())
        {
            build (leftPath,  engine.getDisplayShape (0), true);
            build (rightPath, engine.getDisplayShape (1), true);
        }
        else
        {
            leftPath.clear();
            rightPath.clear();
        }

        //  Spectrum. Drawn from the smoothed bands, not from raw bins.
        spectrumPath.clear();
        if (haveSpectrum)
        {
            for (int b = 0; b < numBands; ++b)
            {
                const float f = metric::displayMinHz
                    * std::pow (metric::displayMaxHz / metric::displayMinHz,
                                (float) b / (float) (numBands - 1));
                const float x = xForHz (f);
                const float y = yForDb (juce::jmax (metric::displayBottomDb - 4.0f, bandDb[(size_t) b]));

                if (b == 0) spectrumPath.startNewSubPath (x, y);
                else        spectrumPath.lineTo (x, y);
            }
        }

        curvesDirty = false;
    }

    // -------------------------------------------------------------------------

    juce::Point<float> CurveView::handlePosition (Grab which) const noexcept
    {
        //  The handles are TARGET handles: they sit on the ghost curve, which
        //  is what dragging them edits. With EDGE at 100 % the two curves
        //  coincide and the handle sits on the bright line as well.
        const auto t = processor.getEngine().getTargetShape();
        const float hz = which == Grab::low  ? t.lowHz
                       : which == Grab::high ? t.highHz
                                             : t.midHz;
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;

        return { xForHz (hz), yForDb ((float) magnitudeDb (t, sr, hz)) };
    }

    void CurveView::setFreeMode (bool shouldBeFree)
    {
        if (freeMode == shouldBeFree)
            return;

        freeMode = shouldBeFree;
        hovered = Grab::none;
        setMouseCursor (juce::MouseCursor::NormalCursor);
        repaint();
    }

    CurveView::Grab CurveView::grabAt (juce::Point<float> p) const noexcept
    {
        const float r = kHandleRadius * 2.4f;

        //  Frequency proximity decides, not distance in pixels: the two handles
        //  can sit on top of each other vertically when both edges are deep.
        const float xLow  = handlePosition (Grab::low).x;
        const float xHigh = handlePosition (Grab::high).x;
        const float dLow  = std::abs (p.x - xLow);
        const float dHigh = std::abs (p.x - xHigh);

        //  The MID handle is checked FIRST, and by real distance rather than
        //  by frequency alone: it lives inside the band the other two define,
        //  so an x-only test would hand every grab in the middle to whichever
        //  edge happened to be nearer.
        const auto midPos = handlePosition (Grab::mid);
        if (midPos.getDistanceFrom (p) <= kHandleRadius * 1.8f)
            return Grab::mid;

        if (juce::jmin (dLow, dHigh) <= r)
            return dLow <= dHigh ? Grab::low : Grab::high;

        //  In FREE the whole band is a target: grabbing between the handles
        //  slides it across the spectrum with its width intact. That is what
        //  the mode is for, so it only exists there.
        if (freeMode && p.x > xLow && p.x < xHigh)
            return Grab::band;

        return Grab::none;
    }

    juce::RangedAudioParameter* CurveView::freqParam (Grab g) const noexcept
    {
        return processor.getState().getParameter (g == Grab::low  ? param::lowFreq
                                                : g == Grab::high ? param::highFreq
                                                                  : param::midFreq);
    }

    //  The MID handle's vertical axis is its GAIN, not a depth - dragging up
    //  makes a peak, dragging down makes a notch, which is the whole point of
    //  it being one control rather than two.
    juce::RangedAudioParameter* CurveView::depthParam (Grab g) const noexcept
    {
        return processor.getState().getParameter (g == Grab::low  ? param::lowDepth
                                                : g == Grab::high ? param::highDepth
                                                                  : param::midGain);
    }

    void CurveView::mouseMove (const juce::MouseEvent& e)
    {
        const auto h = grabAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            setMouseCursor (h == Grab::none  ? juce::MouseCursor::NormalCursor
                          : h == Grab::band  ? juce::MouseCursor::LeftRightResizeCursor
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
        dragStartX = e.position.x;

        auto* lowF  = processor.getState().getParameter (param::lowFreq);
        auto* highF = processor.getState().getParameter (param::highFreq);
        dragStartLowHz  = lowF->convertFrom0to1 (lowF->getValue());
        dragStartHighHz = highF->convertFrom0to1 (highF->getValue());

        if (dragging == Grab::band)
        {
            lowF->beginChangeGesture();
            highF->beginChangeGesture();
            return;
        }

        if (auto* d = depthParam (dragging))
            dragStartDepth = d->convertFrom0to1 (d->getValue());

        freqParam (dragging)->beginChangeGesture();
        depthParam (dragging)->beginChangeGesture();
    }

    void CurveView::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging == Grab::none)
            return;

        //  Dragging the band moves both corners by the same number of OCTAVES,
        //  which is what keeps its width. Moving them by the same number of Hz
        //  would squash it in the bass and stretch it at the top.
        if (dragging == Grab::band)
        {
            const float octaves = std::log2 (hzForX (e.position.x) / hzForX (dragStartX));

            auto* lowF  = processor.getState().getParameter (param::lowFreq);
            auto* highF = processor.getState().getParameter (param::highFreq);

            //  Clamp the SHIFT, not each corner: clamping them independently
            //  would let one hit its end and the other keep going, which is
            //  exactly the width change the octave move exists to avoid.
            const float wantLow  = dragStartLowHz  * std::exp2 (octaves);
            const float wantHigh = dragStartHighHz * std::exp2 (octaves);

            const float limited = juce::jlimit (
                std::log2 (kLowFreqMin / dragStartLowHz),
                juce::jmin (std::log2 (kLowFreqMax / dragStartLowHz),
                            std::log2 (kHighFreqMax / dragStartHighHz)),
                octaves);

            juce::ignoreUnused (wantLow, wantHigh);

            lowF->setValueNotifyingHost (lowF->convertTo0to1 (
                dragStartLowHz * std::exp2 (limited)));
            highF->setValueNotifyingHost (highF->convertTo0to1 (
                dragStartHighHz * std::exp2 (limited)));
            return;
        }

        if (auto* fp = freqParam (dragging))
            fp->setValueNotifyingHost (fp->convertTo0to1 (hzForX (e.position.x)));

        if (auto* dp = depthParam (dragging))
        {
            if (dragging == Grab::mid)
            {
                //  Gain follows the display's own dB scale, so the handle stays
                //  under the cursor instead of drifting away from it.
                const float perPixel = (metric::displayTopDb - metric::displayBottomDb)
                                     / juce::jmax (1.0f, plot.getHeight());
                const float want = dragStartDepth - (e.position.y - dragStartY) * perPixel;
                dp->setValueNotifyingHost (dp->convertTo0to1 (
                    juce::jlimit (-kMidMaxGainDb, kMidMaxGainDb, want)));
            }
            else
            {
                const float delta = (e.position.y - dragStartY)
                                  / juce::jmax (1.0f, plot.getHeight()) * 100.0f;
                dp->setValueNotifyingHost (dp->convertTo0to1 (
                    juce::jlimit (0.0f, 100.0f, dragStartDepth + delta)));
            }
        }
    }

    void CurveView::mouseUp (const juce::MouseEvent&)
    {
        if (dragging == Grab::none)
            return;

        if (dragging == Grab::band)
        {
            processor.getState().getParameter (param::lowFreq)->endChangeGesture();
            processor.getState().getParameter (param::highFreq)->endChangeGesture();
        }
        else
        {
            freqParam (dragging)->endChangeGesture();
            depthParam (dragging)->endChangeGesture();
        }

        dragging = Grab::none;
    }

    void CurveView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto g = grabAt (e.position);
        if (g == Grab::none)
            return;

        //  Back to a full cut, which is this control's default and the setting
        //  BAND is built around. The frequency is left where it is: a double
        //  click that also threw the corner away would destroy more than it
        //  fixed.
        if (auto* dp = depthParam (g))
        {
            dp->beginChangeGesture();
            dp->setValueNotifyingHost (dp->getDefaultValue());
            dp->endChangeGesture();
        }
    }

    // -------------------------------------------------------------------------

    void CurveView::paint (juce::Graphics& g)
    {
        auto frame = getLocalBounds().toFloat().reduced (1.0f);
        paintWell (g, frame, 8.0f);

        if (curvesDirty)
            buildCurves();

        // --- grid ------------------------------------------------------------
        g.setFont (juce::FontOptions (9.5f));

        for (float hz : { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                          2000.0f, 5000.0f, 10000.0f, 20000.0f })
        {
            const float x = xForHz (hz);
            const bool decade = hz == 100.0f || hz == 1000.0f || hz == 10000.0f;
            g.setColour (decade ? colour::gridStrong.withAlpha (0.75f) : colour::grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (colour::textDim.withAlpha (0.8f));
            g.drawText (hz >= 1000.0f ? juce::String (hz / 1000.0f, 0) + "k" : juce::String (hz, 0),
                        (int) x - 18, (int) axisGutter.getY(), 36, 12,
                        juce::Justification::centred, false);
        }

        for (float db : { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -30.0f, -36.0f })
        {
            const float y = yForDb (db);
            g.setColour (db == 0.0f ? colour::gridStrong : colour::grid);
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (colour::textDim.withAlpha (0.7f));
            g.drawText (juce::String ((int) db), (int) plot.getX() + 3, (int) y - 11, 26, 11,
                        juce::Justification::left, false);
        }

        auto& engine = processor.getEngine();
        const auto shape = engine.getDisplayShape();

        // --- spectrum, behind everything -------------------------------------
        if (! spectrumPath.isEmpty())
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (colour::spectrum.withAlpha (0.34f));
            g.strokePath (spectrumPath, { 1.0f, juce::PathStrokeType::curved });
        }

        // --- target (ghost) --------------------------------------------------
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (colour::text.withAlpha (0.22f));
            g.strokePath (targetPath, { 1.0f, juce::PathStrokeType::curved });
        }

        // --- per-channel traces when SPREAD is doing something ----------------
        if (! leftPath.isEmpty())
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (colour::low.withAlpha (0.35f));
            g.strokePath (leftPath, { 1.0f, juce::PathStrokeType::curved });
            g.setColour (colour::high.withAlpha (0.35f));
            g.strokePath (rightPath, { 1.0f, juce::PathStrokeType::curved });
        }

        // --- current response -------------------------------------------------
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());

            juce::Path fill (currentPath);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();

            juce::Graphics::ScopedSaveState fillClip (g);
            g.reduceClipRegion (fill);

            //  The tint has to die away quickly below the line. Filling the
            //  whole well with an orange-to-cyan gradient turns the middle of
            //  the display - where the curve is flat and the fill is tallest -
            //  into a slab of olive.
            juce::ColourGradient tint (colour::low.withAlpha (0.30f), plot.getX(), 0.0f,
                                       colour::high.withAlpha (0.30f), plot.getRight(), 0.0f, false);
            g.setGradientFill (tint);
            g.fillRect (plot);

            juce::ColourGradient fade (juce::Colours::transparentBlack,
                                       plot.getCentreX(), plot.getY(),
                                       colour::wellBottom.withAlpha (0.92f),
                                       plot.getCentreX(), plot.getY() + plot.getHeight() * 0.62f,
                                       false);
            g.setGradientFill (fade);
            g.fillRect (plot);
        }

        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.expanded (0.0f, 6.0f).toNearestInt());

            g.setGradientFill ({ colour::low.withAlpha (0.30f), plot.getX(), 0.0f,
                                 colour::high.withAlpha (0.30f), plot.getRight(), 0.0f, false });
            g.strokePath (currentPath, { 6.0f, juce::PathStrokeType::curved });

            g.setGradientFill ({ colour::low, plot.getX(), 0.0f,
                                 colour::high, plot.getRight(), 0.0f, false });
            g.strokePath (currentPath, { 2.2f, juce::PathStrokeType::curved });
        }

        // --- handles ----------------------------------------------------------
        const auto target = engine.getTargetShape();

        //  An edge that MODE has turned into an identity still has a handle -
        //  you may want to place it before switching - but it must not look
        //  like it is doing something.
        const int mode = (int) std::lround (
            processor.getState().getParameter (param::mode)->convertFrom0to1 (
                processor.getState().getParameter (param::mode)->getValue()));

        auto drawHandle = [&] (Grab which, juce::Colour accentIn, const juce::String& name)
        {
            const bool edgeLive = which == Grab::mid
                                    ? std::abs (processor.getEngine().getTargetShape().midGainDb) > 0.05f
                                    : which == Grab::low ? mode != (int) Mode::lowPass
                                                         : mode != (int) Mode::highPass;
            const auto accent = edgeLive ? accentIn
                                         : accentIn.withSaturation (0.15f).withAlpha (0.45f);

            const auto pos = handlePosition (which);
            const bool active = (dragging == which) || (dragging == Grab::none && hovered == which);
            const float r = kHandleRadius * (active ? 1.15f : 1.0f);

            g.setColour (accent.withAlpha (active ? 0.40f : 0.18f));
            g.drawVerticalLine ((int) pos.x, plot.getY(), plot.getBottom());

            if (active)
            {
                g.setColour (accent.withAlpha (0.20f));
                g.fillEllipse (pos.x - r * 2.0f, pos.y - r * 2.0f, r * 4.0f, r * 4.0f);
            }

            g.setColour (colour::wellTop);
            g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);
            g.setColour (accent);
            g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 2.2f);

            //  The name sits above the handle permanently, as in the mockup;
            //  the numbers only appear while it is being touched.
            //  Clamped into the plot: a MID peak near the top of the scale
            //  puts its handle at the very edge, and an unclamped name is drawn
            //  off the display entirely.
            g.setColour (accent);
            g.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
            auto nameBox = juce::Rectangle<int> ((int) pos.x - 30, (int) pos.y - 32, 60, 12);
            if (nameBox.getY() < (int) plot.getY() + 2)
                nameBox.setY ((int) pos.y + 20);

            g.drawText (name, nameBox, juce::Justification::centred, false);

            if (! active)
                return;

            const float hz = which == Grab::low  ? target.lowHz
                           : which == Grab::high ? target.highHz
                                                 : target.midHz;

            const float depthDb = which == Grab::low  ? target.lowDepthDb
                                : which == Grab::high ? target.highDepthDb
                                                      : target.midGainDb;

            const juce::String label = formatHz (hz) + "   "
                + (which != Grab::mid && depthDb <= kDepthFloorDb + 0.5f
                       ? juce::String ("CUT")
                       : (depthDb > 0.0f ? "+" : "") + juce::String (depthDb, 1) + " dB");

            g.setFont (juce::FontOptions (10.5f));
            auto box = juce::Rectangle<int> ((int) pos.x - 62, (int) pos.y - 50, 124, 16);
            box = box.constrainedWithin (plot.toNearestInt());

            g.setColour (colour::wellTop.withAlpha (0.92f));
            g.fillRoundedRectangle (box.toFloat(), 4.0f);
            g.setColour (accent.withAlpha (0.5f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (colour::textBright);
            g.drawText (label, box, juce::Justification::centred, false);
        };

        //  In FREE the band itself is a control, so it is drawn as one.
        if (freeMode)
        {
            const float xLow  = handlePosition (Grab::low).x;
            const float xHigh = handlePosition (Grab::high).x;
            const bool lit = dragging == Grab::band
                          || (dragging == Grab::none && hovered == Grab::band);

            auto slab = juce::Rectangle<float> (xLow, plot.getY(),
                                                juce::jmax (2.0f, xHigh - xLow),
                                                plot.getHeight());

            g.setColour (colour::textBright.withAlpha (lit ? 0.055f : 0.022f));
            g.fillRect (slab);

            if (lit)
            {
                g.setColour (colour::textBright.withAlpha (0.30f));
                g.drawText ("MOVE BAND", slab.toNearestInt().withHeight (14)
                                             .translated (0, 10),
                            juce::Justification::centred, false);
            }
        }

        drawHandle (Grab::low,  colour::low,  "LOW");
        drawHandle (Grab::high, colour::high, "HIGH");

        //  MID is drawn in the neutral colour: it is neither edge, and a third
        //  accent would stop the other two from meaning anything.
        drawHandle (Grab::mid, colour::textBright, "MID");

        juce::ignoreUnused (shape);
    }
}
