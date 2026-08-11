// v1 -> v2 state migration.
//
// v0.1 shipped, was loaded into a real Cubase project, and its parameter IDs
// ("lowFreq", "focus", "link", ...) are in that project's file. v0.2 renames
// every ID and changes what several of them mean, so a v1 tree loaded as-is
// would silently reset the plug-in to defaults on a session someone had already
// mixed with.
//
// The mapping is deliberately conservative:
//
//   * the five per-edge targets carry across unchanged - they mean the same
//     thing they always did;
//   * `focus` is DROPPED. It has no equivalent; folding its octave offset into
//     the frequencies would move a corner the user set by hand;
//   * `link` is DROPPED as a parameter. It survives as editor behaviour only;
//   * `mode` becomes BAND, the only mode in which both edges behave the way
//     they did in v1;
//   * `edge` becomes 100 %, because in v1 the targets WERE the sound. Anything
//     less would open the filter and change a finished mix;
//   * FOLLOW and SPREAD default to 0, which is v1's behaviour exactly;
//   * `bite` becomes kMigratedBite, chosen so the new BITE law reproduces v1's
//     drive as closely as one number can - see below.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIds.h"
#include "../Dsp/EdgeEngine.h"

namespace edge
{
    //  v1's colour law was drive = 10 * activity. The v2 law is
    //  maxDrive(bite) * activity^gamma(bite). No single BITE reproduces a
    //  different curve everywhere, so this is the value whose drive matches
    //  v1's at the activity a full cut produces (activity = 1), which is where
    //  v1 presets that used a cut actually sat: maxDrive(bite) = 10.
    //
    //      kBiteMaxDrive * b^kBiteDriveCurve = 10
    //      b = (10 / kBiteMaxDrive) ^ (1 / kBiteDriveCurve)
    //
    //  Below a full cut the v2 colour comes in earlier than v1's did. That is
    //  the point of the change, and it is documented rather than hidden.
    inline float migratedBitePercent() noexcept
    {
        return 100.0f * std::pow (10.0f / kBiteMaxDrive, 1.0f / kBiteDriveCurve);
    }

    //  True if `tree` looks like a v1 state.
    inline bool isLegacyState (const juce::ValueTree& tree) noexcept
    {
        if (! tree.isValid())
            return false;

        if ((int) tree.getProperty ("stateVersion", 1) >= kStateVersion)
            return false;

        for (int i = 0; i < tree.getNumChildren(); ++i)
            if (tree.getChild (i).getProperty ("id").toString() == "lowFreq")
                return true;

        return false;
    }

    //  Rewrites a v1 tree in place into a v2 one. Returns false if it was not
    //  a v1 tree, in which case nothing was touched.
    inline bool migrateToCurrent (juce::ValueTree& tree)
    {
        if (! isLegacyState (tree))
            return false;

        //  Collect the old values first: the loop below deletes as it goes.
        std::map<juce::String, float> old;

        for (int i = 0; i < tree.getNumChildren(); ++i)
        {
            const auto child = tree.getChild (i);
            if (child.hasProperty ("id") && child.hasProperty ("value"))
                old[child.getProperty ("id").toString()] = (float) child.getProperty ("value");
        }

        auto take = [&old] (const char* id, float fallback)
        {
            const auto it = old.find (id);
            return it != old.end() ? it->second : fallback;
        };

        tree.removeAllChildren (nullptr);

        auto put = [&tree] (const char* id, float value)
        {
            juce::ValueTree p { "PARAM" };
            p.setProperty ("id", id, nullptr);
            p.setProperty ("value", value, nullptr);
            tree.appendChild (p, nullptr);
        };

        put (param::lowFreq,      take ("lowFreq", 250.0f));
        put (param::lowDepth,     take ("lowDepth", 100.0f));
        put (param::lowCurve,     take ("lowCurve", 75.0f));
        put (param::lowShoulder,  take ("lowShoulder", 0.0f));
        put (param::lowReso,      take ("lowRes", 0.0f));

        put (param::highFreq,     take ("highFreq", 6000.0f));
        put (param::highDepth,    take ("highDepth", 100.0f));
        put (param::highCurve,    take ("highCurve", 75.0f));
        put (param::highShoulder, take ("highShoulder", 0.0f));
        put (param::highReso,     take ("highRes", 0.0f));

        put (param::mode,   (float) (int) Mode::band);
        put (param::edge,   100.0f);
        put (param::follow, 0.0f);
        put (param::spread, 0.0f);
        put (param::bite,   migratedBitePercent());
        put (param::output, take ("output", 0.0f));
        put (param::bypass, take ("bypass", 0.0f));

        //  New in v0.3: a v0.1 project had neither, and the defaults are the
        //  behaviour it had.
        put (param::character, (float) (int) Character::warm);
        put (param::midFreq, 1000.0f);
        put (param::midGain,    0.0f);      // a wire, which is what v0.1 had
        put (param::midReso,   40.0f);

        put (param::followSens,    -12.0f);
        put (param::followAttack,   10.0f);
        put (param::followRelease, 150.0f);

        tree.setProperty ("stateVersion", kStateVersion, nullptr);
        tree.setProperty ("migratedFrom", 1, nullptr);
        return true;
    }
}
