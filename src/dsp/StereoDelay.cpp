#include "StereoDelay.h"
#include "Utils.h"

#include "../Params.h"

namespace vbd::dsp
{

namespace
{
    // Allpass lengths in milliseconds. Mutually prime-ish so the diffuser smears rather
    // than ringing at one pitch.
    constexpr double kDiffuserMs[4] = { 7.3, 11.9, 17.1, 23.7 };

    constexpr float kMaxModMs = 6.0f;
    constexpr float kHardClamp = 4.0f;

    /** One-pole coefficient for a given cutoff. */
    float onePoleCoeff (float hz, double sr) noexcept
    {
        const auto x = std::exp (-2.0 * juce::MathConstants<double>::pi
                                 * static_cast<double> (hz) / juce::jmax (1.0, sr));
        return juce::jlimit (0.0f, 1.0f, 1.0f - static_cast<float> (x));
    }
}

void StereoDelay::prepare (int numChannels, double sr, int maxBlockSize)
{
    sampleRate = sr;

    // Sized for the longest delay plus the deepest modulation, at this rate.
    lineLength = static_cast<int> (std::ceil (static_cast<double> (maxDelayMs + kMaxModMs + 10.0f)
                                              * 0.001 * sr)) + 4;

    channels.assign (static_cast<std::size_t> (juce::jmax (1, numChannels)), {});

    for (auto& ch : channels)
    {
        ch.line.assign (static_cast<std::size_t> (lineLength), 0.0f);

        for (int i = 0; i < 4; ++i)
            ch.diffusers[static_cast<std::size_t> (i)].prepare (
                static_cast<int> (kDiffuserMs[i] * 0.001 * sr));
    }

    dryCopy.setSize (juce::jmax (1, numChannels), juce::jmax (1, maxBlockSize), false, false, true);

    reset();
}

void StereoDelay::reset()
{
    for (auto& ch : channels)
    {
        std::fill (ch.line.begin(), ch.line.end(), 0.0f);
        ch.writePos = 0;
        ch.hpState = 0.0f;
        ch.lpState = 0.0f;
        ch.smoothedDelay = 0.0;

        for (auto& ap : ch.diffusers)
            ap.clear();
    }

    modPhase = 0.0;
    duckEnv = 0.0f;
    running = false;
    needsFlush = false;
    fade = 0.0f;
}

void StereoDelay::setTransport (double newBpm) noexcept
{
    if (newBpm > 1.0 && newBpm < 1000.0)
        bpm = newBpm;
}

double StereoDelay::tailSeconds() const noexcept
{
    return static_cast<double> (maxDelayMs) * 0.001 * 8.0;
}

double StereoDelay::delaySamplesFor (int channel) const noexcept
{
    double ms;

    if (params.sync)
    {
        const auto div = (channel == 0 || params.mode != DelayMode::dual)
                           ? params.divL : params.divR;
        const auto quarters = syncDivToQuarterNotes (div);
        ms = quarters * (60.0 / juce::jmax (1.0, bpm)) * 1000.0;
    }
    else
    {
        ms = static_cast<double> ((channel == 0 || params.mode != DelayMode::dual)
                                    ? params.msL : params.msR);
    }

    // Ping-pong runs both lines at the same time so the bounce is even.
    ms = juce::jlimit (1.0, static_cast<double> (maxDelayMs), ms);

    return ms * 0.001 * sampleRate;
}

float StereoDelay::readInterpolated (const ChannelState& ch, double delaySamples) const noexcept
{
    delaySamples = juce::jlimit (1.0, static_cast<double> (lineLength - 2), delaySamples);

    auto readPos = static_cast<double> (ch.writePos) - delaySamples;

    while (readPos < 0.0)
        readPos += static_cast<double> (lineLength);

    const auto i0 = static_cast<int> (readPos);
    const auto frac = static_cast<float> (readPos - static_cast<double> (i0));
    const auto i1 = (i0 + 1) % lineLength;

    const auto a = ch.line[static_cast<std::size_t> (i0 % lineLength)];
    const auto b = ch.line[static_cast<std::size_t> (i1)];

    return a + (b - a) * frac;
}

float StereoDelay::loopProcess (ChannelState& ch, float x, float hpCoeff, float lpCoeff,
                                float satDrive, float satMakeup, float diffuseG) noexcept
{
    // High-pass: subtract the tracked low content, so repeats do not pile up bass.
    ch.hpState += hpCoeff * (x - ch.hpState);
    auto y = x - ch.hpState;

    // Low-pass: the darkening that makes successive repeats recede.
    ch.lpState += lpCoeff * (y - ch.lpState);
    y = ch.lpState;

    // Diffusion: a short allpass chain, which smears each repeat into a wash rather than
    // a discrete copy.
    if (diffuseG > 0.0f)
        for (auto& ap : ch.diffusers)
            y = ap.process (y, diffuseG);

    // Unconditional soft saturation. Small-signal gain is 1 and the output is bounded by
    // 1/satDrive for any input, which is what makes the loop unable to diverge.
    y = std::tanh (y * satDrive) / satDrive * satMakeup;

    // Backstop against anything pathological reaching the line.
    return juce::jlimit (-kHardClamp, kHardClamp, scrub (y));
}

void StereoDelay::process (juce::AudioBuffer<float>& buffer, int numSamples)
{
    const auto numChannels = juce::jmin (buffer.getNumChannels(),
                                         static_cast<int> (channels.size()));

    if (numChannels <= 0 || numSamples <= 0)
        return;

    // ---- enable/disable, with a crossfade so toggling never clicks
    const auto fadeSeconds = 0.02;
    fadeStep = static_cast<float> (1.0 / juce::jmax (1.0, sampleRate * fadeSeconds));

    if (params.on && ! running)
    {
        reset();
        running = true;
        fade = 0.0f;
    }

    if (! params.on && ! running)
    {
        // Fully off: do nothing at all. This is the "zero CPU when off" path.
        if (needsFlush)
        {
            reset();
            needsFlush = false;
        }

        return;
    }

    const auto fadeTarget = params.on ? 1.0f : 0.0f;

    // ---- coefficients, once per block
    const auto hpCoeff = onePoleCoeff (juce::jlimit (20.0f, 2000.0f, params.hpHz), sampleRate);
    const auto lpCoeff = onePoleCoeff (juce::jlimit (200.0f, 20000.0f, params.lpHz), sampleRate);

    const auto sat = juce::jlimit (0.0f, 1.0f, params.saturation);
    const auto satDrive = 0.5f + sat * 3.5f;
    const auto satMakeup = 1.0f + sat * 0.8f;

    const auto diffuseG = juce::jlimit (0.0f, 0.75f, params.diffusion * 0.75f);

    // Freeze holds unity feedback; bounded because the loop saturator converges.
    const auto feedback = params.freeze ? 1.0f : juce::jlimit (0.0f, 0.98f, params.feedback);

    const auto duckAmount = juce::jlimit (0.0f, 1.0f, params.ducking);
    const auto duckCoeff = onePoleCoeff (12.0f, sampleRate);

    const auto modDepthSamples = static_cast<double> (juce::jlimit (0.0f, 1.0f, params.modDepth))
                               * static_cast<double> (kMaxModMs) * 0.001 * sampleRate;
    const auto modInc = juce::jlimit (0.0, 10.0, static_cast<double> (params.modRateHz))
                      / juce::jmax (1.0, sampleRate);

    // Delay time is smoothed: stepping it would pitch-shift the tail audibly.
    const auto delayCoeff = onePoleCoeff (4.0f, sampleRate);

    const auto width = juce::jlimit (0.0f, 2.0f, params.width);
    const auto mix = juce::jlimit (0.0f, 1.0f, params.mix);

    // ---- keep a dry copy for the mix and the ducking detector
    for (int ch = 0; ch < numChannels; ++ch)
        dryCopy.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    const auto stereo = numChannels >= 2;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = stereo ? buffer.getWritePointer (1) : nullptr;
    const auto* dryL = dryCopy.getReadPointer (0);
    const auto* dryR = stereo ? dryCopy.getReadPointer (1) : dryL;

    auto& chL = channels[0];
    auto* chRptr = stereo ? &channels[1] : nullptr;

    const auto targetL = delaySamplesFor (0);
    const auto targetR = delaySamplesFor (1);

    if (chL.smoothedDelay <= 0.0) chL.smoothedDelay = targetL;
    if (chRptr != nullptr && chRptr->smoothedDelay <= 0.0) chRptr->smoothedDelay = targetR;

    for (int i = 0; i < numSamples; ++i)
    {
        // Fade towards the enabled state.
        fade += juce::jlimit (-fadeStep, fadeStep, fadeTarget - fade);
        fade = juce::jlimit (0.0f, 1.0f, fade);

        // Ducking detector follows the input, not the output, so repeats duck under the
        // part being played rather than under themselves.
        const auto inLevel = 0.5f * (std::abs (dryL[i]) + std::abs (dryR[i]));
        duckEnv += duckCoeff * (inLevel - duckEnv);
        const auto duckGain = 1.0f - duckAmount * juce::jlimit (0.0f, 1.0f, duckEnv * 4.0f);

        // Modulated delay times.
        modPhase += modInc;

        if (modPhase >= 1.0)
            modPhase -= 1.0;

        const auto lfoL = std::sin (juce::MathConstants<double>::twoPi * modPhase);
        const auto lfoR = std::sin (juce::MathConstants<double>::twoPi * (modPhase + 0.25));

        chL.smoothedDelay += static_cast<double> (delayCoeff) * (targetL - chL.smoothedDelay);

        const auto readL = readInterpolated (chL, chL.smoothedDelay + lfoL * modDepthSamples);

        float readR = 0.0f;

        if (chRptr != nullptr)
        {
            chRptr->smoothedDelay += static_cast<double> (delayCoeff) * (targetR - chRptr->smoothedDelay);
            readR = readInterpolated (*chRptr, chRptr->smoothedDelay + lfoR * modDepthSamples);
        }

        const auto inL = params.freeze ? 0.0f : dryL[i];
        const auto inR = params.freeze ? 0.0f : dryR[i];

        float writeL, writeR = 0.0f;

        if (params.mode == DelayMode::pingPong && chRptr != nullptr)
        {
            // Two lines in series: input enters left, left feeds right, right feeds back
            // to left. One feedback multiplication per round trip, so echoes alternate.
            const auto inMono = 0.5f * (inL + inR);
            writeL = inMono + feedback * loopProcess (chL, readR, hpCoeff, lpCoeff,
                                                      satDrive, satMakeup, diffuseG);
            writeR = loopProcess (*chRptr, readL, hpCoeff, lpCoeff,
                                  satDrive, satMakeup, diffuseG);
        }
        else
        {
            writeL = inL + feedback * loopProcess (chL, readL, hpCoeff, lpCoeff,
                                                   satDrive, satMakeup, diffuseG);

            if (chRptr != nullptr)
                writeR = inR + feedback * loopProcess (*chRptr, readR, hpCoeff, lpCoeff,
                                                       satDrive, satMakeup, diffuseG);
        }

        chL.line[static_cast<std::size_t> (chL.writePos)] =
            juce::jlimit (-kHardClamp, kHardClamp, scrub (writeL));

        if (++chL.writePos >= lineLength)
            chL.writePos = 0;

        if (chRptr != nullptr)
        {
            chRptr->line[static_cast<std::size_t> (chRptr->writePos)] =
                juce::jlimit (-kHardClamp, kHardClamp, scrub (writeR));

            if (++chRptr->writePos >= lineLength)
                chRptr->writePos = 0;
        }

        // ---- wet signal, width, ducking, mix
        auto wetL = readL;
        auto wetR = chRptr != nullptr ? readR : readL;

        if (chRptr != nullptr && ! juce::approximatelyEqual (width, 1.0f))
        {
            const auto mid = 0.5f * (wetL + wetR);
            const auto side = 0.5f * (wetL - wetR) * width;
            wetL = mid + side;
            wetR = mid - side;
        }

        const auto wetGain = mix * fade * duckGain;

        outL[i] = scrub (dryL[i] * (1.0f - mix * fade) + wetL * wetGain);

        if (outR != nullptr)
            outR[i] = scrub (dryR[i] * (1.0f - mix * fade) + wetR * wetGain);
    }

    // Once faded out, stop running and schedule the flush.
    if (! params.on && fade <= 0.0f)
    {
        running = false;
        needsFlush = true;
    }
}

} // namespace vbd::dsp
