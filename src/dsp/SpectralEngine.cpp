#include "SpectralEngine.h"
#include "Utils.h"

namespace vbd::dsp
{

namespace
{
    constexpr float kMagFloor = 1.0e-9f;

    // Envelope-ratio limits for Vocode, i.e. +/-24 dB of correction.
    constexpr float kMinEnvRatio = 1.0f / 16.0f;
    constexpr float kMaxEnvRatio = 16.0f;
}

void SpectralEngine::prepare (int numChannels, int maxFftSize, double sr)
{
    sampleRate = sr;

    const auto maxBins = maxFftSize / 2 + 1;

    channels.assign (static_cast<std::size_t> (juce::jmax (1, numChannels)), {});

    for (auto& st : channels)
    {
        st.modMag.assign       (static_cast<std::size_t> (maxBins), 0.0f);
        st.carMag.assign       (static_cast<std::size_t> (maxBins), 0.0f);
        st.modEnv.assign       (static_cast<std::size_t> (maxBins), 0.0f);
        st.carEnv.assign       (static_cast<std::size_t> (maxBins), 0.0f);
        st.warpEnv.assign      (static_cast<std::size_t> (maxBins), 0.0f);
        st.frozenMag.assign    (static_cast<std::size_t> (maxBins), 0.0f);
        st.shiftedRe.assign    (static_cast<std::size_t> (maxBins), 0.0f);
        st.shiftedIm.assign    (static_cast<std::size_t> (maxBins), 0.0f);
        st.prevModPhase.assign (static_cast<std::size_t> (maxBins), 0.0f);
        st.prevCarPhase.assign (static_cast<std::size_t> (maxBins), 0.0f);
        st.outPhase.assign     (static_cast<std::size_t> (maxBins), 0.0f);
    }

    tiltGain.assign (static_cast<std::size_t> (maxBins), 1.0f);
    envelope.prepare (maxBins, maxFftSize);

    reset();
}

void SpectralEngine::reset()
{
    for (auto& st : channels)
    {
        std::fill (st.frozenMag.begin(),    st.frozenMag.end(),    0.0f);
        std::fill (st.prevModPhase.begin(), st.prevModPhase.end(), 0.0f);
        std::fill (st.prevCarPhase.begin(), st.prevCarPhase.end(), 0.0f);
        std::fill (st.outPhase.begin(),     st.outPhase.end(),     0.0f);
        st.hasFrozen  = false;
        st.phaseValid = false;
    }

    cachedTilt = std::numeric_limits<float>::quiet_NaN();
}

void SpectralEngine::setFftSize (int newFftSize, const juce::dsp::FFT* newFft, float newMagScale)
{
    fftSize  = newFftSize;
    fft      = newFft;
    magScale = newMagScale;

    cachedTilt = std::numeric_limits<float>::quiet_NaN();
    reset();
}

void SpectralEngine::buildTilt (int numBins)
{
    if (cachedTiltBins == numBins && juce::exactlyEqual (cachedTilt, params.tilt))
        return;

    cachedTilt = params.tilt;
    cachedTiltBins = numBins;

    const auto binHz = static_cast<float> (sampleRate) / static_cast<float> (fftSize);
    constexpr auto pivotHz = 1000.0f;

    for (int k = 0; k < numBins; ++k)
    {
        const auto f = juce::jmax (binHz * static_cast<float> (k), 1.0f);
        const auto octavesFromPivot = std::log2 (f / pivotHz);
        tiltGain[static_cast<std::size_t> (k)] = dbToGain (params.tilt * octavesFromPivot);
    }
}

void SpectralEngine::applyFreqShift (const float* carrier, ChannelState& st, int numBins)
{
    const auto binHz = static_cast<float> (sampleRate) / static_cast<float> (fftSize);

    // A bin shift is a true (inharmonic) frequency shift. It quantises to the bin width,
    // which is sampleRate/fftSize -- coarse at small FFT sizes. Documented behaviour, not
    // a pitch shift.
    const auto shift = static_cast<int> (std::lround (params.freqShiftHz / binHz));

    if (shift == 0)
    {
        for (int k = 0; k < numBins; ++k)
        {
            st.shiftedRe[static_cast<std::size_t> (k)] = carrier[2 * k];
            st.shiftedIm[static_cast<std::size_t> (k)] = carrier[2 * k + 1];
        }

        return;
    }

    for (int k = 0; k < numBins; ++k)
    {
        const auto src = k - shift;

        if (src >= 0 && src < numBins)
        {
            st.shiftedRe[static_cast<std::size_t> (k)] = carrier[2 * src];
            st.shiftedIm[static_cast<std::size_t> (k)] = carrier[2 * src + 1];
        }
        else
        {
            st.shiftedRe[static_cast<std::size_t> (k)] = 0.0f;
            st.shiftedIm[static_cast<std::size_t> (k)] = 0.0f;
        }
    }
}

void SpectralEngine::processFrame (const float* modulator,
                                   const float* carrier,
                                   float* output,
                                   int numBins,
                                   int channel)
{
    auto& st = channels[static_cast<std::size_t> (channel)];

    // Flip swaps the roles of the two streams.
    const float* modSrc = params.flip ? carrier   : modulator;
    const float* carSrcRaw = params.flip ? modulator : carrier;

    applyFreqShift (carSrcRaw, st, numBins);

    const auto* carRe = st.shiftedRe.data();
    const auto* carIm = st.shiftedIm.data();

    // Magnitudes. Always needed; phases only for Phase Morph.
    for (int k = 0; k < numBins; ++k)
    {
        const auto mr = modSrc[2 * k];
        const auto mi = modSrc[2 * k + 1];
        st.modMag[static_cast<std::size_t> (k)] = std::sqrt (mr * mr + mi * mi);

        const auto cr = carRe[k];
        const auto ci = carIm[k];
        st.carMag[static_cast<std::size_t> (k)] = std::sqrt (cr * cr + ci * ci);
    }

    // Freeze holds the modulator's magnitude frame, so the imprint stops evolving while
    // the carrier keeps moving.
    if (params.freeze)
    {
        if (! st.hasFrozen)
        {
            std::copy (st.modMag.begin(), st.modMag.begin() + numBins, st.frozenMag.begin());
            st.hasFrozen = true;
        }

        std::copy (st.frozenMag.begin(), st.frozenMag.begin() + numBins, st.modMag.begin());
    }
    else
    {
        st.hasFrozen = false;
    }

    buildTilt (numBins);

    const auto gateThreshold = dbToGain (params.gateDb) / juce::jmax (magScale, 1.0e-9f);
    const auto morph = juce::jlimit (0.0f, 1.0f, params.morph);

    // Envelopes are only needed by Vocode; skipping them elsewhere is most of the reason
    // the other modes are cheap.
    if (params.mode == EngineMode::vocode)
    {
        envelope.compute (st.modMag.data(), st.modEnv.data(), numBins, fftSize,
                          params.envRes, params.cepstral, fft);
        envelope.compute (st.carMag.data(), st.carEnv.data(), numBins, fftSize,
                          params.envRes, params.cepstral, fft);

        // Formant shift resamples the modulator envelope along the frequency axis.
        const auto ratio = std::pow (2.0f, params.formantShift / 12.0f);

        for (int k = 0; k < numBins; ++k)
        {
            const auto srcPos = static_cast<float> (k) / ratio;
            const auto i0 = static_cast<int> (srcPos);
            const auto frac = srcPos - static_cast<float> (i0);

            if (i0 >= 0 && i0 + 1 < numBins)
                st.warpEnv[static_cast<std::size_t> (k)] =
                    st.modEnv[static_cast<std::size_t> (i0)] * (1.0f - frac)
                  + st.modEnv[static_cast<std::size_t> (i0 + 1)] * frac;
            else if (i0 >= 0 && i0 < numBins)
                st.warpEnv[static_cast<std::size_t> (k)] = st.modEnv[static_cast<std::size_t> (i0)];
            else
                st.warpEnv[static_cast<std::size_t> (k)] = 0.0f;
        }
    }

    const auto needsPhase = (params.mode == EngineMode::phaseMorph);

    for (int k = 0; k < numBins; ++k)
    {
        const auto ku = static_cast<std::size_t> (k);
        const auto modMag = st.modMag[ku];
        const auto carMag = st.carMag[ku];

        float outRe = 0.0f;
        float outIm = 0.0f;

        // Gate: below the sensitivity threshold the modulator is treated as silence, which
        // is what keeps the carrier from droning through the gaps in a bass growl.
        const auto gateOpen = modMag > gateThreshold;

        if (! gateOpen)
        {
            output[2 * k]     = 0.0f;
            output[2 * k + 1] = 0.0f;

            if (needsPhase)
            {
                st.prevModPhase[ku] = std::atan2 (modSrc[2 * k + 1], modSrc[2 * k]);
                st.prevCarPhase[ku] = std::atan2 (carIm[k], carRe[k]);
            }

            continue;
        }

        switch (params.mode)
        {
            case EngineMode::vocode:
            {
                // Envelope ratio, morphed against unity so Morph sweeps from "carrier
                // untouched" to "fully imprinted".
                const auto me = st.warpEnv[ku];
                const auto ce = juce::jmax (st.carEnv[ku], kMagFloor);

                // +/-24 dB of envelope correction is already generous for a vocoder, and
                // the per-frame energy clamp below catches whatever this lets through.
                const auto ratio = juce::jlimit (kMinEnvRatio, kMaxEnvRatio, me / ce);
                const auto gain = 1.0f + morph * (ratio - 1.0f);

                outRe = carRe[k] * gain;
                outIm = carIm[k] * gain;
                break;
            }

            case EngineMode::magMorph:
            {
                // Pure magnitude crossfade, carrier phase retained -> a complex scale.
                const auto target = carMag + morph * (modMag - carMag);
                const auto gain = target / juce::jmax (carMag, kMagFloor);

                outRe = carRe[k] * gain;
                outIm = carIm[k] * gain;
                break;
            }

            case EngineMode::cross:
            {
                // Magnitude from the modulator, phase from the carrier.
                const auto gain = modMag / juce::jmax (carMag, kMagFloor);

                outRe = carRe[k] * gain;
                outIm = carIm[k] * gain;
                break;
            }

            case EngineMode::phaseMorph:
            {
                const auto modPhase = std::atan2 (modSrc[2 * k + 1], modSrc[2 * k]);
                const auto carPhase = std::atan2 (carIm[k], carRe[k]);

                // Blend instantaneous frequency (the per-frame phase increment), then
                // integrate. This is the phase-vocoder-correct way to interpolate phase.
                const auto modDelta = wrapPhase (modPhase - st.prevModPhase[ku]);
                const auto carDelta = wrapPhase (carPhase - st.prevCarPhase[ku]);

                const auto blendedDelta = carDelta + morph * (modDelta - carDelta);

                if (! st.phaseValid)
                    st.outPhase[ku] = carPhase;

                const auto integrated = wrapPhase (st.outPhase[ku] + blendedDelta);

                // Phase Lock crossfades the integrated gradient against a direct blend on
                // the unit circle: tighter transients, less fluid motion.
                const auto lockRe = std::cos (carPhase) + morph * (std::cos (modPhase) - std::cos (carPhase));
                const auto lockIm = std::sin (carPhase) + morph * (std::sin (modPhase) - std::sin (carPhase));
                const auto locked = std::atan2 (lockIm, lockRe);

                const auto lock = juce::jlimit (0.0f, 1.0f, params.phaseLock);
                const auto finalPhase = integrated + lock * wrapPhase (locked - integrated);

                st.outPhase[ku] = finalPhase;
                st.prevModPhase[ku] = modPhase;
                st.prevCarPhase[ku] = carPhase;

                // Modulator magnitudes, as specified for this mode.
                outRe = modMag * std::cos (finalPhase);
                outIm = modMag * std::sin (finalPhase);
                break;
            }
        }

        const auto tilt = tiltGain[ku];

        output[2 * k]     = scrub (outRe * tilt);
        output[2 * k + 1] = scrub (outIm * tilt);
    }

    if (needsPhase)
        st.phaseValid = true;

    // ---- Vocode energy invariant.
    //
    // Vocode's per-bin gain is an envelope *ratio*, so unlike the other three modes its
    // output magnitude is not inherently bounded by the inputs. That bites hardest with
    // the cepstral backend: a cepstral envelope is a geometric mean, which on a sparse
    // spectrum (a sine carrier, say) sits far below the actual peak, so dividing by it
    // asks for enormous gain exactly where the carrier is loudest.
    //
    // The fix is an invariant rather than another magic clamp: a vocoder's job is to
    // reshape the carrier to follow the modulator, so the frame may never carry more
    // energy than the modulator brought in. Attenuate-only, so quiet passages are left
    // alone and nothing is ever boosted.
    if (params.mode == EngineMode::vocode)
    {
        double modEnergy = 0.0;
        double outEnergy = 0.0;

        for (int k = 0; k < numBins; ++k)
        {
            const auto m = static_cast<double> (st.modMag[static_cast<std::size_t> (k)]);
            modEnergy += m * m;

            const auto re = static_cast<double> (output[2 * k]);
            const auto im = static_cast<double> (output[2 * k + 1]);
            outEnergy += re * re + im * im;
        }

        if (outEnergy > modEnergy && outEnergy > 1.0e-20)
        {
            const auto scale = static_cast<float> (std::sqrt (modEnergy / outEnergy));

            for (int k = 0; k < numBins; ++k)
            {
                output[2 * k]     *= scale;
                output[2 * k + 1] *= scale;
            }
        }
    }
}

} // namespace vbd::dsp
