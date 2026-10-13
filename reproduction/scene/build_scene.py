"""Procedural alcove builder extracted from the accepted teacher source.
Run inside Blender 3.6.0. This file only builds/saves a scene; it never renders.
"""
from pathlib import Path
import argparse,json,math,sys
import bpy
import numpy as np
from mathutils import Matrix,Vector
SCALE=1.0/128.0
def material(name, color, roughness=0.65, metal=0.0):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*color, 1.0)
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = metal
    return mat


def cube(name, lo, hi, mat, bevel=0.0):
    lo, hi = np.asarray(lo), np.asarray(hi)
    bpy.ops.mesh.primitive_cube_add(size=1, location=tuple((lo + hi) * 0.5 * SCALE))
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = tuple((hi - lo) * SCALE)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.data.materials.append(mat)
    if bevel:
        obj.data.use_auto_smooth = True
        mod = obj.modifiers.new("rounded physical edges", "BEVEL")
        mod.width = bevel * SCALE
        mod.segments = 3
        obj.modifiers.new("weighted normals", "WEIGHTED_NORMAL")
    return obj


def sphere(name, center, radius, mat, ellipsoid=(1, 1, 1)):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=48, ring_count=24,
        radius=radius * SCALE, location=tuple(np.asarray(center) * SCALE))
    obj = bpy.context.object
    obj.name = name
    obj.scale = ellipsoid
    obj.data.materials.append(mat)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    return obj


def area(name, center, target, size_x, size_y, color, power):
    data = bpy.data.lights.new(name, "AREA")
    data.shape = "RECTANGLE"
    data.size = size_x * SCALE
    data.size_y = size_y * SCALE
    data.energy = power
    data.color = color
    data.use_shadow = True
    obj = bpy.data.objects.new(name, data)
    bpy.context.collection.objects.link(obj)
    obj.location = Vector(np.asarray(center) * SCALE)
    direction = Vector(np.asarray(target) - np.asarray(center))
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    return obj


def build_scene(width, height):
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"  # preparation cannot initialize a GPU render
    scene.cycles.samples = 64
    scene.cycles.max_bounces = 6
    scene.cycles.diffuse_bounces = 4
    scene.cycles.glossy_bounces = 4
    scene.cycles.transmission_bounces = 0
    scene.cycles.volume_bounces = 0
    scene.cycles.transparent_max_bounces = 0
    scene.cycles.caustics_reflective = False
    scene.cycles.caustics_refractive = False
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = False
    scene.cycles.use_light_tree = True
    scene.cycles.sampling_pattern = "TABULATED_SOBOL"
    scene.cycles.pixel_filter_type = "BOX"
    scene.cycles.filter_width = 0.01  # Frozen file value; Cycles BOX always uses one pixel.
    scene.render.use_persistent_data = True
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.pixel_aspect_x = scene.render.pixel_aspect_y = 1
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "32"
    scene.render.image_settings.exr_codec = "ZIP"
    scene.view_settings.view_transform = "Standard"
    # EXR and float arrays are scene-linear, bypassing the display view transform.
    # Pin the display configuration too, for anyone opening the saved .blend.
    for look in ("None", "Medium High Contrast"):
        try:
            scene.view_settings.look = look
            break
        except TypeError:
            pass
    scene.view_settings.exposure = 0.0
    scene.view_settings.gamma = 1.0
    scene.world.use_nodes = True
    scene.world.node_tree.nodes["Background"].inputs["Color"].default_value = (0, 0, 0, 1)
    scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.0

    stone = material("warm limestone rough", (0.55, 0.48, 0.35), 0.78)
    floor = material("neutral satin floor", (0.48, 0.48, 0.45), 0.32)
    coral = material("coral bounce wall", (0.65, 0.075, 0.035), 0.8)
    teal = material("teal bounce wall", (0.035, 0.35, 0.39), 0.8)
    ivory = material("ivory ceramic", (0.70, 0.66, 0.50), 0.28)
    bronze = material("rough bronze", (0.66, 0.36, 0.10), 0.24, 0.8)
    blue = material("cobalt glaze", (0.028, 0.14, 0.48), 0.22, 0.25)
    charcoal = material("dark baffles", (0.055, 0.065, 0.075), 0.8)

    cube("floor", [-174, -166, 34], [112, 166, 44], floor)
    cube("back wall", [-174, -166, 44], [-164, 166, 302], stone)
    cube("left coral wall", [-164, -166, 44], [105, -156, 302], coral)
    cube("right teal wall", [-164, 156, 44], [105, 166, 302], teal)
    cube("ceiling", [-174, -166, 292], [105, 166, 302], stone)
    # Broad rib relief, not high-frequency texture that a tiny model cannot retain.
    for k, y in enumerate((-115, -57, 1, 59, 117)):
        cube(f"back relief {k}", [-164, y - 9, 58], [-157, y + 9, 278], ivory, 2)
    cube("left plinth", [-52, -116, 44], [57, -23, 64], stone, 3)
    sphere("bronze low sphere", (5, -70, 108), 44, bronze)
    cube("right plinth", [-75, 21, 44], [18, 126, 66], stone, 3)
    sphere("blue tall ellipsoid", (-21, 76, 108), 34, blue, (0.82, 0.90, 1.23))
    sphere("ivory upper sphere", (-24, 74, 171), 25, ivory)
    bpy.ops.mesh.primitive_torus_add(major_segments=64, minor_segments=20,
        location=(-88 * SCALE, 2 * SCALE, 216 * SCALE),
        major_radius=41 * SCALE, minor_radius=11 * SCALE,
        rotation=(0, math.pi / 2, 0))
    ring = bpy.context.object
    ring.name = "rough bronze standing ring"
    ring.data.materials.append(bronze)
    for polygon in ring.data.polygons:
        polygon.use_smooth = True
    # The side strips are hidden from the front by baffles, but illuminate the
    # enclosure and objects. Surface colors/gloss, soft shadows and color bleeding
    # are the complexity; there are no redundant intersection or shading loops.
    cube("left light baffle", [45, -156, 172], [54, -105, 292], charcoal, 2)
    cube("right light baffle", [45, 108, 185], [54, 156, 292], charcoal, 2)
    area("warm hidden area", (-30, -151, 242), (-110, 15, 195), 70, 38,
         (1.0, 0.65, 0.34), 145)
    area("cool hidden area", (-70, 151, 258), (-50, -45, 120), 64, 45,
         (0.38, 0.68, 1.0), 115)
    area("soft overhead", (25, 0, 290), (-40, 0, 75), 88, 34,
         (1.0, 0.90, 0.70), 75)

    data = bpy.data.cameras.new("exact reflected camera")
    data.type = "PERSP"
    data.sensor_fit = "HORIZONTAL"
    data.sensor_width = 36.0
    data.clip_start = 0.001
    data.clip_end = 100.0
    camera = bpy.data.objects.new("exact reflected camera", data)
    bpy.context.collection.objects.link(camera)
    scene.camera = camera
    return scene


def main():
    p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--'in sys.argv else [])
    if args.out.exists():raise FileExistsError('Preserve existing scene; choose a new path')
    args.out.parent.mkdir(parents=True,exist_ok=True)
    build_scene(1920,1080)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.out.resolve()))
    print('Saved procedural alcove; not rendered.')
if __name__=='__main__':main()
