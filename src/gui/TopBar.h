#pragma once

#include "VbdLookAndFeel.h"

#include "../state/Randomizer.h"

#include <array>
#include <vector>
#include <functional>
#include <memory>

namespace vbd::gui
{

/**
    Branding, A/B compare, and the randomiser with its per-section locks.

    The preset browser's prev/next/save/load arrive with the preset system in Phase F;
    the slot that will hold the preset name is drawn now so the layout does not shift when
    it lands.
*/
class TopBar final : public juce::Component
{
public:
    TopBar();
    ~TopBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    // Wiring, set by the editor.
    std::function<void()> onCopyAtoB;
    std::function<void()> onSwapAB;
    std::function<void (juce::uint32 lockMask)> onRandomize;
    std::function<void (bool showingB)> onSlotChanged;
    std::function<void (bool show)> onToggleCharacter;
    std::function<void (bool perf)> onTogglePerformance;
    std::function<void()> onCycleScene;
    std::function<void (int delta)> onPresetStep;
    std::function<void (int index)> onPresetChosen;
    std::function<void()> onPresetSave;

    void setSlot (bool showingB);
    void setStatusText (const juce::String&);

    juce::uint32 getLockMask() const noexcept;
    void setLockMask (juce::uint32);

    void setCharacterVisible (bool);
    void setPerformanceMode (bool);
    void setSceneName (const juce::String&);

    /** Greys the character controls out when no art loaded, rather than offering
        switches that cannot do anything. */
    void setCharacterControlsEnabled (bool);

    /** Fills the preset list. Entries are "Category / Name"; a separator precedes each
        new category so the menu stays navigable as the user adds their own. */
    void setPresetList (const juce::StringArray& categories, const juce::StringArray& names);
    void setCurrentPreset (int index, const juce::String& name, bool modified);

private:
    std::unique_ptr<juce::TextButton> slotA, slotB, copyButton, randomButton, sceneButton;
    std::unique_ptr<GlowToggle> characterToggle, performanceToggle;
    std::unique_ptr<juce::TextButton> prevPreset, nextPreset, savePreset;
    std::unique_ptr<juce::ComboBox> presetBox;
    std::vector<int> presetItemToIndex;
    std::array<std::unique_ptr<GlowToggle>, state::numSections> locks;

    juce::String status;
    bool showingB = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TopBar)
};

} // namespace vbd::gui
