#pragma once

#include "VbdLookAndFeel.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <memory>
#include <vector>

namespace vbd::gui
{

/**
    A titled panel holding knobs, toggles and combo boxes bound to APVTS parameters.

    Controls are added declaratively and laid out on a grid, so the five panels differ
    only in their contents and accent colour. A panel can be told to dim, which the delay
    modules use to show at a glance that they are switched off.
*/
class Panel : public juce::Component
{
public:
    Panel (juce::AudioProcessorValueTreeState&, const juce::String& title, juce::Colour accent);
    ~Panel() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Dimmed panels stay fully interactive -- the point is to show state, not block it. */
    void setDimmed (bool shouldDim);

    juce::Colour getAccent() const noexcept { return accent; }

protected:
    /** Adds a rotary bound to a parameter. */
    void addKnob (const char* paramId, const juce::String& label);

    /** Adds a combo box bound to a choice parameter. Spans the full row width. */
    void addChoice (const char* paramId, const juce::String& label);

    /** Adds a toggle bound to a bool parameter. */
    void addToggle (const char* paramId, const juce::String& label);

    /** Forces the next control onto a new row. */
    void addRowBreak();

    juce::AudioProcessorValueTreeState& apvts;

private:
    enum class Kind { knob, choice, toggle, rowBreak };

    struct Entry
    {
        Kind kind = Kind::knob;
        std::unique_ptr<juce::Component> control;
        std::unique_ptr<juce::Label> caption;
    };

    juce::String title;
    juce::Colour accent;
    bool dimmed = false;

    std::vector<Entry> entries;
    std::vector<std::unique_ptr<juce::SliderParameterAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<juce::ComboBoxParameterAttachment>> comboAttachments;
    std::vector<std::unique_ptr<juce::ButtonParameterAttachment>> buttonAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Panel)
};

// ---------------------------------------------------------------------------- the five

class CarrierPanel final : public Panel
{
public:
    explicit CarrierPanel (juce::AudioProcessorValueTreeState&);

    /** Shows the amber badge when the sidechain is selected but unpatched. */
    void setFallbackActive (bool);
    void paint (juce::Graphics&) override;

private:
    bool fallbackActive = false;
};

class EnginePanel final : public Panel
{
public:
    explicit EnginePanel (juce::AudioProcessorValueTreeState&);
};

class FractalPanel final : public Panel
{
public:
    explicit FractalPanel (juce::AudioProcessorValueTreeState&);

    void setResolvedDepth (int requested, int effective, int bands);
    void paint (juce::Graphics&) override;

private:
    int requestedDepth = 0;
    int effectiveDepth = 0;
    int bandCount = 0;
};

class DelaysPanel final : public Panel
{
public:
    explicit DelaysPanel (juce::AudioProcessorValueTreeState&);

    void setResolvedSpectralTime (float ms);
    void paint (juce::Graphics&) override;

private:
    float resolvedMs = 0.0f;
};

class OutputPanel final : public Panel
{
public:
    explicit OutputPanel (juce::AudioProcessorValueTreeState&);
};

} // namespace vbd::gui
