#include "SpectralEnvelope.h"
#include "Utils.h"

namespace vbd::dsp
{

void SpectralEnvelope::prepare (int maxNumBins, int maxFftSize)
{
    integral.assign (static_cast<std::size_t> (maxNumBins + 1), 0.0);
    cepScratch.assign (static_cast<std::size_t> (2 * maxFftSize), 0.0f);
}

void SpectralEnvelope::compute (const float* mag,
                                float* env,
                                int numBins,
                                int fftSize,
                                float resolution,
                                bool cepstral,
                                const juce::dsp::FFT* fft)
{
    resolution = juce::jlimit (0.0f, 1.0f, resolution);

    if (cepstral && fft != nullptr)
        computeCepstral (mag, env, numBins, fftSize, resolution, *fft);
    else
        computeLogBand (mag, env, numBins, resolution);
}

void SpectralEnvelope::computeLogBand (const float* mag, float* env, int numBins,
                                       float resolution)
{
    // Smoothing half-width in octaves: broad at resolution 0, tight at 1.
    const auto octaves = juce::jmap (resolution, 0.0f, 1.0f, 1.0f, 0.04f);
    const auto ratio   = std::pow (2.0f, octaves);

    integral[0] = 0.0;

    for (int k = 0; k < numBins; ++k)
        integral[static_cast<std::size_t> (k + 1)] =
            integral[static_cast<std::size_t> (k)] + static_cast<double> (mag[k]);

    for (int k = 0; k < numBins; ++k)
    {
        // Constant width in octaves means the window grows with frequency.
        const auto fk = static_cast<float> (k);

        auto lo = static_cast<int> (std::floor (fk / ratio));
        auto hi = static_cast<int> (std::ceil  (fk * ratio));

        // Bin 0 has no octave below it, so give the low end a floor of a few bins.
        lo = juce::jlimit (0, numBins - 1, lo);
        hi = juce::jlimit (0, numBins - 1, juce::jmax (hi, lo + 1));

        const auto count = hi - lo + 1;
        const auto sum = integral[static_cast<std::size_t> (hi + 1)]
                       - integral[static_cast<std::size_t> (lo)];

        env[k] = static_cast<float> (sum / static_cast<double> (count));
    }
}

void SpectralEnvelope::computeCepstral (const float* mag, float* env, int numBins,
                                        int fftSize, float resolution,
                                        const juce::dsp::FFT& fft)
{
    constexpr auto floorMag = 1.0e-7f;

    // Build a conjugate-symmetric real sequence from the log magnitudes so the cepstrum
    // comes out real.
    for (int k = 0; k < numBins; ++k)
        cepScratch[static_cast<std::size_t> (k)] = std::log (juce::jmax (mag[k], floorMag));

    for (int k = numBins; k < fftSize; ++k)
        cepScratch[static_cast<std::size_t> (k)] =
            cepScratch[static_cast<std::size_t> (fftSize - k)];

    std::fill (cepScratch.begin() + fftSize, cepScratch.begin() + 2 * fftSize, 0.0f);

    fft.performRealOnlyForwardTransform (cepScratch.data());

    // Lifter: keep the low-order coefficients, which carry the broad spectral shape.
    // Resolution 0 keeps very few (smooth), 1 keeps many (detailed).
    const auto maxOrder = juce::jmax (4, fftSize / 16);
    const auto keep = juce::jlimit (2, maxOrder,
                                    static_cast<int> (juce::jmap (resolution, 0.0f, 1.0f,
                                                                  4.0f,
                                                                  static_cast<float> (maxOrder))));

    for (int k = keep; k <= fftSize / 2; ++k)
    {
        cepScratch[static_cast<std::size_t> (2 * k)]     = 0.0f;
        cepScratch[static_cast<std::size_t> (2 * k + 1)] = 0.0f;
    }

    mirrorConjugate (cepScratch.data(), fftSize);

    fft.performRealOnlyInverseTransform (cepScratch.data());

    for (int k = 0; k < numBins; ++k)
        env[k] = std::exp (juce::jlimit (-16.0f, 16.0f,
                                         cepScratch[static_cast<std::size_t> (k)]));
}

} // namespace vbd::dsp
