#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/ChordCarrier.h"
#include "dsp/SpectralEngine.h"
#include "dsp/StftEngine.h"
#include "dsp/Utils.h"

#include <random>
#include <vector>

namespace
{

/** Runs a signal through StftEngine + SpectralEngine and reports peak/RMS of the result. */
struct EngineHarness
{
    vbd::dsp::StftEngine stft;
    vbd::dsp::SpectralEngine engine;

    void setup (int fftSize, double sr = 48000.0)
    {
        stft.prepare (2, 512);
        stft.setFftSize (fftSize);
        engine.prepare (2, vbd::dsp::StftEngine::maxFftSize, sr);
        engine.setFftSize (stft.getFftSize(), stft.currentFft(), stft.magnitudeScale());
    }

    /** Runs the engine and returns the left-channel output. */
    std::vector<float> runCapture (const std::vector<float>& mod, const std::vector<float>& car,
                                   int numSamples)
    {
        std::vector<float> outL (static_cast<std::size_t> (numSamples), 0.0f);
        std::vector<float> outR (static_cast<std::size_t> (numSamples), 0.0f);

        const float* mp[] = { mod.data(), mod.data() };
        const float* cp[] = { car.data(), car.data() };
        float* op[]       = { outL.data(), outR.data() };

        stft.process (mp, cp, op, 2, numSamples, engine);

        return outL;
    }

    /** @returns peak absolute output, and sets allFinite. */
    float run (const std::vector<float>& mod, const std::vector<float>& car,
               bool& allFinite, int numSamples)
    {
        const auto out = runCapture (mod, car, numSamples);

        allFinite = true;
        float peak = 0.0f;

        for (auto v : out)
        {
            if (! std::isfinite (v))
                allFinite = false;

            peak = juce::jmax (peak, std::abs (v));
        }

        return peak;
    }
};

std::vector<float> makeSine (int n, double freq, double sr, float amp)
{
    std::vector<float> v (static_cast<std::size_t> (n));

    for (int i = 0; i < n; ++i)
        v[static_cast<std::size_t> (i)] = amp * static_cast<float> (
            std::sin (juce::MathConstants<double>::twoPi * freq * static_cast<double> (i) / sr));

    return v;
}

std::vector<float> makeNoise (int n, float amp, std::uint32_t seed)
{
    std::mt19937 rng { seed };
    std::uniform_real_distribution<float> dist { -amp, amp };

    std::vector<float> v (static_cast<std::size_t> (n));

    for (auto& s : v)
        s = dist (rng);

    return v;
}

class SpectralEngineTests final : public juce::UnitTest
{
public:
    SpectralEngineTests() : juce::UnitTest ("Spectral engine", "dsp") {}

    void runTest() override
    {
        constexpr int n = 2048 * 6;
        constexpr double sr = 48000.0;

        const auto bassNoise = makeNoise (n, 0.7f, 11);
        const auto carrierSine = makeSine (n, 220.0, sr, 0.7f);
        const auto silence = std::vector<float> (static_cast<std::size_t> (n), 0.0f);

        beginTest ("all four modes stay finite and bounded");
        {
            const vbd::dsp::EngineMode modes[] = {
                vbd::dsp::EngineMode::vocode,
                vbd::dsp::EngineMode::magMorph,
                vbd::dsp::EngineMode::phaseMorph,
                vbd::dsp::EngineMode::cross
            };

            for (auto mode : modes)
            {
                for (auto size : { 512, 1024, 2048, 4096 })
                {
                    EngineHarness h;
                    h.setup (size, sr);

                    vbd::dsp::SpectralEngine::Params ep;
                    ep.mode = mode;
                    ep.morph = 1.0f;
                    ep.gateDb = -80.0f;
                    h.engine.setParams (ep);

                    bool finite = false;
                    const auto peak = h.run (bassNoise, carrierSine, finite, n);

                    expect (finite, "non-finite output: mode " + juce::String (static_cast<int> (mode))
                                    + " size " + juce::String (size));
                    expect (peak < 32.0f,
                            "output ran away: mode " + juce::String (static_cast<int> (mode))
                            + " size " + juce::String (size) + " peak " + juce::String (peak));
                }
            }
        }

        beginTest ("silence in gives silence out");
        {
            for (auto size : { 512, 2048 })
            {
                EngineHarness h;
                h.setup (size, sr);

                vbd::dsp::SpectralEngine::Params ep;
                ep.mode = vbd::dsp::EngineMode::vocode;
                h.engine.setParams (ep);

                bool finite = false;
                const auto peak = h.run (silence, silence, finite, n);

                expect (finite);
                expect (peak < 1.0e-6f, "silence produced output, peak " + juce::String (peak));
            }
        }

        beginTest ("gate closes on a silent modulator even with a loud carrier");
        {
            EngineHarness h;
            h.setup (2048, sr);

            vbd::dsp::SpectralEngine::Params ep;
            ep.mode = vbd::dsp::EngineMode::vocode;
            ep.gateDb = -40.0f;
            h.engine.setParams (ep);

            bool finite = false;
            const auto peak = h.run (silence, carrierSine, finite, n);

            expect (finite);
            expect (peak < 1.0e-5f,
                    "carrier leaked through a closed gate, peak " + juce::String (peak));
        }

        beginTest ("extreme parameter values stay finite");
        {
            // Every continuous engine parameter pushed to both rails at once.
            for (float rail : { 0.0f, 1.0f })
            {
                EngineHarness h;
                h.setup (2048, sr);

                vbd::dsp::SpectralEngine::Params ep;
                ep.mode = vbd::dsp::EngineMode::phaseMorph;
                ep.morph = rail;
                ep.formantShift = rail > 0.5f ? 24.0f : -24.0f;
                ep.tilt = rail > 0.5f ? 6.0f : -6.0f;
                ep.envRes = rail;
                ep.cepstral = rail > 0.5f;
                ep.phaseLock = rail;
                ep.gateDb = rail > 0.5f ? 0.0f : -80.0f;
                ep.freeze = rail > 0.5f;
                ep.flip = rail > 0.5f;
                ep.freqShiftHz = rail > 0.5f ? 500.0f : -500.0f;
                h.engine.setParams (ep);

                bool finite = false;
                const auto peak = h.run (bassNoise, carrierSine, finite, n);

                expect (finite, "non-finite output at rail " + juce::String (rail));
                expect (peak < 64.0f, "runaway output at rail " + juce::String (rail)
                                      + ", peak " + juce::String (peak));
            }
        }

        beginTest ("cepstral envelope path stays finite");
        {
            EngineHarness h;
            h.setup (2048, sr);

            vbd::dsp::SpectralEngine::Params ep;
            ep.mode = vbd::dsp::EngineMode::vocode;
            ep.cepstral = true;

            for (float res : { 0.0f, 0.5f, 1.0f })
            {
                ep.envRes = res;
                h.engine.setParams (ep);

                bool finite = false;
                const auto peak = h.run (bassNoise, carrierSine, finite, n);

                expect (finite, "cepstral produced non-finite output at res " + juce::String (res));
                expect (peak < 32.0f, "cepstral runaway at res " + juce::String (res));
            }
        }

        beginTest ("freeze holds the imprint while the carrier keeps moving");
        {
            EngineHarness h;
            h.setup (2048, sr);

            vbd::dsp::SpectralEngine::Params ep;
            ep.mode = vbd::dsp::EngineMode::vocode;
            ep.freeze = true;
            ep.gateDb = -80.0f;
            h.engine.setParams (ep);

            bool finite = false;
            const auto peak = h.run (bassNoise, carrierSine, finite, n);

            expect (finite);
            // Frozen or not, there must still be output: a stuck frame of zeros would
            // mean freeze had muted the plugin.
            expect (peak > 1.0e-4f, "freeze silenced the output");
        }

        beginTest ("flip swaps the two streams");
        {
            // Both streams must be non-silent for this to mean anything: with one side
            // silent, every mode collapses to silence in both flip states (you cannot take
            // phase, or an envelope, from nothing), so the test would pass vacuously.
            EngineHarness unflipped, flipped;
            unflipped.setup (2048, sr);
            flipped.setup (2048, sr);

            vbd::dsp::SpectralEngine::Params ep;
            ep.mode = vbd::dsp::EngineMode::cross;
            ep.gateDb = -80.0f;

            ep.flip = false;
            unflipped.engine.setParams (ep);

            ep.flip = true;
            flipped.engine.setParams (ep);

            const auto a = unflipped.runCapture (bassNoise, carrierSine, n);
            const auto b = flipped.runCapture (bassNoise, carrierSine, n);

            // Measure how different the two results are, relative to their own level.
            double diff = 0.0, energy = 0.0;

            for (int i = 2048 * 2; i < n; ++i)
            {
                const auto u = static_cast<double> (a[static_cast<std::size_t> (i)]);
                const auto v = static_cast<double> (b[static_cast<std::size_t> (i)]);

                expect (std::isfinite (u) && std::isfinite (v));

                diff   += (u - v) * (u - v);
                energy += u * u + v * v;
            }

            expect (energy > 1.0e-9, "both flip states were silent; test proves nothing");
            expect (diff / juce::jmax (energy, 1.0e-20) > 0.01,
                    "flip produced an almost identical result, so the streams were not swapped");
        }
    }
};

class ChordCarrierTests final : public juce::UnitTest
{
public:
    ChordCarrierTests() : juce::UnitTest ("Chord carrier", "dsp") {}

    void runTest() override
    {
        constexpr double sr = 48000.0;
        constexpr int block = 512;

        beginTest ("every oscillator type produces bounded, finite audio");
        {
            const vbd::dsp::CarrierOsc oscs[] = {
                vbd::dsp::CarrierOsc::supersaw, vbd::dsp::CarrierOsc::saw,
                vbd::dsp::CarrierOsc::square, vbd::dsp::CarrierOsc::noise
            };

            for (auto osc : oscs)
            {
                vbd::dsp::ChordCarrier c;
                c.prepare (sr, block);

                vbd::dsp::ChordCarrier::Params cp;
                cp.osc = osc;
                cp.level = 1.0f;
                c.setParams (cp);

                juce::AudioBuffer<float> buf { 2, block };

                float peak = 0.0f;

                for (int b = 0; b < 20; ++b)
                {
                    c.render (buf, block);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < block; ++i)
                        {
                            const auto v = buf.getSample (ch, i);
                            expect (std::isfinite (v), "non-finite carrier sample");
                            peak = juce::jmax (peak, std::abs (v));
                        }
                }

                expect (peak > 0.01f, "oscillator produced no output: "
                                      + juce::String (static_cast<int> (osc)));
                expect (peak < 4.0f, "oscillator output too hot: " + juce::String (peak));
            }
        }

        beginTest ("every chord type renders");
        {
            const vbd::dsp::ChordType types[] = {
                vbd::dsp::ChordType::maj, vbd::dsp::ChordType::min,
                vbd::dsp::ChordType::maj7, vbd::dsp::ChordType::min9,
                vbd::dsp::ChordType::sus2, vbd::dsp::ChordType::add9,
                vbd::dsp::ChordType::power, vbd::dsp::ChordType::custom
            };

            for (auto type : types)
            {
                vbd::dsp::ChordCarrier c;
                c.prepare (sr, block);

                vbd::dsp::ChordCarrier::Params cp;
                cp.chord = type;
                cp.osc = vbd::dsp::CarrierOsc::saw;
                cp.midiOverride = false;
                c.setParams (cp);

                juce::AudioBuffer<float> buf { 2, block };
                c.render (buf, block);
                c.render (buf, block);

                expect (buf.getMagnitude (0, block) > 0.001f,
                        "chord type " + juce::String (static_cast<int> (type)) + " was silent");
            }
        }

        beginTest ("MIDI notes override the chord, and releasing them restores it");
        {
            vbd::dsp::ChordCarrier c;
            c.prepare (sr, block);

            vbd::dsp::ChordCarrier::Params cp;
            cp.midiOverride = true;
            c.setParams (cp);

            expect (! c.isMidiDriven(), "MIDI-driven with no notes held");

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 40, 1.0f), 0);
            midi.addEvent (juce::MidiMessage::noteOn (1, 47, 1.0f), 0);
            c.handleMidi (midi);

            expect (c.isMidiDriven(), "held notes did not take over the chord");

            juce::AudioBuffer<float> buf { 2, block };
            c.render (buf, block);
            c.render (buf, block);
            expect (buf.getMagnitude (0, block) > 0.001f, "MIDI-driven chord was silent");

            juce::MidiBuffer off;
            off.addEvent (juce::MidiMessage::noteOff (1, 40), 0);
            off.addEvent (juce::MidiMessage::noteOff (1, 47), 0);
            c.handleMidi (off);

            expect (! c.isMidiDriven(), "releasing the keys did not restore the chord");
        }

        beginTest ("all-notes-off clears held notes");
        {
            vbd::dsp::ChordCarrier c;
            c.prepare (sr, block);

            vbd::dsp::ChordCarrier::Params cp;
            cp.midiOverride = true;
            c.setParams (cp);

            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
            c.handleMidi (midi);
            expect (c.isMidiDriven());

            juce::MidiBuffer panic;
            panic.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            c.handleMidi (panic);

            expect (! c.isMidiDriven(), "all-notes-off left notes held");
        }

        beginTest ("note storm beyond the voice limit does not overflow");
        {
            vbd::dsp::ChordCarrier c;
            c.prepare (sr, block);

            vbd::dsp::ChordCarrier::Params cp;
            cp.midiOverride = true;
            cp.osc = vbd::dsp::CarrierOsc::supersaw;
            c.setParams (cp);

            juce::MidiBuffer midi;

            // Far more notes than maxNotes, to prove the clamp holds.
            for (int note = 24; note < 100; ++note)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);

            c.handleMidi (midi);

            juce::AudioBuffer<float> buf { 2, block };

            for (int b = 0; b < 8; ++b)
            {
                c.render (buf, block);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        expect (std::isfinite (buf.getSample (ch, i)),
                                "non-finite output under a note storm");
            }
        }

        beginTest ("extreme spread and detune stay finite");
        {
            for (float rail : { 0.0f, 1.0f })
            {
                vbd::dsp::ChordCarrier c;
                c.prepare (sr, block);

                vbd::dsp::ChordCarrier::Params cp;
                cp.spread = rail;
                cp.detuneCents = rail * 50.0f;
                cp.octave = rail > 0.5f ? 2 : -2;
                cp.rootNote = rail > 0.5f ? 11 : 0;
                cp.osc = vbd::dsp::CarrierOsc::supersaw;
                cp.midiOverride = false;
                c.setParams (cp);

                juce::AudioBuffer<float> buf { 2, block };

                for (int b = 0; b < 8; ++b)
                {
                    c.render (buf, block);

                    for (int ch = 0; ch < 2; ++ch)
                        for (int i = 0; i < block; ++i)
                            expect (std::isfinite (buf.getSample (ch, i)),
                                    "non-finite carrier at rail " + juce::String (rail));
                }
            }
        }

        beginTest ("high sample rates do not alias the increment past Nyquist");
        {
            for (double rate : { 44100.0, 192000.0 })
            {
                vbd::dsp::ChordCarrier c;
                c.prepare (rate, block);

                vbd::dsp::ChordCarrier::Params cp;
                cp.octave = 2;
                cp.rootNote = 11;
                cp.osc = vbd::dsp::CarrierOsc::square;
                cp.midiOverride = false;
                c.setParams (cp);

                juce::AudioBuffer<float> buf { 2, block };
                c.render (buf, block);
                c.render (buf, block);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        expect (std::isfinite (buf.getSample (ch, i)),
                                "non-finite at rate " + juce::String (rate));
            }
        }
    }
};

static SpectralEngineTests spectralEngineTests;
static ChordCarrierTests chordCarrierTests;

} // namespace
