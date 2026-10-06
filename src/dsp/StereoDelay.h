#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <vector>

namespace vbd::dsp
{

enum class DelayMode { stereo = 0, pingPong, dual };
enum class DelayPlacement { pre = 0, post };

/**
    Tempo-syncable stereo delay with a bounded feedback loop.

    The loop contains, in order: a high-pass, a low-pass, a soft saturator, and an
    optional allpass diffuser. The saturator is **unconditionally** in circuit, not gated
    behind the Saturation control -- it is what makes the loop incapable of blowing up for
    any parameter combination, including Freeze. Saturation only sets how hard it is
    driven.

    Freeze mutes the input and holds feedback at unity. That is still bounded, because
    unity feedback through a saturator converges rather than diverging; it is not a gain
    above 1.

    Off by default, and when off nothing is traversed -- one branch per block.
*/
class StereoDelay
{
public:
    static constexpr float maxDelayMs = 4000.0f;

    struct Params
    {
        bool  on = false;
        DelayMode mode = DelayMode::pingPong;
        DelayPlacement placement = DelayPlacement::post;
        bool  sync = true;
        int   divL = 8;
        int   divR = 10;
        float msL = 375.0f;
        float msR = 500.0f;
        float feedback = 0.45f;   // 0..1
        float hpHz = 120.0f;
        float lpHz = 8000.0f;
        float saturation = 0.2f;  // 0..1
        float modRateHz = 0.4f;
        float modDepth = 0.15f;   // 0..1
        float diffusion = 0.25f;  // 0..1
        float ducking = 0.0f;     // 0..1
        bool  freeze = false;
        float width = 1.0f;       // 0..2
        float mix = 0.35f;        // 0..1
    };

    void prepare (int numChannels, double sampleRate, int maxBlockSize);
    void reset();

    void setParams (const Params& p) noexcept { params = p; }
    void setTransport (double bpm) noexcept;

    /** Processes in place, mixing wet into dry internally. Returns immediately when off. */
    void process (juce::AudioBuffer<float>& buffer, int numSamples);

    bool isRunning() const noexcept { return running; }

    /** Longest possible tail, for getTailLengthSeconds. */
    double tailSeconds() const noexcept;

private:
    struct Allpass
    {
        std::vector<float> buffer;
        int writePos = 0;
        int length = 0;

        void prepare (int len)
        {
            length = juce::jmax (1, len);
            buffer.assign (static_cast<std::size_t> (length), 0.0f);
            writePos = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); writePos = 0; }

        float process (float x, float g) noexcept
        {
            auto& slot = buffer[static_cast<std::size_t> (writePos)];
            const auto delayed = slot;
            const auto y = -g * x + delayed;
            slot = x + g * y;

            if (++writePos >= length)
                writePos = 0;

            return y;
        }
    };

    struct ChannelState
    {
        std::vector<float> line;
        int writePos = 0;
        float hpState = 0.0f;
        float lpState = 0.0f;
        std::array<Allpass, 4> diffusers;
        double smoothedDelay = 0.0;
    };

    float readInterpolated (const ChannelState& ch, double delaySamples) const noexcept;
    float loopProcess (ChannelState& ch, float x, float hpCoeff, float lpCoeff,
                       float satDrive, float satMakeup, float diffuseG) noexcept;
    double delaySamplesFor (int channel) const noexcept;

    Params params;

    std::vector<ChannelState> channels;
    juce::AudioBuffer<float> dryCopy;

    double sampleRate = 44100.0;
    int    lineLength = 0;
    double bpm = 120.0;

    double modPhase = 0.0;
    float  duckEnv = 0.0f;

    bool running = false;
    bool needsFlush = false;
    float fade = 0.0f;
    float fadeStep = 0.001f;
};

} // namespace vbd::dsp
