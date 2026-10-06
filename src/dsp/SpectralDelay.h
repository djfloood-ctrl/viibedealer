#pragma once

#include "FractalPattern.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace vbd::dsp
{

/**
    Per-band delay in the STFT frame domain, with delay times following the fractal
    subdivision so each band's echoes arrive at its own interval -- a cascading figure
    rather than one flat repeat.

    Storage is capped at 128 frames, as PLAN section 1.5 sets out: a full 2 s range at
    FFT 4096 / 192 kHz would need ~375 frames, around 12.6 MB per instance, which fights
    the "several instances" budget. 128 frames costs ~4.2 MB for stereo. The parameter
    stays in milliseconds and is converted with
    `frames = clamp(round(ms * sr / hop), 1, 128)`, so the reachable maximum shrinks as
    the FFT gets smaller or the sample rate rises. getResolvedTimeMs() reports what was
    actually achieved so the GUI can show it rather than lie.

    Off by default, and when off it costs one branch -- no ring traversal at all.
*/
class SpectralDelay
{
public:
    static constexpr int maxFrames = 128;

    struct Params
    {
        bool  on = false;
        float timeMs = 250.0f;
        float spread = 0.5f;     // 0..1, how much band delays diverge
        float feedback = 0.4f;   // 0..1
        float damping = 0.3f;    // 0..1, treble loss per repeat
        float mix = 0.5f;        // 0..1, added on top of the dry frame
    };

    void prepare (int numChannels, int maxFftSize, double sampleRate);
    void reset();

    void setFftSize (int fftSize, int hopSize, double sampleRate);
    void setParams (const Params& p) noexcept { params = p; }

    /** The slicer's table, so band delays follow the same subdivision. May be null. */
    void setBandTable (const BandTable* t) noexcept { bandTable = t; }

    /** Called once per hop, before the per-channel calls. */
    void beginFrame (int numBins);

    void processFrame (float* frame, int numBins, int channel);

    bool isRunning() const noexcept { return running; }
    float getResolvedTimeMs() const noexcept { return resolvedMs; }

private:
    float softLimit (float v) const noexcept;

    Params params;
    const BandTable* bandTable = nullptr;

    // ring[channel][frame][bin*2]
    std::vector<std::vector<float>> ring;
    std::vector<float> damp;                  // per-bin feedback damping
    std::array<int, BandTable::maxBands> bandFrames {};

    int writeIndex = 0;
    int fftSize = 2048;
    int hopSize = 512;
    double sampleRate = 44100.0;
    int numBinsCached = 0;
    float resolvedMs = 0.0f;

    // Enable/disable needs a short crossfade and a flush, or the stale tail clicks back in.
    bool running = false;
    bool needsFlush = false;
    float fade = 0.0f;
    float fadeStep = 0.1f;
};

} // namespace vbd::dsp
