#include "PresetManager.h"

#include "../Identity.h"
#include "../Params.h"

namespace vbd::state
{

namespace
{
    // Choice indices, spelled out so the preset table reads as music rather than numbers.
    namespace carrierSrc { constexpr float sidechain = 0, chord = 1, self = 2; }
    namespace osc { constexpr float supersaw = 0, saw = 1, square = 2, noise = 3; }
    namespace chordType { constexpr float maj = 0, min = 1, maj7 = 2, min9 = 3,
                                          sus2 = 4, add9 = 5, power = 6, custom = 7; }
    namespace mode { constexpr float vocode = 0, magMorph = 1, phaseMorph = 2, cross = 3; }
    namespace fft { constexpr float n1024 = 1, n2048 = 2, n4096 = 3; }
    namespace pattern { constexpr float cantor = 0, golden = 1, thueMorse = 2,
                                        sierpinski = 3, lSystem = 4; }
    namespace div { constexpr float d1_4 = 5, d1_8 = 8, d1_8t = 9, d1_16 = 11,
                                    d1_16t = 12, d1_32 = 14; }
    namespace delayMode { constexpr float stereo = 0, pingPong = 1, dual = 2; }
    namespace sat { constexpr float softClip = 0, tube = 1, wavefold = 2; }
}

const std::vector<FactoryPreset>& PresetManager::factoryPresets()
{
    // Eighteen presets aimed at colour bass. Each is a diff from the defaults, so what a
    // preset actually does is visible at a glance.
    static const std::vector<FactoryPreset> presets = {

        // ---------------------------------------------------------------- chord growls
        { "Init Chord", "Chord Growl", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::min },
            { pid::engMode, mode::vocode }, { pid::engMorph, 100.0f } } },

        { "Supersaw Imprint", "Chord Growl", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordOsc, osc::supersaw },
            { pid::chordType, chordType::min }, { pid::chordSpread, 48.0f },
            { pid::chordDetune, 22.0f }, { pid::engMorph, 100.0f },
            { pid::engEnvRes, 38.0f }, { pid::engSens, -58.0f },
            { pid::colDrive, 6.0f }, { pid::colWidth, 125.0f } } },

        { "Min9 Cathedral", "Chord Growl", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::min9 },
            { pid::chordSpread, 70.0f }, { pid::chordDetune, 16.0f },
            { pid::engFftSize, fft::n4096 }, { pid::engMorph, 100.0f },
            { pid::engEnvRes, 30.0f }, { pid::colWidth, 140.0f },
            { pid::dlyOn, 1.0f }, { pid::dlyMode, delayMode::pingPong },
            { pid::dlyDivL, div::d1_8 }, { pid::dlyFb, 52.0f }, { pid::dlyMix, 28.0f },
            { pid::dlyDiffuse, 60.0f } } },

        { "Power Sub", "Chord Growl", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::power },
            { pid::chordOctave, -1.0f }, { pid::chordOsc, osc::saw },
            { pid::chordSpread, 0.0f }, { pid::chordDetune, 4.0f },
            { pid::engMorph, 100.0f }, { pid::engTilt, -2.2f },
            { pid::colLoCut, 28.0f }, { pid::colDrive, 9.0f } } },

        { "Add9 Lift", "Chord Growl", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::add9 },
            { pid::chordSpread, 55.0f }, { pid::engMorph, 92.0f },
            { pid::engFormant, 3.5f }, { pid::engTilt, 1.4f },
            { pid::colWidth, 130.0f } } },

        // -------------------------------------------------------------- fractal plucks
        { "Shimmer Pluck", "Fractal Pluck", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::maj7 },
            { pid::engMorph, 100.0f }, { pid::engFormant, 7.0f },
            { pid::fracPattern, pattern::golden }, { pid::fracDepth, 4.0f },
            { pid::fracShatter, 55.0f }, { pid::fracLoHz, 300.0f },
            { pid::fracDiv, div::d1_16 }, { pid::fracGrit, 25.0f },
            { pid::colWidth, 135.0f } } },

        { "Cantor Shred", "Fractal Pluck", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordOsc, osc::supersaw },
            { pid::engMorph, 100.0f },
            { pid::fracPattern, pattern::cantor }, { pid::fracDepth, 5.0f },
            { pid::fracShatter, 78.0f }, { pid::fracGrit, 60.0f },
            { pid::fracDiv, div::d1_8 }, { pid::colDrive, 10.0f } } },

        { "Sierpinski Rain", "Fractal Pluck", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::sus2 },
            { pid::engFftSize, fft::n4096 }, { pid::engMorph, 100.0f },
            { pid::fracPattern, pattern::sierpinski }, { pid::fracDepth, 6.0f },
            { pid::fracShatter, 62.0f }, { pid::fracLoHz, 180.0f },
            { pid::fracDiv, div::d1_16 },
            { pid::sdlyOn, 1.0f }, { pid::sdlyTimeMs, 180.0f },
            { pid::sdlySpread, 72.0f }, { pid::sdlyFb, 48.0f }, { pid::sdlyMix, 55.0f } } },

        { "Golden Bell", "Fractal Pluck", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::maj7 },
            { pid::chordOsc, osc::square }, { pid::engMorph, 100.0f },
            { pid::engFormant, 12.0f }, { pid::fracPattern, pattern::golden },
            { pid::fracDepth, 5.0f }, { pid::fracShatter, 45.0f },
            { pid::fracSync, 0.0f }, { pid::fracRateHz, 0.35f },
            { pid::colWidth, 150.0f } } },

        // --------------------------------------------------------------- glitch basses
        { "Thue Stutter", "Glitch Bass", {
            { pid::carrierSource, carrierSrc::chord }, { pid::engMorph, 100.0f },
            { pid::fracPattern, pattern::thueMorse }, { pid::fracDepth, 5.0f },
            { pid::fracShatter, 70.0f }, { pid::fracGate, 85.0f },
            { pid::fracDiv, div::d1_16 }, { pid::fracGrit, 80.0f },
            { pid::fracSmooth, 1.5f } } },

        { "L-System Mangle", "Glitch Bass", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordOsc, osc::square },
            { pid::engMorph, 100.0f }, { pid::engFreqShift, 90.0f },
            { pid::fracPattern, pattern::lSystem }, { pid::fracDepth, 4.0f },
            { pid::fracSeed, 7.0f }, { pid::fracShatter, 92.0f },
            { pid::fracGate, 70.0f }, { pid::fracDiv, div::d1_32 },
            { pid::fracGrit, 100.0f }, { pid::colDrive, 12.0f },
            { pid::colSatType, sat::wavefold } } },

        { "Triplet Chop", "Glitch Bass", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::min9 },
            { pid::engMorph, 100.0f }, { pid::fracPattern, pattern::cantor },
            { pid::fracDepth, 4.0f }, { pid::fracShatter, 88.0f },
            { pid::fracGate, 92.0f }, { pid::fracDiv, div::d1_16t },
            { pid::fracGrit, 90.0f }, { pid::fracSmooth, 1.0f } } },

        { "Inverted Teeth", "Glitch Bass", {
            { pid::carrierSource, carrierSrc::chord }, { pid::engMorph, 100.0f },
            { pid::fracPattern, pattern::thueMorse }, { pid::fracDepth, 6.0f },
            { pid::fracInvert, 1.0f }, { pid::fracShatter, 80.0f },
            { pid::fracAsym, 55.0f }, { pid::fracDiv, div::d1_8t },
            { pid::colSatType, sat::tube }, { pid::colDrive, 14.0f } } },

        // ------------------------------------------------------- spectral delay tails
        { "Wide Spectral Tail", "Delay Tail", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::min9 },
            { pid::engFftSize, fft::n4096 }, { pid::engMorph, 100.0f },
            { pid::fracPattern, pattern::golden }, { pid::fracDepth, 4.0f },
            { pid::fracShatter, 35.0f },
            { pid::sdlyOn, 1.0f }, { pid::sdlyTimeMs, 420.0f },
            { pid::sdlySpread, 90.0f }, { pid::sdlyFb, 62.0f },
            { pid::sdlyDamp, 45.0f }, { pid::sdlyMix, 70.0f },
            { pid::colWidth, 160.0f } } },

        { "Ping Wash", "Delay Tail", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::maj7 },
            { pid::engMorph, 100.0f },
            { pid::dlyOn, 1.0f }, { pid::dlyMode, delayMode::pingPong },
            { pid::dlyDivL, div::d1_8 }, { pid::dlyFb, 64.0f }, { pid::dlyMix, 42.0f },
            { pid::dlyDiffuse, 85.0f }, { pid::dlyModDepth, 35.0f },
            { pid::dlyLp, 5200.0f }, { pid::dlyDuck, 45.0f },
            { pid::colWidth, 145.0f } } },

        { "Dual Drift", "Delay Tail", {
            { pid::carrierSource, carrierSrc::chord }, { pid::engMorph, 100.0f },
            { pid::dlyOn, 1.0f }, { pid::dlyMode, delayMode::dual },
            { pid::dlySync, 0.0f }, { pid::dlyMsL, 287.0f }, { pid::dlyMsR, 433.0f },
            { pid::dlyFb, 58.0f }, { pid::dlyMix, 38.0f },
            { pid::dlyModRate, 0.18f }, { pid::dlyModDepth, 55.0f },
            { pid::dlySat, 45.0f }, { pid::dlyDiffuse, 50.0f } } },

        // ------------------------------------------------------------- spectral oddity
        { "Freeze Drone", "Spectral", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordType, chordType::min9 },
            { pid::engFftSize, fft::n4096 }, { pid::engMorph, 100.0f },
            { pid::engFreeze, 1.0f }, { pid::engEnvRes, 22.0f },
            { pid::fracPattern, pattern::golden }, { pid::fracShatter, 30.0f },
            { pid::fracSync, 0.0f }, { pid::fracRateHz, 0.12f },
            { pid::colWidth, 155.0f }, { pid::outMix, 70.0f } } },

        { "Noise Vox", "Spectral", {
            { pid::carrierSource, carrierSrc::chord }, { pid::chordOsc, osc::noise },
            { pid::engMorph, 100.0f }, { pid::engEnvCepstral, 1.0f },
            { pid::engEnvRes, 72.0f }, { pid::engFormant, -4.0f },
            { pid::engSens, -52.0f }, { pid::colDrive, 7.0f },
            { pid::colHiCut, 12000.0f } } },
    };

    return presets;
}

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state) : apvts (state)
{
    refresh();
}

juce::File PresetManager::userPresetDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
             .getChildFile (identity::companyName)
             .getChildFile (identity::productName)
             .getChildFile ("Presets");
}

void PresetManager::refresh()
{
    const auto previousName = entries.empty() ? juce::String() : getCurrentName();

    entries.clear();

    const auto& factory = factoryPresets();

    for (int i = 0; i < static_cast<int> (factory.size()); ++i)
        entries.push_back ({ factory[static_cast<std::size_t> (i)].name,
                             factory[static_cast<std::size_t> (i)].category,
                             true, i, {} });

    const auto dir = userPresetDirectory();

    if (dir.isDirectory())
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*.xml");
        files.sort();

        for (const auto& file : files)
            entries.push_back ({ file.getFileNameWithoutExtension(), "User", false, -1, file });
    }

    // Keep pointing at the same preset across a rescan where possible.
    currentIndex = juce::jlimit (0, juce::jmax (0, getNumPresets() - 1), currentIndex);

    if (previousName.isNotEmpty())
        for (int i = 0; i < getNumPresets(); ++i)
            if (entries[static_cast<std::size_t> (i)].name == previousName)
            {
                currentIndex = i;
                break;
            }
}

juce::String PresetManager::getCurrentName() const
{
    if (entries.empty())
        return {};

    return entries[static_cast<std::size_t> (juce::jlimit (0, getNumPresets() - 1, currentIndex))].name;
}

void PresetManager::applyFactory (const FactoryPreset& preset)
{
    // Reset first: a preset lists only what it changes, so anything it does not mention
    // must land on its default rather than inheriting whatever was there before.
    for (auto* param : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& value : preset.values)
    {
        auto* param = apvts.getParameter (value.id);

        if (param == nullptr)
        {
            // A preset naming a parameter that no longer exists is a bug, not something to
            // paper over silently -- but it must not stop the rest of the preset loading.
            jassertfalse;
            continue;
        }

        param->setValueNotifyingHost (
            juce::jlimit (0.0f, 1.0f, param->convertTo0to1 (value.value)));
    }
}

bool PresetManager::load (int index)
{
    if (index < 0 || index >= getNumPresets())
        return false;

    const auto& entry = entries[static_cast<std::size_t> (index)];

    if (entry.isFactory)
    {
        const auto& factory = factoryPresets();

        if (entry.factoryIndex < 0 || entry.factoryIndex >= static_cast<int> (factory.size()))
            return false;

        applyFactory (factory[static_cast<std::size_t> (entry.factoryIndex)]);
    }
    else
    {
        if (! entry.file.existsAsFile())
            return false;

        const auto xml = juce::XmlDocument::parse (entry.file);

        if (xml == nullptr)
            return false;

        auto tree = juce::ValueTree::fromXml (*xml);

        // Accept both the versioned wrapper and a bare parameter tree.
        if (tree.hasType (identity::stateTag))
            tree = tree.getChildWithName (identity::apvtsTag);

        if (! tree.isValid() || ! tree.hasType (identity::apvtsTag))
            return false;

        apvts.replaceState (tree);
    }

    currentIndex = index;
    modified = false;
    return true;
}

bool PresetManager::step (int delta)
{
    if (getNumPresets() == 0)
        return false;

    auto next = currentIndex + delta;
    const auto count = getNumPresets();
    next = ((next % count) + count) % count;

    return load (next);
}

int PresetManager::saveUserPreset (const juce::String& name)
{
    const auto cleaned = juce::File::createLegalFileName (name.trim());

    if (cleaned.isEmpty())
        return -1;

    const auto dir = userPresetDirectory();

    if (! dir.isDirectory() && ! dir.createDirectory().wasOk())
        return -1;

    const auto file = dir.getChildFile (cleaned + ".xml");

    // Saved in the same versioned wrapper the host gets, so a preset and a session
    // restore identically rather than through two slightly different paths.
    juce::ValueTree root { identity::stateTag };
    root.setProperty ("stateVersion", identity::stateVersion, nullptr);
    root.appendChild (apvts.copyState(), nullptr);

    const auto xml = root.createXml();

    if (xml == nullptr || ! xml->writeTo (file))
        return -1;

    refresh();

    for (int i = 0; i < getNumPresets(); ++i)
        if (! entries[static_cast<std::size_t> (i)].isFactory
            && entries[static_cast<std::size_t> (i)].file == file)
        {
            currentIndex = i;
            modified = false;
            return i;
        }

    return -1;
}

bool PresetManager::deleteUserPreset (int index)
{
    if (index < 0 || index >= getNumPresets())
        return false;

    const auto& entry = entries[static_cast<std::size_t> (index)];

    if (entry.isFactory || ! entry.file.existsAsFile())
        return false;

    if (! entry.file.deleteFile())
        return false;

    refresh();
    return true;
}

} // namespace vbd::state
