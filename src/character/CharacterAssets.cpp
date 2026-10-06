#include "CharacterAssets.h"

#include <BinaryData.h>

namespace vbd::character
{

namespace
{
    /** Finds an embedded resource by its original filename.

        JUCE mangles resource names (dots to underscores, collisions numbered), so the
        only stable key is the original filename it records alongside each entry. */
    const char* findResource (const juce::String& filename, int& size)
    {
        for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        {
            const auto* resource = BinaryData::namedResourceList[i];

            if (filename == BinaryData::getNamedResourceOriginalFilename (resource))
                return BinaryData::getNamedResource (resource, size);
        }

        size = 0;
        return nullptr;
    }
}

const LayerInfo* LayerGroup::find (juce::StringRef name) const
{
    for (const auto& layer : layers)
        if (layer.name == name)
            return &layer;

    return nullptr;
}

juce::Image CharacterAssets::loadImage (const juce::String& file, bool preferHiDpi)
{
    int size = 0;

    if (preferHiDpi)
    {
        // The @2x files are emitted with the same name; the generator prefixes the
        // directory, so try the high-resolution one first and fall back silently.
        if (const auto* data = findResource ("@2x_" + file, size))
            if (auto image = juce::ImageFileFormat::loadFrom (data, static_cast<std::size_t> (size));
                image.isValid())
            {
                imageScale = 2;
                return image;
            }
    }

    if (const auto* data = findResource (file, size))
        return juce::ImageFileFormat::loadFrom (data, static_cast<std::size_t> (size));

    return {};
}

bool CharacterAssets::parseGroup (const juce::var& value, LayerGroup& dest, bool preferHiDpi)
{
    if (! value.isObject())
        return false;

    if (const auto* canvas = value.getProperty ("canvas", {}).getArray();
        canvas != nullptr && canvas->size() >= 2)
        dest.canvas = { static_cast<int> (canvas->getUnchecked (0)),
                        static_cast<int> (canvas->getUnchecked (1)) };

    const auto* layers = value.getProperty ("layers", {}).getArray();

    if (layers == nullptr)
        return false;

    for (const auto& entry : *layers)
    {
        LayerInfo info;
        info.name = entry.getProperty ("name", {}).toString();
        info.file = entry.getProperty ("file", {}).toString();
        info.z = static_cast<int> (entry.getProperty ("z", 0));
        info.parallax = static_cast<float> (static_cast<double> (entry.getProperty ("parallax", 0.0)));

        if (const auto* rect = entry.getProperty ("rect", {}).getArray();
            rect != nullptr && rect->size() >= 4)
            info.rect = { static_cast<int> (rect->getUnchecked (0)),
                          static_cast<int> (rect->getUnchecked (1)),
                          static_cast<int> (rect->getUnchecked (2)),
                          static_cast<int> (rect->getUnchecked (3)) };

        if (const auto* pivot = entry.getProperty ("pivot", {}).getArray();
            pivot != nullptr && pivot->size() >= 2)
            info.pivot = { static_cast<float> (static_cast<double> (pivot->getUnchecked (0))),
                           static_cast<float> (static_cast<double> (pivot->getUnchecked (1))) };

        if (const auto* channels = entry.getProperty ("channels", {}).getArray())
            for (const auto& channel : *channels)
                info.channels.add (channel.toString());

        info.image = loadImage (info.file, preferHiDpi);

        // A layer whose art failed to load is kept in the list with an invalid image: the
        // rig skips it when drawing, so one missing file does not shift the z-order or
        // break the layers around it.
        dest.layers.push_back (std::move (info));
    }

    std::sort (dest.layers.begin(), dest.layers.end(),
               [] (const LayerInfo& a, const LayerInfo& b) { return a.z < b.z; });

    return ! dest.layers.empty();
}

bool CharacterAssets::load (bool preferHiDpi)
{
    valid = false;
    error.clear();
    groups.clear();
    sceneGroups.clear();
    scenes.clear();
    imageScale = 1;

    int size = 0;
    const auto* data = findResource ("manifest.json", size);

    if (data == nullptr || size <= 0)
    {
        error = "manifest.json is not embedded";
        return false;
    }

    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (data, size));

    if (! parsed.isObject())
    {
        error = "manifest.json did not parse";
        return false;
    }

    if (const auto* object = parsed.getProperty ("groups", {}).getDynamicObject())
        for (const auto& property : object->getProperties())
        {
            LayerGroup group;

            if (parseGroup (property.value, group, preferHiDpi))
                groups[property.name.toString()] = std::move (group);
        }

    if (const auto* object = parsed.getProperty ("scenes", {}).getDynamicObject())
        for (const auto& property : object->getProperties())
        {
            LayerGroup group;

            if (parseGroup (property.value, group, preferHiDpi))
            {
                scenes.add (property.name.toString());
                sceneGroups[property.name.toString()] = std::move (group);
            }
        }

    scenes.sort (true);

    valid = ! groups.empty();

    if (! valid)
        error = "manifest.json contained no usable layer groups";

    return valid;
}

const LayerGroup* CharacterAssets::group (juce::StringRef name) const
{
    const auto it = groups.find (juce::String (name));
    return it != groups.end() ? &it->second : nullptr;
}

const LayerGroup* CharacterAssets::scene (juce::StringRef name) const
{
    const auto it = sceneGroups.find (juce::String (name));
    return it != sceneGroups.end() ? &it->second : nullptr;
}

} // namespace vbd::character
