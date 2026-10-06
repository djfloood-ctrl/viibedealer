#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/ColorStage.h"
#include "dsp/FractalSlicer.h"
#include "dsp/SpectralDelay.h"
#include "dsp/StereoDelay.h"
#include "dsp/StftEngine.h"

#include <random>
#include <vector>

namespace
{

using namespace vbd::dsp;

void fillNoise (juce::AudioBuffer<float>& b, int numSamples, float amp, std::uint32_t seed)
{
    std::mt19937 rng { seed };
    std::uniform_real_distribution<float> dist { -amp, amp };

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        auto* d = b.getWritePointer (ch);

        for (int i = 0; i < numSamples; ++i)
            d[i] = dist (rng);
    }
}

bool allFinite (const juce::AudioBuffer<float>& b, int numSamples)
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < numSamples; ++i)
            if (! std::isfinite (b.getSample (ch, i)))
                return false;

    return true;
}

float peakOf (const juce::AudioBuffer<float>& b, int numSamples)
{
    float peak = 0.0f;

    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < numSamples; ++i)
            peak = juce::jmax (peak, std::abs (b.getSample (ch, i)));

    return peak;
}

class StereoDelayTests final : public juce::UnitTest
{
public:
    StereoDelayTests() : juce::UnitTest ("Stereo delay", "dsp") {}

    void runTest() override
    {
        constexpr int block = 512;

        beginTest ("off is bit-exact transparent and costs nothing");
        {
            StereoDelay d;
            d.prepare (2, 48000.0, block);

            StereoDelay::Params dp;
            dp.on = false;
            d.setParams (dp);

            for (int b = 0; b < 20; ++b)
            {
                juce::AudioBuffer<float> buf { 2, block };
                fillNoise (buf, block, 0.5f, static_cast<std::uint32_t> (b));
                const juce::AudioBuffer<float> reference { buf };

                d.process (buf, block);

                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        if (! juce::exactlyEqual (buf.getSample (ch, i), reference.getSample (ch, i)))
                        {
                            expect (false, "delay altered the signal while off");
                            return;
                        }
            }

            expect (! d.isRunning(), "delay reports running while off");
        }

        beginTest ("feedback stays bounded for 60 s at maximum settings");
        {
            // The non-negotiable test: everything that could compound, compounding at
            // once, for a minute of audio.
            for (auto mode : { DelayMode::stereo, DelayMode::pingPong, DelayMode::dual })
            {
                StereoDelay d;
                d.prepare (2, 48000.0, block);

                StereoDelay::Params dp;
                dp.on = true;
                dp.mode = mode;
                dp.sync = false;
                dp.msL = 50.0f;          // short, so many round trips fit in the window
                dp.msR = 70.0f;
                dp.feedback = 0.98f;     // the parameter maximum
                dp.saturation = 1.0f;
                dp.diffusion = 1.0f;
                dp.modDepth = 1.0f;
                dp.modRateHz = 10.0f;
                dp.hpHz = 20.0f;         // filters wide open, so nothing is removed
                dp.lpHz = 20000.0f;
                dp.width = 2.0f;
                dp.mix = 1.0f;
                d.setParams (dp);

                const auto totalBlocks = static_cast<int> (60.0 * 48000.0 / block);
                float worstPeak = 0.0f;

                for (int b = 0; b < totalBlocks; ++b)
                {
                    juce::AudioBuffer<float> buf { 2, block };

                    // Keep feeding full-scale noise the whole time, the worst case.
                    fillNoise (buf, block, 1.0f, static_cast<std::uint32_t> (b + 1));

                    d.process (buf, block);

                    if (! allFinite (buf, block))
                    {
                        expect (false, "non-finite output in mode "
                                       + juce::String (static_cast<int> (mode))
                                       + " at block " + juce::String (b));
                        return;
                    }

                    worstPeak = juce::jmax (worstPeak, peakOf (buf, block));
                }

                expect (worstPeak < 4.0f,
                        "mode " + juce::String (static_cast<int> (mode))
                        + " peaked at " + juce::String (worstPeak) + " (must stay < 4.0)");
            }
        }

        beginTest ("freeze holds indefinitely without diverging");
        {
            StereoDelay d;
            d.prepare (2, 48000.0, block);

            StereoDelay::Params dp;
            dp.on = true;
            dp.sync = false;
            dp.msL = 100.0f;
            dp.msR = 100.0f;
            dp.feedback = 0.98f;
            dp.freeze = true;
            dp.saturation = 0.0f;   // no extra drive: only the implicit loop limit
            dp.diffusion = 0.5f;
            dp.mix = 1.0f;
            d.setParams (dp);

            // Prime the line, then freeze with silence going in.
            juce::AudioBuffer<float> buf { 2, block };
            fillNoise (buf, block, 1.0f, 5);
            d.process (buf, block);

            float worstPeak = 0.0f;

            // 30 s of pure recirculation.
            for (int b = 0; b < static_cast<int> (30.0 * 48000.0 / block); ++b)
            {
                buf.clear();
                d.process (buf, block);

                expect (allFinite (buf, block), "freeze produced non-finite output");
                worstPeak = juce::jmax (worstPeak, peakOf (buf, block));
            }

            expect (worstPeak < 4.0f,
                    "freeze diverged, peak " + juce::String (worstPeak));
        }

        beginTest ("toggling on and off does not click");
        {
            StereoDelay d;
            d.prepare (2, 48000.0, block);

            StereoDelay::Params dp;
            dp.on = true;
            dp.sync = false;
            dp.msL = 120.0f;
            dp.msR = 160.0f;
            dp.feedback = 0.8f;
            dp.mix = 1.0f;
            d.setParams (dp);

            // A sine, not noise: this test measures sample-to-sample steps, and a noise
            // tail has large steps by nature, which would drown out an actual click.
            int phase = 0;
            auto fillSine = [&phase] (juce::AudioBuffer<float>& buf, int n)
            {
                for (int i = 0; i < n; ++i)
                {
                    const auto v = 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                    * 100.0f * static_cast<float> (phase)
                                                    / 48000.0f);
                    buf.setSample (0, i, v);
                    buf.setSample (1, i, v);
                    ++phase;
                }
            };

            // Run a while so there is a real tail to cut off.
            for (int b = 0; b < 40; ++b)
            {
                juce::AudioBuffer<float> buf { 2, block };
                fillSine (buf, block);
                d.process (buf, block);
            }

            // Now disable and watch for a step discontinuity at the block boundary.
            dp.on = false;
            d.setParams (dp);

            float lastSample = 0.0f;
            float worstJump = 0.0f;
            bool first = true;

            for (int b = 0; b < 40; ++b)
            {
                juce::AudioBuffer<float> buf { 2, block };
                buf.clear();   // silence in, so anything heard is the tail being faded
                d.process (buf, block);

                for (int i = 0; i < block; ++i)
                {
                    const auto s = buf.getSample (0, i);

                    if (! first)
                        worstJump = juce::jmax (worstJump, std::abs (s - lastSample));

                    lastSample = s;
                    first = false;
                }
            }

            // A 100 Hz sine at 48 kHz moves at most ~0.013 per sample at this amplitude,
            // so anything an order of magnitude above that is a discontinuity, not signal.
            expect (worstJump < 0.05f,
                    "disabling produced a step of " + juce::String (worstJump)
                    + " between consecutive samples (smooth signal moves < 0.013)");
        }

        beginTest ("ping-pong actually alternates between channels");
        {
            StereoDelay d;
            d.prepare (2, 48000.0, block);

            StereoDelay::Params dp;
            dp.on = true;
            dp.mode = DelayMode::pingPong;
            dp.sync = false;
            dp.msL = 100.0f;
            dp.msR = 100.0f;
            dp.feedback = 0.7f;
            dp.saturation = 0.0f;
            dp.diffusion = 0.0f;
            dp.modDepth = 0.0f;
            dp.hpHz = 20.0f;
            dp.lpHz = 20000.0f;
            dp.width = 1.0f;
            dp.mix = 1.0f;
            d.setParams (dp);

            // One impulse in, then silence; collect the tail.
            const auto totalBlocks = 60;
            std::vector<float> l, r;

            for (int b = 0; b < totalBlocks; ++b)
            {
                juce::AudioBuffer<float> buf { 2, block };
                buf.clear();

                if (b == 0)
                {
                    buf.setSample (0, 0, 1.0f);
                    buf.setSample (1, 0, 1.0f);
                }

                d.process (buf, block);

                for (int i = 0; i < block; ++i)
                {
                    l.push_back (buf.getSample (0, i));
                    r.push_back (buf.getSample (1, i));
                }
            }

            // 100 ms = 4800 samples. The first echo should favour one channel and the
            // second the other.
            const auto at = [] (const std::vector<float>& v, int centre)
            {
                float best = 0.0f;

                for (int i = juce::jmax (0, centre - 64);
                     i < juce::jmin (static_cast<int> (v.size()), centre + 64); ++i)
                    best = juce::jmax (best, std::abs (v[static_cast<std::size_t> (i)]));

                return best;
            };

            const auto firstL = at (l, 4800), firstR = at (r, 4800);
            const auto secondL = at (l, 9600), secondR = at (r, 9600);

            expect (firstL + firstR > 1.0e-4f, "no first echo at all");
            expect (secondL + secondR > 1.0e-5f, "no second echo at all");

            // Whichever channel leads on echo 1, the other must lead on echo 2.
            const auto firstFavoursL = firstL > firstR;
            const auto secondFavoursL = secondL > secondR;

            expect (firstFavoursL != secondFavoursL,
                    "ping-pong did not alternate: echo1 L/R = " + juce::String (firstL) + "/"
                    + juce::String (firstR) + ", echo2 L/R = " + juce::String (secondL) + "/"
                    + juce::String (secondR));
        }

        beginTest ("every parameter rail, every mode, every sample rate stays finite");
        {
            const double rates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
            const int blocks[] = { 1, 7, 64, 2048 };

            for (auto rate : rates)
            {
                for (auto bs : blocks)
                {
                    for (auto mode : { DelayMode::stereo, DelayMode::pingPong, DelayMode::dual })
                    {
                        for (float rail : { 0.0f, 1.0f })
                        {
                            StereoDelay d;
                            d.prepare (2, rate, bs);

                            StereoDelay::Params dp;
                            dp.on = true;
                            dp.mode = mode;
                            dp.sync = rail > 0.5f;
                            dp.divL = rail > 0.5f ? 0 : 16;
                            dp.divR = rail > 0.5f ? 16 : 0;
                            dp.msL = rail > 0.5f ? StereoDelay::maxDelayMs : 1.0f;
                            dp.msR = rail > 0.5f ? 1.0f : StereoDelay::maxDelayMs;
                            dp.feedback = rail;
                            dp.hpHz = rail > 0.5f ? 2000.0f : 20.0f;
                            dp.lpHz = rail > 0.5f ? 200.0f : 20000.0f;
                            dp.saturation = rail;
                            dp.modRateHz = rail * 10.0f;
                            dp.modDepth = rail;
                            dp.diffusion = rail;
                            dp.ducking = rail;
                            dp.freeze = rail > 0.5f;
                            dp.width = rail * 2.0f;
                            dp.mix = rail;
                            d.setParams (dp);
                            d.setTransport (rail > 0.5f ? 300.0 : 20.0);

                            for (int b = 0; b < 12; ++b)
                            {
                                juce::AudioBuffer<float> buf { 2, bs };
                                fillNoise (buf, bs, 1.0f, static_cast<std::uint32_t> (b + 2));
                                d.process (buf, bs);

                                if (! allFinite (buf, bs) || peakOf (buf, bs) > 4.0f)
                                {
                                    expect (false, "unbounded at rate " + juce::String (rate)
                                                   + " block " + juce::String (bs)
                                                   + " rail " + juce::String (rail));
                                    return;
                                }
                            }
                        }
                    }
                }
            }

            expect (true);
        }
    }
};

class SpectralDelayTests final : public juce::UnitTest
{
public:
    SpectralDelayTests() : juce::UnitTest ("Spectral delay", "dsp") {}

    void runTest() override
    {
        constexpr int fftSize = 2048;
        constexpr int numBins = fftSize / 2 + 1;
        constexpr int hop = fftSize / 4;
        constexpr double sr = 48000.0;

        auto makeFlatFrame = []
        {
            std::vector<float> f (static_cast<std::size_t> (2 * numBins), 0.0f);

            for (int k = 0; k < numBins; ++k)
                f[static_cast<std::size_t> (2 * k)] = 1.0f;

            return f;
        };

        // A band table for the delay to follow.
        FractalSlicer slicer;
        slicer.prepare (2, StftEngine::maxFftSize, sr);
        slicer.setFftSize (fftSize, sr);
        FractalSlicer::Params sp;
        sp.type = FractalPatternType::cantor;
        sp.depth = 3;
        slicer.setParams (sp);
        slicer.beginFrame (hop, numBins);

        beginTest ("off is bit-exact transparent");
        {
            SpectralDelay d;
            d.prepare (2, StftEngine::maxFftSize, sr);
            d.setFftSize (fftSize, hop, sr);
            d.setBandTable (&slicer.getBandTable());

            SpectralDelay::Params dp;
            dp.on = false;
            d.setParams (dp);

            const auto reference = makeFlatFrame();

            for (int frame = 0; frame < 30; ++frame)
            {
                auto f = reference;
                d.beginFrame (numBins);
                d.processFrame (f.data(), numBins, 0);

                for (std::size_t i = 0; i < f.size(); ++i)
                    if (! juce::exactlyEqual (f[i], reference[i]))
                    {
                        expect (false, "spectral delay altered the frame while off");
                        return;
                    }
            }

            expect (! d.isRunning());
        }

        beginTest ("feedback at maximum stays bounded over thousands of frames");
        {
            SpectralDelay d;
            d.prepare (2, StftEngine::maxFftSize, sr);
            d.setFftSize (fftSize, hop, sr);
            d.setBandTable (&slicer.getBandTable());

            SpectralDelay::Params dp;
            dp.on = true;
            dp.timeMs = 20.0f;      // short, so many round trips happen
            dp.spread = 1.0f;
            dp.feedback = 1.0f;     // asks for more than the internal cap allows
            dp.damping = 0.0f;      // nothing removed between repeats
            dp.mix = 1.0f;
            d.setParams (dp);

            float worst = 0.0f;

            // 4000 frames is about 43 s of audio at this hop.
            for (int frame = 0; frame < 4000; ++frame)
            {
                auto f = makeFlatFrame();
                d.beginFrame (numBins);
                d.processFrame (f.data(), numBins, 0);
                d.processFrame (f.data(), numBins, 1);

                for (auto v : f)
                {
                    if (! std::isfinite (v))
                    {
                        expect (false, "non-finite at frame " + juce::String (frame));
                        return;
                    }

                    worst = juce::jmax (worst, std::abs (v));
                }
            }

            // Bounded by the loop's soft limit, well away from infinity.
            expect (worst < 1.0e5f, "spectral delay diverged, peak " + juce::String (worst));
        }

        beginTest ("resolved time is reported and capped by the frame ring");
        {
            SpectralDelay d;
            d.prepare (2, StftEngine::maxFftSize, sr);
            d.setFftSize (fftSize, hop, sr);
            d.setBandTable (&slicer.getBandTable());

            SpectralDelay::Params dp;
            dp.on = true;
            dp.timeMs = 250.0f;
            d.setParams (dp);
            d.beginFrame (numBins);

            // 250 ms at hop 512 / 48 kHz is ~23 frames: comfortably inside the ring.
            expectWithinAbsoluteError (d.getResolvedTimeMs(), 250.0f, 12.0f,
                                       "resolved time far from the request");

            // Now ask for far more than 128 frames can hold.
            dp.timeMs = 3000.0f;
            d.setParams (dp);
            d.beginFrame (numBins);

            const auto maxPossible = static_cast<float> (SpectralDelay::maxFrames - 1)
                                   * static_cast<float> (hop) / static_cast<float> (sr) * 1000.0f;

            expect (d.getResolvedTimeMs() <= maxPossible + 1.0f,
                    "resolved time exceeded what the ring can hold");
            expect (d.getResolvedTimeMs() > 0.0f, "resolved time collapsed to zero");
        }

        beginTest ("works with no band table attached");
        {
            SpectralDelay d;
            d.prepare (2, StftEngine::maxFftSize, sr);
            d.setFftSize (fftSize, hop, sr);
            // Deliberately no setBandTable call.

            SpectralDelay::Params dp;
            dp.on = true;
            dp.mix = 1.0f;
            dp.feedback = 0.9f;
            d.setParams (dp);

            for (int frame = 0; frame < 20; ++frame)
            {
                auto f = makeFlatFrame();
                d.beginFrame (numBins);
                d.processFrame (f.data(), numBins, 0);

                for (auto v : f)
                    expect (std::isfinite (v), "non-finite with no band table");
            }
        }

        beginTest ("every FFT size and rail stays finite");
        {
            for (auto size : { 512, 1024, 2048, 4096 })
            {
                for (double rate : { 44100.0, 192000.0 })
                {
                    for (float rail : { 0.0f, 1.0f })
                    {
                        const auto bins = size / 2 + 1;
                        const auto h = size / 4;

                        FractalSlicer s2;
                        s2.prepare (2, StftEngine::maxFftSize, rate);
                        s2.setFftSize (size, rate);
                        s2.setParams (sp);
                        s2.beginFrame (h, bins);

                        SpectralDelay d;
                        d.prepare (2, StftEngine::maxFftSize, rate);
                        d.setFftSize (size, h, rate);
                        d.setBandTable (&s2.getBandTable());

                        SpectralDelay::Params dp;
                        dp.on = true;
                        dp.timeMs = rail > 0.5f ? 3000.0f : 1.0f;
                        dp.spread = rail;
                        dp.feedback = rail;
                        dp.damping = 1.0f - rail;
                        dp.mix = rail;
                        d.setParams (dp);

                        for (int frame = 0; frame < 40; ++frame)
                        {
                            std::vector<float> f (static_cast<std::size_t> (2 * bins), 0.5f);
                            d.beginFrame (bins);
                            d.processFrame (f.data(), bins, 0);
                            d.processFrame (f.data(), bins, 1);

                            for (auto v : f)
                                if (! std::isfinite (v))
                                {
                                    expect (false, "non-finite at size " + juce::String (size)
                                                   + " rate " + juce::String (rate));
                                    return;
                                }
                        }
                    }
                }
            }

            expect (true);
        }
    }
};

class ColorStageTests final : public juce::UnitTest
{
public:
    ColorStageTests() : juce::UnitTest ("Colour stage", "dsp") {}

    void runTest() override
    {
        constexpr int block = 512;

        beginTest ("all three saturators are bounded for extreme input");
        {
            for (auto type : { SatType::softClip, SatType::tube, SatType::wavefold })
            {
                ColorStage c;
                c.prepare (2, 48000.0, block);

                ColorStage::Params cp;
                cp.satType = type;
                cp.driveDb = 36.0f;      // the parameter maximum
                cp.limiter = false;      // test the saturator itself, not the limiter
                c.setParams (cp);

                juce::AudioBuffer<float> buf { 2, block };

                // Deliberately far beyond full scale.
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i)
                        buf.setSample (ch, i, (i % 2 == 0 ? 50.0f : -50.0f));

                c.process (buf, block);

                expect (allFinite (buf, block), "non-finite saturator output");
                expect (peakOf (buf, block) < 4.0f,
                        "saturator type " + juce::String (static_cast<int> (type))
                        + " unbounded, peak " + juce::String (peakOf (buf, block)));
            }
        }

        beginTest ("limiter holds the output under full scale");
        {
            ColorStage c;
            c.prepare (2, 48000.0, block);

            ColorStage::Params cp;
            cp.driveDb = 0.0f;
            cp.limiter = true;
            c.setParams (cp);

            // Let the limiter settle, then measure.
            float worst = 0.0f;

            for (int b = 0; b < 200; ++b)
            {
                juce::AudioBuffer<float> buf { 2, block };
                fillNoise (buf, block, 4.0f, static_cast<std::uint32_t> (b));
                c.process (buf, block);

                expect (allFinite (buf, block));

                if (b > 20)
                    worst = juce::jmax (worst, peakOf (buf, block));
            }

            expect (worst < 1.2f,
                    "limiter let through " + juce::String (worst) + " (want < 1.2)");
        }

        beginTest ("drive at 0 dB with the limiter off is close to transparent");
        {
            ColorStage c;
            c.prepare (2, 48000.0, block);

            ColorStage::Params cp;
            cp.driveDb = 0.0f;
            cp.width = 1.0f;
            cp.loCutHz = 20.0f;
            cp.hiCutHz = 20000.0f;
            cp.limiter = false;
            c.setParams (cp);

            juce::AudioBuffer<float> buf { 2, block };
            fillNoise (buf, block, 0.25f, 11);
            const juce::AudioBuffer<float> reference { buf };

            // Settle the filters.
            for (int b = 0; b < 10; ++b)
            {
                juce::AudioBuffer<float> warm { 2, block };
                fillNoise (warm, block, 0.25f, 11);
                c.process (warm, block);
            }

            c.process (buf, block);

            // With the cuts parked at their extremes they are bypassed and the drive is
            // off, so this is a genuine null test, not a "close enough" check.
            double err = 0.0, ref = 0.0;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const auto d = static_cast<double> (buf.getSample (ch, i))
                                 - static_cast<double> (reference.getSample (ch, i));
                    err += d * d;
                    ref += static_cast<double> (reference.getSample (ch, i))
                         * static_cast<double> (reference.getSample (ch, i));
                }

            const auto errDb = 10.0 * std::log10 (juce::jmax (err, 1.0e-30)
                                                / juce::jmax (ref, 1.0e-30));

            expect (errDb < -120.0,
                    "unity settings are not transparent: changed the signal by "
                    + juce::String (errDb, 1) + " dB");
        }

        beginTest ("width extremes behave");
        {
            for (float width : { 0.0f, 2.0f })
            {
                ColorStage c;
                c.prepare (2, 48000.0, block);

                ColorStage::Params cp;
                cp.width = width;
                cp.limiter = false;
                c.setParams (cp);

                juce::AudioBuffer<float> buf { 2, block };

                // Width is smoothed over 20 ms, which is longer than one 512-sample
                // block, so the measurement has to wait for it to arrive.
                for (int b = 0; b < 10; ++b)
                {
                    juce::AudioBuffer<float> warm { 2, block };

                    for (int i = 0; i < block; ++i)
                    {
                        warm.setSample (0, i, 0.5f);
                        warm.setSample (1, i, -0.5f);
                    }

                    c.process (warm, block);
                }

                // Hard-panned content, so collapsing or widening is measurable.
                for (int i = 0; i < block; ++i)
                {
                    buf.setSample (0, i, 0.5f);
                    buf.setSample (1, i, -0.5f);
                }

                c.process (buf, block);

                expect (allFinite (buf, block));

                if (juce::approximatelyEqual (width, 0.0f))
                {
                    // Width 0 is mono: out-of-phase content must cancel.
                    expect (peakOf (buf, block) < 0.1f,
                            "width 0 did not collapse to mono, peak "
                            + juce::String (peakOf (buf, block)));
                }
            }
        }

        beginTest ("pathological input across rates and rails");
        {
            for (double rate : { 44100.0, 192000.0 })
            {
                for (float rail : { 0.0f, 1.0f })
                {
                    for (auto type : { SatType::softClip, SatType::tube, SatType::wavefold })
                    {
                        ColorStage c;
                        c.prepare (2, rate, block);

                        ColorStage::Params cp;
                        cp.satType = type;
                        cp.driveDb = rail * 36.0f;
                        cp.width = rail * 2.0f;
                        cp.loCutHz = rail > 0.5f ? 1000.0f : 20.0f;
                        cp.hiCutHz = rail > 0.5f ? 1000.0f : 20000.0f;
                        cp.limiter = rail > 0.5f;
                        c.setParams (cp);

                        for (int b = 0; b < 10; ++b)
                        {
                            juce::AudioBuffer<float> buf { 2, block };

                            // DC, then full scale, then silence.
                            const auto fill = b % 3 == 0 ? 1.0f : (b % 3 == 1 ? 0.0f : -1.0f);

                            for (int ch = 0; ch < 2; ++ch)
                                for (int i = 0; i < block; ++i)
                                    buf.setSample (ch, i, fill);

                            c.process (buf, block);

                            if (! allFinite (buf, block) || peakOf (buf, block) > 4.0f)
                            {
                                expect (false, "colour stage unbounded at rate "
                                               + juce::String (rate));
                                return;
                            }
                        }
                    }
                }
            }

            expect (true);
        }
    }
};

static StereoDelayTests stereoDelayTests;
static SpectralDelayTests spectralDelayTests;
static ColorStageTests colorStageTests;

} // namespace
