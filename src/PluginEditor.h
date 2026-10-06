#pragma once

// AudioProcessorEditor lives in juce_audio_processors, not juce_gui_basics -- including
// only the latter leaves the base class undeclared and every member lookup fails.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "gui/FractalVisualizer.h"
#include "gui/Panels.h"
#include "gui/SpectrumAnalyzer.h"
#include "gui/TopBar.h"
#include "gui/VbdLookAndFeel.h"
#include "character/CharacterLayer.h"
#include "state/PresetManager.h"

#include <memory>

namespace vbd
{

class VbdProcessor;

/**
    The interface.

    Everything is laid out once at the design size (1000 x 620) inside a fixed-size child,
    and the child carries an AffineTransform for the current zoom. Vector drawing then
    scales without rounding every coordinate by hand, which is what keeps it crisp from
    75% to 200% rather than merely resized.

    One 60 Hz timer drives both visualisers. Each is its own Component, so repainting them
    does not touch the panels -- the panels only repaint when something they display
    actually changes.
*/
class VbdEditor final : public juce::AudioProcessorEditor,
                        private juce::Timer
{
public:
    explicit VbdEditor (VbdProcessor&);
    ~VbdEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Pulls the latest snapshot into the visualisers and refreshes the panel badges.
        Called by the 60 Hz timer, and directly by the screenshot tool, which has no
        message loop to run timers on. */
    void refreshDisplays();

private:
    void timerCallback() override;
    void layoutContent();

    void copyAtoB();
    void showSlot (bool showB);
    void doRandomize (juce::uint32 lockMask);
    void refreshPresetUi();
    void stepPreset (int delta);
    void choosePreset (int index);
    void savePresetAs();

    VbdProcessor& proc;
    gui::VbdLookAndFeel lookAndFeel;

    juce::ComponentBoundsConstrainer constrainer;

    /** Fixed-size container holding the whole interface at design resolution. */
    juce::Component content;

    gui::TopBar topBar;
    character::CharacterLayer characterLayer;
    gui::FractalVisualizer visualizer;
    gui::SpectrumAnalyzer analyzer;

    gui::CarrierPanel carrierPanel;
    gui::EnginePanel  enginePanel;
    gui::FractalPanel fractalPanel;
    gui::DelaysPanel  delaysPanel;
    gui::OutputPanel  outputPanel;

    juce::TooltipWindow tooltips { this, 700 };
    juce::Random rng;
    state::PresetManager presets;
    std::unique_ptr<juce::AlertWindow> saveDialog;

    // The bridge hands back a copy rather than a reference, so the editor owns the frame
    // it is drawing. A failed read simply leaves the previous frame in place.
    gui::VisualSnapshot frame;
    double lastRefreshMs = 0.0;

    // A/B slots. Not serialised: they are a working convenience for the session, and
    // persisting them would double the size of every saved project.
    juce::ValueTree slotAState;
    juce::ValueTree slotBState;
    bool showingB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VbdEditor)
};

} // namespace vbd
