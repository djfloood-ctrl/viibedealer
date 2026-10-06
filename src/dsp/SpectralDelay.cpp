#include "SpectralDelay.h"
#include "Utils.h"

namespace vbd::dsp
{

namespace
{
    // Frame-domain magnitudes are not normalised: a full-scale sine at FFT 2048 peaks
    // near N/4 = 512. The soft limit sits well above that so it only engages on a
    // genuinely runaway loop, never on musical material.
    constexpr float kLoopCeiling = 4096.0f;

    // Hard cap on feedback. Strictly below 1 means the loop decays geometrically, which
    // is the real stability guarantee; the soft limit is the backstop.
    constexpr float kMaxFeedback = 0.95f;
}

void SpectralDelay::prepare (int numChannels, int maxFftSize, double sr)
{
    sampleRate = sr;

    const auto maxBins = maxFftSize / 2 + 1;
    const auto frameFloats = static_cast<std::size_t> (2 * maxBins);

    ring.assign (static_cast<std::size_t> (juce::jmax (1, numChannels)), {});

    for (auto& chRing : ring)
        chRing.assign (frameFloats * static_cast<std::size_t> (maxFrames), 0.0f);

    damp.assign (static_cast<std::size_t> (maxBins), 1.0f);

    reset();
}

void SpectralDelay::reset()
{
    for (auto& chRing : ring)
        std::fill (chRing.begin(), chRing.end(), 0.0f);

    bandFrames.fill (1);
    writeIndex = 0;
    running = false;
    needsFlush = false;
    fade = 0.0f;
    numBinsCached = 0;
}

void SpectralDelay::setFftSize (int newFftSize, int newHopSize, double sr)
{
    fftSize = newFftSize;
    hopSize = juce::jmax (1, newHopSize);
    sampleRate = sr;
    numBinsCached = 0;
    reset();
}

float SpectralDelay::softLimit (float v) const noexcept
{
    // Bounded by kLoopCeiling for any finite input, and essentially linear below it.
    const auto a = std::abs (v);
    return v / (1.0f + a / kLoopCeiling);
}

void SpectralDelay::beginFrame (int numBins)
{
    // Fade length ~15 ms expressed in frames.
    const auto framesPerSecond = sampleRate / static_cast<double> (hopSize);
    fadeStep = static_cast<float> (1.0 / juce::jmax (1.0, framesPerSecond * 0.015));

    if (params.on)
    {
        if (! running)
        {
            // Starting from silence, so no flush needed -- but the ring may hold a stale
            // tail from a previous enable.
            std::for_each (ring.begin(), ring.end(), [] (auto& r)
                           { std::fill (r.begin(), r.end(), 0.0f); });
            running = true;
            fade = 0.0f;
        }

        fade = juce::jmin (1.0f, fade + fadeStep);
    }
    else
    {
        if (running)
        {
            fade -= fadeStep;

            if (fade <= 0.0f)
            {
                fade = 0.0f;
                running = false;
                needsFlush = true;
            }
        }

        if (needsFlush)
        {
            std::for_each (ring.begin(), ring.end(), [] (auto& r)
                           { std::fill (r.begin(), r.end(), 0.0f); });
            needsFlush = false;
        }
    }

    if (! running)
        return;

    // Advance once per hop, before any channel writes, so every channel shares the slot.
    if (++writeIndex >= maxFrames)
        writeIndex = 0;

    // Base delay in frames, with the ceiling the storage allows.
    const auto wanted = static_cast<double> (params.timeMs) * 0.001 * sampleRate
                      / static_cast<double> (hopSize);
    const auto baseFrames = juce::jlimit (1, maxFrames - 1,
                                          static_cast<int> (std::lround (wanted)));

    resolvedMs = static_cast<float> (static_cast<double> (baseFrames)
                                     * static_cast<double> (hopSize) / sampleRate * 1000.0);

    // Per-band delays follow the fractal cascade: a band's level in the subdivision
    // stretches its delay, so repeats fan out instead of arriving together.
    const auto spread = juce::jlimit (0.0f, 1.0f, params.spread);

    if (bandTable != nullptr && bandTable->numBands > 0)
    {
        const auto effDepth = juce::jmax (1, bandTable->effectiveDepth);

        for (int i = 0; i < bandTable->numBands && i < BandTable::maxBands; ++i)
        {
            const auto level = bandTable->bands[static_cast<std::size_t> (i)].level;
            const auto norm = static_cast<float> (level) / static_cast<float> (effDepth);
            const auto scaled = static_cast<float> (baseFrames) * (1.0f + spread * norm * 2.0f);

            bandFrames[static_cast<std::size_t> (i)] =
                juce::jlimit (1, maxFrames - 1, static_cast<int> (std::lround (scaled)));
        }
    }
    else
    {
        bandFrames.fill (baseFrames);
    }

    // Per-bin damping of the feedback path: progressive treble loss across repeats, which
    // is what makes a long tail sound like a tail rather than a stack of copies.
    if (numBinsCached != numBins)
    {
        numBinsCached = numBins;
    }

    const auto damping = juce::jlimit (0.0f, 1.0f, params.damping);

    for (int k = 0; k < numBins; ++k)
    {
        const auto norm = static_cast<float> (k) / static_cast<float> (juce::jmax (1, numBins - 1));
        damp[static_cast<std::size_t> (k)] = juce::jlimit (0.0f, 1.0f,
                                                           1.0f - damping * std::sqrt (norm));
    }
}

void SpectralDelay::processFrame (float* frame, int numBins, int channel)
{
    if (! running || channel >= static_cast<int> (ring.size()))
        return;

    auto& chRing = ring[static_cast<std::size_t> (channel)];
    const auto frameStride = static_cast<std::size_t> (2 * (fftSize / 2 + 1));

    auto* writeFrame = chRing.data() + frameStride * static_cast<std::size_t> (writeIndex);

    // Bins outside every band are never read back, so clearing the slot keeps stale
    // content from reappearing if the band range moves.
    std::fill (writeFrame, writeFrame + 2 * numBins, 0.0f);

    const auto feedback = juce::jlimit (0.0f, kMaxFeedback, params.feedback);
    const auto mix = juce::jlimit (0.0f, 1.0f, params.mix) * fade;

    const auto numBands = (bandTable != nullptr) ? bandTable->numBands : 0;

    if (numBands <= 0)
        return;

    for (int b = 0; b < numBands && b < BandTable::maxBands; ++b)
    {
        const auto& band = bandTable->bands[static_cast<std::size_t> (b)];
        const auto delayFrames = bandFrames[static_cast<std::size_t> (b)];

        auto readIndex = writeIndex - delayFrames;

        while (readIndex < 0)
            readIndex += maxFrames;

        const auto* readFrame = chRing.data() + frameStride * static_cast<std::size_t> (readIndex);

        const auto hi = juce::jmin (band.hiBin, numBins);

        for (int k = juce::jmax (0, band.loBin); k < hi; ++k)
        {
            const auto ku = static_cast<std::size_t> (2 * k);

            const auto inRe = frame[2 * k];
            const auto inIm = frame[2 * k + 1];

            const auto delRe = scrub (readFrame[ku]);
            const auto delIm = scrub (readFrame[ku + 1]);

            // Dry frame plus the delayed content.
            frame[2 * k]     = scrub (inRe + mix * delRe);
            frame[2 * k + 1] = scrub (inIm + mix * delIm);

            // Recirculate, damped and soft-limited.
            const auto d = damp[static_cast<std::size_t> (k)] * feedback;

            writeFrame[ku]     = softLimit (inRe + d * delRe);
            writeFrame[ku + 1] = softLimit (inIm + d * delIm);
        }
    }
}

} // namespace vbd::dsp
