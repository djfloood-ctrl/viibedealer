#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/StftEngine.h"
#include "dsp/Utils.h"

#include <random>
#include <vector>

namespace
{

/** Copies the modulator frame straight to the output. With this sink the whole STFT must
    behave as a pure delay of exactly one FFT frame, at unity gain -- which is the single
    strongest test of the windowing and overlap-add normalisation. */
class IdentitySink final : public vbd::dsp::FrameSink
{
public:
    void processFrame (const float* modulator, const float* carrier, float* output,
                       int numBins, int channel) override
    {
        juce::ignoreUnused (carrier, channel);
        std::copy (modulator, modulator + 2 * numBins, output);
    }
};

/** Takes the carrier instead, so carrier routing can be verified independently. */
class CarrierSink final : public vbd::dsp::FrameSink
{
public:
    void processFrame (const float* modulator, const float* carrier, float* output,
                       int numBins, int channel) override
    {
        juce::ignoreUnused (modulator, channel);
        std::copy (carrier, carrier + 2 * numBins, output);
    }
};

class StftTests final : public juce::UnitTest
{
public:
    StftTests() : juce::UnitTest ("STFT engine", "dsp") {}

    void runTest() override
    {
        const int sizes[] = { 512, 1024, 2048, 4096 };

        beginTest ("latency equals the FFT size, verified by impulse");
        {
            for (auto size : sizes)
            {
                vbd::dsp::StftEngine stft;
                stft.prepare (1, 512);
                stft.setFftSize (size);

                expect (stft.getLatencySamples() == size,
                        "reported latency " + juce::String (stft.getLatencySamples())
                        + " != fft size " + juce::String (size));

                IdentitySink sink;

                // Long enough to contain the impulse plus its full reported latency.
                const auto total = size * 4;
                std::vector<float> in (static_cast<std::size_t> (total), 0.0f);
                std::vector<float> car (static_cast<std::size_t> (total), 0.0f);
                std::vector<float> out (static_cast<std::size_t> (total), 0.0f);

                in[0] = 1.0f;

                const float* inPtr[]  = { in.data() };
                const float* carPtr[] = { car.data() };
                float* outPtr[]       = { out.data() };

                stft.process (inPtr, carPtr, outPtr, 1, total, sink);

                // Find the peak of the reconstructed impulse.
                int peakIdx = -1;
                float peak = 0.0f;

                for (int i = 0; i < total; ++i)
                {
                    if (std::abs (out[static_cast<std::size_t> (i)]) > peak)
                    {
                        peak = std::abs (out[static_cast<std::size_t> (i)]);
                        peakIdx = i;
                    }
                }

                expect (peakIdx == size,
                        "fft " + juce::String (size) + ": impulse peak landed at "
                        + juce::String (peakIdx) + ", expected " + juce::String (size));

                expectWithinAbsoluteError (peak, 1.0f, 0.01f,
                                           "fft " + juce::String (size)
                                           + ": impulse amplitude not unity");
            }
        }

        beginTest ("identity transform reconstructs at unity gain (< 0.01 dB error)");
        {
            for (auto size : sizes)
            {
                vbd::dsp::StftEngine stft;
                stft.prepare (1, 256);
                stft.setFftSize (size);

                IdentitySink sink;

                const auto total = size * 8;
                std::vector<float> in (static_cast<std::size_t> (total), 0.0f);
                std::vector<float> car (static_cast<std::size_t> (total), 0.0f);
                std::vector<float> out (static_cast<std::size_t> (total), 0.0f);

                // Steady sine, deliberately not bin-centred so the test is not flattered
                // by perfect alignment with the analysis grid.
                constexpr auto freq = 1000.0;
                constexpr auto sr   = 48000.0;

                for (int i = 0; i < total; ++i)
                    in[static_cast<std::size_t> (i)] = static_cast<float> (
                        0.5 * std::sin (juce::MathConstants<double>::twoPi * freq
                                        * static_cast<double> (i) / sr));

                // Process in irregular chunks: a correct implementation must not care
                // where the block boundaries fall relative to the hop.
                int pos = 0;
                const int chunks[] = { 37, 1, 256, 13, 199, 64 };
                int chunkIdx = 0;

                while (pos < total)
                {
                    const auto n = juce::jmin (chunks[chunkIdx % 6], total - pos);

                    const float* ip[]  = { in.data() + pos };
                    const float* cp[]  = { car.data() + pos };
                    float* op[]        = { out.data() + pos };

                    stft.process (ip, cp, op, 1, n, sink);

                    pos += n;
                    ++chunkIdx;
                }

                // Compare the steady-state region, past the latency and the ramp-up.
                const auto start = size * 3;
                double errSum = 0.0, refSum = 0.0;

                for (int i = start; i < total; ++i)
                {
                    const auto ref = static_cast<double> (in[static_cast<std::size_t> (i - size)]);
                    const auto got = static_cast<double> (out[static_cast<std::size_t> (i)]);

                    errSum += (got - ref) * (got - ref);
                    refSum += ref * ref;
                }

                expect (refSum > 0.0);

                const auto rmsRatio = std::sqrt (errSum / refSum);
                const auto errorDb = 20.0 * std::log10 (juce::jmax (rmsRatio, 1.0e-12));

                expect (errorDb < -60.0,
                        "fft " + juce::String (size) + ": reconstruction error "
                        + juce::String (errorDb, 2) + " dB (want < -60 dB)");

                // And the gain itself, which is what the 2/3 WOLA factor controls.
                double gotRms = 0.0, refRms = 0.0;

                for (int i = start; i < total; ++i)
                {
                    gotRms += static_cast<double> (out[static_cast<std::size_t> (i)])
                            * static_cast<double> (out[static_cast<std::size_t> (i)]);
                    refRms += static_cast<double> (in[static_cast<std::size_t> (i - size)])
                            * static_cast<double> (in[static_cast<std::size_t> (i - size)]);
                }

                const auto gainDb = 10.0 * std::log10 (juce::jmax (gotRms, 1.0e-20)
                                                     / juce::jmax (refRms, 1.0e-20));

                expect (std::abs (gainDb) < 0.01,
                        "fft " + juce::String (size) + ": WOLA gain error "
                        + juce::String (gainDb, 4) + " dB (want < 0.01 dB)");
            }
        }

        beginTest ("carrier stream is independent of the modulator stream");
        {
            vbd::dsp::StftEngine stft;
            stft.prepare (1, 512);
            stft.setFftSize (1024);

            CarrierSink sink;

            const auto total = 1024 * 6;
            std::vector<float> in (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> car (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> out (static_cast<std::size_t> (total), 0.0f);

            // Modulator silent, carrier a sine: taking the carrier frame must produce the
            // carrier, proving the two rings are not crossed.
            for (int i = 0; i < total; ++i)
                car[static_cast<std::size_t> (i)] = static_cast<float> (
                    0.25 * std::sin (juce::MathConstants<double>::twoPi * 500.0
                                     * static_cast<double> (i) / 48000.0));

            const float* inPtr[]  = { in.data() };
            const float* carPtr[] = { car.data() };
            float* outPtr[]       = { out.data() };

            stft.process (inPtr, carPtr, outPtr, 1, total, sink);

            double energy = 0.0;

            for (int i = 1024 * 3; i < total; ++i)
                energy += std::abs (static_cast<double> (out[static_cast<std::size_t> (i)]));

            expect (energy > 1.0, "carrier did not reach the output");
        }

        beginTest ("multi-channel streams stay separate");
        {
            vbd::dsp::StftEngine stft;
            stft.prepare (2, 512);
            stft.setFftSize (512);

            IdentitySink sink;

            const auto total = 512 * 6;
            std::vector<float> l (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> r (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> cl (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> cr (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> ol (static_cast<std::size_t> (total), 0.0f);
            std::vector<float> orr (static_cast<std::size_t> (total), 0.0f);

            // Left gets an impulse, right stays silent.
            l[0] = 1.0f;

            const float* inPtr[]  = { l.data(), r.data() };
            const float* carPtr[] = { cl.data(), cr.data() };
            float* outPtr[]       = { ol.data(), orr.data() };

            stft.process (inPtr, carPtr, outPtr, 2, total, sink);

            float leftPeak = 0.0f, rightPeak = 0.0f;

            for (int i = 0; i < total; ++i)
            {
                leftPeak  = juce::jmax (leftPeak,  std::abs (ol[static_cast<std::size_t> (i)]));
                rightPeak = juce::jmax (rightPeak, std::abs (orr[static_cast<std::size_t> (i)]));
            }

            expect (leftPeak > 0.9f, "left channel lost its impulse");
            expect (rightPeak < 1.0e-6f,
                    "right channel picked up signal from the left (peak "
                    + juce::String (rightPeak) + ")");
        }

        beginTest ("switching FFT size does not allocate or misreport latency");
        {
            vbd::dsp::StftEngine stft;
            stft.prepare (2, 512);

            IdentitySink sink;

            std::vector<float> in (512, 0.1f), car (512, 0.0f), out (512, 0.0f);

            for (auto size : { 4096, 512, 2048, 1024, 4096 })
            {
                const auto changed = stft.setFftSize (size);
                expect (changed, "setFftSize reported no change for a new size");
                expect (stft.getFftSize() == size);
                expect (stft.getLatencySamples() == size);
                expect (stft.getHopSize() == size / 4);
                expect (stft.getNumBins() == size / 2 + 1);

                // Re-setting the same size must be a no-op.
                expect (! stft.setFftSize (size), "setFftSize reported a spurious change");

                const float* ip[] = { in.data(), in.data() };
                const float* cp[] = { car.data(), car.data() };
                float* op[]       = { out.data(), out.data() };

                stft.process (ip, cp, op, 2, 512, sink);

                for (auto v : out)
                    expect (std::isfinite (v), "non-finite output after an FFT size switch");
            }
        }

        beginTest ("window overlap gain for Hann at 75% overlap is 1.5");
        {
            for (auto size : sizes)
            {
                std::vector<float> w (static_cast<std::size_t> (size));
                vbd::dsp::fillHann (w.data(), size);

                const auto gain = vbd::dsp::windowOverlapGain (w.data(), size, size / 4);

                expectWithinAbsoluteError (gain, 1.5, 1.0e-9,
                                           "COLA sum wrong for size " + juce::String (size));
            }
        }

        beginTest ("pathological inputs produce finite output");
        {
            std::mt19937 rng { 99 };
            std::uniform_real_distribution<float> dist { -1.0f, 1.0f };

            for (auto size : sizes)
            {
                vbd::dsp::StftEngine stft;
                stft.prepare (1, 256);
                stft.setFftSize (size);

                IdentitySink sink;

                const auto total = size * 4;
                std::vector<float> in (static_cast<std::size_t> (total));
                std::vector<float> car (static_cast<std::size_t> (total));
                std::vector<float> out (static_cast<std::size_t> (total), 0.0f);

                for (int i = 0; i < total; ++i)
                {
                    // Full-scale noise plus DC, the worst realistic case for headroom.
                    in[static_cast<std::size_t> (i)] = dist (rng) + 1.0f;
                    car[static_cast<std::size_t> (i)] = dist (rng);
                }

                const float* ip[] = { in.data() };
                const float* cp[] = { car.data() };
                float* op[]       = { out.data() };

                stft.process (ip, cp, op, 1, total, sink);

                for (auto v : out)
                    expect (std::isfinite (v), "non-finite output at size " + juce::String (size));
            }
        }
    }
};

static StftTests stftTests;

} // namespace
