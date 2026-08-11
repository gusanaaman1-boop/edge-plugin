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

    void resolveFrequencies (float lowHz, float highHz, float focus,
                             float& outLowHz, float& outHighHz) noexcept
    {
        const double logLowMin  = std::log2 ((double) kLowFreqMin);
        const double logLowMax  = std::log2 ((double) kLowFreqMax);
        const double logHighMin = std::log2 ((double) kHighFreqMin);
        const double logHighMax = std::log2 ((double) kHighFreqMax);

        double lLow  = juce::jlimit (logLowMin,  logLowMax,  std::log2 ((double) juce::jmax (1.0f, lowHz)));
        double lHigh = juce::jlimit (logHighMin, logHighMax, std::log2 ((double) juce::jmax (1.0f, highHz)));

        const double want = (double) juce::jlimit (-1.0f, 1.0f, focus) * (double) kFocusOctaves;

        //  How far the pair can still travel in the requested direction before
        //  one of them hits the end of its own range.
        const double headroom = want >= 0.0
            ? juce::jmin (logLowMax - lLow, lHigh - logHighMin)
            : juce::jmin (lLow - logLowMin, logHighMax - lHigh);

        //  Soft limit: h*tanh(x/h) equals x while x << h and approaches h
        //  asymptotically. Focus therefore slows down near the boundary rather
        //  than stopping at it - no jump, and no dead zone at the end of the
        //  control's travel.
        double applied = 0.0;
        if (headroom > 1.0e-6)
            applied = std::copysign (headroom * std::tanh (std::abs (want) / headroom), want);

        lLow  += applied;
        lHigh -= applied;

        //  Keep a small minimum separation, softly.
        lHigh = softMax (lHigh, lLow + std::log2 ((double) kMinEdgeRatio), kSeparationEps);

        outLowHz  = (float) std::exp2 (juce::jlimit (logLowMin,  logLowMax,  lLow));
        outHighHz = (float) std::exp2 (juce::jlimit (logHighMin, logHighMax, lHigh));
    }

    float depthPercentToDb (float percent) noexcept
    {
        //  The perceptual table from the work order, interpolated linearly in
        //  dB. The labelled values land exactly on their control positions,
        //  which matters more here than a smooth second derivative: the map is
        //  monotonic and continuous, so the audio is too.
        static constexpr float pts[][2] =
        {
            {   0.0f,    0.0f },
            {  25.0f,   -3.0f },
            {  40.0f,   -6.0f },
            {  58.0f,  -12.0f },
            {  78.0f,  -24.0f },
            {  92.0f,  -48.0f },
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

    float shoulderPercentToDb (float percent) noexcept
    {
        return kShoulderMaxDb * juce::jlimit (0.0f, 100.0f, percent) * 0.01f;
    }

    float colourDrivePercent (float lowActivity, float highActivity,
                              float lowResonance, float highResonance) noexcept
    {
        //  A pure function of the PARAMETER state. No envelope follower, no
        //  level detector: the colour cannot pump, because nothing in this
        //  expression varies with the audio.
        //
        //  The resonance terms are EdgeUnit::resonanceMakeup(), not the raw
        //  Resonance controls. Raw Resonance was wrong: at Depth 0 a section is
        //  a wire whatever its Q is, so turning Resonance up did nothing audible
        //  while still engaging the colour engine - and the colour engine's
        //  internal DC blocker then high-passed the whole signal by 0.97 dB at
        //  20 Hz. resonanceMakeup is already gated by the section's shelf gain,
        //  so it is 0 at Depth 0.
        const float a = juce::jmax (juce::jmax (lowActivity, highActivity),
                                    juce::jmax (lowResonance, highResonance));

        return kColorDriveMax * juce::jlimit (0.0f, 1.0f, a);
    }

    namespace
    {
        //  The colour stage's linear transfer, as a magnitude.
        //
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

            //  DcBlocker: y = x - x1 + R*y1  ->  H(z) = (1 - z^-1)/(1 - R z^-1)
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

        lo.setShape (sampleRate, shape.lowHz,  shape.lowDepthDb,  shape.lowCurve01,
                     shape.lowRes01,  shape.lowShoulderDb);
        hi.setShape (sampleRate, shape.highHz, shape.highDepthDb, shape.highCurve01,
                     shape.highRes01, shape.highShoulderDb);

        const auto h = lo.responseAt (freqHz, sampleRate) * hi.responseAt (freqHz, sampleRate)
                     * colourMagnitude (shape, sampleRate, freqHz);

        const double trim = -(double) kResonanceTrimDb
                          * juce::jmax (lo.resonanceMakeup(), hi.resonanceMakeup());

        return 20.0 * std::log10 (juce::jmax (1.0e-9, std::abs (h)))
             + (double) shape.outputDb + trim;
    }

    // -------------------------------------------------------------------------

    void EdgeEngine::prepare (double sampleRate, int maxBlockSize, int numChannels)
    {
        rate = sampleRate;
        maxBlock = juce::jmax (1, maxBlockSize);
        channels = juce::jlimit (1, 2, numChannels);

        colour.prepare (sampleRate, maxBlock, channels);

        //  Cutoff is smoothed in LOG frequency, so a sweep is perceptually even
        //  and a jump from 20 Hz to 20 kHz does not spend most of its time in
        //  the top octave.
        const double freqSmooth = 0.012;
        logLowFreq.reset (sampleRate, freqSmooth);
        logHighFreq.reset (sampleRate, freqSmooth);

        const double shapeSmooth = 0.020;
        lowDepthDb.reset (sampleRate, shapeSmooth);
        highDepthDb.reset (sampleRate, shapeSmooth);
        lowCurve.reset (sampleRate, shapeSmooth);
        highCurve.reset (sampleRate, shapeSmooth);
        lowRes.reset (sampleRate, shapeSmooth);
        highRes.reset (sampleRate, shapeSmooth);
        lowShoulder.reset (sampleRate, shapeSmooth);
        highShoulder.reset (sampleRate, shapeSmooth);
        focusSmoothed.reset (sampleRate, shapeSmooth);

        //  The colour follows the filter slowly enough that a fast Depth ramp
        //  fades it in rather than stepping it.
        colourDrive.reset (sampleRate, 0.030);
        outputGain.reset (sampleRate, 0.020);
        bypassFade.reset (sampleRate, 0.010);

        dryBuffer.setSize (channels, maxBlock);

        const int delaySamples = (int) std::lround (colour.getLatencySamples());
        dryDelay.setMaximumDelayInSamples (juce::jmax (1, delaySamples) + 4);
        dryDelay.prepare ({ sampleRate, (juce::uint32) maxBlock, (juce::uint32) channels });
        dryDelay.setDelay ((float) delaySamples);

        reset();
    }

    void EdgeEngine::reset() noexcept
    {
        lowEdge.reset();
        highEdge.reset();
        colour.reset();
        dryDelay.reset();
        dryBuffer.clear();
    }

    void EdgeEngine::setSettings (const Settings& s) noexcept
    {
        float effLow = s.lowFreqHz, effHigh = s.highFreqHz;
        resolveFrequencies (s.lowFreqHz, s.highFreqHz, s.focus, effLow, effHigh);

        //  Focus is folded into the target frequencies here rather than being
        //  smoothed separately, so the two controls cannot fight each other's
        //  smoothers and produce a wobble.
        logLowFreq.setTargetValue (std::log2 (effLow));
        logHighFreq.setTargetValue (std::log2 (effHigh));

        lowDepthDb.setTargetValue (depthPercentToDb (s.lowDepthPercent));
        highDepthDb.setTargetValue (depthPercentToDb (s.highDepthPercent));
        lowCurve.setTargetValue (s.lowCurvePercent * 0.01f);
        highCurve.setTargetValue (s.highCurvePercent * 0.01f);
        lowRes.setTargetValue (s.lowResPercent * 0.01f);
        highRes.setTargetValue (s.highResPercent * 0.01f);
        lowShoulder.setTargetValue (shoulderPercentToDb (s.lowShoulderPercent));
        highShoulder.setTargetValue (shoulderPercentToDb (s.highShoulderPercent));
        focusSmoothed.setTargetValue (s.focus);

        pendingOutputDb = s.outputDb;
        bypassFade.setTargetValue (s.bypass ? 1.0f : 0.0f);
    }

    void EdgeEngine::snapToSettings (const Settings& s) noexcept
    {
        setSettings (s);

        logLowFreq.setCurrentAndTargetValue (logLowFreq.getTargetValue());
        logHighFreq.setCurrentAndTargetValue (logHighFreq.getTargetValue());
        lowDepthDb.setCurrentAndTargetValue (lowDepthDb.getTargetValue());
        highDepthDb.setCurrentAndTargetValue (highDepthDb.getTargetValue());
        lowCurve.setCurrentAndTargetValue (lowCurve.getTargetValue());
        highCurve.setCurrentAndTargetValue (highCurve.getTargetValue());
        lowRes.setCurrentAndTargetValue (lowRes.getTargetValue());
        highRes.setCurrentAndTargetValue (highRes.getTargetValue());
        lowShoulder.setCurrentAndTargetValue (lowShoulder.getTargetValue());
        highShoulder.setCurrentAndTargetValue (highShoulder.getTargetValue());
        focusSmoothed.setCurrentAndTargetValue (focusSmoothed.getTargetValue());
        bypassFade.setCurrentAndTargetValue (bypassFade.getTargetValue());

        applyChunkShape (0);

        colourDrive.setCurrentAndTargetValue (colourDrive.getTargetValue());
        colour.setDrive (colourDrive.getCurrentValue());
        updateOutputTarget();
        outputGain.setCurrentAndTargetValue (outputGain.getTargetValue());
    }

    void EdgeEngine::updateOutputTarget() noexcept
    {
        outputGain.setTargetValue (juce::Decibels::decibelsToGain (pendingOutputDb + resonanceTrimDb)
                                       * colour.getLevelTrimGain());

        dispColourEngage.store (colour.getEngageFactor(), std::memory_order_relaxed);
        dispColourGain.store (colour.getMeasuredGain(), std::memory_order_relaxed);
    }

    //  Coefficient work for one automation chunk. `chunkLength` samples are
    //  consumed from every shape smoother; pass 0 to evaluate without advancing.
    void EdgeEngine::applyChunkShape (int chunkLength) noexcept
    {
        auto take = [chunkLength] (juce::SmoothedValue<float>& v)
        {
            return chunkLength > 0 ? v.skip (chunkLength) : v.getCurrentValue();
        };

        const float lowHz   = std::exp2 (take (logLowFreq));
        const float highHz  = std::exp2 (take (logHighFreq));
        const float lowDb   = take (lowDepthDb);
        const float highDb  = take (highDepthDb);
        const float lowC    = take (lowCurve);
        const float highC   = take (highCurve);
        const float lowR    = take (lowRes);
        const float highR   = take (highRes);
        const float lowSh   = take (lowShoulder);
        const float highSh  = take (highShoulder);
        take (focusSmoothed);

        lowEdge.setShape (rate, lowHz, lowDb, lowC, lowR, lowSh);
        highEdge.setShape (rate, highHz, highDb, highC, highR, highSh);

        //  Hidden colour: driven by what the filter is doing, nothing else.
        const float drive = colourDrivePercent (lowEdge.activity(), highEdge.activity(),
                                                lowEdge.resonanceMakeup(),
                                                highEdge.resonanceMakeup());
        colourDrive.setTargetValue (drive);
        colour.setSpectrum (lowHz, highHz);

        //  Static make-up for Resonance only. Self-gating: at Depth 0 section 0
        //  is a wire, resonanceMakeup() is 0, and this is exactly 0 dB - so the
        //  neutral state is still bit-exact.
        resonanceTrimDb = -kResonanceTrimDb
                        * juce::jmax (lowEdge.resonanceMakeup(), highEdge.resonanceMakeup());

        lastResTrimDb.store (resonanceTrimDb, std::memory_order_relaxed);

        dispLowHz.store (lowHz, std::memory_order_relaxed);
        dispLowDepthDb.store (lowDb, std::memory_order_relaxed);
        dispLowCurve.store (lowC, std::memory_order_relaxed);
        dispLowRes.store (lowR, std::memory_order_relaxed);
        dispHighHz.store (highHz, std::memory_order_relaxed);
        dispHighDepthDb.store (highDb, std::memory_order_relaxed);
        dispHighCurve.store (highC, std::memory_order_relaxed);
        dispHighRes.store (highR, std::memory_order_relaxed);
        dispLowShoulderDb.store (lowSh, std::memory_order_relaxed);
        dispHighShoulderDb.store (highSh, std::memory_order_relaxed);
        //  The USER's Output only. magnitudeDb() derives the resonance trim
        //  from the same resonance values, so storing the summed number here
        //  would count it twice in the drawn curve.
        dispOutputDb.store (pendingOutputDb, std::memory_order_relaxed);
    }

    void EdgeEngine::process (juce::AudioBuffer<float>& buffer) noexcept
    {
        const int n = buffer.getNumSamples();
        const int chans = juce::jmin (channels, buffer.getNumChannels());

        if (n == 0 || chans == 0)
            return;

        //  Hosts are supposed to honour the block size they announced, and not
        //  all of them do. dryBuffer was sized for it, so a bigger block would
        //  be a buffer overrun. Slice instead of resizing: resizing here would
        //  allocate on the audio thread.
        if (n > maxBlock)
        {
            for (int pos = 0; pos < n; pos += maxBlock)
            {
                const int slice = juce::jmin (maxBlock, n - pos);
                float* slicePtrs[2] = { buffer.getWritePointer (0) + pos,
                                        chans > 1 ? buffer.getWritePointer (1) + pos : nullptr };
                juce::AudioBuffer<float> view (slicePtrs, chans, slice);
                process (view);
            }

            return;
        }

        //  Latency-matched dry, for the bypass fade. When the colour stage runs
        //  at 1x this delay is 0 and the dry path is the input itself.
        for (int c = 0; c < chans; ++c)
        {
            const auto* src = buffer.getReadPointer (c);
            auto* dst = dryBuffer.getWritePointer (c);

            for (int i = 0; i < n; ++i)
            {
                dryDelay.pushSample (c, src[i]);
                dst[i] = dryDelay.popSample (c);
            }
        }

        float* channelPtrs[2] = { buffer.getWritePointer (0),
                                  chans > 1 ? buffer.getWritePointer (1) : nullptr };

        for (int pos = 0; pos < n; )
        {
            const int chunk = juce::jmin (kAutomationChunk, n - pos);

            applyChunkShape (chunk);
            colour.setDrive (colourDrive.skip (chunk));
            updateOutputTarget();

            float* chunkPtrs[2] = { channelPtrs[0] + pos,
                                    channelPtrs[1] != nullptr ? channelPtrs[1] + pos : nullptr };

            //  Non-owning view; no allocation.
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

        //  Output trim and the bypass fade, per sample. Samples outer so both
        //  channels see identical gains and every smoother steps exactly once.
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
                //  wet + 1.0f*(dry-wet) leaves a rounding residue of about
                //  6e-8, which is enough to stop a bypassed instance from
                //  nulling against the source.
                d[i] = b >= 1.0f ? dry
                     : b <= 0.0f ? wet
                                 : wet + b * (dry - wet);
            }
        }
    }

    EdgeShape EdgeEngine::getDisplayShape() const noexcept
    {
        EdgeShape s;
        s.lowHz       = dispLowHz.load (std::memory_order_relaxed);
        s.lowDepthDb  = dispLowDepthDb.load (std::memory_order_relaxed);
        s.lowCurve01  = dispLowCurve.load (std::memory_order_relaxed);
        s.lowRes01    = dispLowRes.load (std::memory_order_relaxed);
        s.highHz      = dispHighHz.load (std::memory_order_relaxed);
        s.highDepthDb = dispHighDepthDb.load (std::memory_order_relaxed);
        s.highCurve01 = dispHighCurve.load (std::memory_order_relaxed);
        s.highRes01   = dispHighRes.load (std::memory_order_relaxed);
        s.outputDb    = dispOutputDb.load (std::memory_order_relaxed);
        s.lowShoulderDb  = dispLowShoulderDb.load (std::memory_order_relaxed);
        s.highShoulderDb = dispHighShoulderDb.load (std::memory_order_relaxed);
        s.colourEngage = dispColourEngage.load (std::memory_order_relaxed);
        s.colourGain   = dispColourGain.load (std::memory_order_relaxed);
        return s;
    }
}
