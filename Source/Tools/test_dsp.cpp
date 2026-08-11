// EDGE measurement suite.
//
//     build/EdgeTests_artefacts/<config>/EdgeTests
//
// Exit code 0 = every check passed. Every check prints the value it measured,
// so a failure is diagnosable without attaching a debugger. Nothing here
// asserts a number that was not actually measured by the code above it.

#include <cstdio>
#include <cmath>
#include <complex>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "../Core/ParameterIds.h"
#include "../Core/Parameters.h"
#include "../Core/StateMigration.h"
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

        std::printf ("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", label,
                     measured.toRawUTF8());
    }

    void section (const char* name) { std::printf ("\n== %s ==\n", name); }

    juce::String f (double v, int dp = 3) { return juce::String (v, dp); }

    constexpr double kSr = 48000.0;

    using Settings = edge::EdgeEngine::Settings;

    //  Defaults, EDGE closed: the plug-in's actual startup state.
    Settings neutral() { return Settings {}; }

    //  A band that is fully open, which is what most of these checks want.
    Settings openBand()
    {
        Settings s;
        s.edgePercent = 100.0f;
        return s;
    }

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

    //  Measured magnitude response: drive a sine and recover ONLY the amplitude
    //  at that frequency, with a Hann-windowed single-bin DFT.
    //
    //  Broadband RMS was the first attempt and it was wrong: the hidden colour
    //  engine is a nonlinearity, so at a deep cut the RMS in the band is
    //  dominated by harmonics the filter attenuates far less than the
    //  fundamental. A "-66 dB cut" measured that way was really -104 dB of
    //  fundamental sitting under its own distortion products.
    //
    //  The default amplitude is -18 dBFS, the level the colour engine's static
    //  make-up is calibrated at.
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

    edge::EdgeEngine& shapeEngine()
    {
        static edge::EdgeEngine e;
        static bool prepared = false;

        if (! prepared) { e.prepare (kSr, 512, 2); prepared = true; }
        return e;
    }

    //  The shape the EDITOR would draw, read back from a real engine.
    edge::EdgeShape shapeFor (edge::EdgeEngine& e, const Settings& s)
    {
        e.reset();
        e.snapToSettings (s);
        return e.getDisplayShape();
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

    //  Largest sample-to-sample jump, the standard proxy for a click.
    float maxStep (const juce::AudioBuffer<float>& b)
    {
        float m = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer (c);
            for (int i = 1; i < b.getNumSamples(); ++i)
                m = juce::jmax (m, std::abs (d[i] - d[i - 1]));
        }
        return m;
    }

    //  Renders a steady tone while a callback moves the settings every block.
    juce::AudioBuffer<float> sweep (edge::EdgeEngine& e, Settings s, double toneHz,
                                    float amplitude, double seconds, int blockSize,
                                    const std::function<void (Settings&, double)>& move)
    {
        e.reset();
        e.snapToSettings (s);

        const int total = (int) (kSr * seconds);
        juce::AudioBuffer<float> buf (2, total);
        const double w = juce::MathConstants<double>::twoPi * toneHz / kSr;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < total; ++i)
                buf.setSample (c, i, amplitude * (float) std::sin (w * (double) i));

        for (int pos = 0; pos < total; )
        {
            const int n = juce::jmin (blockSize, total - pos);
            move (s, (double) pos / (double) total);
            e.setSettings (s);

            float* ptrs[2] = { buf.getWritePointer (0) + pos, buf.getWritePointer (1) + pos };
            juce::AudioBuffer<float> view (ptrs, 2, n);
            e.process (view);
            pos += n;
        }

        return buf;
    }
}

// =============================================================================
namespace
{
    void testNeutral()
    {
        section ("1. Neutral");

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

        check (worst == 0.0, "EDGE 0 is a BIT-EXACT pass-through",
               "max |out-in| = " + f (worst, 12));

        check (e.getLatencySamples() == 0.0f, "latency",
               f (e.getLatencySamples(), 1) + " samples, "
                   + juce::String (e.getOversamplingFactor()) + "x colour");

        check (e.getColourDrivePercent() == 0.0f,
               "colour drive is exactly 0 at EDGE 0",
               f (e.getColourDrivePercent()) + " %");

        //  ... and it must stay exact with BITE at maximum, which is the
        //  specified guarantee rather than "BITE happened to be low".
        {
            edge::EdgeEngine b;
            b.prepare (kSr, 512, 2);
            Settings s = neutral();
            s.bitePercent = 100.0f;
            b.snapToSettings (s);

            juce::AudioBuffer<float> o (2, 4096);
            juce::Random r2 (7);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 4096; ++i)
                    o.setSample (c, i, r2.nextFloat() * 2.0f - 1.0f);

            juce::AudioBuffer<float> ref (o);
            run (b, o, 128);

            double w2 = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 4096; ++i)
                    w2 = juce::jmax (w2, (double) std::abs (o.getSample (c, i) - ref.getSample (c, i)));

            check (w2 == 0.0 && b.getColourDrivePercent() == 0.0f,
                   "EDGE 0 with BITE 100 is still bit-exact",
                   "max |out-in| = " + f (w2, 12) + ", drive "
                       + f (b.getColourDrivePercent()) + " %");
        }
    }

    void testDrawnVsMeasured()
    {
        section ("2. Drawn curve == measured response");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        struct Case { const char* name; Settings s; };
        std::vector<Case> cases;

        {
            Settings s = openBand();
            s.lowFreqHz = 200.0f; s.lowDepthPercent = 40.0f;
            s.highDepthPercent = 0.0f;
            cases.push_back ({ "low -6 dB @200 Hz", s });
        }
        {
            Settings s = openBand();
            s.mode = (int) edge::Mode::highPass;
            s.lowFreqHz = 100.0f; s.lowCurvePercent = 100.0f;
            cases.push_back ({ "HP mode, CUT @100 Hz, 36 dB/oct", s });
        }
        {
            Settings s = openBand();
            s.mode = (int) edge::Mode::lowPass;
            s.highFreqHz = 4000.0f; s.highDepthPercent = 58.0f; s.highCurvePercent = 0.0f;
            cases.push_back ({ "LP mode, -12 dB @4 kHz, soft", s });
        }
        {
            Settings s = openBand();
            s.lowFreqHz = 300.0f; s.lowResPercent = 80.0f;
            s.highFreqHz = 8000.0f; s.highShoulderPercent = 60.0f;
            cases.push_back ({ "band + resonance + shoulder", s });
        }
        {
            Settings s = openBand();
            s.edgePercent = 45.0f;
            s.lowFreqHz = 400.0f; s.highFreqHz = 3000.0f;
            cases.push_back ({ "mid-travel: EDGE 45 %", s });
        }

        const double probes[] = { 30.0, 60.0, 120.0, 250.0, 500.0, 1000.0,
                                  2000.0, 5000.0, 10000.0 };

        for (auto& c : cases)
        {
            const auto sh = shapeFor (e, c.s);
            double worst = 0.0, at = 0.0;

            for (double fq : probes)
            {
                const double predicted = edge::magnitudeDb (sh, kSr, fq);
                const double measured  = measuredGainDb (e, c.s, kSr, fq);

                //  Below -80 dB the measurement is float32 noise, not filter.
                if (predicted < -80.0)
                    continue;

                const double err = std::abs (predicted - measured);
                if (err > worst) { worst = err; at = fq; }
            }

            check (worst < 0.15, c.name,
                   "worst |drawn-measured| = " + f (worst, 3) + " dB @ " + f (at, 0) + " Hz");
        }
    }

    void testModes()
    {
        section ("3. Filter modes");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        //  LP: the low edge must be a literal identity, so a signal far below
        //  the low target passes untouched even though that target is a CUT.
        {
            Settings s = openBand();
            s.mode = (int) edge::Mode::lowPass;
            s.lowFreqHz = 500.0f; s.highFreqHz = 5000.0f;
            //  BITE 0 isolates the filter. With the colour engaged its internal
            //  10 Hz DC blocker takes a real -0.46 dB off 30 Hz, which is the
            //  colour stage behaving as documented, not the mode failing.
            s.bitePercent = 0.0f;

            const double at30  = measuredGainDb (e, s, kSr, 30.0);
            const double at1k  = measuredGainDb (e, s, kSr, 1000.0);
            const double at15k = measuredGainDb (e, s, kSr, 15000.0);

            check (std::abs (at30) < 0.05 && std::abs (at1k) < 0.05 && at15k < -20.0,
                   "LP: low edge is an identity, high edge cuts",
                   "30 Hz " + f (at30, 2) + ", 1 kHz " + f (at1k, 2)
                       + ", 15 kHz " + f (at15k, 1) + " dB");
        }

        //  HP: the mirror image.
        {
            Settings s = openBand();
            s.mode = (int) edge::Mode::highPass;
            s.lowFreqHz = 500.0f; s.highFreqHz = 5000.0f;
            s.bitePercent = 0.0f;

            const double at30  = measuredGainDb (e, s, kSr, 30.0);
            const double at2k  = measuredGainDb (e, s, kSr, 2000.0);
            const double at15k = measuredGainDb (e, s, kSr, 15000.0);

            check (at30 < -20.0 && std::abs (at2k) < 0.05 && std::abs (at15k) < 0.05,
                   "HP: high edge is an identity, low edge cuts",
                   "30 Hz " + f (at30, 1) + ", 2 kHz " + f (at2k, 2)
                       + ", 15 kHz " + f (at15k, 2) + " dB");
        }

        //  BAND with factory defaults is a real band-pass the moment EDGE opens.
        {
            Settings s = openBand();
            const double at30  = measuredGainDb (e, s, kSr, 30.0);
            const double at1k  = measuredGainDb (e, s, kSr, 1000.0);
            const double at18k = measuredGainDb (e, s, kSr, 18000.0);

            check (at30 < -30.0 && std::abs (at1k) < 0.8 && at18k < -30.0,
                   "BAND defaults give a real band-pass at EDGE 100",
                   "30 Hz " + f (at30, 1) + ", 1 kHz " + f (at1k, 2)
                       + ", 18 kHz " + f (at18k, 1) + " dB");
        }

        //  Switching modes during playback must not step the output.
        {
            Settings s = openBand();
            s.lowFreqHz = 300.0f; s.highFreqHz = 4000.0f;

            const double w = juce::MathConstants<double>::twoPi * 700.0 / kSr;
            const float sourceStep = 0.5f * (float) std::abs (std::sin (w));

            auto buf = sweep (e, s, 700.0, 0.5f, 4.0, 64,
                              [] (Settings& t, double u)
                              {
                                  const int order[] = { (int) edge::Mode::band,
                                                        (int) edge::Mode::lowPass,
                                                        (int) edge::Mode::band,
                                                        (int) edge::Mode::highPass,
                                                        (int) edge::Mode::band,
                                                        (int) edge::Mode::lowPass,
                                                        (int) edge::Mode::highPass,
                                                        (int) edge::Mode::band };
                                  t.mode = order[juce::jlimit (0, 7, (int) (u * 8.0))];
                              });

            //  Anything the mode change adds shows up as a step larger than the
            //  tone's own. Expressed in dBFS, as the spec asks.
            const float excess = juce::jmax (0.0f, maxStep (buf) - sourceStep);
            const float excessDb = juce::Decibels::gainToDecibels (juce::jmax (1.0e-9f, excess));

            check (allFinite (buf) && excessDb < -80.0f,
                   "eight mode switches during playback: excess step",
                   f (excessDb, 1) + " dBFS  (step " + f (maxStep (buf), 8)
                       + " vs source " + f (sourceStep, 8) + ")");
        }
    }

    void testEdgeMacro()
    {
        section ("4. EDGE macro");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        //  Continuity over the whole travel, at several frequencies: the
        //  steepest part of the movement is not in the same place for the two
        //  edges.
        for (double probe : { 60.0, 300.0, 2000.0, 9000.0 })
        {
            constexpr int kSteps = 2000;
            double worstStep = 0.0, prev = 0.0, worstAt = 0.0;

            for (int i = 0; i <= kSteps; ++i)
            {
                Settings s = openBand();
                s.edgePercent = 100.0f * (float) i / (float) kSteps;
                s.lowFreqHz = 400.0f; s.highFreqHz = 4000.0f;
                s.lowResPercent = 40.0f; s.highShoulderPercent = 50.0f;

                const auto sh = shapeFor (shapeEngine(), s);
                const double v = edge::magnitudeDb (sh, kSr, probe);

                if (i > 0 && std::abs (v - prev) > worstStep)
                {
                    worstStep = std::abs (v - prev);
                    worstAt = s.edgePercent;
                }
                prev = v;
            }

            check (worstStep < 0.15,
                   (juce::String ("EDGE 0->100 in 2000 steps @ ")
                        + juce::String (probe, 0) + " Hz").toRawUTF8(),
                   "largest step " + f (worstStep, 4) + " dB at EDGE "
                       + f (worstAt, 1) + " %");
        }

        //  The closed end leaves the response flat.
        {
            Settings closed = openBand();
            closed.edgePercent = 0.0f;
            const double at30  = measuredGainDb (e, closed, kSr, 30.0);
            const double at10k = measuredGainDb (e, closed, kSr, 10000.0);

            check (std::abs (at30) < 0.01 && std::abs (at10k) < 0.01,
                   "EDGE 0 leaves the response flat",
                   "30 Hz " + f (at30, 4) + ", 10 kHz " + f (at10k, 4) + " dB");
        }

        //  Fast automation, the way a Cubase lane would drive it.
        {
            Settings s = openBand();
            s.lowFreqHz = 500.0f; s.highFreqHz = 4000.0f;
            s.lowResPercent = 60.0f; s.highResPercent = 50.0f;

            const double w = juce::MathConstants<double>::twoPi * 440.0 / kSr;
            const float sourceStep = 0.5f * (float) std::abs (std::sin (w));

            auto buf = sweep (e, s, 440.0, 0.5f, 3.0, 64,
                              [] (Settings& t, double u)
                              {
                                  const double phase = u * 6.0 * juce::MathConstants<double>::twoPi;
                                  t.edgePercent = (float) (50.0 * (1.0 - std::cos (phase)));
                              });

            check (allFinite (buf) && maxAbs (buf) < 1.0f && maxStep (buf) < sourceStep * 3.0f,
                   "six full EDGE sweeps in 3 s: no clicks, nothing non-finite",
                   "largest step " + f (maxStep (buf), 6) + " vs source "
                       + f (sourceStep, 6) + ", peak " + f (maxAbs (buf), 4));
        }
    }

    void testFollow()
    {
        section ("5. FOLLOW");

        check (edge::applyFollow (0.5f, 0.0f, 1.0f) == 0.5f,
               "FOLLOW 0 returns the base position bit-exactly",
               "applyFollow(0.5, 0, 1) = " + f (edge::applyFollow (0.5f, 0.0f, 1.0f), 9));

        {
            //  Perceptually balanced: full modulation reaches the boundary from
            //  ANY base, in either direction.
            juce::String detail;
            bool ok = true;

            for (float base : { 0.1f, 0.5f, 0.9f })
            {
                const float up = edge::applyFollow (base, 1.0f, 1.0f);
                const float dn = edge::applyFollow (base, -1.0f, 1.0f);
                ok = ok && std::abs (up - 1.0f) < 1.0e-6f && std::abs (dn) < 1.0e-6f;
                detail += "base " + juce::String (base, 1) + ": + -> " + juce::String (up, 3)
                        + ", - -> " + juce::String (dn, 3) + "   ";
            }

            check (ok, "full FOLLOW reaches the boundary from any base, both ways",
                   detail.trim());
        }

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        //  FOLLOW 0 must be bit-identical to the follower not existing. The
        //  detector still runs; it must not be able to touch anything.
        {
            auto render = [] (const Settings& s)
            {
                edge::EdgeEngine en;
                en.prepare (kSr, 512, 2);
                en.snapToSettings (s);

                juce::Random rng (4242);
                juce::AudioBuffer<float> b (2, 16384);
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < 16384; ++i)
                        b.setSample (c, i, 0.4f * (rng.nextFloat() * 2.0f - 1.0f));

                run (en, b, 128);
                return b;
            };

            Settings a = openBand();
            a.edgePercent = 60.0f;
            a.followPercent = 0.0f;
            a.lowFreqHz = 300.0f; a.highFreqHz = 5000.0f;

            //  The "disabled follower" reference is the same settings with the
            //  detector's own controls at extremes: if any of it leaked into
            //  the audio path, these would differ.
            Settings b = a;
            b.followSensDb = -60.0f;
            b.followAttackMs = 0.1f;
            b.followReleaseMs = 2000.0f;

            const auto x = render (a);
            const auto y = render (b);

            double worst = 0.0;
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 16384; ++i)
                    worst = juce::jmax (worst, (double) std::abs (x.getSample (c, i)
                                                                - y.getSample (c, i)));

            check (worst == 0.0, "at FOLLOW 0 the detector cannot affect the output",
                   "max difference = " + f (worst, 12));
        }

        //  It moves the filter, in the right direction, both ways.
        {
            auto levelBelowCorner = [&] (float followPercent)
            {
                Settings s = openBand();
                s.mode = (int) edge::Mode::highPass;
                s.edgePercent = 50.0f;
                s.lowFreqHz = 2000.0f;
                s.followPercent = followPercent;
                s.followAttackMs = 1.0f;
                s.followReleaseMs = 40.0f;

                //  A loud tone below the corner: positive FOLLOW pushes the
                //  high-pass up over it, negative pulls it away.
                return measuredGainDb (e, s, kSr, 150.0, 0.5f);
            };

            const double none = levelBelowCorner (0.0f);
            const double up   = levelBelowCorner (90.0f);
            const double down = levelBelowCorner (-90.0f);

            check (up < none - 3.0 && down > none + 1.0,
                   "positive FOLLOW cuts more, negative opens up",
                   "0 % " + f (none, 1) + " dB, +90 % " + f (up, 1)
                       + " dB, -90 % " + f (down, 1) + " dB");
        }

        //  Silence and full scale must both stay finite and safe.
        {
            Settings s = openBand();
            s.edgePercent = 50.0f;
            s.followPercent = 100.0f;

            edge::EdgeEngine q;
            q.prepare (kSr, 512, 2);
            q.snapToSettings (s);

            juce::AudioBuffer<float> silence (2, 48000);
            silence.clear();
            run (q, silence, 256);
            const bool silentOk = maxAbs (silence) == 0.0f;

            juce::AudioBuffer<float> hot (2, 48000);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < hot.getNumSamples(); ++i)
                    hot.setSample (c, i, i % 2 == 0 ? 1.0f : -1.0f);

            run (q, hot, 256);

            check (silentOk && allFinite (hot) && maxAbs (hot) < 4.0f,
                   "FOLLOW stays finite over silence and full scale",
                   "silence peak " + f (maxAbs (silence), 9) + ", full-scale peak "
                       + f (maxAbs (hot), 3));
        }

        //  And it must not click while it is working hard.
        {
            Settings s = openBand();
            s.edgePercent = 40.0f;
            s.followPercent = 100.0f;
            s.followAttackMs = 1.0f;
            s.followReleaseMs = 30.0f;
            s.lowFreqHz = 600.0f; s.highFreqHz = 5000.0f;

            edge::EdgeEngine q;
            q.prepare (kSr, 512, 2);
            q.snapToSettings (s);

            const int total = (int) (kSr * 3.0);
            juce::AudioBuffer<float> buf (2, total);
            const double w = juce::MathConstants<double>::twoPi * 220.0 / kSr;

            for (int i = 0; i < total; ++i)
            {
                const double env = 0.05 + 0.95 * (0.5 - 0.5 * std::cos (
                    juce::MathConstants<double>::twoPi * 4.0 * (double) i / kSr));
                const float v = 0.6f * (float) (env * std::sin (w * (double) i));
                buf.setSample (0, i, v);
                buf.setSample (1, i, v);
            }

            juce::AudioBuffer<float> src (buf);
            run (q, buf, 64);

            check (allFinite (buf) && maxStep (buf) < maxStep (src) * 3.0f,
                   "FOLLOW driven hard by a pulsing tone: no click",
                   "largest step " + f (maxStep (buf), 6) + " vs source "
                       + f (maxStep (src), 6));
        }
    }

    void testSpread()
    {
        section ("6. SPREAD");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        //  At 0 the two channels must be identical, which for identical input
        //  means a bit-exact null.
        {
            Settings s = openBand();
            s.edgePercent = 80.0f;
            s.spreadPercent = 0.0f;
            s.lowResPercent = 50.0f;

            e.reset();
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
                diff = juce::jmax (diff, (double) std::abs (buf.getSample (0, i)
                                                          - buf.getSample (1, i)));

            check (diff == 0.0, "SPREAD 0: L and R coefficients match exactly",
                   "max |L-R| = " + f (diff, 12));
        }

        //  With SPREAD the channels differ by a frequency offset, and the two
        //  corners move apart by the stated interval.
        {
            Settings s = openBand();
            s.mode = (int) edge::Mode::highPass;
            s.lowFreqHz = 1000.0f;
            s.spreadPercent = 100.0f;

            const auto left  = shapeFor (shapeEngine(), s);
            const auto right = shapeEngine().getDisplayShape (1);

            const double semis = 12.0 * std::log2 (right.lowHz / left.lowHz);

            check (std::abs (semis - edge::kSpreadMaxSemitones) < 0.05,
                   "SPREAD 100 %: total L-to-R separation",
                   f (semis, 3) + " semitones (target "
                       + f (edge::kSpreadMaxSemitones, 1) + ")");
        }

        //  BAND must move both boundaries of a channel together, so each
        //  channel keeps its bandwidth in octaves.
        {
            Settings s = openBand();
            s.lowFreqHz = 300.0f; s.highFreqHz = 4800.0f;
            s.spreadPercent = 80.0f;

            const auto left  = shapeFor (shapeEngine(), s);
            const auto right = shapeEngine().getDisplayShape (1);

            const double bwL = std::log2 (left.highHz / left.lowHz);
            const double bwR = std::log2 (right.highHz / right.lowHz);

            check (std::abs (bwL - bwR) < 0.002,
                   "BAND + SPREAD preserves each channel's bandwidth",
                   "L " + f (bwL, 4) + " oct, R " + f (bwR, 4) + " oct");
        }

        //  No inter-channel state sharing: a silent channel stays silent no
        //  matter what the other one is doing.
        {
            Settings s = openBand();
            s.edgePercent = 90.0f;
            s.spreadPercent = 70.0f;
            s.lowResPercent = 80.0f; s.highResPercent = 70.0f;
            s.bitePercent = 100.0f;

            edge::EdgeEngine q;
            q.prepare (kSr, 512, 2);
            q.snapToSettings (s);

            juce::AudioBuffer<float> buf (2, 32768);
            buf.clear();

            juce::Random rng (99);
            for (int i = 0; i < 32768; ++i)
                buf.setSample (0, i, rng.nextFloat() * 2.0f - 1.0f);   // R stays silent

            run (q, buf, 128);

            const float bleed = buf.getMagnitude (1, 0, 32768);
            check (bleed == 0.0f, "no crosstalk: a silent channel stays silent",
                   "R peak = " + f (bleed, 12));
        }
    }

    void testBite()
    {
        section ("7. BITE");

        check (edge::biteMaxDrive (0.0f) == 0.0f && edge::colourDrivePercent (0.0f, 1.0f) == 0.0f,
               "BITE 0 gives exactly zero drive at any activity",
               "maxDrive(0) = " + f (edge::biteMaxDrive (0.0f), 9));

        check (edge::colourDrivePercent (100.0f, 0.0f) == 0.0f,
               "zero activity gives exactly zero drive at any BITE",
               "drive(100 %, 0) = " + f (edge::colourDrivePercent (100.0f, 0.0f), 9));

        {
            bool monoBite = true, monoActivity = true;

            for (int a = 1; a <= 10; ++a)
            {
                const float act = 0.1f * (float) a;
                for (int b = 1; b <= 100; ++b)
                    monoBite = monoBite && edge::colourDrivePercent ((float) b, act)
                                         >= edge::colourDrivePercent ((float) (b - 1), act);
            }

            for (int b = 0; b <= 10; ++b)
            {
                const float bp = 10.0f * (float) b;
                for (int a = 1; a <= 100; ++a)
                    monoActivity = monoActivity
                        && edge::colourDrivePercent (bp, 0.01f * (float) a)
                             >= edge::colourDrivePercent (bp, 0.01f * (float) (a - 1));
            }

            check (monoBite && monoActivity,
                   "drive is monotonic in BITE and in activity", "ok");
        }

        //  The point of the rewrite: moderate shelf movement must produce
        //  colour. The vendored engine fades out below 5 % drive, so that is
        //  the bar - the v1 linear law needed activity 0.5 to clear it.
        {
            const float act = 0.30f;   // about a -4 dB shelf
            const float v1 = 10.0f * act;
            const float v2 = edge::colourDrivePercent (35.0f, act);

            check (v2 > 5.0f && v1 < 5.0f,
                   "at BITE 35 a -4 dB shelf clears the engine's engage window",
                   "new drive " + f (v2, 2) + " % vs old " + f (v1, 2)
                       + " % (engage needs 5 %)");
        }

        //  Full disengagement.
        {
            edge::EdgeEngine e;
            e.prepare (kSr, 512, 1);

            Settings s = openBand();
            s.lowFreqHz = 100.0f; s.highFreqHz = 12000.0f;
            s.bitePercent = 0.0f;

            const double at30 = measuredGainDb (e, s, kSr, 30.0);
            const auto sh = shapeFor (e, s);

            check (sh.colourEngage == 0.0f, "BITE 0 fully disengages WARM",
                   "engage = " + f (sh.colourEngage, 9) + ", 30 Hz " + f (at30, 2) + " dB");
        }

        //  Aliasing at the WORST case the plug-in can reach: BITE 100, full cut.
        {
            edge::EdgeEngine en;
            en.prepare (kSr, 1024, 1);

            Settings s = openBand();
            s.mode = (int) edge::Mode::highPass;
            s.lowFreqHz = 20.0f; s.lowCurvePercent = 0.0f;
            s.bitePercent = 100.0f;
            en.snapToSettings (s);

            constexpr int fftOrder = 15;
            constexpr int fftSize = 1 << fftOrder;

            //  Bin-centred fundamental and a Blackman-Harris window: a
            //  non-integer number of periods under a Hann window fakes an
            //  alias floor around -31 dB that is entirely sidelobe.
            const int bin = 683;
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

                if (k == bin)          fundamental = juce::jmax (fundamental, mag);
                else if (! isHarmonic) alias = juce::jmax (alias, mag);
            }

            const double aliasDb = 20.0 * std::log10 (
                juce::jmax (1.0e-12, alias / juce::jmax (1.0e-12, fundamental)));

            check (aliasDb <= -70.0, "aliasing at BITE 100, 1x, full cut",
                   f (aliasDb, 1) + " dBc  (max drive "
                       + f (edge::biteMaxDrive (100.0f), 1) + " %)");
        }
    }

    void testSignalHygiene()
    {
        section ("8. Signal hygiene");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        Settings hard = openBand();
        hard.lowFreqHz = 300.0f;   hard.lowResPercent = 100.0f;  hard.lowCurvePercent = 100.0f;
        hard.highFreqHz = 2000.0f; hard.highResPercent = 100.0f; hard.highCurvePercent = 100.0f;
        hard.lowShoulderPercent = 100.0f; hard.highShoulderPercent = 100.0f;
        hard.bitePercent = 100.0f;
        hard.followPercent = 80.0f;
        hard.spreadPercent = 100.0f;

        e.snapToSettings (hard);
        juce::AudioBuffer<float> buf (2, 8192);
        buf.clear();
        run (e, buf, 128);
        check (maxAbs (buf) == 0.0f, "silence in -> silence out (worst-case settings)",
               f (maxAbs (buf), 12));

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
        check (allFinite (buf) && dcTail < 1.0e-4f, "DC input is removed",
               juce::String (juce::Decibels::gainToDecibels (juce::jmax (1.0e-12f, dcTail)), 1)
                   + " dBFS");

        e.reset();
        e.snapToSettings (hard);
        juce::Random rng (99);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 8192; ++i)
                buf.setSample (c, i, 2.0f * (rng.nextFloat() * 2.0f - 1.0f));
        run (e, buf, 128);
        check (allFinite (buf) && maxAbs (buf) < 8.0f,
               "+6 dBFS noise stays finite and bounded", "peak " + f (maxAbs (buf), 3));

        //  Denormals in the tail.
        e.reset();
        Settings res = openBand();
        res.mode = (int) edge::Mode::highPass;
        res.lowFreqHz = 50.0f; res.lowResPercent = 90.0f;
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

    void testBlocksAndRates()
    {
        section ("9. Block sizes, sample rates, layouts");

        Settings s = openBand();
        s.edgePercent = 70.0f;
        s.lowFreqHz = 200.0f; s.lowResPercent = 50.0f;
        s.highFreqHz = 6000.0f; s.highShoulderPercent = 40.0f;

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

            check (worst < 1.0e-3, ("block size " + juce::String (bs)
                                    + " vs 512: worst deviation").toRawUTF8(),
                   f (worst, 9));
        }

        {
            edge::EdgeEngine big;
            big.prepare (kSr, 128, 2);
            big.snapToSettings (s);

            juce::AudioBuffer<float> b (2, 4096);
            juce::Random r3 (77);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 4096; ++i)
                    b.setSample (c, i, 0.3f * (r3.nextFloat() * 2.0f - 1.0f));

            run (big, b, 1024);
            check (allFinite (b) && maxAbs (b) < 1.0f,
                   "block larger than the announced size is handled",
                   "128 announced, 1024 delivered, peak " + f (maxAbs (b), 3));
        }

        {
            edge::EdgeEngine mono;
            mono.prepare (kSr, 512, 1);
            Settings m = s;
            m.spreadPercent = 100.0f;      // must be harmless with one channel
            mono.snapToSettings (m);
            juce::AudioBuffer<float> b (1, 4096);
            for (int i = 0; i < 4096; ++i)
                b.setSample (0, i, 0.3f * (float) std::sin (0.05 * i));
            run (mono, b, 64);
            check (allFinite (b), "mono layout processes correctly, SPREAD is inert", "ok");
        }

        juce::String rateLine;
        double refCorner = 0.0;
        bool ratesOk = true;

        for (double sr : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
        {
            edge::EdgeEngine en;
            en.prepare (sr, 512, 1);

            Settings t = openBand();
            t.mode = (int) edge::Mode::highPass;
            t.lowFreqHz = 1000.0f;
            const auto sh = shapeFor (en, t);

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

    void testBypassAndOutput()
    {
        section ("10. Bypass and Output");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 1);

        Settings s = openBand();
        s.lowFreqHz = 500.0f; s.lowResPercent = 60.0f;

        const double w = juce::MathConstants<double>::twoPi * 220.0 / kSr;
        const float sourceStep = 0.5f * (float) std::abs (std::sin (w));

        auto buf = sweep (e, s, 220.0, 0.5f, 1.0, 64,
                          [] (Settings& t, double u) { t.bypass = ((int) (u * 6.0)) % 2 == 1; });

        check (allFinite (buf) && maxStep (buf) < sourceStep * 2.0f,
               "six bypass toggles during playback: largest sample step",
               f (maxStep (buf), 6) + " vs source " + f (sourceStep, 6));

        {
            edge::EdgeEngine b;
            b.prepare (kSr, 512, 1);
            Settings bs = openBand();
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
                worst = juce::jmax (worst, (double) std::abs (out.getSample (0, i)
                                                            - in.getSample (0, i)));

            check (worst == 0.0, "bypass is a bit-exact dry path",
                   "max |out-in| = " + f (worst, 12));
        }

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

    void testParametersAndState()
    {
        section ("11. Parameters, state, migration");

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

        const char* ids[] = {
            edge::param::lowFreq, edge::param::lowDepth, edge::param::lowCurve,
            edge::param::lowShoulder, edge::param::lowReso,
            edge::param::highFreq, edge::param::highDepth, edge::param::highCurve,
            edge::param::highShoulder, edge::param::highReso,
            edge::param::mode, edge::param::edge, edge::param::follow,
            edge::param::spread, edge::param::bite, edge::param::output, edge::param::bypass,
            edge::param::followSens, edge::param::followAttack, edge::param::followRelease };

        check (apvts.processor.getParameters().size() == 20,
               "20 host parameters, exactly as specified",
               juce::String (apvts.processor.getParameters().size()));

        bool allPresent = true;
        for (auto* id : ids)
            allPresent = allPresent && apvts.getParameter (id) != nullptr;

        check (allPresent, "every documented parameter ID exists", "20 / 20");

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

        //  --- v1 -> v2 migration ---------------------------------------------
        {
            juce::ValueTree v1 { "EDGE" };
            auto put = [&v1] (const char* id, float value)
            {
                juce::ValueTree p { "PARAM" };
                p.setProperty ("id", id, nullptr);
                p.setProperty ("value", value, nullptr);
                v1.appendChild (p, nullptr);
            };

            put ("lowFreq", 180.0f);   put ("lowDepth", 78.0f);
            put ("lowCurve", 75.0f);   put ("lowRes", 40.0f);
            put ("lowShoulder", 55.0f);
            put ("highFreq", 9000.0f); put ("highDepth", 100.0f);
            put ("highCurve", 50.0f);  put ("highRes", 20.0f);
            put ("highShoulder", 30.0f);
            put ("focus", 40.0f);      put ("link", 1.0f);
            put ("output", -3.0f);     put ("bypass", 0.0f);

            check (edge::isLegacyState (v1), "a v0.1 tree is recognised as legacy", "yes");

            juce::ValueTree tree = v1.createCopy();
            const bool did = edge::migrateToCurrent (tree);

            auto read = [&tree] (const char* id) -> float
            {
                for (int i = 0; i < tree.getNumChildren(); ++i)
                    if (tree.getChild (i).getProperty ("id").toString() == id)
                        return (float) tree.getChild (i).getProperty ("value");
                return -12345.0f;
            };

            const bool carried = read (edge::param::lowFreq) == 180.0f
                              && read (edge::param::lowDepth) == 78.0f
                              && read (edge::param::lowShoulder) == 55.0f
                              && read (edge::param::highReso) == 20.0f
                              && read (edge::param::output) == -3.0f;

            const bool opened = read (edge::param::edge) == 100.0f
                             && read (edge::param::mode) == (float) (int) edge::Mode::band
                             && read (edge::param::follow) == 0.0f
                             && read (edge::param::spread) == 0.0f;

            check (did && carried && opened,
                   "v0.1 state migrates: targets kept, EDGE opened, BAND selected",
                   "edge " + f (read (edge::param::edge), 0) + " %, bite "
                       + f (read (edge::param::bite), 1) + " %, version "
                       + tree.getProperty ("stateVersion").toString());

            const float driveAtCut = edge::colourDrivePercent (edge::migratedBitePercent(), 1.0f);
            check (std::abs (driveAtCut - 10.0f) < 0.05f,
                   "migrated BITE reproduces v0.1's drive at a full cut",
                   f (driveAtCut, 3) + " % (v0.1 was 10.0 %)");

            juce::ValueTree again = tree.createCopy();
            check (! edge::migrateToCurrent (again), "migration is not applied twice", "ok");
        }

        auto* depth = apvts.getParameter (edge::param::lowDepth);
        depth->setValueNotifyingHost (depth->convertTo0to1 (65.0f));
        check (depth->getText (depth->getValue(), 24).contains ("-24"),
               "Depth displays dB of attenuation",
               "65 % reads \"" + depth->getText (depth->getValue(), 24) + "\"");

        auto* curveP = apvts.getParameter (edge::param::lowCurve);
        curveP->setValueNotifyingHost (curveP->convertTo0to1 (75.0f));
        check (curveP->getText (curveP->getValue(), 24) == "24 dB/oct",
               "Curve reads a real slope",
               "75 % reads \"" + curveP->getText (curveP->getValue(), 24) + "\"");
    }

    void testMonotonicAndShape()
    {
        section ("12. Response shape invariants");

        double worstBulge = 0.0, worstOver = -1000.0;
        juce::String where;

        for (int edgeStep = 0; edgeStep <= 4; ++edgeStep)
            for (int d = 0; d <= 4; ++d)
                for (int c = 0; c <= 3; ++c)
                    for (int sh = 0; sh <= 2; ++sh)
                    {
                        Settings s = openBand();
                        s.mode = (int) edge::Mode::highPass;
                        s.edgePercent = 25.0f * (float) edgeStep;
                        s.lowFreqHz = 400.0f;
                        s.lowDepthPercent = 25.0f * (float) d;
                        s.lowCurvePercent = 33.3f * (float) c;
                        s.lowShoulderPercent = 50.0f * (float) sh;
                        s.bitePercent = 0.0f;      // isolate the filter

                        const auto shape = shapeFor (shapeEngine(), s);

                        double prev = -1000.0;
                        for (int k = 0; k <= 400; ++k)
                        {
                            const double fq = 15.0 * std::pow (1300.0, (double) k / 400.0);
                            const double v = edge::magnitudeDb (shape, kSr, fq);

                            if (prev - v > worstBulge)
                            {
                                worstBulge = prev - v;
                                where = "edge " + juce::String (s.edgePercent, 0)
                                      + " depth " + juce::String (s.lowDepthPercent, 0)
                                      + " curve " + juce::String (s.lowCurvePercent, 0)
                                      + " shoulder " + juce::String (s.lowShoulderPercent, 0);
                            }
                            worstOver = juce::jmax (worstOver, v);
                            prev = v;
                        }
                    }

        check (worstBulge < 0.01 && worstOver < 0.01,
               "monotonic, and never above 0 dB, across the whole space",
               "worst dip " + f (worstBulge, 6) + " dB, worst peak "
                   + f (worstOver, 6) + " dB   " + where);

        {
            double worst = 0.0;
            juce::String detail;

            for (int i = 1; i < edge::kNumSlopeChoices; ++i)
            {
                Settings t = openBand();
                t.mode = (int) edge::Mode::highPass;
                t.lowFreqHz = 1000.0f;
                t.lowCurvePercent = edge::kSlopeChoices[i].curvePercent;
                t.bitePercent = 0.0f;
                const auto sh = shapeFor (shapeEngine(), t);

                //  Measure between the -20 dB and -50 dB crossings rather than
                //  at fixed frequencies: that window is always below the knee
                //  and always clear of the -90 dB floor, whatever the slope is.
                //  Fixed probes at Fc/2 and Fc/4 reported 36 dB/oct as 32.6
                //  once the floor moved, because Fc/4 had run into it.
                auto crossing = [&sh] (double targetDb)
                {
                    double lo = 20.0, hi = 1000.0;
                    for (int it = 0; it < 60; ++it)
                    {
                        const double mid = std::sqrt (lo * hi);
                        (edge::magnitudeDb (sh, kSr, mid) < targetDb ? lo : hi) = mid;
                    }
                    return std::sqrt (lo * hi);
                };

                const double f20 = crossing (-20.0);
                const double f50 = crossing (-50.0);
                const double slope = 30.0 / std::log2 (f20 / f50);
                const double want = juce::String (edge::kSlopeChoices[i].name).getDoubleValue();

                worst = juce::jmax (worst, std::abs (slope - want) / want);
                detail += juce::String (edge::kSlopeChoices[i].name) + "="
                        + juce::String (slope, 1) + "  ";
            }

            //  10 %, and the measured numbers are printed, because the names
            //  describe POLE COUNT - which is exactly what the cascade has, and
            //  what every filter on the market labels - while the slope you can
            //  measure in the -20..-50 dB window is shallower than the
            //  asymptote. For three cascaded Butterworth-2 sections at one
            //  corner, |H|^2 per section is (u^4 + G^2)/(1 + u^4), and solving
            //  that for the two crossings predicts 32.97 dB/oct. Measured 32.9.
            //  The filter is right; the asymptote simply is not reached until
            //  much further down, and no finite depth floor lets it be.
            check (worst < 0.10, "every slope in the combo delivers its stated dB/oct",
                   "worst error " + f (100.0 * worst, 1) + " %   [" + detail.trim()
                       + "]  (6-pole predicts 33.0)");
        }

        {
            Settings s = openBand();
            s.lowDepthPercent = 0.0f; s.highDepthPercent = 0.0f;
            s.lowResPercent = 100.0f; s.highResPercent = 100.0f;
            s.bitePercent = 0.0f;
            const auto sh = shapeFor (shapeEngine(), s);

            double worst = 0.0;
            for (int k = 0; k <= 300; ++k)
                worst = juce::jmax (worst, std::abs (edge::magnitudeDb (
                    sh, kSr, 20.0 * std::pow (1000.0, (double) k / 300.0))));

            check (worst < 1.0e-6, "Resonance 100 % at Depth 0 changes nothing",
                   "worst |dB| = " + f (worst, 9));
        }
    }

    void testRealtimeSafety()
    {
        section ("13. Real-time safety and CPU");

        edge::EdgeEngine e;
        e.prepare (kSr, 512, 2);

        Settings s = openBand();
        s.edgePercent = 60.0f;
        s.followPercent = 70.0f;
        s.spreadPercent = 55.0f;
        s.bitePercent = 45.0f;
        s.lowResPercent = 40.0f; s.highResPercent = 30.0f;
        s.lowShoulderPercent = 35.0f; s.highShoulderPercent = 45.0f;
        e.snapToSettings (s);

        juce::AudioBuffer<float> buf (2, 512);
        juce::Random rng (11);
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < 512; ++i)
                buf.setSample (c, i, 0.2f * (rng.nextFloat() * 2.0f - 1.0f));

        for (int i = 0; i < 8; ++i) { e.setSettings (s); e.process (buf); }

        auto countAllocations = [&] (bool analyzer)
        {
            e.setAnalyzerEnabled (analyzer);
            gAllocations = 0;
            gCountAllocations = true;

            for (int i = 0; i < 10000; ++i)
            {
                s.lowFreqHz = 100.0f + 50.0f * (float) (i % 20);
                s.edgePercent = (float) (i % 101);
                s.mode = (i / 500) % 3;
                e.setSettings (s);
                e.process (buf);
            }

            gCountAllocations = false;
            return gAllocations.load();
        };

        const int allocClosed = countAllocations (false);
        const int allocOpen   = countAllocations (true);

        check (allocClosed == 0 && allocOpen == 0,
               "no heap allocation in 10,000 blocks, analyser off and on",
               juce::String (allocClosed) + " off, " + juce::String (allocOpen) + " on");

        auto measureCpu = [&] (bool analyzer)
        {
            e.setAnalyzerEnabled (analyzer);
            e.setSettings (s);

            const int blocks = 20000;
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int i = 0; i < blocks; ++i)
            {
                e.setSettings (s);
                e.process (buf);
            }
            const double secs = juce::Time::highResolutionTicksToSeconds (
                juce::Time::getHighResolutionTicks() - t0);

            return ((double) blocks * 512.0 / kSr) / secs;
        };

        const double closed = measureCpu (false);
        const double open   = measureCpu (true);

        //  A sanitiser build runs the same code roughly 20x slower, so the
        //  throughput bar there measures the sanitiser, not the plug-in. The
        //  ratio between analyser-on and analyser-off below is still meaningful
        //  and is left at full strength.
       #if defined(__SANITIZE_ADDRESS__) \
           || (defined(__has_feature) && __has_feature(address_sanitizer))
        constexpr double kMinRealtime = 10.0;
       #else
        constexpr double kMinRealtime = 200.0;
       #endif

        check (open >= kMinRealtime, "CPU with the analyser feeding: real-time factor",
               f (open, 0) + "x realtime (" + f (100.0 / open, 2) + " % of one core, bar "
                   + f (kMinRealtime, 0) + "x)");

        check (open >= closed * 0.90,
               "analyser costs no more than 10 % of throughput",
               "closed " + f (closed, 0) + "x, open " + f (open, 0) + "x, delta "
                   + f (100.0 * (closed - open) / closed, 1) + " %");

        e.setAnalyzerEnabled (false);
    }
}

int main()
{
    std::printf ("EDGE test suite\n");

    testNeutral();
    testDrawnVsMeasured();
    testModes();
    testEdgeMacro();
    testFollow();
    testSpread();
    testBite();
    testSignalHygiene();
    testBlocksAndRates();
    testBypassAndOutput();
    testParametersAndState();
    testMonotonicAndShape();
    testRealtimeSafety();

    std::printf ("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
