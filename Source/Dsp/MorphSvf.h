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
//              Depth 0 needs no bypass branch, and an inactive section in the
//              cascade is an identity rather than something switched off.
//   G = 0  ->  b = 0  ->  y = high (or low): a textbook 2nd-order cut.
//   b^2 = G and k >= sqrt(2)  ->  |H| is provably monotonic in frequency for
//              every G. Proof in docs/DSP-TOPOLOGY.md section 2; the test suite
//              checks it numerically over the whole (Depth x Curve) plane.
//
// Coefficients are shared across channels; state is per channel. That is the
// seam a future Mid/Side mode plugs into without touching this file.

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
            for (auto& s : svf)
                s.reset();
        }

        //  g = tan(pi*fc/fs), damping k = 1/Q, shelfGain G in [0, 1].
        void setShape (float g, float damping, float shelfGain) noexcept
        {
            k = damping;
            gm1 = shelfGain - 1.0f;
            bm1 = std::sqrt (shelfGain) - 1.0f;

            for (auto& s : svf)
                s.setCoefficients (g, damping);
        }

        template <Side side>
        float processSample (float x, int channel) noexcept
        {
            const auto o = svf[channel].process (x);
            const float shelved = (side == Side::low) ? o.low : o.high;

            //  When gm1 and bm1 are both exactly 0 this returns x unchanged.
            return x + gm1 * shelved + bm1 * (k * o.band);
        }

        //  Exact response of the DISCRETE filter, not of an analogue model: the
        //  TPT section is the bilinear transform of the normalised prototype,
        //  and the warping is captured exactly by omega = tan(pi f/fs)/g. This
        //  is what the GUI curve is drawn from, so the drawing cannot drift
        //  away from the audio.
        template <Side side>
        std::complex<double> responseAt (double omega) const noexcept
        {
            const std::complex<double> s { 0.0, omega };
            const std::complex<double> den = s * s + (double) k * s + 1.0;

            const double G = (double) gm1 + 1.0;
            const double b = (double) bm1 + 1.0;

            const std::complex<double> num = (side == Side::low)
                ? (G + b * (double) k * s + s * s)
                : (1.0 + b * (double) k * s + G * s * s);

            return num / den;
        }

        float damping() const noexcept { return k; }
        float shelfGain() const noexcept { return gm1 + 1.0f; }

    private:
        fourcolor::dsp::TptSvf svf[maxChannels];
        float k = 1.41421356f;
        float gm1 = 0.0f;    // G - 1
        float bm1 = 0.0f;    // sqrt(G) - 1
    };
}
