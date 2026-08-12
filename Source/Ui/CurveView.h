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

#include <functional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "../Dsp/EdgeEngine.h"
#include "ShapePanel.h"
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

        //  FREE mode adds a third grab target: the band itself. The editor
        //  tells the view rather than the view reading the parameter, so the
        //  cursor and the hit test change on the same frame the button does.
        void setFreeMode (bool shouldBeFree);

        enum class Grab { none, low, high, mid, band };

        //  Which handle the inspector is currently pointed at. Touching a
        //  handle selects it, and picking a band in the panel highlights it
        //  here - one selection, owned by the editor, two places to change it.
        void setSelected (Grab);
        std::function<void (SelectedControl)> onSelectionChanged;

        //  --- test support ----------------------------------------------------
        //  The display defects this view has shipped were all of the form "the
        //  screen no longer matches the state". These accessors let a test
        //  assert the match instead of a human noticing the mismatch.
        const juce::Path& testResponsePath() const noexcept { return currentPath; }
        const juce::Path& testTargetPath() const noexcept { return targetPath; }
        int testSpectrumPointCount() const noexcept { return (int) spectrumPoints.size(); }
        juce::Point<float> testHandlePosition (Grab g) const noexcept { return handlePosition (g); }

        //  Drive a drag without synthesising juce::MouseEvents: the same
        //  internal path the mouse handlers use.
        void testBeginDrag (Grab, juce::Point<float> at);
        void testDragTo (juce::Point<float> at);
        void testEndDrag();

        float testXForHz (float hz) const noexcept { return xForHz (hz); }
        int testRefreshIntervalMs() const noexcept { return getTimerInterval(); }

    private:

        static constexpr int fftOrder = 11;
        static constexpr int fftSize  = 1 << fftOrder;   // 2048
        static constexpr int numBands = 220;

        void timerCallback() override;
        void pullAudio();

        //  Two invalidation lifetimes, two paths, two rebuild functions.
        //
        //  The response paths depend on the engine's DisplaySnapshot and
        //  rebuild when its revision moves. The spectrum depends on the FFT
        //  and rebuilds when a new frame lands. Under the old single
        //  `buildCurves()` a frozen response froze the spectrum with it, and
        //  the spectrum's arrival rebuilt response paths nothing had changed.
        bool updateSpectrumData();                       // true = new FFT frame
        void updateSpectrumPath();
        void updateResponsePaths();

        float xForHz (float hz) const noexcept;
        float hzForX (float x) const noexcept;
        float yForDb (float db) const noexcept;

        Grab grabAt (juce::Point<float>) const noexcept;

        //  The one gesture implementation, shared by the mouse handlers and the
        //  test hooks so the tested path IS the shipped path.
        void beginDragInternal (juce::Point<float>);
        void dragInternal (juce::Point<float>);
        void endDragInternal();

        //  An edge that MODE has turned into an identity has no handle: it is
        //  not dimmed, it is absent. A handle for a control that provably does
        //  nothing is worse than no handle.
        bool isHandleLive (Grab) const noexcept;
        juce::Point<float> handlePosition (Grab) const noexcept;

        juce::RangedAudioParameter* freqParam (Grab) const noexcept;
        juce::RangedAudioParameter* depthParam (Grab) const noexcept;

        EdgeAudioProcessor& processor;
        juce::Rectangle<float> plot, axisGutter;

        Grab dragging = Grab::none;
        Grab hovered = Grab::none;
        Grab selected = Grab::low;
        bool freeMode = false;
        float dragStartDepth = 0.0f;
        float dragStartY = 0.0f;
        float dragStartX = 0.0f;
        float dragStartLowHz = 0.0f, dragStartHighHz = 0.0f;

        //  --- analyser --------------------------------------------------------
        juce::dsp::FFT fft { fftOrder };
        std::vector<float> ring;         // rolling window of input samples
        int ringWrite = 0;
        std::vector<float> scratch;      // 2*fftSize, the FFT workspace
        std::vector<float> window;
        std::vector<float> bandDb;       // smoothed, display resolution
        std::vector<float> frameDb;      // this frame's bands, pre-smoothing
        bool haveSpectrum = false;

        //  --- display state ---------------------------------------------------
        //  THE state everything below paints from: one coherent snapshot,
        //  fetched once per tick. The float hash this replaces compared a
        //  subset of ~40 loose atomics and missed every MID field - which is
        //  exactly the shipped "drag MID, nothing moves until you touch HIGH"
        //  defect.
        DisplaySnapshot snap;

        //  --- cached paths ----------------------------------------------------
        juce::Path currentPath, targetPath, leftPath, rightPath;

        //  The spectrum's screen points with a per-band opacity, so the level
        //  gating below -66 dBFS is a fade rather than a hard edge. Display
        //  gating only - the audio is untouched.
        struct SpectrumPoint { float x, y, alpha; };
        std::vector<SpectrumPoint> spectrumPoints;
        bool responseDirty = true;
        bool spectrumDirty = false;

        //  60 Hz while the user is dragging or the snapshot is moving; 30 Hz
        //  once both have been quiet for a moment.
        int refreshHz = 30;
        juce::uint32 lastChangeMs = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
    };
}
