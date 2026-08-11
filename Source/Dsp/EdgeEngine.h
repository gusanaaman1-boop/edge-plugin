// The whole of EDGE's audio path.
//
//   in -> COLOUR (hidden) -> LOW EDGE -> HIGH EDGE -> Output -> out
//
// Coefficients are recomputed every kAutomationChunk samples, which is what
// makes a 20 Hz -> 20 kHz automation sweep smooth without paying for
// per-sample coefficient maths. Gain-like quantities (Output, the bypass fade)
// are smoothed PER SAMPLE, because a 32-sample step of a 24 dB Output ramp is
// audible zipper while a 32-sample step of a cutoff on a TPT filter is not.
//
// v0.2 adds four things on top of the v0.1 filter, none of which changes it:
//
//   MODE    an inactive edge has its Depth, Shoulder and Resonance driven to
//           zero, which makes it a bit-exact wire. No branch, no crossfade.
//   EDGE    one macro that walks both edges from open to their targets.
//   FOLLOW  a stereo-linked envelope follower modulating EDGE's position.
//   SPREAD  per-channel corner frequencies, in octaves, bandwidth preserved.

#pragma once

#include <atomic>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include "ColorStage.h"
#include "EdgeUnit.h"
#include "FollowDetector.h"

namespace edge
{
    //  Keep a small minimum separation between the two effective corners, and
    //  clamp both into their ranges. Focus is gone; this is what is left of it.
    void resolveSeparation (float& lowHz, float& highHz) noexcept;

    //  Everything the response curve depends on, with the frequencies ALREADY
    //  resolved. The editor fills this in, calls magnitudeDb(), and gets the
    //  same number the audio path produces.
    struct EdgeShape
    {
        float lowHz = kLowFreqMin,   lowDepthDb = 0.0f,  lowCurve01 = 0.5f,  lowRes01 = 0.0f;
        float highHz = kHighFreqMax, highDepthDb = 0.0f, highCurve01 = 0.5f, highRes01 = 0.0f;
        float lowShoulderDb = 0.0f, highShoulderDb = 0.0f;
        float outputDb = 0.0f;

        //  The colour stage's LINEAR contribution. It has no EQ curve of its
        //  own and gets no panel, but it is not invisible either: once the
        //  engine engages, its internal DC blocker high-passes the whole signal
        //  at 10 Hz, which is a real -0.46 dB at 30 Hz.
        float colourEngage = 0.0f;
        float colourGain = 1.0f;
    };

    double magnitudeDb (const EdgeShape& shape, double sampleRate, double freqHz) noexcept;

    //  Depth control (0..100 %) -> dB of attenuation. Piecewise linear in dB
    //  through the perceptual table, so the labelled values land exactly where
    //  they are supposed to.
    float depthPercentToDb (float percent) noexcept;

    //  Shoulder control (0..100 %) -> dB, linear.
    float shoulderPercentToDb (float percent) noexcept;

    //  The Curve percentages that land on a whole dB/oct slope. 12 / 24 / 36 and
    //  no odd slopes, because at full Depth every section carrying a share of
    //  the dB becomes a full second-order cut, so the slope is 12 x (active
    //  sections) - an integer. Curve 62.5 % measures 23.5 dB/oct, not 18.
    struct SlopeChoice { const char* name; float curvePercent; };
    inline constexpr int kNumSlopeChoices = 4;
    extern const SlopeChoice kSlopeChoices[kNumSlopeChoices];
    int slopeIndexFor (float curvePercent) noexcept;
    juce::String slopeTextFor (float curvePercent);

    //  BITE -> the two halves of the colour law. Exposed so the tests can
    //  assert the law rather than the audio it happens to produce.
    float biteMaxDrive (float bitePercent) noexcept;
    float biteGamma (float bitePercent) noexcept;
    float colourDrivePercent (float bitePercent, float activity) noexcept;

    class EdgeEngine
    {
    public:
        EdgeEngine() = default;

        static constexpr int kAutomationChunk = 32;
        static constexpr int maxChannels = 2;

        struct Settings
        {
            // targets
            float lowFreqHz = 250.0f,  lowDepthPercent = 100.0f,
                  lowCurvePercent = 75.0f, lowShoulderPercent = 0.0f, lowResPercent = 0.0f;
            float highFreqHz = 6000.0f, highDepthPercent = 100.0f,
                  highCurvePercent = 75.0f, highShoulderPercent = 0.0f, highResPercent = 0.0f;

            // performance
            int   mode = (int) Mode::band;
            float edgePercent = 0.0f;
            float followPercent = 0.0f;
            float spreadPercent = 0.0f;
            float bitePercent = 35.0f;
            float outputDb = 0.0f;
            bool  bypass = false;

            // follow setup
            float followSensDb = -12.0f;
            float followAttackMs = 10.0f;
            float followReleaseMs = 150.0f;
        };

        //  Where the hidden colour sits relative to the filters. NOT a
        //  parameter: no ID, no automation lane, no UI. It exists so pre / post
        //  can be measured rather than argued about. Ships locked to `pre`.
        enum class ColourPlacement { pre, post };
        void setColourPlacement (ColourPlacement p) noexcept { placement = p; }

        void prepare (double sampleRate, int maxBlockSize, int numChannels);
        void reset() noexcept;

        void setSettings (const Settings& s) noexcept;
        void snapToSettings (const Settings& s) noexcept;

        void process (juce::AudioBuffer<float>& buffer) noexcept;

        float getLatencySamples() const noexcept { return colour.getLatencySamples(); }
        int getOversamplingFactor() const noexcept { return colour.getOversamplingFactor(); }
        void setOversamplingFactor (int f) noexcept { colour.setOversamplingFactor (f); }

        //  --- lock-free display state ----------------------------------------
        //
        //  Written on the audio thread, read on the message thread. Each field
        //  is a plain float; a torn read costs one stale frame of a curve.

        //  The shape actually being applied, channel 0.
        EdgeShape getDisplayShape() const noexcept;

        //  The same, for one channel - so the editor can draw the two faint
        //  L/R traces when SPREAD is doing something.
        EdgeShape getDisplayShape (int channel) const noexcept;

        //  The TARGET shape: what EDGE at 100 % would produce. Drawn as the
        //  ghost curve.
        EdgeShape getTargetShape() const noexcept;

        //  EDGE's resolved position after FOLLOW, 0..1. This is what the big
        //  knob's ring shows moving when the follower is working.
        float getLiveEdge01() const noexcept { return dispLiveEdge.load (std::memory_order_relaxed); }
        float getFollowEnvelope01() const noexcept { return dispFollowEnv.load (std::memory_order_relaxed); }
        bool  isSpreadActive() const noexcept { return dispSpreadActive.load (std::memory_order_relaxed); }

        //  Is the colour engine actually producing anything - what the WARM
        //  lamp reads, rather than "BITE > 0".
        bool  isColourEngaged() const noexcept { return dispColourEngage.load (std::memory_order_relaxed) > 0.001f; }

        float getColourDrivePercent() const noexcept { return lastColourDrive.load (std::memory_order_relaxed); }
        float getResonanceTrimDb() const noexcept { return lastResTrimDb.load (std::memory_order_relaxed); }
        float getColourTrimDb() const noexcept
        {
            return juce::Decibels::gainToDecibels (colour.getLevelTrimGain());
        }

        //  --- analyzer feed ---------------------------------------------------
        //
        //  The editor turns this on when it opens and off when it closes; with
        //  it off the audio thread does not touch the FIFO at all.
        void setAnalyzerEnabled (bool shouldBeEnabled) noexcept
        {
            analyzerEnabled.store (shouldBeEnabled, std::memory_order_relaxed);
        }

        //  Mono output samples, most recent last. Returns how many were copied.
        int readAnalyzerSamples (float* dest, int maxSamples) noexcept;

    private:
        struct Resolved
        {
            float lowHz, lowDepthDb, lowCurve01, lowShoulderDb, lowRes01;
            float highHz, highDepthDb, highCurve01, highShoulderDb, highRes01;
        };

        void applyChunkShape (int chunkLength, float liveEdge01) noexcept;
        void updateOutputTarget() noexcept;
        Resolved resolveFor (float edge01, float channelOctaveOffset) const noexcept;
        void pushAnalyzer (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

        EdgeUnit<Side::low>  lowEdge;
        EdgeUnit<Side::high> highEdge;
        ColorStage colour;
        FollowDetector follower;

        ColourPlacement placement = ColourPlacement::pre;

        double rate = 48000.0;
        int channels = 2;
        int maxBlock = 512;

        //  Targets, smoothed. Frequencies are smoothed in LOG frequency so a
        //  sweep is perceptually even.
        juce::SmoothedValue<float> logLowFreq, logHighFreq;
        juce::SmoothedValue<float> lowDepth, highDepth;       // percent
        juce::SmoothedValue<float> lowCurve, highCurve;
        juce::SmoothedValue<float> lowShoulder, highShoulder; // percent
        juce::SmoothedValue<float> lowRes, highRes;
        juce::SmoothedValue<float> lowEnable, highEnable;     // MODE, 0..1
        juce::SmoothedValue<float> edgeBase;                  // 0..1
        juce::SmoothedValue<float> followAmount;              // -1..1
        juce::SmoothedValue<float> spreadOctaves;             // +/- octaves, per channel
        juce::SmoothedValue<float> bite;                      // percent
        juce::SmoothedValue<float> colourDrive;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
        juce::SmoothedValue<float> bypassFade;

        float pendingOutputDb = 0.0f;
        float resonanceTrimDb = 0.0f;

        juce::AudioBuffer<float> dryBuffer;

        //  --- analyzer FIFO ---------------------------------------------------
        static constexpr int kAnalyzerFifoSize = 1 << 14;
        std::atomic<bool> analyzerEnabled { false };
        juce::AbstractFifo analyzerFifo { kAnalyzerFifoSize };
        std::vector<float> analyzerBuffer;

        std::atomic<float> lastColourDrive { 0.0f };
        std::atomic<float> lastResTrimDb { 0.0f };
        std::atomic<float> dispLiveEdge { 0.0f };
        std::atomic<float> dispFollowEnv { 0.0f };
        std::atomic<bool>  dispSpreadActive { false };
        std::atomic<float> dispOutputDb { 0.0f };
        std::atomic<float> dispColourEngage { 0.0f }, dispColourGain { 1.0f };

        //  One published shape per channel, plus the target.
        struct DisplayShape
        {
            std::atomic<float> lowHz { kLowFreqMin }, lowDepthDb { 0.0f };
            std::atomic<float> lowCurve { 0.5f }, lowRes { 0.0f }, lowShoulderDb { 0.0f };
            std::atomic<float> highHz { kHighFreqMax }, highDepthDb { 0.0f };
            std::atomic<float> highCurve { 0.5f }, highRes { 0.0f }, highShoulderDb { 0.0f };

            void store (const Resolved& r) noexcept;
            void load (EdgeShape& s) const noexcept;
        };

        DisplayShape dispChannel[maxChannels];
        DisplayShape dispTarget;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeEngine)
    };
}
