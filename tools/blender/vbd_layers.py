"""The single source of truth for what the art is made of.

Both the Blender builder and the manifest writer import this, so a layer cannot exist in
one and be missing from the other. An illustrator replacing the placeholders works from
exactly this list -- see ASSETS.md, which is generated from it.

Each layer declares:

    name      stable identifier; the manifest and the C++ rig key off it
    group     "character" | "dragon" | a scene name
    z         draw order within the group, low to high
    pivot     normalised canvas coordinate the layer rotates and scales about.
              Semantic, not derived from the artwork: hair swings about the scalp, not
              about the centre of its bounding box.
    channels  which animation channels drive it; unknown names are ignored by the rig,
              so new channels can be added to art before code understands them
    shapes    placeholder geometry, as primitives in canvas space (ignored once real art
              replaces the PNGs)

Canvas space is x right, y DOWN from the top-left, normalised 0..1 on both axes, so the
numbers read the same way as the image files they describe.
"""

CHARACTER_CANVAS = (512, 1024)
DRAGON_CANVAS = (1024, 512)
SCENE_CANVAS = (1024, 640)

# Neon palette, matching src/gui/Theme.h.
NEON = (0.21, 0.94, 1.00)
VIOLET = (0.54, 0.50, 0.83)
MAGENTA = (1.00, 0.31, 0.85)
AMBER = (1.00, 0.71, 0.27)
LIME = (0.55, 1.00, 0.42)
DEEP = (0.08, 0.09, 0.16)
SKIN = (0.92, 0.80, 0.78)


def ellipse(cx, cy, rx, ry, rot=0.0):
    return {"kind": "ellipse", "cx": cx, "cy": cy, "rx": rx, "ry": ry, "rot": rot}


def capsule(x0, y0, x1, y1, r):
    return {"kind": "capsule", "x0": x0, "y0": y0, "x1": x1, "y1": y1, "r": r}


def tri(points):
    return {"kind": "tri", "points": points}


# --------------------------------------------------------------------------- character
#
# An original adult anime-styled figure: tall silhouette, long hair, a long coat. Nothing
# here reproduces an existing character or studio design.

CHARACTER = [
    dict(name="back_hair", z=0, pivot=(0.50, 0.17), colour=VIOLET, glow=0.25,
         channels=["sway_slow", "breathe", "low_energy"],
         shapes=[ellipse(0.50, 0.42, 0.30, 0.34),
                 capsule(0.26, 0.30, 0.20, 0.72, 0.075),
                 capsule(0.74, 0.30, 0.80, 0.72, 0.075)]),

    dict(name="body", z=1, pivot=(0.50, 0.46), colour=SKIN, glow=0.0,
         channels=["breathe"],
         shapes=[capsule(0.50, 0.26, 0.50, 0.60, 0.085),
                 capsule(0.42, 0.33, 0.30, 0.56, 0.036),
                 capsule(0.58, 0.33, 0.70, 0.56, 0.036),
                 ellipse(0.50, 0.195, 0.093, 0.108)]),

    dict(name="outfit", z=2, pivot=(0.50, 0.34), colour=DEEP, glow=0.08,
         channels=["breathe", "sway_fast"],
         shapes=[tri([(0.34, 0.30), (0.66, 0.30), (0.76, 0.98), (0.24, 0.98)]),
                 capsule(0.50, 0.27, 0.50, 0.40, 0.10)]),

    dict(name="outfit_trim", z=3, pivot=(0.50, 0.34), colour=NEON, glow=1.0,
         channels=["breathe", "sway_fast", "spectral_glow"],
         shapes=[capsule(0.345, 0.32, 0.245, 0.96, 0.009),
                 capsule(0.655, 0.32, 0.755, 0.96, 0.009),
                 capsule(0.40, 0.44, 0.60, 0.44, 0.008)]),

    dict(name="face", z=4, pivot=(0.50, 0.21), colour=SKIN, glow=0.0,
         channels=["head_bob", "breathe"],
         shapes=[ellipse(0.50, 0.195, 0.088, 0.103)]),

    dict(name="eyes_open", z=5, pivot=(0.50, 0.20), colour=NEON, glow=1.0,
         channels=["head_bob", "blink", "eye_track", "spectral_glow"],
         shapes=[ellipse(0.455, 0.197, 0.021, 0.026),
                 ellipse(0.545, 0.197, 0.021, 0.026)]),

    dict(name="eyes_half", z=5, pivot=(0.50, 0.20), colour=NEON, glow=0.8,
         channels=["head_bob", "blink", "eye_track"],
         shapes=[ellipse(0.455, 0.200, 0.021, 0.012),
                 ellipse(0.545, 0.200, 0.021, 0.012)]),

    dict(name="eyes_closed", z=5, pivot=(0.50, 0.20), colour=VIOLET, glow=0.4,
         channels=["head_bob", "blink"],
         shapes=[capsule(0.436, 0.200, 0.474, 0.200, 0.004),
                 capsule(0.526, 0.200, 0.564, 0.200, 0.004)]),

    dict(name="mouth_neutral", z=6, pivot=(0.50, 0.22), colour=MAGENTA, glow=0.3,
         channels=["head_bob", "mouth"],
         shapes=[capsule(0.484, 0.232, 0.516, 0.232, 0.0035)]),

    dict(name="mouth_open", z=6, pivot=(0.50, 0.22), colour=MAGENTA, glow=0.4,
         channels=["head_bob", "mouth"],
         shapes=[ellipse(0.50, 0.235, 0.018, 0.012)]),

    dict(name="mouth_smile", z=6, pivot=(0.50, 0.22), colour=MAGENTA, glow=0.35,
         channels=["head_bob", "mouth"],
         shapes=[capsule(0.480, 0.229, 0.500, 0.236, 0.0035),
                 capsule(0.500, 0.236, 0.520, 0.229, 0.0035)]),

    dict(name="front_hair", z=7, pivot=(0.50, 0.14), colour=VIOLET, glow=0.35,
         channels=["sway_fast", "head_bob", "low_energy"],
         shapes=[ellipse(0.50, 0.135, 0.105, 0.065),
                 tri([(0.40, 0.13), (0.47, 0.13), (0.405, 0.245)]),
                 tri([(0.53, 0.13), (0.60, 0.13), (0.595, 0.245)])]),

    dict(name="accessories", z=8, pivot=(0.50, 0.19), colour=AMBER, glow=1.0,
         channels=["head_bob", "spectral_glow", "sway_fast"],
         shapes=[capsule(0.41, 0.175, 0.59, 0.175, 0.006),
                 ellipse(0.62, 0.205, 0.016, 0.016),
                 ellipse(0.38, 0.205, 0.016, 0.016)]),

    dict(name="glow_fx", z=9, pivot=(0.50, 0.30), colour=NEON, glow=0.55, soft=True,
         channels=["spectral_glow", "low_energy", "react"],
         shapes=[ellipse(0.50, 0.30, 0.26, 0.26)]),
]

# ----------------------------------------------------------------------------- dragon
#
# A serpentine eastern-style dragon that coils around the visualiser. Body segments are
# separate layers so the rig can run them along a spline with secondary motion.

DRAGON_SEGMENTS = 8
DRAGON_TAIL_SEGMENTS = 4

DRAGON = []

for i in range(DRAGON_SEGMENTS):
    t = i / float(DRAGON_SEGMENTS - 1)
    radius = 0.085 - 0.030 * t
    DRAGON.append(dict(
        name="body_%02d" % i, z=10 + i, colour=NEON, glow=0.45 + 0.25 * (1.0 - t),
        pivot=(0.5, 0.5),
        channels=["spline_%d" % i, "band_energy", "spectral_glow", "ghost_trail"],
        shapes=[ellipse(0.5, 0.5, radius, radius * 0.78)]))

for i in range(DRAGON_TAIL_SEGMENTS):
    t = i / float(DRAGON_TAIL_SEGMENTS - 1)
    radius = 0.050 - 0.030 * t
    DRAGON.append(dict(
        name="tail_%02d" % i, z=20 + i, colour=VIOLET, glow=0.4,
        pivot=(0.5, 0.5),
        channels=["spline_%d" % (DRAGON_SEGMENTS + i), "tail_drag", "ghost_trail"],
        shapes=[ellipse(0.5, 0.5, radius, radius * 0.7)]))

DRAGON += [
    dict(name="wing_l", z=9, pivot=(0.78, 0.60), colour=VIOLET, glow=0.5,
         channels=["wing_flap", "spline_2", "ghost_trail"],
         shapes=[tri([(0.80, 0.60), (0.30, 0.18), (0.44, 0.66)]),
                 tri([(0.80, 0.60), (0.44, 0.66), (0.52, 0.86)])]),

    dict(name="wing_r", z=8, pivot=(0.78, 0.60), colour=VIOLET, glow=0.35,
         channels=["wing_flap", "spline_2", "ghost_trail"],
         shapes=[tri([(0.80, 0.60), (0.36, 0.30), (0.50, 0.70)])]),

    dict(name="head", z=30, pivot=(0.30, 0.50), colour=NEON, glow=0.8,
         channels=["spline_head", "look_at", "roar", "ghost_trail"],
         shapes=[ellipse(0.30, 0.50, 0.105, 0.070),
                 tri([(0.20, 0.47), (0.20, 0.53), (0.06, 0.50)]),
                 capsule(0.34, 0.44, 0.44, 0.33, 0.012),
                 capsule(0.34, 0.56, 0.44, 0.67, 0.012)]),

    dict(name="jaw", z=29, pivot=(0.34, 0.54), colour=NEON, glow=0.6,
         channels=["spline_head", "look_at", "roar"],
         shapes=[tri([(0.34, 0.52), (0.33, 0.60), (0.09, 0.53)])]),

    dict(name="eye", z=31, pivot=(0.30, 0.50), colour=AMBER, glow=1.0,
         channels=["spline_head", "look_at", "spectral_glow", "roar"],
         shapes=[ellipse(0.265, 0.482, 0.017, 0.013)]),

    dict(name="fire", z=32, pivot=(0.08, 0.50), colour=AMBER, glow=1.0, soft=True,
         channels=["fire", "spectral_glow"],
         shapes=[tri([(0.10, 0.44), (0.10, 0.56), (0.00, 0.50)]),
                 ellipse(0.055, 0.50, 0.035, 0.030)]),
]

# ------------------------------------------------------------------------------ scenes
#
# Parallax backdrops. Each scene has three layers at different depths; the rig offsets
# them by different amounts as the pointer moves.

SCENES = {
    "neon_city": [
        dict(name="far", z=0, colour=(0.10, 0.06, 0.20), glow=0.15, parallax=0.15,
             shapes=[tri([(0.00, 1.00), (0.08, 0.42), (0.16, 1.00)]),
                     tri([(0.20, 1.00), (0.30, 0.30), (0.40, 1.00)]),
                     tri([(0.55, 1.00), (0.66, 0.36), (0.78, 1.00)]),
                     tri([(0.84, 1.00), (0.93, 0.26), (1.00, 1.00)])]),
        dict(name="mid", z=1, colour=VIOLET, glow=0.35, parallax=0.4,
             shapes=[capsule(0.12, 0.55, 0.12, 1.00, 0.030),
                     capsule(0.35, 0.46, 0.35, 1.00, 0.040),
                     capsule(0.70, 0.52, 0.70, 1.00, 0.034),
                     capsule(0.90, 0.40, 0.90, 1.00, 0.026)]),
        dict(name="near", z=2, colour=MAGENTA, glow=0.7, parallax=0.9,
             shapes=[capsule(0.00, 0.86, 1.00, 0.86, 0.004),
                     capsule(0.05, 0.93, 0.26, 0.93, 0.010),
                     capsule(0.62, 0.90, 0.94, 0.90, 0.010)]),
    ],
    "shrine_dusk": [
        dict(name="far", z=0, colour=(0.22, 0.10, 0.12), glow=0.2, parallax=0.15,
             shapes=[ellipse(0.74, 0.30, 0.13, 0.13),
                     tri([(0.00, 1.00), (0.26, 0.52), (0.52, 1.00)])]),
        dict(name="mid", z=1, colour=AMBER, glow=0.45, parallax=0.4,
             shapes=[capsule(0.20, 0.52, 0.20, 1.00, 0.018),
                     capsule(0.46, 0.52, 0.46, 1.00, 0.018),
                     capsule(0.14, 0.50, 0.52, 0.50, 0.012),
                     capsule(0.16, 0.58, 0.50, 0.58, 0.008)]),
        dict(name="near", z=2, colour=MAGENTA, glow=0.6, parallax=0.9,
             shapes=[ellipse(0.10, 0.80, 0.055, 0.030),
                     ellipse(0.86, 0.86, 0.070, 0.034)]),
    ],
    "cosmic_void": [
        dict(name="far", z=0, colour=(0.06, 0.05, 0.14), glow=0.1, parallax=0.1,
             shapes=[ellipse(0.50, 0.50, 0.48, 0.44)]),
        dict(name="mid", z=1, colour=NEON, glow=0.5, parallax=0.35,
             shapes=[ellipse(0.50, 0.50, 0.30, 0.09, rot=0.35),
                     ellipse(0.50, 0.50, 0.40, 0.12, rot=-0.2)]),
        dict(name="near", z=2, colour=LIME, glow=0.9, parallax=0.8,
             shapes=[ellipse(0.22, 0.30, 0.010, 0.010),
                     ellipse(0.71, 0.22, 0.008, 0.008),
                     ellipse(0.84, 0.64, 0.012, 0.012),
                     ellipse(0.33, 0.74, 0.009, 0.009),
                     ellipse(0.58, 0.84, 0.007, 0.007)]),
    ],
}


def all_layers():
    """Flat list of (group, canvas, layer) for every layer in the set."""
    out = []

    for layer in CHARACTER:
        out.append(("character", CHARACTER_CANVAS, layer))

    for layer in DRAGON:
        out.append(("dragon", DRAGON_CANVAS, layer))

    for scene, layers in SCENES.items():
        for layer in layers:
            out.append(("scenes/" + scene, SCENE_CANVAS, layer))

    return out
