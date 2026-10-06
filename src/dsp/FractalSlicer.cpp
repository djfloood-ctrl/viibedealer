#include "FractalSlicer.h"
#include "StftEngine.h"
#include "Utils.h"

#include "../Params.h"

namespace vbd::dsp
{

void FractalSlicer::prepare (int numChannels, int maxFftSize, double sr)
{
    sampleRate = sr;

    const auto maxBins = maxFftSize / 2 + 1;

    targetGain.assign (static_cast<std::size_t> (maxBins), 1.0f);
    sourceBin.assign (static_cast<std::size_t> (maxBins), 0);
    energies.assign (static_cast<std::size_t> (BandTable::maxBands), 0.0f);

    channels.assign (static_cast<std::size_t> (juce::jmax (1, numChannels)), {});

    for (auto& ch : channels)
    {
        ch.smoothed.assign (static_cast<std::size_t> (maxBins), 1.0f);
        ch.scratch.assign (static_cast<std::size_t> (2 * maxBins), 0.0f);
    }

    reset();
}

void FractalSlicer::reset()
{
    for (auto& ch : channels)
    {
        std::fill (ch.smoothed.begin(), ch.smoothed.end(), 1.0f);
        std::fill (ch.scratch.begin(), ch.scratch.end(), 0.0f);
    }

    std::fill (energies.begin(), energies.end(), 0.0f);

    phase = 0.0;
    rotationOffset = 0;
    rhythmStepIndex = 0;
    haveApplied = false;
    cachedBins = 0;
}

void FractalSlicer::setFftSize (int newFftSize, double sr)
{
    fftSize = newFftSize;
    sampleRate = sr;
    haveApplied = false;   // band edges are in bins, so the table must be rebuilt
    cachedBins = 0;
    reset();
}

void FractalSlicer::setTransport (double newBpm, bool playing) noexcept
{
    if (newBpm > 1.0 && newBpm < 1000.0)
        bpm = newBpm;

    transportPlaying = playing;
}

void FractalSlicer::rebuildIfNeeded (int numBins)
{
    const auto changed = ! haveApplied
        || cachedBins != numBins
        || pending.type != applied.type
        || pending.depth != applied.depth
        || pending.seed != applied.seed
        || ! juce::approximatelyEqual (pending.ratio, applied.ratio)
        || ! juce::approximatelyEqual (pending.asym, applied.asym)
        || ! juce::approximatelyEqual (pending.loHz, applied.loHz)
        || ! juce::approximatelyEqual (pending.hiHz, applied.hiHz);

    params = pending;

    if (! changed)
        return;

    const auto binHz = static_cast<float> (sampleRate) / static_cast<float> (fftSize);

    auto lo = static_cast<int> (std::floor (params.loHz / binHz));
    auto hi = static_cast<int> (std::ceil  (params.hiHz / binHz));

    // Bin 0 is DC: gating it produces a lurching offset rather than a musical effect.
    lo = juce::jlimit (1, numBins - 1, lo);
    hi = juce::jlimit (lo + 1, numBins, hi);

    FractalPatternParams pp;
    pp.type  = params.type;
    pp.depth = params.depth;
    pp.seed  = params.seed;
    pp.ratio = params.ratio;
    pp.asym  = params.asym;
    pp.loBin = lo;
    pp.hiBin = hi;

    buildBandTable (pp, table);

    applied = params;
    haveApplied = true;
    cachedBins = numBins;
}

void FractalSlicer::buildGainMap (int numBins)
{
    // Outside the selected range the slicer is transparent.
    std::fill (targetGain.begin(), targetGain.begin() + numBins, 1.0f);

    for (int k = 0; k < numBins; ++k)
        sourceBin[static_cast<std::size_t> (k)] = k;

    anyShift = false;

    if (table.numBands <= 0)
        return;

    const auto shatter = juce::jlimit (0.0f, 1.0f, params.shatter);
    const auto grit    = juce::jlimit (0.0f, 1.0f, params.grit);
    const auto gate    = juce::jlimit (0.0f, 1.0f, params.gate);

    // Bin shifting only engages in the upper half of Shatter.
    const auto shiftAmount = juce::jmax (0.0f, (shatter - 0.5f) * 2.0f);

    const auto effDepth = juce::jmax (1, table.effectiveDepth);

    for (int i = 0; i < table.numBands; ++i)
    {
        // Rotation slides the pattern across fixed edges.
        auto patternIndex = i + rotationOffset;
        patternIndex = ((patternIndex % table.numBands) + table.numBands) % table.numBands;

        const auto& geom = table.bands[static_cast<std::size_t> (i)];
        const auto& rule = table.bands[static_cast<std::size_t> (patternIndex)];

        auto isActive = rule.active;

        if (params.invert)
            isActive = ! isActive;

        float bandGain;

        if (isActive)
        {
            // Multiplicative cascade weight, faded in by Shatter so 0 is transparent.
            bandGain = 1.0f + shatter * (rule.weight - 1.0f);
        }
        else
        {
            bandGain = 1.0f - shatter;
        }

        // Rhythm gate: the same sequence, sampled along time.
        if (gate > 0.0f)
        {
            const auto step = rhythmStepIndex + i;

            if (! patternStep (params.type, params.seed, table.numBands, step))
                bandGain *= (1.0f - gate);
        }

        const auto width = geom.hiBin - geom.loBin;

        if (width <= 0)
            continue;

        // Raised-cosine edge taper, narrowed by Grit. Without this, hard bin edges click
        // and ring; with Grit at 1 the edges are deliberately hard.
        auto taper = static_cast<int> (std::lround (static_cast<double> (juce::jmin (width / 4, 8))
                                                    * (1.0 - static_cast<double> (grit))));
        taper = juce::jlimit (0, juce::jmax (0, width / 2 - 1), taper);

        for (int k = geom.loBin; k < geom.hiBin && k < numBins; ++k)
        {
            float edge = 1.0f;

            if (taper > 0)
            {
                const auto fromLo = k - geom.loBin;
                const auto fromHi = geom.hiBin - 1 - k;
                const auto dist = juce::jmin (fromLo, fromHi);

                if (dist < taper)
                {
                    const auto t = (static_cast<float> (dist) + 0.5f) / static_cast<float> (taper);
                    edge = 0.5f - 0.5f * std::cos (t * juce::MathConstants<float>::pi);
                }
            }

            // Blend the band's gain towards unity across the taper, so neighbouring bands
            // cross-fade into each other instead of stepping.
            targetGain[static_cast<std::size_t> (k)] = 1.0f + (bandGain - 1.0f) * edge;
        }

        if (shiftAmount > 0.0f && geom.level > 0)
        {
            const auto shift = static_cast<int> (std::lround (
                static_cast<double> (shiftAmount) * static_cast<double> (geom.level)
                * static_cast<double> (effDepth) * 0.5));

            if (shift != 0)
            {
                anyShift = true;

                for (int k = geom.loBin; k < geom.hiBin && k < numBins; ++k)
                {
                    const auto src = k - shift;
                    sourceBin[static_cast<std::size_t> (k)] =
                        (src >= 1 && src < numBins) ? src : -1;
                }
            }
        }
    }
}

void FractalSlicer::beginFrame (int hopSamples, int numBins)
{
    rebuildIfNeeded (numBins);

    // Animation rate: tempo-synced from the host's BPM, or free-running in Hz.
    double cyclesPerSecond;

    if (params.sync)
    {
        const auto quarterNotes = syncDivToQuarterNotes (params.divIndex);
        const auto secondsPerQuarter = 60.0 / juce::jmax (1.0, bpm);
        const auto cycleSeconds = juce::jmax (1.0e-4, quarterNotes * secondsPerQuarter);
        cyclesPerSecond = 1.0 / cycleSeconds;
    }
    else
    {
        cyclesPerSecond = juce::jlimit (0.01, 50.0, static_cast<double> (params.rateHz));
    }

    const auto frameSeconds = static_cast<double> (hopSamples) / juce::jmax (1.0, sampleRate);

    phase += cyclesPerSecond * frameSeconds;

    if (phase >= 1.0)
        phase -= std::floor (phase);

    const auto bands = juce::jmax (1, table.numBands);

    rotationOffset = static_cast<int> (phase * static_cast<double> (bands));
    rotationOffset = juce::jlimit (0, bands - 1, rotationOffset);

    // The rhythm gate steps through the sequence at the same rate.
    rhythmStepIndex = rotationOffset;

    // Per-frame gain slew. Grit shortens it towards instant.
    //
    // The time constant is measured in frames, and when it falls below a frame the slew
    // must become a jump. Clamping the divisor to >= 1 instead -- as an obvious guard
    // against dividing by zero -- would cap the coefficient at 1 - 1/e = 0.632, so Grit
    // at maximum would never reach hard edges and any smoothing under ~11 ms would be
    // quietly ignored. Guard the degenerate case explicitly rather than by clamping.
    const auto frameRate = juce::jmax (1.0, sampleRate / juce::jmax (1.0, static_cast<double> (hopSamples)));
    const auto smoothSeconds = juce::jmax (0.0,
        static_cast<double> (params.smoothMs) * 0.001 * (1.0 - static_cast<double> (params.grit)));
    const auto tauFrames = smoothSeconds * frameRate;

    slewCoeff = tauFrames <= 1.0e-6
                  ? 1.0f
                  : juce::jlimit (0.0f, 1.0f,
                                  static_cast<float> (1.0 - std::exp (-1.0 / tauFrames)));

    buildGainMap (numBins);
}

void FractalSlicer::processFrame (float* frame, int numBins, int channel)
{
    if (channel >= static_cast<int> (channels.size()))
        return;

    auto& ch = channels[static_cast<std::size_t> (channel)];

    const auto mix = juce::jlimit (0.0f, 1.0f, params.mix);

    // Nothing to do when the slicer is fully transparent: skip the work entirely rather
    // than multiply every bin by 1.
    const auto transparent = mix <= 0.0f
                          || (params.shatter <= 0.0f && params.gate <= 0.0f && ! params.invert);

    if (transparent)
    {
        // Keep the smoother primed at unity so re-engaging does not jump.
        std::fill (ch.smoothed.begin(), ch.smoothed.begin() + numBins, 1.0f);
        return;
    }

    if (anyShift)
    {
        std::copy (frame, frame + 2 * numBins, ch.scratch.begin());

        for (int k = 1; k < numBins; ++k)
        {
            const auto src = sourceBin[static_cast<std::size_t> (k)];

            if (src == k)
                continue;

            if (src < 0)
            {
                frame[2 * k]     = 0.0f;
                frame[2 * k + 1] = 0.0f;
            }
            else
            {
                frame[2 * k]     = ch.scratch[static_cast<std::size_t> (2 * src)];
                frame[2 * k + 1] = ch.scratch[static_cast<std::size_t> (2 * src + 1)];
            }
        }
    }

    for (int k = 0; k < numBins; ++k)
    {
        const auto ku = static_cast<std::size_t> (k);

        // One-pole slew towards the target, which is what keeps gate transitions from
        // clicking across frames.
        ch.smoothed[ku] += slewCoeff * (targetGain[ku] - ch.smoothed[ku]);

        const auto g = 1.0f + (ch.smoothed[ku] - 1.0f) * mix;

        frame[2 * k]     = scrub (frame[2 * k] * g);
        frame[2 * k + 1] = scrub (frame[2 * k + 1] * g);
    }

    // Band energies for the visualiser, measured post-slice on the first channel only.
    if (channel == 0)
    {
        for (int i = 0; i < table.numBands && i < static_cast<int> (energies.size()); ++i)
        {
            const auto& b = table.bands[static_cast<std::size_t> (i)];

            double sum = 0.0;

            for (int k = b.loBin; k < b.hiBin && k < numBins; ++k)
            {
                const auto re = static_cast<double> (frame[2 * k]);
                const auto im = static_cast<double> (frame[2 * k + 1]);
                sum += re * re + im * im;
            }

            const auto width = juce::jmax (1, b.hiBin - b.loBin);
            const auto rms = static_cast<float> (std::sqrt (sum / static_cast<double> (width)));

            // Mild smoothing so the visualiser does not strobe.
            auto& e = energies[static_cast<std::size_t> (i)];
            e += 0.35f * (rms - e);
        }
    }
}

} // namespace vbd::dsp
