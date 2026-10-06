#include <juce_audio_processors/juce_audio_processors.h>

#include "Params.h"

namespace
{

class ParamLayoutTests final : public juce::UnitTest
{
public:
    ParamLayoutTests() : juce::UnitTest ("Parameter layout", "params") {}

    void runTest() override
    {
        // A throwaway processor whose only job is to own an APVTS built from our layout.
        struct Host final : juce::AudioProcessor
        {
            Host() : apvts (*this, nullptr, "params", vbd::createParameterLayout()) {}

            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            const juce::String getName() const override { return "host"; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            double getTailLengthSeconds() const override { return 0.0; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}

            juce::AudioProcessorValueTreeState apvts;
        };

        Host host;
        const auto& params = host.getParameters();

        beginTest ("parameter count is as planned");
        {
            // Guards against an accidental deletion during a refactor.
            expect (params.size() >= 60,
                    "expected at least 60 parameters, found " + juce::String (params.size()));
        }

        beginTest ("every ID is unique and non-empty");
        {
            juce::StringArray ids;

            for (auto* p : params)
            {
                auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (p);
                expect (withId != nullptr, "parameter without an ID");

                if (withId == nullptr)
                    continue;

                expect (withId->paramID.isNotEmpty(), "empty parameter ID");
                expect (! ids.contains (withId->paramID),
                        "duplicate parameter ID: " + withId->paramID);
                ids.add (withId->paramID);
            }
        }

        beginTest ("every parameter has a display name");
        {
            for (auto* p : params)
                expect (p->getName (128).isNotEmpty(), "parameter with no name");
        }

        beginTest ("defaults round-trip through normalised space");
        {
            for (auto* p : params)
            {
                const auto def = p->getDefaultValue();

                expect (def >= 0.0f && def <= 1.0f,
                        "default out of normalised range: " + p->getName (64));

                // Setting a parameter to its own default must be a fixed point, otherwise
                // host A/B comparison and preset recall drift.
                p->setValueNotifyingHost (def);
                expectWithinAbsoluteError (p->getValue(), def, 1.0e-4f,
                                           "default is not a fixed point: " + p->getName (64));
            }
        }

        beginTest ("text conversion survives the full range");
        {
            for (auto* p : params)
            {
                for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    const auto text = p->getText (v, 64);
                    expect (text.isNotEmpty(),
                            "empty text for " + p->getName (64) + " at " + juce::String (v));
                }
            }
        }

        beginTest ("delay modules default to off");
        {
            auto* sdly = host.apvts.getParameter (vbd::pid::sdlyOn);
            auto* dly  = host.apvts.getParameter (vbd::pid::dlyOn);

            expect (sdly != nullptr && dly != nullptr, "delay toggles missing");

            if (sdly != nullptr) expect (sdly->getValue() < 0.5f, "spectral delay not off by default");
            if (dly  != nullptr) expect (dly->getValue()  < 0.5f, "stereo delay not off by default");
        }

        beginTest ("sync divisions are musically consistent");
        {
            const auto choices = vbd::syncDivChoices();
            const auto n = choices.size();

            expect (n == 17, "unexpected sync division count: " + juce::String (n));

            // Deliberately NOT asserting monotonically descending duration. The list uses
            // the conventional grouped order (1/2D, 1/2, 1/2T, 1/4D, ...), in which a
            // dotted value is longer than the triplet of the division above it --
            // 1/4D is 1.5 quarters against 1/2T's 1.333. Sorting by duration instead would
            // give a combo box no musician wants to read.
            for (int i = 0; i < n; ++i)
            {
                const auto q = vbd::syncDivToQuarterNotes (i);
                expect (q > 0.0, "non-positive duration at index " + juce::String (i));
                expect (q <= 4.0, "duration longer than a whole note at index " + juce::String (i));
            }

            beginTest ("sync divisions: no duplicate durations or labels");
            {
                for (int i = 0; i < n; ++i)
                {
                    for (int j = i + 1; j < n; ++j)
                    {
                        expect (choices[i] != choices[j],
                                "duplicate division label: " + choices[i]);
                        expect (std::abs (vbd::syncDivToQuarterNotes (i)
                                          - vbd::syncDivToQuarterNotes (j)) > 1.0e-9,
                                "duplicate division duration: " + choices[i] + " / " + choices[j]);
                    }
                }
            }

            beginTest ("sync divisions: dotted is 1.5x and triplet is 2/3x the plain value");
            {
                // Plain divisions, in the order they appear in the list.
                const int plainIdx[] = { 0, 2, 5, 8, 11, 14, 16 };

                for (int i = 1; i < static_cast<int> (std::size (plainIdx)); ++i)
                {
                    const auto prev = vbd::syncDivToQuarterNotes (plainIdx[i - 1]);
                    const auto cur  = vbd::syncDivToQuarterNotes (plainIdx[i]);

                    expectWithinAbsoluteError (cur, prev * 0.5, 1.0e-9,
                                               "plain divisions must halve: index "
                                               + juce::String (plainIdx[i]));
                }

                // Each dotted/triplet entry relates to the plain division that follows it.
                const int dottedIdx[] = { 1, 4, 7, 10, 13 };
                for (int idx : dottedIdx)
                    expectWithinAbsoluteError (vbd::syncDivToQuarterNotes (idx),
                                               vbd::syncDivToQuarterNotes (idx + 1) * 1.5, 1.0e-9,
                                               "dotted value wrong at index " + juce::String (idx));

                const int tripletIdx[] = { 3, 6, 9, 12, 15 };
                for (int idx : tripletIdx)
                    expectWithinAbsoluteError (vbd::syncDivToQuarterNotes (idx),
                                               vbd::syncDivToQuarterNotes (idx - 1) * 2.0 / 3.0,
                                               1.0e-9,
                                               "triplet value wrong at index " + juce::String (idx));
            }
        }

        beginTest ("FFT size mapping covers exactly the documented sizes");
        {
            expect (vbd::fftSizeForIndex (0) == 512);
            expect (vbd::fftSizeForIndex (1) == 1024);
            expect (vbd::fftSizeForIndex (2) == 2048);
            expect (vbd::fftSizeForIndex (3) == 4096);

            for (int i = 0; i < vbd::fftSizeChoices().size(); ++i)
            {
                const auto size = vbd::fftSizeForIndex (i);
                expect (juce::isPowerOfTwo (size), "FFT size is not a power of two");
                expect (vbd::fftSizeChoices()[i].getIntValue() == size,
                        "choice label disagrees with mapped FFT size at index " + juce::String (i));
            }
        }
    }
};

static ParamLayoutTests paramLayoutTests;

} // namespace
