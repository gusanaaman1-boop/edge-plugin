#include <algorithm>
#include <cmath>
#include <iterator>

#include "CurveView.h"

#include "../PluginProcessor.h"

namespace edge::ui
{
    namespace
    {
        //  v0.12 sizes: 9 px unselected handle, 11 px selected + 15 px halo,
        //  11 px live puck, 7 px hollow target diamond. These are DIAMETERS;
        //  the grab radius stays larger than the visual.
        constexpr float kHandleRadius = 4.5f;
        constexpr float kHandleRadiusSel = 5.5f;
        constexpr float kHaloRadius = 7.5f;
        constexpr float kGrabRadius = 11.0f;

        //  Analyzer constants, v0.13. Time-based ballistics (35 ms attack,
        //  220 ms release), 96 log bands over 20 Hz - 20 kHz, a visible range
        //  of 0 to -66 dBFS with a fade to nothing by -84. Display gating
        //  only - the audio is untouched.
        constexpr float kFloorDb = -96.0f;
        constexpr float kSpectrumFadeDb = -66.0f;
        constexpr float kSpectrumGateDb = -84.0f;
        constexpr float kAttackSeconds  = 0.028f;
        constexpr float kReleaseSeconds = 0.260f;
        constexpr float kSpecMinHz = 20.0f, kSpecMaxHz = 20000.0f;
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
        //  v0.16 full-bleed: the view IS the window. The readable content sits
        //  inside fixed insets - 24 px sides, 68 px under the floating header,
        //  136 px above the floating dock - and the surface runs edge to edge.
        auto r = getLocalBounds().toFloat();
        r.removeFromTop (76.0f);        // right under the floating header
        r.removeFromBottom (200.0f);    // dock 170 + graph bottom inset 22 + gap
        r.removeFromLeft (30.0f);
        r.removeFromRight (30.0f);

        axisGutter = r.removeFromBottom (14.0f);
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

        //  Two calibrations against the Hann window, and the louder wins.
        //
        //    - kSumNorm: from Parseval, the amplitude a band's summed POWER
        //      implies. Right for broadband content.
        //    - kPeakNorm: the amplitude the strongest single bin implies
        //      (coherent gain 0.5). Right for a tone, whose main lobe a narrow
        //      low band cannot contain - the sum there under-reads, the peak
        //      does not. Worst case is the 1.4 dB Hann scalloping loss.
        const float kSumNorm  = std::sqrt (32.0f / 3.0f) / (float) fftSize;
        const float kPeakNorm = 4.0f / (float) fftSize;

        for (int b = 0; b < numBands; ++b)
        {
            const float f0 = kSpecMinHz * std::pow (kSpecMaxHz / kSpecMinHz,
                                                    (float) b / (float) numBands);
            const float f1 = kSpecMinHz * std::pow (kSpecMaxHz / kSpecMinHz,
                                                    (float) (b + 1) / (float) numBands);

            const int k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 / binHz));
            const int k1 = juce::jlimit (k0, fftSize / 2 - 1, (int) (f1 / binHz));

            float power = 0.0f, peak = 0.0f;
            for (int k = k0; k <= k1; ++k)
            {
                power += scratch[(size_t) k] * scratch[(size_t) k];
                peak = juce::jmax (peak, scratch[(size_t) k]);
            }

            const float amplitude = juce::jmax (std::sqrt (power) * kSumNorm,
                                                peak * kPeakNorm);
            frameDb[(size_t) b] = juce::Decibels::gainToDecibels (amplitude, kFloorDb);
        }

        //  Spatial smoothing, radius 2 bands (1-2-3-2-1 kernel), BEFORE the
        //  temporal ballistics so attacks are not smeared sideways.
        //  Then 35 ms attack / 220 ms release, from real elapsed time.
        const auto now = juce::Time::getMillisecondCounter();
        const float dt = lastSpectrumMs == 0 ? 0.033f
                        : juce::jlimit (0.001f, 0.5f, (float) (now - lastSpectrumMs) * 0.001f);
        lastSpectrumMs = now;

        const float aAtk = 1.0f - std::exp (-dt / kAttackSeconds);
        const float aRel = 1.0f - std::exp (-dt / kReleaseSeconds);

        for (int b = 0; b < numBands; ++b)
        {
            //  Peak-preserving smoothing, radius 2, in the POWER domain.
            //  Averaging in dB crushed tones: one lit band among floor-level
            //  neighbours read (3*(-18) + 6*(-96)) / 9 = -70 dB - a 50 dB lie
            //  at 10 kHz. A max-combine keeps the band's own level exact and
            //  gives its neighbours skirts.
            static constexpr float skirt[5] = { 0.25f, 0.55f, 1.0f, 0.55f, 0.25f };

            float smooth = kFloorDb;
            for (int o = -2; o <= 2; ++o)
            {
                const int idx = juce::jlimit (0, numBands - 1, b + o);
                smooth = juce::jmax (smooth,
                                     frameDb[(size_t) idx]
                                       + juce::Decibels::gainToDecibels (skirt[o + 2]));
            }

            auto& slot = bandDb[(size_t) b];
            slot += (smooth > slot ? aAtk : aRel) * (smooth - slot);
        }

        return true;
    }

    void CurveView::timerCallback()
    {
        pullAudio();

        //  The analyzer holds its own 30 Hz clock: interaction lifts the
        //  timer to 60 Hz for the response, and the spectrum must not follow
        //  it up - its ballistics are tuned for ~33 ms frames.
        const auto tickNow = juce::Time::getMillisecondCounter();
        if (tickNow - lastSpectrumTickMs >= 30)
        {
            lastSpectrumTickMs = tickNow;
            if (updateSpectrumData())
                spectrumDirty = true;
        }

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

    //  The analyzer's own vertical mapping, from the v0.14 spec - NOT the
    //  response curve's gain axis, which squeezed all useful signal into the
    //  top strip of the display:
    //
    //      0 dBFS -> 12 %      -36 dBFS -> 65 %
    //    -18 dBFS -> 36 %      -66 dBFS -> 96 %
    static float spectrumY01 (float db) noexcept
    {
        //  Recalibrated so the analyzer OCCUPIES the graph the way the
        //  approved mockup does: per-band pink-noise energy (~ -38 dBFS per
        //  96th-octave band for a -18 dBFS total) lands around 42 % of graph
        //  height instead of hugging the floor. Band VALUES are untouched -
        //  this is the display mapping only.
        struct P { float db, y; };
        static constexpr P map[] = { { 0.0f, 0.26f }, { -18.0f, 0.42f },
                                     { -36.0f, 0.62f }, { -66.0f, 0.88f } };

        if (db >= map[0].db) return map[0].y;

        for (int i = 1; i < 4; ++i)
            if (db >= map[i].db)
                return map[i - 1].y + (map[i].y - map[i - 1].y)
                         * (map[i - 1].db - db) / (map[i - 1].db - map[i].db);

        return 1.0f;
    }

    float CurveView::spectrumLineAlphaForDb (float db) noexcept
    {
        //  52 % nominal, times the -66/-84 gate.
        const float gate = juce::jlimit (0.0f, 1.0f,
                                         (db - (-84.0f)) / ((-66.0f) - (-84.0f)));
        return 0.52f * gate;
    }

    void CurveView::updateSpectrumPath()
    {
        spectrumPoints.clear();

        if (haveSpectrum)
        {
            for (int b = 0; b < numBands; ++b)
            {
                const float db = bandDb[(size_t) b];
                const float alpha = juce::jlimit (0.0f, 1.0f,
                                                  (db - kSpectrumGateDb)
                                                      / (kSpectrumFadeDb - kSpectrumGateDb));

                const float f = kSpecMinHz * std::pow (kSpecMaxHz / kSpecMinHz,
                                                       ((float) b + 0.5f) / (float) numBands);

                spectrumPoints.push_back (
                    { xForHz (f), plot.getY() + spectrumY01 (db) * plot.getHeight(), alpha });
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

    //  Semantic display names. Internally LP's active section is the HIGH
    //  edge, but the user selected "LP" and every label must say so - the
    //  internal name never reaches the screen.
    juce::String CurveView::nameFor (Grab which) const
    {
        if (which == Grab::mid)
            return "MID";

        if (which == Grab::high)
            return snap.mode == (int) Mode::lowPass ? "LP" : "HIGH";

        return snap.mode == (int) Mode::highPass ? "HP" : "LOW";
    }

    //  The persistent readout, bottom left: the active identity and where it
    //  is, always visible. `LP (bullet) 3.2 kHz` in LP, both edges in BAND and
    //  FREE, plus the MID line whenever the bell is doing something.
    juce::String CurveView::readoutText() const
    {
        auto* lowF  = processor.getState().getParameter (param::lowFreq);
        auto* highF = processor.getState().getParameter (param::highFreq);
        auto* midF  = processor.getState().getParameter (param::midFreq);
        auto* midG  = processor.getState().getParameter (param::midGain);

        const float lowHz  = lowF->convertFrom0to1 (lowF->getValue());
        const float highHz = highF->convertFrom0to1 (highF->getValue());

        //  One bullet, decoded once - concatenating raw UTF-8 bytes into
        //  string literals and re-decoding at the end double-encoded it.
        const auto sep = juce::String::fromUTF8 (" \xe2\x80\xa2 ");

        //  While EDGE is travelling the readout shows the journey itself:
        //  `LP (bullet) 2.4 kHz -> 1.2 kHz` - where the cutoff IS, then where
        //  it is going. Arrived (or parked), it shows one number.
        const auto arrow = juce::String::fromUTF8 (" \xe2\x86\x92 ");

        auto travelling = [&] (float liveHz, float targetHz)
        {
            return std::abs (std::log2 (juce::jmax (1.0f, liveHz)
                                          / juce::jmax (1.0f, targetHz))) > 0.06f;
        };

        juce::String text;

        if (snap.mode == (int) Mode::lowPass)
        {
            const float live = snap.currentCentre.highHz;
            text = "LP" + sep + (travelling (live, highHz)
                                     ? formatHz (live) + arrow + formatHz (highHz)
                                     : formatHz (highHz));
        }
        else if (snap.mode == (int) Mode::highPass)
        {
            const float live = snap.currentCentre.lowHz;
            text = "HP" + sep + (travelling (live, lowHz)
                                     ? formatHz (live) + arrow + formatHz (lowHz)
                                     : formatHz (lowHz));
        }
        else
        {
            text = "LOW" + sep + formatHz (lowHz)
                 + "   HIGH" + sep + formatHz (highHz);
        }

        const float gain = midG->convertFrom0to1 (midG->getValue());
        if (std::abs (gain) > 0.05f || selected == Grab::mid)
            text << "   MID" << sep << formatHz (midF->convertFrom0to1 (midF->getValue()))
                 << sep << (gain > 0 ? "+" : "") << juce::String (gain, 1) << " dB";

        return text;
    }

    float CurveView::responseLengthInside (juce::Rectangle<float> area) const noexcept
    {
        //  Walk the cached path in ~4 px steps and add up what falls inside.
        const float total = currentPath.getLength();
        float inside = 0.0f;

        for (float d = 0.0f; d < total; d += 4.0f)
            if (area.contains (currentPath.getPointAlongPath (d)))
                inside += 4.0f;

        return inside;
    }

    juce::Rectangle<int> CurveView::readoutBounds() const noexcept
    {
        //  Matches the box paint() draws, with a conservative width - clamped
        //  by the same limit paint() applies, so the fixed inspector's region
        //  is honestly excluded here too.
        int w = 260;
        if (readoutRightLimit > 0)
            w = juce::jmin (w, readoutRightLimit - ((int) plot.getX() + 8));

        return { (int) plot.getX() + 8, (int) plot.getBottom() - 26, juce::jmax (60, w), 22 };
    }

    void CurveView::updateTrail (int side, juce::Point<float> puck)
    {
        auto& t = trail[side];
        const auto now = juce::Time::getMillisecondCounter();

        //  Append only real movement; expire after 180 ms, cap at the last 8
        //  positions and 52 px of length.
        if (t.empty() || std::abs (t.back().x - puck.x) > 0.5f
                      || std::abs (t.back().y - puck.y) > 0.5f)
            t.push_back ({ puck.x, puck.y, now });

        t.erase (std::remove_if (t.begin(), t.end(),
                                 [now, &puck] (const TrailPoint& p)
                                 { return now - p.t > 180
                                       || std::abs (p.x - puck.x) > 52.0f; }),
                 t.end());

        while (t.size() > 8)
            t.erase (t.begin());
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
        const float r = kGrabRadius;

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
        if (midPos.getDistanceFrom (p) <= kGrabRadius)
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

        //  Empty graph space closes the inspector - the second half of the
        //  selection contract.
        if (dragging == Grab::none)
        {
            if (onSelectionCleared)
                onSelectionCleared();
            return;
        }

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
        //  One continuous instrument surface, edge to edge, with the v0.17
        //  three-stop vertical ramp - restrained depth, no texture.
        {
            juce::ColourGradient ramp (colour::graphTop, 0.0f, 0.0f,
                                       colour::graphBottom, 0.0f, (float) getHeight(), false);
            ramp.addColour (0.5, colour::graph);
            g.setGradientFill (ramp);
            g.fillRect (getLocalBounds());
        }

        //  Nothing is rebuilt in paint: the timer owns invalidation, paint
        //  only draws what is cached. (First paint before the first tick still
        //  needs paths.)
        if (responseDirty)
            updateResponsePaths();

        // --- dot lattice: dots at the major intersections, no lines ------------
        //  A line grid reads as instrumentation and competes with the
        //  analyzer; the lattice keeps orientation and goes quiet.
        g.setFont (juce::FontOptions (9.0f));

        static constexpr float hzMarks[] = { 20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f,
                                             2000.0f, 5000.0f, 10000.0f, 20000.0f };
        static constexpr float dbMarks[] = { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f, -18.0f,
                                             -24.0f, -30.0f, -36.0f };

        const juce::Colour dot (0xff8292A8);
        for (float hz : hzMarks)
        {
            const float px = xForHz (hz);

            for (float db : dbMarks)
            {
                const float py = yForDb (db);
                g.setColour (dot.withAlpha (0.08f));
                g.fillEllipse (px - 0.625f, py - 0.625f, 1.25f, 1.25f);
            }

            g.setColour (colour::text.withAlpha (0.46f));
            g.drawText (hz >= 1000.0f ? juce::String (juce::roundToInt (hz / 1000.0f)) + "k"
                                      : juce::String (juce::roundToInt (hz)),
                        (int) px - 18, (int) axisGutter.getY(), 36, 12,
                        juce::Justification::centred, false);
        }

        //  The one line that stays: the 0 dB reference.
        g.setColour (colour::text.withAlpha (0.12f));
        g.drawHorizontalLine ((int) yForDb (0.0f), plot.getX(), plot.getRight());

        for (float db : dbMarks)
        {
            g.setColour (colour::text.withAlpha (0.46f));
            g.drawText (juce::String ((int) db), (int) plot.getX() + 3,
                        (int) yForDb (db) - 11, 26, 11, juce::Justification::left, false);
        }

        const auto& shape = snap.currentCentre;

        // --- spectrum: above the grid, below every response curve -------------
        //  Two layers: a gradient body (#557895, 22 % at the line falling to
        //  4 % at the floor) and a 1.5 px line (#7898B2, 44-55 % with level).
        //  Still quieter than the response, the path and the handles.
        if (spectrumPoints.size() > 1)
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());

            juce::Path fillPath;
            fillPath.startNewSubPath (spectrumPoints.front().x, plot.getBottom());

            for (const auto& pt : spectrumPoints)
                fillPath.lineTo (pt.x, pt.alpha > 0.01f ? pt.y : plot.getBottom());

            fillPath.lineTo (spectrumPoints.back().x, plot.getBottom());
            fillPath.closeSubPath();

            //  The serious steel-blue studio palette: #405A70 at the line
            //  through #263D51 to #101B27 at the floor. No purple, no glow
            //  cloud, no particles.
            juce::ColourGradient grad (juce::Colour (0xff405A70).withAlpha (0.30f),
                                       plot.getCentreX(), plot.getY() + 0.26f * plot.getHeight(),
                                       juce::Colour (0xff101B27).withAlpha (0.05f),
                                       plot.getCentreX(), plot.getBottom(), false);
            grad.addColour (0.55, juce::Colour (0xff263D51).withAlpha (0.18f));
            g.setGradientFill (grad);
            g.fillPath (fillPath);

            const juce::Colour line (0xff6F879C);
            for (size_t i = 1; i < spectrumPoints.size(); ++i)
            {
                const auto& a = spectrumPoints[i - 1];
                const auto& b = spectrumPoints[i];

                //  Per-segment level from the y mapping is not recoverable, so
                //  the alpha comes from the band dB via the shared helper.
                const float db = bandDb[(size_t) juce::jlimit (0, numBands - 1, (int) i)];
                const float alpha = spectrumLineAlphaForDb (db);

                if (a.alpha <= 0.01f && b.alpha <= 0.01f)
                    continue;

                g.setColour (line.withAlpha (alpha));
                g.drawLine (a.x, a.y, b.x, b.y, 1.5f);
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
            const float dashes[] = { 5.0f, 4.0f };
            juce::PathStrokeType (1.25f).createDashedStroke (dashed, targetPath, dashes, 2);

            //  Where the dotted journey rides the cut, the dash hands over -
            //  drawing both stacked dots onto dashes and buried the route.
            juce::Graphics::ScopedSaveState dashClip (g);

            if (snap.mode == (int) Mode::lowPass)
                g.reduceClipRegion (juce::Rectangle<int> ((int) plot.getX(), 0,
                    (int) (xForHz (snap.currentCentre.highHz) - plot.getX()), getHeight()));
            else if (snap.mode == (int) Mode::highPass)
                g.reduceClipRegion (juce::Rectangle<int> ((int) xForHz (snap.currentCentre.lowHz), 0,
                    getWidth(), getHeight()));
            else if (snap.mode == (int) Mode::band)
                g.reduceClipRegion (juce::Rectangle<int> ((int) xForHz (snap.currentCentre.lowHz), 0,
                    (int) juce::jmax (0.0f, xForHz (snap.currentCentre.highHz)
                                              - xForHz (snap.currentCentre.lowHz)), getHeight()));

            g.setColour (colour::target.withAlpha (0.22f + 0.10f * juce::jlimit (0.0f, 1.0f, snap.liveEdge01 / 0.12f)));
            g.fillPath (dashed);
        }

        // --- per-channel traces when SPREAD is doing something ----------------
        if (! leftPath.isEmpty())
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.toNearestInt());
            g.setColour (colour::low.withAlpha (0.22f));
            g.strokePath (leftPath, { 1.0f, juce::PathStrokeType::curved });
            g.setColour (colour::high.withAlpha (0.22f));
            g.strokePath (rightPath, { 1.0f, juce::PathStrokeType::curved });
        }

        // --- current response: neutral ice-white, 2.25 px ---------------------
        //  Band identity comes from a 28 px wash of amber or cyan around each
        //  edge's OWN handle - never from interpolating the two across the
        //  whole curve, which is where the muddy green came from.
        {
            juce::Graphics::ScopedSaveState clip (g);
            g.reduceClipRegion (plot.expanded (0.0f, 6.0f).toNearestInt());

            //  Item 10: at EDGE 0 the flat truthful line dims to 48 % with no
            //  glow, ramping to full presence by EDGE 12 % - a parked filter
            //  should not read as a broken horizontal line.
            const float wake = juce::jlimit (0.0f, 1.0f, snap.liveEdge01 / 0.12f);
            const float coreA = 0.48f + 0.52f * wake;

            if (wake > 0.01f)
            {
                g.setColour (colour::response.withAlpha (0.05f * wake));
                g.strokePath (currentPath, { 8.0f, juce::PathStrokeType::curved });
                g.setColour (colour::response.withAlpha (0.16f * wake));
                g.strokePath (currentPath, { 4.0f, juce::PathStrokeType::curved });
            }

            g.setColour (colour::response.withAlpha (coreA));
            g.strokePath (currentPath, { 2.0f, juce::PathStrokeType::curved });

            auto tintNear = [&] (Grab which, juce::Colour accent)
            {
                if (! isHandleLive (which))
                    return;

                const float x = handlePosition (which).x;
                juce::Graphics::ScopedSaveState tintClip (g);
                g.reduceClipRegion (juce::Rectangle<int> ((int) (x - 24.0f), (int) plot.getY(),
                                                          48, (int) plot.getHeight() + 8));
                g.setColour (accent.withAlpha (0.30f + 0.70f * wake));
                g.strokePath (currentPath, { 2.0f, juce::PathStrokeType::curved });
            };

            tintNear (Grab::low,  colour::low);
            tintNear (Grab::high, colour::high);
        }

        //  Hard clip for everything from here on: the journey, handles and
        //  readout may never paint outside the content region - a stray
        //  cyan artifact at y=0 proved that trusting geometry is not enough.
        juce::Graphics::ScopedSaveState contentClip (g);
        g.reduceClipRegion (plot.toNearestInt().expanded (10, 10));

        // --- EDGE PATH: dots riding the TARGET response curve ------------------
        //  The horizontal rail was a rendering defect: the journey the sound
        //  will take IS the target curve. The dotted route is sampled along
        //  the cached targetPath by ACCUMULATED ARC LENGTH - every dot's y
        //  comes from the curve - from the live corner's projection down the
        //  cut to the destination diamond near the graph floor.
        if (snap.mode != (int) Mode::freeBand)
        {
            const double sr = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
            auto* edgeParam = processor.getState().getParameter (param::edge);
            const float baseEdge01 = edgeParam->convertFrom0to1 (edgeParam->getValue()) * 0.01f;
            const float push01 = juce::jlimit (0.0f, 1.0f,
                                               std::abs (snap.liveEdge01 - baseEdge01));

            const float endY = juce::jmin (yForDb (-54.0f), plot.getBottom() - 12.0f);

            //  Arrived = no journey. At EDGE ~100 % the route has zero length
            //  and the diamond just floated under the handle cluster as an
            //  irrelevant extra control - reported from Cubase while sweeping
            //  HIGH at full EDGE.
            const bool journeyDone = snap.liveEdge01 > 0.985f;

            auto drawJourney = [&] (bool isHigh)
            {
                const float liveHz = isHigh ? snap.currentCentre.highHz : snap.currentCentre.lowHz;
                const float liveX = xForHz (liveHz);
                const auto accent = isHigh ? colour::high : colour::low;

                //  Sample the target path at ~1 px steps and keep the ordered
                //  sub-path between the live corner's x and the point where
                //  the cut reaches the display threshold.
                const float total = targetPath.getLength();
                if (total < 4.0f)
                    return;

                std::vector<juce::Point<float>> route;
                bool inside = false;

                for (float d = 0.0f; d <= total; d += 1.0f)
                {
                    const auto pt = isHigh ? targetPath.getPointAlongPath (d)
                                           : targetPath.getPointAlongPath (total - d);

                    //  Walking outward from the passband: high side walks
                    //  right, low side walks left (reversed traversal).
                    if (! inside)
                    {
                        if ((isHigh && pt.x >= liveX) || (! isHigh && pt.x <= liveX))
                            inside = true;
                        else
                            continue;
                    }

                    route.push_back (pt);

                    if (pt.y >= endY)
                        break;
                }

                if (route.size() < 6)
                    return;

                if (journeyDone)
                {
                    //  Draw only the puck block below; skip route and diamond.
                    route.resize (0);
                }

                //  The live puck's position on the LIVE curve, and its
                //  exclusion circle - no dot inside the core + 3 px.
                auto shapeNoMid = snap.currentCentre;
                shapeNoMid.midGainDb = 0.0f;
                const float puckY = juce::jlimit (plot.getY() + 8.0f, plot.getBottom() - 8.0f,
                                                  yForDb ((float) (magnitudeDb (shapeNoMid, sr, liveHz)
                                                                   + snap.colourTrimDb)));
                const juce::Point<float> puck (liveX, puckY);

                //  Item 5: a CONTINUOUS journey. A 1 px base stroke at 15 %
                //  under dots every 7 px - travelled treatment (3 px, 86 %)
                //  within the hot zone behind the puck, 2.25 px at 36 % on the
                //  remaining road. The route starts 8 px outside the puck core
                //  and its last dot lands on the diamond.
                if (! route.empty())
                {
                    juce::Path base;
                    bool started = false;
                    for (const auto& pt : route)
                    {
                        if (pt.getDistanceFrom (puck) < 14.0f)
                            continue;
                        if (! started) { base.startNewSubPath (pt); started = true; }
                        else             base.lineTo (pt);
                    }
                    g.setColour (accent.withAlpha (0.15f));
                    g.strokePath (base, juce::PathStrokeType (1.0f));
                }

                float acc = 7.0f;
                for (size_t i = 1; i < route.size(); ++i)
                {
                    acc += route[i].getDistanceFrom (route[i - 1]);
                    if (acc < 7.0f)
                        continue;
                    acc = 0.0f;

                    const float fromPuck = route[i].getDistanceFrom (puck);
                    if (fromPuck < 14.0f)
                        continue;

                    const bool hot = fromPuck < 42.0f;
                    float alpha = hot ? 0.86f : 0.36f;
                    float d = hot ? 3.0f : 2.25f;

                    if (fromPuck < 24.0f)
                        alpha = juce::jmin (alpha, 0.16f);      // knee exclusion cap

                    g.setColour (accent.withAlpha (alpha));
                    g.fillEllipse (route[i].x - d * 0.5f, route[i].y - d * 0.5f, d, d);
                }

                //  Item 6: the destination diamond, 10 px, with a 3 px core at
                //  42 % - and the final dot ON its nearest corner so the route
                //  visibly terminates there.
                if (! route.empty())
                {
                    const auto endPt = route.back();
                    const float r = 5.0f;
                    juce::Path diamond;
                    diamond.addQuadrilateral (endPt.x, endPt.y - r, endPt.x + r, endPt.y,
                                              endPt.x, endPt.y + r, endPt.x - r, endPt.y);
                    g.setColour (accent);
                    g.strokePath (diamond, juce::PathStrokeType (1.5f));
                    g.setColour (accent.withAlpha (0.42f));
                    g.fillEllipse (endPt.x - 1.5f, endPt.y - 1.5f, 3.0f, 3.0f);

                    g.setColour (accent.withAlpha (0.86f));
                    g.fillEllipse (endPt.x - 1.5f, endPt.y - r - 1.5f, 3.0f, 3.0f);
                }

                //  Trail: the puck's real recent history.
                {
                    const auto& t = trail[isHigh ? 1 : 0];
                    const auto now = juce::Time::getMillisecondCounter();

                    for (size_t i = 1; i < t.size(); ++i)
                    {
                        const float age = (float) (now - t[i].t) / 180.0f;
                        const float alpha = juce::jlimit (0.0f, 0.26f, (1.0f - age) * 0.26f);

                        if (alpha <= 0.02f)
                            continue;

                        g.setColour (accent.withAlpha (alpha));
                        g.drawLine (t[i - 1].x, t[i - 1].y, t[i].x, t[i].y,
                                    2.0f + 3.0f * (1.0f - age));
                    }
                }

                //  FOLLOW energy: three violet strokes, hard-gated on real
                //  displacement, capped at 28 %.
                //  Item 4: restrained. Inner radius 10, outer at most 17
                //  (13.5 at env 0.5), three thin strokes, combined opacity
                //  never past 22 %, nothing filled.
                if (push01 > 0.01f)
                {
                    const float env = juce::jlimit (0.0f, 1.0f, snap.followEnv01);
                    const float r = 10.0f + 7.0f * env;

                    g.setColour (colour::movement.withAlpha (0.11f));
                    g.drawEllipse (puck.x - r, puck.y - r, r * 2.0f, r * 2.0f, 1.2f);
                    g.setColour (colour::movement.withAlpha (0.07f));
                    g.drawEllipse (puck.x - r - 1.5f, puck.y - r - 1.5f,
                                   r * 2.0f + 3.0f, r * 2.0f + 3.0f, 1.2f);
                    g.setColour (colour::movement.withAlpha (0.04f));
                    g.drawEllipse (puck.x - juce::jmin (17.0f, r + 3.0f),
                                   puck.y - juce::jmin (17.0f, r + 3.0f),
                                   juce::jmin (17.0f, r + 3.0f) * 2.0f,
                                   juce::jmin (17.0f, r + 3.0f) * 2.0f, 1.2f);
                }

                //  The layered live puck.
                const bool isSelected = (isHigh && selected == Grab::high)
                                     || (! isHigh && selected == Grab::low);
                if (isSelected)
                {
                    g.setColour (accent.withAlpha (0.22f));
                    g.fillEllipse (puck.x - 9.0f, puck.y - 9.0f, 18.0f, 18.0f);
                }

                g.setColour (accent);
                g.fillEllipse (puck.x - 6.0f, puck.y - 6.0f, 12.0f, 12.0f);
                g.setColour (colour::graph);
                g.drawEllipse (puck.x - 4.0f, puck.y - 4.0f, 8.0f, 8.0f, 2.0f);
                g.setColour (colour::text);
                g.fillEllipse (puck.x - 2.0f, puck.y - 2.0f, 4.0f, 4.0f);

                updateTrail (isHigh ? 1 : 0, puck);
            };

            if (snap.mode != (int) Mode::lowPass)  drawJourney (false);
            if (snap.mode != (int) Mode::highPass) drawJourney (true);

            g.setColour (colour::text.withAlpha (0.46f));
            g.setFont (juce::FontOptions (9.0f));
            g.drawText ("EDGE PATH",
                        (int) plot.getRight() - 90, (int) plot.getY() + 6, 82, 14,
                        juce::Justification::centredRight, false);
        }

        // --- handles ----------------------------------------------------------        // --- handles ----------------------------------------------------------
        placedLabels.clear();
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
            pos.y = juce::jlimit (plot.getY() + kHaloRadius + 2.0f,
                                  plot.getBottom() - kHaloRadius - 2.0f, pos.y);
            const bool active = (dragging == which) || (dragging == Grab::none && hovered == which);
            const bool isSelected = selected == which;
            const float r = isSelected ? kHandleRadiusSel : kHandleRadius;

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

            //  Selection halo: 15 px at 22 %.
            if (isSelected || active)
            {
                g.setColour (accent.withAlpha (0.22f));
                g.fillEllipse (pos.x - kHaloRadius, pos.y - kHaloRadius,
                               kHaloRadius * 2.0f, kHaloRadius * 2.0f);
            }

            g.setColour (colour::graph);
            g.fillEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f);

            //  The selected handle is filled - one glance answers "what am I
            //  editing?".
            if (isSelected)
            {
                g.setColour (accent.withAlpha (0.65f));
                g.fillEllipse (pos.x - r + 2.0f, pos.y - r + 2.0f,
                               r * 2.0f - 4.0f, r * 2.0f - 4.0f);
            }

            g.setColour (accent);
            g.drawEllipse (pos.x - r, pos.y - r, r * 2.0f, r * 2.0f, 1.8f);

            //  Deterministic label placement: four candidates scored against
            //  everything already on the display. If every candidate collides,
            //  an UNSELECTED label is suppressed - the selected one never is.
            g.setColour (accent);
            g.setFont (juce::FontOptions (font::caption).withStyle ("Bold"));

            const juce::Rectangle<int> candidates[4] = {
                { (int) pos.x - 30, (int) pos.y - 24, 60, 12 },     // above centre
                { (int) pos.x - 30, (int) pos.y + 14, 60, 12 },     // below centre
                { (int) pos.x - 64, (int) pos.y - 20, 60, 12 },     // upper left
                { (int) pos.x + 6,  (int) pos.y - 20, 60, 12 } };   // upper right

            const auto handleRect = juce::Rectangle<int> ((int) pos.x - 8, (int) pos.y - 8,
                                                          16, 16).expanded (8);
            const auto inset = plot.toNearestInt().reduced (8);

            juce::Rectangle<int> chosen;
            float bestScore = 1.0e9f;

            for (const auto& cand : candidates)
            {
                if (! inset.contains (cand))                 continue;
                if (cand.intersects (handleRect))            continue;
                if (cand.intersects (readoutBounds()))       continue;

                bool hitsLabel = false;
                for (const auto& placed : placedLabels)
                    if (cand.intersects (placed.expanded (6)))
                        hitsLabel = true;
                if (hitsLabel)                               continue;

                bool hitsAvoid = false;
                for (const auto& avoid : avoidRects)
                    if (cand.intersects (avoid))
                        hitsAvoid = true;
                if (hitsAvoid)                               continue;

                //  The response curve, expanded 4 px, breaks ties.
                const float score = responseLengthInside (cand.toFloat().expanded (4.0f));
                if (score < bestScore)
                {
                    bestScore = score;
                    chosen = cand;
                }
            }

            if (chosen.isEmpty() && isSelected)
                chosen = candidates[0];        // the selected label always draws

            if (! chosen.isEmpty())
            {
                placedLabels.push_back (chosen);
                g.drawText (name, chosen, juce::Justification::centred, false);
            }

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

        //  Semantic names: in LP the active handle says "LP", never the
        //  internal "HIGH". The identity the user selected never disappears.
        drawHandle (Grab::low,  colour::low,  nameFor (Grab::low));
        drawHandle (Grab::high, colour::high, nameFor (Grab::high));

        //  MID is drawn in the neutral colour: it is neither edge, and a third
        //  accent would stop the other two from meaning anything. Violet is
        //  movement, not a band.
        drawHandle (Grab::mid, colour::textBright, "MID");

        // --- persistent semantic readout, bottom left --------------------------
        //  Always visible - the LP identity and its cutoff have a permanent
        //  home whether or not anything is being touched.
        {
            const auto text = readoutText();
            g.setFont (juce::FontOptions (12.0f));

            int w = juce::jmax (110, juce::GlyphArrangement::getStringWidthInt (
                              juce::Font (juce::FontOptions (12.0f)), text) + 22);

            //  Never under the fixed inspector: truncate at its left edge.
            if (readoutRightLimit > 0)
                w = juce::jmin (w, readoutRightLimit - ((int) plot.getX() + 8));
            const auto box = juce::Rectangle<int> ((int) plot.getX() + 8,
                                                   (int) plot.getBottom() - 26, w, 22);

            g.setColour (colour::raised.withAlpha (0.92f));
            g.fillRoundedRectangle (box.toFloat(), 7.0f);
            g.setColour (colour::text.withAlpha (0.24f));
            g.drawRoundedRectangle (box.toFloat().reduced (0.5f), 7.0f, 1.0f);
            g.setColour (colour::text);
            g.drawText (text, box.reduced (10, 0), juce::Justification::centredLeft, false);
        }

        juce::ignoreUnused (shape);
    }
}
