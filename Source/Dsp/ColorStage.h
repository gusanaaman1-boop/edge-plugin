// EDGE's hidden colour stage.
//
// It owns exactly one thing the project already had: FOUR COLOR's WARM engine,
// vendored unmodified in Source/Vendor. Nothing about it is exposed - no
// parameter, no automation lane, no menu, no panel. Its drive is a pure
// function of how hard the FILTER is working, computed in EdgeEngine.
//
// Why not reuse FOUR COLOR's NonlinearStage, which already wraps the engines:
// it exists to serve a user-facing Quality control with four pre-allocated
// equiripple oversamplers, and it reports a constant 65 samples of latency to
// pay for switching between them safely. EDGE has no Quality control and is
// specified as minimum-latency, so it wraps the engine itself. The engine is
// untouched and is driven only through its published API.
//
// Oversampling factor is a runtime choice so the aliasing measurement, not a
// guess, decides whether EDGE pays for it. See docs/COLOUR-PLACEMENT.md.

#pragma once

#include <array>

#include <juce_dsp/juce_dsp.h>

#include "../Vendor/FourColor/Dsp/ColorEngine.h"

namespace edge
{
    class ColorStage
    {
    public:
        ColorStage() = default;

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset() noexcept;

        //  0..100, straight into ColorEngine::setDrive. At 0 the engine's own
        //  blend() returns the input bit for bit; below 5 it fades out on a
        //  smoothstep. That contract is what makes EDGE's neutral state exact.
        void setDrive (float drivePercent) noexcept;

        //  Where the colour sits in the spectrum. WARM does not read it today,
        //  but the engine base class does, and handing it a truthful context
        //  costs nothing and keeps a later engine swap honest.
        void setSpectrum (float lowHz, float highHz) noexcept;

        void process (juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

        //  1 (no oversampling, zero latency) or 2. Must be called before
        //  prepare(); changing it later is not audio-thread safe and EDGE never
        //  does it at runtime.
        void setOversamplingFactor (int factor) noexcept { osFactor = factor == 2 ? 2 : 1; }
        int getOversamplingFactor() const noexcept { return osFactor; }

        //  Base-rate latency of this stage. 0 at 1x.
        float getLatencySamples() const noexcept { return latency; }

        float getCompensationGain() const noexcept { return engines[0]->getCompensationGain(); }

        //  The engine's own drive-engage factor, 0 below drive 5 % and 1 above.
        float getEngageFactor() const noexcept { return engines[0]->getEngageTarget(); }

        //  The measured small-signal gain at the current drive, BEFORE the trim
        //  below cancels it. The drawn curve needs it to model what the colour
        //  stage does to the response; see EdgeShape.
        float getMeasuredGain() const noexcept { return measuredGain; }

        //  The vendored WarmEngine puts a fourcolor::dsp::DcBlocker after its
        //  shaper, at that struct's default 10 Hz. It is inside the wet leg of
        //  blend(), so once the engine is engaged the whole signal passes it -
        //  a real -0.46 dB at 30 Hz that the response curve has to show.
        //  Modelled rather than measured, and test 2 fails if the model is ever
        //  wrong, which is what keeps it honest if FOUR COLOR changes it.
        static constexpr float kDcBlockerHz = 10.0f;

        //  Linear gain that cancels the SMALL-SIGNAL level change the engine
        //  introduces at the current drive. 1.0 exactly at drive 0.
        //
        //  ColorEngine's own make-up matches the RMS of a -12 dBFS sine through
        //  its curve. That is the right normalisation for a saturator with a
        //  Drive knob, but it is not unity for quiet material: measured on WARM
        //  at EDGE's maximum drive the small-signal gain is well above 1, which
        //  would read as "Depth makes the track louder". The work order allows a
        //  static correction for exactly this, so ColorStage measures the
        //  engine's own small-signal gain at prepare() - through the public API,
        //  with a probe engine, never assuming a formula - and hands the
        //  reciprocal to EdgeEngine, which folds it into the per-sample output
        //  gain.
        float getLevelTrimGain() const noexcept { return levelTrim; }

        //  The measured table, for the tests. index i corresponds to
        //  drive = 100 * i / (kTrimPoints - 1).
        static constexpr int kTrimPoints = 33;
        float getMeasuredSmallSignalGain (int index) const noexcept
        {
            return trimTable[(size_t) juce::jlimit (0, kTrimPoints - 1, index)];
        }

    private:
        void buildTrimTable();

        //  ONE ENGINE PER CHANNEL, deliberately.
        //
        //  ColorEngine keeps its per-channel filter state in an array indexed by
        //  the channel argument, but its drive-engage SmoothedValue is a single
        //  member advanced once per sample by blend(). Processing L then R on
        //  one instance therefore hands the two channels different points on the
        //  engage ramp whenever drive is moving - measured here as 0.014 of
        //  L/R divergence on identical input, which is a stereo image that
        //  wanders while Depth is automated. The engine is not ours to change,
        //  so EDGE gives each channel its own copy. Coefficients are identical
        //  because setDrive and setContext are applied to both.
        std::unique_ptr<fourcolor::ColorEngine> engines[2];
        std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;

        double baseRate = 48000.0;
        int channels = 2;
        int osFactor = 1;
        float latency = 0.0f;
        float drivePercent = 0.0f;
        float levelTrim = 1.0f;
        float measuredGain = 1.0f;
        std::array<float, (size_t) kTrimPoints> trimTable {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ColorStage)
    };
}
