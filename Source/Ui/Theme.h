// The ONLY place EDGE's colours and metrics live. Nothing else in the UI may
// hard-code a colour: two accents is a product decision, and it stops being
// true the moment a third one is typed somewhere else.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace edge::ui
{
    namespace colour
    {
        //  Matched to the approved mockup: a near-black shell with a slightly
        //  lifted top, and a display well that is darker than the chassis.
        inline const juce::Colour shellTop     { 0xff232427 };
        inline const juce::Colour shellBottom  { 0xff141517 };
        inline const juce::Colour chassis      { 0xff1c1d20 };
        inline const juce::Colour wellTop      { 0xff0d0e10 };
        inline const juce::Colour wellBottom   { 0xff141518 };
        inline const juce::Colour panelEdge    { 0xff34363b };
        inline const juce::Colour panelHilite  { 0xff45484f };

        inline const juce::Colour grid         { 0xff232529 };
        inline const juce::Colour gridStrong   { 0xff32353b };
        inline const juce::Colour spectrum     { 0xff8a8f98 };

        inline const juce::Colour text         { 0xffc8cdd4 };
        inline const juce::Colour textDim      { 0xff787f89 };
        inline const juce::Colour textBright   { 0xfff4f7fa };

        //  The two accents. Nothing else in the plug-in is coloured.
        inline const juce::Colour low          { 0xffF2A03C };
        inline const juce::Colour high         { 0xff31C6E8 };
    }

    namespace metric
    {
        inline constexpr int defaultWidth  = 900;
        inline constexpr int defaultHeight = 560;
        inline constexpr int minWidth      = 720;
        inline constexpr int maxWidth      = 1800;

        inline constexpr int shapeHeight   = 190;   // extra height when SHAPE is open

        //  One vertical rhythm for every knob column in the plug-in, so the
        //  main strip and the SHAPE panel line up with each other instead of
        //  each inventing its own spacing.
        inline constexpr int markRow    = 11;   // the "0" above a bipolar knob
        inline constexpr int pillRow    = 16;   // the value read-out
        inline constexpr int captionRow = 13;   // the name underneath
        inline constexpr int rowGap     = 3;

        inline constexpr float displayTopDb    = 15.0f;
        inline constexpr float displayBottomDb = -45.0f;
        inline constexpr float displayMinHz    = 18.0f;
        inline constexpr float displayMaxHz    = 22000.0f;
    }

    //  One type scale. Every size in the plug-in comes from here; the previous
    //  build had eight different ones and nothing lined up.
    namespace font
    {
        inline constexpr float tiny    = 9.0f;    // axis numbers, +/- marks
        inline constexpr float caption = 10.0f;   // control names
        inline constexpr float value   = 11.0f;   // read-outs
        inline constexpr float title   = 11.0f;   // section titles
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
