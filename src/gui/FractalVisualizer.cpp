#include "FractalVisualizer.h"
#include "Theme.h"

namespace vbd::gui
{

namespace
{
    // How fast interpolated geometry chases the snapshot, per 60 Hz frame.
    constexpr float kGeometrySlew = 0.22f;
    constexpr float kEnergySlew   = 0.35f;
    constexpr float kActiveSlew   = 0.18f;
}

FractalVisualizer::FractalVisualizer()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);
}

void FractalVisualizer::update (const VisualSnapshot& snap)
{
    effectiveDepth = snap.effectiveDepth;
    lowEnergy += 0.2f * (snap.lowEnergy - lowEnergy);

    // The prism sweep drifts continuously; low-end energy nudges it along faster, so the
    // colour moves with the music rather than on a fixed clock.
    phase += 0.0012f + lowEnergy * 0.006f;

    if (phase > 1.0f)
        phase -= 1.0f;

    const auto count = juce::jlimit (0, VisualSnapshot::maxBands, snap.numBands);

    incoming.resize (static_cast<std::size_t> (count));

    for (int i = 0; i < count; ++i)
    {
        auto& b = incoming[static_cast<std::size_t> (i)];
        b.lo = snap.bandLo[static_cast<std::size_t> (i)];
        b.hi = snap.bandHi[static_cast<std::size_t> (i)];
        b.energy = snap.bandEnergy[static_cast<std::size_t> (i)];
        b.active = snap.bandActive[static_cast<std::size_t> (i)] != 0 ? 1.0f : 0.0f;
        b.level = static_cast<int> (snap.bandLevel[static_cast<std::size_t> (i)]);
    }

    if (count != lastBandCount)
    {
        // The figure changed shape. Interpolating across a different number of bands
        // would pair up unrelated ones, so cross-fade the whole thing instead.
        lastBandCount = count;
        bands = incoming;
        crossfade = 0.0f;
    }
    else
    {
        bands.resize (static_cast<std::size_t> (count));

        for (int i = 0; i < count; ++i)
        {
            auto& dst = bands[static_cast<std::size_t> (i)];
            const auto& src = incoming[static_cast<std::size_t> (i)];

            dst.lo     += kGeometrySlew * (src.lo - dst.lo);
            dst.hi     += kGeometrySlew * (src.hi - dst.hi);
            dst.energy += kEnergySlew   * (src.energy - dst.energy);
            dst.active += kActiveSlew   * (src.active - dst.active);
            dst.level   = src.level;
        }
    }

    crossfade = juce::jmin (1.0f, crossfade + 0.08f);

    repaint();
}

void FractalVisualizer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.isEmpty())
        return;

    // Translucent backdrop: the character layer sits behind this panel, and an opaque
    // fill would hide it completely.
    g.setColour (theme::panel.withAlpha (0.62f));
    g.fillRoundedRectangle (bounds, 5.0f);

    auto plot = bounds.reduced (10.0f);

    if (bands.empty())
    {
        g.setColour (theme::textDim);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("no signal", plot, juce::Justification::centred, false);
        return;
    }

    const auto depth = juce::jmax (1, effectiveDepth);

    // Each band is a column whose TOP sits at its depth in the cascade and whose bottom
    // reaches the floor. Shallow bands are tall, deep bands are short, so the subdivision
    // reads as a nested skyline across frequency -- a band placed only in its own level's
    // row leaves most of the panel empty and the self-similarity invisible.
    const auto rows = depth + 1;
    const auto rowHeight = plot.getHeight() / static_cast<float> (rows + 1);

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    // Level guides first, so bands sit on top of them.
    g.setColour (theme::panelEdge.withAlpha (0.45f));

    for (int row = 1; row <= rows; ++row)
    {
        const auto y = plot.getY() + static_cast<float> (row) * rowHeight;
        g.fillRect (plot.getX(), y, plot.getWidth(), 0.5f);
    }

    for (const auto& band : bands)
    {
        const auto width = (band.hi - band.lo) * plot.getWidth();

        if (width <= 0.0f)
            continue;

        const auto level = juce::jlimit (0, rows - 1, band.level);
        const auto top = plot.getY() + static_cast<float> (level) * rowHeight;

        auto cell = juce::Rectangle<float> (plot.getX() + band.lo * plot.getWidth(),
                                            top,
                                            width,
                                            plot.getBottom() - top).reduced (0.5f, 0.0f);

        if (cell.getWidth() < 0.8f)
            cell.setWidth (0.8f);

        // Colour follows frequency through the prism sweep, so the palette shifts across
        // the spectrum and drifts over time.
        const auto centre = (band.lo + band.hi) * 0.5f;
        const auto colour = theme::prism (centre * 0.75f + phase);

        const auto energy = juce::jlimit (0.0f, 1.0f, band.energy);
        const auto openness = juce::jlimit (0.0f, 1.0f, band.active);

        // A gated band stays visible as a dim shape: seeing where the holes are is the
        // point of the display.
        const auto base = (0.05f + openness * 0.14f) * crossfade;

        g.setColour (colour.withAlpha (base));
        g.fillRect (cell);

        // Live energy fills upward from the floor, so the column reads as a level meter
        // inside its own band.
        if (energy > 0.005f)
        {
            const auto fill = cell.withTop (cell.getBottom() - cell.getHeight() * energy);

            g.setColour (colour.withAlpha ((0.25f + openness * 0.55f) * crossfade));
            g.fillRect (fill);

            // Glow along the top edge of the fill.
            g.setColour (colour.withAlpha (energy * openness * 0.8f * crossfade));
            g.fillRect (fill.getX(), fill.getY() - 1.0f, fill.getWidth(), 2.0f);
        }

        // Band edges, brighter for shallower subdivisions, which is what makes the
        // hierarchy legible.
        const auto edgeAlpha = (0.5f - 0.35f * static_cast<float> (level)
                                              / static_cast<float> (rows)) * crossfade;

        g.setColour (colour.withAlpha (juce::jmax (0.05f, edgeAlpha * (0.3f + openness * 0.7f))));
        g.fillRect (cell.getX(), cell.getY(), 0.8f, cell.getHeight());
        g.fillRect (cell.getX(), cell.getY(), cell.getWidth(), 0.8f);
    }

    g.restoreState();

    g.setColour (theme::textDim);
    g.setFont (juce::FontOptions (9.5f));
    g.drawText ("FRACTAL  depth " + juce::String (effectiveDepth)
                + "   " + juce::String (static_cast<int> (bands.size())) + " bands",
                bounds.reduced (12.0f, 6.0f), juce::Justification::topLeft, false);
}

} // namespace vbd::gui
