#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace vbd::state
{

/** One parameter override in a factory preset, in real units rather than normalised. */
struct PresetValue
{
    const char* id;
    float value;   // dB, %, Hz, cents, semitones, or a choice index
};

struct FactoryPreset
{
    const char* name;
    const char* category;
    std::vector<PresetValue> values;
};

/**
    Factory and user presets.

    Factory presets are a sparse list of overrides in real units: everything a preset does
    not mention is reset to its default first. That keeps each one readable as a diff from
    the plugin's resting state, and means adding a parameter later does not silently leave
    old presets carrying a stale value for it.

    User presets are XML files in the user's application data directory -- the same
    versioned state the host saves, so a preset and a session round-trip identically.
*/
class PresetManager
{
public:
    struct Entry
    {
        juce::String name;
        juce::String category;
        bool isFactory = true;
        int factoryIndex = -1;
        juce::File file;
    };

    explicit PresetManager (juce::AudioProcessorValueTreeState&);

    /** Rescans the user preset directory. Factory presets are compiled in. */
    void refresh();

    const std::vector<Entry>& getEntries() const noexcept { return entries; }
    int getNumPresets() const noexcept { return static_cast<int> (entries.size()); }

    int getCurrentIndex() const noexcept { return currentIndex; }
    juce::String getCurrentName() const;

    /** True when a parameter has moved since the preset was loaded. */
    bool isModified() const noexcept { return modified; }
    void markModified() noexcept { modified = true; }

    bool load (int index);
    bool step (int delta);

    /** Saves the current state as a user preset. Returns the new index, or -1. */
    int saveUserPreset (const juce::String& name);
    bool deleteUserPreset (int index);

    static juce::File userPresetDirectory();
    static const std::vector<FactoryPreset>& factoryPresets();

    /** Applies a factory preset: every parameter to its default, then the overrides. */
    void applyFactory (const FactoryPreset&);

private:
    juce::AudioProcessorValueTreeState& apvts;
    std::vector<Entry> entries;
    int currentIndex = 0;
    bool modified = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace vbd::state
