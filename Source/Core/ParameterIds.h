// EDGE parameter identifiers and control-law constants.
//
// These strings are FROZEN once the first build ships: host automation lanes
// and saved projects reference them. New parameters may only be appended.

#pragma once

#include <juce_core/juce_core.h>

namespace edge
{
    namespace param
    {
        inline constexpr const char* lowFreq   = "lowFreq";
        inline constexpr const char* lowDepth  = "lowDepth";
        inline constexpr const char* lowCurve  = "lowCurve";
        inline constexpr const char* lowRes    = "lowRes";

        inline constexpr const char* highFreq  = "highFreq";
        inline constexpr const char* highDepth = "highDepth";
        inline constexpr const char* highCurve = "highCurve";
        inline constexpr const char* highRes   = "highRes";

        inline constexpr const char* link      = "link";
        inline constexpr const char* focus     = "focus";
        inline constexpr const char* output    = "output";
        inline constexpr const char* bypass    = "bypass";

        //  APPENDED after v0.1. The IDs above were already frozen, so these go
        //  at the end - existing projects load unchanged and simply get
        //  Shoulder at its default of 0, which is a bit-exact wire.
        inline constexpr const char* lowShoulder  = "lowShoulder";
        inline constexpr const char* highShoulder = "highShoulder";
    }

    //  Frequency travel. The two edges deliberately overlap in the mids: the
    //  useful gesture "cut everything below 1 kHz" and "cut everything above
    //  1 kHz" both have to be reachable.
    inline constexpr float kLowFreqMin  = 20.0f;
    inline constexpr float kLowFreqMax  = 8000.0f;
    inline constexpr float kHighFreqMin = 200.0f;
    inline constexpr float kHighFreqMax = 20000.0f;

    //  Depth's floor. Not -inf: see docs/DSP-TOPOLOGY.md section 3 - a literal
    //  zero makes the composite slope jump by 12 dB/oct the instant Curve gives
    //  a section a non-zero share of the depth, which is a discontinuity in
    //  CURVE at the top of DEPTH. -120 dB removes it and is 20 dB below a
    //  16-bit dither floor.
    inline constexpr float kDepthFloorDb = -120.0f;

    //  Damping (k = 1/Q) at the two ends of Curve's soft half. sqrt(2) is
    //  Butterworth and is the tightest knee a single section is allowed to
    //  reach: below it the section peaks, which is Resonance's job, and the
    //  monotonicity proof stops holding.
    inline constexpr float kCurveSoftDamping    = 3.6f;
    inline constexpr float kCurveNeutralDamping = 1.41421356f;

    //  Resonance's floor damping. Q ~ 2.9. Self-oscillation needs k = 0.
    inline constexpr float kResonanceMinDamping = 0.35f;

    //  Focus travel, in octaves at +/-100 %.
    inline constexpr float kFocusOctaves = 2.0f;

    //  Smallest ratio allowed between the two effective corner frequencies.
    //  Under a fifth of a semitone - invisible in use, but it stops the pair
    //  from collapsing to a zero-width passband.
    inline constexpr float kMinEdgeRatio = 1.05f;

    //  Colour law. drivePercent = kColorDriveMax * activity, into an engine
    //  whose full-scale pre-gain is 24 dB, so 10 % is 2.4 dB at the maximum.
    //
    //  Chosen by measurement, not taste. At 14 % the saturator's own
    //  compression made the level vary by 1.0 dB across a -60..-18 dBFS input
    //  range, which is a lot for something with no control and no display; at
    //  10 % it is 0.6 dB and the colour is still clearly there. The engine's
    //  engage window (drive < 5 %) means colour starts appearing at about half
    //  activity, i.e. around -10 dB of Depth - below that EDGE is linear.
    inline constexpr float kColorDriveMax = 10.0f;

    //  Static make-up applied for Resonance only, at full resonance and full
    //  depth. Gated by the section's own shelf gain, so it is exactly 0 dB
    //  whenever Depth is 0 - see EdgeEngine::resonanceTrimDb.
    inline constexpr float kResonanceTrimDb = 1.5f;

    inline constexpr int kNumSections = 3;

    //  SHOULDER. A second, much gentler shelf sitting deep INTO the passband
    //  from the cut's corner, so the whole side that passes leans down towards
    //  the corner instead of staying flat up to it. At 0 dB its section is an
    //  exact identity, so the control costs nothing when it is not in use.
    //
    //  SIX octaves, not three. Three was the first version and it only leaned
    //  the region immediately next to the corner; the point of the control is
    //  to pull down the whole passband - "0 to 9 kHz under a 9 kHz cut" - so
    //  that the lean travels with the cutoff when it is automated. Six octaves
    //  puts the shoulder's own corner at 12.8 kHz for a 200 Hz low cut, i.e.
    //  effectively across everything the filter is letting through.
    inline constexpr float kShoulderOctaves = 6.0f;
    inline constexpr float kShoulderDamping = 2.2f;
    inline constexpr float kShoulderMaxDb   = -12.0f;
}
