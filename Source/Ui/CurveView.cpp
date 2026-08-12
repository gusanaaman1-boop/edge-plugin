#include <algorithm>
#include <cmath>
#include <iterator>

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

        //  Display gating for the analyzer - NOT an audio gate. Content fades
        //  out below -66 dBFS and is not drawn at all at -84, which removes the
        //  writhing low-level hedge without touching anything audible.
        constexpr float kSpectrumFadeDb = -66.0f;
        constexpr float kSpectrumGateDb = -84.0f;
    }

    CurveView::CurveView (EdgeAudioProcessor& p) : processor (p)
    {
        setOpaque (false);

        ring.assign ((size_t) fftSize, 0.0f);
        scratch.assign ((size_t) fftSize * 2, 0.0f);
        window.resize ((size_t) fftSize);
        bandDb.assign ((size_t) numBands, kFloorDb);
        frameDb.assign ((size_t) numBands, kFloorDb);
        spectrumPoints.reserve ((size_t) numBands);

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

        //  Both path families are in component coordinates, so both die here.
        responseDirty = true;
        spectrumDirty = true;
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

    bool CurveView::updateSpectrumData()
    {
        if (! haveSpectrum)
            return false;

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

            //  Aggregate POWER across the band's bins, not the loudest bin.
            //  Peak-picking meant one noisy bin set the whole band, which is
            //  most of the low-level visual noise this display had.
            float power = 0.0f;
            for (int k = k0; k <= k1; ++k)
                power += scratch[(size_t) k] * scratch[(size_t) k];

            const float amplitude = std::sqrt (power / (float) (k1 - k0 + 1));
            frameDb[(size_t) b] = juce::Decibels::gainToDecibels (amplitude * norm, kFloorDb);
        }

        //  Restrained smoothing ACROSS adjacent display bands, then the
        //  fast-rise / slow-fall smoothing in time. Order matters: smoothing
        //  the already-slewed values would smear attacks sideways.
        for (int b = 0; b < numBands; ++b)
        {
            const float left   = frameDb[(size_t) juce::jmax (0, b - 1)];
            const float centre = frameDb[(size_t) b];
            const float right  = frameDb[(size_t) juce::jmin (numBands - 1, b + 1)];
            const float smooth = 0.25f * left + 0.5f * centre + 0.25f * right;

            auto& slot = bandDb[(size_t) b];
            slot += (smooth > slot ? kRise : kFall) * (smooth - slot);
        }

        return true;
    }

    void CurveView::timerCallback()
    {
        pullAudio();

        if (updateSpectrumData())
            spectrumDirty = true;

        //  One coherent snapshot per tick. The revision moves exactly when the
        //  geometry moved - the engine decides, not a hash of a subset here.
        const auto fresh = processor.getEngine().getDisplaySnapshot();
        const bool geometryMoved = fresh.revision != snap.revision;
        snap = fresh;

        if (geometryMoved)
            responseDirty = true;

        if (responseDirty)
            updateResponsePaths();

        if (spectrumDirty)
            updateSpectrumPath();

        //  60 Hz while something is happening - a drag, host automation, the
        //  follower working - and 30 Hz once it has been quiet for a moment.
        const auto now = juce::Time::getMillisecondCounter();
        if (geometryMoved || dragging != Grab::none)
            lastChangeMs = now;

        const int wantHz = (dragging != Grab::none || now - lastChangeMs < 400) ? 60 : 30;
        if (wantHz != refreshHz)
        {
            refreshHz = wantHz;
            startTimerHz (refreshHz);
        }

        repaint();
    }

    void CurveView::updateResponsePaths()
    {
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
        const double colourDb = (double) snap.colourTrimDb;
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

        build (currentPath, snap.currentCentre, true);
        build (targetPath,  snap.target,        false);

        if (snap.spreadActive)
        {
            build (leftPath,  snap.currentLeft,  true);
            build (rightPath, snap.currentRight, true);
        }
        else
        {
            leftPath.clear();
            rightPath.clear();
        }

        responseDirty = false;
    }

    void CurveView::updateSpectrumPath()
    {
        //  Drawn from the smoothed bands, not from raw bins - and on its own
        //  clock: a new FFT frame rebuilds this and nothing else.
        spectrumPoints.clear();

        if (haveSpectrum)
        {
            for (int b = 0; b < numBands; ++b)
            {
                const float db = bandDb[(size_t) b];

                //  Nothing at or below the gate; a fade up to full between the
                //  gate and the fade threshold.
                const float alpha = juce::jlimit (0.0f, 1.0f,
                                                  (db - kSpectrumGateDb)
                                                      / (kSpectrumFadeDb - kSpectrumGateDb));

                const float f = metric::displayMinHz
                    * std::pow (metric::displayMaxHz / metric::displayMinHz,
                                (float) b / (float) (numBands - 1));

                spectrumPoints.push_back (
                    { xForHz (f),
                      yForDb (juce::jmax (metric::displayBottomDb - 4.0f, db)),
                      alpha });
            }
        }

        spectrumDirty = false;
    }

    // -------------------------------------------------------------------------

    juce::Point<float> CurveView::handlePosition (Grab which) const noexcept
    {
        //  X comes from the PARAMETER - the single source of truth - so a drag
        //  moves the handle the same frame the gesture happens, before the
        //  audio thread has resolved anything. In FREE mode the band has
        //  travelled, and the handle travels with it.
        auto* fp = freqParam (which);
        float hz = fp->convertFrom0to1 (fp->getValue());

        if (std::abs (snap.freeTravelOctaves) > 1.0e-4f)
            hz *= std::exp2 (snap.freeTravelOctaves);

        hz = juce::jlimit (metric::displayMinHz, metric::displayMaxHz, hz);

        //  Y sits on the ghost (target) curve, which is what dragging edits.
        //  An EDGE handle sits on the response of the EDGES, with the bell
        //  taken out: leaving it in meant that touching MID's gain slid the
        //  LOW and HIGH handles - and their labels - around the display. The
        //  MID handle keeps the bell, because the bell is what it is showing.
        auto t = snap.target;
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;

        if (which != Grab::mid)
            t.midGainDb = 0.0f;
        else
        {
            //  While MID itself is being dragged, its gain read from the
            //  snapshot lags the gesture by one audio chunk. The handle is the
            //  gesture, so it uses the parameter.
            auto* gp = processor.getState().getParameter (param::midGain);
            t.midGainDb = gp->convertFrom0to1 (gp->getValue());
            t.midHz = hz;
        }

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

    //  MODE turns one edge into a literal identity. A handle for it is a
    //  control that provably does nothing, sitting on the curve at 0.0 dB and
    //  inviting a drag that changes nothing audible - which is exactly what it
    //  looked like. It is not dimmed now, it is absent.
    bool CurveView::isHandleLive (Grab which) const noexcept
    {
        //  From the snapshot, so the hit test and the drawing agree by
        //  construction - they read the same field of the same struct.
        if (which == Grab::low)  return snap.mode != (int) Mode::lowPass;
        if (which == Grab::high) return snap.mode != (int) Mode::highPass;

        return true;      // the bell is never switched off, nor is the band
    }

    void CurveView::setSelected (Grab which)
    {
        if (which == selected)
            return;

        selected = which;
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

        //  An absent handle is not grabbable either, or the invisible one still
        //  wins the hit test wherever it happens to sit.
        const bool lowLive  = isHandleLive (Grab::low);
        const bool highLive = isHandleLive (Grab::high);

        if (juce::jmin (lowLive ? dLow : 1.0e9f, highLive ? dHigh : 1.0e9f) <= r)
            return (lowLive && (! highLive || dLow <= dHigh)) ? Grab::low : Grab::high;

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

    void CurveView::testBeginDrag (Grab g, juce::Point<float> at)
    {
        dragging = g;
        beginDragInternal (at);
    }

    void CurveView::testDragTo (juce::Point<float> at)
    {
        dragInternal (at);
    }

    void CurveView::testEndDrag()
    {
        endDragInternal();
    }

    void CurveView::mouseDown (const juce::MouseEvent& e)
    {
        dragging = grabAt (e.position);
        if (dragging == Grab::none)
            return;

        beginDragInternal (e.position);
    }

    void CurveView::beginDragInternal (juce::Point<float> at)
    {
        //  Touching a handle points the inspector at it. This is the whole
        //  navigation model: one set of knobs, and the thing you just touched
        //  is what they are driving. The SELECTION is reported before the
        //  parameter gesture begins, so the inspector is already showing the
        //  right band while the drag runs.
        if (dragging != Grab::band)
        {
            setSelected (dragging);

            if (onSelectionChanged)
                onSelectionChanged (dragging == Grab::low  ? SelectedControl::low
                                  : dragging == Grab::high ? SelectedControl::high
                                                           : SelectedControl::mid);
        }

        dragStartY = at.y;
        dragStartX = at.x;

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
        dragInternal (e.position);
    }

    void CurveView::dragInternal (juce::Point<float> at)
    {
        if (dragging == Grab::none)
            return;

        //  Dragging the band moves both corners by the same number of OCTAVES,
        //  which is what keeps its width. Moving them by the same number of Hz
        //  would squash it in the bass and stretch it at the top.
        if (dragging == Grab::band)
        {
            const float octaves = std::log2 (hzForX (at.x) / hzForX (dragStartX));

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
            repaint();
            return;
        }

        if (auto* fp = freqParam (dragging))
            fp->setValueNotifyingHost (fp->convertTo0to1 (hzForX (at.x)));

        if (auto* dp = depthParam (dragging))
        {
            if (dragging == Grab::mid)
            {
                //  Gain follows the display's own dB scale, so the handle stays
                //  under the cursor instead of drifting away from it.
                const float perPixel = (metric::displayTopDb - metric::displayBottomDb)
                                     / juce::jmax (1.0f, plot.getHeight());
                const float want = dragStartDepth - (at.y - dragStartY) * perPixel;
                dp->setValueNotifyingHost (dp->convertTo0to1 (
                    juce::jlimit (-kMidMaxGainDb, kMidMaxGainDb, want)));
            }
            else
            {
                const float delta = (at.y - dragStartY)
                                  / juce::jmax (1.0f, plot.getHeight()) * 100.0f;
                dp->setValueNotifyingHost (dp->convertTo0to1 (
                    juce::jlimit (0.0f, 100.0f, dragStartDepth + delta)));
            }
        }

        //  The handles are drawn from the parameters just written, so this
        //  repaint shows the gesture in the SAME frame - the response curve
        //  follows one audio chunk later through the snapshot.
        repaint();
    }

    void CurveView::mouseUp (const juce::MouseEvent&)
    {
        endDragInternal();
    }

    void CurveView::endDragInternal()
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

        //  Nothing is rebuilt in paint: the timer owns invalidation, paint
        //  only draws what is cached. (First paint before the first tick still
        //  needs paths.)
        if (responseDirty)
            updateResponsePaths();

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
            g.drawText (hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k"
                                      : juce::String (juce::roundToInt (hz)),
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

        const auto& shape = snap.currentCentre;

        // --- spectrum: above the grid, below every response curve -------------
        //  Per-segment opacity: full above -66 dBFS, fading to nothing at -84,
        //  and no line at all below that. No fill under it - the analyzer is a
        //  reading, not a shape.
        if (spectrumPoints.size() > 1)
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());

            for (size_t i = 1; i < spectrumPoints.size(); ++i)
            {
                const auto& a = spectrumPoints[i - 1];
                const auto& b = spectrumPoints[i];
                const float alpha = 0.5f * (a.alpha + b.alpha);

                if (alpha <= 0.01f)
                    continue;

                g.setColour (colour::spectrum.withAlpha (0.34f * alpha));
                g.drawLine (a.x, a.y, b.x, b.y, 1.0f);
            }
        }

        // --- target (ghost) --------------------------------------------------
        //
        //  DASHED, and bright enough to read. The handles are TARGET handles -
        //  they sit on this line, not on the solid one - and when EDGE is part
        //  way open the two curves are far apart in the middle, which made the
        //  handles look like they were floating in empty space. Dashed means
        //  "where this is going", solid means "where it is"; at EDGE 100 they
        //  coincide and the dashes vanish under the solid line.
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());

            juce::Path dashed;
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.1f).createDashedStroke (dashed, targetPath, dashes, 2);

            g.setColour (colour::text.withAlpha (0.45f));
            g.fillPath (dashed);
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
        const auto& target = snap.target;
        const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;

        auto drawHandle = [&] (Grab which, juce::Colour accentIn, const juce::String& name)
        {
            //  MODE has made this edge a wire. Nothing to show and nothing to
            //  drag - drawing a dimmed handle at 0.0 dB only invited the
            //  question "why does moving this do nothing?".
            if (! isHandleLive (which))
                return;

            const auto accent = accentIn;

            //  Clamped into the plot. A +18 dB bell sits above the top of the
            //  scale, and an unclamped handle was drawn half outside the
            //  display with its name cut off by the border.
            auto pos = handlePosition (which);
            pos.y = juce::jlimit (plot.getY() + kHandleRadius + 2.0f,
                                  plot.getBottom() - kHandleRadius - 2.0f, pos.y);
            const bool active = (dragging == which) || (dragging == Grab::none && hovered == which);
            const bool isSelected = selected == which;
            const float r = kHandleRadius * (active ? 1.15f : 1.0f);

            g.setColour (accent.withAlpha (active ? 0.40f : 0.18f));
            g.drawVerticalLine ((int) pos.x, plot.getY(), plot.getBottom());

            //  Where the target and the current response have pulled apart,
            //  a short tick on the solid curve at the same frequency says which
            //  one the handle is going to become.
            {
                auto live = snap.currentCentre;
                const float hz = which == Grab::low  ? target.lowHz
                               : which == Grab::high ? target.highHz
                                                     : target.midHz;

                if (which != Grab::mid)
                    live.midGainDb = 0.0f;

                const float liveY = yForDb ((float) (magnitudeDb (live, sr, hz)
                                                     + snap.colourTrimDb));

                if (std::abs (liveY - pos.y) > 6.0f)
                {
                    g.setColour (accent.withAlpha (0.75f));
                    g.fillRect (pos.x - 5.0f, liveY - 1.0f, 10.0f, 2.0f);
                }
            }

            if (active)
            {
                g.setColour (accent.withAlpha (0.20f));
                g.fillEllipse (pos.x - r * 2.0f, pos.y - r * 2.0f, r * 4.0f, r * 4.0f);
            }

            g.setColour (colour::wellTop);
            g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);

            //  The selected handle is the one the knobs below are driving, so
            //  it is filled rather than merely outlined. One glance answers
            //  "what am I editing?".
            if (isSelected)
            {
                g.setColour (accent.withAlpha (0.55f));
                g.fillEllipse (pos.x - r + 3.0f, pos.y - r + 3.0f,
                               r * 2.0f - 6.0f, r * 2.0f - 6.0f);
            }

            g.setColour (accent);
            g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f,
                           isSelected ? 3.0f : 2.2f);

            //  The name sits above the handle permanently, as in the mockup;
            //  the numbers only appear while it is being touched.
            //  Clamped into the plot: a MID peak near the top of the scale
            //  puts its handle at the very edge, and an unclamped name is drawn
            //  off the display entirely.
            g.setColour (accent);
            g.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));
            auto nameBox = juce::Rectangle<int> ((int) pos.x - 30, (int) pos.y - 34, 60, 12);
            if (nameBox.getY() < (int) plot.getY() + 2)
                nameBox.setY ((int) pos.y + 22);

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

            //  Pinned to the top-left of the plot, not floating over the
            //  handle. The floating box sat exactly where the name label is and
            //  covered it, and near the top of the scale it covered the curve
            //  as well.
            g.setFont (juce::FontOptions (11.0f));
            const auto box = juce::Rectangle<int> ((int) plot.getX() + 8,
                                                   (int) plot.getY() + 8, 150, 18);

            g.setColour (colour::wellTop.withAlpha (0.92f));
            g.fillRoundedRectangle (box.toFloat(), 4.0f);
            g.setColour (accent.withAlpha (0.5f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 4.0f, 1.0f);
            g.setColour (colour::textBright);
            g.drawText (name + "   " + label, box.reduced (7, 0),
                        juce::Justification::centredLeft, false);
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
