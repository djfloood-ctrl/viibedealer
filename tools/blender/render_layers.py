"""Runs INSIDE Blender. Builds the placeholder art and renders one PNG per layer.

    blender --background --factory-startup --python render_layers.py -- <outdir> [group]

Each layer renders alone against a transparent film from a shared orthographic camera, so
every PNG lands on the same pixel grid and the rig's pivots line up without any per-layer
fudging.

Blender 5.2 notes, probe-verified rather than assumed (the same traps mgfx/compat.py
documents):
  * EEVEE Next is registered as 'BLENDER_EEVEE' -- the '_NEXT' suffix is gone.
  * Cycles is not registered in every install, so EEVEE is the only safe choice. It is
    also the right one here: path tracing would fight a flat cel look.
  * Blender exits 0 even when an embedded script raises, so the orchestrator watches for
    a traceback on stdout rather than trusting the exit code.
"""
import json
import math
import os
import sys

import bpy

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import vbd_layers  # noqa: E402

EEVEE = 'BLENDER_EEVEE'


def clear_scene():
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete()

    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.curves):
        for item in list(block):
            block.remove(item)


def set_blend_mode(mat):
    """Enable alpha blending, across Blender versions that renamed the property."""
    for attr, value in (("surface_render_method", 'BLENDED'), ("blend_method", 'BLEND')):
        try:
            setattr(mat, attr, value)
            return
        except (AttributeError, TypeError):
            continue


def make_soft_material(name, colour, glow):
    """A radial falloff rather than a disc: used for glows and fire.

    An opaque ellipse is not a glow -- composited over the character it simply hides it.
    Alpha falls off from the centre so the layer reads as light.
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    set_blend_mode(mat)
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new('ShaderNodeOutputMaterial')
    out.location = (700, 0)

    emission = nt.nodes.new('ShaderNodeEmission')
    emission.location = (420, -120)
    emission.inputs['Color'].default_value = (colour[0], colour[1], colour[2], 1.0)
    emission.inputs['Strength'].default_value = 0.5 + 0.8 * glow

    transparent = nt.nodes.new('ShaderNodeBsdfTransparent')
    transparent.location = (420, 60)

    coord = nt.nodes.new('ShaderNodeTexCoord')
    coord.location = (-300, 0)

    offset = nt.nodes.new('ShaderNodeVectorMath')
    offset.operation = 'SUBTRACT'
    offset.location = (-120, 0)
    offset.inputs[1].default_value = (0.5, 0.5, 0.5)

    length = nt.nodes.new('ShaderNodeVectorMath')
    length.operation = 'LENGTH'
    length.location = (60, 0)

    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.location = (240, 0)
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (1.0, 1.0, 1.0, 1.0)
    ramp.color_ramp.elements[1].position = 0.52
    ramp.color_ramp.elements[1].color = (0.0, 0.0, 0.0, 1.0)

    mix = nt.nodes.new('ShaderNodeMixShader')
    mix.location = (560, 0)

    nt.links.new(coord.outputs['Generated'], offset.inputs[0])
    nt.links.new(offset.outputs['Vector'], length.inputs[0])
    nt.links.new(length.outputs['Value'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], mix.inputs['Fac'])
    nt.links.new(transparent.outputs['BSDF'], mix.inputs[1])
    nt.links.new(emission.outputs['Emission'], mix.inputs[2])
    nt.links.new(mix.outputs['Shader'], out.inputs['Surface'])

    return mat


def make_material(name, colour, glow):
    """Cel-ish shading: a flat emission core lifted by a rim term.

    Placeholder art wants to read as cel-shaded line work, not as a lit 3D object, so the
    surface is emission-driven with a Fresnel-weighted rim rather than a diffuse BSDF.
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()

    out = nt.nodes.new('ShaderNodeOutputMaterial')
    out.location = (600, 0)

    emission = nt.nodes.new('ShaderNodeEmission')
    emission.location = (380, -80)
    emission.inputs['Color'].default_value = (colour[0], colour[1], colour[2], 1.0)
    emission.inputs['Strength'].default_value = 0.35 + 0.75 * glow

    rim = nt.nodes.new('ShaderNodeEmission')
    rim.location = (380, 140)
    rim.inputs['Color'].default_value = (
        min(1.0, colour[0] * 0.5 + 0.5),
        min(1.0, colour[1] * 0.5 + 0.5),
        min(1.0, colour[2] * 0.5 + 0.6),
        1.0)
    rim.inputs['Strength'].default_value = 0.7 + 1.1 * glow

    fresnel = nt.nodes.new('ShaderNodeFresnel')
    fresnel.location = (180, 60)
    fresnel.inputs['IOR'].default_value = 1.25

    mix = nt.nodes.new('ShaderNodeMixShader')
    mix.location = (480, 0)

    nt.links.new(fresnel.outputs['Fac'], mix.inputs['Fac'])
    nt.links.new(emission.outputs['Emission'], mix.inputs[1])
    nt.links.new(rim.outputs['Emission'], mix.inputs[2])
    nt.links.new(mix.outputs['Shader'], out.inputs['Surface'])

    return mat


def to_world(cx, cy, aspect):
    """Canvas space (x right, y down, 0..1) to Blender XY, origin centred."""
    return ((cx - 0.5) * 2.0 * aspect, (0.5 - cy) * 2.0)


def add_ellipse(shape, aspect, mat, depth):
    x, y = to_world(shape["cx"], shape["cy"], aspect)
    bpy.ops.mesh.primitive_circle_add(vertices=64, radius=1.0, fill_type='NGON',
                                      location=(x, y, depth))
    ob = bpy.context.object
    ob.scale = (shape["rx"] * 2.0 * aspect, shape["ry"] * 2.0, 1.0)
    ob.rotation_euler = (0.0, 0.0, shape.get("rot", 0.0))
    ob.data.materials.append(mat)
    return ob


def add_capsule(shape, aspect, mat, depth):
    """A thick line with rounded ends, as a rectangle plus two discs."""
    x0, y0 = to_world(shape["x0"], shape["y0"], aspect)
    x1, y1 = to_world(shape["x1"], shape["y1"], aspect)
    r = shape["r"] * 2.0
    dx, dy = x1 - x0, y1 - y0
    length = math.hypot(dx, dy)
    angle = math.atan2(dy, dx)

    parts = []

    if length > 1e-6:
        bpy.ops.mesh.primitive_plane_add(size=1.0,
                                         location=((x0 + x1) * 0.5, (y0 + y1) * 0.5, depth))
        body = bpy.context.object
        body.scale = (length, r * 2.0, 1.0)
        body.rotation_euler = (0.0, 0.0, angle)
        body.data.materials.append(mat)
        parts.append(body)

    for px, py in ((x0, y0), (x1, y1)):
        bpy.ops.mesh.primitive_circle_add(vertices=32, radius=r, fill_type='NGON',
                                          location=(px, py, depth))
        cap = bpy.context.object
        cap.data.materials.append(mat)
        parts.append(cap)

    return parts


def add_tri(shape, aspect, mat, depth):
    verts = [to_world(px, py, aspect) + (depth,) for px, py in shape["points"]]
    mesh = bpy.data.meshes.new("tri")
    mesh.from_pydata([(v[0], v[1], v[2]) for v in verts], [], [list(range(len(verts)))])
    mesh.update()
    ob = bpy.data.objects.new("tri", mesh)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(mat)
    return ob


def build_layer(layer, aspect):
    """Creates the objects for one layer and returns them."""
    if layer.get("soft"):
        mat = make_soft_material(layer["name"], layer.get("colour", (1, 1, 1)),
                                 layer.get("glow", 0.0))
    else:
        mat = make_material(layer["name"], layer.get("colour", (1, 1, 1)),
                            layer.get("glow", 0.0))
    objects = []

    for index, shape in enumerate(layer.get("shapes", [])):
        depth = -0.001 * index
        kind = shape["kind"]

        if kind == "ellipse":
            objects.append(add_ellipse(shape, aspect, mat, depth))
        elif kind == "capsule":
            objects.extend(add_capsule(shape, aspect, mat, depth))
        elif kind == "tri":
            objects.append(add_tri(shape, aspect, mat, depth))

    return objects


def setup_scene(width, height, samples):
    sc = bpy.context.scene
    sc.render.engine = EEVEE
    sc.render.resolution_x = width
    sc.render.resolution_y = height
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = True
    sc.render.image_settings.file_format = 'PNG'
    sc.render.image_settings.color_mode = 'RGBA'
    sc.render.image_settings.compression = 25

    try:
        sc.eevee.taa_render_samples = samples
    except AttributeError:
        pass

    # Standard view transform, not the default AgX.
    #
    # AgX is a filmic tone map built for photographic renders: it rolls off highlights and
    # desaturates hard, which is exactly wrong for flat cel art. With it enabled the neon
    # palette came out as washed grey-blue. 'Standard' passes the authored colours through.
    try:
        sc.view_settings.view_transform = 'Standard'
        sc.view_settings.look = 'None'
    except (AttributeError, TypeError):
        pass

    aspect = float(width) / float(height)

    cam_data = bpy.data.cameras.new("cam")
    cam_data.type = 'ORTHO'
    # to_world() puts the canvas in x = [-aspect, aspect], y = [-1, 1], so the world is
    # 2*aspect wide and 2 tall. ortho_scale sizes the camera's LARGER dimension, not its
    # width -- setting it to the width framed only the middle of a portrait canvas, which
    # rendered everything oversized and pushed the small layers off-frame entirely.
    cam_data.ortho_scale = 2.0 * max(aspect, 1.0)
    cam = bpy.data.objects.new("cam", cam_data)
    sc.collection.objects.link(cam)
    cam.location = (0.0, 0.0, 6.0)
    sc.camera = cam

    return aspect


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    outdir = argv[0]
    only_group = argv[1] if len(argv) > 1 else None
    samples = int(argv[2]) if len(argv) > 2 else 24

    written = []

    for group, canvas, layer in vbd_layers.all_layers():
        if only_group is not None and group != only_group:
            continue

        clear_scene()
        # Render at 2x and let Pillow derive the 1x: downscaling is sharper than a second
        # render and guarantees the two sizes are perfectly registered.
        aspect = setup_scene(canvas[0] * 2, canvas[1] * 2, samples)

        build_layer(layer, aspect)

        # Flat, group-prefixed filenames. JUCE's binary-data generator keys resources on
        # the bare filename, and three scenes each having a "far.png" would collide.
        flat = group.replace("/", "_") + "_" + layer["name"] + ".png"
        path = os.path.join(outdir, "layers", "@2x_" + flat)
        os.makedirs(os.path.dirname(path), exist_ok=True)

        bpy.context.scene.render.filepath = path
        bpy.ops.render.render(write_still=True)

        written.append({"group": group, "name": layer["name"], "path": path,
                        "canvas": list(canvas)})

    print("VBD_INFO " + json.dumps({"written": len(written), "layers": written}))


main()
