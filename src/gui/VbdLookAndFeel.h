#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace vbd::gui
{

/** Rotary knobs with glowing value arcs, lit toggles, and dark combo boxes. */
class VbdLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    VbdLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height,
                           float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                           juce::Slider&) override;

    void drawToggleButton (juce::Graphics&, juce::ToggleButton&,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox&) override;

    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;

    juce::Font getComboBoxFont (juce::ComboBox&) override;

    /** JUCE sizes button text as min(16, height * 0.6), which ellipsises short labels in
        the compact top-bar buttons. */
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont (juce::Label&) override;

    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;

    /** Each knob can carry its own accent, so panels read as distinct families. */
    static constexpr const char* accentProperty = "vbdAccent";
};

/**
    Rotary with the interaction the spec asks for: shift-drag for fine control,
    double-click to reset, mouse wheel, and a value readout while hovered or dragging.
*/
class GlowKnob final : public juce::Slider
{
public:
    GlowKnob (const juce::String& labelText, juce::Colour accent);

    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void paint (juce::Graphics&) override;

    const juce::String& getLabelText() const noexcept { return label; }

private:
    juce::String label;
    juce::Colour accentColour;
    bool hovering = false;

    // Normal drag sensitivity, and the much coarser one used while shift is held.
    static constexpr int normalSensitivity = 220;
    static constexpr int fineSensitivity   = 1400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowKnob)
};

/** A toggle that lights up, with an optional label drawn beside it. */
class GlowToggle final : public juce::ToggleButton
{
public:
    GlowToggle (const juce::String& text, juce::Colour accent);

    void paintButton (juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour accentColour;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GlowToggle)
};

} // namespace vbd::gui
