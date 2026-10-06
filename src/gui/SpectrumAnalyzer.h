#pragma once

#include "VisualBridge.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace vbd::gui
{

/**
    Input, output and carrier overlaid on a log frequency axis.

    The snapshot already arrives log-spaced from the audio side, so the horizontal axis is
    a straight mapping. Magnitudes come through linear and are converted to dB here --
    one log per point at 60 Hz on the message thread, rather than on the audio thread at
    the frame rate.
*/
class SpectrumAnalyzer final : public juce::Component
{
public:
    SpectrumAnalyzer();

    void update (const VisualSnapshot&);

    void paint (juce::Graphics&) override;

private:
    static constexpr int points = VisualSnapshot::spectrumPoints;
    static constexpr float minDb = -78.0f;
    static constexpr float maxDb = 6.0f;

    void buildPath (juce::Path&, const std::array<float, points>&,
                    juce::Rectangle<float>) const;

    // Smoothed display values, in dB.
    std::array<float, points> inputDb {};
    std::array<float, points> outputDb {};
    std::array<float, points> carrierDb {};

    bool primed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};

} // namespace vbd::gui
