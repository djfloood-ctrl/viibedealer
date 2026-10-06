#pragma once

#include <juce_dsp/juce_dsp.h>

#include <array>
#include <memory>
#include <vector>

namespace vbd::dsp
{

/** Receives one aligned pair of analysis frames and writes the output frame.

    Buffers are interleaved complex, `numBins` entries of (re, im), covering bins 0..N/2.
    Implementations must not allocate, lock or block. */
class FrameSink
{
public:
    virtual ~FrameSink() = default;

    virtual void processFrame (const float* modulator,
                               const float* carrier,
                               float* output,
                               int numBins,
                               int channel) = 0;
};

/**
    Weighted overlap-add STFT with two aligned analysis streams and one synthesis stream.

    Hann analysis and Hann synthesis at hop = N/4. The synthesis gain is the measured
    sum of squared window values across the overlapping frames (exactly 1.5 for periodic
    Hann at 75% overlap), so the reconstruction is unity-gain when the frame transform is
    the identity.

    Latency is exactly N samples. The derivation: a frame triggered after pushing input
    sample n covers inputs n-N+1..n, and its first output sample is emitted as y(n+1),
    so y(n+1) carries input n-N+1.

    All memory is sized for the largest FFT in prepare(). Changing FFT size afterwards only
    moves indices around -- it never allocates.
*/
class StftEngine
{
public:
    static constexpr int maxFftSize = 4096;
    static constexpr int minFftSize = 512;
    static constexpr int overlap    = 4;
    static constexpr int maxBins    = maxFftSize / 2 + 1;

    void prepare (int numChannels, int maxBlockSize);
    void reset();

    /** Sets the FFT size. Returns true if it changed. Never allocates. */
    bool setFftSize (int newFftSize);

    int getFftSize() const noexcept  { return fftSize; }
    int getHopSize() const noexcept  { return fftSize / overlap; }
    int getNumBins() const noexcept  { return fftSize / 2 + 1; }

    /** Latency in samples, equal to the FFT size. */
    int getLatencySamples() const noexcept { return fftSize; }

    /** The FFT object for the current size, exposed so a sink can run a cepstrum without
        owning a duplicate set of twiddle tables. */
    const juce::dsp::FFT* currentFft() const noexcept { return ffts[fftIndex].get(); }

    /** Magnitude scale that maps a full-scale sine's peak bin to 1.0, so magnitude
        thresholds expressed in dBFS mean what they say. */
    float magnitudeScale() const noexcept { return 4.0f / static_cast<float> (fftSize); }

    /** Processes one block. `modulator` and `carrier` must hold numSamples per channel;
        `output` receives the synthesised signal, delayed by getLatencySamples(). */
    void process (const float* const* modulator,
                  const float* const* carrier,
                  float* const* output,
                  int numChannels,
                  int numSamples,
                  FrameSink& sink);

private:
    void processFrameForChannel (int channel, FrameSink& sink);

    struct ChannelState
    {
        std::vector<float> inRing;      // most recent N modulator samples
        std::vector<float> carRing;     // most recent N carrier samples
        std::vector<float> outRing;     // overlap-add accumulator
        std::vector<float> modScratch;  // 2N, FFT workspace
        std::vector<float> carScratch;  // 2N
        std::vector<float> outScratch;  // 2N
        int writePos = 0;
        int readPos  = 0;
    };

    std::vector<ChannelState> channels;
    std::vector<float> window;

    // One FFT per supported size, built in prepare so switching never allocates.
    std::array<std::unique_ptr<juce::dsp::FFT>, 4> ffts;

    int fftSize   = 2048;
    int fftIndex  = 2;
    int hopSize   = 512;
    int hopCount  = 0;
    float synthesisGain = 1.0f;
    int preparedChannels = 0;
};

} // namespace vbd::dsp
