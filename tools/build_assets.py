#!/usr/bin/env python3
"""Generates the placeholder character art. Runs OUTSIDE Blender.

    python tools/build_assets.py              # everything
    python tools/build_assets.py --group dragon
    python tools/build_assets.py --fast       # fewer samples, for iteration

Blender is a BUILD-TIME tool only. The committed PNGs under assets/ are what the plugin
embeds, so anyone cloning the repo builds without Blender installed -- which Phase E2's
exit criteria verifies by building with Blender absent from PATH.

Structure mirrors the mgfx renderer next door: one subprocess per layer group so a failure
cannot take down the whole set, structured results on VBD_INFO sentinel lines, and a
traceback on stdout treated as failure -- because Blender exits 0 even when an embedded
script raises.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")
SCRIPT = os.path.join(ROOT, "tools", "blender", "render_layers.py")

BLENDER_CANDIDATES = [
    r"C:\Program Files\Blender Foundation\Blender 5.2\blender.exe",
    r"C:\Program Files\Blender Foundation\Blender 4.2\blender.exe",
    "/Applications/Blender.app/Contents/MacOS/Blender",
    "/usr/bin/blender",
]


def find_blender():
    for path in BLENDER_CANDIDATES:
        if os.path.isfile(path):
            return path

    found = shutil.which("blender")

    if found:
        return found

    sys.exit("Blender not found. Add its path to BLENDER_CANDIDATES in this script.\n"
             "Blender is only needed to regenerate placeholder art -- building the plugin "
             "does not require it.")


def run_group(blender, group, samples, verbose):
    cmd = [blender, "--background", "--factory-startup", "--python", SCRIPT,
           "--", ASSETS]

    if group:
        cmd.append(group)
        cmd.append(str(samples))

    started = time.time()
    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            text=True, encoding="utf-8", errors="replace", bufsize=1)

    info, crashed, tail = None, False, []

    for line in proc.stdout:
        line = line.rstrip()
        tail.append(line)

        if len(tail) > 40:
            tail.pop(0)

        # Blender exits 0 even when an embedded script raises, so the traceback itself is
        # the only reliable failure signal.
        if line.startswith("Traceback (most recent call last)"):
            crashed = True

        if line.startswith("VBD_INFO "):
            info = json.loads(line[len("VBD_INFO "):])
        elif verbose:
            print("   " + line)

    proc.wait()

    if crashed or (proc.returncode != 0):
        print("\n".join(tail))
        sys.exit("Blender failed while rendering group %r" % (group or "all"))

    return info, time.time() - started


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--group", default=None,
                        help="only this group (character, dragon, scenes/neon_city, ...)")
    parser.add_argument("--fast", action="store_true", help="fewer samples")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    blender = find_blender()
    samples = 8 if args.fast else 32

    print("Blender: %s" % blender)
    print("Output:  %s" % ASSETS)

    os.makedirs(ASSETS, exist_ok=True)

    info, seconds = run_group(blender, args.group, samples, args.verbose)

    rendered = info["written"] if info else 0
    print("rendered %d layers in %.1fs" % (rendered, seconds))

    # Trim, derive the 1x set, and write the manifest.
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    import postprocess

    _, counts, path = postprocess.build(ASSETS)

    print("manifest: %s" % os.path.relpath(path, ROOT))

    for group in sorted(counts):
        print("  %-24s %d layers" % (group, counts[group]))


if __name__ == "__main__":
    main()
