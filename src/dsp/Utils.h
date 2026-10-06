#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstddef>

namespace vbd::dsp
{

/** Replaces non-finite values with zero. Cheaper than a branchy guard at every call site,
    and the only thing standing between one bad sample and a feedback loop full of NaN. */
inline float scrub (float x) noexcept
{
    return std::isfinite (x) ? x : 0.0f;
}

inline void scrubBlock (float* data, int n) noexcept
{
    for (int i = 0; i < n; ++i)
        data[i] = scrub (data[i]);
}

inline float dbToGain (float db) noexcept
{
    return db <= -99.0f ? 0.0f : std::pow (10.0f, db * 0.05f);
}

inline float gainToDb (float gain) noexcept
{
    return gain <= 1.0e-9f ? -180.0f : 20.0f * std::log10 (gain);
}

/** Wraps a phase into (-pi, pi]. */
inline float wrapPhase (float p) noexcept
{
    constexpr auto twoPi = juce::MathConstants<float>::twoPi;
    constexpr auto pi    = juce::MathConstants<float>::pi;

    p = std::fmod (p + pi, twoPi);

    if (p < 0.0f)
        p += twoPi;

    return p - pi;
}

/** Periodic Hann window. Periodic (not symmetric) is what makes Hann satisfy COLA at
    75% overlap -- a symmetric window leaves a ripple in the overlap-add sum. */
inline void fillHann (float* w, int n) noexcept
{
    const auto scale = juce::MathConstants<double>::twoPi / static_cast<double> (n);

    for (int i = 0; i < n; ++i)
        w[i] = static_cast<float> (0.5 - 0.5 * std::cos (scale * static_cast<double> (i)));
}

/** Sum of w^2 across all overlapping frames, which is the WOLA normalisation denominator.
    For periodic Hann at hop = N/4 this is exactly 1.5. Computed rather than hardcoded so
    the engine stays correct if the window or overlap ever changes. */
inline double windowOverlapGain (const float* w, int n, int hop) noexcept
{
    double sum = 0.0;

    for (int offset = 0; offset < n; offset += hop)
        sum += static_cast<double> (w[offset]) * static_cast<double> (w[offset]);

    return sum;
}

/** Rebuilds the conjugate-symmetric upper half of a real signal's spectrum.

    JUCE's real-only inverse transform expects the full interleaved layout that the forward
    transform produced. After we modify bins 0..N/2 we must restore that symmetry, or the
    inverse transform sees an inconsistent spectrum and returns a signal with a spurious
    imaginary component folded into it. */
inline void mirrorConjugate (float* data, int fftSize) noexcept
{
    const auto half = fftSize / 2;

    // DC and Nyquist are real by definition.
    data[1] = 0.0f;
    data[2 * half + 1] = 0.0f;

    for (int k = 1; k < half; ++k)
    {
        const auto src = 2 * k;
        const auto dst = 2 * (fftSize - k);

        data[dst]     =  data[src];
        data[dst + 1] = -data[src + 1];
    }
}

/** Integer-sample delay line used to align the dry path with the STFT's latency. */
class LatencyDelay
{
public:
    void prepare (int maxDelaySamples, int maxBlockSize)
    {
        capacity = maxDelaySamples + maxBlockSize + 1;
        buffer.assign (static_cast<std::size_t> (capacity), 0.0f);
        writePos = 0;
        delay = 0;
    }

    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePos = 0;
    }

    /** Changing the delay flushes the line: a smooth change of an integer delay is not
        possible, so the caller is expected to fade the output around the switch. */
    void setDelay (int samples) noexcept
    {
        samples = juce::jlimit (0, capacity - 1, samples);

        if (samples != delay)
        {
            delay = samples;
            reset();
        }
    }

    int getDelay() const noexcept { return delay; }

    void process (const float* in, float* out, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[static_cast<std::size_t> (writePos)] = in[i];

            auto readPos = writePos - delay;

            if (readPos < 0)
                readPos += capacity;

            out[i] = buffer[static_cast<std::size_t> (readPos)];

            if (++writePos >= capacity)
                writePos = 0;
        }
    }

private:
    std::vector<float> buffer;
    int capacity = 0;
    int writePos = 0;
    int delay = 0;
};

} // namespace vbd::dsp
