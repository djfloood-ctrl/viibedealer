#include "SpectrumAnalyzer.h"
#include "Theme.h"

namespace vbd::gui
{

namespace
{
    // Asymmetric smoothing: rise quickly so transients register, fall slowly so the
    // display stays readable instead of flickering.
    constexpr float kRise = 0.55f;
    constexpr float kFall = 0.12f;

    float magToDb (float mag) noexcept
    {
        return mag <= 1.0e-6f ? -120.0f : 20.0f * std::log10 (mag);
    }
}

SpectrumAnalyzer::SpectrumAnalyzer()
{
    setOpaque (false);
    setInterceptsMouseClicks (false, false);

    inputDb.fill (minDb);
    outputDb.fill (minDb);
    carrierDb.fill (minDb);
}

void SpectrumAnalyzer::update (const VisualSnapshot& snap)
{
    auto smooth = [this] (std::array<float, points>& dst, const std::array<float, points>& src)
    {
        for (int i = 0; i < points; ++i)
        {
            const auto target = juce::jlimit (minDb, maxDb,
                                              magToDb (src[static_cast<std::size_t> (i)]));
            auto& v = dst[static_cast<std::size_t> (i)];

            if (! primed)
                v = target;
            else
                v += (target > v ? kRise : kFall) * (target - v);
        }
    };

    smooth (inputDb, snap.inputMag);
    smooth (outputDb, snap.outputMag);
    smooth (carrierDb, snap.carrierMag);

    primed = true;
    repaint();
}

void SpectrumAnalyzer::buildPath (juce::Path& path, const std::array<float, points>& db,
                                  juce::Rectangle<float> plot) const
{
    const auto toY = [&] (float value)
    {
        const auto norm = (value - minDb) / (maxDb - minDb);
        return plot.getBottom() - juce::jlimit (0.0f, 1.0f, norm) * plot.getHeight();
    };

    for (int i = 0; i < points; ++i)
    {
        const auto x = plot.getX() + plot.getWidth()
                     * (static_cast<float> (i) / static_cast<float> (points - 1));
        const auto y = toY (db[static_cast<std::size_t> (i)]);

        if (i == 0)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    if (bounds.isEmpty())
        return;

    g.setColour (theme::panel.withAlpha (0.86f));
    g.fillRoundedRectangle (bounds, 5.0f);

    auto plot = bounds.reduced (10.0f, 8.0f);

    // Decade gridlines. The axis is log-spaced from 20 Hz to 20 kHz, so these land at
    // fixed fractions of the width.
    g.setColour (theme::panelEdge.withAlpha (0.6f));
    g.setFont (juce::FontOptions (8.5f));

    const struct { float hz; const char* label; } marks[] = {
        { 50.0f, "50" }, { 100.0f, "100" }, { 500.0f, "500" },
        { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }
    };

    constexpr auto loHz = 20.0f;
    constexpr auto hiHz = 20000.0f;
    const auto logSpan = std::log (hiHz / loHz);

    for (const auto& mark : marks)
    {
        const auto norm = std::log (mark.hz / loHz) / logSpan;
        const auto x = plot.getX() + norm * plot.getWidth();

        g.setColour (theme::panelEdge.withAlpha (0.55f));
        g.fillRect (x, plot.getY(), 0.5f, plot.getHeight());

        g.setColour (theme::textDim.withAlpha (0.7f));
        g.drawText (mark.label, juce::Rectangle<float> (x - 14.0f, plot.getBottom() - 10.0f,
                                                         28.0f, 10.0f),
                    juce::Justification::centred, false);
    }

    g.saveState();
    g.reduceClipRegion (plot.toNearestInt());

    // Carrier first, dimmest: it is context for the other two.
    {
        juce::Path p;
        buildPath (p, carrierDb, plot);
        g.setColour (theme::violet.withAlpha (0.45f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // Input, filled, so the shape the engine is tracking is obvious.
    {
        juce::Path p;
        buildPath (p, inputDb, plot);

        auto filled = p;
        filled.lineTo (plot.getRight(), plot.getBottom());
        filled.lineTo (plot.getX(), plot.getBottom());
        filled.closeSubPath();

        g.setColour (theme::neonDim.withAlpha (0.2f));
        g.fillPath (filled);

        g.setColour (theme::neonDim.withAlpha (0.8f));
        g.strokePath (p, juce::PathStrokeType (1.0f));
    }

    // Output last and brightest, with a glow pass.
    {
        juce::Path p;
        buildPath (p, outputDb, plot);

        g.setColour (theme::neon.withAlpha (0.22f));
        g.strokePath (p, juce::PathStrokeType (3.5f));

        g.setColour (theme::neon);
        g.strokePath (p, juce::PathStrokeType (1.4f));
    }

    g.restoreState();

    // Legend.
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));

    const struct { juce::Colour colour; const char* label; } keys[] = {
        { theme::neon, "OUT" }, { theme::neonDim, "IN" }, { theme::violet, "CARRIER" }
    };

    auto legend = bounds.reduced (12.0f, 6.0f).removeFromTop (10.0f);

    for (const auto& key : keys)
    {
        auto cell = legend.removeFromRight (56.0f);
        g.setColour (key.colour);
        g.drawText (key.label, cell, juce::Justification::centredRight, false);
    }
}

} // namespace vbd::gui
