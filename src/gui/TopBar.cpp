#include "TopBar.h"
#include "Theme.h"

namespace vbd::gui
{

TopBar::TopBar()
{
    auto makeButton = [this] (const juce::String& text, juce::Colour accent)
    {
        auto b = std::make_unique<juce::TextButton> (text);
        b->setColour (juce::TextButton::buttonColourId, theme::panelRaised);
        b->setColour (juce::TextButton::textColourOffId, accent);
        b->setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        b->getProperties().set (VbdLookAndFeel::accentProperty,
                                static_cast<int> (accent.getARGB()));
        addAndMakeVisible (*b);
        return b;
    };

    slotA = makeButton ("A", theme::neon);
    slotB = makeButton ("B", theme::neon);
    copyButton = makeButton ("A>B", theme::violet);
    randomButton = makeButton ("RAND", theme::magenta);
    sceneButton = makeButton ("SCENE", theme::lime);

    characterToggle = std::make_unique<GlowToggle> ("CHAR", theme::neon);
    characterToggle->setTooltip ("Show or hide the character layer");
    addAndMakeVisible (*characterToggle);

    performanceToggle = std::make_unique<GlowToggle> ("PERF", theme::amber);
    performanceToggle->setTooltip ("Performance Mode: freeze all animation");
    addAndMakeVisible (*performanceToggle);

    prevPreset = makeButton ("<", theme::neon);
    nextPreset = makeButton (">", theme::neon);
    savePreset = makeButton ("SAVE", theme::violet);
    savePreset->setTooltip ("Save the current state as a user preset");

    presetBox = std::make_unique<juce::ComboBox>();
    presetBox->setTextWhenNothingSelected ("-- presets --");
    presetBox->getProperties().set (VbdLookAndFeel::accentProperty,
                                    static_cast<int> (theme::neon.getARGB()));
    addAndMakeVisible (*presetBox);

    prevPreset->onClick = [this] { if (onPresetStep != nullptr) onPresetStep (-1); };
    nextPreset->onClick = [this] { if (onPresetStep != nullptr) onPresetStep (1); };
    savePreset->onClick = [this] { if (onPresetSave != nullptr) onPresetSave(); };

    presetBox->onChange = [this]
    {
        const auto item = presetBox->getSelectedId() - 1;

        if (item < 0 || item >= static_cast<int> (presetItemToIndex.size()))
            return;

        if (onPresetChosen != nullptr)
            onPresetChosen (presetItemToIndex[static_cast<std::size_t> (item)]);
    };

    sceneButton->onClick = [this] { if (onCycleScene != nullptr) onCycleScene(); };

    characterToggle->onClick = [this]
    {
        if (onToggleCharacter != nullptr)
            onToggleCharacter (characterToggle->getToggleState());
    };

    performanceToggle->onClick = [this]
    {
        if (onTogglePerformance != nullptr)
            onTogglePerformance (performanceToggle->getToggleState());
    };

    slotA->onClick = [this]
    {
        if (! showingB)
            return;

        setSlot (false);

        if (onSlotChanged != nullptr)
            onSlotChanged (false);
    };

    slotB->onClick = [this]
    {
        if (showingB)
            return;

        setSlot (true);

        if (onSlotChanged != nullptr)
            onSlotChanged (true);
    };

    copyButton->onClick = [this] { if (onCopyAtoB != nullptr) onCopyAtoB(); };

    randomButton->onClick = [this]
    {
        if (onRandomize != nullptr)
            onRandomize (getLockMask());
    };

    // One lock per section, so a randomise can be aimed at just the parts you want to
    // move. Locks are GUI state, not parameters -- they are not automatable and should
    // not be.
    for (int i = 0; i < state::numSections; ++i)
    {
        const auto section = static_cast<state::Section> (i);
        auto lock = std::make_unique<GlowToggle> (state::sectionShortName (section), theme::amber);
        lock->setTooltip ("Lock " + juce::String (state::sectionName (section))
                          + " against Randomize");
        addAndMakeVisible (*lock);
        locks[static_cast<std::size_t> (i)] = std::move (lock);
    }

    setSlot (false);
}

TopBar::~TopBar() = default;

void TopBar::setSlot (bool shouldShowB)
{
    showingB = shouldShowB;
    slotA->setToggleState (! showingB, juce::dontSendNotification);
    slotB->setToggleState (showingB, juce::dontSendNotification);

    slotA->setColour (juce::TextButton::buttonColourId,
                      showingB ? theme::panelRaised : theme::neon.withAlpha (0.25f));
    slotB->setColour (juce::TextButton::buttonColourId,
                      showingB ? theme::neon.withAlpha (0.25f) : theme::panelRaised);

    repaint();
}

void TopBar::setStatusText (const juce::String& text)
{
    if (status == text)
        return;

    status = text;
    repaint();
}

juce::uint32 TopBar::getLockMask() const noexcept
{
    juce::uint32 mask = 0;

    for (int i = 0; i < state::numSections; ++i)
        if (locks[static_cast<std::size_t> (i)] != nullptr
            && locks[static_cast<std::size_t> (i)]->getToggleState())
            mask |= (1u << i);

    return mask;
}

void TopBar::setLockMask (juce::uint32 mask)
{
    for (int i = 0; i < state::numSections; ++i)
        if (locks[static_cast<std::size_t> (i)] != nullptr)
            locks[static_cast<std::size_t> (i)]->setToggleState ((mask & (1u << i)) != 0,
                                                                 juce::dontSendNotification);
}

void TopBar::setCharacterVisible (bool show)
{
    characterToggle->setToggleState (show, juce::dontSendNotification);
}

void TopBar::setPerformanceMode (bool perf)
{
    performanceToggle->setToggleState (perf, juce::dontSendNotification);
}

void TopBar::setSceneName (const juce::String& name)
{
    // Only the first word: the full "COSMIC VOID" wrapped onto two lines in a 68px button.
    sceneButton->setButtonText (name.isEmpty() ? "SCENE"
                                               : name.upToFirstOccurrenceOf ("_", false, false)
                                                     .toUpperCase());
    sceneButton->setTooltip (name.isEmpty() ? "Cycle the backdrop scene"
                                            : "Scene: " + name.replace ("_", " "));
}

void TopBar::setCharacterControlsEnabled (bool enabled)
{
    characterToggle->setEnabled (enabled);
    performanceToggle->setEnabled (enabled);
    sceneButton->setEnabled (enabled);
}

void TopBar::setPresetList (const juce::StringArray& categories, const juce::StringArray& names)
{
    presetBox->clear (juce::dontSendNotification);
    presetItemToIndex.clear();

    juce::String lastCategory;

    for (int i = 0; i < names.size(); ++i)
    {
        const auto category = i < categories.size() ? categories[i] : juce::String();

        if (category != lastCategory)
        {
            presetBox->addSectionHeading (category);
            lastCategory = category;
        }

        presetItemToIndex.push_back (i);
        presetBox->addItem (names[i], static_cast<int> (presetItemToIndex.size()));
    }
}

void TopBar::setCurrentPreset (int index, const juce::String& name, bool isModified)
{
    for (std::size_t item = 0; item < presetItemToIndex.size(); ++item)
        if (presetItemToIndex[item] == index)
        {
            presetBox->setSelectedId (static_cast<int> (item) + 1, juce::dontSendNotification);
            break;
        }

    // An asterisk is the conventional "edited since loaded" marker, and it matters here:
    // without it, stepping to the next preset would silently discard your edits.
    presetBox->setText (isModified ? name + " *" : name, juce::dontSendNotification);
}

void TopBar::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (theme::panelRaised);
    g.fillRect (bounds);

    // A prism rule along the bottom edge: the plugin's signature, and it costs nothing.
    const auto ruleY = bounds.getBottom() - 2.0f;
    const auto steps = 64;

    for (int i = 0; i < steps; ++i)
    {
        const auto t = static_cast<float> (i) / static_cast<float> (steps);
        const auto w = bounds.getWidth() / static_cast<float> (steps) + 1.0f;

        g.setColour (theme::prism (t).withAlpha (0.75f));
        g.fillRect (bounds.getX() + t * bounds.getWidth(), ruleY, w, 2.0f);
    }

    g.setColour (theme::neon);
    g.setFont (juce::FontOptions (15.5f, juce::Font::bold));
    g.drawText ("VIIBEDEALER", bounds.reduced (12.0f, 0.0f).withTrimmedBottom (2.0f),
                juce::Justification::centredLeft, false);

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (8.5f));
    g.drawText (status, bounds.reduced (12.0f, 0.0f).withTrimmedBottom (2.0f)
                              .withTrimmedLeft (344.0f).removeFromLeft (164.0f),
                juce::Justification::centredLeft, false);
}

void TopBar::resized()
{
    auto area = getLocalBounds().withTrimmedBottom (2).reduced (10, 7);

    // Right to left: randomise, then the locks, then A/B.
    // Right to left: randomise, the locks, the character controls, then A/B.
    // Right to left: randomise, the locks, the character controls, then A/B.
    randomButton->setBounds (area.removeFromRight (56));
    area.removeFromRight (5);

    for (int i = state::numSections - 1; i >= 0; --i)
        locks[static_cast<std::size_t> (i)]->setBounds (area.removeFromRight (31).reduced (1, 0));

    area.removeFromRight (7);

    sceneButton->setBounds (area.removeFromRight (64));
    area.removeFromRight (3);
    performanceToggle->setBounds (area.removeFromRight (34));
    area.removeFromRight (3);
    characterToggle->setBounds (area.removeFromRight (34));
    area.removeFromRight (7);

    copyButton->setBounds (area.removeFromRight (36));
    area.removeFromRight (3);
    slotB->setBounds (area.removeFromRight (22));
    slotA->setBounds (area.removeFromRight (22));

    // Left: room for the wordmark, then the preset browser.
    area.removeFromLeft (102);
    area.removeFromLeft (6);

    prevPreset->setBounds (area.removeFromLeft (22));
    area.removeFromLeft (2);
    presetBox->setBounds (area.removeFromLeft (juce::jmax (90, juce::jmin (152, area.getWidth() - 60))));
    area.removeFromLeft (2);
    nextPreset->setBounds (area.removeFromLeft (22));
    area.removeFromLeft (3);
    savePreset->setBounds (area.removeFromLeft (40));
}

} // namespace vbd::gui
