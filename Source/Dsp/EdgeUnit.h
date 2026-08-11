// One EDGE: three MorphSections in series at a shared corner frequency, plus a
// fourth, much gentler one for the Shoulder.
//
// Curve decides how the total Depth (in dB) is SHARED between the three:
//
//     P    = continuous pole-pair count in [1, 3]
//     w_i  = smoothstep(clamp(P - i, 0, 1)) / sum, so sum(w_i) = 1
//     G_i  = 10^(w_i * depthDb / 20)
//
// w_i = 0 gives G_i = 1, and a MorphSection at G = 1 is a bit-exact wire. So a
// section leaves the cascade by becoming an identity, never by being switched
// out, and it keeps running so its state stays warm for when it comes back.
// That property is what makes MODE switching (an edge becoming an identity),
// the EDGE macro at 0, and Depth 0 all click-free by construction.
//
// Curve's soft half widens the knee (damping k from 3.6 down to sqrt(2)) and
// its tight half adds order (P from 1 to 3). The two halves meet at P = 1,
// k = sqrt(2), so Curve is continuous and never crosses k < sqrt(2) - which
// keeps the monotonicity proof in MorphSvf.h valid across its whole travel.
//
// Resonance lowers k on SECTION 0 ONLY, so the peak height cannot compound
// with Curve's pole count.
//
// EVERYTHING IS PER CHANNEL. setShape takes a channel index because SPREAD
// gives the two channels different corner frequencies.

#pragma once

#include <array>

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Core/ParameterIds.h"
#include "MorphSvf.h"

namespace edge
{
    template <Side side>
    class EdgeUnit
    {
    public:
        static constexpr int maxChannels = MorphSection::maxChannels;

        void reset() noexcept
        {
            for (auto& s : sections)
                s.reset();

            shoulder.reset();
        }

        //  Curve -> (pole-pair count, damping). Public so the tests and the
        //  editor can reason about the same mapping the audio path uses.
        static void curveToShape (float curve01, float& poles, float& damping) noexcept
        {
            const float c = juce::jlimit (0.0f, 1.0f, curve01);

            if (c <= 0.5f)
            {
                poles   = 1.0f;
                damping = kCurveSoftDamping
                        + (kCurveNeutralDamping - kCurveSoftDamping) * (c * 2.0f);
            }
            else
            {
                //  1 -> 6 pole pairs across the tight half: 12, 24, 36, 48, 60
                //  and 72 dB/oct all land on it.
                poles   = 1.0f + 10.0f * (c - 0.5f);
                damping = kCurveNeutralDamping;
            }
        }

        static std::array<float, kNumSections> sectionWeights (float poles) noexcept
        {
            const float p = juce::jlimit (1.0f, (float) kNumSections, poles);
            std::array<float, kNumSections> w {};
            float sum = 0.0f;

            for (int i = 0; i < kNumSections; ++i)
            {
                //  Smoothstep, not a linear ramp. A linear clamp(P-i,0,1) has a
                //  kink in its derivative at every integer P, and at full Depth
                //  that kink is enormous: the section's share of -120 dB starts
                //  moving at 120 dB per unit P the instant P leaves 1, which
                //  measured as a 1.8 dB jump for a 0.5 % nudge of Curve.
                const float t = juce::jlimit (0.0f, 1.0f, p - (float) i);
                w[(size_t) i] = t * t * (3.0f - 2.0f * t);
                sum += w[(size_t) i];
            }

            for (auto& v : w)
                v /= sum;

            return w;
        }

        //  Coefficient work only - no allocation, safe on the audio thread.
        //  Called once per automation chunk (<= 32 samples), per channel.
        void setShape (int channel, double sampleRate, float freqHz, float depthDb,
                       float curve01, float res01, float shoulderDb,
                       int rampSamples = 0) noexcept
        {
            float poles = 1.0f, damping = kCurveNeutralDamping;
            curveToShape (curve01, poles, damping);

            const auto w = sectionWeights (poles);
            const float g = fourcolor::dsp::svfG (sampleRate, freqHz);
            const float res = juce::jlimit (0.0f, 1.0f, res01);

            //  Section 0 carries the resonance.
            const float k0 = damping + (kResonanceMinDamping - damping) * res;

            for (int i = 0; i < kNumSections; ++i)
            {
                //  Splitting in dB rather than calling pow() on the linear gain
                //  keeps the deep end well conditioned.
                const float sectionDb = w[(size_t) i] * depthDb;
                const float gain = w[(size_t) i] <= 0.0f
                                     ? 1.0f     // exact identity, not 10^0
                                     : juce::Decibels::decibelsToGain (sectionDb, kDepthFloorDb * 2.0f);

                sections[(size_t) i].setShape (channel, g, i == 0 ? k0 : damping, gain,
                                               rampSamples);
            }

            //  SHOULDER: the same section type, one shelf, six octaves into the
            //  passband with a deliberately wide knee. At 0 dB it is a bit-exact
            //  wire, and because it is another `side` shelf with k > sqrt(2) the
            //  cascade stays monotonic by the same proof.
            const float shoulderHz = side == Side::low
                ? freqHz * std::exp2 (kShoulderOctaves)
                : freqHz * std::exp2 (-kShoulderOctaves);

            auto& c = ch[channel];
            c.shoulderHz = juce::jlimit (10.0f, (float) sampleRate * 0.45f, shoulderHz);
            c.freqHz = freqHz;

            shoulder.setShape (channel, fourcolor::dsp::svfG (sampleRate, c.shoulderHz),
                               kShoulderDamping,
                               juce::Decibels::decibelsToGain (shoulderDb, kDepthFloorDb * 2.0f),
                               rampSamples);

            //  What a resonance make-up should be scaled by: 0 when section 0 is
            //  a wire, 1 when it is a full cut.
            c.resonanceActivity = res * (1.0f - sections[0].shelfGain (channel));
        }

        void processChunk (float* const* data, int numChannels, int numSamples) noexcept
        {
            for (int c = 0; c < numChannels; ++c)
            {
                auto* d = data[c];

                for (int i = 0; i < numSamples; ++i)
                {
                    //  Shoulder first, then the cut cascade. The order is
                    //  arbitrary - every section here is linear - but keeping
                    //  the gentle one first matches how the curve reads.
                    float x = shoulder.template processSample<side> (d[i], c);

                    for (auto& s : sections)
                        x = s.template processSample<side> (x, c);

                    d[i] = x;
                }
            }
        }

        //  Exact linear response of the whole unit at freqHz, from the live
        //  coefficients of one channel.
        std::complex<double> responseAt (double freqHz, double sampleRate,
                                         int channel = 0) const noexcept
        {
            const auto& c = ch[channel];
            const double limited = std::fmin (freqHz, sampleRate * 0.4999);
            const double warped = std::tan (juce::MathConstants<double>::pi * limited / sampleRate);

            const double g = (double) fourcolor::dsp::svfG (sampleRate, c.freqHz);
            const double omega = warped / g;

            std::complex<double> h { 1.0, 0.0 };

            for (const auto& s : sections)
                h *= s.template responseAt<side> (omega, channel);

            //  The shoulder sits at its own corner, so it needs its own omega.
            const double gS = (double) fourcolor::dsp::svfG (sampleRate, c.shoulderHz);
            return h * shoulder.template responseAt<side> (warped / gS, channel);
        }

        //  0 at Depth 0, -> 1 as the edge approaches a full cut. Drives the
        //  hidden colour law. Shoulder counts: leaning 12 dB off the passband
        //  IS the filter working.
        float activity (int channel = 0) const noexcept
        {
            float g = shoulder.shelfGain (channel);
            for (const auto& s : sections)
                g *= s.shelfGain (channel);

            return juce::jlimit (0.0f, 1.0f, 1.0f - std::sqrt (g));
        }

        float resonanceMakeup (int channel = 0) const noexcept
        {
            return ch[channel].resonanceActivity;
        }

    private:
        std::array<MorphSection, (size_t) kNumSections> sections;
        MorphSection shoulder;

        struct Ch
        {
            float freqHz = 1000.0f;
            float shoulderHz = 8000.0f;
            float resonanceActivity = 0.0f;
        };

        Ch ch[maxChannels];
    };
}
