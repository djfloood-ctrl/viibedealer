#pragma once

#include "SpectralEnvelope.h"
#include "StftEngine.h"

#include <vector>

namespace vbd::dsp
{

enum class EngineMode
{
    vocode = 0,
    magMorph,
    phaseMorph,
    cross
};

/**
    The spectral morph/vocode stage. Operates on one aligned frame pair at a time.

    Mode notes:

    - **Vocode** imposes the modulator's spectral envelope on the carrier. Only magnitudes
      are touched, so no `atan2` is needed: the carrier's complex value is simply scaled.
    - **Mag Morph** crossfades magnitudes and keeps the carrier's phase -- again a pure
      scale, no polar conversion.
    - **Cross** takes magnitude from one source and phase from the other, which is also
      expressible as a scale of the phase donor.
    - **Phase Morph** is the only mode that needs real phase arithmetic. Blending raw phase
      values would be wrong (phase is circular, so lerp(0.1*pi, 1.9*pi) lands on pi -- the
      opposite of both inputs). Instead it blends the *phase gradient*, the frame-to-frame
      phase increment, and integrates it. Phase Lock crossfades that against a
      blend-on-the-unit-circle, which is tighter but less fluid.
*/
class SpectralEngine final : public FrameSink
{
public:
    struct Params
    {
        EngineMode mode = EngineMode::vocode;
        float morph       = 1.0f;   // 0..1
        float formantShift = 0.0f;  // semitones
        float tilt        = 0.0f;   // dB/octave
        float envRes      = 0.5f;   // 0..1
        bool  cepstral    = false;
        float phaseLock   = 0.5f;   // 0..1
        float gateDb      = -60.0f;
        bool  freeze      = false;
        bool  flip        = false;
        float freqShiftHz = 0.0f;
    };

    void prepare (int numChannels, int maxFftSize, double sampleRate);
    void reset();

    void setFftSize (int fftSize, const juce::dsp::FFT* fft, float magnitudeScale);
    void setParams (const Params& p) noexcept { params = p; }

    void processFrame (const float* modulator,
                       const float* carrier,
                       float* output,
                       int numBins,
                       int channel) override;

private:
    struct ChannelState
    {
        std::vector<float> modMag;
        std::vector<float> carMag;
        std::vector<float> modEnv;
        std::vector<float> carEnv;
        std::vector<float> warpEnv;
        std::vector<float> frozenMag;
        std::vector<float> shiftedRe;
        std::vector<float> shiftedIm;
        std::vector<float> prevModPhase;
        std::vector<float> prevCarPhase;
        std::vector<float> outPhase;
        bool hasFrozen = false;
        bool phaseValid = false;
    };

    void applyFreqShift (const float* carrier, ChannelState& st, int numBins);
    void buildTilt (int numBins);

    std::vector<ChannelState> channels;
    std::vector<float> tiltGain;
    SpectralEnvelope envelope;

    Params params;
    const juce::dsp::FFT* fft = nullptr;
    int fftSize = 2048;
    double sampleRate = 44100.0;
    float magScale = 1.0f;
    float cachedTilt = std::numeric_limits<float>::quiet_NaN();
    int cachedTiltBins = 0;
};

} // namespace vbd::dsp
