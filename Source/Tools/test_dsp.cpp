// EDGE measurement suite.
//
//     build/EdgeTests_artefacts/<config>/EdgeTests
//
// Exit code 0 = every check passed. Every check prints its measured value, so a
// failure is diagnosable without attaching a debugger. Nothing here asserts a
// number that was not actually measured by the code above it.

#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "../Core/ParameterIds.h"
#include "../Core/Parameters.h"
#include "../Dsp/EdgeEngine.h"

// -----------------------------------------------------------------------------
//  Allocation counter. Global operator new/delete are replaced so the audio
//  thread can be proven allocation-free rather than assumed to be.
// -----------------------------------------------------------------------------
namespace
{
    std::atomic<int> gAllocations { 0 };
    std::atomic<bool> gCountAllocations { false };
}

void* operator new (std::size_t n)
{
    if (gCountAllocations.load (std::memory_order_relaxed))
        gAllocations.fetch_add (1, std::memory_order_relaxed);

    if (auto* p = std::malloc (n == 0 ? 1 : n))
        return p;

    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }

// -----------------------------------------------------------------------------
namespace
{
    int gChecks = 0, gFailures = 0;

    void check (bool ok, const char* label, const juce::String& measured)
    {
        ++gChecks;
        if (! ok)
            ++gFailures;

        std::printf ("  [%s] %-56s %s\n", ok ? "PASS" : "FAIL", label,
                     measured.toRawUTF8());
    }

    void section (const char* name)
    {
        std::printf ("\n== %s ==\n", name);
    }

    juce::String f (double v, int dp = 3) { return juce::String (v, dp); }

    using Settings = edge::EdgeEngine::Settings;

    Settings neutral()
    {
        return Settings {};
    }

    //  Run a buffer through a freshly prepared, snapped engine.
    void run (edge::EdgeEngine& e, juce::AudioBuffer<float>& buf, int blockSize)
    {
        for (int pos = 0; pos < buf.getNumSamples(); )
        {
            const int n = juce::jmin (blockSize, buf.getNumSamples() - pos);
            float* ptrs[2] = { buf.getWritePointer (0),
                               buf.getNumChannels() > 1 ? buf.getWritePointer (1) : nullptr };
            float* chunk[2] = { ptrs[0] + pos, ptrs[1] != nullptr ? ptrs[1] + pos : nullptr };
            juce::AudioBuffer<float> view (chunk, buf.getNumChannels(), n);
            e.process (view);
            pos += n;
        }
    }

    //  Measured magnitude response: drive a sine at freqHz and recover ONLY the
    //  amplitude at that frequency, with a Hann-windowed single-bin DFT.
    //
    //  Broadband RMS was the first attempt and it was wrong: the hidden colour
    //  engine is a nonlinearity, so at a deep cut the RMS in the band is
    //  dominated by harmonics that the filter attenuates far less than the
    //  fundamental. A "-66 dB cut" measured that way was really -104 dB of
    //  fundamental sitting under its own distortion products.
    //
    //  The default amplitude is -18 dBFS, the level the colour engine's static
    //  make-up is calibrated at, so a linear measurement is not reading the
    //  saturator's compression curve either.
    double measuredGainDb (edge::EdgeEngine& e, const Settings& s,
                           double sampleRate, double freqHz, float amplitude = 0.125f)
    {
        e.reset();
        e.snapToSettings (s);

        const int settle = (int) (sampleRate * 0.30);
        const int measure = juce::jmax (8192, (int) (sampleRate / juce::jmax (5.0, freqHz)) * 32);

        juce::AudioBuffer<float> buf (1, settle + measure);
        const double w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;

        for (int i = 0; i < buf.getNumSamples(); ++i)
            buf.setSample (0, i, amplitude * (float) std::sin (w * (double) i));

        run (e, buf, 128);

        double re = 0.0, im = 0.0, winSum = 0.0;
        for (int i = 0; i < measure; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi
                                                         * (double) i / (measure - 1));
            const double phase = w * (double) (settle + i);
            const double v = buf.getSample (0, settle + i) * win;

            re += v * std::sin (phase);
            im += v * std::cos (phase);
            winSum += win;
        }

        const double outAmp = 2.0 * std::sqrt (re * re + im * im) / juce::jmax (1.0, winSum);

        return 20.0 * std::log10 (juce::jmax (1.0e-14, outAmp / (double) amplitude));
    }

    //  The shape the EDITOR would draw: read back from a real engine after it
    //  has been snapped to the settings, so the curve under test is exactly the
    //  one the plug-in shows, colour contribution and all.
    edge::EdgeShape shapeFor (edge::EdgeEngine& e, const Settings& s)
    {
        e.reset();
        e.snapToSettings (s);
        return e.getDisplayShape();
    }

    //  An engine parked at kSr, for the many checks that only need the drawn
    //  response and never touch audio.
    edge::EdgeEngine& shapeEngine()
    {
        static edge::EdgeEngine e;
        static bool prepared = false;

        if (! prepared)
        {
            e.prepare (48000.0, 512, 1);
            prepared = true;
        }

        return e;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getSample (c, i)))
                    return false;

        return true;
    }

    float maxAbs (const juce::AudioBuffer<float>& b)
    {
        float m = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            m = juce::jmax (m, b.getMagnitude (c, 0, b.getNumSamples()));
        return m;
    }

    //  Largest sample-to-sample jump, the standard proxy for a click. Compared
    //  against the same figure for the unprocessed source so that a legitimate
    //  transient is not counted as a click.
    float maxStep (const juce::AudioBuffer<float>& b, int from = 1)
    {
        float m = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);
            for (int i = juce::jmax (1, from); i < b.getNumSamples(); ++i)
                m = juce::jmax (m, std::abs (d[i] - d[i - 1]));
        }
        return m;
    }
}

// =============================================================================
namespace
{
    constexpr double kSr = 48000.0;

    void testNeutral()
    {
        section ("1. Neutral state");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);
        e.snapToSettings (neutral());

        juce::Random rng (1234);
        juce::AudioBuffer<float> in (2, 8192), out (2, 8192);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 8192; ++i)
            {
                const float v = rng.nextFloat() * 2.0f - 1.0f;
                in.setSample (c, i, v);
                out.setSample (c, i, v);
            }

        run (e, out, 128);

        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 8192; ++i)
                worst = juce::jmax (worst, (double) std::abs (out.getSample (c, i)
                                                            - in.getSample (c, i)));

        check (worst == 0.0, "defaults are a BIT-EXACT pass-through",
               "max |out-in| = " + f (worst, 12));

        check (e.getColourDrivePercent() == 0.0f, "colour drive is exactly 0 when neutral",
               f (e.getColourDrivePercent()) + " %");

        check (e.getLatencySamples() == 0.0f, "reported latency",
               f (e.getLatencySamples(), 1) + " samples, "
               + juce::String (e.getOversamplingFactor()) + "x colour");
    }

    void testAnalyticVsMeasured()
    {
        section ("2. Drawn curve == measured response");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        struct Case { const char* name; Settings s; };
        std::vector<Case> cases;

        {
            Settings s = neutral();
            s.lowFreqHz = 200.0f; s.lowDepthPercent = 40.0f;   // -6 dB
            cases.push_back ({ "low -6 dB @200 Hz, neutral curve", s });
        }
        {
            Settings s = neutral();
            s.lowFreqHz = 100.0f; s.lowDepthPercent = 100.0f; s.lowCurvePercent = 100.0f;
            cases.push_back ({ "low CUT @100 Hz, tight", s });
        }
        {
            Settings s = neutral();
            s.highFreqHz = 4000.0f; s.highDepthPercent = 58.0f; s.highCurvePercent = 0.0f;
            cases.push_back ({ "high -12 dB @4 kHz, soft", s });
        }
        {
            Settings s = neutral();
            s.lowFreqHz = 300.0f; s.lowDepthPercent = 100.0f; s.lowResPercent = 80.0f;
            cases.push_back ({ "low CUT @300 Hz, resonance 80", s });
        }

        const double probes[] = { 30.0, 60.0, 120.0, 250.0, 500.0, 1000.0,
                                  2000.0, 5000.0, 10000.0 };

        for (auto& c : cases)
        {
            const auto sh = shapeFor (shapeEngine(), c.s);
            double worst = 0.0;
            double at = 0.0;

            for (double fq : probes)
            {
                const double predicted = edge::magnitudeDb (sh, kSr, fq);
                const double measured  = measuredGainDb (e, c.s, kSr, fq);

                //  Below -80 dB the measurement is dominated by float32 noise,
                //  not by the filter, so it is not compared.
                if (predicted < -80.0)
                    continue;

                const double err = std::abs (predicted - measured);
                if (err > worst) { worst = err; at = fq; }
            }

            check (worst < 0.35, c.name,
                   "worst |drawn-measured| = " + f (worst, 3) + " dB @ " + f (at, 0) + " Hz");
        }
    }

    void testDepth()
    {
        section ("3. Depth");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        //  A shelf's Depth IS its DC gain. Assert that exactly, then assert
        //  that the real filter gets there.
        struct Row { float percent; double wantDb; };
        const Row rows[] = { { 25.0f, -3.0 }, { 40.0f, -6.0 }, { 58.0f, -12.0 },
                             { 78.0f, -24.0 }, { 92.0f, -48.0 } };

        for (auto r : rows)
        {
            Settings s = neutral();
            s.lowFreqHz = 2000.0f;
            s.lowDepthPercent = r.percent;

            //  The FILTER's DC gain, with colourEngage left at 0. At 0.5 Hz
            //  the colour stage's 10 Hz DC blocker dominates everything, so
            //  including it here would measure the blocker, not the shelf.
            edge::EdgeShape pure {};
            pure.lowHz = s.lowFreqHz;
            pure.lowDepthDb = edge::depthPercentToDb (s.lowDepthPercent);
            pure.lowCurve01 = s.lowCurvePercent * 0.01f;
            const double dc = edge::magnitudeDb (pure, kSr, 0.5);

            //  Six octaves below the corner: a second-order shelf's approach to
            //  its asymptote goes as (f/fc)^2, so four octaves still leaves
            //  0.4 dB on the table at -24 dB and 3 dB at -48 dB. That is the
            //  shelf being a shelf, not an error - which is why the asymptote
            //  and the settling are checked separately.
            const double got = measuredGainDb (e, s, kSr, 31.25);

            //  0.5 dB on the measured figure: it is the whole plug-in, so it
            //  carries the colour stage's -0.42 dB at 31 Hz on top of the
            //  shelf's own residual approach. Test 2 is what pins the measured
            //  value to the drawn one.
            check (std::abs (dc - r.wantDb) < 0.05 && std::abs (got - r.wantDb) < 0.5,
                   (juce::String ("Depth ") + juce::String (r.percent, 0)
                        + " % -> " + f (r.wantDb, 0) + " dB").toRawUTF8(),
                   "DC " + f (dc, 3) + " dB, measured 6 oct down " + f (got, 2) + " dB");
        }

        Settings cut = neutral();
        cut.lowFreqHz = 2000.0f;
        cut.lowDepthPercent = 100.0f;
        cut.lowCurvePercent = 100.0f;
        const double atCut = measuredGainDb (e, cut, kSr, 250.0);
        check (atCut < -90.0, "Depth 100 % is a real cut (3 oct below Fc, tight)",
               f (atCut, 1) + " dB");

        //  Continuity: no step anywhere along the control, including through
        //  the shelf -> cut region at the top.
        constexpr int kSteps = 2000;
        double worstStep = 0.0;
        float worstAt = 0.0f;
        double prev = 0.0;
        for (int i = 0; i <= kSteps; ++i)
        {
            Settings s = neutral();
            s.lowFreqHz = 500.0f;
            s.lowDepthPercent = 100.0f * (float) i / (float) kSteps;
            const auto sh = shapeFor (shapeEngine(), s);

            //  Half an octave above the corner, where the shelf->cut morph is
            //  most likely to misbehave.
            const double v = edge::magnitudeDb (sh, kSr, 700.0);

            if (i > 0 && std::abs (v - prev) > worstStep)
            {
                worstStep = std::abs (v - prev);
                worstAt = s.lowDepthPercent;
            }
            prev = v;
        }

        check (worstStep < 0.05, "Depth 0->100 % in 2000 steps: largest step @700 Hz",
               f (worstStep, 4) + " dB at " + f (worstAt, 2) + " %");
    }

    void testMonotonic()
    {
        section ("4. Monotonicity without resonance");

        double worstBulge = 0.0;
        juce::String worstWhere;

        for (int d = 1; d <= 20; ++d)
        {
            for (int c = 0; c <= 10; ++c)
            {
                Settings s = neutral();
                s.lowFreqHz = 500.0f;
                s.lowDepthPercent = 100.0f * (float) d / 20.0f;
                s.lowCurvePercent = 100.0f * (float) c / 10.0f;
                const auto sh = shapeFor (shapeEngine(), s);

                double prev = -1000.0;
                for (int k = 0; k <= 600; ++k)
                {
                    const double fq = 10.0 * std::pow (2000.0, (double) k / 600.0);
                    if (fq >= kSr * 0.45) break;

                    const double v = edge::magnitudeDb (sh, kSr, fq);
                    const double drop = prev - v;      // > 0 means it went DOWN
                    if (drop > worstBulge)
                    {
                        worstBulge = drop;
                        worstWhere = "depth " + juce::String (s.lowDepthPercent, 0)
                                   + " % curve " + juce::String (s.lowCurvePercent, 0)
                                   + " % @ " + juce::String (fq, 0) + " Hz";
                    }
                    prev = v;
                }
            }
        }

        check (worstBulge < 0.01,
               "LOW: response never dips going up in frequency",
               f (worstBulge, 6) + " dB   " + worstWhere);

        //  And nothing anywhere may exceed 0 dB.
        double worstOver = -1000.0;
        for (int d = 0; d <= 20; ++d)
            for (int c = 0; c <= 10; ++c)
            {
                Settings s = neutral();
                s.lowFreqHz = 500.0f;  s.lowDepthPercent = 5.0f * (float) d;
                s.lowCurvePercent = 10.0f * (float) c;
                s.highFreqHz = 3000.0f; s.highDepthPercent = 5.0f * (float) d;
                s.highCurvePercent = 10.0f * (float) c;
                const auto sh = shapeFor (shapeEngine(), s);

                for (int k = 0; k <= 400; ++k)
                {
                    const double fq = 15.0 * std::pow (1300.0, (double) k / 400.0);
                    worstOver = juce::jmax (worstOver, edge::magnitudeDb (sh, kSr, fq));
                }
            }

        check (worstOver < 0.001, "no unplanned peak above 0 dB anywhere",
               "worst = " + f (worstOver, 6) + " dB");
    }

    void testCurve()
    {
        section ("5. Curve");

        //  Slope at full cut, one octave below the corner.
        for (int c : { 0, 50, 100 })
        {
            Settings s = neutral();
            s.lowFreqHz = 1000.0f;
            s.lowDepthPercent = 100.0f;
            s.lowCurvePercent = (float) c;
            const auto sh = shapeFor (shapeEngine(), s);

            const double a = edge::magnitudeDb (sh, kSr, 250.0);
            const double b = edge::magnitudeDb (sh, kSr, 125.0);
            const double slope = a - b;      // dB per octave

            check (slope > 6.0 && slope < 40.0,
                   (juce::String ("Curve ") + juce::String (c) + " % slope at full cut").toRawUTF8(),
                   f (slope, 1) + " dB/oct");
        }

        //  Continuity across the whole travel, at a moderate depth and at cut.
        for (float depth : { 40.0f, 100.0f })
        {
            constexpr int kSteps = 2000;
            double worstStep = 0.0;
            double prev = 0.0;

            for (int i = 0; i <= kSteps; ++i)
            {
                Settings s = neutral();
                s.lowFreqHz = 500.0f;
                s.lowDepthPercent = depth;
                s.lowCurvePercent = 100.0f * (float) i / (float) kSteps;
                const auto sh = shapeFor (shapeEngine(), s);

                const double v = edge::magnitudeDb (sh, kSr, 350.0);
                if (i > 0)
                    worstStep = juce::jmax (worstStep, std::abs (v - prev));
                prev = v;
            }

            //  0.83 dB over 200 steps became 0.09 over 2000: the response is
            //  continuous, it is simply steep near the corner at full cut,
            //  where Curve legitimately moves the response by about 14 dB.
            check (worstStep < 0.12,
                   (juce::String ("Curve 0->100 % in 2000 steps at depth ") + juce::String (depth, 0)
                        + " %: largest step").toRawUTF8(),
                   f (worstStep, 4) + " dB");
        }
    }

    void testResonance()
    {
        section ("6. Resonance");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        //  At Depth 0 the section is a wire whatever k is, so resonance must be
        //  literally inaudible - not "small".
        Settings s = neutral();
        s.lowResPercent = 100.0f;
        s.highResPercent = 100.0f;

        double worst = 0.0;
        const auto sh = shapeFor (shapeEngine(), s);
        for (int k = 0; k <= 300; ++k)
        {
            const double fq = 20.0 * std::pow (1000.0, (double) k / 300.0);
            worst = juce::jmax (worst, std::abs (edge::magnitudeDb (sh, kSr, fq)));
        }
        check (worst < 1.0e-6, "Resonance 100 % at Depth 0 changes nothing",
               "worst |dB| = " + f (worst, 9));

        //  Peak height at full cut, and never any self-oscillation.
        Settings r = neutral();
        r.lowFreqHz = 500.0f;
        r.lowDepthPercent = 100.0f;
        r.lowResPercent = 100.0f;
        const auto rs = shapeFor (shapeEngine(), r);

        double peak = -1000.0, peakAt = 0.0;
        for (int k = 0; k <= 800; ++k)
        {
            const double fq = 100.0 * std::pow (40.0, (double) k / 800.0);
            const double v = edge::magnitudeDb (rs, kSr, fq);
            if (v > peak) { peak = v; peakAt = fq; }
        }
        check (peak < 12.0 && peak > 3.0, "full resonance peak height at full cut",
               f (peak, 2) + " dB @ " + f (peakAt, 0) + " Hz");

        //  How much the emphasis survives as Curve tightens. Resonance lives on
        //  section 0 only, and with a tight Curve the other two sections keep
        //  falling straight through the peak - so the emphasis is strongest at
        //  Soft/Neutral. Recorded here because it is a real characteristic of
        //  the design, documented in docs/DSP-TOPOLOGY.md section 5.
        juce::String byCurve;
        bool alwaysAudible = true;

        for (int c : { 0, 25, 50, 75, 100 })
        {
            Settings t = r;
            t.lowCurvePercent = (float) c;
            const auto ts = shapeFor (shapeEngine(), t);

            double flat = ts.outputDb, p2 = -1000.0;
            for (int k = 0; k <= 600; ++k)
            {
                const double fq = 100.0 * std::pow (40.0, (double) k / 600.0);
                p2 = juce::jmax (p2, edge::magnitudeDb (ts, kSr, fq));
            }

            byCurve += juce::String (c) + "%:" + juce::String (p2 - flat, 1) + "dB  ";
            alwaysAudible = alwaysAudible && (p2 - flat) > 1.0;
        }

        check (alwaysAudible, "resonance stays audible across the whole Curve range",
               byCurve.trim());

        //  Silence in with maximum resonance must stay silence: nothing rings
        //  on its own.
        e.reset();
        e.snapToSettings (r);
        juce::AudioBuffer<float> buf (1, (int) kSr);
        buf.clear();
        buf.setSample (0, 0, 1.0f);       // one impulse, then leave it alone
        run (e, buf, 64);

        float tail = 0.0f;
        for (int i = (int) (kSr * 0.5); i < buf.getNumSamples(); ++i)
            tail = juce::jmax (tail, std::abs (buf.getSample (0, i)));

        check (tail < 1.0e-5f, "no self-oscillation: impulse tail after 0.5 s",
               juce::String (juce::Decibels::gainToDecibels (juce::jmax (1.0e-12f, tail)), 1) + " dBFS");

        //  Resonance must not change level when the cutoff moves.
        double minPeak = 1000.0, maxPeak = -1000.0;
        for (double fc : { 60.0, 200.0, 800.0, 3000.0 })
        {
            Settings t = r;
            t.lowFreqHz = (float) fc;
            const auto ts = shapeFor (shapeEngine(), t);

            double p = -1000.0;
            for (int k = -200; k <= 200; ++k)
            {
                const double fq = fc * std::pow (2.0, (double) k / 100.0);
                if (fq > kSr * 0.45 || fq < 5.0) continue;
                p = juce::jmax (p, edge::magnitudeDb (ts, kSr, fq));
            }
            minPeak = juce::jmin (minPeak, p);
            maxPeak = juce::jmax (maxPeak, p);
        }
        check (maxPeak - minPeak < 0.5, "resonance peak is cutoff-independent",
               "spread = " + f (maxPeak - minPeak, 3) + " dB over 60 Hz..3 kHz");
    }

    void testAutomation()
    {
        section ("7. Automation");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        //  A 20 Hz -> 20 kHz -> 20 Hz cutoff sweep on a steady tone, at cut
        //  depth. The classic zipper test.
        Settings s = neutral();
        s.lowDepthPercent = 100.0f;
        s.lowCurvePercent = 100.0f;
        s.lowFreqHz = 20.0f;
        e.snapToSettings (s);

        const int total = (int) (kSr * 2.0);
        juce::AudioBuffer<float> buf (2, total);
        const double w = juce::MathConstants<double>::twoPi * 440.0 / kSr;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                buf.setSample (c, i, 0.5f * (float) std::sin (w * (double) i));

        const float sourceStep = 0.5f * (float) std::abs (std::sin (w) - 0.0);

        int pos = 0;
        const int block = 64;
        while (pos < total)
        {
            const int n = juce::jmin (block, total - pos);
            const double t = (double) pos / total;
            const double tri = t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0;

            s.lowFreqHz = (float) (20.0 * std::pow (400.0, tri));   // 20 Hz .. 8 kHz
            e.setSettings (s);

            float* ptrs[2] = { buf.getWritePointer (0) + pos, buf.getWritePointer (1) + pos };
            juce::AudioBuffer<float> view (ptrs, 2, n);
            e.process (view);
            pos += n;
        }

        check (allFinite (buf), "cutoff sweep 20 Hz..8 kHz stays finite", "ok");
        check (maxAbs (buf) < 1.0f, "cutoff sweep never exceeds the source level",
               "peak " + f (maxAbs (buf), 4));
        check (maxStep (buf) < sourceStep * 3.0f,
               "cutoff sweep: largest sample step vs source",
               f (maxStep (buf), 6) + " vs source " + f (sourceStep, 6));

        //  Depth 0 -> 100 -> 0 during playback.
        e.reset();
        s = neutral();
        s.lowFreqHz = 400.0f;
        e.snapToSettings (s);

        buf.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                buf.setSample (c, i, 0.5f * (float) std::sin (w * (double) i));

        pos = 0;
        while (pos < total)
        {
            const int n = juce::jmin (block, total - pos);
            const double t = (double) pos / total;
            s.lowDepthPercent = (float) (100.0 * (t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0));
            e.setSettings (s);

            float* ptrs[2] = { buf.getWritePointer (0) + pos, buf.getWritePointer (1) + pos };
            juce::AudioBuffer<float> view (ptrs, 2, n);
            e.process (view);
            pos += n;
        }

        check (allFinite (buf) && maxStep (buf) < sourceStep * 3.0f,
               "Depth 0->100->0 sweep: largest sample step",
               f (maxStep (buf), 6) + " vs source " + f (sourceStep, 6));

        //  Curve swept end to end while a cut is active.
        e.reset();
        s = neutral();
        s.lowFreqHz = 400.0f;
        s.lowDepthPercent = 100.0f;
        e.snapToSettings (s);
        buf.clear();
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                buf.setSample (c, i, 0.5f * (float) std::sin (w * (double) i));

        pos = 0;
        while (pos < total)
        {
            const int n = juce::jmin (block, total - pos);
            const double t = (double) pos / total;
            s.lowCurvePercent = (float) (100.0 * (t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0));
            e.setSettings (s);
            float* ptrs[2] = { buf.getWritePointer (0) + pos, buf.getWritePointer (1) + pos };
            juce::AudioBuffer<float> view (ptrs, 2, n);
            e.process (view);
            pos += n;
        }

        check (allFinite (buf) && maxStep (buf) < sourceStep * 3.0f,
               "Curve swept end to end at full cut: largest sample step",
               f (maxStep (buf), 6));
    }

    void testFocusAndLink()
    {
        section ("8. Focus");

        //  Continuity over the whole travel, including at both boundaries.
        double worstLow = 0.0, worstHigh = 0.0;
        float prevLow = 0.0f, prevHigh = 0.0f;

        for (int i = 0; i <= 400; ++i)
        {
            const float focus = -1.0f + 2.0f * (float) i / 400.0f;
            float lo = 0.0f, hi = 0.0f;
            edge::resolveFrequencies (100.0f, 5000.0f, focus, lo, hi);

            if (i > 0)
            {
                worstLow  = juce::jmax (worstLow,  (double) std::abs (std::log2 (lo / prevLow)));
                worstHigh = juce::jmax (worstHigh, (double) std::abs (std::log2 (hi / prevHigh)));
            }
            prevLow = lo; prevHigh = hi;
        }

        check (worstLow < 0.02 && worstHigh < 0.02,
               "Focus -100..+100 in 400 steps: largest jump",
               "low " + f (worstLow * 1200.0, 1) + " cents, high "
                   + f (worstHigh * 1200.0, 1) + " cents");

        //  At the boundary Focus must slow down, not stop dead or overshoot.
        float lo = 0.0f, hi = 0.0f;
        edge::resolveFrequencies (edge::kLowFreqMax, edge::kHighFreqMin, 1.0f, lo, hi);
        check (lo <= edge::kLowFreqMax + 0.1f && hi >= edge::kHighFreqMin - 0.1f
                   && hi >= lo * (edge::kMinEdgeRatio - 0.01f),
               "Focus +100 with the edges already touching stays legal",
               "low " + f (lo, 1) + " Hz, high " + f (hi, 1) + " Hz");

        edge::resolveFrequencies (edge::kLowFreqMin, edge::kHighFreqMax, -1.0f, lo, hi);
        check (lo >= edge::kLowFreqMin - 0.01f && hi <= edge::kHighFreqMax + 0.01f,
               "Focus -100 at both range ends stays inside the ranges",
               "low " + f (lo, 2) + " Hz, high " + f (hi, 1) + " Hz");

        //  Minimum separation.
        edge::resolveFrequencies (2000.0f, 2000.0f, 0.0f, lo, hi);
        check (hi / lo >= edge::kMinEdgeRatio * 0.98f,
               "edges cannot collapse onto each other",
               "ratio " + f (hi / lo, 4));
    }

    void testSignalHygiene()
    {
        section ("9. Signal hygiene");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        Settings hard = neutral();
        hard.lowFreqHz = 300.0f;  hard.lowDepthPercent = 100.0f;  hard.lowResPercent = 100.0f;
        hard.lowCurvePercent = 100.0f;
        hard.highFreqHz = 2000.0f; hard.highDepthPercent = 100.0f; hard.highResPercent = 100.0f;
        hard.highCurvePercent = 100.0f;

        //  Silence.
        e.snapToSettings (hard);
        juce::AudioBuffer<float> buf (2, 8192);
        buf.clear();
        run (e, buf, 128);
        check (maxAbs (buf) == 0.0f, "silence in -> silence out (worst-case settings)",
               f (maxAbs (buf), 12));

        //  DC.
        e.reset();
        e.snapToSettings (hard);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 8192; ++i)
                buf.setSample (c, i, 1.0f);
        run (e, buf, 128);
        float dcTail = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int i = 4096; i < 8192; ++i)
                dcTail = juce::jmax (dcTail, std::abs (buf.getSample (c, i)));
        check (allFinite (buf) && dcTail < 1.0e-4f, "DC input is removed by the low cut",
               juce::String (juce::Decibels::gainToDecibels (juce::jmax (1.0e-12f, dcTail)), 1) + " dBFS");

        //  Hot input, +6 dBFS.
        e.reset();
        e.snapToSettings (hard);
        juce::Random rng (99);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 8192; ++i)
                buf.setSample (c, i, 2.0f * (rng.nextFloat() * 2.0f - 1.0f));
        run (e, buf, 128);
        check (allFinite (buf) && maxAbs (buf) < 8.0f, "+6 dBFS noise stays finite and bounded",
               "peak " + f (maxAbs (buf), 3));

        //  Denormals: a decaying impulse tail must not slow the process down or
        //  produce subnormal state. ScopedNoDenormals is the plug-in's job, so
        //  here we only assert the tail actually reaches zero.
        e.reset();
        Settings res = neutral();
        res.lowFreqHz = 50.0f; res.lowDepthPercent = 100.0f; res.lowResPercent = 90.0f;
        e.snapToSettings (res);
        juce::AudioBuffer<float> tailBuf (1, (int) (kSr * 4));
        tailBuf.clear();
        tailBuf.setSample (0, 0, 1.0f);
        {
            juce::ScopedNoDenormals noDenormals;
            run (e, tailBuf, 256);
        }
        float last = 0.0f;
        for (int i = tailBuf.getNumSamples() - 1024; i < tailBuf.getNumSamples(); ++i)
            last = juce::jmax (last, std::abs (tailBuf.getSample (0, i)));
        char tailText[64];
        std::snprintf (tailText, sizeof (tailText), "|tail| = %.3e%s", (double) last,
                       (last != 0.0f && std::abs (last) < 1.18e-38f) ? "  DENORMAL" : "");
        check (last == 0.0f || std::abs (last) >= 1.18e-38f,
               "no denormal state left in the tail", tailText);
    }

    void testStereoAndBlocks()
    {
        section ("10. Stereo, block sizes, sample rates");

        //  Both channels must be identical for identical input, and there must
        //  be no timing difference between them.
        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);
        Settings s = neutral();
        s.lowFreqHz = 200.0f; s.lowDepthPercent = 70.0f; s.lowResPercent = 50.0f;
        s.highFreqHz = 6000.0f; s.highDepthPercent = 60.0f;
        e.snapToSettings (s);

        juce::Random rng (7);
        juce::AudioBuffer<float> buf (2, 16384);
        for (int i = 0; i < 16384; ++i)
        {
            const float v = rng.nextFloat() * 2.0f - 1.0f;
            buf.setSample (0, i, v);
            buf.setSample (1, i, v);
        }
        run (e, buf, 128);

        double diff = 0.0;
        for (int i = 0; i < 16384; ++i)
            diff = juce::jmax (diff, (double) std::abs (buf.getSample (0, i) - buf.getSample (1, i)));
        check (diff == 0.0, "stereo-linked: L and R are sample-identical",
               "max |L-R| = " + f (diff, 12));

        //  Block-size invariance, including a one-sample block.
        auto renderWith = [&] (int blockSize)
        {
            edge::EdgeEngine en;
            en.prepare (kSr, 512, 1);
            en.snapToSettings (s);

            juce::Random r2 (4242);
            juce::AudioBuffer<float> b (1, 4096);
            for (int i = 0; i < 4096; ++i)
                b.setSample (0, i, r2.nextFloat() * 2.0f - 1.0f);

            run (en, b, blockSize);
            return b;
        };

        auto ref = renderWith (512);
        for (int bs : { 1, 3, 32, 64, 127, 256 })
        {
            auto got = renderWith (bs);
            double worst = 0.0;
            for (int i = 0; i < 4096; ++i)
                worst = juce::jmax (worst, (double) std::abs (got.getSample (0, i)
                                                            - ref.getSample (0, i)));

            //  Not bit-identical by design: coefficients are refreshed every
            //  32 samples, so a 1-sample block refreshes them 32x more often
            //  during the first 20 ms of smoothing. After that they agree.
            check (worst < 1.0e-3, ("block size " + juce::String (bs)
                                    + " vs 512: worst deviation").toRawUTF8(),
                   f (worst, 9));
        }

        //  A host that ignores the block size it announced. dryBuffer is sized
        //  for the announced size, so this used to be a buffer overrun.
        {
            edge::EdgeEngine big;
            big.prepare (kSr, 128, 2);      // announce 128 ...
            big.snapToSettings (s);

            juce::AudioBuffer<float> b (2, 4096);
            juce::Random r3 (77);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 4096; ++i)
                    b.setSample (c, i, 0.3f * (r3.nextFloat() * 2.0f - 1.0f));

            run (big, b, 1024);             // ... then send 1024
            check (allFinite (b) && maxAbs (b) < 1.0f,
                   "block larger than the announced size is handled",
                   "128 announced, 1024 delivered, peak " + f (maxAbs (b), 3));
        }

        //  Mono.
        {
            edge::EdgeEngine mono;
            mono.prepare (kSr, 512, 1);
            mono.snapToSettings (s);
            juce::AudioBuffer<float> b (1, 4096);
            for (int i = 0; i < 4096; ++i)
                b.setSample (0, i, 0.3f * (float) std::sin (0.05 * i));
            run (mono, b, 64);
            check (allFinite (b), "mono layout processes correctly", "ok");
        }

        //  Corner accuracy at every supported rate. The measurement is the
        //  frequency at which a -6 dB shelf has fallen by 3 dB; TPT prewarping
        //  should put it in the same place at every rate.
        juce::String rateLine;
        double refCorner = 0.0;
        bool ratesOk = true;

        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            edge::EdgeEngine en;
            en.prepare (sr, 512, 1);

            Settings t = neutral();
            t.lowFreqHz = 1000.0f;
            t.lowDepthPercent = 100.0f;
            t.lowCurvePercent = 50.0f;
            const auto sh = shapeFor (en, t);

            //  Find the -3 dB point of the cut.
            double lo = 100.0, hi = 8000.0;
            for (int it = 0; it < 60; ++it)
            {
                const double mid = std::sqrt (lo * hi);
                (edge::magnitudeDb (sh, sr, mid) < -3.0 ? lo : hi) = mid;
            }
            const double corner = std::sqrt (lo * hi);

            if (refCorner == 0.0) refCorner = corner;
            if (std::abs (std::log2 (corner / refCorner)) > 0.01) ratesOk = false;

            rateLine += juce::String (sr / 1000.0, 1) + "k:" + juce::String (corner, 1) + "Hz  ";
        }

        check (ratesOk, "-3 dB corner is rate-independent (prewarping)", rateLine);
    }

    void testColour()
    {
        section ("11. Hidden colour engine");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        //  The law: silent when neutral, bounded when not.
        check (edge::colourDrivePercent (0.0f, 0.0f, 0.0f, 0.0f) == 0.0f,
               "colour law is exactly 0 at neutral", "0 %");
        check (edge::colourDrivePercent (1.0f, 1.0f, 1.0f, 1.0f) == edge::kColorDriveMax,
               "colour law saturates at its ceiling",
               f (edge::kColorDriveMax, 1) + " % = "
                   + f (24.0 * edge::kColorDriveMax / 100.0, 2) + " dB of pre-gain");

        //  Broadband level: pushing Depth must not quietly change the level of
        //  material in the PASSBAND. This is what the measured small-signal
        //  trim in ColorStage exists to guarantee.
        juce::String levels;
        double worstShift = 0.0;

        for (float depth : { 0.0f, 40.0f, 78.0f, 100.0f })
        {
            Settings s = neutral();
            s.lowFreqHz = 100.0f;
            s.lowDepthPercent = depth;

            //  5 kHz is far above the low edge: the filter should do nothing
            //  there, so any change is the colour engine's doing.
            for (float amp : { 1.0e-3f, 0.125f })
            {
                const double got = measuredGainDb (e, s, kSr, 5000.0, amp);
                worstShift = juce::jmax (worstShift, std::abs (got));
                levels += juce::String (depth, 0) + "%@"
                        + juce::String (juce::Decibels::gainToDecibels (amp), 0) + "dB:"
                        + juce::String (got, 2) + "  ";
            }
        }

        check (worstShift < 1.0, "colour does not shift passband level with Depth",
               "worst " + f (worstShift, 2) + " dB   [" + levels.trim() + "]");

        //  Aliasing. A 1 kHz sine at a hot level with the colour at maximum;
        //  everything that is not a harmonic of 1 kHz is aliasing.
        auto aliasFloorDb = [&] (int osFactor)
        {
            edge::EdgeEngine en;
            en.setOversamplingFactor (osFactor);
            en.prepare (kSr, 1024, 1);

            Settings s = neutral();
            s.lowFreqHz = 20.0f;
            s.lowDepthPercent = 100.0f;      // maximum colour activity
            s.lowCurvePercent = 0.0f;
            en.snapToSettings (s);

            constexpr int fftOrder = 15;
            constexpr int fftSize = 1 << fftOrder;

            //  Bin-centred fundamental and a Blackman-Harris window: a
            //  non-integer number of periods under a Hann window fakes an
            //  alias floor around -31 dB that is entirely sidelobe.
            const int bin = 683;                       // 683 * 48000/32768 = 1000.2 Hz
            const double f0 = bin * kSr / fftSize;

            juce::AudioBuffer<float> b (1, fftSize * 2);
            const double w = juce::MathConstants<double>::twoPi * f0 / kSr;
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.5f * (float) std::sin (w * (double) i));

            run (en, b, 256);

            std::vector<float> fftData ((size_t) fftSize * 2, 0.0f);
            for (int i = 0; i < fftSize; ++i)
            {
                const double t = (double) i / (fftSize - 1);
                const double win = 0.35875 - 0.48829 * std::cos (juce::MathConstants<double>::twoPi * t)
                                 + 0.14128 * std::cos (2.0 * juce::MathConstants<double>::twoPi * t)
                                 - 0.01168 * std::cos (3.0 * juce::MathConstants<double>::twoPi * t);
                fftData[(size_t) i] = b.getSample (0, fftSize + i) * (float) win;
            }

            juce::dsp::FFT fft (fftOrder);
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            double fundamental = 0.0, alias = 0.0;
            for (int k = 1; k < fftSize / 2; ++k)
            {
                const double mag = fftData[(size_t) k];
                const bool isHarmonic = (k % bin) <= 3 || (bin - (k % bin)) <= 3;

                if (k == bin)
                    fundamental = juce::jmax (fundamental, mag);
                else if (! isHarmonic)
                    alias = juce::jmax (alias, mag);
            }

            return 20.0 * std::log10 (juce::jmax (1.0e-12, alias / juce::jmax (1.0e-12, fundamental)));
        };

        const double alias1x = aliasFloorDb (1);
        const double alias2x = aliasFloorDb (2);

        //  -70 dBc is the bar. The interesting number is the DIFFERENCE: real
        //  aliasing from a soft 2.4 dB saturator drops 20-40 dB when you
        //  oversample it. A few dB means most of what is being measured is not
        //  aliasing at all (it is WARM's sag envelope amplitude-modulating the
        //  tone, which 2x cannot help), and that is the evidence for shipping
        //  at 1x with zero latency.
        edge::EdgeEngine osProbe;
        osProbe.setOversamplingFactor (2);
        osProbe.prepare (kSr, 1024, 1);

        check (alias1x < -70.0, "aliasing at 1x with the colour at maximum",
               f (alias1x, 1) + " dBc;  2x gives " + f (alias2x, 1) + " dBc for "
                   + f (osProbe.getLatencySamples(), 2) + " samples of latency");
    }

    //  Level of one frequency in a buffer, Hann-windowed single-bin DFT.
    double binLevelDb (const juce::AudioBuffer<float>& b, double sampleRate,
                       double freqHz, int from, int count)
    {
        const double w = juce::MathConstants<double>::twoPi * freqHz / sampleRate;
        double re = 0.0, im = 0.0, winSum = 0.0;

        for (int i = 0; i < count; ++i)
        {
            const double win = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi
                                                         * (double) i / (count - 1));
            const double v = b.getSample (0, from + i) * win;
            re += v * std::sin (w * (double) (from + i));
            im += v * std::cos (w * (double) (from + i));
            winSum += win;
        }

        const double amp = 2.0 * std::sqrt (re * re + im * im) / juce::jmax (1.0, winSum);
        return juce::Decibels::gainToDecibels (juce::jmax (1.0e-14, amp));
    }

    //  STAGE 4. Pre-filter vs post-filter colour, measured rather than argued.
    void testColourPlacement()
    {
        section ("16. Colour placement: pre vs post");

        constexpr int settle = 24000;
        constexpr int measure = 1 << 16;

        auto renderTwoTone = [&] (edge::EdgeEngine::ColourPlacement place,
                                  const Settings& s, double fA, double fB)
        {
            edge::EdgeEngine e;
            e.setColourPlacement (place);
            e.prepare (kSr, 512, 1);
            e.snapToSettings (s);

            juce::AudioBuffer<float> b (1, settle + measure);
            const double wA = juce::MathConstants<double>::twoPi * fA / kSr;
            const double wB = juce::MathConstants<double>::twoPi * fB / kSr;

            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.35f * (float) (std::sin (wA * i) + std::sin (wB * i)));

            run (e, b, 256);
            return b;
        };

        //  LOW EDGE. Two tones a hundred hertz apart, well inside the passband
        //  of a 300 Hz cut. Their intermodulation difference product lands at
        //  100 Hz - two octaves BELOW the cut, where the plug-in has just
        //  promised there is nothing.
        {
            Settings s = neutral();
            s.lowFreqHz = 300.0f;
            s.lowDepthPercent = 100.0f;
            s.lowCurvePercent = 100.0f;

            const auto pre  = renderTwoTone (edge::EdgeEngine::ColourPlacement::pre,  s, 1000.0, 1100.0);
            const auto post = renderTwoTone (edge::EdgeEngine::ColourPlacement::post, s, 1000.0, 1100.0);

            const double refPre  = binLevelDb (pre,  kSr, 1000.0, settle, measure);
            const double imdPre  = binLevelDb (pre,  kSr,  100.0, settle, measure) - refPre;
            const double imdPost = binLevelDb (post, kSr,  100.0, settle, measure) - refPre;

            check (imdPre < imdPost - 20.0,
                   "LOW: post-filter colour puts IMD back under the cut",
                   "100 Hz product: pre " + f (imdPre, 1) + " dBc, post "
                       + f (imdPost, 1) + " dBc  (advantage "
                       + f (imdPost - imdPre, 1) + " dB)");
        }

        //  HIGH EDGE. One tone below a 3 kHz cut; its own harmonics land above
        //  it. Pre-filter they are removed by the very filter that is supposed
        //  to remove them, post-filter they are the loudest thing up there.
        {
            Settings s = neutral();
            s.highFreqHz = 3000.0f;
            s.highDepthPercent = 100.0f;
            s.highCurvePercent = 100.0f;

            const auto pre  = renderTwoTone (edge::EdgeEngine::ColourPlacement::pre,  s, 1500.0, 1500.0);
            const auto post = renderTwoTone (edge::EdgeEngine::ColourPlacement::post, s, 1500.0, 1500.0);

            const double refPre   = binLevelDb (pre,  kSr, 1500.0, settle, measure);
            const double harmPre  = binLevelDb (pre,  kSr, 4500.0, settle, measure) - refPre;
            const double harmPost = binLevelDb (post, kSr, 4500.0, settle, measure) - refPre;

            check (harmPre < harmPost - 20.0,
                   "HIGH: post-filter colour puts harmonics back over the cut",
                   "4.5 kHz harmonic: pre " + f (harmPre, 1) + " dBc, post "
                       + f (harmPost, 1) + " dBc  (advantage "
                       + f (harmPost - harmPre, 1) + " dB)");
        }

        //  And the placement that ships is the one that is wired in.
        {
            edge::EdgeEngine e;
            e.prepare (kSr, 512, 1);
            Settings s = neutral();
            s.lowFreqHz = 300.0f;
            s.lowDepthPercent = 100.0f;
            s.lowCurvePercent = 100.0f;

            juce::AudioBuffer<float> b (1, settle + measure);
            const double wA = juce::MathConstants<double>::twoPi * 1000.0 / kSr;
            const double wB = juce::MathConstants<double>::twoPi * 1100.0 / kSr;
            e.snapToSettings (s);
            for (int i = 0; i < b.getNumSamples(); ++i)
                b.setSample (0, i, 0.35f * (float) (std::sin (wA * i) + std::sin (wB * i)));
            run (e, b, 256);

            const double ref = binLevelDb (b, kSr, 1000.0, settle, measure);
            const double imd = binLevelDb (b, kSr, 100.0, settle, measure) - ref;

            check (imd < -80.0, "shipping default is pre-filter",
                   "100 Hz product " + f (imd, 1) + " dBc");
        }
    }

    void testBypassAndOutput()
    {
        section ("12. Bypass and Output");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        Settings s = neutral();
        s.lowFreqHz = 500.0f;
        s.lowDepthPercent = 100.0f;
        s.lowResPercent = 60.0f;
        e.snapToSettings (s);

        const int total = (int) kSr;
        juce::AudioBuffer<float> buf (1, total);
        const double w = juce::MathConstants<double>::twoPi * 220.0 / kSr;
        for (int i = 0; i < total; ++i)
            buf.setSample (0, i, 0.5f * (float) std::sin (w * (double) i));

        const float sourceStep = 0.5f * (float) std::abs (std::sin (w));

        int pos = 0;
        while (pos < total)
        {
            const int n = juce::jmin (64, total - pos);
            s.bypass = (pos / 8000) % 2 == 1;      // toggle six times
            e.setSettings (s);
            float* p[1] = { buf.getWritePointer (0) + pos };
            juce::AudioBuffer<float> view (p, 1, n);
            e.process (view);
            pos += n;
        }

        check (allFinite (buf) && maxStep (buf) < sourceStep * 2.0f,
               "six bypass toggles during playback: largest sample step",
               f (maxStep (buf), 6) + " vs source " + f (sourceStep, 6));

        //  Bypass must be a true dry path.
        edge::EdgeEngine b;
        b.prepare (kSr, 512, 1);
        Settings bs = neutral();
        bs.lowDepthPercent = 100.0f;
        bs.outputDb = 12.0f;
        bs.bypass = true;
        b.snapToSettings (bs);

        juce::Random rng (5);
        juce::AudioBuffer<float> in (1, 4096), out (1, 4096);
        for (int i = 0; i < 4096; ++i)
        {
            const float v = rng.nextFloat() * 2.0f - 1.0f;
            in.setSample (0, i, v);
            out.setSample (0, i, v);
        }
        run (b, out, 128);

        double worst = 0.0;
        for (int i = 0; i < 4096; ++i)
            worst = juce::jmax (worst, (double) std::abs (out.getSample (0, i) - in.getSample (0, i)));

        check (worst == 0.0, "bypass is a bit-exact dry path", "max |out-in| = " + f (worst, 12));

        //  Output trim.
        for (double db : { -24.0, -6.0, 6.0, 24.0 })
        {
            Settings o = neutral();
            o.outputDb = (float) db;
            const double got = measuredGainDb (e, o, kSr, 1000.0);
            check (std::abs (got - db) < 0.05, ("Output " + juce::String (db, 0)
                                                + " dB delivers it").toRawUTF8(),
                   f (got, 3) + " dB");
        }
    }

    void testImpulseAndNoise()
    {
        section ("14. Impulse, noise, preset recall");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        Settings s = neutral();
        s.lowFreqHz = 400.0f; s.lowDepthPercent = 85.0f; s.lowResPercent = 40.0f;
        s.highFreqHz = 5000.0f; s.highDepthPercent = 70.0f; s.highCurvePercent = 80.0f;
        e.snapToSettings (s);

        //  Impulse response: finite, causal, and it must decay.
        juce::AudioBuffer<float> ir (1, 1 << 15);
        ir.clear();
        ir.setSample (0, 0, 1.0f);
        run (e, ir, 256);

        double early = 0.0, late = 0.0;
        for (int i = 0; i < 4096; ++i)
            early += (double) ir.getSample (0, i) * ir.getSample (0, i);
        for (int i = ir.getNumSamples() - 4096; i < ir.getNumSamples(); ++i)
            late += (double) ir.getSample (0, i) * ir.getSample (0, i);

        check (allFinite (ir) && early > 1.0e-6 && late < early * 1.0e-8,
               "impulse response is finite and decays",
               "early " + f (10.0 * std::log10 (juce::jmax (1.0e-30, early)), 1)
                   + " dB, late " + f (10.0 * std::log10 (juce::jmax (1.0e-30, late)), 1) + " dB");

        //  White noise at 0 dBFS through the worst case.
        e.reset();
        e.snapToSettings (s);
        juce::Random rng (2024);
        juce::AudioBuffer<float> noise (1, 1 << 16);
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f);
        run (e, noise, 512);
        check (allFinite (noise) && maxAbs (noise) < 2.0f,
               "0 dBFS white noise stays finite and bounded",
               "peak " + f (maxAbs (noise), 3));

        //  Preset change during playback: jump between two very different
        //  settings six times on a steady tone. The plug-in glides rather than
        //  snapping on purpose - see PluginProcessor::setStateInformation.
        e.reset();
        Settings a = neutral();
        a.lowFreqHz = 40.0f; a.lowDepthPercent = 20.0f;
        Settings b = neutral();
        b.lowFreqHz = 3000.0f; b.lowDepthPercent = 100.0f; b.lowResPercent = 100.0f;
        b.highFreqHz = 4000.0f; b.highDepthPercent = 100.0f; b.outputDb = -8.0f;
        e.snapToSettings (a);

        const int total = (int) (kSr * 3.0);
        juce::AudioBuffer<float> tone (1, total);
        const double w = juce::MathConstants<double>::twoPi * 330.0 / kSr;
        for (int i = 0; i < total; ++i)
            tone.setSample (0, i, 0.4f * (float) std::sin (w * (double) i));

        const float sourceStep = 0.4f * (float) std::abs (std::sin (w));

        for (int pos = 0; pos < total; )
        {
            const int n = juce::jmin (64, total - pos);
            e.setSettings ((pos / 16000) % 2 == 0 ? a : b);
            float* ptr[1] = { tone.getWritePointer (0) + pos };
            juce::AudioBuffer<float> view (ptr, 1, n);
            e.process (view);
            pos += n;
        }

        check (allFinite (tone) && maxStep (tone) < sourceStep * 2.0f,
               "six full preset changes during playback: largest sample step",
               f (maxStep (tone), 6) + " vs source " + f (sourceStep, 6));
    }

    void testParameterState()
    {
        section ("15. Parameters and state");

        juce::AudioProcessorValueTreeState::ParameterLayout layout = edge::createParameterLayout();

        //  Round-trip every parameter through a ValueTree, the way a host saves
        //  and restores a project.
        struct Dummy : juce::AudioProcessor
        {
            Dummy() : juce::AudioProcessor (BusesProperties()
                          .withInput ("In", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
            const juce::String getName() const override { return "Dummy"; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            double getTailLengthSeconds() const override { return 0.0; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        };

        Dummy dummy;
        juce::AudioProcessorValueTreeState apvts (dummy, nullptr, "EDGE",
                                                  edge::createParameterLayout());

        const auto& params = apvts.processor.getParameters();
        check (params.size() == 14, "parameter count is the specified control set",
               juce::String (params.size()) + " host parameters");

        //  Frozen IDs: presets and automation lanes reference these strings.
        const char* ids[] = { edge::param::lowFreq, edge::param::lowDepth,
                              edge::param::lowCurve, edge::param::lowRes,
                              edge::param::highFreq, edge::param::highDepth,
                              edge::param::highCurve, edge::param::highRes,
                              edge::param::link, edge::param::focus,
                              edge::param::output, edge::param::bypass,
                              edge::param::lowShoulder, edge::param::highShoulder };

        //  Set every parameter to a distinctive value, save, disturb, restore.
        //  The values compared are the RAW ones the DSP reads, not the
        //  normalised floats: AudioParameterBool::getValue() hands back the
        //  un-snapped float it was given, so comparing those reports a 0.15
        //  "error" on Bypass that no part of the plug-in can ever see.
        juce::Random rng (31);
        std::vector<float> written;

        for (auto* id : ids)
        {
            auto* p = apvts.getParameter (id);
            p->setValueNotifyingHost (0.1f + 0.8f * rng.nextFloat());
            written.push_back (apvts.getRawParameterValue (id)->load());
        }

        auto saved = apvts.copyState();

        for (auto* id : ids)
            apvts.getParameter (id)->setValueNotifyingHost (0.0f);

        apvts.replaceState (saved);

        double worst = 0.0;
        juce::String worstName;
        for (size_t i = 0; i < written.size(); ++i)
        {
            const float now = apvts.getRawParameterValue (ids[i])->load();
            const double d = std::abs (now - written[i]) / juce::jmax (1.0f, std::abs (written[i]));

            if (d > worst) { worst = d; worstName = ids[i]; }
        }

        check (worst < 1.0e-6, "save / restore round-trips every parameter",
               "worst relative error = " + f (worst, 9) + " on \"" + worstName + "\"");

        bool allPresent = true;
        for (auto* id : ids)
            allPresent = allPresent && apvts.getParameter (id) != nullptr;

        check (allPresent, "every documented parameter ID exists", "14 / 14");

        //  The slope table the knob readout, the display combo and the DSP all
        //  share. Every entry must land on the slope it claims.
        {
            double worst = 0.0;
            juce::String detail;

            for (int i = 1; i < edge::kNumSlopeChoices; ++i)   // skip SOFT: a voicing, not a slope
            {
                Settings t = neutral();
                t.lowFreqHz = 1000.0f;
                t.lowDepthPercent = 100.0f;
                t.lowCurvePercent = edge::kSlopeChoices[i].curvePercent;
                const auto sh = shapeFor (shapeEngine(), t);

                //  One octave, between Fc/2 and Fc/4. Far enough below the
                //  corner that the knee is done, and near enough that 36 dB/oct
                //  has not yet reached the -120 dB floor: at Fc = 1 kHz that
                //  floor arrives at 99 Hz, which is why probing at 30-60 Hz
                //  reported 36 dB/oct as 1.8.
                const double a = edge::magnitudeDb (sh, kSr, 500.0);
                const double b = edge::magnitudeDb (sh, kSr, 250.0);

                const double slope = a - b;
                const double want = juce::String (edge::kSlopeChoices[i].name)
                                        .getDoubleValue();

                worst = juce::jmax (worst, std::abs (slope - want) / want);
                detail += juce::String (edge::kSlopeChoices[i].name) + "=" 
                        + juce::String (slope, 1) + "  ";
            }

            check (worst < 0.05, "every slope in the combo delivers its stated dB/oct",
                   "worst error " + f (100.0 * worst, 1) + " %   [" + detail.trim() + "]");
        }

        //  Depth must read out in dB, not in percent.
        auto* depth = apvts.getParameter (edge::param::lowDepth);
        depth->setValueNotifyingHost (depth->convertTo0to1 (78.0f));
        const auto text = depth->getText (depth->getValue(), 24);
        check (text.contains ("-24"), "Depth displays dB of attenuation",
               "78 % reads \"" + text + "\"");

        //  Knob readout and combo must not be two names for one setting.
        auto* curveP = apvts.getParameter (edge::param::lowCurve);
        curveP->setValueNotifyingHost (curveP->convertTo0to1 (75.0f));
        const auto curveTxt = curveP->getText (curveP->getValue(), 24);
        check (curveTxt == edge::slopeTextFor (75.0f) && curveTxt == "24 dB/oct",
               "Curve knob reads the same string the slope combo shows",
               "75 % reads \"" + curveTxt + "\"");
    }

    void testShoulder()
    {
        section ("17. Shoulder");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        //  Off must be free: the section is an identity, so the whole plug-in
        //  is still bit-exact with every other control at its default.
        {
            edge::EdgeEngine n;
            n.prepare (kSr, 512, 2);
            n.snapToSettings (neutral());

            juce::Random rng (808);
            juce::AudioBuffer<float> in (2, 8192), out (2, 8192);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 8192; ++i)
                {
                    const float v = rng.nextFloat() * 2.0f - 1.0f;
                    in.setSample (c, i, v);
                    out.setSample (c, i, v);
                }
            run (n, out, 128);

            double worst = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 8192; ++i)
                    worst = juce::jmax (worst, (double) std::abs (out.getSample (c, i)
                                                                - in.getSample (c, i)));
            check (worst == 0.0, "Shoulder at 0 leaves the neutral path bit-exact",
                   "max |out-in| = " + f (worst, 12));
        }

        //  It does what it says: the passband next to a low cut leans down, and
        //  three octaves further up it is back to flat.
        {
            Settings s = neutral();
            s.lowFreqHz = 200.0f;
            s.lowDepthPercent = 100.0f;
            s.lowShoulderPercent = 100.0f;      // -12 dB

            const double near400 = measuredGainDb (e, s, kSr, 400.0);
            const double at1k6   = measuredGainDb (e, s, kSr, 1600.0);
            const double at10k   = measuredGainDb (e, s, kSr, 10000.0);

            //  Six octaves of reach: the lean must still be most of the way
            //  down a THIRD of the way up the passband, and only give up near
            //  the top. Three octaves used to leave 1.6 kHz at -4.4 dB, which
            //  is the version this replaced.
            check (near400 < -10.0 && at1k6 < -9.0 && at1k6 > near400
                       && at10k > near400 + 4.0 && at10k < -2.0,
                   "LOW shoulder leans the WHOLE passband, recovering only at the top",
                   "400 Hz " + f (near400, 2) + " dB, 1.6 kHz " + f (at1k6, 2)
                       + " dB, 10 kHz " + f (at10k, 2) + " dB");
        }

        //  Mirrored for the high edge.
        {
            Settings s = neutral();
            s.highFreqHz = 4000.0f;
            s.highDepthPercent = 100.0f;
            s.highShoulderPercent = 100.0f;

            const double at2k = measuredGainDb (e, s, kSr, 2000.0);
            const double at500 = measuredGainDb (e, s, kSr, 500.0);
            const double at100 = measuredGainDb (e, s, kSr, 100.0);

            check (at2k < -10.0 && at500 < -9.0 && at500 > at2k
                       && at100 > at2k + 4.0 && at100 < -2.0,
                   "HIGH shoulder leans the WHOLE passband, recovering only at the bottom",
                   "2 kHz " + f (at2k, 2) + " dB, 500 Hz " + f (at500, 2)
                       + " dB, 100 Hz " + f (at100, 2) + " dB");
        }

        //  Still monotonic, still nothing above 0 dB, over the whole
        //  (Depth x Curve x Shoulder) space.
        {
            double worstBulge = 0.0, worstOver = -1000.0;
            juce::String where;

            for (int sh = 0; sh <= 4; ++sh)
                for (int d = 0; d <= 5; ++d)
                    for (int c = 0; c <= 4; ++c)
                    {
                        Settings s = neutral();
                        s.lowFreqHz = 400.0f;
                        s.lowDepthPercent = 20.0f * (float) d;
                        s.lowCurvePercent = 25.0f * (float) c;
                        s.lowShoulderPercent = 25.0f * (float) sh;
                        const auto shp = shapeFor (shapeEngine(), s);

                        double prev = -1000.0;
                        for (int k = 0; k <= 400; ++k)
                        {
                            const double fq = 15.0 * std::pow (1300.0, (double) k / 400.0);
                            const double v = edge::magnitudeDb (shp, kSr, fq);

                            if (prev - v > worstBulge)
                            {
                                worstBulge = prev - v;
                                where = "depth " + juce::String (s.lowDepthPercent, 0)
                                      + " curve " + juce::String (s.lowCurvePercent, 0)
                                      + " shoulder " + juce::String (s.lowShoulderPercent, 0);
                            }
                            worstOver = juce::jmax (worstOver, v);
                            prev = v;
                        }
                    }

            check (worstBulge < 0.01 && worstOver < 0.001,
                   "monotonic and never above 0 dB with Shoulder in play",
                   "worst dip " + f (worstBulge, 6) + " dB, worst peak "
                       + f (worstOver, 6) + " dB   " + where);
        }

        //  Continuity of the control itself.
        {
            constexpr int kSteps = 2000;
            double worstStep = 0.0, prev = 0.0;

            for (int i = 0; i <= kSteps; ++i)
            {
                Settings s = neutral();
                s.lowFreqHz = 200.0f;
                s.lowDepthPercent = 80.0f;
                s.lowShoulderPercent = 100.0f * (float) i / (float) kSteps;
                const auto shp = shapeFor (shapeEngine(), s);

                const double v = edge::magnitudeDb (shp, kSr, 600.0);
                if (i > 0)
                    worstStep = juce::jmax (worstStep, std::abs (v - prev));
                prev = v;
            }

            check (worstStep < 0.05, "Shoulder 0->100 % in 2000 steps: largest step",
                   f (worstStep, 5) + " dB");
        }

        //  And it must not click when automated hard.
        {
            e.reset();
            Settings s = neutral();
            s.lowFreqHz = 300.0f;
            s.lowDepthPercent = 90.0f;
            e.snapToSettings (s);

            const int total = (int) (kSr * 2.0);
            juce::AudioBuffer<float> buf (1, total);
            const double w = juce::MathConstants<double>::twoPi * 500.0 / kSr;
            for (int i = 0; i < total; ++i)
                buf.setSample (0, i, 0.5f * (float) std::sin (w * (double) i));

            const float sourceStep = 0.5f * (float) std::abs (std::sin (w));

            for (int pos = 0; pos < total; )
            {
                const int n = juce::jmin (64, total - pos);
                const double t = (double) pos / total;
                s.lowShoulderPercent = (float) (100.0 * (t < 0.5 ? t * 2.0 : (1.0 - t) * 2.0));
                e.setSettings (s);
                float* ptr[1] = { buf.getWritePointer (0) + pos };
                juce::AudioBuffer<float> view (ptr, 1, n);
                e.process (view);
                pos += n;
            }

            check (allFinite (buf) && maxStep (buf) < sourceStep * 2.0f,
                   "Shoulder swept 0->100->0 during playback: largest sample step",
                   f (maxStep (buf), 6) + " vs source " + f (sourceStep, 6));
        }
    }

    void testRealtimeSafety()
    {
        section ("13. Real-time safety");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        Settings s = neutral();
        s.lowFreqHz = 300.0f; s.lowDepthPercent = 65.0f; s.lowResPercent = 40.0f;
        s.highFreqHz = 5000.0f; s.highDepthPercent = 55.0f; s.highResPercent = 30.0f;
        e.snapToSettings (s);

        juce::AudioBuffer<float> buf (2, 512);
        juce::Random rng (11);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample (c, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));

        //  Warm everything up first so lazily-built state is not counted.
        for (int i = 0; i < 8; ++i) { e.setSettings (s); e.process (buf); }

        gAllocations = 0;
        gCountAllocations = true;

        for (int i = 0; i < 400; ++i)
        {
            s.lowFreqHz = 100.0f + 50.0f * (float) (i % 20);
            s.highDepthPercent = 40.0f + (float) (i % 30);
            e.setSettings (s);
            e.process (buf);
        }

        gCountAllocations = false;
        const int allocs = gAllocations.load();
        check (allocs == 0, "no heap allocation in 400 audio blocks",
               juce::String (allocs) + " allocations");

        //  Throughput.
        const int blocks = 20000;
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int i = 0; i < blocks; ++i)
        {
            e.setSettings (s);
            e.process (buf);
        }
        const double secs = juce::Time::highResolutionTicksToSeconds (
            juce::Time::getHighResolutionTicks() - t0);

        const double audioSecs = (double) blocks * 512.0 / kSr;
        check (secs < audioSecs * 0.05, "CPU: stereo real-time factor",
               f (audioSecs / secs, 0) + "x realtime ("
                   + f (100.0 * secs / audioSecs, 2) + " % of one core)");
    }
}

int main()
{
    std::printf ("EDGE test suite\n");

    testNeutral();
    testAnalyticVsMeasured();
    testDepth();
    testMonotonic();
    testCurve();
    testResonance();
    testAutomation();
    testFocusAndLink();
    testSignalHygiene();
    testStereoAndBlocks();
    testColour();
    testBypassAndOutput();
    testColourPlacement();
    testImpulseAndNoise();
    testParameterState();
    testShoulder();
    testRealtimeSafety();

    std::printf ("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
