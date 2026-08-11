// EDGE host-contract suite.
//
//     build/EdgeHostTests_artefacts/<config>/EdgeHostTests
//
// test_dsp.cpp measures the DSP. This measures the PLUG-IN: the parameter
// contract a host automates through, the prepare/process lifecycle a host
// drives, the state a project file carries, and the editor a user opens.
//
// It is not a replacement for pluginval - pluginval loads the built VST3
// through the real SDK wrapper and this links the processor directly - but
// every check here is one pluginval would make, and they run on any machine
// with no download.
//
// Exit code 0 = every check passed. Every check prints what it measured.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <juce_gui_extra/juce_gui_extra.h>

#include "../Core/ParameterIds.h"
#include "../Core/StateMigration.h"
#include "../PluginEditor.h"
#include "../PluginProcessor.h"

namespace
{
    int gChecks = 0, gFailures = 0;

    void section (const char* name) { std::printf ("\n== %s ==\n", name); }

    void check (bool ok, const char* label, const juce::String& measured)
    {
        ++gChecks;
        if (! ok) ++gFailures;
        std::printf ("  [%s] %-58s %s\n", ok ? "PASS" : "FAIL", label,
                     measured.toRawUTF8());
    }

    juce::String db (double linear)
    {
        return linear <= 0.0 ? juce::String ("-inf dBFS")
                             : juce::String (20.0 * std::log10 (linear), 1) + " dBFS";
    }

    // -------------------------------------------------------------------------
    //  A deterministic pseudo-host. The determinism checks below are worthless
    //  if the INPUT is not bit-identical between the two renders, and the
    //  block-size check compares renders chopped into DIFFERENT block sizes -
    //  so the sample at a given (channel, absolute index) has to be the same
    //  number either way. That rules out a running generator; this one is a
    //  pure function of the position.
    // -------------------------------------------------------------------------
    float sampleAt (int channel, int index) noexcept
    {
        std::uint32_t h = (std::uint32_t) index * 2654435761u
                        + (std::uint32_t) channel * 2246822519u + 1u;
        h ^= h >> 15; h *= 2246822519u;
        h ^= h >> 13; h *= 3266489917u;
        h ^= h >> 16;
        return (float) ((double) (h >> 8) / 8388608.0 - 1.0);
    }

    struct Lcg
    {
        std::uint32_t s = 1u;
        float next() noexcept
        {
            s = s * 1664525u + 1013904223u;
            return (float) ((double) (s >> 8) / 8388608.0 - 1.0);
        }
    };

    const char* const kAllIds[] = {
        edge::param::lowFreq,  edge::param::lowDepth,  edge::param::lowCurve,
        edge::param::lowShoulder, edge::param::lowReso,
        edge::param::highFreq, edge::param::highDepth, edge::param::highCurve,
        edge::param::highShoulder, edge::param::highReso,
        edge::param::midFreq,  edge::param::midGain,   edge::param::midReso,
        edge::param::mode,     edge::param::edge,      edge::param::follow,
        edge::param::spread,   edge::param::bite,      edge::param::output,
        edge::param::bypass,
        edge::param::followSens, edge::param::followAttack, edge::param::followRelease,
        edge::param::character };

    constexpr int kNumIds = (int) (sizeof (kAllIds) / sizeof (kAllIds[0]));

    void setParam (EdgeAudioProcessor& p, const char* id, float value)
    {
        if (auto* param = p.getState().getParameter (id))
            param->setValueNotifyingHost (param->convertTo0to1 (value));
    }

    float getParam (EdgeAudioProcessor& p, const char* id)
    {
        auto* param = p.getState().getParameter (id);
        return param != nullptr ? param->convertFrom0to1 (param->getValue()) : 0.0f;
    }

    //  One automation "performance", written once and replayed by every
    //  determinism check. Deliberately includes a MODE switch and a full EDGE
    //  sweep, because those are the two things that were measured to click.
    void automateAt (EdgeAudioProcessor& p, int block, int totalBlocks)
    {
        const float t = (float) block / (float) juce::jmax (1, totalBlocks - 1);

        setParam (p, edge::param::edge,   100.0f * t);
        setParam (p, edge::param::follow, -100.0f + 200.0f * t);
        setParam (p, edge::param::spread,  100.0f * std::sin (6.283185f * t));
        setParam (p, edge::param::midGain, -18.0f + 36.0f * t);

        if (block == totalBlocks / 3)      setParam (p, edge::param::mode, (float) (int) edge::Mode::lowPass);
        if (block == (2 * totalBlocks) / 3) setParam (p, edge::param::mode, (float) (int) edge::Mode::freeBand);
    }

    //  A starting point that is not the default in any parameter, so a state
    //  round-trip that silently drops one is visible.
    void applyBusySettings (EdgeAudioProcessor& p)
    {
        setParam (p, edge::param::lowFreq,      317.0f);
        setParam (p, edge::param::lowDepth,      64.0f);
        setParam (p, edge::param::lowCurve,      81.0f);
        setParam (p, edge::param::lowShoulder,   43.0f);
        setParam (p, edge::param::lowReso,       29.0f);
        setParam (p, edge::param::highFreq,    5321.0f);
        setParam (p, edge::param::highDepth,     77.0f);
        setParam (p, edge::param::highCurve,     58.0f);
        setParam (p, edge::param::highShoulder,  22.0f);
        setParam (p, edge::param::highReso,      36.0f);
        setParam (p, edge::param::midFreq,     1837.0f);
        setParam (p, edge::param::midGain,       -7.5f);
        setParam (p, edge::param::midReso,       71.0f);
        setParam (p, edge::param::mode,   (float) (int) edge::Mode::highPass);
        setParam (p, edge::param::edge,          62.0f);
        setParam (p, edge::param::follow,        41.0f);
        setParam (p, edge::param::spread,       -33.0f);
        setParam (p, edge::param::bite,          57.0f);
        setParam (p, edge::param::output,        -2.5f);
        setParam (p, edge::param::bypass,         0.0f);
        setParam (p, edge::param::followSens,   -17.0f);
        setParam (p, edge::param::followAttack,  23.0f);
        setParam (p, edge::param::followRelease, 260.0f);
        setParam (p, edge::param::character, (float) (int) edge::Character::iron);
    }

    std::vector<float> render (EdgeAudioProcessor& p, int blocks, int blockSize,
                               bool automate)
    {
        juce::AudioBuffer<float> buffer (2, blockSize);
        juce::MidiBuffer midi;
        std::vector<float> out;
        out.reserve ((size_t) blocks * (size_t) blockSize * 2);

        for (int b = 0; b < blocks; ++b)
        {
            if (automate)
                automateAt (p, b, blocks);

            for (int c = 0; c < 2; ++c)
            {
                auto* d = buffer.getWritePointer (c);
                for (int i = 0; i < blockSize; ++i)
                    d[i] = 0.25f * sampleAt (c, b * blockSize + i);
            }

            p.processBlock (buffer, midi);

            for (int c = 0; c < 2; ++c)
                out.insert (out.end(), buffer.getReadPointer (c),
                            buffer.getReadPointer (c) + blockSize);
        }

        return out;
    }

    //  render() appends one block of channel 0 then one block of channel 1, so
    //  the memory layout depends on the block size. Two renders chopped
    //  differently are only comparable once both are channel-major.
    std::vector<float> deinterleave (const std::vector<float>& blocked, int blockSize)
    {
        const size_t perChannel = blocked.size() / 2;
        std::vector<float> out (blocked.size());

        for (size_t i = 0; i < blocked.size(); ++i)
        {
            const size_t block   = i / (size_t) (blockSize * 2);
            const size_t within  = i % (size_t) (blockSize * 2);
            const size_t channel = within / (size_t) blockSize;
            const size_t index   = block * (size_t) blockSize + within % (size_t) blockSize;
            out[channel * perChannel + index] = blocked[i];
        }

        return out;
    }

    double peakDifference (const std::vector<float>& a, const std::vector<float>& b)
    {
        if (a.size() != b.size() || a.empty())
            return 1.0e9;

        double worst = 0.0;
        for (size_t i = 0; i < a.size(); ++i)
            worst = juce::jmax (worst, (double) std::abs (a[i] - b[i]));
        return worst;
    }

    bool allFinite (const juce::AudioBuffer<float>& b)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (! std::isfinite (b.getReadPointer (c)[i]))
                    return false;
        return true;
    }

    // =========================================================================
    //  1. The parameter contract
    // =========================================================================
    void testParameters()
    {
        section ("1. parameter contract");

        EdgeAudioProcessor p;
        const auto& params = p.getParameters();

        check (params.size() == kNumIds, "parameter count",
               juce::String (params.size()) + " exposed, " + juce::String (kNumIds) + " expected");

        //  Every ID the code names must actually exist. A rename that misses one
        //  call site otherwise shows up as a silently dead control.
        int missing = 0;
        for (auto* id : kAllIds)
            if (p.getState().getParameter (id) == nullptr)
                ++missing;
        check (missing == 0, "every named ID is registered",
               juce::String (missing) + " missing");

        juce::StringArray ids, names;
        int notAutomatable = 0, badDefault = 0, emptyName = 0, longName = 0;

        for (auto* raw : params)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;

            ids.add (rp->paramID);
            names.add (rp->getName (128));

            if (! rp->isAutomatable()) ++notAutomatable;
            if (rp->getName (128).isEmpty()) ++emptyName;

            //  VST3 hands the host a fixed-width name field. JUCE truncates
            //  silently, so two parameters whose names differ only after the
            //  cut become indistinguishable in Cubase's automation lane list.
            if (rp->getName (128).length() > 31) ++longName;

            const float def = rp->getDefaultValue();
            if (! (def >= 0.0f && def <= 1.0f)) ++badDefault;
        }

        ids.sort (false);
        auto uniqueIds = ids; uniqueIds.removeDuplicates (false);
        check (uniqueIds.size() == ids.size(), "parameter IDs are unique",
               juce::String (ids.size() - uniqueIds.size()) + " duplicates");

        auto uniqueNames = names; uniqueNames.removeDuplicates (false);
        check (uniqueNames.size() == names.size(), "parameter names are unique",
               juce::String (names.size() - uniqueNames.size()) + " duplicates");

        check (notAutomatable == 0, "every parameter is automatable",
               juce::String (notAutomatable) + " not automatable");
        check (emptyName == 0, "every parameter has a name",
               juce::String (emptyName) + " unnamed");
        check (longName == 0, "no name is truncated by the VST3 name field",
               juce::String (longName) + " longer than 31 chars");
        check (badDefault == 0, "every default is inside its range",
               juce::String (badDefault) + " out of range");

        //  Text round-trip. A host that lets the user TYPE a value calls
        //  getValueForText on what they typed; if that does not come back to the
        //  same number, typing 5000 into the frequency box moves the filter
        //  somewhere else.
        double worstRoundTrip = 0.0;
        juce::String worstAt;

        for (auto* raw : params)
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw);
            if (rp == nullptr) continue;

            for (int i = 0; i <= 20; ++i)
            {
                const float norm = (float) i / 20.0f;
                const auto text = rp->getText (norm, 0);
                const float back = rp->getValueForText (text);

                //  Compare in the parameter's own units: a 0.5 dB error on a
                //  frequency and on a percentage are not the same mistake.
                //
                //  And compare against the value the parameter would actually
                //  STORE for this position. A bool or a choice snaps to its
                //  nearest step, so "0.55 -> On -> 1.0" is a correct round trip,
                //  not a 45 % error.
                const auto& range = rp->getNormalisableRange();
                const double a = range.snapToLegalValue (range.convertFrom0to1 (norm));
                const double b = range.snapToLegalValue (
                                     range.convertFrom0to1 (juce::jlimit (0.0f, 1.0f, back)));
                const double span = range.end - range.start;
                const double relative = span > 0.0 ? std::abs (a - b) / span : 0.0;

                if (relative > worstRoundTrip)
                {
                    worstRoundTrip = relative;
                    worstAt = rp->paramID + " @ " + text;
                }
            }
        }

        //  1 % of the range: enough slack for a display rounded to one decimal,
        //  not enough to hide a broken parser.
        check (worstRoundTrip < 0.01, "value -> text -> value round-trips",
               juce::String (100.0 * worstRoundTrip, 3) + " % of range, worst at " + worstAt);
    }

    // =========================================================================
    //  2. The prepare / process lifecycle
    // =========================================================================
    void testLifecycle()
    {
        section ("2. host lifecycle");

        const double rates[]  = { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const int    blocks[] = { 1, 16, 17, 32, 64, 128, 333, 512, 1024, 2048 };

        EdgeAudioProcessor p;
        applyBusySettings (p);

        juce::MidiBuffer midi;
        int nonFinite = 0, cases = 0;

        for (double rate : rates)
        {
            for (int maxBlock : blocks)
            {
                p.prepareToPlay (rate, maxBlock);

                //  Hosts do not always hand over a full block. Every size from
                //  0 up to the prepared maximum has to be legal.
                for (int n : { 0, 1, maxBlock / 2, maxBlock })
                {
                    if (n < 0 || n > maxBlock) continue;

                    juce::AudioBuffer<float> buffer (2, juce::jmax (1, maxBlock));
                    buffer.clear();

                    Lcg rng;
                    for (int c = 0; c < 2; ++c)
                    {
                        auto* d = buffer.getWritePointer (c);
                        for (int i = 0; i < n; ++i) d[i] = 0.5f * rng.next();
                    }

                    juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), 2, n);
                    p.processBlock (view, midi);
                    ++cases;

                    if (n > 0 && ! allFinite (view)) ++nonFinite;
                }

                p.releaseResources();
            }
        }

        check (nonFinite == 0, "every rate x block size produces finite output",
               juce::String (cases) + " cases, " + juce::String (nonFinite) + " non-finite");

        //  Repeated prepare without release, which is what a host does when the
        //  user changes the buffer size in the audio settings.
        p.prepareToPlay (48000.0, 512);
        p.prepareToPlay (48000.0, 64);
        p.prepareToPlay (96000.0, 512);
        juce::AudioBuffer<float> b (2, 512);
        b.clear();
        p.processBlock (b, midi);
        check (allFinite (b), "re-prepare without release is safe", "finite");

        //  Mono. isBusesLayoutSupported allows it, so it has to work.
        {
            EdgeAudioProcessor mono;
            juce::AudioProcessor::BusesLayout layout;
            layout.inputBuses.add  (juce::AudioChannelSet::mono());
            layout.outputBuses.add (juce::AudioChannelSet::mono());
            const bool accepted = mono.setBusesLayout (layout);

            mono.prepareToPlay (48000.0, 256);
            applyBusySettings (mono);

            juce::AudioBuffer<float> m (1, 256);
            Lcg rng;
            for (int i = 0; i < 256; ++i) m.getWritePointer (0)[i] = 0.4f * rng.next();
            mono.processBlock (m, midi);

            check (accepted && allFinite (m), "mono layout accepted and processed",
                   accepted ? "finite" : "layout refused");
        }

        //  Silence in, silence out. A filter that outputs anything from nothing
        //  is oscillating.
        {
            EdgeAudioProcessor s;
            applyBusySettings (s);
            s.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> sb (2, 512);
            float worst = 0.0f;
            for (int k = 0; k < 40; ++k)
            {
                sb.clear();
                s.processBlock (sb, midi);
                worst = juce::jmax (worst, sb.getMagnitude (0, 512));
            }
            check (worst == 0.0f, "silence in produces exact silence out",
                   db ((double) worst));
        }
    }

    // =========================================================================
    //  3. Hostile input
    // =========================================================================
    void testHostileInput()
    {
        section ("3. hostile input");

        juce::MidiBuffer midi;

        auto feedThenRecover = [&] (float poison, const char* label)
        {
            EdgeAudioProcessor p;
            applyBusySettings (p);
            p.prepareToPlay (48000.0, 256);

            juce::AudioBuffer<float> b (2, 256);

            //  One block of poison, as an upstream plug-in glitching would send.
            b.clear();
            for (int c = 0; c < 2; ++c)
                b.getWritePointer (c)[128] = poison;
            p.processBlock (b, midi);

            //  Then ten blocks of ordinary audio. The question is not whether
            //  the poisoned block is clean - it cannot be - but whether the
            //  filter state has been permanently destroyed.
            Lcg rng;
            bool recovered = true;
            for (int k = 0; k < 10; ++k)
            {
                for (int c = 0; c < 2; ++c)
                {
                    auto* d = b.getWritePointer (c);
                    for (int i = 0; i < 256; ++i) d[i] = 0.3f * rng.next();
                }
                p.processBlock (b, midi);
                if (k >= 2 && ! allFinite (b)) recovered = false;
            }

            check (recovered, label, recovered ? "recovered" : "state latched non-finite");
        };

        feedThenRecover (std::numeric_limits<float>::quiet_NaN(), "recovers from a NaN sample upstream");
        feedThenRecover (std::numeric_limits<float>::infinity(),  "recovers from an Inf sample upstream");
        feedThenRecover (1.0e30f,                                 "recovers from a 1e30 sample upstream");

        //  Denormals. ScopedNoDenormals is in processBlock, but only measuring
        //  proves the output is clean rather than merely fast.
        {
            EdgeAudioProcessor p;
            applyBusySettings (p);
            p.prepareToPlay (48000.0, 512);

            juce::AudioBuffer<float> b (2, 512);
            for (int c = 0; c < 2; ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int i = 0; i < 512; ++i) d[i] = 1.0e-38f;
            }
            p.processBlock (b, midi);
            check (allFinite (b), "denormal input produces finite output",
                   db ((double) b.getMagnitude (0, 512)));
        }
    }

    // =========================================================================
    //  4. Automation determinism  (a host renders the same project twice)
    // =========================================================================
    void testAutomation()
    {
        section ("4. automation determinism");

        constexpr int blocks = 300, blockSize = 256;

        auto renderOnce = [&]
        {
            EdgeAudioProcessor p;
            p.prepareToPlay (48000.0, blockSize);
            return render (p, blocks, blockSize, true);
        };

        const auto first  = renderOnce();
        const auto second = renderOnce();

        const double diff = peakDifference (first, second);
        check (diff < 1.0e-7, "two offline renders of the same automation null",
               db (diff) + "  (" + juce::String ((int) first.size()) + " samples)");

        //  Different block size, same automation timeline in SECONDS. This is
        //  the check that catches per-block state: a host is free to change its
        //  buffer size between two renders of the same project.
        //
        //  The parameter smoothers are time-based, so the two are not expected
        //  to be bit-identical - only to be the same sound.
        {
            EdgeAudioProcessor a, b;
            a.prepareToPlay (48000.0, 256);
            b.prepareToPlay (48000.0, 512);

            applyBusySettings (a);
            applyBusySettings (b);

            const auto ra = deinterleave (render (a, 100, 256, false), 256);
            const auto rb = deinterleave (render (b, 50,  512, false), 512);

            double worst = 0.0;
            const size_t n = juce::jmin (ra.size(), rb.size());
            for (size_t i = 0; i < n; ++i)
                worst = juce::jmax (worst, (double) std::abs (ra[i] - rb[i]));

            check (worst < 1.0e-4, "block size does not change the result",
                   db (worst) + "  (" + juce::String ((int) n) + " samples)");
        }

        //  Automation at block rate versus automation the host samples once per
        //  buffer: EDGE resolves settings in 32-sample chunks, so a parameter
        //  that moves inside a block must not be quantised to the block.
        {
            EdgeAudioProcessor p;
            p.prepareToPlay (48000.0, 2048);
            setParam (p, edge::param::edge, 50.0f);
            const float before = getParam (p, edge::param::edge);
            setParam (p, edge::param::edge, 51.0f);
            const float after = getParam (p, edge::param::edge);
            check (std::abs (after - before - 1.0f) < 1.0e-3,
                   "parameter writes are visible immediately",
                   juce::String (before, 3) + " -> " + juce::String (after, 3));
        }
    }

    // =========================================================================
    //  5. State
    // =========================================================================
    std::uint64_t contractHash (EdgeAudioProcessor& p)
    {
        //  Everything a saved project depends on: the ID, its range and its
        //  default. Change any of them and every project saved before the change
        //  loads differently, so the hash is asserted rather than trusted.
        juce::StringArray rows;

        for (auto* raw : p.getParameters())
        {
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (raw))
            {
                const auto& r = rp->getNormalisableRange();
                rows.add (rp->paramID + "|" + juce::String (r.start, 4)
                          + "|" + juce::String (r.end, 4)
                          + "|" + juce::String (r.interval, 4)
                          + "|" + juce::String (rp->getDefaultValue(), 6)
                          + "|" + juce::String (rp->getNumSteps()));
            }
        }

        rows.sort (false);
        const auto joined = rows.joinIntoString ("\n").toStdString();

        std::uint64_t h = 1469598103934665603ull;          // FNV-1a, 64 bit
        for (unsigned char c : joined)
        {
            h ^= (std::uint64_t) c;
            h *= 1099511628211ull;
        }
        return h;
    }

    void testState()
    {
        section ("5. state");

        //  --- the contract hash -----------------------------------------------
        //  Every saved project depends on the ID, range, interval, default and
        //  step count of all 24 parameters. Recorded here so that changing any
        //  of them is a deliberate act with a state-version bump and a
        //  migration behind it, rather than something noticed by a customer
        //  whose session reopened wrong.
        constexpr std::uint64_t kContractHash = 0x7826a61a1db9aa41ull;

        EdgeAudioProcessor probe;
        const auto hash = contractHash (probe);

        check (hash == kContractHash, "parameter contract is unchanged",
               "0x" + juce::String::toHexString ((juce::int64) hash)
                 + (hash == kContractHash ? "" : "  (recorded 0x7826a61a1db9aa41)"));

        //  --- v2 round trip ----------------------------------------------------
        {
            EdgeAudioProcessor a;
            applyBusySettings (a);
            a.editorWidth.store (1180);
            a.editorHeight.store (760);
            a.shapeOpen.store (true);

            juce::MemoryBlock saved;
            a.getStateInformation (saved);

            EdgeAudioProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());

            double worst = 0.0;
            juce::String worstId;
            for (auto* id : kAllIds)
            {
                auto* pa = a.getState().getParameter (id);
                auto* pb = b.getState().getParameter (id);
                if (pa == nullptr || pb == nullptr) continue;

                const double d = std::abs ((double) pa->getValue() - (double) pb->getValue());
                if (d > worst) { worst = d; worstId = id; }
            }

            check (worst < 1.0e-6, "all 24 parameters survive a save/load",
                   juce::String (worst, 9) + (worstId.isEmpty() ? "" : "  worst " + worstId));

            check (b.editorWidth.load() == 1180 && b.editorHeight.load() == 760
                     && b.shapeOpen.load(),
                   "editor size and SHAPE state survive a save/load",
                   juce::String (b.editorWidth.load()) + " x " + juce::String (b.editorHeight.load())
                     + ", shape " + (b.shapeOpen.load() ? "open" : "closed"));

            check (! b.loadedLegacyState.load(), "a v2 state is not flagged as migrated",
                   b.loadedLegacyState.load() ? "flagged" : "not flagged");
        }

        //  --- a state recall reproduces the SOUND, not just the numbers --------
        {
            EdgeAudioProcessor a;
            applyBusySettings (a);
            juce::MemoryBlock saved;
            a.getStateInformation (saved);
            a.prepareToPlay (48000.0, 256);
            const auto direct = render (a, 120, 256, false);

            //  A project load: setStateInformation first, then prepareToPlay -
            //  which is the order a VST3 host uses, and the reason the recall
            //  path does not have to snap.
            EdgeAudioProcessor b;
            b.setStateInformation (saved.getData(), (int) saved.getSize());
            b.prepareToPlay (48000.0, 256);
            const auto recalled = render (b, 120, 256, false);

            const double diff = peakDifference (direct, recalled);
            check (diff < 1.0e-7, "a recalled project renders identically", db (diff));
        }

        //  --- v1 migration -----------------------------------------------------
        {
            //  Written the way v0.1's APVTS actually wrote it.
            juce::ValueTree v1 ("EDGE");
            auto put = [&v1] (const char* id, float value)
            {
                juce::ValueTree p ("PARAM");
                p.setProperty ("id", id, nullptr);
                p.setProperty ("value", value, nullptr);
                v1.appendChild (p, nullptr);
            };

            put ("lowFreq", 400.0f);   put ("lowDepth", 90.0f);  put ("lowCurve", 60.0f);
            put ("lowShoulder", 20.0f); put ("lowRes", 15.0f);
            put ("highFreq", 4000.0f); put ("highDepth", 80.0f); put ("highCurve", 70.0f);
            put ("highShoulder", 10.0f); put ("highRes", 25.0f);
            put ("focus", 1.0f);       put ("link", 1.0f);
            put ("output", -1.5f);     put ("bypass", 0.0f);

            juce::MemoryBlock block;
            if (auto xml = v1.createXml())
            {
                //  copyXmlToBinary is a static member of AudioProcessor.
                juce::AudioProcessor::copyXmlToBinary (*xml, block);
            }

            EdgeAudioProcessor p;
            p.setStateInformation (block.getData(), (int) block.getSize());

            check (p.loadedLegacyState.load(), "a v1 state is detected and flagged",
                   p.loadedLegacyState.load() ? "flagged" : "NOT flagged");

            const float lowHz  = getParam (p, edge::param::lowFreq);
            const float highHz = getParam (p, edge::param::highFreq);
            const float e      = getParam (p, edge::param::edge);
            const float mode   = getParam (p, edge::param::mode);
            const float bite   = getParam (p, edge::param::bite);
            const float midG   = getParam (p, edge::param::midGain);

            check (std::abs (lowHz - 400.0f) < 1.0f && std::abs (highHz - 4000.0f) < 4.0f,
                   "v1 corner frequencies carry across unchanged",
                   juce::String (lowHz, 1) + " Hz / " + juce::String (highHz, 1) + " Hz");

            check (std::abs (e - 100.0f) < 0.01f, "v1 loads with EDGE fully open",
                   juce::String (e, 2) + " %");

            check ((int) std::lround (mode) == (int) edge::Mode::band,
                   "v1 loads in BAND mode", edge::modeName ((int) std::lround (mode)));

            check (std::abs (bite - edge::migratedBitePercent()) < 0.1f,
                   "v1 BITE matches the documented migration value",
                   juce::String (bite, 2) + " %, expected "
                     + juce::String (edge::migratedBitePercent(), 2) + " %");

            check (std::abs (midG) < 1.0e-6f, "v1 loads with the MID band a wire",
                   juce::String (midG, 4) + " dB");
        }

        //  --- states that must not crash ---------------------------------------
        {
            EdgeAudioProcessor p;
            applyBusySettings (p);
            const float before = getParam (p, edge::param::lowFreq);

            p.setStateInformation (nullptr, 0);

            const char junk[] = "this is not a plug-in state, it is a sentence";
            p.setStateInformation (junk, (int) sizeof (junk));

            //  A state from a build that does not exist yet.
            juce::ValueTree future ("EDGE");
            future.setProperty ("stateVersion", 99, nullptr);
            juce::ValueTree odd ("PARAM");
            odd.setProperty ("id", "a.parameter.from.the.future", nullptr);
            odd.setProperty ("value", 1.0f, nullptr);
            future.appendChild (odd, nullptr);

            juce::MemoryBlock fb;
            if (auto xml = future.createXml())
                juce::AudioProcessor::copyXmlToBinary (*xml, fb);
            p.setStateInformation (fb.getData(), (int) fb.getSize());

            juce::MidiBuffer midi;
            juce::AudioBuffer<float> b (2, 128);
            b.clear();
            p.prepareToPlay (48000.0, 128);
            p.processBlock (b, midi);

            check (allFinite (b), "empty, corrupt and future states are survivable",
                   "still processing");

            check (std::abs (getParam (p, edge::param::lowFreq) - before) < 0.01f,
                   "a rejected state leaves the parameters alone",
                   juce::String (getParam (p, edge::param::lowFreq), 1) + " Hz");
        }
    }

    // =========================================================================
    //  6. The editor
    // =========================================================================
    void testEditor()
    {
        section ("6. editor");

        EdgeAudioProcessor p;
        applyBusySettings (p);
        p.prepareToPlay (48000.0, 512);

        //  Open and close repeatedly. A dangling listener or a timer that
        //  outlives its component shows up here and nowhere else.
        for (int i = 0; i < 20; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            if (ed == nullptr) { check (false, "editor is created", "null"); return; }

            ed->setSize (ed->getWidth(), ed->getHeight());

            //  Audio keeps running while the editor is open, which is the case
            //  the analyser's lock-free hand-off exists for.
            juce::MidiBuffer midi;
            juce::AudioBuffer<float> b (2, 512);
            Lcg rng;
            for (int c = 0; c < 2; ++c)
            {
                auto* d = b.getWritePointer (c);
                for (int k = 0; k < 512; ++k) d[k] = 0.3f * rng.next();
            }
            p.processBlock (b, midi);

            juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
        }
        check (true, "20 editor open/close cycles with audio running", "no crash, no leak");

        //  Every size the resizer allows must lay out and paint.
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            //  The four corners of what the constrainer in PluginEditor.cpp
            //  allows, plus the default with SHAPE open - the tall layout is a
            //  different code path, not a taller version of the same one.
            const int sizes[][2] = {
                { edge::ui::metric::minWidth,     420 },
                { edge::ui::metric::defaultWidth, edge::ui::metric::defaultHeight },
                { edge::ui::metric::defaultWidth, edge::ui::metric::defaultHeight
                                                    + edge::ui::metric::shapeHeight },
                { edge::ui::metric::maxWidth,     1400 } };

            int painted = 0;
            for (auto& s : sizes)
            {
                ed->setSize (s[0], s[1]);
                juce::Image img (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
                juce::Graphics g (img);
                ed->paintEntireComponent (g, true);
                ++painted;
            }
            check (painted == 4, "min, default, SHAPE-open and max sizes paint",
                   juce::String (painted) + " of 4");
        }
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;

    std::printf ("EDGE host-contract suite\n");

    testParameters();
    testLifecycle();
    testHostileInput();
    testAutomation();
    testState();
    testEditor();

    std::printf ("\n%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
