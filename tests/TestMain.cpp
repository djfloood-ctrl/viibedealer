#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <iostream>

namespace
{
/**
    Renders the editor to a PNG so the layout can actually be looked at.

    A GUI that compiles and passes its tests can still be laid out badly, and there is no
    substitute for seeing it. Invoked as `viibedealer_tests --shot <file.png> [width]`.
*/
int renderScreenshot (const juce::String& path, int width, int blocksOfAudio)
{
    vbd::VbdProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    // Push audio through first so the visualisers have something real to draw.
    juce::Random rng { 1 };

    for (int b = 0; b < blocksOfAudio; ++b)
    {
        juce::AudioBuffer<float> buf { 2, 512 };

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
            {
                const auto t = static_cast<float> (b * 512 + i) / 48000.0f;
                // A growly bass-ish signal: low fundamental plus noise.
                buf.setSample (ch, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 55.0f * t)
                                    + 0.25f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * t)
                                    + 0.1f * (rng.nextFloat() * 2.0f - 1.0f));
            }

        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor { proc.createEditor() };

    if (editor == nullptr)
    {
        std::cout << "createEditor returned null" << std::endl;
        return 1;
    }

    const auto height = static_cast<int> (static_cast<double> (width) * 620.0 / 1000.0);
    editor->setSize (width, height);

    // The visualisers interpolate towards each snapshot, so step the refresh enough
    // times for the geometry to settle rather than catching it mid-morph. There is no
    // message loop here to run the editor's timer, hence the direct calls.
    if (auto* vbdEditor = dynamic_cast<vbd::VbdEditor*> (editor.get()))
        for (int i = 0; i < 40; ++i)
            vbdEditor->refreshDisplays();

    juce::Image image { juce::Image::ARGB, width, height, true };
    {
        juce::Graphics g { image };
        editor->paintEntireComponent (g, true);
    }

    juce::File out { path };
    out.deleteFile();

    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> stream { out.createOutputStream() };

    if (stream == nullptr || ! png.writeImageToStream (image, *stream))
    {
        std::cout << "failed to write " << path << std::endl;
        return 1;
    }

    stream->flush();
    std::cout << "wrote " << out.getFullPathName() << " (" << width << "x" << height << ")"
              << std::endl;
    return 0;
}

/**
    Measures how much CPU the plugin actually uses, rather than asserting it is "light".

    Reports the realtime factor: how many seconds of audio one instance renders per second
    of wall clock. A factor of 50x means one instance costs about 2% of a core, so the
    "several instances at FFT 2048" target in PLAN section 3 can be read straight off it.
*/
int runBenchmark (int fftSizeIndex, bool withDelays)
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;
    constexpr double seconds = 20.0;

    vbd::VbdProcessor proc;

    auto setParam = [&proc] (const char* id, float norm)
    {
        if (auto* param = proc.state().getParameter (id))
            param->setValueNotifyingHost (norm);
    };

    setParam (vbd::pid::engFftSize, static_cast<float> (fftSizeIndex) / 3.0f);
    setParam (vbd::pid::fracShatter, 0.6f);     // slicer doing real work

    if (withDelays)
    {
        setParam (vbd::pid::sdlyOn, 1.0f);
        setParam (vbd::pid::dlyOn, 1.0f);
        setParam (vbd::pid::dlyDiffuse, 0.8f);
    }

    proc.prepareToPlay (sr, block);

    const auto totalBlocks = static_cast<int> (seconds * sr / block);

    juce::AudioBuffer<float> buf { 2, block };
    juce::Random rng { 1 };

    // Warm up, so page faults and first-touch allocation do not land in the measurement.
    for (int b = 0; b < 40; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buf.setSample (ch, i, rng.nextFloat() * 0.5f - 0.25f);

        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
    }

    const auto start = juce::Time::getMillisecondCounterHiRes();

    for (int b = 0; b < totalBlocks; ++b)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < block; ++i)
                buf.setSample (ch, i, rng.nextFloat() * 0.5f - 0.25f);

        juce::MidiBuffer midi;
        proc.processBlock (buf, midi);
    }

    const auto elapsed = (juce::Time::getMillisecondCounterHiRes() - start) * 0.001;
    const auto realtimeFactor = seconds / juce::jmax (1.0e-6, elapsed);
    const auto corePercent = 100.0 / realtimeFactor;

    const int sizes[] = { 512, 1024, 2048, 4096 };

    std::cout << "  FFT " << sizes[juce::jlimit (0, 3, fftSizeIndex)]
              << (withDelays ? "  delays ON " : "  delays off")
              << "   realtime x" << juce::String (realtimeFactor, 1)
              << "   ~" << juce::String (corePercent, 2) << "% of one core"
              << "   (~" << static_cast<int> (100.0 / corePercent) << " instances per core)"
              << std::endl;

    return 0;
}
}

int main (int argc, char** argv)
{
    // The processor touches fonts and timers during construction, so the JUCE
    // message-manager singletons need to exist for the lifetime of the run.
    juce::ScopedJuceInitialiser_GUI juceInit;

    if (argc >= 2 && juce::String (argv[1]) == "--bench")
    {
        std::cout << "VIIBEDEALER CPU benchmark (48 kHz, 512-sample blocks, stereo)"
                  << std::endl << std::endl;

        for (int fftIndex = 0; fftIndex < 4; ++fftIndex)
            runBenchmark (fftIndex, false);

        std::cout << std::endl;
        runBenchmark (2, true);
        std::cout << std::endl;

        return 0;
    }

    if (argc >= 3 && juce::String (argv[1]) == "--shot")
    {
        const auto width = argc >= 4 ? juce::String (argv[3]).getIntValue() : 1000;
        return renderScreenshot (juce::String (argv[2]),
                                 width > 0 ? width : 1000,
                                 60);
    }

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure (false);
    runner.setPassesAreLogged (false);
    runner.runAllTests();

    int failures = 0;
    int total    = 0;

    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* r = runner.getResult (i))
        {
            total    += r->passes + r->failures;
            failures += r->failures;

            if (r->failures > 0)
                std::cout << "FAILED: " << r->unitTestName << " / " << r->subcategoryName
                          << "  (" << r->failures << " failure(s))" << std::endl;
        }
    }

    std::cout << "\n==== " << (failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED")
              << " ====\nassertions: " << total << "   failures: " << failures << std::endl;

    return failures == 0 ? 0 : 1;
}
