#include "EdgeEngine.h"

namespace edge
{
    namespace
    {
        //  Smooth maximum. Reaches the exact value of the larger argument once
        //  they are more than ~30*eps apart, so it only rounds the corner where
        //  a hard max() would put a kink.
        double softMax (double a, double b, double eps) noexcept
        {
            const double d = (a - b) / eps;
            if (d > 30.0)  return a;
            if (d < -30.0) return b;
            return b + eps * std::log1p (std::exp (d));
        }

        constexpr double kSeparationEps = 0.05;   // octaves
    }

    void resolveSeparation (float& lowHz, float& highHz) noexcept
    {
        const double logLowMin  = std::log2 ((double) kLowFreqMin);
        const double logLowMax  = std::log2 ((double) kLowFreqMax);
        const double logHighMin = std::log2 ((double) kHighFreqMin);
        const double logHighMax = std::log2 ((double) kHighFreqMax);

        double lLow  = juce::jlimit (logLowMin,  logLowMax,  std::log2 ((double) juce::jmax (1.0f, lowHz)));
        double lHigh = juce::jlimit (logHighMin, logHighMax, std::log2 ((double) juce::jmax (1.0f, highHz)));

        lHigh = softMax (lHigh, lLow + std::log2 ((double) kMinEdgeRatio), kSeparationEps);

        lowHz  = (float) std::exp2 (juce::jlimit (logLowMin,  logLowMax,  lLow));
        highHz = (float) std::exp2 (juce::jlimit (logHighMin, logHighMax, lHigh));
    }

    float depthPercentToDb (float percent) noexcept
    {
        //  The perceptual anchors from the work order, at control positions
        //  chosen so that the STEEPEST segment is 2.5 dB per 1 % of travel.
        //
        //  That number is the EDGE macro's continuity budget. EDGE walks this
        //  same taper, and the acceptance test asks for under 0.15 dB of change
        //  per 0.05 % of EDGE - so any segment steeper than 3 dB/% fails it. The
        //  first version put -24 dB at 78 % and -48 dB at 92 %, which is 9 dB/%
        //  at the top and measured 0.39 dB per step.
        static constexpr float pts[][2] =
        {
            {   0.0f,    0.0f },
            {  20.0f,   -3.0f },
            {  33.0f,   -6.0f },
            {  48.0f,  -12.0f },
            {  65.0f,  -24.0f },
            { 100.0f, kDepthFloorDb }
        };

        const float p = juce::jlimit (0.0f, 100.0f, percent);
        constexpr int n = (int) (sizeof (pts) / sizeof (pts[0]));

        for (int i = 1; i < n; ++i)
        {
            if (p <= pts[i][0])
            {
                const float t = (p - pts[i - 1][0]) / (pts[i][0] - pts[i - 1][0]);
                return pts[i - 1][1] + t * (pts[i][1] - pts[i - 1][1]);
            }
        }

        return kDepthFloorDb;
    }

    float shoulderPercentToDb (float percent) noexcept
    {
        return kShoulderMaxDb * juce::jlimit (0.0f, 100.0f, percent) * 0.01f;
    }

    const SlopeChoice kSlopeChoices[kNumSlopeChoices] =
    {
        { "SOFT",       0.0f  },
        { "12 dB/oct", 50.0f  },
        { "24 dB/oct", 75.0f  },
        { "36 dB/oct", 100.0f },
    };

    int slopeIndexFor (float curvePercent) noexcept
    {
        for (int i = 0; i < kNumSlopeChoices; ++i)
            if (std::abs (curvePercent - kSlopeChoices[i].curvePercent) < 0.4f)
                return i;

        return -1;
    }

    juce::String slopeTextFor (float curvePercent)
    {
        const int i = slopeIndexFor (curvePercent);
        if (i >= 0)
            return kSlopeChoices[i].name;

        //  Between two entries Curve is a voicing, not a slope. Printing
        //  12 x poles here would claim "18 dB/oct" for a setting that measures
        //  23.5, so the percentage is shown instead.
        return juce::String (curvePercent, 0) + " %";
    }

    // --- BITE ----------------------------------------------------------------

    float biteMaxDrive (float bitePercent) noexcept
    {
        const float b = juce::jlimit (0.0f, 1.0f, bitePercent * 0.01f);

        //  b == 0 must give EXACTLY zero, which pow() does, and which is what
        //  makes "BITE 0 fully disengages" a bit-exact statement rather than a
        //  very quiet one.
        return kBiteMaxDrive * std::pow (b, kBiteDriveCurve);
    }

    float biteGamma (float bitePercent) noexcept
    {
        const float b = juce::jlimit (0.0f, 1.0f, bitePercent * 0.01f);
        return kBiteGammaLow + (kBiteGammaHigh - kBiteGammaLow) * b;
    }

    float colourDrivePercent (float bitePercent, float activity) noexcept
    {
        //  A pure function of the PARAMETER state and the FILTER's activity.
        //  No envelope follower, no level detector, no adaptation: the colour
        //  cannot pump, because nothing in this expression varies with the
        //  audio. (FOLLOW moves the filter; the colour then follows the filter,
        //  which is a different thing from the colour following the signal.)
        const float a = juce::jlimit (0.0f, 1.0f, activity);

        if (a <= 0.0f)
            return 0.0f;

        return biteMaxDrive (bitePercent) * std::pow (a, biteGamma (bitePercent));
    }

    // --- response ------------------------------------------------------------

    namespace
    {
        //  ColorEngine::blend() is  y = x + e*(shaped*comp - x), so at small
        //  signal  y = (1-e)*x + e*G*HP(x)  where HP is the engine's internal
        //  DC blocker and G its small-signal gain. EDGE then multiplies by
        //  1/M, M being the measured gain of that whole expression at 1 kHz.
        //  Hence  H(f) = [ (1-e) + (M - (1-e)) * HP(f)/HP(1k) ] / M.
        double colourMagnitude (const EdgeShape& shape, double sampleRate, double freqHz) noexcept
        {
            const double e = juce::jlimit (0.0, 1.0, (double) shape.colourEngage);
            const double m = (double) shape.colourGain;

            if (e <= 0.0 || m <= 0.0)
                return 1.0;

            const double R = juce::jmax (0.9, 1.0 - juce::MathConstants<double>::twoPi
                                                      * (double) ColorStage::kDcBlockerHz / sampleRate);

            auto hp = [R, sampleRate] (double f)
            {
                const std::complex<double> z = std::polar (1.0,
                    -juce::MathConstants<double>::twoPi * f / sampleRate);
                return std::abs ((1.0 - z) / (1.0 - R * z));
            };

            const double ref = juce::jmax (1.0e-9, hp (1000.0));
            return ((1.0 - e) + (m - (1.0 - e)) * hp (freqHz) / ref) / m;
        }
    }

    double magnitudeDb (const EdgeShape& shape, double sampleRate, double freqHz) noexcept
    {
        EdgeUnit<Side::low>  lo;
        EdgeUnit<Side::high> hi;

        lo.setShape (0, sampleRate, shape.lowHz,  shape.lowDepthDb,  shape.lowCurve01,
                     shape.lowRes01,  shape.lowShoulderDb);
        hi.setShape (0, sampleRate, shape.highHz, shape.highDepthDb, shape.highCurve01,
                     shape.highRes01, shape.highShoulderDb);

        const auto h = lo.responseAt (freqHz, sampleRate) * hi.responseAt (freqHz, sampleRate)
                     * colourMagnitude (shape, sampleRate, freqHz);

        const double trim = -(double) kResonanceTrimDb
                          * juce::jmax (lo.resonanceMakeup(), hi.resonanceMakeup());

        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)))
             + (double) shape.outputDb + trim;
    }

    // -------------------------------------------------------------------------

    void EdgeEngine::DisplayShape::store (const Resolved& r) noexcept
    {
        constexpr auto o = std::memory_order_relaxed;
        lowHz.store (r.lowHz, o);            lowDepthDb.store (r.lowDepthDb, o);
        lowCurve.store (r.lowCurve01, o);    lowRes.store (r.lowRes01, o);
        lowShoulderDb.store (r.lowShoulderDb, o);
        highHz.store (r.highHz, o);          highDepthDb.store (r.highDepthDb, o);
        highCurve.store (r.highCurve01, o);  highRes.store (r.highRes01, o);
        highShoulderDb.store (r.highShoulderDb, o);
    }

    void EdgeEngine::DisplayShape::load (EdgeShape& s) const noexcept
    {
        constexpr auto o = std::memory_order_relaxed;
        s.lowHz = lowHz.load (o);              s.lowDepthDb = lowDepthDb.load (o);
        s.lowCurve01 = lowCurve.load (o);      s.lowRes01 = lowRes.load (o);
        s.lowShoulderDb = lowShoulderDb.load (o);
        s.highHz = highHz.load (o);            s.highDepthDb = highDepthDb.load (o);
        s.highCurve01 = highCurve.load (o);    s.highRes01 = highRes.load (o);
        s.highShoulderDb = highShoulderDb.load (o);
    }

    void EdgeEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        rate = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        channels = juce::jlimit (1, maxChannels, numChannels);

        colour.prepare (sampleRate, maxBlock, channels);
        follower.prepare (sampleRate);

        const double freqSmooth = 0.012;
        logLowFreq.reset (sampleRate, freqSmooth);
        logHighFreq.reset (sampleRate, freqSmooth);

        const double shapeSmooth = 0.020;
        for (auto* v : { &lowDepth, &highDepth, &lowCurve, &highCurve,
                         &lowShoulder, &highShoulder, &lowRes, &highRes,
                         &edgeBase, &followAmount, &spreadOctaves, &bite })
            v->reset (sampleRate, shapeSmooth);

        //  MODE is a step. 30 ms of ramp on an edge's enable is what turns a
        //  discrete switch into a click-free transition, and it is the only
        //  thing MODE does.
        lowEnable.reset (sampleRate, 0.050);
        highEnable.reset (sampleRate, 0.050);

        colourDrive.reset (sampleRate, 0.030);
        outputGain.reset (sampleRate, 0.020);
        bypassFade.reset (sampleRate, 0.010);

        dryBuffer.setSize (channels, maxBlock);
        analyzerBuffer.assign ((size_t) kAnalyzerFifoSize, 0.0f);
        analyzerFifo.reset();

        reset();
    }

    void EdgeEngine::reset() noexcept
    {
        lowEdge.reset();
        highEdge.reset();
        colour.reset();
        follower.reset();
        dryBuffer.clear();
        analyzerFifo.reset();
    }

    void EdgeEngine::setSettings (const Settings& s) noexcept
    {
        float lowHz = s.lowFreqHz, highHz = s.highFreqHz;
        resolveSeparation (lowHz, highHz);

        logLowFreq.setTargetValue (std::log2 (lowHz));
        logHighFreq.setTargetValue (std::log2 (highHz));

        lowDepth.setTargetValue (s.lowDepthPercent);
        highDepth.setTargetValue (s.highDepthPercent);
        lowCurve.setTargetValue (s.lowCurvePercent * 0.01f);
        highCurve.setTargetValue (s.highCurvePercent * 0.01f);
        lowShoulder.setTargetValue (s.lowShoulderPercent);
        highShoulder.setTargetValue (s.highShoulderPercent);
        lowRes.setTargetValue (s.lowResPercent * 0.01f);
        highRes.setTargetValue (s.highResPercent * 0.01f);

        //  MODE: an inactive edge is driven to a bit-exact wire, not switched
        //  out. LP keeps the HIGH edge (the low-pass target); HP keeps the LOW.
        lowEnable.setTargetValue (s.mode == (int) Mode::lowPass ? 0.0f : 1.0f);
        highEnable.setTargetValue (s.mode == (int) Mode::highPass ? 0.0f : 1.0f);

        edgeBase.setTargetValue (juce::jlimit (0.0f, 1.0f, s.edgePercent * 0.01f));
        followAmount.setTargetValue (juce::jlimit (-1.0f, 1.0f, s.followPercent * 0.01f));
        spreadOctaves.setTargetValue (juce::jlimit (-1.0f, 1.0f, s.spreadPercent * 0.01f)
                                          * (kSpreadMaxSemitones * 0.5f / 12.0f));
        bite.setTargetValue (juce::jlimit (0.0f, 100.0f, s.bitePercent));

        follower.setSensitivity (s.followSensDb);
        follower.setTimes (s.followAttackMs, s.followReleaseMs);

        pendingOutputDb = s.outputDb;
        bypassFade.setTargetValue (s.bypass ? 1.0f : 0.0f);
    }

    void EdgeEngine::snapToSettings (const Settings& s) noexcept
    {
        setSettings (s);

        for (auto* v : { &logLowFreq, &logHighFreq, &lowDepth, &highDepth,
                         &lowCurve, &highCurve, &lowShoulder, &highShoulder,
                         &lowRes, &highRes, &lowEnable, &highEnable,
                         &edgeBase, &followAmount, &spreadOctaves, &bite, &bypassFade })
            v->setCurrentAndTargetValue (v->getTargetValue());

        applyChunkShape (0, edgeBase.getCurrentValue());

        colourDrive.setCurrentAndTargetValue (colourDrive.getTargetValue());
        colour.setDrive (colourDrive.getCurrentValue());
        updateOutputTarget();
        outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    }

    //  The EDGE macro. Everything an edge does is a journey from "open" to its
    //  target, and this is the only place that journey is defined.
    //
    //  Frequency travels GEOMETRICALLY (linear in log2) from the range boundary
    //  to the target, so the movement is perceptually even rather than spending
    //  most of its travel in the top octave.
    //
    //  Depth travels in the CONTROL domain, not in dB. Interpolating the dB
    //  linearly would put a -120 dB target at -12 dB by EDGE 10 %, which is
    //  already a deep cut; interpolating the percentage keeps EDGE on the same
    //  perceptual taper the Depth knob uses, and it is still continuous.
    EdgeEngine::Resolved EdgeEngine::resolveFor (float edge01, float octaveOffset) const noexcept
    {
        const float e = juce::jlimit (0.0f, 1.0f, edge01);

        const float lowOn  = lowEnable.getCurrentValue();
        const float highOn = highEnable.getCurrentValue();

        const float lowE  = e * lowOn;
        const float highE = e * highOn;

        const float logLowOrigin  = std::log2 (kLowFreqMin);
        const float logHighOrigin = std::log2 (kHighFreqMax);

        //  The FREQUENCY travel follows EDGE alone, not EDGE x enable.
        //
        //  Letting MODE drag the corner back to its origin as well meant that
        //  switching an edge off swept its cutoff across the spectrum in the
        //  30 ms the enable took - a coefficient sweep that produced -43 dBFS
        //  of discontinuity for no benefit, since an edge whose gains are 1 is
        //  a bit-exact wire and its corner is unobservable. MODE now only
        //  drives the gains.
        //
        //  SPREAD shifts BOTH corners of a channel by the same number of
        //  octaves, which is exactly what preserves the band's width.
        const float logLow  = (logLowOrigin  + e * (logLowFreq.getCurrentValue()  - logLowOrigin))
                            + octaveOffset;
        const float logHigh = (logHighOrigin + e * (logHighFreq.getCurrentValue() - logHighOrigin))
                            + octaveOffset;

        Resolved r;
        r.lowHz  = juce::jlimit (kLowFreqMin,  kLowFreqMax,  std::exp2 (logLow));
        r.highHz = juce::jlimit (kHighFreqMin, kHighFreqMax, std::exp2 (logHigh));
        resolveSeparation (r.lowHz, r.highHz);

        r.lowDepthDb     = depthPercentToDb (lowE  * lowDepth.getCurrentValue());
        r.highDepthDb    = depthPercentToDb (highE * highDepth.getCurrentValue());
        r.lowShoulderDb  = shoulderPercentToDb (lowE  * lowShoulder.getCurrentValue());
        r.highShoulderDb = shoulderPercentToDb (highE * highShoulder.getCurrentValue());
        r.lowRes01       = lowE  * lowRes.getCurrentValue();
        r.highRes01      = highE * highRes.getCurrentValue();

        //  Curve is a SHAPE, not an amount, so EDGE does not interpolate it.
        //  There is no meaningful "curve of an inactive filter" to travel from,
        //  and at EDGE 0 the depth is 0 so the curve is unobservable anyway.
        r.lowCurve01  = lowCurve.getCurrentValue();
        r.highCurve01 = highCurve.getCurrentValue();

        return r;
    }

    void EdgeEngine::applyChunkShape (int chunkLength, float liveEdge01) noexcept
    {
        if (chunkLength > 0)
        {
            for (auto* v : { &logLowFreq, &logHighFreq, &lowDepth, &highDepth,
                             &lowCurve, &highCurve, &lowShoulder, &highShoulder,
                             &lowRes, &highRes, &lowEnable, &highEnable,
                             &edgeBase, &followAmount, &spreadOctaves, &bite })
                v->skip (chunkLength);
        }

        const float spread = spreadOctaves.getCurrentValue();

        for (int c = 0; c < channels; ++c)
        {
            //  Left goes down, right goes up. With one channel there is no
            //  spread to apply.
            const float offset = channels < 2 ? 0.0f
                                              : (c == 0 ? -spread : spread);

            const auto r = resolveFor (liveEdge01, offset);

            lowEdge.setShape  (c, rate, r.lowHz,  r.lowDepthDb,  r.lowCurve01,
                               r.lowRes01,  r.lowShoulderDb,  chunkLength);
            highEdge.setShape (c, rate, r.highHz, r.highDepthDb, r.highCurve01,
                               r.highRes01, r.highShoulderDb, chunkLength);

            dispChannel[c].store (r);
        }

        //  The ghost curve: what EDGE at 100 % would do, without spread.
        dispTarget.store (resolveFor (1.0f, 0.0f));

        //  Hidden colour, from the FILTER's activity and BITE. Channel 0's
        //  activity: SPREAD moves frequencies, not gains, so both channels have
        //  the same activity by construction.
        const float activity = juce::jmax (lowEdge.activity (0), highEdge.activity (0));
        colourDrive.setTargetValue (colourDrivePercent (bite.getCurrentValue(), activity));

        const auto shape0 = getDisplayShape (0);
        colour.setSpectrum (shape0.lowHz, shape0.highHz);

        //  Static make-up for Resonance only. Self-gating: at Depth 0 section 0
        //  is a wire, resonanceMakeup() is 0, and this is exactly 0 dB.
        resonanceTrimDb = -kResonanceTrimDb
                        * juce::jmax (lowEdge.resonanceMakeup (0), highEdge.resonanceMakeup (0));

        lastResTrimDb.store (resonanceTrimDb, std::memory_order_relaxed);
        dispLiveEdge.store (liveEdge01, std::memory_order_relaxed);
        dispSpreadActive.store (std::abs (spread) > 1.0e-4f, std::memory_order_relaxed);
        dispOutputDb.store (pendingOutputDb, std::memory_order_relaxed);
    }

    void EdgeEngine::updateOutputTarget() noexcept
    {
        outputGain.setTargetValue (juce::Decibels::decibelsToGain (pendingOutputDb + resonanceTrimDb)
                                       * colour.getLevelTrimGain());

        dispColourEngage.store (colour.getEngageFactor(), std::memory_order_relaxed);
        dispColourGain.store (colour.getMeasuredGain(), std::memory_order_relaxed);
    }

    void EdgeEngine::pushAnalyzer (const juce::AudioBuffer<float>& buffer, int n) noexcept
    {
        if (! analyzerEnabled.load (std::memory_order_relaxed))
            return;

        //  Never blocks and never grows: if the editor is not draining fast
        //  enough the oldest samples are simply dropped.
        const auto scope = analyzerFifo.write (n);
        const int chans = juce::jmin (channels, buffer.getNumChannels());

        auto fill = [&] (int startIndex, int size, int offset)
        {
            for (int i = 0; i < size; ++i)
            {
                float mono = 0.0f;
                for (int c = 0; c < chans; ++c)
                    mono += buffer.getReadPointer (c)[offset + i];

                analyzerBuffer[(size_t) (startIndex + i)] = mono / (float) juce::jmax (1, chans);
            }
        };

        fill (scope.startIndex1, scope.blockSize1, 0);
        fill (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }

    int EdgeEngine::readAnalyzerSamples (float* dest, int maxSamples) noexcept
    {
        const int ready = juce::jmin (maxSamples, analyzerFifo.getNumReady());
        if (ready <= 0)
            return 0;

        const auto scope = analyzerFifo.read (ready);

        for (int i = 0; i < scope.blockSize1; ++i)
            dest[i] = analyzerBuffer[(size_t) (scope.startIndex1 + i)];

        for (int i = 0; i < scope.blockSize2; ++i)
            dest[scope.blockSize1 + i] = analyzerBuffer[(size_t) (scope.startIndex2 + i)];

        return ready;
    }

    void EdgeEngine::process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (channels, buffer.getNumChannels());

        if (n == 0 || chans == 0)
            return;

        //  Hosts are supposed to honour the block size they announced, and not
        //  all of them do. dryBuffer was sized for it, so a bigger block would
        //  be a buffer overrun. Slice instead of resizing.
        if (n > maxBlock)
        {
            for (int pos = 0; pos < n; pos += maxBlock)
            {
                const int slice = juce::jmin (maxBlock, n - pos);
                float* slicePtrs[maxChannels] = { buffer.getWritePointer (0) + pos,
                                                  chans > 1 ? buffer.getWritePointer (1) + pos : nullptr };
                juce::AudioBuffer<float> view (slicePtrs, chans, slice);
                process (view);
            }

            return;
        }

        //  The dry copy is the bypass path AND the follower's detector input.
        //  Latency is zero, so it is the input itself.
        for (int c = 0; c < chans; ++c)
            dryBuffer.copyFrom (c, 0, buffer, c, 0, n);

        float* channelPtrs[maxChannels] = { buffer.getWritePointer (0),
                                            chans > 1 ? buffer.getWritePointer (1) : nullptr };

        for (int pos = 0; pos < n; )
        {
            const int chunk = juce::jmin (kAutomationChunk, n - pos);

            //  FOLLOW. The detector consumes every sample of the chunk's INPUT,
            //  then its value drives that chunk's coefficients.
            const float* detectPtrs[maxChannels] =
            {
                dryBuffer.getReadPointer (0) + pos,
                chans > 1 ? dryBuffer.getReadPointer (1) + pos : nullptr
            };

            const float env = follower.processBlock (detectPtrs, chans, chunk);
            const float liveEdge = applyFollow (edgeBase.getCurrentValue(),
                                                followAmount.getCurrentValue(), env);

            dispFollowEnv.store (env, std::memory_order_relaxed);

            applyChunkShape (chunk, liveEdge);
            colour.setDrive (colourDrive.skip (chunk));
            updateOutputTarget();

            float* chunkPtrs[maxChannels] = { channelPtrs[0] + pos,
                                              channelPtrs[1] != nullptr ? channelPtrs[1] + pos : nullptr };

            juce::AudioBuffer<float> view (chunkPtrs, chans, chunk);

            if (placement == ColourPlacement::pre)
                colour.process (view, chunk);

            lowEdge.processChunk (chunkPtrs, chans, chunk);
            highEdge.processChunk (chunkPtrs, chans, chunk);

            if (placement == ColourPlacement::post)
                colour.process (view, chunk);

            pos += chunk;
        }

        lastColourDrive.store (colourDrive.getCurrentValue(), std::memory_order_relaxed);

        //  Output trim and the bypass fade, per sample.
        for (int i = 0; i < n; ++i)
        {
            const float g = outputGain.getNextValue();
            const float b = bypassFade.getNextValue();

            for (int c = 0; c < chans; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                const float dry = dryBuffer.getReadPointer (c)[i];
                const float wet = d[i] * g;

                //  The two endpoints are assigned, not interpolated to. Writing
                //  wet + 1.0f*(dry-wet) leaves a rounding residue of about 6e-8,
                //  which is enough to stop a bypassed instance nulling.
                d[i] = b >= 1.0f ? dry
                     : b <= 0.0f ? wet
                                 : wet + b * (dry - wet);
            }
        }

        pushAnalyzer (buffer, n);
    }

    EdgeShape EdgeEngine::getDisplayShape (int channel) const noexcept
    {
        EdgeShape s;
        dispChannel[juce::jlimit (0, maxChannels - 1, channel)].load (s);
        s.outputDb     = dispOutputDb.load (std::memory_order_relaxed);
        s.colourEngage = dispColourEngage.load (std::memory_order_relaxed);
        s.colourGain   = dispColourGain.load (std::memory_order_relaxed);
        return s;
    }

    EdgeShape EdgeEngine::getDisplayShape() const noexcept { return getDisplayShape (0); }

    EdgeShape EdgeEngine::getTargetShape() const noexcept
    {
        EdgeShape s;
        dispTarget.load (s);
        s.outputDb = dispOutputDb.load (std::memory_order_relaxed);
        //  The ghost curve is the FILTER's target, so it deliberately carries
        //  no colour contribution.
        return s;
    }
}
