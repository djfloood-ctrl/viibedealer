#pragma once

#include <juce_graphics/juce_graphics.h>

#include <map>
#include <vector>

namespace vbd::character
{

/** One drawable layer, as described by assets/manifest.json. */
struct LayerInfo
{
    juce::String name;
    juce::String file;
    int z = 0;

    /** Position and size in 1x canvas pixels, measured from the artwork's alpha bounds. */
    juce::Rectangle<int> rect;

    /** Normalised canvas coordinate this layer rotates and scales about. Semantic, from
        the layer declaration -- hair swings about the scalp, not its bounding box. */
    juce::Point<float> pivot { 0.5f, 0.5f };

    juce::StringArray channels;
    float parallax = 0.0f;

    juce::Image image;

    bool hasChannel (juce::StringRef channel) const { return channels.contains (channel); }
};

struct LayerGroup
{
    juce::Point<int> canvas { 1, 1 };
    std::vector<LayerInfo> layers;

    const LayerInfo* find (juce::StringRef name) const;
};

/**
    Loads the layered art and its manifest from embedded binary data.

    Replacing the placeholders is a file swap: re-run tools/build_assets.py (or drop in an
    illustrator's export plus a regenerated manifest) and rebuild. No code changes, which
    is the whole point of describing pivots, z-order and animation channels in data.

    Missing or malformed assets are not fatal. isValid() goes false, the character layer
    draws nothing, and the rest of the interface carries on -- a plugin should not fail to
    open because a decorative layer is absent.
*/
class CharacterAssets
{
public:
    /** @param preferHiDpi load the @2x set, drawn at half size */
    bool load (bool preferHiDpi);

    bool isValid() const noexcept { return valid; }
    const juce::String& getError() const noexcept { return error; }

    const LayerGroup* group (juce::StringRef name) const;
    const LayerGroup* scene (juce::StringRef name) const;
    const juce::StringArray& sceneNames() const noexcept { return scenes; }

    /** 2 when the @2x set was loaded, otherwise 1. Layer rects are always in 1x units. */
    int getImageScale() const noexcept { return imageScale; }

private:
    bool parseGroup (const juce::var& value, LayerGroup& dest, bool preferHiDpi);
    juce::Image loadImage (const juce::String& file, bool preferHiDpi);

    std::map<juce::String, LayerGroup> groups;
    std::map<juce::String, LayerGroup> sceneGroups;
    juce::StringArray scenes;

    bool valid = false;
    int imageScale = 1;
    juce::String error;
};

} // namespace vbd::character
