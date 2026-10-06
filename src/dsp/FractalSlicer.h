#pragma once

#include "FractalPattern.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace vbd::dsp
{

/**
    Gates and reshapes the spectrum along a fractal subdivision, animated in time.

    How the controls relate, since the spec leaves it open:

    - **Shatter is the amount.** At 0 the slicer is transparent: inactive bands are not
      muted, the cascade weights are flattened to unity, and nothing is bin-shifted. Past
      0 it does three things progressively -- mutes inactive bands, scales active bands by
      their position in the multiplicative cascade, and above halfway starts bin-shifting
      bands by their cascade level, which is the deliberately metallic region flagged in
      PLAN section 1.8.
    - **Rhythm Gate** stutters bands using the same self-similar sequence sampled along
      time instead of frequency, so the stutter and the spectral split are the same figure.
    - **Rotation** slides the pattern across fixed band edges rather than moving the edges
      themselves, so animation changes which bands are open without smearing their
      boundaries.
    - **Grit** sharpens both smoothings at once: the per-bin edge taper in frequency and
      the per-frame gain slew in time. At 1 both are instant, which is where it bites.
*/
class FractalSlicer
{
public:
    struct Params
    {
        FractalPatternType type = FractalPatternType::cantor;
        int   depth = 3;
        int   seed  = 1;
        float ratio = 0.5f;
        float asym  = 0.0f;
        float loHz  = 60.0f;
        float hiHz  = 12000.0f;
        bool  invert = false;
        float shatter = 0.0f;   // 0..1, the amount
        bool  sync = true;
        int   divIndex = 8;     // index into the sync division list
        float rateHz = 2.0f;    // used when sync is off
        float gate = 0.0f;      // 0..1 rhythm gate depth
        float grit = 0.0f;      // 0..1
        float smoothMs = 6.0f;
        float mix = 1.0f;       // 0..1 slicer wet/dry
    };

    void prepare (int numChannels, int maxFftSize, double sampleRate);
    void reset();

    void setFftSize (int fftSize, double sampleRate);
    void setParams (const Params& p) noexcept { pending = p; }
    void setTransport (double bpm, bool playing) noexcept;

    /** Called once per hop, before the per-channel calls. Advances the animation and
        rebuilds the gain map if anything changed. */
    void beginFrame (int hopSamples, int numBins);

    /** Applies the current gain map to one channel's frame, in place. */
    void processFrame (float* frame, int numBins, int channel);

    int getEffectiveDepth() const noexcept { return table.effectiveDepth; }
    int getNumBands() const noexcept       { return table.numBands; }
    bool wasDepthClamped() const noexcept  { return table.depthWasClamped; }

    /** The current subdivision, shared with the spectral delay so its per-band times
        follow the same fractal figure. */
    const BandTable& getBandTable() const noexcept { return table; }

    /** Per-band energy for the visualiser, filled during processFrame on channel 0. */
    const std::vector<float>& bandEnergies() const noexcept { return energies; }

private:
    void rebuildIfNeeded (int numBins);
    void buildGainMap (int numBins);

    Params params;
    Params pending;
    Params applied;
    bool haveApplied = false;

    BandTable table;

    std::vector<float> targetGain;   // per bin
    std::vector<int>   sourceBin;    // per bin, for bin shifting
    bool anyShift = false;

    struct ChannelState
    {
        std::vector<float> smoothed;  // per-bin smoothed gain
        std::vector<float> scratch;   // interleaved complex copy, used when shifting
    };

    std::vector<ChannelState> channels;
    std::vector<float> energies;

    double sampleRate = 44100.0;
    int    fftSize = 2048;
    double bpm = 120.0;
    bool   transportPlaying = false;

    double phase = 0.0;          // 0..1 animation phase
    int    rotationOffset = 0;
    int    rhythmStepIndex = 0;
    float  slewCoeff = 0.5f;
    int    cachedBins = 0;
};

} // namespace vbd::dsp
