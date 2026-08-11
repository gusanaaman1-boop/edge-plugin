// EDGE's core filter section: a TPT/ZDF state-variable section whose three
// outputs are recombined so that the SAME structure is a flat wire, a shelf of
// any depth, and a clean second-order cut, with nothing switched in between.
//
//     low + k*band + high == x            (exactly, by TptSvf's construction)
//
//     LOW  side:  y = G*low  + b*(k*band) + high   = x + (G-1)*low  + (b-1)*k*band
//     HIGH side:  y = low    + b*(k*band) + G*high = x + (G-1)*high + (b-1)*k*band
//
// with b = sqrt(G). Three consequences carry most of the product:
//
//   G = 1  ->  b = 1  ->  y = x + 0*low + 0*band  ->  BIT-EXACT pass-through.
//              Depth 0, EDGE 0 and an inactive filter MODE all reduce to this,
//              so none of them needs a bypass branch or a crossfade.
//   G = 0  ->  b = 0  ->  y = high (or low): a textbook 2nd-order cut.
//   b^2 = G and k >= sqrt(2)  ->  |H| is provably monotonic in frequency for
//              every G. Proof in docs/DSP-TOPOLOGY.md section 2.
//
// COEFFICIENTS ARE PER CHANNEL as of v0.2. They used to be shared, with only
// the state per channel; SPREAD moves the two channels' corner frequencies
// apart, so each channel now needs its own g, k, G and b. Nothing is shared
// between channels - no state, no coefficients, no feedback.

#pragma once

#include <cmath>
#include <complex>

#include "../Vendor/FourColor/Dsp/TptFilters.h"

namespace edge
{
    enum class Side { low, high };

    class MorphSection
    {
    public:
        static constexpr int maxChannels = 2;

        void reset() noexcept
        {
            for (auto& c : ch)
            {
                c.svf.reset();
                c.gm1 = c.targetGm1;
                c.bm1 = c.targetBm1;
                c.gm1Step = c.bm1Step = 0.0f;
                c.rampLeft = 0;
            }
        }

        //  g = tan(pi*fc/fs), damping k = 1/Q, shelfGain G in [0, 1].
        //
        //  `rampSamples` interpolates the two GAIN multipliers across the block
        //  instead of stepping them at its start. The cutoff and the damping do
        //  not need this - a TPT section's state is the physical state of the
        //  analogue prototype, so it absorbs a coefficient change smoothly -
        //  but the gains are multipliers on the output:
        //
        //      y = x + (G-1)*low + (b-1)*k*band
        //
        //  so a step in G is a step in y proportional to `low`. Updating them
        //  once per 32-sample chunk measured as -43 dBFS of discontinuity when
        //  MODE drove an edge from a full cut to an identity in 30 ms; the
        //  requirement is -80. Two extra multiply-adds per sample buys it.
        //
        //  When current and target are equal the step is exactly 0 and the
        //  accumulator never moves, so the bit-exact identity at G = 1 survives.
        void setShape (int channel, float g, float damping, float shelfGain,
                       int rampSamples = 0) noexcept
        {
            auto& c = ch[channel];
            c.k = damping;
            c.svf.setCoefficients (g, damping);

            c.targetGm1 = shelfGain - 1.0f;
            c.targetBm1 = std::sqrt (shelfGain) - 1.0f;

            if (rampSamples > 0)
            {
                const float inv = 1.0f / (float) rampSamples;
                c.gm1Step = (c.targetGm1 - c.gm1) * inv;
                c.bm1Step = (c.targetBm1 - c.bm1) * inv;
                c.rampLeft = rampSamples;
            }
            else
            {
                c.gm1 = c.targetGm1;
                c.bm1 = c.targetBm1;
                c.gm1Step = c.bm1Step = 0.0f;
                c.rampLeft = 0;
            }
        }

        template <Side side>
        float processSample (float x, int channel) noexcept
        {
            auto& c = ch[channel];

            if (c.rampLeft > 0)
            {
                c.gm1 += c.gm1Step;
                c.bm1 += c.bm1Step;

                if (--c.rampLeft == 0)
                {
                    c.gm1 = c.targetGm1;
                    c.bm1 = c.targetBm1;
                }
            }

            const auto o = c.svf.process (x);
            const float shelved = (side == Side::low) ? o.low : o.high;

            //  When gm1 and bm1 are both exactly 0 this returns x unchanged.
            return x + c.gm1 * shelved + c.bm1 * (c.k * o.band);
        }

        //  Exact response of the DISCRETE filter, not of an analogue model: the
        //  TPT section is the bilinear transform of the normalised prototype,
        //  and the warping is captured exactly by omega = tan(pi f/fs)/g. This
        //  is what the GUI curve is drawn from, so the drawing cannot drift
        //  away from the audio.
        template <Side side>
        std::complex<double> responseAt (double omega, int channel) const noexcept
        {
            const auto& c = ch[channel];
            const std::complex<double> s { 0.0, omega };
            const std::complex<double> den = s * s + (double) c.k * s + 1.0;

            const double G = (double) c.gm1 + 1.0;
            const double b = (double) c.bm1 + 1.0;

            const std::complex<double> num = (side == Side::low)
                ? (G + b * (double) c.k * s + s * s)
                : (1.0 + b * (double) c.k * s + G * s * s);

            return num / den;
        }

        float damping (int channel) const noexcept { return ch[channel].k; }

        //  The TARGET, not the ramping value: the colour law and the resonance
        //  make-up describe where the filter is going, and they are recomputed
        //  once per chunk anyway.
        float shelfGain (int channel) const noexcept { return ch[channel].targetGm1 + 1.0f; }

    private:
        struct Ch
        {
            fourcolor::dsp::TptSvf svf;
            float k = 1.41421356f;
            float gm1 = 0.0f;        // G - 1, the value in use right now
            float bm1 = 0.0f;        // sqrt(G) - 1
            float targetGm1 = 0.0f;
            float targetBm1 = 0.0f;
            float gm1Step = 0.0f, bm1Step = 0.0f;
            int rampLeft = 0;
        };

        Ch ch[maxChannels];
    };

    //  The MID band: a peaking bell on the same TPT section.
    //
    //      low + k*band + high == x
    //      y = low + G*(k*band) + high  =  x + (G-1)*(k*band)
    //
    //  which is  H(s) = (s^2 + G*k*s + 1) / (s^2 + k*s + 1)  -  unity at DC and
    //  at Nyquist, exactly G at the corner. One multiply-add on top of the SVF.
    //
    //  G = 1 gives y = x + 0*(k*band), bit-exact, for the same reason every
    //  other section in EDGE is a wire at unity: that is what lets the MID band
    //  exist at all without costing anything when its gain is 0 dB, and what
    //  keeps EDGE 0 bit-exact with a MID target set.
    //
    //  A bell is deliberately NOT monotonic. It is the one thing in the
    //  plug-in allowed above 0 dB, and the shape tests know it.
    class BellSection
    {
    public:
        static constexpr int maxChannels = MorphSection::maxChannels;

        void reset() noexcept
        {
            for (auto& c : ch)
            {
                c.svf.reset();
                c.gm1 = c.targetGm1;
                c.gm1Step = 0.0f;
                c.rampLeft = 0;
            }
        }

        void setShape (int channel, float g, float damping, float peakGain,
                       int rampSamples = 0) noexcept
        {
            auto& c = ch[channel];
            c.k = damping;
            c.svf.setCoefficients (g, damping);
            c.targetGm1 = peakGain - 1.0f;

            if (rampSamples > 0)
            {
                c.gm1Step = (c.targetGm1 - c.gm1) / (float) rampSamples;
                c.rampLeft = rampSamples;
            }
            else
            {
                c.gm1 = c.targetGm1;
                c.gm1Step = 0.0f;
                c.rampLeft = 0;
            }
        }

        float processSample (float x, int channel) noexcept
        {
            auto& c = ch[channel];

            if (c.rampLeft > 0)
            {
                c.gm1 += c.gm1Step;
                if (--c.rampLeft == 0)
                    c.gm1 = c.targetGm1;
            }

            const auto o = c.svf.process (x);
            return x + c.gm1 * (c.k * o.band);
        }

        std::complex<double> responseAt (double omega, int channel) const noexcept
        {
            const auto& c = ch[channel];
            const std::complex<double> s { 0.0, omega };
            const double G = (double) c.gm1 + 1.0;

            return (s * s + G * (double) c.k * s + 1.0)
                 / (s * s + (double) c.k * s + 1.0);
        }

        float peakGain (int channel) const noexcept { return ch[channel].targetGm1 + 1.0f; }

    private:
        struct Ch
        {
            fourcolor::dsp::TptSvf svf;
            float k = 1.0f;
            float gm1 = 0.0f;
            float targetGm1 = 0.0f;
            float gm1Step = 0.0f;
            int rampLeft = 0;
        };

        Ch ch[maxChannels];
    };
}
