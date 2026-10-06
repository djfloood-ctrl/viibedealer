#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace vbd::state
{

/** The lockable sections, matching the GUI panels. */
enum class Section
{
    carrier = 0,
    engine,
    fractal,
    delays,
    output,
    count
};

inline constexpr int numSections = static_cast<int> (Section::count);

const char* sectionName (Section) noexcept;

/** Three-letter form, for the narrow lock buttons in the top bar. */
const char* sectionShortName (Section) noexcept;

/** Which parameter group a parameter id belongs to. */
Section sectionFor (const juce::String& paramId) noexcept;

/**
    Randomises unlocked sections.

    Deliberately not uniform over the whole range: a uniform roll would mostly produce
    unusable settings (FFT at random, Mix near zero, feedback near maximum). Instead each
    parameter is nudged within a musically plausible window, structural choices like FFT
    size are left alone, and the module on/off switches keep their state so Randomize
    cannot silently turn a delay on and bury the dry signal.
*/
void randomize (juce::AudioProcessorValueTreeState&,
                juce::uint32 lockMask,
                juce::Random&);

} // namespace vbd::state
