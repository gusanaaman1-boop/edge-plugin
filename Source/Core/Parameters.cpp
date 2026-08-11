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

            return juce::String (db, db > -10.0f ? 1 : 1) + " dB";
        }

        //  Exactly what the display's slope combo shows, from the same table.
        //  The knob used to say "NEUTRAL" where the combo said "12 dB/oct" -
        //  two names for one setting, on screen at the same time.
        juce::String curveText (float percent, int) { return slopeTextFor (percent); }

        juce::String percentText (float v, int) { return juce::String (v, 0) + " %"; }

        juce::String shoulderText (float percent, int)
        {
            if (percent < 0.5f)
                return "OFF";

            return juce::String (shoulderPercentToDb (percent), 1) + " dB";
        }
        juce::String dbText (float v, int)      { return juce::String (v, 1) + " dB"; }

        juce::String focusText (float v, int)
        {
            if (std::abs (v) < 0.5f)
                return "0";

            return (v > 0.0f ? "+" : "") + juce::String (v, 0)
                 + (v > 0.0f ? "  narrow" : "  wide");
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
                juce::ParameterID { id, 1 }, name, range, def, attr));
        };

        //  --- LOW EDGE --------------------------------------------------------
        //  Centre of the knob at 120 Hz: half the control lives below it, which
        //  is where a bass-end filter is actually used.
        addFloat (param::lowFreq, "Low Freq",
                  frequencyRange (kLowFreqMin, kLowFreqMax, 120.0f), kLowFreqMin,
                  hzText, hzValue);
        addFloat (param::lowDepth, "Low Depth",  { 0.0f, 100.0f }, 0.0f,  depthText);
        addFloat (param::lowCurve, "Low Curve",  { 0.0f, 100.0f }, 50.0f, curveText);
        addFloat (param::lowRes,   "Low Reso",   { 0.0f, 100.0f }, 0.0f,  percentText);

        //  --- HIGH EDGE -------------------------------------------------------
        addFloat (param::highFreq, "High Freq",
                  frequencyRange (kHighFreqMin, kHighFreqMax, 3000.0f), kHighFreqMax,
                  hzText, hzValue);
        addFloat (param::highDepth, "High Depth", { 0.0f, 100.0f }, 0.0f,  depthText);
        addFloat (param::highCurve, "High Curve", { 0.0f, 100.0f }, 50.0f, curveText);
        addFloat (param::highRes,   "High Reso",  { 0.0f, 100.0f }, 0.0f,  percentText);

        //  --- SHARED ----------------------------------------------------------
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { param::link, 1 }, "Link", false));

        addFloat (param::focus,  "Focus",  { -100.0f, 100.0f }, 0.0f, focusText);
        addFloat (param::output, "Output", { -24.0f, 24.0f, 0.0f }, 0.0f, dbText);

        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { param::bypass, 1 }, "Bypass", false));

        //  --- APPENDED after v0.1 ---------------------------------------------
        //  New parameters go at the end so an existing project's automation
        //  lanes keep pointing at the same controls.
        addFloat (param::lowShoulder,  "Low Shoulder",  { 0.0f, 100.0f }, 0.0f, shoulderText);
        addFloat (param::highShoulder, "High Shoulder", { 0.0f, 100.0f }, 0.0f, shoulderText);

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
        s.lowFreqHz        = get (param::lowFreq);
        s.lowDepthPercent  = get (param::lowDepth);
        s.lowCurvePercent  = get (param::lowCurve);
        s.lowResPercent    = get (param::lowRes);
        s.highFreqHz       = get (param::highFreq);
        s.highDepthPercent = get (param::highDepth);
        s.highCurvePercent = get (param::highCurve);
        s.highResPercent   = get (param::highRes);
        s.lowShoulderPercent  = get (param::lowShoulder);
        s.highShoulderPercent = get (param::highShoulder);
        s.focus            = get (param::focus) * 0.01f;
        s.outputDb         = get (param::output);
        s.bypass           = get (param::bypass) > 0.5f;
        return s;
    }
}
