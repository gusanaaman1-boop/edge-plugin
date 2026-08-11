// EDGE's hidden colour stage.
//
// It owns exactly one thing the project already had: FOUR COLOR's colour
// engines, vendored unmodified in Source/Vendor. Nothing about them is exposed
// except BITE (how much) and CHARACTER (which of two voicings) - no drive, no
// bias, no mix, no oversampling, no panel.
//
// Why not reuse FOUR COLOR's NonlinearStage, which already wraps the engines:
// it exists to serve a user-facing Quality control with four pre-allocated
// equiripple oversamplers, and it reports a constant 65 samples of latency to
// pay for switching between them safely. EDGE has no Quality control and is
// specified as minimum-latency, so it wraps the engines itself.
//
// ONE ENGINE INSTANCE PER CHANNEL, PER CHARACTER, all built at prepare().
// ColorEngine keeps its per-channel filter state in an array indexed by the
// channel argument, but its drive-engage SmoothedValue is a single member
// advanced once per sample by blend(). Processing L then R on one instance
// therefore hands the two channels different points on the engage ramp -
// measured as 0.014 of L/R divergence on identical input. The engine is not
// ours to change, so each channel gets its own copy.

#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

#include "../Core/ParameterIds.h"
#include "../Vendor/FourColor/Dsp/ColorEngine.h"

namespace edge
{
    class ColorStage
    {
    public:
        ColorStage() = default;

        static constexpr int numCharacters = kNumCharacters;
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset() noexcept;

        //  0..100, straight into ColorEngine::setDrive. At 0 the engine's own
        //  blend() returns the input bit for bit; below 5 it fades out on a
        //  smoothstep. That contract is what makes EDGE's neutral state exact.
        void setDrive (float drivePercent) noexcept;

        //  Audio-thread safe: starts a short equal-gain crossfade if the
        //  character actually changed. Both characters are already built and
        //  already running, so nothing is allocated and nothing is reset.
        void setCharacter (int character) noexcept;
        int getCharacter() const noexcept { return activeCharacter; }
        bool isCrossfading() const noexcept { return fadeLeft > 0; }

        //  Where the colour sits in the spectrum. WARM ignores it; IRON derives
        //  its core-loss corner from it, which is exactly why it is passed.
        void setSpectrum (float lowHz, float highHz) noexcept;

        void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

        //  1 (no oversampling, zero latency) or 2. Must be called before
        //  prepare(); EDGE never changes it at runtime.
        void setOversamplingFactor (int factor) noexcept { osFactor = factor == 2 ? 2 : 1; }
        int getOversamplingFactor() const noexcept { return osFactor; }

        float getLatencySamples() const noexcept { return latency; }

        //  The engine's own drive-engage factor, 0 below drive 5 % and 1 above.
        float getEngageFactor() const noexcept;

        //  Linear gain that cancels the SMALL-SIGNAL level change the engine
        //  introduces at the current drive. 1.0 exactly at drive 0.
        //
        //  ColorEngine's own make-up matches the RMS of a -12 dBFS sine through
        //  its curve. That is the right normalisation for a saturator with a
        //  Drive knob, but it is not unity for quiet material, which would read
        //  as "opening EDGE makes the track louder". ColorStage measures each
        //  character's own small-signal gain at prepare() - through the public
        //  API, with a probe engine, never assuming a formula - and hands the
        //  reciprocal to EdgeEngine, which folds it into the output gain.
        float getLevelTrimGain() const noexcept { return levelTrim; }
        float getMeasuredGain() const noexcept { return measuredGain; }

        //  The vendored engines put a fourcolor::dsp::DcBlocker after their
        //  shaper, at that struct's default 10 Hz. It is inside the wet leg of
        //  blend(), so once an engine is engaged the whole signal passes it -
        //  a real -0.46 dB at 30 Hz that the response curve has to show.
        static constexpr float kDcBlockerHz = 10.0f;

        static constexpr int kTrimPoints = 33;
        float getMeasuredSmallSignalGain (int character, int index) const noexcept
        {
            return trimTable[(size_t) juce::jlimit (0, numCharacters - 1, character)]
                            [(size_t) juce::jlimit (0, kTrimPoints - 1, index)];
        }

    private:
        void buildTrimTable (int character);
        void runEngines (int character, float* const* data, int numChannels, int n) noexcept;

        //  [character][channel]
        std::unique_ptr<fourcolor::ColorEngine> engines[numCharacters][maxChannels];
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

        //  Scratch for the outgoing character during a crossfade.
        juce::AudioBuffer<float> fadeScratch;

        double baseRate = 48000.0;
        int channels = 2;
        int osFactor = 1;
        float latency = 0.0f;
        float drivePercent = 0.0f;
        float levelTrim = 1.0f;
        float measuredGain = 1.0f;

        int activeCharacter = (int) Character::warm;
        int fadingFrom = (int) Character::warm;
        int fadeLeft = 0;
        int fadeLength = 1;

        std::array<std::array<float, (size_t) kTrimPoints>, (size_t) numCharacters> trimTable {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorStage)
    };
}
