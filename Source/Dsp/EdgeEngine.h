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
#include <cstddef>
#include <cstdint>
#include <vector>

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
        float midHz = 1000.0f, midGainDb = 0.0f, midReso01 = 0.4f;
        float outputDb = 0.0f;

        //  The colour stage's LINEAR contribution. It has no EQ curve of its
        //  own and gets no panel, but it is not invisible either: once the
        //  engine engages, its internal DC blocker high-passes the whole signal
        //  at 10 Hz, which is a real -0.46 dB at 30 Hz.
        float colourEngage = 0.0f;
        float colourGain = 1.0f;
    };

    double magnitudeDb (const EdgeShape& shape, double sampleRate, double freqHz) noexcept;

    //  Everything the display needs, as ONE coherent value.
    //
    //  The previous scheme published ~40 independent relaxed atomics and let
    //  the editor decide what had changed by hashing a subset of them - which
    //  is how a MID drag could leave a stale curve on screen until an unrelated
    //  control was touched. Now the audio thread publishes this struct as a
    //  unit, bumps `revision` only when the content actually changed, and the
    //  editor's whole question is "is your revision mine?".
    struct DisplaySnapshot
    {
        //  --- geometry: everything a response path is built from --------------
        //  `revision` advances only when THIS region changes, so the editor
        //  rebuilds its paths exactly when a curve moved and never because a
        //  meter ticked.
        EdgeShape target;          // EDGE at 100 % - the ghost curve, the handles
        EdgeShape currentCentre;   // now, without SPREAD - the solid curve
        EdgeShape currentLeft;     // now, per channel - the faint traces
        EdgeShape currentRight;

        float colourTrimDb = 0.0f;
        float resonanceTrimDb = 0.0f;
        float outputDb = 0.0f;
        int   mode = (int) Mode::band;
        bool  spreadActive = false;

        //  --- live meters: published coherently, excluded from revision -------
        float liveEdge01 = 0.0f;         // EDGE's resolved position after FOLLOW
        float followEnv01 = 0.0f;
        float freeTravelOctaves = 0.0f;
        float colourEngage01 = 0.0f;
        float colourGain = 1.0f;
        float colourDrivePercent = 0.0f;

        //  MUST stay the last member.
        std::uint64_t revision = 0;

        //  memcmp needs the padding deterministic: value-initialise, always.
        static constexpr std::size_t geometryBytes() noexcept
        {
            return offsetof (DisplaySnapshot, liveEdge01);
        }
    };

    //  Depth control (0..100 %) -> dB of attenuation. Piecewise linear in dB
    //  through the perceptual table, so the labelled values land exactly where
    //  they are supposed to.
    float depthPercentToDb (float percent) noexcept;
    float depthDbToPercent (float db) noexcept;

    //  Shoulder control (0..100 %) -> dB, linear.
    float shoulderPercentToDb (float percent) noexcept;
    float shoulderDbToPercent (float db) noexcept;

    //  MID Resonance (0..100 %) -> damping. Wide tilt to formant.
    float midResoToDamping (float percent) noexcept;

    //  The Curve percentages that land on a whole dB/oct slope: 12, 24, 36, 48
    //  and 72, one per pole pair. There are no ODD slopes, because at full
    //  Depth every section carrying a share of the dB becomes a full
    //  second-order cut, so the slope is 12 x (active sections) - an integer.
    //  Curve between two entries is a voicing, not a slope, and reads as a
    //  percentage rather than claiming a number it does not deliver.
    struct SlopeChoice { const char* name; float curvePercent; };
    inline constexpr int kNumSlopeChoices = 6;
    extern const SlopeChoice kSlopeChoices[kNumSlopeChoices];
    int slopeIndexFor (float curvePercent) noexcept;
    juce::String slopeTextFor (float curvePercent);

    //  Every control whose read-out is not a plain number needs an inverse, or
    //  typing into its text box - which a host's generic editor also does -
    //  parses the WORDS as a percentage.
    float curvePercentForText (const juce::String& text) noexcept;

    //  BITE -> the two halves of the colour law. Exposed so the tests can
    //  assert the law rather than the audio it happens to produce.
    float biteMaxDrive (float bitePercent, int character = 0) noexcept;
    float biteGamma (float bitePercent) noexcept;
    float colourDrivePercent (float bitePercent, float activity, int character = 0) noexcept;

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

            // mid band
            float midFreqHz = 1000.0f, midGainDb = 0.0f, midResPercent = 40.0f;

            // performance
            int   mode = (int) Mode::band;
            float edgePercent = 0.0f;
            float followPercent = 0.0f;
            float spreadPercent = 0.0f;
            float bitePercent = 35.0f;
            int   character = (int) Character::warm;
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

        //  --- display -----------------------------------------------------------

        //  The one read path for everything the editor draws. Coherent: either
        //  the whole snapshot from before a publication, or the whole snapshot
        //  from after it, never a mixture.
        DisplaySnapshot getDisplaySnapshot() const noexcept;

        //  Convenience views over the same snapshot, kept for the measurement
        //  suite. They are IMPLEMENTED on getDisplaySnapshot(), so there is
        //  still exactly one source.
        EdgeShape getDisplayShape() const noexcept    { return getDisplaySnapshot().currentCentre; }
        EdgeShape getDisplayShape (int channel) const noexcept
        {
            const auto s = getDisplaySnapshot();
            return channel == 0 ? s.currentLeft : s.currentRight;
        }
        EdgeShape getTargetShape() const noexcept     { return getDisplaySnapshot().target; }

        float getLiveEdge01() const noexcept          { return getDisplaySnapshot().liveEdge01; }
        float getFollowEnvelope01() const noexcept    { return getDisplaySnapshot().followEnv01; }
        bool  isSpreadActive() const noexcept         { return getDisplaySnapshot().spreadActive; }
        float getFreeTravelOctaves() const noexcept   { return getDisplaySnapshot().freeTravelOctaves; }
        bool  isColourEngaged() const noexcept        { return getDisplaySnapshot().colourEngage01 > 0.001f; }
        float getColourDrivePercent() const noexcept  { return getDisplaySnapshot().colourDrivePercent; }
        float getResonanceTrimDb() const noexcept     { return getDisplaySnapshot().resonanceTrimDb; }
        float getColourTrimDb() const noexcept        { return getDisplaySnapshot().colourTrimDb; }

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
            float midHz, midGainDb, midReso01;
        };

        void applyChunkShape (int chunkLength, float liveEdge01) noexcept;
        void updateOutputTarget() noexcept;
        Resolved resolveFor (float edge01, float channelOctaveOffset) const noexcept;
        void pushAnalyzer (const juce::AudioBuffer<float>& buffer, int numSamples) noexcept;

        EdgeUnit<Side::low>  lowEdge;
        EdgeUnit<Side::high> highEdge;
        BellSection midBand;
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
        juce::SmoothedValue<float> logMidFreq, midGain, midRes;
        juce::SmoothedValue<float> lowEnable, highEnable;     // MODE, 0..1
        //  1 in FREE mode. Smoothed, because it re-routes both the corner
        //  travel and FOLLOW's destination, and a step in either is a click.
        juce::SmoothedValue<float> freeAmount;
        juce::SmoothedValue<float> edgeBase;                  // 0..1
        juce::SmoothedValue<float> followAmount;              // -1..1
        juce::SmoothedValue<float> spreadOctaves;             // +/- octaves, per channel
        juce::SmoothedValue<float> bite;                      // percent
        juce::SmoothedValue<float> colourDrive;
        juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> outputGain;
        juce::SmoothedValue<float> bypassFade;

        float pendingOutputDb = 0.0f;
        float resonanceTrimDb = 0.0f;
        int   pendingCharacter = (int) Character::warm;
        float freeTravelOctaves = 0.0f;

        juce::AudioBuffer<float> dryBuffer;

        //  --- analyzer FIFO ---------------------------------------------------
        static constexpr int kAnalyzerFifoSize = 1 << 14;
        std::atomic<bool> analyzerEnabled { false };
        juce::AbstractFifo analyzerFifo { kAnalyzerFifoSize };
        std::vector<float> analyzerBuffer;

        //  --- display publication ---------------------------------------------
        //
        //  A seqlock. The audio thread is the only writer: it bumps `snapSeq`
        //  to odd, copies the struct, bumps back to even. The message thread
        //  copies the struct between two equal even reads of the sequence, so
        //  it can never observe half of one publication and half of another.
        //  No lock, no allocation, and the writer never waits.
        //
        //  `snapToSettings` also publishes, from the message thread - but only
        //  inside prepareToPlay, when the host is not calling processBlock.
        void publishSnapshot() noexcept;

        DisplaySnapshot snapSlot;
        std::atomic<std::uint64_t> snapSeq { 0 };

        //  Writer-side state: what was last published (for change detection)
        //  and the pieces assembled between applyChunkShape and the publish.
        DisplaySnapshot snapPending;
        std::uint64_t snapRevision = 0;
        int   lastMode = (int) Mode::band;
        float lastFollowEnv = 0.0f;

        struct SnapWork
        {
            Resolved centre {}, left {}, right {}, target {};
            float liveEdge01 = 0.0f;
            bool spreadActive = false;
        };
        SnapWork snapWork;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EdgeEngine)
    };
}
