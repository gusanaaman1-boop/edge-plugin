// The whole of EDGE's audio path.
//
//   in -> COLOUR (hidden) -> LOW EDGE -> HIGH EDGE -> Output -> out
//
// Coefficients are recomputed every kAutomationChunk samples, which is what
// makes a 20 Hz -> 20 kHz automation sweep smooth without paying for
// per-sample coefficient maths. Gain-like quantities (Output, the bypass fade)
// are smoothed PER SAMPLE, because a 32-sample step of a 24 dB Output ramp is
// audible zipper while a 32-sample step of a cutoff on a TPT filter is not.

#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "ColorStage.h"
#include "EdgeUnit.h"

namespace edge
{
    //  Focus and the minimum-separation rule, in the log-frequency domain.
    //  Free function so the editor's curve and the audio path cannot disagree.
    //
    //  focus in [-1, +1]: positive brings the edges together. Applied in
    //  octaves, and soft-limited so that reaching a range boundary slows Focus
    //  down instead of stopping it dead.
    void resolveFrequencies (float lowHz, float highHz, float focus,
                             float& outLowHz, float& outHighHz) noexcept;

    //  Everything the response curve depends on, with the frequencies ALREADY
    //  resolved. The editor fills this in, calls magnitudeDb(), and gets the
    //  same number the audio path produces.
    struct EdgeShape
    {
        float lowHz = 20.0f,    lowDepthDb = 0.0f,  lowCurve01 = 0.5f,  lowRes01 = 0.0f;
        float highHz = 20000.0f, highDepthDb = 0.0f, highCurve01 = 0.5f, highRes01 = 0.0f;
        float lowShoulderDb = 0.0f, highShoulderDb = 0.0f;
        float outputDb = 0.0f;

        //  The colour stage's LINEAR contribution. It has no EQ curve of its
        //  own and gets no panel, but it is not invisible either: once the
        //  engine engages, its internal DC blocker high-passes the whole signal
        //  at 10 Hz, which is a real -0.46 dB at 30 Hz. Drawing 0 dB there
        //  would make the curve a lie, so the response includes it.
        //
        //  colourEngage  - the engine's own 0..1 engage factor
        //  colourGain    - its measured small-signal gain at the current drive
        //                  (the reciprocal is applied as EDGE's output trim, so
        //                  the two cancel in the passband)
        float colourEngage = 0.0f;
        float colourGain = 1.0f;
    };

    double magnitudeDb (const EdgeShape& shape, double sampleRate, double freqHz) noexcept;

    //  Depth control (0..100 %) -> dB of attenuation. Piecewise linear in dB
    //  through the perceptual table in the work order, so the labelled values
    //  land exactly where they are supposed to.
    float depthPercentToDb (float percent) noexcept;

    //  Shoulder control (0..100 %) -> dB, linear. Unlike Depth this one has no
    //  cut at the end of its travel, so a plain ramp is the honest mapping.
    float shoulderPercentToDb (float percent) noexcept;

    //  The Curve percentages that land on a whole dB/oct slope.
    //
    //  12 / 24 / 36 and no odd slopes, because those are the ones the cascade
    //  ACTUALLY delivers. At full Depth every section with a non-zero share of
    //  the dB becomes a full second-order cut, so the asymptotic slope is
    //  12 x (number of active sections) - an integer. P = 1.5 does not give
    //  18 dB/oct, it gives two active sections carrying unequal shares, and it
    //  measures 23.5. Offering "18 dB/oct" would be a label that lies.
    //
    //  6, 18 and 30 need an additional FIRST-order section in the cascade.
    //  That is a real feature and a small one, but it is not free: a one-pole
    //  stage switching in and out with the parity of the order is a topology
    //  change on a control that can be automated. Left for a follow-up.
    //
    //  SOFT is the other end of the control - the widest knee, a voicing
    //  rather than a slope, so it is named instead of numbered.
    //
    //  Lives here rather than in the UI because three different things read it
    //  (the knob's readout, the display's combo box, the tests) and they must
    //  not be able to disagree about what "24 dB/oct" means.
    struct SlopeChoice { const char* name; float curvePercent; };
    inline constexpr int kNumSlopeChoices = 4;
    extern const SlopeChoice kSlopeChoices[kNumSlopeChoices];

    //  Index of the matching entry, or -1 when Curve is between two of them.
    int slopeIndexFor (float curvePercent) noexcept;

    //  What that Curve percentage should read as, snapped or not.
    juce::String slopeTextFor (float curvePercent);

    //  The hidden colour law, exposed only so the tests can assert its shape.
    float colourDrivePercent (float lowActivity, float highActivity,
                              float lowResonance, float highResonance) noexcept;

    class EdgeEngine
    {
    public:
        EdgeEngine() = default;

        static constexpr int kAutomationChunk = 32;

        struct Settings
        {
            float lowFreqHz = 20.0f,    lowDepthPercent = 0.0f,
                  lowCurvePercent = 50.0f, lowResPercent = 0.0f,
                  lowShoulderPercent = 0.0f;
            float highFreqHz = 20000.0f, highDepthPercent = 0.0f,
                  highCurvePercent = 50.0f, highResPercent = 0.0f,
                  highShoulderPercent = 0.0f;
            float focus = 0.0f;        // -1..+1
            float outputDb = 0.0f;
            bool  bypass = false;
        };

        //  Where the hidden colour sits relative to the filters.
        //
        //  This is NOT a parameter and never will be: it has no ID, no
        //  automation lane and no UI. It exists because stage 4 of the work
        //  order asks for pre / post to be COMPARED, and an argument is not a
        //  comparison. Ships locked to `pre`; the test suite is the only caller
        //  that ever moves it. (The third candidate, inside the resonance
        //  feedback path, is not implementable here at all - see
        //  docs/DSP-TOPOLOGY.md section 6.)
        enum class ColourPlacement { pre, post };
        void setColourPlacement (ColourPlacement p) noexcept { placement = p; }

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset() noexcept;

        //  Block rate. Sets smoother targets only; no coefficient work here.
        void setSettings (const Settings& s) noexcept;

        //  Skips every smoother to its target. For preset recall and for the
        //  first block after prepare(), so a loaded project does not sweep in.
        void snapToSettings (const Settings& s) noexcept;

        void process (juce::AudioBuffer<float>& buffer) noexcept;

        float getLatencySamples() const noexcept { return colour.getLatencySamples(); }
        int getOversamplingFactor() const noexcept { return colour.getOversamplingFactor(); }
        void setOversamplingFactor (int f) noexcept { colour.setOversamplingFactor (f); }

        //  Live, resolved shape for the editor. Written on the audio thread,
        //  read on the message thread; each field is a plain float and a torn
        //  read only ever costs one stale frame of a curve.
        EdgeShape getDisplayShape() const noexcept;

        //  Diagnostics for the test suite.
        float getColourDrivePercent() const noexcept { return lastColourDrive.load (std::memory_order_relaxed); }
        float getResonanceTrimDb() const noexcept { return lastResTrimDb.load (std::memory_order_relaxed); }

        //  Broadband dB the colour engine's measured level trim is currently
        //  contributing. The drawn curve adds it, so what is drawn is what is
        //  heard even though the colour has no curve of its own.
        float getColourTrimDb() const noexcept
        {
            return juce::Decibels::gainToDecibels (colour.getLevelTrimGain());
        }

    private:
        void applyChunkShape (int chunkLength) noexcept;

        //  Output = user's Output + the resonance make-up + the colour engine's
        //  measured small-signal level change. All three are static functions of
        //  the parameter state, all three are smoothed per sample.
        void updateOutputTarget() noexcept;

        EdgeUnit<Side::low>  lowEdge;
        EdgeUnit<Side::high> highEdge;
        ColorStage colour;

        ColourPlacement placement = ColourPlacement::pre;

        double rate = 48000.0;
        int channels = 2;
        int maxBlock = 512;

        juce::SmoothedValue<float> logLowFreq, logHighFreq;
        juce::SmoothedValue<float> lowDepthDb, highDepthDb;
        juce::SmoothedValue<float> lowCurve, highCurve;
        juce::SmoothedValue<float> lowRes, highRes;
        juce::SmoothedValue<float> lowShoulder, highShoulder;
        juce::SmoothedValue<float> focusSmoothed;
        juce::SmoothedValue<float> colourDrive;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
        juce::SmoothedValue<float> bypassFade;

        float pendingOutputDb = 0.0f;
        float resonanceTrimDb = 0.0f;

        juce::AudioBuffer<float> dryBuffer;
        juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay;

        std::atomic<float> lastColourDrive { 0.0f };
        std::atomic<float> lastResTrimDb { 0.0f };
        std::atomic<float> dispLowHz { 20.0f },  dispLowDepthDb { 0.0f };
        std::atomic<float> dispLowCurve { 0.5f }, dispLowRes { 0.0f };
        std::atomic<float> dispHighHz { 20000.0f }, dispHighDepthDb { 0.0f };
        std::atomic<float> dispHighCurve { 0.5f },  dispHighRes { 0.0f };
        std::atomic<float> dispOutputDb { 0.0f };
        std::atomic<float> dispColourEngage { 0.0f }, dispColourGain { 1.0f };
        std::atomic<float> dispLowShoulderDb { 0.0f }, dispHighShoulderDb { 0.0f };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeEngine)
    };
}
