#include "ColorStage.h"
#include "Utils.h"

namespace vbd::dsp
{

namespace
{
    constexpr float kLimiterThreshold = 0.98f;   // just under full scale
    constexpr float kHardClamp = 4.0f;

    float onePoleCoeff (float hz, double sr) noexcept
    {
        const auto x = std::exp (-2.0 * juce::MathConstants<double>::pi
                                 * static_cast<double> (hz) / juce::jmax (1.0, sr));
        return juce::jlimit (0.0f, 1.0f, 1.0f - static_cast<float> (x));
    }
}

void ColorStage::prepare (int numChannels, double sr, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);

    sampleRate = sr;
    channels.assign (static_cast<std::size_t> (juce::jmax (1, numChannels)), {});

    driveSmoothed.reset (sr, 0.02);
    widthSmoothed.reset (sr, 0.02);
    driveSmoothed.setCurrentAndTargetValue (dbToGain (params.driveDb));
    widthSmoothed.setCurrentAndTargetValue (params.width);

    reset();
}

void ColorStage::reset()
{
    for (auto& ch : channels)
        ch = {};

    limiterEnv = 0.0f;
    limiterGain = 1.0f;
}

float ColorStage::saturate (float x, SatType type) noexcept
{
    switch (type)
    {
        case SatType::softClip:
            return std::tanh (x);

        case SatType::tube:
        {
            // Asymmetric: a touch of even-harmonic content, then bounded by tanh.
            const auto biased = x + 0.15f * x * x;
            return std::tanh (biased * 0.9f);
        }

        case SatType::wavefold:
        {
            // Triangle fold with period 4: exactly linear inside [-1, 1], and beyond that
            // it reflects instead of clipping. Periodic, so pushing harder does not simply
            // get louder -- that reciprocating character is the point.
            const auto p = x + 1.0f;
            const auto q = p - 4.0f * std::floor (p * 0.25f);     // [0, 4)
            const auto t = q < 2.0f ? q : 4.0f - q;               // [0, 2]
            return t - 1.0f;                                       // [-1, 1]
        }
    }

    return std::tanh (x);
}

void ColorStage::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto numChannels = juce::jmin (buffer.getNumChannels(),
                                         static_cast<int> (channels.size()));

    if (numChannels <= 0 || numSamples <= 0)
        return;

    driveSmoothed.setTargetValue (dbToGain (params.driveDb));
    widthSmoothed.setTargetValue (juce::jlimit (0.0f, 2.0f, params.width));

    const auto loCoeff = onePoleCoeff (juce::jlimit (10.0f, 2000.0f, params.loCutHz), sampleRate);
    const auto hiCoeff = onePoleCoeff (juce::jlimit (200.0f, 21000.0f, params.hiCutHz), sampleRate);
    const auto dcCoeff = onePoleCoeff (5.0f, sampleRate);

    const auto driveActive = params.driveDb > 0.01f;

    // Parked at the extremes means bypassed, so default settings are truly transparent.
    const auto loCutActive = params.loCutHz > 20.5f;
    const auto hiCutActive = params.hiCutHz < 19900.0f;

    // Makeup roughly compensates the level the drive adds, so Drive changes character
    // more than loudness.
    const auto makeup = driveActive ? 1.0f / std::sqrt (dbToGain (params.driveDb)) : 1.0f;

    const auto limiterAttack = onePoleCoeff (2000.0f, sampleRate);   // ~0.08 ms
    const auto limiterRelease = onePoleCoeff (20.0f, sampleRate);    // ~8 ms

    auto* left = buffer.getWritePointer (0);
    auto* right = numChannels > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto drive = driveSmoothed.getNextValue();
        const auto width = widthSmoothed.getNextValue();

        float l = left[i];
        float r = right != nullptr ? right[i] : l;

        // ---- saturation
        if (driveActive)
        {
            l = saturate (l * drive, params.satType) * makeup;
            r = saturate (r * drive, params.satType) * makeup;
        }

        // ---- stereo width (mid/side)
        if (right != nullptr && ! juce::approximatelyEqual (width, 1.0f))
        {
            const auto mid = 0.5f * (l + r);
            const auto side = 0.5f * (l - r) * width;
            l = mid + side;
            r = mid - side;
        }

        // ---- low cut (one-pole high-pass) and high cut (one-pole low-pass)
        //
        // Both are skipped when parked at their extremes. A one-pole low-pass at 20 kHz
        // is a long way from transparent at a 48 kHz rate -- it would colour every
        // default-settings instance -- so "off" has to mean genuinely bypassed, not
        // "cutoff set very high". Cheaper too.
        auto& cl = channels[0];

        if (loCutActive)
        {
            cl.loCutState += loCoeff * (l - cl.loCutState);
            l -= cl.loCutState;
        }

        if (hiCutActive)
        {
            cl.hiCutState += hiCoeff * (l - cl.hiCutState);
            l = cl.hiCutState;
        }

        // DC blocker, only where it is needed: the asymmetric tube curve and the
        // wavefolder both introduce offset, but a clean path does not.
        if (driveActive)
        {
            cl.dcState += dcCoeff * (l - cl.dcState);
            l -= cl.dcState;
        }

        if (right != nullptr)
        {
            auto& cr = channels[1];

            if (loCutActive)
            {
                cr.loCutState += loCoeff * (r - cr.loCutState);
                r -= cr.loCutState;
            }

            if (hiCutActive)
            {
                cr.hiCutState += hiCoeff * (r - cr.hiCutState);
                r = cr.hiCutState;
            }

            if (driveActive)
            {
                cr.dcState += dcCoeff * (r - cr.dcState);
                r -= cr.dcState;
            }
        }

        // ---- safety limiter, stereo-linked
        if (params.limiter)
        {
            const auto peak = juce::jmax (std::abs (l), std::abs (r));
            const auto coeff = peak > limiterEnv ? limiterAttack : limiterRelease;
            limiterEnv += coeff * (peak - limiterEnv);

            const auto target = limiterEnv > kLimiterThreshold
                                  ? kLimiterThreshold / juce::jmax (limiterEnv, 1.0e-6f)
                                  : 1.0f;

            // Gain itself is smoothed, so the limiter cannot introduce its own clicks.
            limiterGain += (target < limiterGain ? limiterAttack : limiterRelease)
                         * (target - limiterGain);

            l *= limiterGain;
            r *= limiterGain;
        }

        left[i] = juce::jlimit (-kHardClamp, kHardClamp, scrub (l));

        if (right != nullptr)
            right[i] = juce::jlimit (-kHardClamp, kHardClamp, scrub (r));
    }

    // Channels past the second get the same treatment without the stereo matrix.
    for (int ch = 2; ch < numChannels; ++ch)
        scrubBlock (buffer.getWritePointer (ch), numSamples);
}

} // namespace vbd::dsp
