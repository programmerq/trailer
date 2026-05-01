"""
Trailer app icon — parametric Blender scene.

Run inside the Blender Docker container:
    blender --background --python render.py -- \
        --size 1024 --samples 256 --out output/icon_1024.png

All knobs are at the top of build_scene(). Tweak, re-render, iterate.
"""
import argparse
import math
import os
import sys

import bpy
import bmesh
from mathutils import Vector


# ---------- CLI ----------

def parse_args():
    argv = sys.argv
    if "--" in argv:
        argv = argv[argv.index("--") + 1:]
    else:
        argv = []
    p = argparse.ArgumentParser()
    p.add_argument("--size", type=int, default=1024)
    p.add_argument("--samples", type=int, default=256)
    p.add_argument("--out", type=str, default="output/icon.png")
    p.add_argument("--photo", type=str, default="inputs/arches-3.jpg")
    p.add_argument("--engine", type=str, default="CYCLES", choices=["CYCLES", "BLENDER_EEVEE_NEXT"])
    p.add_argument("--background", type=str, default="transparent",
                   choices=["transparent", "rounded_white"])
    p.add_argument("--tilt", type=float, default=62.0,
                   help="Camera tilt off vertical, degrees. 0=top-down ortho.")
    p.add_argument("--azimuth", type=float, default=25.0,
                   help="Camera orbit around vertical axis, degrees. 0=looking from -Y, "
                        "positive = orbit right (toward +X).")
    p.add_argument("--fov", type=float, default=22.0,
                   help="Camera field of view, degrees (perspective only).")
    p.add_argument("--cam-distance", type=float, default=210.0,
                   help="Camera distance from scene origin, mm.")
    p.add_argument("--save-blend", action="store_true",
                   help="Also save the assembled scene as a .blend next to the PNG.")
    p.add_argument("--ortho", action="store_true",
                   help="Use orthographic projection instead of perspective.")
    return p.parse_args(argv)


# ---------- Scene reset ----------

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    # Wipe any leftover orphan data.
    for collection in (bpy.data.meshes, bpy.data.materials, bpy.data.images,
                       bpy.data.lights, bpy.data.cameras, bpy.data.objects,
                       bpy.data.curves, bpy.data.node_groups):
        for item in list(collection):
            collection.remove(item)


# ---------- Geometry helpers ----------

def superellipse_xy(theta, a, b, n):
    """A point on a superellipse |x/a|^n + |y/b|^n = 1."""
    c = math.cos(theta)
    s = math.sin(theta)
    x = math.copysign(abs(c) ** (2.0 / n), c) * a
    y = math.copysign(abs(s) ** (2.0 / n), s) * b
    return x, y


def smoothstep(t):
    return t * t * (3.0 - 2.0 * t)


def make_loupe_body_box(name, length=36.0, width=24.0, height=28.0,
                          wall_thickness=1.8, bevel_width=0.6, bevel_segments=3):
    """
    Hollow rectangular ABS/polycarbonate body — like an injection-molded
    specimen box. Outer dimensions match a 35mm film frame (36×24mm) so the
    body's bottom footprint is the size of one frame. Walls are ~1.8mm thick;
    refraction through the corners shows the characteristic "thick clear box"
    look from the reference image.

    Mesh is closed-manifold (top and bottom are annular faces) so Cycles can
    refract through it cleanly. Edges are beveled via the Bevel modifier to
    soften the corners — real molded plastic always has edge breaks for
    tolerance and demolding.
    """
    bm = bmesh.new()
    half_l = length / 2.0
    half_w = width / 2.0
    inner_l = half_l - wall_thickness
    inner_w = half_w - wall_thickness

    def quad(z, half_x, half_y):
        return [bm.verts.new((-half_x, -half_y, z)),
                bm.verts.new((half_x, -half_y, z)),
                bm.verts.new((half_x, half_y, z)),
                bm.verts.new((-half_x, half_y, z))]

    outer_bot = quad(0.0, half_l, half_w)
    outer_top = quad(height, half_l, half_w)
    inner_bot = quad(0.0, inner_l, inner_w)
    inner_top = quad(height, inner_l, inner_w)

    # Outer walls (4), normals pointing outward.
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([outer_bot[i], outer_bot[j], outer_top[j], outer_top[i]])

    # Inner walls (4), normals pointing inward (reversed winding).
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([inner_bot[i], inner_top[i], inner_top[j], inner_bot[j]])

    # Top annular cap (closes top of walls; central rectangular hole remains open
    # to look down through the body to the photo).
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([outer_top[i], inner_top[i], inner_top[j], outer_top[j]])

    # Bottom annular cap (sits on the film frame).
    for i in range(4):
        j = (i + 1) % 4
        bm.faces.new([outer_bot[j], inner_bot[j], inner_bot[i], outer_bot[i]])

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    # Bevel all edges for that injection-molded edge-break look.
    bmesh.ops.bevel(bm, geom=list(bm.verts) + list(bm.edges) + list(bm.faces),
                     offset=bevel_width, segments=bevel_segments, profile=0.5,
                     affect="EDGES")

    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    bm.to_mesh(mesh)
    bm.free()

    for poly in mesh.polygons:
        poly.use_smooth = True

    return obj


def make_rect_shoulder(name, body_length, body_width, lens_r, thickness=2.5):
    """
    Black rectangular plate (shoulder) bridging the body and the lens ring.
    Built with a primitive cube + Boolean DIFFERENCE against a cylinder for
    the lens hole — much more reliable than hand-triangulating the annulus.
    """
    bpy.ops.mesh.primitive_cube_add(size=1, location=(0, 0, thickness / 2.0))
    plate = bpy.context.object
    plate.scale = (body_length, body_width, thickness)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    plate.name = name

    # Cutter cylinder (taller than the plate to ensure clean cut).
    bpy.ops.mesh.primitive_cylinder_add(
        radius=lens_r, depth=thickness * 4.0,
        vertices=64, location=(0, 0, thickness / 2.0),
    )
    cutter = bpy.context.object
    cutter.name = f"{name}_cutter"

    bool_mod = plate.modifiers.new("hole", "BOOLEAN")
    bool_mod.operation = "DIFFERENCE"
    bool_mod.object = cutter
    bool_mod.solver = "EXACT"

    bpy.context.view_layer.objects.active = plate
    bpy.ops.object.modifier_apply(modifier="hole")

    cutter.hide_render = True
    cutter.hide_viewport = True

    for poly in plate.data.polygons:
        poly.use_smooth = False  # keep flat shading on the rectangular faces
    return plate


def make_lens_ring(name, outer_r, inner_r, height, segments=96,
                     band_z_frac=0.62, band_h=0.6):
    """
    Clean cylindrical black lens housing with a thin horizontal band near the
    top (will get a white marking material via face index assignment). No
    explicit knurling geometry — the lens-ring "grip" feel can be added later
    via a procedural shader bump if wanted.

    Returns (obj, band_face_indices).
    """
    bm = bmesh.new()
    z0, z1 = 0.0, height
    band_low = height * band_z_frac - band_h / 2.0
    band_high = height * band_z_frac + band_h / 2.0
    z_levels = [z0, band_low, band_high, z1]

    def ring(r, z):
        return [bm.verts.new((r * math.cos(2 * math.pi * k / segments),
                              r * math.sin(2 * math.pi * k / segments), z))
                for k in range(segments)]

    outer = [ring(outer_r, z) for z in z_levels]
    inner = [ring(inner_r, z) for z in z_levels]

    band_face_indices = []
    face_i = 0

    # Outer wall (3 vertical sections — middle one is the white band).
    for li in range(len(z_levels) - 1):
        in_band = (li == 1)
        for k in range(segments):
            kn = (k + 1) % segments
            bm.faces.new([outer[li][k], outer[li][kn],
                          outer[li + 1][kn], outer[li + 1][k]])
            if in_band:
                band_face_indices.append(face_i)
            face_i += 1

    # Inner wall (single section, reversed winding for inward normal).
    for k in range(segments):
        kn = (k + 1) % segments
        bm.faces.new([inner[0][k], inner[-1][k], inner[-1][kn], inner[0][kn]])
        face_i += 1

    # Top annular cap.
    for k in range(segments):
        kn = (k + 1) % segments
        bm.faces.new([outer[-1][k], inner[-1][k], inner[-1][kn], outer[-1][kn]])
        face_i += 1

    # Bottom annular cap (reversed winding).
    for k in range(segments):
        kn = (k + 1) % segments
        bm.faces.new([outer[0][kn], inner[0][kn], inner[0][k], outer[0][k]])
        face_i += 1

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    bm.to_mesh(mesh)
    bm.free()

    for poly in mesh.polygons:
        poly.use_smooth = True
    return obj, band_face_indices


def make_housing_ring(name, outer_r, inner_r, height, segments=128):
    """A black-plastic ring (solid annular tube) — not a solid disk, so the
    lens can sit in its hole and rays can pass through unobstructed."""
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)

    bm = bmesh.new()
    z0, z1 = 0.0, height

    def ring(r, z):
        return [bm.verts.new((r * math.cos(2 * math.pi * k / segments),
                              r * math.sin(2 * math.pi * k / segments), z))
                for k in range(segments)]

    outer_bot = ring(outer_r, z0)
    outer_top = ring(outer_r, z1)
    inner_bot = ring(inner_r, z0)
    inner_top = ring(inner_r, z1)

    for k in range(segments):
        kn = (k + 1) % segments
        # Outer wall (normal outward).
        bm.faces.new([outer_bot[k], outer_top[k], outer_top[kn], outer_bot[kn]])
        # Inner wall (normal inward).
        bm.faces.new([inner_bot[kn], inner_top[kn], inner_top[k], inner_bot[k]])
        # Top annular face.
        bm.faces.new([outer_top[k], inner_top[k], inner_top[kn], outer_top[kn]])
        # Bottom annular face.
        bm.faces.new([outer_bot[kn], inner_bot[kn], inner_bot[k], outer_bot[k]])

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()

    for poly in mesh.polygons:
        poly.use_smooth = True
    return obj


def make_plano_convex_lens(name, radius, dome_height, segments=128, rings=24):
    """A solid plano-convex lens: flat bottom, domed top. Closed manifold mesh
    so refraction renders correctly."""
    mesh = bpy.data.meshes.new(name)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)

    bm = bmesh.new()

    # Top dome: parametrize as a portion of a sphere with radius >> dome_height.
    # For a plano-convex lens with edge radius `radius` and sag `dome_height`,
    # the spherical radius R satisfies R = (radius^2 + dome_height^2) / (2*dome_height).
    R = (radius * radius + dome_height * dome_height) / (2.0 * dome_height)
    cz = -R + dome_height  # sphere center z so top of dome is at z=dome_height

    # Build dome rings from edge (z=0, r=radius) up to apex (z=dome_height, r=0).
    dome_rings = []
    for i in range(rings + 1):
        t = i / rings
        # Parametrize by polar angle from the +z axis on the sphere.
        # At t=0 (edge), z=0 → angle = acos((0 - cz)/R) = acos(-cz/R)
        # At t=1 (apex), z=dome_height → angle = 0
        edge_angle = math.acos((0 - cz) / R)
        angle = edge_angle * (1 - t)
        z = cz + R * math.cos(angle)
        r = R * math.sin(angle)
        ring = [bm.verts.new((r * math.cos(2 * math.pi * k / segments),
                              r * math.sin(2 * math.pi * k / segments), z))
                for k in range(segments)]
        dome_rings.append(ring)

    # Apex (single point): collapse the last ring to one vertex if needed.
    # Currently the last ring has many vertices at r≈0; we'll triangle-fan to apex.
    # Replace the last ring with a single apex vertex.
    apex = bm.verts.new((0, 0, dome_height))
    last_ring = dome_rings[-2]  # second-to-last ring is near apex
    # Remove the verts of dome_rings[-1] (they're stacked at near-zero radius).
    for v in dome_rings[-1]:
        bm.verts.remove(v)
    dome_rings = dome_rings[:-1]

    # Side quads between dome rings (excluding apex).
    for i in range(len(dome_rings) - 1):
        for k in range(segments):
            kn = (k + 1) % segments
            bm.faces.new([dome_rings[i][k], dome_rings[i][kn],
                          dome_rings[i + 1][kn], dome_rings[i + 1][k]])
    # Triangle fan from last_ring to apex.
    for k in range(segments):
        kn = (k + 1) % segments
        bm.faces.new([last_ring[k], last_ring[kn], apex])

    # Flat bottom face — close the lens into a manifold.
    bottom_ring = dome_rings[0]
    bm.faces.new(list(reversed(bottom_ring)))

    bm.normal_update()
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()

    for poly in mesh.polygons:
        poly.use_smooth = True
    return obj


def _build_border_mesh(length, half_w, image_h, sprocket_pitch, sprocket_w, sprocket_h):
    """
    Construct the matte-black border + sprocket-hole mesh directly, no booleans.
    Each border band (top and bottom) is built as a long ladder of quads with
    rectangular gaps where sprocket holes belong.
    """
    bm = bmesh.new()
    half_l = length / 2.0
    band_top_y0, band_top_y1 = image_h, half_w
    band_bot_y0, band_bot_y1 = -half_w, -image_h

    num_sprockets = int(length / sprocket_pitch)
    start_x = -((num_sprockets - 1) * sprocket_pitch) / 2.0

    # Centerline-y for the sprocket holes within each band.
    spr_top_cy = (band_top_y0 + band_top_y1) / 2.0
    spr_bot_cy = (band_bot_y0 + band_bot_y1) / 2.0

    # Build x-cut points: alternating border-segment ↔ hole.
    cuts = [-half_l]
    for k in range(num_sprockets):
        cx = start_x + k * sprocket_pitch
        cuts.append(cx - sprocket_w / 2.0)
        cuts.append(cx + sprocket_w / 2.0)
    cuts.append(half_l)

    def make_band(y0, y1, hole_cy):
        hole_y0 = hole_cy - sprocket_h / 2.0
        hole_y1 = hole_cy + sprocket_h / 2.0
        for i in range(len(cuts) - 1):
            x0, x1 = cuts[i], cuts[i + 1]
            is_hole = (i % 2 == 1)  # odd segments are holes (cut)
            if is_hole:
                # Above-hole strip
                v00 = bm.verts.new((x0, hole_y1, 0))
                v01 = bm.verts.new((x1, hole_y1, 0))
                v02 = bm.verts.new((x1, y1, 0))
                v03 = bm.verts.new((x0, y1, 0))
                bm.faces.new([v00, v01, v02, v03])
                # Below-hole strip
                v10 = bm.verts.new((x0, y0, 0))
                v11 = bm.verts.new((x1, y0, 0))
                v12 = bm.verts.new((x1, hole_y0, 0))
                v13 = bm.verts.new((x0, hole_y0, 0))
                bm.faces.new([v10, v11, v12, v13])
            else:
                v0 = bm.verts.new((x0, y0, 0))
                v1 = bm.verts.new((x1, y0, 0))
                v2 = bm.verts.new((x1, y1, 0))
                v3 = bm.verts.new((x0, y1, 0))
                bm.faces.new([v0, v1, v2, v3])

    make_band(band_top_y0, band_top_y1, spr_top_cy)
    make_band(band_bot_y0, band_bot_y1, spr_bot_cy)
    return bm


def _build_frame_separators(length, image_h, frame_w, separator_w=0.6):
    """Thin black bars at the BOUNDARIES between adjacent photo frames within
    the image window. The photo tiling places frame *centers* at strip-x = 0,
    ±frame_w, etc.; tile *boundaries* sit at strip-x = ±frame_w/2, ±1.5*frame_w.
    Separators must land on the boundaries so each visible frame reads as a
    single distinct image."""
    bm = bmesh.new()
    half_l = length / 2.0
    # Boundaries at ±frame_w/2 + n*frame_w. Generate enough to cover the strip.
    num_each_side = int(math.ceil(half_l / frame_w)) + 1
    boundary_xs = []
    for n in range(-num_each_side, num_each_side + 1):
        cx = (n + 0.5) * frame_w  # ±frame_w/2, ±1.5*frame_w, …
        if -half_l - frame_w < cx < half_l + frame_w:
            boundary_xs.append(cx)

    for cx in boundary_xs:
        x0 = cx - separator_w / 2.0
        x1 = cx + separator_w / 2.0
        v0 = bm.verts.new((x0, -image_h, 0))
        v1 = bm.verts.new((x1, -image_h, 0))
        v2 = bm.verts.new((x1, image_h, 0))
        v3 = bm.verts.new((x0, image_h, 0))
        bm.faces.new([v0, v1, v2, v3])
    return bm


def make_film_strip(length=180.0, width=35.0, frame_w=38.0, frame_h=24.0,
                    sprocket_pitch=4.75, sprocket_w=2.0, sprocket_h=2.8,
                    photo_image=None):
    """
    A horizontal film strip lying on Z=0:
      - Bottom layer: the photographic image (emissive, like backlit film)
      - Middle layer: thin black separators between frames
      - Top layer: matte-black border bands with sprocket holes built into the mesh

    Returns a parent empty so the whole strip can be rotated as one unit.
    """
    parent = bpy.data.objects.new("FilmStripParent", None)
    bpy.context.collection.objects.link(parent)

    image_h = frame_h / 2.0  # half-height of the central image window
    half_w = width / 2.0

    # ---- Photo layer ----
    # Sized to the *central image window only* — NOT the full strip — so
    # sprocket holes (which lie in the border bands) don't reveal the photo
    # underneath. Anything looking through a sprocket hole hits the white
    # backdrop / transparent void.
    bpy.ops.mesh.primitive_plane_add(size=1, location=(0, 0, 0))
    photo_plane = bpy.context.object
    photo_plane.name = "PhotoLayer"
    photo_plane.scale = (length, frame_h, 1)
    photo_plane.parent = parent

    photo_mat = bpy.data.materials.new("PhotoMaterial")
    photo_mat.use_nodes = True
    nt = photo_mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    # Moderate emission so the strip outside the loupe doesn't clip to white.
    # The view transform's highlight roll-off (Filmic/AgX) keeps blue sky from
    # getting washed out at this strength.
    emit.inputs["Strength"].default_value = 2.8
    tex_coord = nt.nodes.new("ShaderNodeTexCoord")
    mapping = nt.nodes.new("ShaderNodeMapping")

    # Tile the photo so one full frame is centered at strip x=0 (the loupe
    # sits at origin — this puts a hero frame directly under it).
    # Per-frame UV span: 1/repeats. We want texture_u = 0.5 at uv_u = 0.5.
    # Mapping node formula: texture_u = scale * uv_u + location.
    repeats = length / frame_w
    nearest_center = math.floor(0.5 * repeats) + 0.5
    location_u = nearest_center - 0.5 * repeats
    mapping.inputs["Scale"].default_value = (repeats, 1.0, 1.0)
    mapping.inputs["Location"].default_value = (location_u, 0.0, 0.0)

    if photo_image is not None:
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = photo_image
        tex.extension = "REPEAT"
        nt.links.new(tex_coord.outputs["UV"], mapping.inputs["Vector"])
        nt.links.new(mapping.outputs["Vector"], tex.inputs["Vector"])
        nt.links.new(tex.outputs["Color"], emit.inputs["Color"])
    else:
        gradient = nt.nodes.new("ShaderNodeTexGradient")
        gradient.gradient_type = "LINEAR"
        ramp = nt.nodes.new("ShaderNodeValToRGB")
        elements = ramp.color_ramp.elements
        elements[0].position = 0.0
        elements[0].color = (0.18, 0.42, 0.65, 1.0)  # sky blue
        elements[1].position = 1.0
        elements[1].color = (0.55, 0.20, 0.10, 1.0)  # canyon red
        mid = ramp.color_ramp.elements.new(0.55)
        mid.color = (0.78, 0.45, 0.22, 1.0)  # sandstone
        nt.links.new(tex_coord.outputs["UV"], mapping.inputs["Vector"])
        nt.links.new(mapping.outputs["Vector"], gradient.inputs["Vector"])
        nt.links.new(gradient.outputs["Color"], ramp.inputs["Fac"])
        nt.links.new(ramp.outputs["Color"], emit.inputs["Color"])

    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    photo_plane.data.materials.append(photo_mat)

    # Shared matte-black material for the border + frame separators.
    black_mat = bpy.data.materials.new("FilmBlackMaterial")
    black_mat.use_nodes = True
    bsdf = black_mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.015, 0.015, 0.015, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.55

    # ---- Border (with sprocket holes built into mesh) ----
    border_mesh = bpy.data.meshes.new("FilmBorder")
    border_obj = bpy.data.objects.new("FilmBorder", border_mesh)
    bpy.context.collection.objects.link(border_obj)
    border_obj.parent = parent
    border_obj.location = (0, 0, 0.05)
    bm = _build_border_mesh(length, half_w, image_h, sprocket_pitch, sprocket_w, sprocket_h)
    bm.to_mesh(border_mesh)
    bm.free()
    border_obj.data.materials.append(black_mat)

    # ---- Frame separators ----
    sep_mesh = bpy.data.meshes.new("FrameSeparators")
    sep_obj = bpy.data.objects.new("FrameSeparators", sep_mesh)
    bpy.context.collection.objects.link(sep_obj)
    sep_obj.parent = parent
    sep_obj.location = (0, 0, 0.04)
    bm = _build_frame_separators(length, image_h, frame_w)
    bm.to_mesh(sep_mesh)
    bm.free()
    sep_obj.data.materials.append(black_mat)

    return parent


# ---------- Materials ----------

def make_clear_abs_material(name="ClearABS",
                              ior=1.55, surface_roughness=0.04,
                              absorption_color=(0.95, 0.96, 0.97),
                              absorption_density=0.045):
    """
    Plain polycarbonate (Glass BSDF, IOR 1.55) — full physical Fresnel.
    The "silvery back-wall mirror" issue is now solved at the integrator
    level instead: scene.cycles.transmission_bounces is reduced to a small
    number so rays terminate after a few refractions instead of ping-ponging.
    First-hit reflections (the look the user wants) stay physical; the
    accumulated multi-bounce silver does not.
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")

    glass = nt.nodes.new("ShaderNodeBsdfGlass")
    glass.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    glass.inputs["Roughness"].default_value = surface_roughness
    glass.inputs["IOR"].default_value = ior
    nt.links.new(glass.outputs["BSDF"], out.inputs["Surface"])

    absorption = nt.nodes.new("ShaderNodeVolumeAbsorption")
    absorption.inputs["Color"].default_value = (*absorption_color, 1.0)
    absorption.inputs["Density"].default_value = absorption_density
    nt.links.new(absorption.outputs["Volume"], out.inputs["Volume"])

    return mat


def make_black_housing_material(name="BlackABSHousing"):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (0.015, 0.015, 0.015, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.45
    bsdf.inputs["Specular IOR Level"].default_value = 0.3
    return mat


def make_optical_glass_material(name="OpticalGlass", ior=1.5168):
    """
    Plain Glass BSDF for the lens. IOR 1.5168 = real BK7 crown glass, the
    standard optical glass used in loupe inspection lenses. At low / side-on
    camera angles this produces the strong reflective Fresnel arc + the
    discrete refracted slice the user actually wants — the "smeary cusp" is
    real lens behavior at high angle of incidence.
    """
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    glass = nt.nodes.new("ShaderNodeBsdfGlass")
    glass.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    glass.inputs["Roughness"].default_value = 0.0
    glass.inputs["IOR"].default_value = ior
    nt.links.new(glass.outputs["BSDF"], out.inputs["Surface"])
    return mat


# ---------- Lighting ----------

def make_lighting():
    # Strong defined key from camera-left, ~35° elevation. Bigger source = soft
    # shadow with definite direction (cinematographer's brief — "modeling on
    # the loupe barrel, kick on the rim, cast shadow committing the loupe to
    # the surface").
    bpy.ops.object.light_add(type="AREA", location=(-75, -45, 60))
    key = bpy.context.object
    key.data.energy = 14000
    key.data.size = 50
    key.data.color = (1.0, 0.96, 0.88)
    # Aim at scene origin.
    direction = Vector((0, 0, 7)) - key.location
    key.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

    # Cool fill from opposite side (pulls a hint of canyon-shadow blue into rims).
    bpy.ops.object.light_add(type="AREA", location=(60, 50, 50))
    fill = bpy.context.object
    fill.data.energy = 4000
    fill.data.size = 80
    fill.data.color = (0.85, 0.92, 1.0)
    direction = Vector((0, 0, 7)) - fill.location
    fill.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()

    # Discrete "softbox" panel directly above and slightly forward — the lens
    # dome should pick this up as a clean specular arc highlight, the way
    # photographed lenses do under a softbox.
    bpy.ops.mesh.primitive_plane_add(size=40, location=(8, -10, 70))
    softbox = bpy.context.object
    softbox.name = "LensSoftbox"
    sb_mat = bpy.data.materials.new("Softbox")
    sb_mat.use_nodes = True
    nt = sb_mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (1.0, 0.97, 0.92, 1.0)
    # Lower strength so the softbox catches a tasteful highlight on the lens
    # dome without burning out the polycarbonate front face — at side-on
    # angles the body's Fresnel reflection picks up far more of the softbox
    # than at top-down, so the same strength reads much hotter.
    emit.inputs["Strength"].default_value = 6.0
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    softbox.data.materials.append(sb_mat)
    # Hide softbox from camera rays so it doesn't appear in renders, but still
    # contributes to specular reflections off the lens.
    softbox.visible_camera = False
    softbox.visible_diffuse = False
    softbox.visible_shadow = False


def make_rounded_backdrop(camera, fov_deg, distance_behind_camera=200.0,
                            corner_radius_ratio=0.17, segments_per_corner=24,
                            margin=1.04):
    """
    A flat white rounded-square plane parented to the camera, sitting far
    behind the scene. Sized so it just fills the camera frame at its distance
    — outside the rounded corners stays transparent (film_transparent), inside
    is solid white. This gives the icon the macOS/iOS rounded-square backdrop
    while preserving alpha at the corners.

    `margin` slightly oversizes the plane so the rounded corners aren't
    clipped by the frame edge.
    """
    # Visible side at the plane's distance, with `margin` slack for safety.
    side = 2.0 * distance_behind_camera * math.tan(math.radians(fov_deg) / 2.0) * margin
    half = side / 2.0
    cr = corner_radius_ratio * side
    inner = half - cr

    bm = bmesh.new()
    perimeter = []

    def arc(cx, cy, start_deg, end_deg, n=segments_per_corner):
        for i in range(n + 1):
            a = math.radians(start_deg + (end_deg - start_deg) * i / n)
            perimeter.append(bm.verts.new((cx + cr * math.cos(a),
                                            cy + cr * math.sin(a), 0)))

    arc(inner, -inner, -90, 0)            # bottom-right
    arc(inner, inner, 0, 90)              # top-right (skip first to avoid duplicate)
    arc(-inner, inner, 90, 180)           # top-left
    arc(-inner, -inner, 180, 270)         # bottom-left

    # Triangle-fan from a central vertex.
    center = bm.verts.new((0, 0, 0))
    for i in range(len(perimeter)):
        v1 = perimeter[i]
        v2 = perimeter[(i + 1) % len(perimeter)]
        bm.faces.new([center, v1, v2])

    mesh = bpy.data.meshes.new("RoundedBackdrop")
    obj = bpy.data.objects.new("RoundedBackdrop", mesh)
    bpy.context.collection.objects.link(obj)
    bm.to_mesh(mesh)
    bm.free()

    # White emissive material — strength 1.0 acts like a flat backdrop.
    mat = bpy.data.materials.new("WhiteBackdrop")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    emit = nt.nodes.new("ShaderNodeEmission")
    emit.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    emit.inputs["Strength"].default_value = 1.0
    nt.links.new(emit.outputs["Emission"], out.inputs["Surface"])
    obj.data.materials.append(mat)

    # Position explicitly along the camera's forward axis, perpendicular to
    # its view direction. We must force a depsgraph update first because the
    # camera's rotation_euler was set just before this and matrix_world is
    # otherwise stale.
    bpy.context.view_layer.update()
    cam_world = camera.matrix_world.copy()
    cam_quat = cam_world.to_quaternion()
    forward = cam_quat @ Vector((0.0, 0.0, -1.0))
    obj.location = cam_world.translation + forward * distance_behind_camera
    obj.rotation_mode = "QUATERNION"
    obj.rotation_quaternion = cam_quat

    # Camera-only — don't pour light into the scene or appear in reflections.
    obj.visible_diffuse = False
    obj.visible_glossy = False
    obj.visible_transmission = False
    obj.visible_volume_scatter = False
    obj.visible_shadow = False
    return obj


def make_camera(distance=140.0, tilt_deg=18.0, azimuth_deg=0.0, fov_deg=22.0,
                ortho=False, ortho_scale=50.0, look_at=(0, 0, 11)):
    """
    Camera placed in spherical coords around `look_at`:
      - tilt_deg: angle off the world vertical (z-axis). 0 = top-down.
      - azimuth_deg: orbit around z. 0 = camera at -Y of look_at, positive =
        orbit right toward +X (counter-clockwise when viewed from above).
      - distance: distance from look_at.
    Low FoV reads almost-ortho but adds the depth cues that let the body's
    wall thickness and lens dome register optically.
    """
    tilt = math.radians(tilt_deg)
    azimuth = math.radians(azimuth_deg)
    horizontal = distance * math.sin(tilt)
    cam_x = look_at[0] + horizontal * math.sin(azimuth)
    cam_y = look_at[1] - horizontal * math.cos(azimuth)
    cam_z = look_at[2] + distance * math.cos(tilt)
    bpy.ops.object.camera_add(location=(cam_x, cam_y, cam_z))
    cam = bpy.context.object
    if ortho:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho_scale
    else:
        cam.data.type = "PERSP"
        cam.data.lens_unit = "FOV"
        cam.data.angle = math.radians(fov_deg)

    # Aim at look_at by computing rotation from camera location to target.
    direction = Vector(look_at) - cam.location
    rot_quat = direction.to_track_quat("-Z", "Y")
    cam.rotation_euler = rot_quat.to_euler()
    bpy.context.scene.camera = cam
    return cam


# ---------- Scene assembly ----------

def build_scene(args):
    reset_scene()

    # ---- World ----
    # White world environment — the lens dome and the body's polycarbonate
    # pick this up via Fresnel reflections at glancing angles. Pairs with
    # the white squircle that postprocess.py composites behind the render,
    # so the lens reads as "reflecting the white card" instead of dark voids
    # showing through the transparent areas.
    world = bpy.data.worlds.new("World")
    bpy.context.scene.world = world
    bg = world.node_tree.nodes.get("Background")
    bg.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    bg.inputs["Strength"].default_value = 0.85

    # ---- Photo input ----
    photo_image = None
    if os.path.isfile(args.photo):
        photo_image = bpy.data.images.load(os.path.abspath(args.photo))
        print(f"[icon] Loaded photo: {args.photo}")
    else:
        print(f"[icon] No photo at {args.photo} — using procedural Arches-palette gradient.")

    # ---- Film strip ----
    strip = make_film_strip(photo_image=photo_image)
    # Diagonal: 7 o'clock → 2 o'clock is roughly +35° around Z.
    strip.rotation_euler = (0, 0, math.radians(35))

    # ---- Loupe geometry (stacked from bottom to top) ----
    # The loupe is parented to a single empty so the whole assembly can be
    # rotated in z to align the rectangular body with the film frame below it.
    loupe_parent = bpy.data.objects.new("LoupeParent", None)
    bpy.context.collection.objects.link(loupe_parent)

    # 1) Hollow rectangular polycarbonate body — outer 40×28 with 2 mm walls
    #    so the inner cavity is exactly 36×24 (one full 35mm film frame fits
    #    inside the body's hollow). Looking down through the body now frames
    #    one whole frame within the plastic walls. 28 mm tall, beveled edges.
    body_length = 40.0
    body_width = 28.0
    body_height = 28.0
    body_wall = 2.0
    body = make_loupe_body_box(
        "LoupeBody",
        length=body_length, width=body_width,
        height=body_height, wall_thickness=body_wall,
    )
    body.data.materials.append(make_clear_abs_material())
    body.parent = loupe_parent

    # 2) Black rectangular shoulder sitting on top of the body, with a circular
    #    hole for the lens housing.
    lens_housing_outer_r = 13.5
    lens_housing_inner_r = 11.5
    lens_housing_height = 4.0   # shorter so the lens dome reads above the rim
    shoulder_thickness = 2.5

    # Shoulder ~1mm smaller than the body in each dimension so the body's
    # beveled top edge peeks out around it as a thin specular line — that
    # catalog-photography "molded part pulled from a tool" cue.
    shoulder_inset = 0.6
    shoulder = make_rect_shoulder(
        "BlackShoulder",
        body_length=body_length - 2 * shoulder_inset,
        body_width=body_width - 2 * shoulder_inset,
        lens_r=lens_housing_outer_r + 0.1,
        thickness=shoulder_thickness,
    )
    shoulder.data.materials.append(make_black_housing_material())
    shoulder.location.z = body_height
    shoulder.parent = loupe_parent

    # 3) Clean cylindrical lens ring on top of the shoulder, with a white
    #    marking band near the top.
    lens_ring, band_face_indices = make_lens_ring(
        "LensRing",
        outer_r=lens_housing_outer_r, inner_r=lens_housing_inner_r,
        height=lens_housing_height,
    )
    # Sink the ring 0.5mm into the shoulder so the bottom-of-ring meets-edge
    # of-shoulder seam is hidden inside the shoulder's volume — without this,
    # the slight gap between the cylinder bottom and the flat shoulder reads
    # at 1024px as "missing geometry" along a horizontal line.
    lens_ring.location.z = body_height + shoulder_thickness - 0.5
    lens_ring.parent = loupe_parent

    # Two materials: black knurled body + bright white marking band.
    black_mat = make_black_housing_material("BlackLensRing")
    white_marking = bpy.data.materials.new("WhiteMarking")
    white_marking.use_nodes = True
    nt = white_marking.node_tree
    nt.nodes.clear()
    out = nt.nodes.new("ShaderNodeOutputMaterial")
    bsdf = nt.nodes.new("ShaderNodeBsdfPrincipled")
    bsdf.inputs["Base Color"].default_value = (0.95, 0.95, 0.93, 1.0)
    bsdf.inputs["Roughness"].default_value = 0.45
    nt.links.new(bsdf.outputs["BSDF"], out.inputs["Surface"])
    lens_ring.data.materials.append(black_mat)        # slot 0
    lens_ring.data.materials.append(white_marking)    # slot 1
    for fi in band_face_indices:
        if fi < len(lens_ring.data.polygons):
            lens_ring.data.polygons[fi].material_index = 1

    # 4) Plano-convex glass lens — raised within the ring so the dome peeks
    #    above the ring's top edge (visible from the side-on camera).
    lens_radius = lens_housing_inner_r + 0.1
    lens_dome = 1.5
    lens = make_plano_convex_lens("OpticalLens", radius=lens_radius, dome_height=lens_dome)
    # Lens flat-bottom z. Top of ring = body + shoulder + lens_housing_height.
    # Set lens flat-bottom = top_of_ring - 0.5 so the dome (1.5mm tall) pokes
    # above the rim by ~1.0mm.
    top_of_ring = body_height + shoulder_thickness + lens_housing_height
    lens.location.z = top_of_ring - 0.5
    lens.data.materials.append(make_optical_glass_material())
    lens.parent = loupe_parent

    # Align the loupe assembly with the film frame underneath. The strip is
    # rotated 35° around z; rotating the loupe parent the same amount makes
    # the rectangular body's footprint sit squarely on one frame.
    loupe_parent.rotation_euler = (0, 0, math.radians(35))

    # ---- Camera ----
    cam = make_camera(
        distance=args.cam_distance,
        tilt_deg=args.tilt,
        azimuth_deg=args.azimuth,
        fov_deg=args.fov,
        ortho=args.ortho,
    )

    # The white rounded-square backdrop is now handled by post-processing
    # (postprocess.py composites the transparent render onto an inset white
    # rounded square with proper margin). Blender just renders the loupe +
    # film strip with film_transparent so the alpha is preserved.


    # ---- Lighting ----
    make_lighting()


# ---------- Render ----------

def configure_render(args):
    scene = bpy.context.scene
    scene.render.engine = args.engine
    scene.render.resolution_x = args.size
    scene.render.resolution_y = args.size
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = (args.background == "transparent")
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.render.image_settings.color_depth = "16"
    if args.engine == "CYCLES":
        scene.cycles.samples = args.samples
        scene.cycles.use_denoising = True
        scene.cycles.device = "CPU"  # Docker on Mac → no GPU
        scene.cycles.max_bounces = 8
        # Transmission bounces deliberately low. With ~24 (Cycles default-ish),
        # rays ping-pong inside the hollow body and accumulate into a silver
        # mirror reflection over the canyon photo. Capping at 4 lets rays go
        # camera→front-outer→front-inner→back-inner→exit, so the first hit on
        # the back wall is fully physical (the look we want) but subsequent
        # multi-hops don't keep adding light.
        scene.cycles.transmission_bounces = 4
        scene.cycles.glossy_bounces = 4
        scene.cycles.transparent_max_bounces = 8
        scene.cycles.volume_bounces = 2
        scene.cycles.caustics_reflective = True
        scene.cycles.caustics_refractive = True
    # AgX (Blender 4+/5 default) handles highlights better than Standard —
    # blue sky in the photo doesn't blow out to white when the emission
    # strength pushes it past 1.0.
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Base Contrast"


def render(args):
    out_path = os.path.abspath(args.out)
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    bpy.context.scene.render.filepath = out_path
    bpy.ops.render.render(write_still=True)
    print(f"[icon] Wrote {out_path}")

    if args.save_blend:
        blend_path = os.path.splitext(out_path)[0] + ".blend"
        bpy.ops.wm.save_as_mainfile(filepath=blend_path)
        print(f"[icon] Saved .blend at {blend_path}")


def main():
    args = parse_args()
    build_scene(args)
    configure_render(args)
    render(args)


if __name__ == "__main__":
    main()
