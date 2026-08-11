// FOLLOW's detector: one stereo-linked envelope follower.
//
// It is the only thing in EDGE that looks at the signal, and it is deliberately
// as dumb as an envelope follower can be: a mono sum, one attack/release pole,
// and a fixed dB window. It produces a 0..1 modulation value and NOTHING else -
// it never touches gain, never compensates, never adapts a drive.
//
// STEREO-LINKED, not per channel, because a per-channel follower moves the two
// channels' cutoffs by different amounts on the same material, and the image
// wanders. SPREAD is the only thing allowed to separate the channels.

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include "../Core/ParameterIds.h"

namespace edge
{
    class FollowDetector
    {
    public:
        void prepare (double sampleRate) noexcept
        {
            rate = sampleRate;
            setTimes (attackMs, releaseMs);
            reset();
        }

        void reset() noexcept { env = 0.0f; }

        void setTimes (float newAttackMs, float newReleaseMs) noexcept
        {
            attackMs  = juce::jmax (0.05f, newAttackMs);
            releaseMs = juce::jmax (1.0f, newReleaseMs);

            attackCoeff  = 1.0f - std::exp (-1.0f / (attackMs  * 0.001f * (float) rate));
            releaseCoeff = 1.0f - std::exp (-1.0f / (releaseMs * 0.001f * (float) rate));
        }

        //  Sensitivity is the input level, in dBFS, at which the follower
        //  reaches full modulation; kFollowRangeDb below it, it reaches zero.
        void setSensitivity (float dbFS) noexcept { sensDb = dbFS; }

        //  Consumes a block and returns the modulation value at its end. The
        //  detector must see every sample, but the coefficients it drives are
        //  recomputed per automation chunk, so one value per chunk is what the
        //  caller needs back.
        float processBlock (const float* const* data, int numChannels, int numSamples) noexcept
        {
            const float* left  = data[0];
            const float* right = numChannels > 1 ? data[1] : data[0];

            for (int i = 0; i < numSamples; ++i)
            {
                //  Mono sum of magnitudes: stereo-linked by construction.
                const float mono = 0.5f * (std::abs (left[i]) + std::abs (right[i]));
                const float coeff = mono > env ? attackCoeff : releaseCoeff;

                env += coeff * (mono - env);
            }

            //  Silence must reach exactly zero rather than crawl down through
            //  the denormal range for minutes.
            if (env < 1.0e-20f)
                env = 0.0f;

            return normalise (env);
        }

        //  0..1. Public so a test can assert the mapping without running audio.
        float normalise (float linear) const noexcept
        {
            if (linear <= 0.0f)
                return 0.0f;

            const float db = juce::Decibels::gainToDecibels (linear, -120.0f);
            return juce::jlimit (0.0f, 1.0f,
                                 (db - (sensDb - kFollowRangeDb)) / kFollowRangeDb);
        }

        float getEnvelope() const noexcept { return env; }

    private:
        double rate = 48000.0;
        float env = 0.0f;
        float sensDb = -12.0f;
        float attackMs = 10.0f, releaseMs = 150.0f;
        float attackCoeff = 0.1f, releaseCoeff = 0.01f;
    };

    //  How FOLLOW's bipolar amount is applied to the base EDGE position.
    //
    //  The obvious form, clamp(base + amount*env, 0, 1), is not perceptually
    //  balanced: at base = 0.8 a positive FOLLOW has 0.2 of travel and a
    //  negative one has 0.8, so the same knob position does wildly different
    //  amounts of work depending on where EDGE happens to sit. Scaling by the
    //  headroom in the direction of travel makes full FOLLOW always reach
    //  exactly the boundary, from any base, in either direction - and it can
    //  never need clamping.
    inline float applyFollow (float baseEdge01, float amount, float env01) noexcept
    {
        const float base = juce::jlimit (0.0f, 1.0f, baseEdge01);
        const float a = juce::jlimit (-1.0f, 1.0f, amount);
        const float e = juce::jlimit (0.0f, 1.0f, env01);

        //  a == 0 returns base bit-exactly, which is what makes "FOLLOW 0 is
        //  identical to the follower being disabled" true rather than nearly
        //  true.
        if (a == 0.0f)
            return base;

        const float headroom = a > 0.0f ? (1.0f - base) : base;
        return juce::jlimit (0.0f, 1.0f, base + a * e * headroom);
    }
}
