"""Trims the rendered layers, derives the 1x set, and writes assets/manifest.json.

Runs outside Blender, needs only Pillow.

Two jobs worth spelling out:

* **Trim to the alpha bounding box.** Full-canvas layers would be 1024x2048 RGBA each at
  2x -- about 8 MB per layer in memory, 200 MB for the set. Cropping to content and
  recording the offset keeps the embedded art to a few MB, and the rig reconstructs the
  original placement from the offset.

* **Measure, do not guess.** The crop rectangle is measured from the artwork; the pivot
  comes from the layer declaration because it is semantic (hair swings about the scalp).
  Both go into the manifest, so the rig never has to infer either.
"""
import json
import os
import sys

from PIL import Image

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "blender"))

import vbd_layers  # noqa: E402


def process_layer(assets_dir, group, canvas, layer):
    name = layer["name"]
    flat = group.replace("/", "_") + "_" + name + ".png"
    src_path = os.path.join(assets_dir, "layers", "@2x_" + flat)

    if not os.path.isfile(src_path):
        return None

    image = Image.open(src_path).convert("RGBA")
    bbox = image.getbbox()

    if bbox is None:
        # Fully transparent: keep a 1x1 stub so the manifest stays complete and the rig
        # has something to draw rather than a missing-file branch.
        bbox = (0, 0, 1, 1)

    cropped = image.crop(bbox)

    # The rendered image is 2x the declared canvas.
    scale = 2
    # Both resolutions live flat in layers/, the high-resolution one prefixed. They have
    # to be distinguishable by bare filename: JUCE's binary-data generator keys on that,
    # and two files called character_face.png would collide no matter which folders they
    # sit in.
    out2x = os.path.join(assets_dir, "layers", "@2x_" + flat)
    out1x = os.path.join(assets_dir, "layers", flat)

    os.makedirs(os.path.dirname(out1x), exist_ok=True)

    cropped.save(out2x, "PNG", optimize=True)

    one_x = cropped.resize((max(1, cropped.width // scale), max(1, cropped.height // scale)),
                           Image.LANCZOS)
    one_x.save(out1x, "PNG", optimize=True)

    entry = {
        "name": name,
        "file": flat,
        "z": layer["z"],
        # Offset and size in 1x canvas pixels, measured from the artwork.
        "rect": [bbox[0] // scale, bbox[1] // scale, one_x.width, one_x.height],
        "pivot": list(layer.get("pivot", (0.5, 0.5))),
        "channels": list(layer.get("channels", [])),
    }

    if "parallax" in layer:
        entry["parallax"] = layer["parallax"]

    return entry


def build(assets_dir):
    manifest = {
        "version": 1,
        "generator": "tools/build_assets.py",
        "groups": {},
        "scenes": {},
    }

    counts = {}

    for group, canvas, layer in vbd_layers.all_layers():
        entry = process_layer(assets_dir, group, canvas, layer)

        if entry is None:
            continue

        if group.startswith("scenes/"):
            scene = group.split("/", 1)[1]
            bucket = manifest["scenes"].setdefault(
                scene, {"canvas": list(canvas), "layers": []})
        else:
            bucket = manifest["groups"].setdefault(
                group, {"canvas": list(canvas), "layers": []})

        bucket["layers"].append(entry)
        counts[group] = counts.get(group, 0) + 1

    for bucket in list(manifest["groups"].values()) + list(manifest["scenes"].values()):
        bucket["layers"].sort(key=lambda e: e["z"])

    path = os.path.join(assets_dir, "manifest.json")

    with open(path, "w", encoding="utf-8") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")

    return manifest, counts, path


if __name__ == "__main__":
    assets = sys.argv[1] if len(sys.argv) > 1 else "assets"
    _, counts, path = build(assets)
    print("manifest: %s" % path)

    for group in sorted(counts):
        print("  %-24s %d layers" % (group, counts[group]))
