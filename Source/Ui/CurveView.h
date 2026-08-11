// The frequency-response display, and the two handles that are EDGE's primary
// gesture: drag sideways for frequency, drag up and down for depth.
//
// The curve is drawn from edge::magnitudeDb() using the engine's LIVE resolved
// shape, which is the same function the audio path's coefficients come from.
// There is no second model of the filter to drift out of step, and Focus and
// the minimum-separation rule are already folded in because the shape is read
// after they were applied.
//
// The hidden colour engine is not drawn as an EQ curve. Its one visible effect
// is a broadband level offset, and that IS included, because the alternative is
// a curve that says 0 dB while the plug-in is 1 dB louder.

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

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

        void timerCallback() override;

        float xForHz (float hz) const noexcept;
        float hzForX (float x) const noexcept;
        float yForDb (float db) const noexcept;

        //  Named grabAt, not hitTest: Component::hitTest is virtual and an
        //  overload with a different signature silently hides it.
        Grab grabAt (juce::Point<float>) const noexcept;
        juce::Point<float> handlePosition (Grab) const noexcept;

        juce::RangedAudioParameter* freqParam (Grab) const noexcept;
        juce::RangedAudioParameter* depthParam (Grab) const noexcept;

        EdgeAudioProcessor& processor;
        juce::Rectangle<float> plot, axisGutter;

        //  Slope selectors, ON the display where the shape they describe is.
        //  They write the same Curve parameter the knobs do - no new parameter,
        //  no second source of truth - snapping it to a whole dB/oct.
        void buildSlopeBox (juce::ComboBox&, const char* paramId, juce::Colour accent);
        void refreshSlopeBoxes();

        juce::ComboBox lowSlope, highSlope;

        Grab dragging = Grab::none;
        Grab hovered = Grab::none;
        float dragStartDepth = 0.0f;
        float dragStartY = 0.0f;

        juce::Path curvePath;
        float lastShapeHash = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CurveView)
    };
}
