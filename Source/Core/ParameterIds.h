// EDGE parameter identifiers and control-law constants.
//
// STATE VERSION 2. The v1 IDs ("lowFreq", "focus", ...) are gone, replaced by
// namespaced semantic ones, because several of them would otherwise have kept
// their old name while changing meaning. Projects saved with v1 are migrated on
// load - see Core/StateMigration.h - rather than silently reset.

#pragma once

#include <juce_core/juce_core.h>

namespace edge
{
    inline constexpr int kStateVersion = 2;

    namespace param
    {
        // --- LOW target ------------------------------------------------------
        inline constexpr const char* lowFreq     = "low.freq";
        inline constexpr const char* lowDepth    = "low.depth";
        inline constexpr const char* lowCurve    = "low.curve";
        inline constexpr const char* lowShoulder = "low.shoulder";
        inline constexpr const char* lowReso     = "low.reso";

        // --- HIGH target -----------------------------------------------------
        inline constexpr const char* highFreq     = "high.freq";
        inline constexpr const char* highDepth    = "high.depth";
        inline constexpr const char* highCurve    = "high.curve";
        inline constexpr const char* highShoulder = "high.shoulder";
        inline constexpr const char* highReso     = "high.reso";

        // --- performance -----------------------------------------------------
        inline constexpr const char* mode   = "mode";
        inline constexpr const char* edge   = "edge";
        inline constexpr const char* follow = "follow";
        inline constexpr const char* spread = "spread";
        inline constexpr const char* bite   = "bite";
        inline constexpr const char* output = "output";
        inline constexpr const char* bypass = "bypass";

        // --- follow setup (inside SHAPE) -------------------------------------
        inline constexpr const char* followSens    = "follow.sens";
        inline constexpr const char* followAttack  = "follow.attack";
        inline constexpr const char* followRelease = "follow.release";

        //  Which hidden engine BITE is driving. Two characters, and only two -
        //  it is a voicing, not a model browser.
        inline constexpr const char* character = "character";
    }

    enum class Mode { lowPass = 0, band, highPass, freeBand };

    inline const char* modeName (int m) noexcept
    {
        switch (m)
        {
            case 0: return "LP";
            case 1: return "BAND";
            case 2: return "HP";
            case 3: return "FREE";
        }
        return "?";
    }

    //  The two hidden colour characters, both vendored from FOUR COLOR:
    //
    //    WARM  a memoryless rational soft saturator with a slow sag envelope
    //          and a drive-dependent bias. Round, fat, gently compressing.
    //    IRON  the same saturator inside a feedback loop whose return passes a
    //          one-pole "core loss" filter, so the transfer depends on what just
    //          happened. Denser, more transformer-ish, more even harmonics.
    //
    //  Neither is a "model" in the sense the earlier rule forbade: there is no
    //  drive, bias, mix or oversampling exposed, and BITE is still the only
    //  amount control. This is one two-way voicing switch.
    enum class Character { warm = 0, iron };

    inline const char* characterName (int c) noexcept
    {
        return c == (int) Character::iron ? "IRON" : "WARM";
    }

    //  FREE mode: the band keeps its width and travels. FOLLOW moves its CENTRE
    //  by up to this many octaves instead of driving EDGE's amount.
    inline constexpr float kFreeTravelOctaves = 2.0f;

    //  Frequency travel. These are also the EDGE macro's ORIGINS: at EDGE 0 the
    //  low corner sits at kLowFreqMin and the high corner at kHighFreqMax, which
    //  is the open, do-nothing position.
    inline constexpr float kLowFreqMin  = 20.0f;
    inline constexpr float kLowFreqMax  = 8000.0f;
    inline constexpr float kHighFreqMin = 200.0f;
    inline constexpr float kHighFreqMax = 20000.0f;

    //  Depth's floor. Not -inf, for two reasons.
    //
    //  A literal zero makes the composite slope jump by 12 dB/oct the instant
    //  Curve gives a section a non-zero share of the depth - a discontinuity in
    //  CURVE at the top of DEPTH.
    //
    //  And the depth in dB is what the EDGE macro walks, so the floor sets how
    //  steep the taper has to be at its top. -120 dB needed 9 dB per 1 % of
    //  travel there and broke EDGE's continuity budget.
    //
    //  -110 dB with -24 dB placed at 65 % of travel reaches the floor at
    //  2.5 dB/%, which is inside the budget, and leaves the floor far enough
    //  below the -20..-50 dB window that a 36 dB/oct setting actually measures
    //  36 there. At -90 dB it measured 32.4: each of the three sections was
    //  only 30 dB from its own floor and had already started to flatten.
    inline constexpr float kDepthFloorDb = -110.0f;

    inline constexpr float kCurveSoftDamping    = 3.6f;
    inline constexpr float kCurveNeutralDamping = 1.41421356f;
    inline constexpr float kResonanceMinDamping = 0.35f;

    //  Smallest ratio allowed between the two effective corner frequencies.
    inline constexpr float kMinEdgeRatio = 1.05f;

    inline constexpr int kNumSections = 3;

    //  SHOULDER: a second, much gentler shelf six octaves INTO the passband from
    //  the cut's corner, so the whole passing side leans down towards it and the
    //  lean travels with the cutoff.
    inline constexpr float kShoulderOctaves = 6.0f;
    inline constexpr float kShoulderDamping = 2.2f;
    inline constexpr float kShoulderMaxDb   = -12.0f;

    //  Static make-up applied for Resonance only. Self-gating: exactly 0 dB
    //  whenever the resonant section is a wire.
    inline constexpr float kResonanceTrimDb = 1.5f;

    // --- BITE ----------------------------------------------------------------
    //
    //  drive = maxDrive(bite) * activity ^ gamma(bite)
    //
    //  The v1 law was linear (drive = 10 * activity) and engaged far too late:
    //  the vendored engine fades out below 5 % drive, so colour only appeared
    //  around -12 dB of Depth. gamma < 1 pulls it forward into the shelf range
    //  where the plug-in actually spends its time.
    //
    //  kBiteMaxDrive is a CEILING, and it is set by the aliasing measurement,
    //  not by taste. If it ever fails the -70 dBc limit at 1x the cap comes
    //  down; latency is never traded for it.
    //  Per character, because the ceiling is set by each one's own aliasing,
    //  not by a shared guess. IRON puts its saturator inside a loop whose
    //  return is low-passed, so it throws far less energy at Nyquist than WARM
    //  does: measured -107 dBc against WARM's -74 at the same drive. Handing
    //  that headroom back is what makes IRON the denser voicing rather than a
    //  differently-labelled WARM - at a shared 24 % the two measured only
    //  1.4 dB apart in second-harmonic content.
    inline constexpr float kBiteMaxDriveWarm = 24.0f;   // % at BITE 100
    inline constexpr float kBiteMaxDriveIron = 46.0f;

    //  What the v0.1 migration is written against.
    inline constexpr float kBiteMaxDrive   = kBiteMaxDriveWarm;
    inline constexpr float kBiteDriveCurve = 0.70f;   // maxDrive shape vs BITE
    inline constexpr float kBiteGammaLow   = 0.65f;   // gamma at BITE -> 0
    inline constexpr float kBiteGammaHigh  = 0.35f;   // gamma at BITE 100

    // --- SPREAD --------------------------------------------------------------
    //
    //  Total left-to-right separation in semitones at +/-100 %. Both corners of
    //  a channel move together, so each channel keeps its bandwidth in octaves.
    inline constexpr float kSpreadMaxSemitones = 12.0f;

    // --- FOLLOW --------------------------------------------------------------
    //
    //  The detector reaches full modulation at the Sensitivity level and zero
    //  kFollowRangeDb below it.
    inline constexpr float kFollowRangeDb = 24.0f;
}
