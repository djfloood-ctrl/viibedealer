#!/usr/bin/env python3
"""Composites the layers of a group into one PNG, so the placeholder art can be looked at.

    python tools/preview_assets.py character out.png
"""
import json
import os
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")


def main():
    group = sys.argv[1] if len(sys.argv) > 1 else "character"
    out = sys.argv[2] if len(sys.argv) > 2 else "preview.png"

    with open(os.path.join(ASSETS, "manifest.json"), encoding="utf-8") as handle:
        manifest = json.load(handle)

    bucket = manifest["groups"].get(group) or manifest["scenes"].get(group)

    if bucket is None:
        sys.exit("no such group: %s" % group)

    width, height = bucket["canvas"]
    canvas = Image.new("RGBA", (width, height), (10, 10, 18, 255))

    for layer in bucket["layers"]:
        # Only one variant of the mutually exclusive sets, so the preview is readable.
        if layer["name"] in ("eyes_half", "eyes_closed", "mouth_open", "mouth_smile"):
            continue

        path = os.path.join(ASSETS, "layers", layer["file"])

        if not os.path.isfile(path):
            print("missing: %s" % path)
            continue

        image = Image.open(path).convert("RGBA")
        x, y = layer["rect"][0], layer["rect"][1]
        canvas.alpha_composite(image, (x, y))

    canvas.convert("RGB").save(out, "PNG")
    print("wrote %s (%dx%d)" % (out, width, height))


main()
