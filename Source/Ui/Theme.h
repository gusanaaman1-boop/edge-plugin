// The ONLY place EDGE's colours and metrics live. Nothing else in the UI may
// hard-code a colour: two accents is a product decision, and it stops being
// true the moment a third one is typed somewhere else.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace edge::ui
{
    namespace colour
    {
        //  The shell is a graded charcoal with a faint blue cast rather than a
        //  flat fill - a single flat colour behind a glowing curve reads as an
        //  unfinished window, not as a dark theme.
        inline const juce::Colour shellTop     { 0xff20242b };
        inline const juce::Colour shellBottom  { 0xff0e1014 };
        inline const juce::Colour shellGlow    { 0xff2b3340 };
        inline const juce::Colour panelTop     { 0xff181c22 };
        inline const juce::Colour panelBottom  { 0xff10131a };
        inline const juce::Colour panelEdge    { 0xff333a44 };
        inline const juce::Colour panelHilite  { 0xff414a57 };

        inline const juce::Colour grid         { 0xff262c34 };
        inline const juce::Colour gridStrong   { 0xff3a434f };

        inline const juce::Colour text         { 0xffc3ccd6 };
        inline const juce::Colour textDim      { 0xff78828e };
        inline const juce::Colour textBright   { 0xfff2f6fa };

        //  The two accents, one step brighter and more saturated than v0.1.
        //  Nothing else in the plug-in is coloured.
        inline const juce::Colour low          { 0xffFF8534 };
        inline const juce::Colour high         { 0xff35D2F0 };
    }

    namespace metric
    {
        inline constexpr int defaultWidth  = 880;
        inline constexpr int defaultHeight = 520;
        inline constexpr int minWidth      = 660;
        inline constexpr int maxWidth      = 1760;

        inline constexpr float displayTopDb    = 12.0f;
        inline constexpr float displayBottomDb = -36.0f;
        inline constexpr float displayMinHz    = 18.0f;
        inline constexpr float displayMaxHz    = 22000.0f;
    }

    juce::String formatHz (float hz);

    //  A soft vertical gradient plus a wide radial lift behind the centre.
    //  Used for the window and, at a smaller scale, for the display panel.
    void paintShell (juce::Graphics&, juce::Rectangle<float> bounds);

    //  Soft drop shadow under a rounded panel. Concentric strokes rather than a
    //  real blur: cheap, and at these radii indistinguishable.
    void dropShadow (juce::Graphics&, juce::Rectangle<float> bounds, float corner,
                     int depth = 6);

    //  One knob renderer for the whole plug-in. Per-control variation travels
    //  in the Slider's property set ("accent"), never in a second
    //  look-and-feel.
    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look();

        void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h,
                               float pos, float startAngle, float endAngle,
                               juce::Slider&) override;

        void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                               bool highlighted, bool down) override;

        void drawComboBox (juce::Graphics&, int w, int h, bool down,
                           int buttonX, int buttonY, int buttonW, int buttonH,
                           juce::ComboBox&) override;
        void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
        juce::Font getComboBoxFont (juce::ComboBox&) override;
        void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;
    };
}
