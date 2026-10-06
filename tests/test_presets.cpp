#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"
#include "state/PresetManager.h"

#include <map>
#include <set>

namespace
{

class PresetTests final : public juce::UnitTest
{
public:
    PresetTests() : juce::UnitTest ("Presets", "state") {}

    void runTest() override
    {
        beginTest ("the factory set is the size the spec asks for");
        {
            const auto& factory = vbd::state::PresetManager::factoryPresets();

            expect (factory.size() >= 15 && factory.size() <= 20,
                    "expected 15-20 factory presets, found "
                    + juce::String (static_cast<int> (factory.size())));
        }

        beginTest ("every factory preset names only real parameters, with in-range values");
        {
            vbd::VbdProcessor p;
            const auto& factory = vbd::state::PresetManager::factoryPresets();

            std::set<juce::String> names;

            for (const auto& preset : factory)
            {
                const juce::String presetName { preset.name };

                expect (presetName.isNotEmpty(), "preset with no name");
                expect (names.insert (presetName).second, "duplicate preset name: " + presetName);
                expect (juce::String (preset.category).isNotEmpty(),
                        "preset with no category: " + presetName);

                for (const auto& value : preset.values)
                {
                    auto* param = p.state().getParameter (value.id);

                    expect (param != nullptr,
                            "preset '" + presetName + "' names a parameter that does not exist: "
                            + juce::String (value.id));

                    if (param == nullptr)
                        continue;

                    // A value outside the parameter's range would silently clamp, which
                    // is how a preset ends up quietly not sounding like its author meant.
                    const auto range = param->getNormalisableRange();

                    expect (value.value >= range.start - 1.0e-4f
                            && value.value <= range.end + 1.0e-4f,
                            "preset '" + presetName + "': " + juce::String (value.id) + " = "
                            + juce::String (value.value) + " is outside ["
                            + juce::String (range.start) + ", " + juce::String (range.end) + "]");
                }
            }
        }

        beginTest ("loading a preset is exact, and resets what it does not mention");
        {
            vbd::VbdProcessor p;
            vbd::state::PresetManager manager { p.state() };

            // Move something no preset touches, then load one: it must come back to its
            // default rather than linger.
            auto* seed = p.state().getParameter (vbd::pid::fracSeed);
            expect (seed != nullptr);

            if (seed != nullptr)
                seed->setValueNotifyingHost (0.9f);

            const auto& factory = vbd::state::PresetManager::factoryPresets();

            for (std::size_t i = 0; i < factory.size(); ++i)
            {
                expect (manager.load (static_cast<int> (i)),
                        "failed to load factory preset " + juce::String (static_cast<int> (i)));

                const auto& preset = factory[i];

                for (const auto& value : preset.values)
                {
                    auto* param = p.state().getParameter (value.id);

                    if (param == nullptr)
                        continue;

                    const auto actual = param->convertFrom0to1 (param->getValue());
                    const auto tolerance = juce::jmax (0.01f, std::abs (value.value) * 0.002f);

                    expectWithinAbsoluteError (actual, value.value, tolerance,
                                               juce::String (preset.name) + ": "
                                               + juce::String (value.id) + " did not take");
                }
            }

            // The untouched parameter is back at its default.
            if (seed != nullptr)
                expectWithinAbsoluteError (seed->getValue(), seed->getDefaultValue(), 1.0e-5f,
                                           "a parameter no preset mentions kept its old value");
        }

        beginTest ("every factory preset produces finite, bounded audio");
        {
            vbd::VbdProcessor p;
            vbd::state::PresetManager manager { p.state() };
            p.prepareToPlay (48000.0, 256);

            const auto& factory = vbd::state::PresetManager::factoryPresets();

            for (std::size_t i = 0; i < factory.size(); ++i)
            {
                manager.load (static_cast<int> (i));

                float worst = 0.0f;

                for (int b = 0; b < 48; ++b)
                {
                    juce::AudioBuffer<float> buf { 2, 256 };

                    for (int ch = 0; ch < 2; ++ch)
                        for (int s = 0; s < 256; ++s)
                        {
                            const auto t = static_cast<float> (b * 256 + s) / 48000.0f;
                            buf.setSample (ch, s,
                                0.6f * std::sin (juce::MathConstants<float>::twoPi * 55.0f * t)
                              + 0.3f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * t));
                        }

                    juce::MidiBuffer midi;
                    p.processBlock (buf, midi);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int s = 0; s < 256; ++s)
                        {
                            const auto v = buf.getSample (ch, s);

                            if (! std::isfinite (v))
                            {
                                expect (false, juce::String (factory[i].name)
                                               + " produced non-finite output");
                                return;
                            }

                            worst = juce::jmax (worst, std::abs (v));
                        }
                }

                expect (worst < 2.0f, juce::String (factory[i].name)
                                      + " peaked at " + juce::String (worst));

                // A preset that makes no sound is a broken preset.
                expect (worst > 1.0e-4f, juce::String (factory[i].name) + " was silent");
            }
        }

        beginTest ("user presets round-trip through XML");
        {
            vbd::VbdProcessor p;
            vbd::state::PresetManager manager { p.state() };

            const auto testName = "__vbd_unit_test_preset__";
            const auto file = vbd::state::PresetManager::userPresetDirectory()
                                  .getChildFile (juce::String (testName) + ".xml");
            file.deleteFile();

            // Set a recognisable state.
            auto* morph = p.state().getParameter (vbd::pid::engMorph);
            auto* pattern = p.state().getParameter (vbd::pid::fracPattern);
            expect (morph != nullptr && pattern != nullptr);

            if (morph == nullptr || pattern == nullptr)
                return;

            morph->setValueNotifyingHost (0.37f);
            pattern->setValueNotifyingHost (1.0f);

            const auto savedIndex = manager.saveUserPreset (testName);
            expect (savedIndex >= 0, "saving a user preset failed");
            expect (file.existsAsFile(), "no XML file was written");

            // Disturb the state, then load the preset back.
            morph->setValueNotifyingHost (0.9f);
            pattern->setValueNotifyingHost (0.0f);

            expect (manager.load (savedIndex), "loading the saved preset failed");

            expectWithinAbsoluteError (morph->getValue(), 0.37f, 1.0e-4f,
                                       "morph did not survive the round trip");
            expectWithinAbsoluteError (pattern->getValue(), 1.0f, 1.0e-4f,
                                       "pattern did not survive the round trip");

            expect (manager.deleteUserPreset (savedIndex), "deleting the preset failed");
            expect (! file.existsAsFile(), "the XML file was not removed");
        }

        beginTest ("stepping wraps at both ends");
        {
            vbd::VbdProcessor p;
            vbd::state::PresetManager manager { p.state() };

            const auto count = manager.getNumPresets();
            expect (count > 0);

            manager.load (0);
            expect (manager.step (-1), "stepping back from the first preset failed");
            expectEquals (manager.getCurrentIndex(), count - 1, "did not wrap to the end");

            expect (manager.step (1), "stepping forward from the last preset failed");
            expectEquals (manager.getCurrentIndex(), 0, "did not wrap to the start");
        }

        beginTest ("out-of-range indices are refused rather than crashing");
        {
            vbd::VbdProcessor p;
            vbd::state::PresetManager manager { p.state() };

            expect (! manager.load (-1));
            expect (! manager.load (manager.getNumPresets()));
            expect (! manager.load (999999));
            expect (! manager.deleteUserPreset (0), "a factory preset was deletable");
        }
    }
};

static PresetTests presetTests;

} // namespace
