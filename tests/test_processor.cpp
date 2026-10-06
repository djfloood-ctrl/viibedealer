#include <juce_audio_processors/juce_audio_processors.h>

#include "PluginProcessor.h"

#include <iostream>
#include <random>

namespace
{

/** Fills a buffer with one of the pathological signals from the test matrix. */
enum class Signal { silence, noise, fullScaleSine, dc, step };

void fillSignal (juce::AudioBuffer<float>& b, Signal sig, double sampleRate, int seed = 1)
{
    std::mt19937 rng { static_cast<std::uint32_t> (seed) };
    std::uniform_real_distribution<float> dist { -1.0f, 1.0f };

    const auto n = b.getNumSamples();

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getWritePointer (ch);

        for (int i = 0; i < n; ++i)
        {
            switch (sig)
            {
                case Signal::silence:       d[i] = 0.0f; break;
                case Signal::noise:         d[i] = dist (rng); break;
                case Signal::fullScaleSine:
                    d[i] = std::sin (juce::MathConstants<float>::twoPi * 110.0f
                                     * static_cast<float> (i) / static_cast<float> (sampleRate));
                    break;
                case Signal::dc:            d[i] = 1.0f; break;
                case Signal::step:          d[i] = i < n / 2 ? 0.0f : 1.0f; break;
            }
        }
    }
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const auto* d = b.getReadPointer (ch);

        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (d[i]))
                return false;
    }

    return true;
}

class ProcessorTests final : public juce::UnitTest
{
public:
    ProcessorTests() : juce::UnitTest ("Processor contract", "processor") {}

    void runTest() override
    {
        beginTest ("bus layouts: stereo, mono, and sidechain absent/mono/stereo");
        {
            vbd::VbdProcessor p;

            using Set = juce::AudioChannelSet;

            auto layoutFor = [] (const Set& main, const Set& side)
            {
                juce::AudioProcessor::BusesLayout l;
                l.inputBuses.add (main);
                l.inputBuses.add (side);
                l.outputBuses.add (main);
                return l;
            };

            expect (p.checkBusesLayoutSupported (layoutFor (Set::stereo(), Set::disabled())),
                    "stereo main with no sidechain must be supported");
            expect (p.checkBusesLayoutSupported (layoutFor (Set::stereo(), Set::stereo())),
                    "stereo main with stereo sidechain must be supported");
            expect (p.checkBusesLayoutSupported (layoutFor (Set::stereo(), Set::mono())),
                    "stereo main with mono sidechain must be supported");
            expect (p.checkBusesLayoutSupported (layoutFor (Set::mono(), Set::disabled())),
                    "mono main with no sidechain must be supported");
        }

        beginTest ("works with no sidechain connected");
        {
            vbd::VbdProcessor p;
            expect (! p.hasActiveSidechain(),
                    "sidechain must be reported inactive when the bus is disabled by default");
        }

        beginTest ("null test: at mix = 0 the output is the latency-aligned dry signal");
        {
            vbd::VbdProcessor p;

            // mix = 0 -> dry only. The dry path runs through an integer delay of exactly
            // the reported latency, so this must be sample-exact, not merely close: any
            // error here would show up as comb filtering at intermediate mix settings.
            if (auto* mix = p.state().getParameter (vbd::pid::outMix))
                mix->setValueNotifyingHost (0.0f);

            constexpr int block = 512;
            p.prepareToPlay (48000.0, block);

            const auto latency = p.getLatencySamples();
            expect (latency > 0, "Phase B must report a non-zero latency");

            // Enough blocks to push the signal fully through the delay.
            const auto numBlocks = (latency / block) + 4;
            const auto total = numBlocks * block;

            std::vector<float> input (static_cast<std::size_t> (total));
            std::vector<float> output (static_cast<std::size_t> (total));

            std::mt19937 rng { 4242 };
            std::uniform_real_distribution<float> dist { -0.5f, 0.5f };

            for (auto& s : input)
                s = dist (rng);

            juce::AudioBuffer<float> buf { 2, block };

            for (int b = 0; b < numBlocks; ++b)
            {
                for (int ch = 0; ch < 2; ++ch)
                    buf.copyFrom (ch, 0, input.data() + b * block, block);

                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + block,
                           output.begin() + b * block);
            }

            // Compare past the latency, where the delay line is fully primed.
            double errSum = 0.0, refSum = 0.0;

            for (int i = latency; i < total; ++i)
            {
                const auto ref = static_cast<double> (input[static_cast<std::size_t> (i - latency)]);
                const auto got = static_cast<double> (output[static_cast<std::size_t> (i)]);

                errSum += (got - ref) * (got - ref);
                refSum += ref * ref;
            }

            expect (refSum > 0.0);

            const auto errDb = 10.0 * std::log10 (juce::jmax (errSum, 1.0e-30)
                                                / juce::jmax (refSum, 1.0e-30));

            expect (errDb < -120.0,
                    "null test residual " + juce::String (errDb, 1) + " dB (want < -120 dB)");
        }

        beginTest ("latency is reported before prepareToPlay");
        {
            // Some hosts query latency right after instantiation and cache the answer, so
            // reporting 0 until the first prepareToPlay would leave the plugin permanently
            // misaligned in them. (VST3 hosts refresh at setupProcessing and so will show
            // 0 before that no matter what the plugin says -- this covers the rest.)
            vbd::VbdProcessor fresh;

            expect (fresh.getLatencySamples() == 2048,
                    "fresh instance reported latency " + juce::String (fresh.getLatencySamples())
                    + ", expected the default FFT size of 2048");
            expect (fresh.getActiveFftSize() == 2048);
        }

        beginTest ("reported latency matches the FFT size and tracks changes");
        {
            vbd::VbdProcessor p;
            p.prepareToPlay (48000.0, 512);

            expect (p.getLatencySamples() == p.getActiveFftSize(),
                    "latency " + juce::String (p.getLatencySamples())
                    + " does not match FFT size " + juce::String (p.getActiveFftSize()));

            // Walk every FFT size and confirm the engine follows. The host is told from
            // the message thread, so getLatencySamples() may lag by a timer tick -- the
            // engine's own active size is the authority here.
            auto* fftParam = p.state().getParameter (vbd::pid::engFftSize);
            expect (fftParam != nullptr);

            if (fftParam != nullptr)
            {
                const int expectedSizes[] = { 512, 1024, 2048, 4096 };

                for (int i = 0; i < 4; ++i)
                {
                    fftParam->setValueNotifyingHost (static_cast<float> (i) / 3.0f);

                    juce::AudioBuffer<float> buf { 2, 512 };
                    fillSignal (buf, Signal::noise, 48000.0, i);
                    juce::MidiBuffer midi;
                    p.processBlock (buf, midi);

                    expect (p.getActiveFftSize() == expectedSizes[i],
                            "FFT size " + juce::String (expectedSizes[i])
                            + " not applied; got " + juce::String (p.getActiveFftSize()));

                    expect (allFinite (buf), "non-finite output after an FFT size change");
                }
            }
        }

        beginTest ("carrier falls back to the chord when the sidechain is unpatched");
        {
            vbd::VbdProcessor p;

            // Select the sidechain carrier with no sidechain bus enabled.
            if (auto* src = p.state().getParameter (vbd::pid::carrierSource))
                src->setValueNotifyingHost (0.0f);

            p.prepareToPlay (48000.0, 256);

            juce::AudioBuffer<float> buf { 2, 256 };
            fillSignal (buf, Signal::noise, 48000.0, 5);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);

            expect (p.isCarrierFallbackActive(),
                    "fallback flag not raised with an unpatched sidechain");
            expect (allFinite (buf));
        }

        beginTest ("no NaN/Inf across pathological signals, block sizes and sample rates");
        {
            const double rates[]  = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
            const int    blocks[] = { 1, 7, 16, 64, 512, 2048 };
            const Signal sigs[]   = { Signal::silence, Signal::noise, Signal::fullScaleSine,
                                      Signal::dc, Signal::step };

            for (auto rate : rates)
            {
                for (auto block : blocks)
                {
                    vbd::VbdProcessor p;
                    p.prepareToPlay (rate, block);

                    for (auto sig : sigs)
                    {
                        juce::AudioBuffer<float> buf { 2, block };
                        fillSignal (buf, sig, rate);

                        juce::MidiBuffer midi;
                        p.processBlock (buf, midi);

                        if (! allFinite (buf))
                        {
                            expect (false, "non-finite output at rate " + juce::String (rate)
                                           + " block " + juce::String (block));
                            return;
                        }
                    }
                }
            }

            expect (true);
        }

        beginTest ("held MIDI notes change the carrier, and thus the output");
        {
            // The Phase A version of this test asserted that MIDI left the audio alone,
            // which was only true while the plugin was a passthrough. Imprinting played
            // chords onto the bass is the entire point, so the assertion is inverted:
            // identical output with and without keys held would mean MIDI was being
            // dropped.
            constexpr int block = 256;
            constexpr int numBlocks = 24;

            auto render = [] (bool sendNotes)
            {
                vbd::VbdProcessor p;

                if (auto* src = p.state().getParameter (vbd::pid::carrierSource))
                    src->setValueNotifyingHost (1.0f / 2.0f);   // Chord carrier

                p.prepareToPlay (48000.0, block);

                std::vector<float> captured;
                captured.reserve (static_cast<std::size_t> (block * numBlocks));

                for (int b = 0; b < numBlocks; ++b)
                {
                    juce::AudioBuffer<float> buf { 2, block };
                    fillSignal (buf, Signal::noise, 48000.0, 3);

                    juce::MidiBuffer midi;

                    if (sendNotes && b == 0)
                    {
                        midi.addEvent (juce::MidiMessage::noteOn (1, 41, 1.0f), 0);
                        midi.addEvent (juce::MidiMessage::noteOn (1, 48, 1.0f), 0);
                        midi.addEvent (juce::MidiMessage::noteOn (1, 56, 1.0f), 0);
                    }

                    p.processBlock (buf, midi);

                    captured.insert (captured.end(), buf.getReadPointer (0),
                                     buf.getReadPointer (0) + block);
                }

                return captured;
            };

            vbd::VbdProcessor probe;
            expect (probe.acceptsMidi(), "plugin must accept MIDI input");

            const auto withNotes = render (true);
            const auto without   = render (false);

            expect (withNotes.size() == without.size());

            double diff = 0.0, energy = 0.0;

            for (std::size_t i = 0; i < withNotes.size(); ++i)
            {
                const auto a = static_cast<double> (withNotes[i]);
                const auto b = static_cast<double> (without[i]);

                expect (std::isfinite (a) && std::isfinite (b), "non-finite output");

                diff   += (a - b) * (a - b);
                energy += a * a + b * b;
            }

            expect (energy > 1.0e-9, "no output in either case; test proves nothing");
            expect (diff / juce::jmax (energy, 1.0e-20) > 1.0e-4,
                    "held MIDI notes did not change the output, so MIDI is being ignored");
        }

        beginTest ("vocode imprints the chord's pitches onto a broadband bass signal");
        {
            // The functional heart of the plugin, made measurable: drive it with
            // broadband noise (standing in for a bass growl) and a C minor chord carrier,
            // then confirm the output's spectrum has peaks at the chord's pitches and not
            // between them. If the vocoder were not working, the output would simply be
            // noise-shaped and these ratios would sit near 1.
            constexpr double sr = 48000.0;
            constexpr int fftOrder = 14;
            constexpr int analysisLen = 1 << fftOrder;   // 16384
            constexpr int block = 512;

            vbd::VbdProcessor p;

            auto setParam = [&p] (const char* id, float norm)
            {
                if (auto* param = p.state().getParameter (id))
                    param->setValueNotifyingHost (norm);
            };

            setParam (vbd::pid::carrierSource, 0.5f);   // Chord
            setParam (vbd::pid::engMode, 0.0f);         // Vocode
            setParam (vbd::pid::engMorph, 1.0f);        // fully imprinted
            setParam (vbd::pid::outMix, 1.0f);          // wet only
            setParam (vbd::pid::chordOsc, 1.0f / 3.0f); // plain saw: clean harmonics
            setParam (vbd::pid::chordRoot, 0.0f);       // C
            setParam (vbd::pid::chordSpread, 0.0f);     // exact intervals, no stretch
            setParam (vbd::pid::chordDetune, 0.0f);     // no detune, so peaks stay sharp
            setParam (vbd::pid::chordMidiOvr, 0.0f);    // use Root + Type, not MIDI
            setParam (vbd::pid::engFreeze, 0.0f);
            setParam (vbd::pid::engFlip, 0.0f);

            // Chord Type: minor is index 1 of 8 choices.
            if (auto* ct = p.state().getParameter (vbd::pid::chordType))
                ct->setValueNotifyingHost (1.0f / 7.0f);

            p.prepareToPlay (sr, block);

            const auto latency = p.getLatencySamples();

            // Discard the latency plus a settling margin, then capture the analysis window.
            const auto warmupBlocks = (latency / block) + 8;
            const auto captureBlocks = analysisLen / block;

            std::mt19937 rng { 777 };
            std::uniform_real_distribution<float> dist { -0.5f, 0.5f };

            std::vector<float> captured;
            captured.reserve (static_cast<std::size_t> (analysisLen));

            for (int b = 0; b < warmupBlocks + captureBlocks; ++b)
            {
                juce::AudioBuffer<float> buf { 2, block };

                for (int ch = 0; ch < 2; ++ch)
                {
                    auto* d = buf.getWritePointer (ch);

                    for (int i = 0; i < block; ++i)
                        d[i] = dist (rng);
                }

                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                if (b >= warmupBlocks)
                    captured.insert (captured.end(), buf.getReadPointer (0),
                                     buf.getReadPointer (0) + block);
            }

            expect (static_cast<int> (captured.size()) == analysisLen);

            // Window and transform.
            std::vector<float> fftData (static_cast<std::size_t> (2 * analysisLen), 0.0f);

            for (int i = 0; i < analysisLen; ++i)
            {
                const auto w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi
                                                       * static_cast<float> (i)
                                                       / static_cast<float> (analysisLen));
                fftData[static_cast<std::size_t> (i)] =
                    captured[static_cast<std::size_t> (i)] * w;
            }

            juce::dsp::FFT fft { fftOrder };
            fft.performFrequencyOnlyForwardTransform (fftData.data());

            const auto binHz = static_cast<float> (sr) / static_cast<float> (analysisLen);

            // Peak magnitude within +/-2 bins of a target, which at this resolution is
            // +/-5.9 Hz. The window has to stay tighter than the spacing between the
            // frequencies being compared, or two probes resolve to the same bin and the
            // comparison becomes meaningless.
            auto peakNear = [&] (float hz)
            {
                const auto centre = static_cast<int> (std::lround (hz / binHz));
                float best = 0.0f;

                for (int k = centre - 2; k <= centre + 2; ++k)
                    if (k > 0 && k < analysisLen / 2)
                        best = juce::jmax (best, fftData[static_cast<std::size_t> (k)]);

                return best;
            };

            // Root at MIDI 36 (C2). Minor triad -> MIDI 36, 39, 43.
            const float chordHz[] = { 65.406f, 77.782f, 97.999f };

            // In the bass the three chord tones sit only 12-20 Hz apart, so there is no
            // usable "gap" between the fundamentals at this resolution. Probe instead at
            // frequencies that are inharmonic to all three tones -- each is at least 3
            // bins clear of every chord harmonic below 300 Hz:
            //   112 Hz: nearest harmonics 98.0 and 130.8
            //   175 Hz: nearest harmonics 155.6 and 196.0
            const float gapHz[] = { 112.0f, 175.0f };

            float chordPeak = 0.0f;

            for (auto hz : chordHz)
                chordPeak = juce::jmax (chordPeak, peakNear (hz));

            float gapPeak = 0.0f;

            for (auto hz : gapHz)
                gapPeak = juce::jmax (gapPeak, peakNear (hz));

            std::cout << "  analysis bin width: " << binHz << " Hz\n"
                      << "  chord tones: " << peakNear (chordHz[0]) << " / "
                      << peakNear (chordHz[1]) << " / " << peakNear (chordHz[2]) << "\n"
                      << "  inharmonic:  " << peakNear (gapHz[0]) << " / "
                      << peakNear (gapHz[1]) << std::endl;

            expect (chordPeak > 0.0f, "no energy at the chord pitches at all");

            const auto ratioDb = 20.0f * std::log10 (juce::jmax (chordPeak, 1.0e-12f)
                                                   / juce::jmax (gapPeak, 1.0e-12f));

            std::cout << "  chord-to-inharmonic ratio: " << ratioDb << " dB" << std::endl;

            expect (ratioDb > 12.0f,
                    "chord pitches only " + juce::String (ratioDb, 1)
                    + " dB above inharmonic probes (want > 12 dB) -- the chord is not "
                      "being imprinted");

            // Each individual chord tone must be present, not just the loudest one.
            for (auto hz : chordHz)
            {
                const auto tone = peakNear (hz);
                const auto toneDb = 20.0f * std::log10 (juce::jmax (tone, 1.0e-12f)
                                                      / juce::jmax (gapPeak, 1.0e-12f));

                expect (toneDb > 6.0f,
                        "chord tone at " + juce::String (hz, 1) + " Hz is only "
                        + juce::String (toneDb, 1) + " dB above the inharmonic floor");
            }
        }

        beginTest ("state round-trips and is byte-identical on re-save");
        {
            vbd::VbdProcessor p;

            // Move a representative spread of parameter types off their defaults.
            auto set = [&p] (const char* id, float normalised)
            {
                if (auto* param = p.state().getParameter (id))
                    param->setValueNotifyingHost (normalised);
            };

            set (vbd::pid::engMorph,     0.33f);
            set (vbd::pid::fracDepth,    1.0f);
            set (vbd::pid::dlyOn,        1.0f);
            set (vbd::pid::engMode,      1.0f);
            set (vbd::pid::colLoCut,     0.42f);
            set (vbd::pid::outLimiter,   0.0f);

            p.setUiProperty ("scene", 2);
            p.setUiProperty ("characterVisible", false);

            juce::MemoryBlock first;
            p.getStateInformation (first);

            vbd::VbdProcessor q;
            q.setStateInformation (first.getData(), static_cast<int> (first.getSize()));

            juce::MemoryBlock second;
            q.getStateInformation (second);

            expect (first == second, "state is not stable across a save/load/save cycle");

            // Spot-check that values genuinely transferred rather than both being defaults.
            auto* pm = p.state().getParameter (vbd::pid::engMorph);
            auto* qm = q.state().getParameter (vbd::pid::engMorph);

            expect (pm != nullptr && qm != nullptr);

            if (pm != nullptr && qm != nullptr)
                expectWithinAbsoluteError (qm->getValue(), pm->getValue(), 1.0e-6f,
                                           "morph did not survive the round trip");

            expect (static_cast<int> (q.uiState().getProperty ("scene")) == 2,
                    "UI scene did not survive the round trip");
            expect (! static_cast<bool> (q.uiState().getProperty ("characterVisible")),
                    "character visibility did not survive the round trip");
        }

        beginTest ("state carries a version number");
        {
            vbd::VbdProcessor p;
            juce::MemoryBlock mb;
            p.getStateInformation (mb);

            auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(),
                                                               static_cast<int> (mb.getSize()));
            expect (xml != nullptr, "state did not deserialise");

            if (xml != nullptr)
            {
                expect (xml->hasTagName (vbd::identity::stateTag), "unexpected root tag");
                expect (xml->hasAttribute ("stateVersion"), "state has no version number");
                expect (xml->getIntAttribute ("stateVersion") == vbd::identity::stateVersion,
                        "state version mismatch");
            }
        }

        beginTest ("garbage and truncated state are survived");
        {
            vbd::VbdProcessor p;

            const char junk[] = "not a valid plugin state at all";
            p.setStateInformation (junk, static_cast<int> (sizeof (junk)));

            p.setStateInformation (nullptr, 0);

            juce::MemoryBlock good;
            p.getStateInformation (good);

            // Half a valid state: must be rejected rather than half-applied.
            p.setStateInformation (good.getData(), static_cast<int> (good.getSize() / 2));

            juce::AudioBuffer<float> buf { 2, 128 };
            fillSignal (buf, Signal::noise, 48000.0);
            juce::MidiBuffer midi;
            p.prepareToPlay (48000.0, 128);
            p.processBlock (buf, midi);

            expect (allFinite (buf), "processor unusable after malformed state");
        }

        beginTest ("randomised parameter states stay stable");
        {
            vbd::VbdProcessor p;
            p.prepareToPlay (48000.0, 256);

            std::mt19937 rng { 12345 };
            std::uniform_real_distribution<float> dist { 0.0f, 1.0f };

            for (int iteration = 0; iteration < 200; ++iteration)
            {
                for (auto* param : p.getParameters())
                    param->setValueNotifyingHost (dist (rng));

                juce::AudioBuffer<float> buf { 2, 256 };
                fillSignal (buf, Signal::noise, 48000.0, iteration);

                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                if (! allFinite (buf))
                {
                    expect (false, "non-finite output on random state " + juce::String (iteration));
                    return;
                }
            }

            expect (true);
        }

        beginTest ("extremes: every parameter at min and at max");
        {
            for (float extreme : { 0.0f, 1.0f })
            {
                vbd::VbdProcessor p;
                p.prepareToPlay (48000.0, 256);

                for (auto* param : p.getParameters())
                    param->setValueNotifyingHost (extreme);

                juce::AudioBuffer<float> buf { 2, 256 };
                fillSignal (buf, Signal::fullScaleSine, 48000.0);

                juce::MidiBuffer midi;
                p.processBlock (buf, midi);

                expect (allFinite (buf),
                        "non-finite output with all parameters at " + juce::String (extreme));
            }
        }
    }
};

static ProcessorTests processorTests;

} // namespace
