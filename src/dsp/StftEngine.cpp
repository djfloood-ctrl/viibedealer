#include "StftEngine.h"
#include "Utils.h"

namespace vbd::dsp
{

namespace
{
    int indexForFftSize (int n) noexcept
    {
        switch (n)
        {
            case 512:  return 0;
            case 1024: return 1;
            case 2048: return 2;
            case 4096: return 3;
            default:   return 2;
        }
    }
}

void StftEngine::prepare (int numChannels, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);

    preparedChannels = juce::jmax (1, numChannels);

    for (int i = 0; i < 4; ++i)
    {
        const auto size  = minFftSize << i;
        const auto order = static_cast<int> (std::log2 (static_cast<double> (size)) + 0.5);
        ffts[static_cast<std::size_t> (i)] = std::make_unique<juce::dsp::FFT> (order);
    }

    channels.assign (static_cast<std::size_t> (preparedChannels), {});

    for (auto& ch : channels)
    {
        // Every buffer is sized for the largest FFT, so changing size later is index
        // arithmetic only.
        ch.inRing.assign  (static_cast<std::size_t> (maxFftSize), 0.0f);
        ch.carRing.assign (static_cast<std::size_t> (maxFftSize), 0.0f);
        ch.outRing.assign (static_cast<std::size_t> (maxFftSize), 0.0f);
        ch.modScratch.assign (static_cast<std::size_t> (2 * maxFftSize), 0.0f);
        ch.carScratch.assign (static_cast<std::size_t> (2 * maxFftSize), 0.0f);
        ch.outScratch.assign (static_cast<std::size_t> (2 * maxFftSize), 0.0f);
    }

    window.assign (static_cast<std::size_t> (maxFftSize), 0.0f);

    const auto requested = fftSize;
    fftSize = 0;              // force setFftSize to rebuild the window
    setFftSize (requested);
    reset();
}

bool StftEngine::setFftSize (int newFftSize)
{
    newFftSize = juce::jlimit (minFftSize, maxFftSize, newFftSize);

    if (! juce::isPowerOfTwo (newFftSize))
        newFftSize = 2048;

    if (newFftSize == fftSize)
        return false;

    fftSize  = newFftSize;
    fftIndex = indexForFftSize (fftSize);
    hopSize  = fftSize / overlap;

    fillHann (window.data(), fftSize);

    // Measured rather than hardcoded: if the window or overlap ever changes, the gain
    // follows automatically instead of silently becoming wrong.
    const auto olaGain = windowOverlapGain (window.data(), fftSize, hopSize);
    synthesisGain = olaGain > 1.0e-9 ? static_cast<float> (1.0 / olaGain) : 1.0f;

    reset();
    return true;
}

void StftEngine::reset()
{
    for (auto& ch : channels)
    {
        std::fill (ch.inRing.begin(),  ch.inRing.end(),  0.0f);
        std::fill (ch.carRing.begin(), ch.carRing.end(), 0.0f);
        std::fill (ch.outRing.begin(), ch.outRing.end(), 0.0f);
        ch.writePos = 0;
        ch.readPos  = 0;
    }

    hopCount = 0;
}

void StftEngine::processFrameForChannel (int channel, FrameSink& sink)
{
    auto& ch = channels[static_cast<std::size_t> (channel)];

    const auto n       = fftSize;
    const auto numBins = getNumBins();

    // Unwrap the ring into the FFT scratch, windowing as we go. The frame covers the N
    // most recent samples, oldest first.
    for (int i = 0; i < n; ++i)
    {
        auto idx = ch.writePos + i;

        if (idx >= n)
            idx -= n;

        const auto w = window[static_cast<std::size_t> (i)];

        ch.modScratch[static_cast<std::size_t> (i)] = ch.inRing[static_cast<std::size_t> (idx)]  * w;
        ch.carScratch[static_cast<std::size_t> (i)] = ch.carRing[static_cast<std::size_t> (idx)] * w;
    }

    std::fill (ch.modScratch.begin() + n, ch.modScratch.begin() + 2 * n, 0.0f);
    std::fill (ch.carScratch.begin() + n, ch.carScratch.begin() + 2 * n, 0.0f);

    const auto& fft = *ffts[static_cast<std::size_t> (fftIndex)];

    fft.performRealOnlyForwardTransform (ch.modScratch.data());
    fft.performRealOnlyForwardTransform (ch.carScratch.data());

    sink.processFrame (ch.modScratch.data(), ch.carScratch.data(),
                       ch.outScratch.data(), numBins, channel);

    // The sink only writes bins 0..N/2; the inverse transform needs the mirrored half.
    mirrorConjugate (ch.outScratch.data(), n);

    fft.performRealOnlyInverseTransform (ch.outScratch.data());

    // Synthesis window + WOLA normalisation, accumulated into the output ring starting at
    // the next sample due out.
    for (int i = 0; i < n; ++i)
    {
        auto idx = ch.readPos + i;

        if (idx >= n)
            idx -= n;

        const auto w = window[static_cast<std::size_t> (i)];

        ch.outRing[static_cast<std::size_t> (idx)] +=
            ch.outScratch[static_cast<std::size_t> (i)] * w * synthesisGain;
    }
}

void StftEngine::process (const float* const* modulator,
                          const float* const* carrier,
                          float* const* output,
                          int numChannels,
                          int numSamples,
                          FrameSink& sink)
{
    const auto chans = juce::jmin (numChannels, static_cast<int> (channels.size()));
    const auto n = fftSize;

    for (int i = 0; i < numSamples; ++i)
    {
        for (int c = 0; c < chans; ++c)
        {
            auto& ch = channels[static_cast<std::size_t> (c)];

            ch.inRing[static_cast<std::size_t> (ch.writePos)]  = modulator[c][i];
            ch.carRing[static_cast<std::size_t> (ch.writePos)] = carrier[c][i];

            output[c][i] = ch.outRing[static_cast<std::size_t> (ch.readPos)];
            ch.outRing[static_cast<std::size_t> (ch.readPos)] = 0.0f;

            if (++ch.writePos >= n) ch.writePos = 0;
            if (++ch.readPos  >= n) ch.readPos  = 0;
        }

        if (++hopCount >= hopSize)
        {
            hopCount = 0;

            for (int c = 0; c < chans; ++c)
                processFrameForChannel (c, sink);
        }
    }
}

} // namespace vbd::dsp
