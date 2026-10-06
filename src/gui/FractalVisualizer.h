#pragma once

#include "VisualBridge.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace vbd::gui
{

/**
    The centrepiece: the current band subdivision drawn as nested shapes that glow with
    each band's live energy.

    Band geometry is interpolated towards the incoming snapshot rather than snapped to it,
    so changing pattern or depth morphs instead of jumping. When the band *count* changes
    the old figure cannot be matched up one-to-one, so the new one cross-fades in over a
    few frames.
*/
class FractalVisualizer final : public juce::Component
{
public:
    FractalVisualizer();

    /** Called from the editor's 60 Hz timer with the latest snapshot. */
    void update (const VisualSnapshot&);

    void paint (juce::Graphics&) override;

private:
    struct Band
    {
        float lo = 0.0f;
        float hi = 0.0f;
        float energy = 0.0f;
        float active = 0.0f;   // smoothed, so gating fades rather than blinks
        int   level = 0;
    };

    std::vector<Band> bands;
    std::vector<Band> incoming;

    float crossfade = 1.0f;       // 1 = fully showing `bands`
    int   lastBandCount = 0;
    int   effectiveDepth = 0;
    float phase = 0.0f;           // drives the prism sweep
    float lowEnergy = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractalVisualizer)
};

} // namespace vbd::gui
