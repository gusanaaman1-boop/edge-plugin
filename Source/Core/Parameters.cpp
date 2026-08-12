#include <cmath>
#include <functional>
#include <memory>
#include <utility>

#include "Parameters.h"

namespace edge
{
    namespace
    {
        juce::String hzText (float v, int)
        {
            if (v >= 1000.0f)
                return juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz";

            return juce::String (v, v < 100.0f ? 1 : 0) + " Hz";
        }

        float hzValue (const juce::String& t)
        {
            auto s = t.trim().toLowerCase();
            const bool kilo = s.contains ("k");
            return s.getFloatValue() * (kilo ? 1000.0f : 1.0f);
        }

        //  Depth reads out in the unit that matters - dB of attenuation - not
        //  in percent, so what the knob says is what the curve does.
        juce::String depthText (float percent, int)
        {
            const float db = depthPercentToDb (percent);
            if (db <= kDepthFloorDb + 0.5f)
                return "CUT";

            return juce::String (db, 1) + " dB";
        }

        //  Three controls below read out in a unit that is NOT their own
        //  parameter range, so each one needs its inverse spelled out. Without
        //  it JUCE's default parser reads the printed number as a percentage:
        //  "-12.0 dB" of Depth became 0 %, and "24 dB/oct" of Curve became
        //  24 % - a gentler filter than the one the read-out named.
        float depthValue (const juce::String& t)
        {
            if (t.trim().equalsIgnoreCase ("CUT"))
                return 100.0f;

            return depthDbToPercent (t.getFloatValue());
        }

        //  Exactly what the display's slope combo shows, from the same table.
        juce::String curveText (float percent, int) { return slopeTextFor (percent); }
        float curveValue (const juce::String& t) { return curvePercentForText (t); }

        juce::String shoulderText (float percent, int)
        {
            if (percent < 0.5f)
                return "OFF";

            return juce::String (shoulderPercentToDb (percent), 1) + " dB";
        }

        float shoulderValue (const juce::String& t)
        {
            if (t.trim().equalsIgnoreCase ("OFF"))
                return 0.0f;

            return shoulderDbToPercent (t.getFloatValue());
        }

        //  NOT juce::String (v, 0): JUCE only applies the precision when it is
        //  ABOVE zero, so 0 falls through to the stream default and prints six
        //  significant digits - "13.0016 %" where the knob means 13.
        juce::String percentText (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
        juce::String dbText (float v, int)      { return juce::String (v, 1) + " dB"; }

        juce::String signedPercentText (float v, int)
        {
            if (std::abs (v) < 0.5f)
                return "0";

            return (v > 0.0f ? "+" : "") + juce::String (juce::roundToInt (v)) + " %";
        }

        juce::String msText (float v, int)
        {
            if (v < 10.0f)  return juce::String (v, 2) + " ms";
            if (v < 100.0f) return juce::String (v, 1) + " ms";
            return juce::String (juce::roundToInt (v)) + " ms";
        }
    }

    juce::NormalisableRange<float> frequencyRange (float minHz, float maxHz, float centreHz)
    {
        juce::NormalisableRange<float> r { minHz, maxHz };
        r.setSkewForCentre (centreHz);
        r.interval = 0.0f;
        return r;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using Attr = juce::AudioParameterFloatAttributes;
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        auto addFloat = [&layout] (const char* id, const juce::String& name,
                                   juce::NormalisableRange<float> range, float def,
                                   std::function<juce::String (float, int)> toText,
                                   std::function<float (const juce::String&)> fromText = {})
        {
            auto attr = Attr().withStringFromValueFunction (std::move (toText));
            if (fromText)
                attr = attr.withValueFromStringFunction (std::move (fromText));

            layout.add (std::make_unique<juce::AudioParameterFloat> (
                juce::ParameterID { id, kStateVersion }, name, range, def, attr));
        };

        //  --- LOW target ------------------------------------------------------
        //  Knob centre at 120 Hz: half the control lives below it, which is
        //  where a bass-end filter is actually used.
        addFloat (param::lowFreq, "Low Freq",
                  frequencyRange (kLowFreqMin, kLowFreqMax, 120.0f), 250.0f, hzText, hzValue);
        //  Depth defaults to CUT on both sides so that selecting BAND and
        //  opening EDGE immediately produces a real band-pass.
        addFloat (param::lowDepth,    "Low Depth",    { 0.0f, 100.0f }, 100.0f, depthText, depthValue);
        addFloat (param::lowCurve,    "Low Curve",    { 0.0f, 100.0f },  75.0f, curveText, curveValue);
        addFloat (param::lowShoulder, "Low Shoulder", { 0.0f, 100.0f },   0.0f, shoulderText, shoulderValue);
        addFloat (param::lowReso,     "Low Reso",     { 0.0f, 100.0f },   0.0f, percentText);

        //  --- HIGH target -----------------------------------------------------
        addFloat (param::highFreq, "High Freq",
                  frequencyRange (kHighFreqMin, kHighFreqMax, 3000.0f), 6000.0f, hzText, hzValue);
        addFloat (param::highDepth,    "High Depth",    { 0.0f, 100.0f }, 100.0f, depthText, depthValue);
        addFloat (param::highCurve,    "High Curve",    { 0.0f, 100.0f },  75.0f, curveText, curveValue);
        addFloat (param::highShoulder, "High Shoulder", { 0.0f, 100.0f },   0.0f, shoulderText, shoulderValue);
        addFloat (param::highReso,     "High Reso",     { 0.0f, 100.0f },   0.0f, percentText);

        //  --- MID band --------------------------------------------------------
        addFloat (param::midFreq, "Mid Freq",
                  frequencyRange (kMidFreqMin, kMidFreqMax, 800.0f), 1000.0f, hzText, hzValue);
        addFloat (param::midGain, "Mid Gain",
                  { -kMidMaxGainDb, kMidMaxGainDb, 0.0f }, 0.0f, dbText);
        addFloat (param::midReso, "Mid Reso", { 0.0f, 100.0f }, 40.0f, percentText);

        //  --- performance -----------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { param::mode, kStateVersion }, "Mode",
            juce::StringArray { "LP", "BAND", "HP", "FREE" }, (int) Mode::band));

        addFloat (param::edge,   "Edge",   { 0.0f, 100.0f }, 0.0f, percentText);
        addFloat (param::follow, "Follow", { -100.0f, 100.0f }, 0.0f, signedPercentText);
        addFloat (param::spread, "Spread", { -100.0f, 100.0f }, 0.0f, signedPercentText);
        addFloat (param::bite,   "Bite",   { 0.0f, 100.0f }, 35.0f, percentText);
        addFloat (param::output, "Output", { -24.0f, 24.0f, 0.0f }, 0.0f, dbText);

        //  JUCE prints a bool as "On"/"Off" but parses text with getIntValue,
        //  so a host's generic editor - and the plug-in's own text box - read
        //  the word "On" back as 0. Spell out the inverse.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { param::bypass, kStateVersion }, "Bypass", false,
            juce::AudioParameterBoolAttributes().withValueFromStringFunction (
                [] (const juce::String& t)
                {
                    const auto s = t.trim();
                    return s.equalsIgnoreCase ("on")   || s.equalsIgnoreCase ("yes")
                        || s.equalsIgnoreCase ("true") || s.getIntValue() != 0;
                })));

        //  CHARACTER: which of the two hidden engines BITE drives. Two entries,
        //  and it stays two - it is a voicing, not a model browser, and nothing
        //  underneath it is exposed.
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { param::character, kStateVersion }, "Character",
            juce::StringArray { "WARM", "IRON" }, (int) Character::warm));

        //  --- follow setup, inside SHAPE --------------------------------------
        addFloat (param::followSens, "Follow Sens", { -60.0f, 0.0f }, -12.0f, dbText);

        {
            juce::NormalisableRange<float> atk { 0.1f, 200.0f };
            atk.setSkewForCentre (10.0f);
            addFloat (param::followAttack, "Follow Attack", atk, 10.0f, msText);

            juce::NormalisableRange<float> rel { 5.0f, 2000.0f };
            rel.setSkewForCentre (150.0f);
            addFloat (param::followRelease, "Follow Release", rel, 150.0f, msText);
        }

        return layout;
    }

    EdgeEngine::Settings readSettings (const juce::AudioProcessorValueTreeState& state) noexcept
    {
        auto get = [&state] (const char* id)
        {
            auto* p = state.getRawParameterValue (id);
            return p != nullptr ? p->load() : 0.0f;
        };

        EdgeEngine::Settings s;
        s.lowFreqHz           = get (param::lowFreq);
        s.lowDepthPercent     = get (param::lowDepth);
        s.lowCurvePercent     = get (param::lowCurve);
        s.lowShoulderPercent  = get (param::lowShoulder);
        s.lowResPercent       = get (param::lowReso);

        s.highFreqHz          = get (param::highFreq);
        s.highDepthPercent    = get (param::highDepth);
        s.highCurvePercent    = get (param::highCurve);
        s.highShoulderPercent = get (param::highShoulder);
        s.highResPercent      = get (param::highReso);

        s.midFreqHz     = get (param::midFreq);
        s.midGainDb     = get (param::midGain);
        s.midResPercent = get (param::midReso);

        s.mode          = (int) get (param::mode);
        s.edgePercent   = get (param::edge);
        s.followPercent = get (param::follow);
        s.spreadPercent = get (param::spread);
        s.bitePercent   = get (param::bite);
        s.character     = (int) get (param::character);
        s.outputDb      = get (param::output);
        s.bypass        = get (param::bypass) > 0.5f;

        s.followSensDb    = get (param::followSens);
        s.followAttackMs  = get (param::followAttack);
        s.followReleaseMs = get (param::followRelease);
        return s;
    }
}
