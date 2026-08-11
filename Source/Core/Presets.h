// Factory presets.
//
// A filter with 24 parameters and no presets does not demonstrate itself. These
// exist to show what EDGE IS - that a corner walks from shelf to cut on one
// control, that the signal can drive it, that the colour arrives with the
// depth - not to be a library.
//
// Every preset is a full parameter set. There is no "only the ones I changed":
// a partial preset silently inherits whatever the previous one left behind, and
// then the same preset sounds different depending on what you loaded before it.
//
// Program 0 is DEFAULT and is exactly the parameter defaults, so a host that
// selects program 0 on load - several do - lands on the plug-in's neutral state
// rather than on someone's taste.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIds.h"

namespace edge
{
    struct Preset
    {
        const char* name;

        float lowFreq, lowDepth, lowCurve, lowShoulder, lowReso;
        float highFreq, highDepth, highCurve, highShoulder, highReso;
        float midFreq, midGain, midReso;
        int   mode;
        float edge, follow, spread, bite;
        int   character;
        float output;
        float followSens, followAttack, followRelease;
    };

    //  Curve percentages that name a slope, from kSlopeChoices: 0 SOFT,
    //  50 = 12, 60 = 24, 70 = 36, 80 = 48, 100 = 72 dB/oct.
    //
    //  The count is DERIVED from the table. Writing it by hand meant adding a
    //  preset and getting "excess elements in array initializer" instead of a
    //  preset.
    inline const Preset kPresets[] =
    {
        //  name                low:  Hz   dep  crv  shl  res | high: Hz    dep  crv  shl  res | mid: Hz   gain res | mode                     edge  foll  sprd  bite | char                       out | sens   atk    rel
        { "DEFAULT",                  250,  100,  75,   0,   0,        6000, 100,  75,   0,   0,      1000,   0,  40, (int) Mode::band,           0,    0,    0,  35, (int) Character::warm,      0,  -12,   10,  150 },

        // --- the gesture ------------------------------------------------------
        { "Open Up",                  180,  100,  60,   0,  18,        7000, 100,  60,   0,  14,    1000,   0,  40, (int) Mode::band,          62,    0,    0,  40, (int) Character::warm,      0,  -12,   10,  150 },
        { "Slow Sweep Down",           20,    0,  75,   0,   0,        2400, 100,  70,  35,  30,      1000,   0,  40, (int) Mode::lowPass,       48,    0,    0,  45, (int) Character::warm,      0,  -12,   10,  150 },
        { "Rise Into The Drop",       900,  100,  70,  30,  26,       20000,   0,  75,   0,   0,      1000,   0,  40, (int) Mode::highPass,      55,    0,    0,  52, (int) Character::iron,      0,  -12,   10,  150 },

        // --- LP ----------------------------------------------------------------
        { "LP Soft Lid",               20,    0,  75,   0,   0,        4800,  62,   0,  20,   0,      1000,   0,  40, (int) Mode::lowPass,       70,    0,    0,  30, (int) Character::warm,      0,  -12,   10,  150 },
        { "LP 24 Classic",             20,    0,  75,   0,   0,        1800, 100,  60,   0,  42,      1000,   0,  40, (int) Mode::lowPass,       72,    0,    0,  38, (int) Character::warm,      0,  -12,   10,  150 },
        { "LP 72 Brick",               20,    0,  75,   0,   0,         900, 100, 100,   0,   0,      1000,   0,  40, (int) Mode::lowPass,       85,    0,    0,  25, (int) Character::iron,      0,  -12,   10,  150 },
        { "LP Tape Lid",               20,    0,  75,   0,   0,        6200,  48,   0,  45,   0,      1000,   0,  40, (int) Mode::lowPass,       64,    0,    0,  68, (int) Character::warm,   -1.5,  -12,   10,  150 },

        // --- HP ----------------------------------------------------------------
        { "HP Clean Out",             120,  100,  60,   0,   0,       20000,   0,  75,   0,   0,      1000,   0,  40, (int) Mode::highPass,      80,    0,    0,  20, (int) Character::warm,      0,  -12,   10,  150 },
        { "HP Thin It",               420,  100,  70,   0,  34,       20000,   0,  75,   0,   0,      1000,   0,  40, (int) Mode::highPass,      66,    0,    0,  44, (int) Character::iron,      0,  -12,   10,  150 },
        { "HP Radio",                 700,  100,  70,   0,  22,        3400, 100,  70,   0,  18,      1800,   6,  62, (int) Mode::band,          78,    0,    0,  62, (int) Character::iron,   -1.0,  -12,   10,  150 },

        // --- BAND ---------------------------------------------------------------
        { "Band Body",                160,   72,  50,   0,  12,        5600,  66,  50,  18,  10,       650,   4,  30, (int) Mode::band,          58,    0,    0,  42, (int) Character::warm,      0,  -12,   10,  150 },
        { "Band Narrow",              600,  100,  70,   0,  46,        2600, 100,  70,   0,  44,      1300,   8,  74, (int) Mode::band,          82,    0,    0,  50, (int) Character::iron,   -2.0,  -12,   10,  150 },
        { "Band Wide Tilt",            60,   40,   0,  55,   0,       14000,  40,   0,  55,   0,      1000,   0,  40, (int) Mode::band,          70,    0,    0,  36, (int) Character::warm,      0,  -12,   10,  150 },

        // --- the follower --------------------------------------------------------
        { "Follow Open",              200,  100,  60,   0,  20,        5200, 100,  60,   0,  16,      1000,   0,  40, (int) Mode::band,          30,  -72,    0,  46, (int) Character::warm,      0,  -14,    6,  180 },
        { "Follow Closed",             20,    0,  75,   0,   0,        3000, 100,  70,  25,  36,      1000,   0,  40, (int) Mode::lowPass,       35,   78,    0,  52, (int) Character::iron,      0,  -16,   14,  260 },
        { "Pump Wah",                 340,  100,  70,   0,  40,        4200, 100,  70,   0,  30,      1600,  10,  70, (int) Mode::band,          40,   88,    0,  58, (int) Character::iron,   -1.5,  -18,    3,   90 },

        // --- stereo ---------------------------------------------------------------
        { "Wide Edges",               220,   84,  60,  15,  24,        6400,  84,  60,  15,  20,      1000,   0,  40, (int) Mode::band,          66,    0,   72,  40, (int) Character::warm,      0,  -12,   10,  150 },
        { "Counter Sweep",            300,  100,  60,   0,  28,        4800, 100,  60,   0,  24,      1000,   0,  40, (int) Mode::band,          58,   60,  -90,  44, (int) Character::warm,      0,  -13,    8,  200 },

        // --- FREE -----------------------------------------------------------------
        { "Free Formant",             480,  100,  70,   0,  38,        2200, 100,  70,   0,  36,      1100,  12,  78, (int) Mode::freeBand,      74,    0,    0,  55, (int) Character::iron,   -2.0,  -12,   10,  150 },
        { "Free Travelling Band",     260,  100,  60,   0,  30,        1600, 100,  60,   0,  28,       900,   9,  66, (int) Mode::freeBand,      68,   82,    0,  48, (int) Character::warm,   -1.0,  -15,   12,  220 },

        // --- colour ----------------------------------------------------------------
        { "Warm Shelf Only",          140,   30,   0,  25,   0,        9000,  26,   0,  25,   0,      1000,   0,  40, (int) Mode::band,          75,    0,    0,  92, (int) Character::warm,   -1.0,  -12,   10,  150 },
        { "Iron Grind",               260,   66,  50,  20,  16,        5000,  62,  50,  20,  14,       800,   7,  48, (int) Mode::band,          70,    0,   24, 100, (int) Character::iron,   -2.5,  -12,   10,  150 },
    };

    inline constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

    //  Writes a preset through the parameters, which is what makes the host's
    //  automation lanes, the UI and the plug-in state all agree about what just
    //  happened.
    inline void applyPreset (juce::AudioProcessorValueTreeState& state, int index)
    {
        const auto& p = kPresets[juce::jlimit (0, kNumPresets - 1, index)];

        auto set = [&state] (const char* id, float value)
        {
            if (auto* param = state.getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        };

        set (param::lowFreq,     p.lowFreq);
        set (param::lowDepth,    p.lowDepth);
        set (param::lowCurve,    p.lowCurve);
        set (param::lowShoulder, p.lowShoulder);
        set (param::lowReso,     p.lowReso);

        set (param::highFreq,     p.highFreq);
        set (param::highDepth,    p.highDepth);
        set (param::highCurve,    p.highCurve);
        set (param::highShoulder, p.highShoulder);
        set (param::highReso,     p.highReso);

        set (param::midFreq, p.midFreq);
        set (param::midGain, p.midGain);
        set (param::midReso, p.midReso);

        set (param::mode,      (float) p.mode);
        set (param::edge,      p.edge);
        set (param::follow,    p.follow);
        set (param::spread,    p.spread);
        set (param::bite,      p.bite);
        set (param::character, (float) p.character);
        set (param::output,    p.output);

        //  Never part of a preset. A preset that silently un-bypasses the
        //  plug-in is a preset that ruins a mix while you are auditioning.
        //  set (param::bypass, ...) - deliberately absent.

        set (param::followSens,    p.followSens);
        set (param::followAttack,  p.followAttack);
        set (param::followRelease, p.followRelease);
    }
}
