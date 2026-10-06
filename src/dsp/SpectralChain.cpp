#include "SpectralChain.h"

namespace vbd::dsp
{

namespace
{
    constexpr float kDisplayLoHz = 20.0f;
    constexpr float kDisplayHiHz = 20000.0f;

    /** Normalised 0..1 position of a frequency on the display's log axis. */
    float logPosition (float hz) noexcept
    {
        const auto clamped = juce::jlimit (kDisplayLoHz, kDisplayHiHz, hz);
        return std::log (clamped / kDisplayLoHz) / std::log (kDisplayHiHz / kDisplayLoHz);
    }
}

void SpectralChain::rebuildDisplayMap (int numBins)
{
    displayMapBins = numBins;

    const auto binHz = static_cast<float> (chainSampleRate)
                     / static_cast<float> (juce::jmax (1, chainFftSize));

    constexpr auto points = gui::VisualSnapshot::spectrumPoints;
    const auto logSpan = std::log (kDisplayHiHz / kDisplayLoHz);

    for (int i = 0; i <= points; ++i)
    {
        const auto norm = static_cast<float> (i) / static_cast<float> (points);
        const auto hz = kDisplayLoHz * std::exp (norm * logSpan);
        const auto bin = static_cast<int> (std::lround (hz / juce::jmax (binHz, 1.0e-6f)));

        displayEdges[static_cast<std::size_t> (i)] = juce::jlimit (0, numBins - 1, bin);
    }
}

void SpectralChain::captureVisuals (const float* modulator, const float* carrier,
                                    const float* output, int numBins)
{
    if (displayMapBins != numBins)
        rebuildDisplayMap (numBins);

    auto& snap = bridge->beginWrite();

    constexpr auto points = gui::VisualSnapshot::spectrumPoints;

    // Peak within each display point's bin range: a peak reads better than an average on
    // a spectrum display, where narrow partials are what you are looking for.
    auto decimate = [this, numBins] (const float* frame, std::array<float, points>& dst)
    {
        for (int i = 0; i < points; ++i)
        {
            auto lo = displayEdges[static_cast<std::size_t> (i)];
            auto hi = displayEdges[static_cast<std::size_t> (i + 1)];

            if (hi <= lo)
                hi = lo + 1;

            hi = juce::jmin (hi, numBins);

            float peak = 0.0f;

            for (int k = lo; k < hi; ++k)
            {
                const auto re = frame[2 * k];
                const auto im = frame[2 * k + 1];
                const auto mag = std::sqrt (re * re + im * im);
                peak = juce::jmax (peak, mag);
            }

            dst[static_cast<std::size_t> (i)] = peak * displayScale;
        }
    };

    decimate (modulator, snap.inputMag);
    decimate (carrier, snap.carrierMag);
    decimate (output, snap.outputMag);

    // Band geometry.
    //
    // Deliberately NOT on the analyser's log axis. The subdivision is linear in bins, so
    // plotted logarithmically the lowest band swallows a third of the display and the
    // figure stops looking self-similar at all. Positions are normalised across the
    // slicer's own frequency range instead, which fills the width and draws equal
    // subdivisions as equal widths -- which is what they are.
    const auto& table = slicer.getBandTable();
    const auto& energies = slicer.bandEnergies();

    const auto count = juce::jlimit (0, gui::VisualSnapshot::maxBands, table.numBands);

    const auto rangeLo = count > 0 ? table.bands[0].loBin : 0;
    const auto rangeHi = count > 0 ? table.bands[static_cast<std::size_t> (count - 1)].hiBin
                                   : numBins;
    const auto rangeSpan = static_cast<float> (juce::jmax (1, rangeHi - rangeLo));
    snap.numBands = count;
    snap.effectiveDepth = table.effectiveDepth;

    float peakEnergy = 1.0e-6f;

    for (int i = 0; i < count; ++i)
        peakEnergy = juce::jmax (peakEnergy, energies[static_cast<std::size_t> (i)]);

    for (int i = 0; i < count; ++i)
    {
        const auto& band = table.bands[static_cast<std::size_t> (i)];
        const auto iu = static_cast<std::size_t> (i);

        snap.bandLo[iu] = static_cast<float> (band.loBin - rangeLo) / rangeSpan;
        snap.bandHi[iu] = static_cast<float> (band.hiBin - rangeLo) / rangeSpan;

        // Normalised against the loudest band, so the display stays legible at any level.
        snap.bandEnergy[iu] = juce::jlimit (0.0f, 1.0f,
                                            energies[iu] / peakEnergy);
        snap.bandActive[iu] = band.active ? 1u : 0u;
        snap.bandLevel[iu] = static_cast<unsigned char> (juce::jlimit (0, 255, band.level));
    }

    // Low-end energy, for the character layer's hair sway in Phase E2.
    float lowSum = 0.0f;
    const auto lowPoints = points / 6;   // roughly up to 200 Hz on a log axis

    for (int i = 0; i < lowPoints; ++i)
        lowSum += snap.outputMag[static_cast<std::size_t> (i)];

    snap.lowEnergy = juce::jlimit (0.0f, 1.0f, lowSum / static_cast<float> (lowPoints) * 4.0f);

    float inPeak = 0.0f, outPeak = 0.0f;

    for (int i = 0; i < points; ++i)
    {
        inPeak = juce::jmax (inPeak, snap.inputMag[static_cast<std::size_t> (i)]);
        outPeak = juce::jmax (outPeak, snap.outputMag[static_cast<std::size_t> (i)]);
    }

    snap.inputPeak = inPeak;
    snap.outputPeak = outPeak;

    snap.bpm = snapBpm;
    snap.ppqPosition = snapPpq;
    snap.playing = snapPlaying;

    bridge->publish();
}

} // namespace vbd::dsp
