// The ONLY place EDGE's colours and metrics live. Nothing else in the UI may
// hard-code a colour: two accents is a product decision, and it stops being
// true the moment a third one is typed somewhere else.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace edge::ui
{
    namespace colour
    {
        //  The v0.12 direction's palette. Three accents with FIXED meanings:
        //  amber is the low-frequency edge, cyan is the high edge (and so the
        //  LP cutoff), violet is movement/modulation and NOTHING else - it is
        //  never a third band colour.
        inline const juce::Colour chassis      { 0xff17191D };
        inline const juce::Colour graph        { 0xff0B0D10 };
        inline const juce::Colour raised       { 0xff15171B };

        inline const juce::Colour shellTop     { 0xff1D2025 };
        inline const juce::Colour shellBottom  { 0xff131519 };
        inline const juce::Colour wellTop      { 0xff0B0D10 };
        inline const juce::Colour wellBottom   { 0xff0E1013 };
        inline const juce::Colour panelEdge    { 0xff2A2D33 };
        inline const juce::Colour panelHilite  { 0xff3A3E46 };

        inline const juce::Colour grid         { 0xff1A1D22 };
        inline const juce::Colour gridStrong   { 0xff23262C };
        inline const juce::Colour spectrum     { 0xff8a8f98 };

        inline const juce::Colour text         { 0xffE7EBF0 };
        inline const juce::Colour textDim      { 0xff7C8490 };
        inline const juce::Colour textBright   { 0xffF4F7FA };

        inline const juce::Colour low          { 0xffF2A03C };
        inline const juce::Colour high         { 0xff31C6E8 };
        inline const juce::Colour movement     { 0xff9B8CFF };
    }

    namespace metric
    {
        inline constexpr int defaultWidth  = 900;
        inline constexpr int defaultHeight = 560;
        inline constexpr int minWidth      = 720;
        inline constexpr int minHeight     = 420;
        inline constexpr int maxWidth      = 1800;
        inline constexpr int maxHeight     = 1400;

        //  One 8 px base grid, and a radius scale derived from it. The old
        //  build had 4, 8 and 10 px corners, none derived from anything -
        //  which is what "the angles are not professional" looks like.
        inline constexpr int   grid         = 8;
        inline constexpr float radiusSmall  = 8.0f;    // segments, pills, notches
        inline constexpr float radiusLarge  = 16.0f;   // graph, deck, inspector

        inline constexpr int headerHeight = 48;
        inline constexpr int margin       = 12;
        inline constexpr int deckHeight   = 176;

        inline constexpr int pillRow    = 16;
        inline constexpr int captionRow = 13;
        inline constexpr int rowGap     = 3;

        inline constexpr float displayTopDb    = 15.0f;
        inline constexpr float displayBottomDb = -45.0f;
        inline constexpr float displayMinHz    = 18.0f;
        inline constexpr float displayMaxHz    = 22000.0f;
    }

    //  One type scale, from the v0.12 spec. Only control categories and short
    //  control labels are uppercase; values and preset names never are.
    namespace font
    {
        inline constexpr float axis      = 10.0f;   // axis labels, regular
        inline constexpr float tiny      = 9.0f;
        inline constexpr float caption   = 10.0f;   // inspector labels, uppercase
        inline constexpr float knobLabel = 11.0f;   // knob labels, semibold uppercase
        inline constexpr float modeLabel = 11.0f;   // segmented control
        inline constexpr float value     = 13.0f;   // knob values
        inline constexpr float readout   = 12.0f;
        inline constexpr float title     = 12.0f;   // inspector header
        inline constexpr float wordmark  = 22.0f;
    }

    juce::String formatHz (float hz);

    void paintShell (juce::Graphics&, juce::Rectangle<float> bounds);
    void paintWell (juce::Graphics&, juce::Rectangle<float> bounds, float corner);

    //  Soft drop shadow under a rounded panel. Concentric strokes rather than a
    //  real blur: cheap, and at these radii indistinguishable.
    void dropShadow (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                     int depth = 6);

    //  A small round lamp. `lit` drives brightness rather than visibility, so
    //  it reads as an indicator that is off rather than one that vanished.
    void drawLamp (juce::Graphics&, juce::Point<float> centre, float radius,
                   juce::Colour, bool lit);

    //  One knob renderer for the whole plug-in. Per-control variation travels
    //  in the Slider's property set, never in a second look-and-feel:
    //
    //    "accent"   ARGB of the value arc
    //    "accent2"  optional second ARGB; the arc becomes a gradient between
    //               them, which is what makes the big EDGE knob read as
    //               spanning both edges of the spectrum
    //    "bipolar"  the arc grows out of 12 o'clock in both directions
    //    "ticks"    draw a ring of tick marks outside the arc
    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                               float pos, float startAngle, float endAngle,
                               juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool highlighted, bool down) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&,
                                   const juce::Colour&, bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&,
                             bool highlighted, bool down) override;

        void drawComboBox (juce::Graphics&, int w, int h, bool down,
                           int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;

        //  Value read-outs are drawn as dark recessed pills. JUCE's default
        //  paints a transparent box with a light outline, which read as boxes
        //  floating on top of the panel rather than sunk into it.
        void drawLabel (juce::Graphics&, juce::Label&) override;
    };
}
