// The display: spectrum, three response curves, and the two target handles.
//
// Three things are drawn, and they are deliberately three different things:
//
//   1. TARGET     what EDGE at 100 % would produce - a thin ghost curve, so you
//                 can see where the movement is going while it is going there.
//   2. CURRENT    what the filter is doing right now - the bright orange-to-cyan
//                 line, drawn from the engine's LIVE coefficients.
//   3. SPECTRUM   the audio, a restrained grey trace, never coloured.
//
// When SPREAD is doing something, the two channels' responses are added as
// faint traces. The stereo information is in the RESPONSE, not in a second and
// third spectrum: two more bright analyser lines would bury everything else.
//
// ANALYSER ARCHITECTURE. The audio thread writes mono output into a fixed
// lock-free FIFO and does nothing else. This component drains it on a 30 Hz
// timer, windows, transforms and smooths - all on the message thread - and the
// engine's FIFO writer is switched off entirely while no editor exists.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "Theme.h"

class EdgeAudioProcessor;

namespace edge::ui
{
    class CurveView : public juce::Component,
                      private juce::Timer
    {
    public:
        explicit CurveView (EdgeAudioProcessor&);
        ~CurveView() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        enum class Grab { none, low, high };

        static constexpr int fftOrder = 11;
        static constexpr int fftSize  = 1 << fftOrder;   // 2048
        static constexpr int numBands = 220;

        void timerCallback() override;
        void pullAudio();
        void updateSpectrum();
        void buildCurves();

        float xForHz (float hz) const noexcept;
        float hzForX (float x) const noexcept;
        float yForDb (float db) const noexcept;

        Grab grabAt (juce::Point<float>) const noexcept;
        juce::Point<float> handlePosition (Grab) const noexcept;

        juce::RangedAudioParameter* freqParam (Grab) const noexcept;
        juce::RangedAudioParameter* depthParam (Grab) const noexcept;

        EdgeAudioProcessor& processor;
        juce::Rectangle<float> plot, axisGutter;

        Grab dragging = Grab::none;
        Grab hovered = Grab::none;
        float dragStartDepth = 0.0f;
        float dragStartY = 0.0f;

        //  --- analyser --------------------------------------------------------
        juce::dsp::FFT fft { fftOrder };
        std::vector<float> ring;         // rolling window of input samples
        int ringWrite = 0;
        std::vector<float> scratch;      // 2*fftSize, the FFT workspace
        std::vector<float> window;
        std::vector<float> bandDb;       // smoothed, display resolution
        bool haveSpectrum = false;

        //  --- cached paths ----------------------------------------------------
        juce::Path currentPath, targetPath, leftPath, rightPath, spectrumPath;
        float lastShapeHash = 0.0f;
        bool curvesDirty = true;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
    };
}
