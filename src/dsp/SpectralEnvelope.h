#pragma once

#include <juce_dsp/juce_dsp.h>

#include <vector>

namespace vbd::dsp
{

/**
    Extracts a smoothed spectral envelope from a magnitude spectrum.

    Two backends, both driven by one Resolution control:

    - **Log-band smoothing** (default). A running-integral average over a log-frequency
      window, so the smoothing width is constant in octaves rather than in Hz. O(numBins)
      regardless of width.
    - **Cepstral liftering** (opt-in). Log magnitude through an FFT, high-order coefficients
      discarded, back again. Truer formant estimation, but it costs an extra forward and
      inverse FFT per frame per channel -- roughly doubling engine cost, which is why it is
      not the default.
*/
class SpectralEnvelope
{
public:
    void prepare (int maxNumBins, int maxFftSize);

    /** @param mag        input magnitudes, numBins entries
        @param env        output envelope, numBins entries (may alias nothing)
        @param resolution 0 = very smooth (broad formants), 1 = detailed
        @param cepstral   use the cepstral backend
        @param fft        FFT matching fftSize, required only when cepstral is true */
    void compute (const float* mag,
                  float* env,
                  int numBins,
                  int fftSize,
                  float resolution,
                  bool cepstral,
                  const juce::dsp::FFT* fft);

private:
    void computeLogBand (const float* mag, float* env, int numBins, float resolution);
    void computeCepstral (const float* mag, float* env, int numBins, int fftSize,
                          float resolution, const juce::dsp::FFT& fft);

    std::vector<double> integral;   // running sum, numBins + 1
    std::vector<float>  cepScratch; // 2 * maxFftSize
};

} // namespace vbd::dsp
