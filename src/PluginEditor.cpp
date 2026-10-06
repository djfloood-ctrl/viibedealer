#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "gui/Theme.h"
#include "state/Randomizer.h"

namespace vbd
{

namespace
{
    constexpr double kMinScale = 0.75;
    constexpr double kMaxScale = 2.00;
}

VbdEditor::VbdEditor (VbdProcessor& p)
    : juce::AudioProcessorEditor (&p),
      proc (p),
      presets (p.state()),
      carrierPanel (p.state()),
      enginePanel (p.state()),
      fractalPanel (p.state()),
      delaysPanel (p.state()),
      outputPanel (p.state())
{
    setLookAndFeel (&lookAndFeel);

    addAndMakeVisible (content);
    content.addAndMakeVisible (topBar);
    // Added before the visualiser so it sits behind: art must never cover a control, and
    // the visualiser's panel is translucent so the character reads through it.
    content.addAndMakeVisible (characterLayer);
    content.addAndMakeVisible (visualizer);
    content.addAndMakeVisible (analyzer);
    content.addAndMakeVisible (carrierPanel);
    content.addAndMakeVisible (enginePanel);
    content.addAndMakeVisible (fractalPanel);
    content.addAndMakeVisible (delaysPanel);
    content.addAndMakeVisible (outputPanel);

    content.setSize (theme::designWidth, theme::designHeight);
    layoutContent();

    topBar.onCopyAtoB = [this] { copyAtoB(); };
    topBar.onSlotChanged = [this] (bool showB) { showSlot (showB); };
    topBar.onRandomize = [this] (juce::uint32 mask) { doRandomize (mask); };
    topBar.onPresetStep = [this] (int delta) { stepPreset (delta); };
    topBar.onPresetChosen = [this] (int index) { choosePreset (index); };
    topBar.onPresetSave = [this] { savePresetAs(); };

    topBar.onToggleCharacter = [this] (bool show)
    {
        characterLayer.setCharacterVisible (show);
        proc.setUiProperty ("characterVisible", show);
    };

    topBar.onTogglePerformance = [this] (bool perf)
    {
        characterLayer.setPerformanceMode (perf);
        proc.setUiProperty ("perfMode", perf);
    };

    topBar.onCycleScene = [this]
    {
        characterLayer.setScene (characterLayer.getScene() + 1);
        proc.setUiProperty ("scene", characterLayer.getScene());
        topBar.setSceneName (characterLayer.getSceneName());
    };

    // Double-clicking the character fires the randomiser, respecting the same locks.
    characterLayer.onRandomizeRequested = [this] { doRandomize (topBar.getLockMask()); };

    // Clicking the dragon toggles the spectral delay.
    characterLayer.onSpectralDelayToggled = [this]
    {
        if (auto* param = proc.state().getParameter (pid::sdlyOn))
            param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
    };

    // Dragging the dragon's tail drives a macro, selectable in the plugin state.
    characterLayer.onTailMacro = [this] (float value)
    {
        const auto target = static_cast<int> (proc.uiState().getProperty ("tailMacroTarget", 0));
        const auto* id = target == 1 ? pid::engMorph : pid::fracShatter;

        if (auto* param = proc.state().getParameter (id))
            param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, value));
    };

    // Both slots start as the current state, so switching to B before copying anything
    // is a no-op rather than a jump to defaults.
    slotAState = proc.state().copyState();
    slotBState = slotAState.createCopy();

    refreshPresetUi();

    // Restore the character-layer state the session was saved with.
    const auto showCharacter = static_cast<bool> (proc.uiState().getProperty ("characterVisible", true));
    const auto perfMode = static_cast<bool> (proc.uiState().getProperty ("perfMode", false));
    characterLayer.setCharacterVisible (showCharacter);
    characterLayer.setPerformanceMode (perfMode);
    characterLayer.setScene (static_cast<int> (proc.uiState().getProperty ("scene", 0)));

    topBar.setCharacterVisible (showCharacter);
    topBar.setPerformanceMode (perfMode);
    topBar.setSceneName (characterLayer.getSceneName());
    topBar.setCharacterControlsEnabled (characterLayer.hasArt());

    // Restore the lock mask the session was saved with.
    topBar.setLockMask (static_cast<juce::uint32> (
        static_cast<int> (proc.uiState().getProperty ("lockMask", 0))));

    constrainer.setFixedAspectRatio (static_cast<double> (theme::designWidth)
                                   / static_cast<double> (theme::designHeight));
    constrainer.setSizeLimits (static_cast<int> (theme::designWidth  * kMinScale),
                               static_cast<int> (theme::designHeight * kMinScale),
                               static_cast<int> (theme::designWidth  * kMaxScale),
                               static_cast<int> (theme::designHeight * kMaxScale));
    setConstrainer (&constrainer);
    setResizable (true, true);

    // Restore the size the session was saved with.
    const auto savedScale = juce::jlimit (kMinScale, kMaxScale,
                                          static_cast<double> (proc.uiState()
                                              .getProperty ("sizeScale", 1.0)));

    setSize (static_cast<int> (theme::designWidth * savedScale),
             static_cast<int> (theme::designHeight * savedScale));

    startTimerHz (60);
}

VbdEditor::~VbdEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void VbdEditor::layoutContent()
{
    auto area = juce::Rectangle<int> (0, 0, theme::designWidth, theme::designHeight);

    topBar.setBounds (area.removeFromTop (theme::topBarHeight));

    area.reduce (theme::gutter, theme::gutter);

    auto left = area.removeFromLeft (theme::columnWidth);
    area.removeFromLeft (theme::gutter);
    auto right = area.removeFromRight (theme::columnWidth);
    area.removeFromRight (theme::gutter);

    // Five panels will not fit in two columns at a readable control size, so OUTPUT --
    // the one section that reads as a horizontal chain -- becomes a strip beneath the
    // analyser instead of a third column panel.
    carrierPanel.setBounds (left.removeFromTop (212));
    left.removeFromTop (theme::gutter);
    enginePanel.setBounds (left);

    fractalPanel.setBounds (right.removeFromTop (236));
    right.removeFromTop (theme::gutter);
    delaysPanel.setBounds (right);

    outputPanel.setBounds (area.removeFromBottom (146));
    area.removeFromBottom (theme::gutter);

    // The character layer spans the visualiser and analyser area, behind both.
    characterLayer.setBounds (area);

    const auto analyzerHeight = 176;
    analyzer.setBounds (area.removeFromBottom (analyzerHeight));
    area.removeFromBottom (theme::gutter);
    visualizer.setBounds (area);
}

void VbdEditor::timerCallback()
{
    refreshDisplays();
}

void VbdEditor::refreshDisplays()
{
    // A failed read means the audio thread was mid-update; reusing the previous frame is
    // correct and invisible at 60 Hz.
    proc.visuals().read (frame);

    const auto& snap = frame;

    visualizer.update (snap);
    analyzer.update (snap);

    // Panel badges. Each setter is a no-op when the value has not changed, so these do
    // not cause 60 repaints a second.
    carrierPanel.setFallbackActive (proc.isCarrierFallbackActive());

    fractalPanel.setResolvedDepth (proc.getRequestedFractalDepth(),
                                   proc.getEffectiveFractalDepth(),
                                   proc.getFractalBandCount());

    delaysPanel.setResolvedSpectralTime (proc.getResolvedSpectralDelayMs());

    // Delay modules dim when off, so their state reads at a glance.
    const auto spectralOn = proc.isSpectralDelayOn();
    const auto stereoOn = proc.isStereoDelayOn();
    delaysPanel.setDimmed (! spectralOn && ! stereoOn);

    // Drive the character rig from the same snapshot and the same clock.
    const auto now = juce::Time::getMillisecondCounterHiRes();
    const auto dt = lastRefreshMs > 0.0 ? (now - lastRefreshMs) * 0.001 : 1.0 / 60.0;
    lastRefreshMs = now;

    characterLayer.setDelayState (spectralOn || stereoOn,
                                  proc.getDelayFeedbackForDisplay(),
                                  proc.getDelayTimeForDisplay());
    characterLayer.update (snap, dt);

    topBar.setStatusText ("FFT " + juce::String (proc.getActiveFftSize())
                          + "   " + juce::String (proc.getLatencySamples()) + " smp latency"
                          + (snap.playing ? "   " + juce::String (snap.bpm, 1) + " BPM"
                                          : juce::String()));
}

void VbdEditor::refreshPresetUi()
{
    juce::StringArray categories, names;

    for (const auto& entry : presets.getEntries())
    {
        categories.add (entry.category);
        names.add (entry.name);
    }

    topBar.setPresetList (categories, names);
    topBar.setCurrentPreset (presets.getCurrentIndex(), presets.getCurrentName(),
                             presets.isModified());
}

void VbdEditor::stepPreset (int delta)
{
    if (presets.step (delta))
        refreshPresetUi();
}

void VbdEditor::choosePreset (int index)
{
    if (presets.load (index))
        refreshPresetUi();
}

void VbdEditor::savePresetAs()
{
    saveDialog = std::make_unique<juce::AlertWindow> (
        "Save preset", "Name this preset. It is written to your user preset folder.",
        juce::MessageBoxIconType::NoIcon, this);

    saveDialog->addTextEditor ("name", presets.getCurrentName(), "Name:");
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create (
        [this] (int result)
        {
            if (result == 1 && saveDialog != nullptr)
            {
                const auto name = saveDialog->getTextEditorContents ("name");

                if (presets.saveUserPreset (name) >= 0)
                    refreshPresetUi();
            }

            saveDialog.reset();
        }), false);
}

void VbdEditor::copyAtoB()
{
    // Copy whichever slot is showing into the other, which is what "A>B" means in
    // practice: take what you have now as the starting point for the comparison.
    if (showingB)
        slotAState = proc.state().copyState();
    else
        slotBState = proc.state().copyState();
}

void VbdEditor::showSlot (bool showB)
{
    if (showB == showingB)
        return;

    // Stash the current state into the slot being left, then load the other.
    if (showingB)
        slotBState = proc.state().copyState();
    else
        slotAState = proc.state().copyState();

    showingB = showB;

    const auto& target = showingB ? slotBState : slotAState;

    if (target.isValid())
        proc.state().replaceState (target.createCopy());
}

void VbdEditor::doRandomize (juce::uint32 lockMask)
{
    proc.setUiProperty ("lockMask", static_cast<int> (lockMask));
    state::randomize (proc.state(), lockMask, rng);

    presets.markModified();
    refreshPresetUi();
}

void VbdEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void VbdEditor::resized()
{
    const auto scale = static_cast<double> (getWidth())
                     / static_cast<double> (theme::designWidth);

    // One transform on the container rather than rescaling every coordinate: vector
    // drawing stays sharp and the layout code never has to know about zoom.
    content.setTransform (juce::AffineTransform::scale (static_cast<float> (scale)));
    content.setBounds (0, 0, theme::designWidth, theme::designHeight);

    proc.setUiProperty ("sizeScale", scale);
}

} // namespace vbd
