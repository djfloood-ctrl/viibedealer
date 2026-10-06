#pragma once

#include <juce_graphics/juce_graphics.h>

namespace vbd::theme
{
    // Dark, futuristic, neon on black with a shifting prism gradient. Original palette --
    // nothing here is taken from an existing product's look.

    inline const juce::Colour background   { 0xff06060c };
    inline const juce::Colour panel        { 0xff0c0d16 };
    inline const juce::Colour panelRaised  { 0xff12131f };
    inline const juce::Colour panelEdge    { 0xff1e2033 };

    inline const juce::Colour neon         { 0xff35f0ff };   // cyan, the primary accent
    inline const juce::Colour neonDim      { 0xff1b6f7d };
    inline const juce::Colour violet       { 0xff8a7fd4 };
    inline const juce::Colour magenta      { 0xffff4fd8 };
    inline const juce::Colour amber        { 0xffffb545 };
    inline const juce::Colour lime         { 0xff8cff6b };

    inline const juce::Colour text         { 0xffd9dcea };
    inline const juce::Colour textDim      { 0xff6f7490 };

    /** The prism sweep, used for spectral colour across frequency and for glows.
        position wraps, so animating it past 1 is fine. */
    inline juce::Colour prism (float position, float brightness = 1.0f) noexcept
    {
        position -= std::floor (position);

        // Four stops: cyan -> violet -> magenta -> amber -> back to cyan.
        static const juce::Colour stops[] = { neon, violet, magenta, amber };
        constexpr int numStops = 4;

        const auto scaled = position * static_cast<float> (numStops);
        const auto index = static_cast<int> (scaled) % numStops;
        const auto frac = scaled - std::floor (scaled);

        const auto a = stops[index];
        const auto b = stops[(index + 1) % numStops];

        return a.interpolatedWith (b, frac).withMultipliedBrightness (brightness);
    }

    /** Layout constants. The whole interface is laid out at this fixed size and then
        scaled by a transform, which keeps vector drawing crisp at any zoom rather than
        rounding every coordinate. */
    inline constexpr int designWidth  = 1000;
    inline constexpr int designHeight = 620;

    inline constexpr int topBarHeight = 46;
    inline constexpr int columnWidth  = 300;
    inline constexpr int gutter       = 8;
}
