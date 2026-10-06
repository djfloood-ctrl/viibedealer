#include "VbdLookAndFeel.h"
#include "Theme.h"

namespace vbd::gui
{

namespace
{
    juce::Colour accentOf (const juce::Component& c, juce::Colour fallback)
    {
        const auto v = c.getProperties()[VbdLookAndFeel::accentProperty];

        if (v.isVoid())
            return fallback;

        return juce::Colour (static_cast<juce::uint32> (static_cast<int> (v)));
    }
}

VbdLookAndFeel::VbdLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::background);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::ComboBox::backgroundColourId, theme::panelRaised);
    setColour (juce::ComboBox::textColourId, theme::neon);
    setColour (juce::ComboBox::outlineColourId, theme::panelEdge);
    setColour (juce::ComboBox::arrowColourId, theme::violet);
    setColour (juce::PopupMenu::backgroundColourId, theme::panel);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::neonDim);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
    setColour (juce::TextButton::buttonColourId, theme::panelRaised);
    setColour (juce::TextButton::textColourOffId, theme::text);
    setColour (juce::TextButton::textColourOnId, theme::neon);
    setColour (juce::ScrollBar::thumbColourId, theme::neonDim);
}

void VbdLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    const auto accent = accentOf (slider, theme::neon);

    auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const auto lineW = juce::jmax (2.0f, radius * 0.16f);
    const auto arcRadius = radius - lineW * 0.6f;

    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    // Track.
    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (theme::panelEdge);
    g.strokePath (track, juce::PathStrokeType (lineW, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc, drawn three times at decreasing width and increasing opacity so it
    // reads as a glow rather than a flat stroke. Cheaper than a real blur and vector,
    // so it stays crisp when the whole interface is scaled.
    if (slider.isEnabled())
    {
        const auto isBipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
        const auto zeroPos = isBipolar
            ? static_cast<float> ((0.0 - slider.getMinimum())
                                  / (slider.getMaximum() - slider.getMinimum()))
            : 0.0f;
        const auto fromAngle = rotaryStartAngle + zeroPos * (rotaryEndAngle - rotaryStartAngle);

        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f,
                             juce::jmin (fromAngle, angle), juce::jmax (fromAngle, angle), true);

        for (int pass = 0; pass < 3; ++pass)
        {
            const auto w = lineW * (2.6f - static_cast<float> (pass) * 0.8f);
            const auto alpha = 0.18f + static_cast<float> (pass) * 0.3f;

            g.setColour (accent.withAlpha (alpha));
            g.strokePath (value, juce::PathStrokeType (w, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }
    }

    // Hub and pointer.
    const auto hubRadius = arcRadius - lineW * 1.2f;

    g.setColour (theme::panelRaised);
    g.fillEllipse (juce::Rectangle<float> (hubRadius * 2.0f, hubRadius * 2.0f)
                       .withCentre (centre));

    const auto pointerLength = hubRadius * 0.82f;
    const auto tip = centre.getPointOnCircumference (pointerLength, angle);

    g.setColour (slider.isEnabled() ? accent : theme::textDim);
    g.drawLine (juce::Line<float> (centre.getPointOnCircumference (hubRadius * 0.25f, angle), tip),
                juce::jmax (1.5f, lineW * 0.45f));
}

void VbdLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                       bool shouldDrawButtonAsHighlighted,
                                       bool shouldDrawButtonAsDown)
{
    juce::ignoreUnused (shouldDrawButtonAsDown);

    const auto accent = accentOf (button, theme::neon);
    const auto on = button.getToggleState();

    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto corner = 3.0f;

    g.setColour (on ? accent.withAlpha (0.18f) : theme::panelRaised);
    g.fillRoundedRectangle (bounds, corner);

    if (on)
    {
        g.setColour (accent.withAlpha (0.35f));
        g.drawRoundedRectangle (bounds.expanded (1.0f), corner + 1.0f, 2.0f);
    }

    g.setColour (on ? accent : (shouldDrawButtonAsHighlighted ? theme::text : theme::panelEdge));
    g.drawRoundedRectangle (bounds, corner, 1.0f);

    g.setColour (on ? accent : (shouldDrawButtonAsHighlighted ? theme::text : theme::textDim));
    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    g.drawText (button.getButtonText(), bounds, juce::Justification::centred, false);
}

void VbdLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                                   int buttonX, int buttonY, int buttonW, int buttonH,
                                   juce::ComboBox& box)
{
    juce::ignoreUnused (isButtonDown, buttonX, buttonY, buttonW, buttonH);

    const auto accent = accentOf (box, theme::neon);
    auto bounds = juce::Rectangle<float> (0.0f, 0.0f, static_cast<float> (width),
                                          static_cast<float> (height)).reduced (0.5f);

    g.setColour (theme::panelRaised);
    g.fillRoundedRectangle (bounds, 3.0f);

    g.setColour (box.isMouseOver() ? accent.withAlpha (0.7f) : theme::panelEdge);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    // Chevron.
    const auto cx = bounds.getRight() - 11.0f;
    const auto cy = bounds.getCentreY();

    juce::Path chevron;
    chevron.startNewSubPath (cx - 4.0f, cy - 2.0f);
    chevron.lineTo (cx, cy + 2.5f);
    chevron.lineTo (cx + 4.0f, cy - 2.0f);

    g.setColour (theme::violet);
    g.strokePath (chevron, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
}

void VbdLookAndFeel::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
    label.setFont (getComboBoxFont (box));
    label.setJustificationType (juce::Justification::centredLeft);
}

juce::Font VbdLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return juce::Font (juce::FontOptions (11.5f));
}

juce::Font VbdLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return juce::Font (juce::FontOptions (
        juce::jlimit (9.0f, 12.0f, static_cast<float> (buttonHeight) * 0.45f),
        juce::Font::bold));
}

juce::Font VbdLookAndFeel::getPopupMenuFont()
{
    return juce::Font (juce::FontOptions (12.5f));
}

juce::Font VbdLookAndFeel::getLabelFont (juce::Label&)
{
    return juce::Font (juce::FontOptions (11.0f));
}

void VbdLookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int width, int height)
{
    g.fillAll (theme::panel);
    g.setColour (theme::panelEdge);
    g.drawRect (0, 0, width, height, 1);
}

// ============================================================================== GlowKnob

GlowKnob::GlowKnob (const juce::String& labelText, juce::Colour accent)
    : label (labelText), accentColour (accent)
{
    setSliderStyle (juce::Slider::RotaryVerticalDrag);
    setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    setRotaryParameters (juce::MathConstants<float>::pi * 1.2f,
                         juce::MathConstants<float>::pi * 2.8f, true);
    setMouseDragSensitivity (normalSensitivity);
    setVelocityBasedMode (false);
    getProperties().set (VbdLookAndFeel::accentProperty,
                         static_cast<int> (accent.getARGB()));
}

void GlowKnob::mouseDown (const juce::MouseEvent& e)
{
    // Shift gives fine control. Setting sensitivity on mouseDown rather than mid-drag
    // keeps the gesture consistent even if the key is released part-way through.
    setMouseDragSensitivity (e.mods.isShiftDown() ? fineSensitivity : normalSensitivity);
    juce::Slider::mouseDown (e);
    repaint();
}

void GlowKnob::mouseUp (const juce::MouseEvent& e)
{
    juce::Slider::mouseUp (e);
    setMouseDragSensitivity (normalSensitivity);
    repaint();
}

void GlowKnob::mouseDrag (const juce::MouseEvent& e)
{
    juce::Slider::mouseDrag (e);
    repaint();
}

void GlowKnob::mouseEnter (const juce::MouseEvent& e)
{
    hovering = true;
    juce::Slider::mouseEnter (e);
    repaint();
}

void GlowKnob::mouseExit (const juce::MouseEvent& e)
{
    hovering = false;
    juce::Slider::mouseExit (e);
    repaint();
}

void GlowKnob::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto textHeight = 13;

    auto dial = bounds.removeFromTop (bounds.getHeight() - textHeight);

    getLookAndFeel().drawRotarySlider (
        g, dial.getX(), dial.getY(), dial.getWidth(), dial.getHeight(),
        static_cast<float> (valueToProportionOfLength (getValue())),
        juce::MathConstants<float>::pi * 1.2f,
        juce::MathConstants<float>::pi * 2.8f, *this);

    // While hovered or dragging, the readout replaces the name: the value is what you
    // care about in the moment, and there is not room for both.
    const auto showValue = hovering || isMouseButtonDown();

    g.setColour (showValue ? accentColour : theme::textDim);
    g.setFont (juce::FontOptions (showValue ? 11.0f : 10.0f,
                                  showValue ? juce::Font::bold : juce::Font::plain));

    const auto caption = showValue ? getTextFromValue (getValue()) : label;

    g.drawText (caption, bounds.expanded (6, 0), juce::Justification::centredTop, false);
}

// ============================================================================ GlowToggle

GlowToggle::GlowToggle (const juce::String& text, juce::Colour accent)
    : accentColour (accent)
{
    setButtonText (text);
    getProperties().set (VbdLookAndFeel::accentProperty,
                         static_cast<int> (accent.getARGB()));
}

void GlowToggle::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawToggleButton (g, *this, highlighted, down);
}

} // namespace vbd::gui
