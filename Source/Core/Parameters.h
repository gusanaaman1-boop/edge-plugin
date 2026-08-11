#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "ParameterIds.h"
#include "../Dsp/EdgeEngine.h"

namespace edge
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    //  Perceptual (log) frequency travel. A plain skew factor puts far too
    //  little of the control in the bass; skewFromMidPoint anchors the centre
    //  of the knob at a chosen frequency, which is how the low edge gets real
    //  resolution between 20 and 200 Hz.
    juce::NormalisableRange<float> frequencyRange (float minHz, float maxHz, float centreHz);

    EdgeEngine::Settings readSettings (const juce::AudioProcessorValueTreeState& state) noexcept;
}
