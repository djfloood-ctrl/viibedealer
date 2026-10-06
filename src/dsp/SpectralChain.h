#pragma once

#include <array>
#include <cmath>

#include "FractalSlicer.h"
#include "SpectralDelay.h"
#include "../gui/VisualBridge.h"
#include "SpectralEngine.h"
#include "StftEngine.h"

namespace vbd::dsp
{

/**
    Runs the per-frame stages in order as a single FrameSink.

        SpectralEngine (morph/vocode) -> FractalSlicer -> SpectralDelay

    The StftEngine calls this once per channel per hop. Stages that need to advance once
    per hop rather than once per channel (the slicer's animation) are stepped on channel 0,
    which the STFT guarantees comes first.

*/
class SpectralChain final : public FrameSink
{
public:
    void prepare (int numChannels, int maxFftSize, double sampleRate)
    {
        chainSampleRate = sampleRate;

        engine.prepare (numChannels, maxFftSize, sampleRate);
        slicer.prepare (numChannels, maxFftSize, sampleRate);
        sdelay.prepare (numChannels, maxFftSize, sampleRate);
        sdelay.setBandTable (&slicer.getBandTable());
    }

    void reset()
    {
        engine.reset();
        slicer.reset();
        sdelay.reset();
    }

    void setFftSize (int fftSize, const juce::dsp::FFT* fft, float magnitudeScale, double sampleRate)
    {
        chainFftSize = fftSize;
        chainSampleRate = sampleRate;
        displayScale = magnitudeScale;
        displayMapBins = 0;

        engine.setFftSize (fftSize, fft, magnitudeScale);
        slicer.setFftSize (fftSize, sampleRate);
        sdelay.setFftSize (fftSize, fftSize / StftEngine::overlap, sampleRate);
        sdelay.setBandTable (&slicer.getBandTable());
    }

    /** Hop size must be known so the slicer can advance its animation per frame. */
    void setHopSize (int hop) noexcept { hopSize = hop; }

    /** Where to publish display data. Optional: null simply skips the capture. */
    void setVisualBridge (gui::VisualBridge* b) noexcept { bridge = b; }

    void setTransportInfo (double bpm, double ppq, bool playing) noexcept
    {
        snapBpm = bpm;
        snapPpq = ppq;
        snapPlaying = playing;
    }

    void processFrame (const float* modulator,
                       const float* carrier,
                       float* output,
                       int numBins,
                       int channel) override
    {
        if (channel == 0)
        {
            slicer.beginFrame (hopSize, numBins);
            sdelay.beginFrame (numBins);
        }

        engine.processFrame (modulator, carrier, output, numBins, channel);
        slicer.processFrame (output, numBins, channel);
        sdelay.processFrame (output, numBins, channel);

        // Display capture reuses the spectra this frame already produced, so the three
        // analyser curves cost a decimation pass rather than three extra FFTs.
        if (channel == 0 && bridge != nullptr)
            captureVisuals (modulator, carrier, output, numBins);
    }

    SpectralEngine& spectralEngine() noexcept { return engine; }
    FractalSlicer&  fractalSlicer()  noexcept { return slicer; }
    SpectralDelay&  spectralDelay()  noexcept { return sdelay; }

private:
    void rebuildDisplayMap (int numBins);
    void captureVisuals (const float* modulator, const float* carrier,
                         const float* output, int numBins);

    gui::VisualBridge* bridge = nullptr;

    // Maps each of the 256 log-spaced display points to a range of source bins.
    std::array<int, gui::VisualSnapshot::spectrumPoints + 1> displayEdges {};
    int displayMapBins = 0;
    float displayScale = 1.0f;

    int chainFftSize = 2048;
    double chainSampleRate = 44100.0;

    double snapBpm = 120.0;
    double snapPpq = 0.0;
    bool snapPlaying = false;

    SpectralEngine engine;
    FractalSlicer  slicer;
    SpectralDelay  sdelay;
    int hopSize = 512;
};

} // namespace vbd::dsp
