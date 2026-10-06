#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace vbd::dsp
{

enum class SatType { softClip = 0, tube, wavefold };

/**
    Output colouring: saturation, stereo width, shelving cuts, and a toggleable safety
    limiter.

    All three saturators are bounded for any input. Wavefold is the exception worth
    naming: folding is periodic, so it can *reduce* a rising input's output -- that is the
    intended sound, not a bug, but it means Drive is not monotonic in loudness there.
*/
class ColorStage
{
public:
    struct Params
    {
        SatType satType = SatType::softClip;
        float driveDb = 0.0f;
        float width = 1.0f;      // 0..2
        float loCutHz = 20.0f;
        float hiCutHz = 20000.0f;
        bool  limiter = true;
    };

    void prepare (int numChannels, double sampleRate, int maxBlockSize);
    void reset();

    void setParams (const Params& p) noexcept { params = p; }

    void process (juce::AudioBuffer<float>& buffer, int numSamples);

private:
    static float saturate (float x, SatType type) noexcept;

    Params params;

    struct ChannelState
    {
        float loCutState = 0.0f;
        float hiCutState = 0.0f;
        float dcState = 0.0f;
    };

    std::vector<ChannelState> channels;

    double sampleRate = 44100.0;

    // Limiter state, stereo-linked so it cannot shift the image.
    float limiterEnv = 0.0f;
    float limiterGain = 1.0f;

    juce::SmoothedValue<float> driveSmoothed;
    juce::SmoothedValue<float> widthSmoothed;
};

} // namespace vbd::dsp
