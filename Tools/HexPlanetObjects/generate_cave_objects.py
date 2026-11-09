"""Generates the meshes of the cave objects of the underground of the hex planet: clusters and fields of stalagmites, glowing mushrooms, crystal
clusters, pebbles and broken slabs, a ribcage, a horned skull, scattered bones and rune stones, the rims of the chasms: broken rocks, spires and
blocks of runes on the lips of the passages and stalactites below them, and masses that fill a cell and block it: a mass of stalagmites, a druse of
giant crystals and a rockfall.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_cave_objects.py
Writes GLB files and objects.json to Saved/HexCaveObjects, then run import_cave_objects.py with the commandlet. It shares the helpers of
generate_planet_objects.py, which it imports without writing the meshes of the surface again, so the surface objects and their import stay apart.

Every mesh stands on its pivot at height 0, in world units: a cell is about 200 across (HexRadius 100, an apothem of about 87), and from the camera
of the planet a pixel covers its distance / 640 units, about 1.9 at 1200, so no visible part is smaller than about 3 units across and no glowing
part smaller than about 6, or it flickers under the temporal filter. The color of a vertex is its color in linear space, as M_HexPlanetCave reads
it (the glTF import undoes the sRGB encoding of vertex colors), varied by instance and a noise, and its alpha is how much it glows in the GlowColor of
its set (UHexPlanetObjectSet): high on the rims and spots of the caps of mushrooms and low on their crowns, so the lit dome and the darker gills
read, rising from the roots of crystals to their tips, 1 in the carved glyphs of rune stones, 0 on rock and bone.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from generate_planet_objects import (Mesh, Random, add, add_tube, bent_path, cross, dot, frame_of, fractal_noise, hash_unit, icosphere, length, lerp,
                                     mul, scale_color, sub, unit, value_noise, write_glb)

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexCaveObjects'))

# warm grey-brown, as the cool placeholder light made the first tones, (0.10, 0.085, 0.07) to (0.30, 0.27, 0.23), read as cold grey spires
ROCK_FOOT = (0.11, 0.08, 0.055)
ROCK_TIP = (0.34, 0.27, 0.18)
CAP = (0.05, 0.36, 0.40)
GILLS = (0.02, 0.17, 0.2)
STEM = (0.12, 0.16, 0.15)
# lighter and warmer than the gravel of the cave floor, as at (0.20, 0.18, 0.15) the pebbles vanished into it
STONE = (0.30, 0.27, 0.22)
SLAB_TOP = (0.32, 0.29, 0.24)
SLAB_SIDE = (0.18, 0.16, 0.13)
# teal, darker at the root, so the glow and the faces of the prisms read rather than a white cut-out, as (0.45, 0.8, 0.8) to white did
CRYSTAL_ROOT = (0.08, 0.30, 0.32)
CRYSTAL_TIP = (0.40, 0.80, 0.80)
CRYSTAL_BASE = (0.08, 0.07, 0.065)
BONE = (0.55, 0.50, 0.40)
BONE_DARK = (0.3, 0.27, 0.2)
SOCKET = (0.06, 0.05, 0.04)
RUNE_STONE = (0.17, 0.16, 0.15)
RUNE_GROOVE = (0.06, 0.06, 0.065)


# --- parts --------------------------------------------------------------------------------------------------------------------------------------

def rotate_z(point, angle):
    c, s = math.cos(angle), math.sin(angle)
    return (point[0] * c - point[1] * s, point[0] * s + point[1] * c, point[2])


def rotate_x(point, angle):
    c, s = math.cos(angle), math.sin(angle)
    return (point[0], point[1] * c - point[2] * s, point[1] * s + point[2] * c)


def rotate_y(point, angle):
    c, s = math.cos(angle), math.sin(angle)
    return (point[0] * c + point[2] * s, point[1], -point[0] * s + point[2] * c)


def add_spire(mesh, base, height, radius, seed, sink=2.0, sides=6, bend=0.14, fractions=None, colors=None):
    """A sharp faceted cone of rock from below the ground to its tip, in rings of `sides` vertices that narrow upwards, its axis bending a little,
    dark at its foot and lighter towards its tip (colors, the foot and the tip, ROCK_FOOT and ROCK_TIP unless given)"""
    foot_color, tip_color = colors if colors else (ROCK_FOOT, ROCK_TIP)
    mesh.begin_part(flat=True)
    random = Random(seed)
    lean = (random.range(-bend, bend), random.range(-bend, bend))
    twist = random.range(0.0, 2.0 * math.pi)
    # taller spires take one more ring, so their sides bend and break
    if fractions is None:
        fractions = [0.0, 0.4, 0.75] if height >= 35.0 else [0.0, 0.55]
    total = height + sink

    def axis_at(f):
        z = -sink + total * f
        return (base[0] + lean[0] * total * f * f, base[1] + lean[1] * total * f * f, base[2] + z)

    rings = []
    for index, f in enumerate(fractions):
        center = axis_at(f)
        ring_radius = radius * (1.0 - f) ** 0.85
        color = lerp(foot_color, tip_color, min(f * 1.1, 1.0))
        ring = []
        for step in range(sides):
            angle = twist + 2.0 * math.pi * (step + 0.3 * (hash_unit(seed, index, step) - 0.5)) / sides
            reach = ring_radius * (0.82 + 0.36 * hash_unit(seed, index, step, 7))
            position = (center[0] + math.cos(angle) * reach, center[1] + math.sin(angle) * reach, center[2])
            ring.append(mesh.vertex(position, scale_color(color, 0.85 + 0.3 * hash_unit(seed, index, step, 3)), 0.0))
        rings.append((ring, center))
    tip_point = axis_at(1.0)
    tip = mesh.vertex(tip_point, tip_color, 0.0)
    for index in range(len(rings) - 1):
        lower, lower_center = rings[index]
        upper, upper_center = rings[index + 1]
        middle = mul(add(lower_center, upper_center), 0.5)
        for step in range(sides):
            following = (step + 1) % sides
            mesh.quad(lower[step], lower[following], upper[following], upper[step], middle)
    top, top_center = rings[-1]
    inside = lerp(top_center, tip_point, 0.3)
    for step in range(sides):
        mesh.triangle(top[step], top[(step + 1) % sides], tip, inside)


def add_lump(mesh, center, radii, color, seed, subdivisions=1, alpha=0.0, bumps=0.3, flat=True, color_at=None):
    """A lumpy rock or bone, an icosphere bent by a noise, flattened at its foot, darker underneath; color_at, if given, colors a vertex by its
    direction from the middle"""
    mesh.begin_part(flat)
    points, faces = icosphere(subdivisions)
    indices = []
    for point in points:
        noise = fractal_noise(add(mul(point, 1.8), (seed * 1.37, seed * 0.71, seed * 2.13)), seed, 3)
        stretch = 1.0 + bumps * (noise * 2.0 - 1.0)
        position = (center[0] + point[0] * radii[0] * stretch, center[1] + point[1] * radii[1] * stretch,
                    center[2] + point[2] * radii[2] * stretch)
        light = 0.6 + 0.4 * (point[2] * 0.5 + 0.5)
        tone = 0.85 + 0.3 * value_noise(point[0] * 2.5 + seed, point[1] * 2.5, point[2] * 2.5, seed + 5)
        base = color_at(point) if color_at else color
        indices.append(mesh.vertex(position, scale_color(base, light * tone), alpha))
    for a, b, c in faces:
        mesh.triangle(indices[a], indices[b], indices[c], center)


def add_cut_stone(mesh, center, radius, seed, color, subdivisions=1, squash=None, cuts=4, depth=(0.55, 0.85)):
    """A stone cut by a few planes (cuts, each at depth of its radius from its middle) into broad split faces, turned at random, a normal per face;
    more and deeper cuts make an angular block"""
    mesh.begin_part(flat=True)
    random = Random(seed)
    points, faces = icosphere(subdivisions)
    planes = []
    for _ in range(cuts):
        normal = unit((random.range(-1.0, 1.0), random.range(-1.0, 1.0), random.range(-0.3, 1.0)))
        planes.append((normal, random.range(depth[0], depth[1])))
    squash = squash if squash is not None else random.range(0.5, 0.75)
    stretch = random.range(1.0, 1.4)
    yaw = random.range(0.0, 2.0 * math.pi)
    tone = scale_color(color, random.range(0.8, 1.2))
    indices = []
    for point in points:
        bump = 0.85 + 0.3 * fractal_noise(mul(point, 2.2), seed, 2)
        p = (point[0] * bump * stretch, point[1] * bump, point[2] * bump * squash)
        for normal, distance in planes:
            over = dot(p, normal) - distance
            if over > 0.0:
                p = sub(p, mul(normal, over))
        p = (p[0], p[1], max(p[2], -0.35 * squash))
        p = rotate_z(mul(p, radius), yaw)
        shade = 0.75 + 0.35 * (point[2] * 0.5 + 0.5)
        indices.append(mesh.vertex(add(center, p), scale_color(tone, shade), 0.0))
    for a, b, c in faces:
        mesh.triangle(indices[a], indices[b], indices[c], center)


def place_in_disc(random, count, radius, spacing_of, tries=400):
    """Points in a disc, the first near its middle, each spacing_of(i, j) from those before where it can be"""
    points = []
    for index in range(count):
        best = None
        for _ in range(tries):
            out = radius * math.sqrt(random.unit()) * (0.25 if index == 0 else 1.0)
            angle = random.range(0.0, 2.0 * math.pi)
            point = (math.cos(angle) * out, math.sin(angle) * out)
            if all(math.hypot(point[0] - other[0], point[1] - other[1]) >= spacing_of(index, other_index) for other_index, other in enumerate(points)):
                best = point
                break
        if best is None:
            continue
        points.append(best)
    return points


# --- stalagmites --------------------------------------------------------------------------------------------------------------------------------

def stalagmites(seed, count, radius, height_range):
    """A clump of sharp spires on a low lumpy rubble base, the tallest near the middle"""
    mesh = Mesh()
    random = Random(seed)
    low, high = height_range
    spots = place_in_disc(random, count, radius, lambda i, j: 0.45 * (radius / math.sqrt(count)) * 1.6)
    for index, (x, y) in enumerate(spots):
        out = math.hypot(x, y) / max(radius, 1e-6)
        height = lerp((high,), (low,), out ** 1.4)[0] * random.range(0.8, 1.08)
        spire_radius = max(height * random.range(0.16, 0.22), 2.0)
        add_spire(mesh, (x, y, 0.0), height, spire_radius, seed * 31 + index)
    add_lump(mesh, (0.0, 0.0, -radius * 0.08), (radius * 0.8, radius * 0.8, radius * 0.16), ROCK_FOOT, seed * 7 + 1, subdivisions=2, bumps=0.35)
    for index in range(4):
        angle = random.range(0.0, 2.0 * math.pi)
        out = radius * random.range(0.75, 1.05)
        add_cut_stone(mesh, (math.cos(angle) * out, math.sin(angle) * out, 0.0), random.range(2.0, 3.5), seed * 11 + index, ROCK_FOOT, subdivisions=0)
    return mesh


def stalagmite_field(seed, count, radius):
    """A field of spires that fills most of a cell, the tallest near the middle, on a broad lumpy base of a few mounds"""
    mesh = Mesh()
    random = Random(seed)
    # dense enough, and tall enough out to its edge, that it reads as one forest of spires filling the cell rather than a sparse clump
    spots = place_in_disc(random, count, radius, lambda i, j: 5.5)
    for index, (x, y) in enumerate(spots):
        out = math.hypot(x, y) / radius
        height = (85.0 - 50.0 * out ** 1.2) * random.range(0.75, 1.08)
        height = max(height, 25.0 * random.range(0.9, 1.1))
        spire_radius = max(height * random.range(0.16, 0.21), 3.0)
        add_spire(mesh, (x, y, 0.0), height, spire_radius, seed * 37 + index, sides=6 if height >= 45.0 else 5)
    for index in range(4):
        angle = 2.0 * math.pi * index / 4 + random.range(-0.4, 0.4)
        out = radius * (0.0 if index == 0 else random.range(0.35, 0.6))
        size = radius * random.range(0.42, 0.55)
        add_lump(mesh, (math.cos(angle) * out, math.sin(angle) * out, -size * 0.1), (size, size, size * 0.18), ROCK_FOOT, seed * 13 + index, subdivisions=1, bumps=0.35)
    return mesh


# --- mushrooms ----------------------------------------------------------------------------------------------------------------------------------

def add_mushroom(mesh, base, cap_radius, stem_height, seed, segments):
    """A glowing mushroom: a pale stem that bends a little, a round cap that glows on top and its gills underneath that glow less"""
    random = Random(seed)
    lean = (random.range(-0.25, 0.25), random.range(-0.25, 0.25), 1.0)
    stem_radius = max(cap_radius * 0.2, 0.8)
    stem = bent_path((base[0], base[1], base[2] - 0.6), lean, stem_height + 0.6, 3, 0.15, seed)
    add_tube(mesh, stem, [stem_radius * 1.25, stem_radius, stem_radius * 0.92, stem_radius * 0.85], STEM, [0.15] * 4, seed, segments=6, cap=False, bark=0.15)
    top = stem[-1]
    cap_height = cap_radius * random.range(0.55, 0.7)
    tilt = (random.range(-0.15, 0.15), random.range(-0.15, 0.15))

    mesh.begin_part()
    # the dome from its rim, a little drooping, to its crown, in rings; it glows most at its rim and on a few spots and least on its crown, so the
    # lit dome reads, where a glow of 1 all over drew flat discs of cyan
    thetas = [1.75, 1.25, 0.7]
    glows = [0.9, 0.45, 0.2]
    rings = []
    for ring_index, theta in enumerate(thetas):
        ring = []
        for step in range(segments):
            phi = 2.0 * math.pi * (step + 0.2 * (hash_unit(seed, ring_index, step) - 0.5)) / segments
            wobble = 1.0 + 0.08 * (hash_unit(seed, ring_index, step, 5) - 0.5)
            x = math.cos(phi) * math.sin(theta) * cap_radius * wobble
            y = math.sin(phi) * math.sin(theta) * cap_radius * wobble
            z = math.cos(theta) * cap_height + (x * tilt[0] + y * tilt[1])
            tone = 0.85 + 0.25 * hash_unit(seed, ring_index, step, 9)
            glow = 0.85 if ring_index > 0 and hash_unit(seed, ring_index, step, 13) > 0.8 else glows[ring_index]
            ring.append(mesh.vertex((top[0] + x, top[1] + y, top[2] + z), scale_color(CAP, tone * (0.85 + 0.15 * ring_index)), glow))
        rings.append(ring)
    crown = mesh.vertex((top[0], top[1], top[2] + cap_height), scale_color(CAP, 1.1), 0.12)
    inside = (top[0], top[1], top[2] + cap_height * 0.2)
    for index in range(len(rings) - 1):
        for step in range(segments):
            following = (step + 1) % segments
            mesh.quad(rings[index][step], rings[index][following], rings[index + 1][following], rings[index + 1][step], inside)
    for step in range(segments):
        mesh.triangle(rings[-1][step], rings[-1][(step + 1) % segments], crown, inside)

    # the gills, from the rim in to the stem, facing down
    mesh.begin_part()
    rim = [mesh.vertex(mesh.positions[index], GILLS, 0.7) for index in rings[0]]
    hub = []
    for step in range(segments):
        phi = 2.0 * math.pi * step / segments
        hub.append(mesh.vertex((top[0] + math.cos(phi) * stem_radius * 1.1, top[1] + math.sin(phi) * stem_radius * 1.1, top[2] + cap_height * 0.05),
                               scale_color(GILLS, 0.7), 0.5))
    above = (top[0], top[1], top[2] + cap_height * 3.0)
    for step in range(segments):
        following = (step + 1) % segments
        mesh.quad(rim[step], rim[following], hub[following], hub[step], above)


def mushrooms(seed, sizes, spread):
    """A clump of glowing mushrooms of the given cap radii, the first in the middle"""
    mesh = Mesh()
    random = Random(seed)
    spots = place_in_disc(random, len(sizes), spread, lambda i, j: (sizes[i] + sizes[j]) * 0.75)
    for index, (x, y) in enumerate(spots):
        cap = sizes[index]
        add_mushroom(mesh, (x, y, 0.0), cap, cap * random.range(1.0, 1.3), seed * 17 + index, 10 if cap >= 6.0 else 8 if cap >= 4.0 else 7)
    return mesh


# --- rubble -------------------------------------------------------------------------------------------------------------------------------------

def pebbles(seed, count):
    """Cut stones of a few sizes lying close together"""
    mesh = Mesh()
    random = Random(seed)
    sizes = sorted((random.range(2.0, 5.0) for _ in range(count)), reverse=True)
    spots = place_in_disc(random, count, 14.0, lambda i, j: (sizes[i] + sizes[j]) * 0.8)
    for index, (x, y) in enumerate(spots):
        add_cut_stone(mesh, (x, y, 0.0), sizes[index], seed * 19 + index, STONE, subdivisions=1 if sizes[index] >= 4.0 else 0)
    return mesh


def clip_polygon(polygon, normal, distance):
    """The part of a convex polygon where dot(normal, p) <= distance"""
    result = []
    for index, point in enumerate(polygon):
        following = polygon[(index + 1) % len(polygon)]
        a = point[0] * normal[0] + point[1] * normal[1] - distance
        b = following[0] * normal[0] + following[1] * normal[1] - distance
        if a <= 0.0:
            result.append(point)
        if (a < 0.0) != (b < 0.0) and abs(a - b) > 1e-9:
            t = a / (a - b)
            result.append((point[0] + (following[0] - point[0]) * t, point[1] + (following[1] - point[1]) * t))
    return result


def add_slab_piece(mesh, outline, thickness, seed, tilt_axis=None, tilt=0.0):
    """A piece of a slab: a convex outline extruded into a plate with a lighter top, turned about an edge by a tilt"""
    mesh.begin_part(flat=True)
    middle = (sum(p[0] for p in outline) / len(outline), sum(p[1] for p in outline) / len(outline))

    def place(x, y, z):
        point = (x, y, z)
        if tilt_axis:
            origin, direction = tilt_axis
            relative = (point[0] - origin[0], point[1] - origin[1], point[2])
            # turned about the axis through the origin along the direction, in the plane
            c, s = math.cos(tilt), math.sin(tilt)
            along = relative[0] * direction[0] + relative[1] * direction[1]
            across = (relative[0] - along * direction[0], relative[1] - along * direction[1])
            side = math.hypot(*across)
            sign = 1.0 if across[0] * -direction[1] + across[1] * direction[0] >= 0.0 else -1.0
            lifted = side * c - relative[2] * s * sign
            z_new = side * s * sign + relative[2] * c
            unit_across = (across[0] / side, across[1] / side) if side > 1e-9 else (0.0, 0.0)
            point = (origin[0] + along * direction[0] + unit_across[0] * lifted, origin[1] + along * direction[1] + unit_across[1] * lifted, z_new)
        return point

    top = [mesh.vertex(place(x, y, thickness * (0.85 + 0.3 * hash_unit(seed, index))), scale_color(SLAB_TOP, 0.85 + 0.3 * hash_unit(seed, index, 2)), 0.0)
           for index, (x, y) in enumerate(outline)]
    bottom = [mesh.vertex(place(x, y, -0.3), SLAB_SIDE, 0.0) for x, y in outline]
    inside = place(middle[0], middle[1], thickness * 0.4)
    for index in range(1, len(outline) - 1):
        mesh.triangle(top[0], top[index], top[index + 1], place(middle[0], middle[1], -5.0))
        mesh.triangle(bottom[0], bottom[index], bottom[index + 1], place(middle[0], middle[1], 5.0))
    for index in range(len(outline)):
        following = (index + 1) % len(outline)
        mesh.quad(bottom[index], bottom[following], top[following], top[index], inside)


def broken_slab(seed):
    """A flat slab 8 by 6 with chipped corners, cracked into three pieces with gaps between them, one of them tilted up"""
    mesh = Mesh()
    random = Random(seed)
    half_x, half_y = 4.0, 3.0
    outline = []
    for cx, cy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        chip_a = random.range(0.4, 1.3)
        chip_b = random.range(0.4, 1.3)
        corner = (cx * half_x, cy * half_y)
        # each corner chipped off along a short cut between its two sides
        if cx * cy > 0:
            outline += [(corner[0], corner[1] - cy * chip_a), (corner[0] - cx * chip_b, corner[1])]
        else:
            outline += [(corner[0] - cx * chip_b, corner[1]), (corner[0], corner[1] - cy * chip_a)]
    first_normal = unit((1.0, random.range(-0.4, 0.4), 0.0))
    first = random.range(-1.2, 0.2)
    second_normal = unit((random.range(-0.3, 0.3), 1.0, 0.0))
    second = random.range(-0.6, 0.6)
    gap = 0.25
    piece_a = clip_polygon(outline, first_normal, first)
    rest = clip_polygon(outline, (-first_normal[0], -first_normal[1]), -(first + gap))
    piece_b = clip_polygon(rest, second_normal, second)
    piece_c = clip_polygon(rest, (-second_normal[0], -second_normal[1]), -(second + gap))
    add_slab_piece(mesh, piece_a, 0.8, seed * 3 + 1)
    add_slab_piece(mesh, piece_b, 0.8, seed * 3 + 2)
    # the third lifted along its crack, as if a root or a fall broke it
    edge_origin = (0.0, second + gap)
    add_slab_piece(mesh, piece_c, 0.8, seed * 3 + 3, tilt_axis=(edge_origin, (1.0, 0.0)), tilt=math.radians(random.range(8.0, 13.0)))
    return mesh


# --- crystals -----------------------------------------------------------------------------------------------------------------------------------

def add_prism(mesh, base, direction, length_value, radius, seed, alpha_root=0.2):
    """A hexagonal crystal with a pyramid tip, from below the ground along a direction, glowing more towards its tip"""
    mesh.begin_part(flat=True)
    axis, side, across = frame_of(direction)
    twist = hash_unit(seed, 1) * math.pi
    rings = []
    for f, color, alpha in ((-0.06, CRYSTAL_ROOT, alpha_root), (0.78, lerp(CRYSTAL_ROOT, CRYSTAL_TIP, 0.7), lerp((alpha_root,), (1.0,), 0.55)[0])):
        center = add(base, mul(axis, length_value * f))
        ring = []
        for step in range(6):
            angle = twist + 2.0 * math.pi * step / 6
            offset = add(mul(side, math.cos(angle) * radius), mul(across, math.sin(angle) * radius))
            ring.append(mesh.vertex(add(center, offset), scale_color(color, 0.9 + 0.2 * hash_unit(seed, step, 3)), alpha))
        rings.append((ring, center))
    tip_point = add(base, mul(axis, length_value))
    tip = mesh.vertex(tip_point, CRYSTAL_TIP, 0.9)
    (lower, lower_center), (upper, upper_center) = rings
    middle = mul(add(lower_center, upper_center), 0.5)
    for step in range(6):
        following = (step + 1) % 6
        mesh.quad(lower[step], lower[following], upper[following], upper[step], middle)
        mesh.triangle(upper[step], upper[following], tip, upper_center)


def crystals(seed, count, small_count):
    """A cluster of long pale teal crystals splayed from a middle on a dark rock base, with small ones at their feet"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(count):
        # the first the longest and most upright, the others shorter and leaning further out
        out = 0.0 if index == 0 else random.range(0.4, 1.0)
        angle = random.range(0.0, 2.0 * math.pi)
        lean = math.radians(lerp((10.0,), (40.0,), out)[0] * random.range(0.8, 1.1))
        direction = (math.sin(lean) * math.cos(angle), math.sin(lean) * math.sin(angle), math.cos(lean))
        base = (math.cos(angle) * out * 3.5, math.sin(angle) * out * 3.5, 0.0)
        size = 38.0 if index == 0 else lerp((30.0,), (8.0,), out)[0] * random.range(0.8, 1.15)
        radius = max(0.8, min(2.6, size * random.range(0.06, 0.075)))
        add_prism(mesh, base, direction, size, radius, seed * 23 + index)
    for index in range(small_count):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(4.5, 7.5)
        lean = math.radians(random.range(30.0, 60.0))
        direction = (math.sin(lean) * math.cos(angle), math.sin(lean) * math.sin(angle), math.cos(lean))
        add_prism(mesh, (math.cos(angle) * out, math.sin(angle) * out, 0.0), direction, random.range(3.0, 6.0), random.range(0.8, 1.1),
                  seed * 29 + index, alpha_root=0.35)
    add_lump(mesh, (0.0, 0.0, -0.8), (7.0, 7.0, 2.2), CRYSTAL_BASE, seed * 5 + 3, subdivisions=1, bumps=0.35)
    for index in range(3):
        angle = random.range(0.0, 2.0 * math.pi)
        add_cut_stone(mesh, (math.cos(angle) * 8.0, math.sin(angle) * 8.0, 0.0), random.range(1.8, 2.8), seed * 41 + index, CRYSTAL_BASE, subdivisions=0)
    return mesh


# --- bones --------------------------------------------------------------------------------------------------------------------------------------

def skull_color(eye_left, eye_right, forward):
    """Colors a point of a skull by its direction from the middle: dark in its sockets, a little darker underneath"""
    def color_at(point):
        for eye in (eye_left, eye_right):
            if dot(point, unit(eye)) > 0.93:
                return SOCKET
        if dot(point, forward) > 0.9:
            return lerp(BONE, BONE_DARK, 0.6)
        return BONE if point[2] > -0.3 else BONE_DARK
    return color_at


def add_bone(mesh, start, end, thickness, seed):
    """A long bone: a shaft with a knob at each end"""
    middle = lerp(start, end, 0.5)
    add_tube(mesh, [start, middle, end], [thickness * 0.55, thickness * 0.45, thickness * 0.55], BONE, [0.0, 0.0, 0.0], seed, segments=5, cap=False, bark=0.1)
    for index, point in enumerate((start, end)):
        knob = thickness * 0.85
        add_lump(mesh, point, (knob, knob, knob * 0.8), lerp(BONE, BONE_DARK, 0.4), seed * 3 + index, subdivisions=0, bumps=0.2, flat=False)


def add_small_skull(mesh, center, radius, yaw, seed):
    forward = (math.cos(yaw), math.sin(yaw), 0.0)
    left = (-forward[1], forward[0], 0.0)
    eyes = (add(mul(forward, 0.8), add(mul(left, 0.45), (0.0, 0.0, 0.3))), add(mul(forward, 0.8), add(mul(left, -0.45), (0.0, 0.0, 0.3))))
    add_lump(mesh, center, (radius * 1.2, radius, radius * 0.85), BONE, seed, subdivisions=1, bumps=0.12, flat=False,
             color_at=skull_color(eyes[0], eyes[1], forward))
    jaw = add(center, add(mul(forward, radius * 0.9), (0.0, 0.0, -radius * 0.45)))
    add_lump(mesh, jaw, (radius * 0.8, radius * 0.6, radius * 0.3), BONE_DARK, seed + 1, subdivisions=0, bumps=0.1, flat=False)


def ribcage(seed):
    """The ribcage of a great beast lying on its back, half sunk: a bent spine of vertebrae, pairs of ribs curving up from it like claws, and its
    skull at one end"""
    mesh = Mesh()
    random = Random(seed)
    spine = []
    for index in range(11):
        t = index / 10.0
        spine.append((-20.0 + 40.0 * t, 3.0 * math.sin(t * math.pi * 1.3 + 0.4), 1.2 + 0.6 * math.sin(t * math.pi)))
    add_tube(mesh, spine, [1.1] * len(spine), BONE_DARK, [0.0] * len(spine), seed, segments=6, cap=False, bark=0.2)
    for index, point in enumerate(spine[:-1]):
        add_lump(mesh, point, (2.0, 1.7, 1.5), lerp(BONE, BONE_DARK, 0.3), seed * 7 + index, subdivisions=0, bumps=0.2, flat=False)
    for pair in range(8):
        t = 0.18 + 0.62 * pair / 7.0
        index = t * 10.0
        low = spine[int(index)]
        high = spine[min(int(index) + 1, 10)]
        root = lerp(low, high, index - int(index))
        rise = 10.0 + 4.0 * math.sin(pair / 7.0 * math.pi) + random.range(-0.8, 0.8)
        for side in (-1.0, 1.0):
            width = (7.0 + 2.0 * math.sin(pair / 7.0 * math.pi)) * random.range(0.9, 1.1)
            points = []
            for step in range(6):
                s = step / 5.0
                # out and up from the spine, then curving back in near the top
                y = side * (1.0 + width * math.sin(s * math.pi * 0.62) - (width * 0.35) * s * s)
                z = root[2] - 0.5 + rise * (1.0 - (1.0 - s) ** 2.2)
                x = root[0] + random.range(-0.3, 0.3) + 1.5 * s * side * 0.0 + 0.8 * s
                points.append((x, root[1] + y, z))
            add_tube(mesh, points, [0.95, 0.9, 0.85, 0.78, 0.68, 0.55], BONE, [0.0] * 6, seed * 11 + pair * 2 + int(side > 0), segments=5, bark=0.12)
    head = add(spine[-1], (5.0, 0.5, 1.0))
    forward = (1.0, 0.0, 0.0)
    eyes = ((0.55, 0.55, 0.5), (0.55, -0.55, 0.5))
    add_lump(mesh, head, (6.0, 4.0, 3.2), BONE, seed * 13, subdivisions=1, bumps=0.15, flat=False, color_at=skull_color(eyes[0], eyes[1], forward))
    add_lump(mesh, add(head, (4.5, 0.0, -2.2)), (5.0, 3.0, 1.1), BONE_DARK, seed * 13 + 1, subdivisions=1, bumps=0.1, flat=False)
    return mesh


def horned_skull(seed):
    """A big horned skull lying on the ground, with dark sockets, a long snout and a jaw, two curved horns and a few bones around it"""
    mesh = Mesh()
    random = Random(seed)
    forward = (1.0, 0.0, 0.0)
    eyes = ((0.5, 0.6, 0.45), (0.5, -0.6, 0.45))
    head = (0.0, 0.0, 3.2)
    add_lump(mesh, head, (7.0, 5.0, 4.5), BONE, seed, subdivisions=2, bumps=0.12, flat=False, color_at=skull_color(eyes[0], eyes[1], forward))
    snout = (7.5, 0.0, 2.0)
    add_lump(mesh, snout, (5.0, 3.0, 2.4), BONE, seed + 1, subdivisions=1, bumps=0.12, flat=False,
             color_at=lambda p: lerp(BONE, SOCKET, 0.8) if dot(p, (0.95, 0.0, 0.3)) > 0.92 else BONE)
    add_lump(mesh, (6.5, 0.0, -0.2), (6.0, 3.2, 1.3), BONE_DARK, seed + 2, subdivisions=1, bumps=0.1, flat=False)
    for side in (-1.0, 1.0):
        root = (-1.5, side * 4.0, 5.5)
        horn = []
        for step in range(6):
            s = step / 5.0
            horn.append((root[0] - 2.0 * s - 3.0 * s * s, root[1] + side * (5.0 * s), root[2] + 6.0 * s - 3.5 * s * s))
        add_tube(mesh, horn, [1.6, 1.4, 1.15, 0.9, 0.6, 0.3], lerp(BONE, BONE_DARK, 0.35), [0.0] * 6, seed * 3 + int(side > 0), segments=6, bark=0.15)
    for index in range(3):
        angle = random.range(0.5, 5.8)
        out = random.range(11.0, 15.0)
        start = (math.cos(angle) * out, math.sin(angle) * out, 0.6)
        turn = random.range(0.0, 2.0 * math.pi)
        size = random.range(5.0, 8.0)
        add_bone(mesh, start, add(start, (math.cos(turn) * size, math.sin(turn) * size, 0.3)), random.range(1.3, 1.7), seed * 5 + index)
    return mesh


def bone_scatter(seed):
    """Bones and two small skulls scattered on the ground"""
    mesh = Mesh()
    random = Random(seed)
    spots = place_in_disc(random, 9, 14.0, lambda i, j: 3.5)
    for index, (x, y) in enumerate(spots):
        if index < 2:
            add_small_skull(mesh, (x, y, 1.8), random.range(2.6, 3.3), random.range(0.0, 2.0 * math.pi), seed * 7 + index)
            continue
        turn = random.range(0.0, 2.0 * math.pi)
        size = random.range(4.0, 9.0)
        start = (x - math.cos(turn) * size * 0.5, y - math.sin(turn) * size * 0.5, 0.5)
        add_bone(mesh, start, add(start, (math.cos(turn) * size, math.sin(turn) * size, random.range(-0.2, 0.4))), random.range(1.2, 1.6), seed * 11 + index)
    return mesh


# --- rune stones --------------------------------------------------------------------------------------------------------------------------------

def rune_stone(seed, runes):
    """A chipped, slightly leaning standing slab 9 wide, 3 thick and 18 high, with a hexagon and three rune strokes carved into both broad faces:
    the floor of a stroke is darker and glows faintly (alpha 1). The strokes are strips just in front of the face, as a groove 0.3 deep shows no
    depth at the distance of the camera, where a pixel covers more than a unit"""
    mesh = Mesh()
    random = Random(seed)
    half_x, half_y = 4.5, 1.5
    heights = [-2.5, 5.0, 11.0, 18.0]
    outline = []
    for cx, cy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        chip = random.range(0.4, 0.9)
        if cx * cy > 0:
            outline += [(cx * half_x, cy * (half_y - chip)), (cx * (half_x - chip), cy * half_y)]
        else:
            outline += [(cx * (half_x - chip), cy * half_y), (cx * half_x, cy * (half_y - chip))]
    vertices_by_ring = []
    mesh.begin_part(flat=True)
    for ring_index, z in enumerate(heights):
        ring = []
        for index, (x, y) in enumerate(outline):
            # the top is broken unevenly, and the sides bulge a little
            if ring_index == len(heights) - 1:
                z_here = z - random.range(0.0, 3.0) * (1.0 if abs(x) > half_x - 1.0 else 0.3)
                shrink = 0.9
            else:
                z_here = z
                shrink = 1.0 + 0.04 * (hash_unit(seed, ring_index, index) - 0.5)
            color = scale_color(RUNE_STONE, 0.85 + 0.3 * hash_unit(seed, ring_index, index, 4))
            ring.append(mesh.vertex((x * shrink, y * shrink, z_here), color, 0.0))
        vertices_by_ring.append(ring)
    count = len(outline)
    for ring_index in range(len(heights) - 1):
        lower, upper = vertices_by_ring[ring_index], vertices_by_ring[ring_index + 1]
        middle = (0.0, 0.0, (heights[ring_index] + heights[ring_index + 1]) * 0.5)
        for index in range(count):
            following = (index + 1) % count
            mesh.quad(lower[index], lower[following], upper[following], upper[index], middle)
    top = vertices_by_ring[-1]
    for index in range(1, count - 1):
        mesh.triangle(top[0], top[index], top[index + 1], (0.0, 0.0, 0.0))

    # the glyphs: a hexagon and the strokes of a rune on each broad face, in the plane of the face (x, z) just in front of it
    hexagon_center = (0.0, 12.0)
    hexagon_radius = 2.8
    strokes = []
    for side in range(6):
        a = math.pi / 6.0 + side * math.pi / 3.0
        b = a + math.pi / 3.0
        strokes.append(((hexagon_center[0] + math.cos(a) * hexagon_radius, hexagon_center[1] + math.sin(a) * hexagon_radius),
                        (hexagon_center[0] + math.cos(b) * hexagon_radius, hexagon_center[1] + math.sin(b) * hexagon_radius)))
    strokes += runes
    for face in (-1.0, 1.0):
        mesh.begin_part(flat=True)
        for (ax, az), (bx, bz) in strokes:
            direction = unit((bx - ax, bz - az, 0.0))
            normal = (-direction[1] * 0.75, direction[0] * 0.75)
            # longer by half the width at each end, so the strokes of the hexagon meet at its corners
            ax2, az2 = ax - direction[0] * 0.75, az - direction[1] * 0.75
            bx2, bz2 = bx + direction[0] * 0.75, bz + direction[1] * 0.75
            y = face * (half_y + 0.08)
            corners = [(ax2 + normal[0], y, az2 + normal[1]), (bx2 + normal[0], y, bz2 + normal[1]), (bx2 - normal[0], y, bz2 - normal[1]),
                       (ax2 - normal[0], y, az2 - normal[1])]
            indices = [mesh.vertex(corner, RUNE_GROOVE, 1.0) for corner in corners]
            mesh.quad(indices[0], indices[1], indices[2], indices[3], (0.0, 0.0, 9.0))

    # it leans a little, the whole stone with its glyphs
    lean_x = math.radians(random.range(3.0, 6.0)) * (1.0 if random.unit() < 0.5 else -1.0)
    lean_y = math.radians(random.range(-3.0, 3.0))
    mesh.positions = [rotate_y(rotate_x(point, lean_x), lean_y) for point in mesh.positions]
    return mesh


RUNES = [
    # an Algiz: a stroke up with two arms
    [((0.0, 2.5), (0.0, 8.0)), ((0.0, 6.0), (-2.4, 8.4)), ((0.0, 6.0), (2.4, 8.4))],
    # a Tiwaz: an arrow pointing up
    [((0.0, 2.5), (0.0, 8.6)), ((0.0, 8.6), (-2.2, 6.4)), ((0.0, 8.6), (2.2, 6.4))],
]


# --- rims of the chasms -------------------------------------------------------------------------------------------------------------------------
# They stand along the sides of the passages towards the voids (EHexObjectLayer::Rim): X runs along the side and -Y looks over the chasm. All take
# M_HexPlanetCave by their vertex colors, grey-brown rock as the stalagmites, as M_HexPlanetBoulder draws the basalt of the cliff without the tint of
# its layer (DA_HexTerrainLayers) and the rims came out nearly black against the lit lip; the blocks of runes glow faintly in their glyphs

RIM_ROCK = (0.23, 0.185, 0.135)
STALACTITE_ROOT = (0.12, 0.095, 0.07)
STALACTITE_TIP = (0.33, 0.27, 0.19)


def add_rim_stone(mesh, center, radius, stretch, squash, cuts, seed, subdivisions=1, yaw_range=0.25):
    """A stone cut by planes into broad split faces, then stretched along X and squashed, so it lies along the lip, turned only a little; cut deep
    and only a little bumpy, so the faces stay flat and their edges sharp, as shallow cuts of a bumpy ball left smooth pale ovals"""
    mesh.begin_part(flat=True)
    random = Random(seed)
    points, faces = icosphere(subdivisions)
    planes = []
    for _ in range(cuts):
        normal = unit((random.range(-1.0, 1.0), random.range(-1.0, 1.0), random.range(-0.3, 1.0)))
        planes.append((normal, random.range(0.45, 0.78)))
    yaw = random.range(-yaw_range, yaw_range)
    indices = []
    for point in points:
        bump = 0.92 + 0.16 * fractal_noise(mul(point, 2.2), seed, 2)
        p = mul(point, bump)
        # cut while round, so the faces stay broad once it is stretched
        for normal, distance in planes:
            over = dot(p, normal) - distance
            if over > 0.0:
                p = sub(p, mul(normal, over))
        p = (p[0] * stretch, p[1], max(p[2], -0.35) * squash)
        p = rotate_z(mul(p, radius), yaw)
        # darker underneath and in patches, lighter on top
        tone = 0.85 + 0.3 * value_noise(point[0] * 2.5 + seed, point[1] * 2.5, point[2] * 2.5, seed + 5)
        indices.append(mesh.vertex(add(center, p), scale_color(RIM_ROCK, (0.7 + 0.45 * (point[2] * 0.5 + 0.5)) * tone), 0.0))
    for a, b, c in faces:
        mesh.triangle(indices[a], indices[b], indices[c], center)


def rim_rock(seed):
    """A low broken slab or lump on the lip: a cut stone stretched 1.8 to 2.6 times along the side and squashed, with a chip broken off at one end.
    Squashed to 0.65 to 0.9 rather than the 0.4 to 0.6 first planned, which left slabs under 2 units high, a pixel at the distance of the camera"""
    mesh = Mesh()
    random = Random(seed)
    radius = random.range(3.5, 5.5)
    stretch = random.range(1.8, 2.6)
    add_rim_stone(mesh, (0.0, 0.0, 0.0), radius, stretch, random.range(0.65, 0.9), 7 + int(random.unit() * 3), seed)
    end = -1.0 if random.unit() < 0.5 else 1.0
    add_rim_stone(mesh, (end * radius * stretch * random.range(0.8, 0.95), random.range(-1.0, 1.0), 0.0), radius * random.range(0.42, 0.55),
                  random.range(1.0, 1.5), random.range(0.5, 0.8), 5, seed * 7 + 1, subdivisions=0, yaw_range=0.8)
    return mesh


def rim_spires(seed, count):
    """One to three sharp five-sided spires of rock, 10 to 26 high and 2 to 4 wide at the foot, leaning a little, on a small broken foot"""
    mesh = Mesh()
    random = Random(seed)
    spots = place_in_disc(random, count, 4.5 if count > 1 else 0.5, lambda i, j: 4.0)
    for index, (x, y) in enumerate(spots):
        height = random.range(19.0, 26.0) if index == 0 else random.range(10.0, 17.0)
        radius = max(2.0, min(4.0, height * random.range(0.14, 0.17)))
        add_spire(mesh, (x, y, 0.0), height, radius, seed * 13 + index, sides=5, bend=0.18, fractions=[0.0, 0.3, 0.6, 0.82])
    add_cut_stone(mesh, (0.0, 0.0, -0.3), 4.5, seed * 5 + 3, ROCK_FOOT, subdivisions=0, squash=0.45)
    return mesh


def add_stalactite(mesh, root, length, radius, seed, sides=5):
    """A sharp faceted cone of rock hanging down from a root, narrowing downwards in rings, bending a little out from the face it hangs on (+Y)"""
    mesh.begin_part(flat=True)
    random = Random(seed)
    lean = (random.range(-0.08, 0.08), random.range(0.0, 0.1))
    twist = random.range(0.0, 2.0 * math.pi)

    def axis_at(f):
        return (root[0] + lean[0] * length * f * f, root[1] + lean[1] * length * f * f, root[2] - length * f)

    rings = []
    for index, f in enumerate((0.0, 0.35, 0.7)):
        center = axis_at(f)
        ring_radius = radius * (1.0 - f) ** 0.9
        color = lerp(STALACTITE_ROOT, STALACTITE_TIP, f)
        ring = []
        for step in range(sides):
            angle = twist + 2.0 * math.pi * (step + 0.3 * (hash_unit(seed, index, step) - 0.5)) / sides
            reach = ring_radius * (0.82 + 0.36 * hash_unit(seed, index, step, 7))
            ring.append(mesh.vertex((center[0] + math.cos(angle) * reach, center[1] + math.sin(angle) * reach, center[2]), color, 0.0))
        rings.append((ring, center))
    tip_point = axis_at(1.0)
    tip = mesh.vertex(tip_point, STALACTITE_TIP, 0.0)
    for index in range(len(rings) - 1):
        upper, upper_center = rings[index]
        lower, lower_center = rings[index + 1]
        middle = mul(add(upper_center, lower_center), 0.5)
        for step in range(sides):
            following = (step + 1) % sides
            mesh.quad(upper[step], upper[following], lower[following], lower[step], middle)
    last, last_center = rings[-1]
    inside = lerp(last_center, tip_point, 0.3)
    for step in range(sides):
        mesh.triangle(last[step], last[(step + 1) % sides], tip, inside)


def stalactites(seed, count):
    """Three to seven cones 6 to 28 long hanging along -Z from a lumpy root band 10 along X, 3 along Y and 3 deep (Z 0 to -3), which lies in front of
    the face of the cliff (+Y) at the height of the lip; the longest near the middle"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(count):
        x = -5.0 + 10.0 * (index + random.range(0.2, 0.8)) / count
        middle = 1.0 - abs(x) / 5.0
        length = lerp((6.0,), (28.0,), middle * random.range(0.6, 1.0))[0]
        radius = max(1.6, min(3.0, length * random.range(0.1, 0.13)))
        add_stalactite(mesh, (x, random.range(0.5, 2.5), -1.0), length, radius, seed * 17 + index)
    add_lump(mesh, (0.0, 1.5, -1.5), (5.5, 1.6, 1.5), STALACTITE_ROOT, seed * 3 + 1, subdivisions=1, bumps=0.3)
    return mesh


def rune_block(seed, runes):
    """A squared block of worked stone, 8 along X, 4 along Y and 6 high, chipped at its corners, with a hexagon and two rune strokes carved into its
    -Y face, which looks over the chasm, and a small hexagon on its top, which the camera above sees: the floor of a stroke is darker and glows
    faintly (alpha 1), drawn as strips just in front of the face, as the rune stones are"""
    mesh = Mesh()
    random = Random(seed)
    half_x, half_y = 4.0, 2.0
    heights = [-1.5, 2.5, 6.0]
    outline = []
    for cx, cy in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        chip = random.range(0.3, 0.8)
        if cx * cy > 0:
            outline += [(cx * half_x, cy * (half_y - chip)), (cx * (half_x - chip), cy * half_y)]
        else:
            outline += [(cx * (half_x - chip), cy * half_y), (cx * half_x, cy * (half_y - chip))]
    mesh.begin_part(flat=True)
    rings = []
    for ring_index, z in enumerate(heights):
        ring = []
        for index, (x, y) in enumerate(outline):
            # a flat top worn back at its edges, and sides that bulge a little
            shrink = 0.92 if ring_index == len(heights) - 1 else 1.0 + 0.03 * (hash_unit(seed, ring_index, index) - 0.5)
            ring.append(mesh.vertex((x * shrink, y * shrink, z), scale_color(RUNE_STONE, 0.85 + 0.3 * hash_unit(seed, ring_index, index, 4)), 0.0))
        rings.append(ring)
    count = len(outline)
    for ring_index in range(len(heights) - 1):
        lower, upper = rings[ring_index], rings[ring_index + 1]
        middle = (0.0, 0.0, (heights[ring_index] + heights[ring_index + 1]) * 0.5)
        for index in range(count):
            following = (index + 1) % count
            mesh.quad(lower[index], lower[following], upper[following], upper[index], middle)
    top = rings[-1]
    for index in range(1, count - 1):
        mesh.triangle(top[0], top[index], top[index + 1], (0.0, 0.0, 0.0))

    def hexagon(center, radius, turn):
        corners = [(center[0] + math.cos(turn + side * math.pi / 3.0) * radius, center[1] + math.sin(turn + side * math.pi / 3.0) * radius)
                   for side in range(6)]
        return [(corners[side], corners[(side + 1) % 6]) for side in range(6)]

    def add_strokes(strokes, place, outside_of, width):
        mesh.begin_part(flat=True)
        half = width * 0.5
        for (ax, az), (bx, bz) in strokes:
            direction = unit((bx - ax, bz - az, 0.0))
            normal = (-direction[1] * half, direction[0] * half)
            ax2, az2 = ax - direction[0] * half, az - direction[1] * half
            bx2, bz2 = bx + direction[0] * half, bz + direction[1] * half
            corners = [(ax2 + normal[0], az2 + normal[1]), (bx2 + normal[0], bz2 + normal[1]), (bx2 - normal[0], bz2 - normal[1]), (ax2 - normal[0], az2 - normal[1])]
            indices = [mesh.vertex(place(u, v), RUNE_GROOVE, 1.0) for u, v in corners]
            mesh.quad(indices[0], indices[1], indices[2], indices[3], outside_of)

    # on the face over the chasm (x, z) at y = -half_y, and a smaller hexagon on the top (x, y)
    # strokes 1.5 wide on the face, inside its edges; the top is narrower, so its hexagon has flat sides along X and thinner strokes
    face = hexagon((-1.4, 2.9), 1.9, math.pi / 6.0) + runes
    add_strokes(face, lambda u, v: (u, -(half_y + 0.08), v), (0.0, 0.0, 2.5), 1.5)
    add_strokes(hexagon((0.0, 0.0), 1.3, 0.0), lambda u, v: (u, v, heights[-1] + 0.08), (0.0, 0.0, 0.0), 1.0)
    return mesh


RIM_RUNES = [
    # a Kaunan beside the hexagon: a stroke up and an arm reaching back to it
    [((2.0, 0.9), (2.0, 5.0)), ((2.0, 3.0), (3.0, 4.6))],
    # an Isa crossed: a stroke up and a short bar across it
    [((2.1, 0.9), (2.1, 5.0)), ((1.4, 3.4), (3.0, 2.6))],
]


# --- masses that fill a cell and block it -------------------------------------------------------------------------------------------------------
# a cell of a passage no hero crosses, which the generation of the underground gives only where no passage gets narrower than two cells: they fill
# most of the cell and stand about as high as the walls, but keep within MASS_REACH of its middle, inside the cell on its sides towards a void
# (an apothem of about 87, less the margin of the footprint)

MASS_REACH = 74.0
# darker than the spires of the floor, as the first masses read as pale cones of plastic: the rock of the walls near them is dark basalt
MASS_FOOT = (0.04, 0.032, 0.024)
MASS_TIP = (0.13, 0.105, 0.08)


def stalagmite_mass(seed, count, radius):
    """A mass of tall stalagmites fused at their feet on a high mound of rock: the tallest, about as high as the walls, near the middle and lower
    towards the edge, a few thick ones among many thin"""
    mesh = Mesh()
    random = Random(seed)
    spots = place_in_disc(random, count, radius, lambda i, j: 7.0)
    for index, (x, y) in enumerate(spots):
        out = math.hypot(x, y) / radius
        height = max((175.0 - 120.0 * out ** 1.1) * random.range(0.7, 1.08), 34.0 * random.range(0.9, 1.15))
        thick = random.range(0.15, 0.2) if random.unit() > 0.25 else random.range(0.24, 0.32)
        spire_radius = max(height * thick, 4.0)
        # the foot of a spire, whose ring reaches up to 1.2 times its radius, and its leaning tip stay within the reach
        room = MASS_REACH - 2.0 - math.hypot(x, y) - 0.08 * height
        if room < 1.2 * spire_radius:
            if room < 1.2 * 3.0:
                continue
            spire_radius = room / 1.2
        tall = height >= 80.0
        add_spire(mesh, (x, y, 0.0), height, spire_radius, seed * 43 + index, sides=7 if tall else 6, bend=0.08,
                  fractions=[0.0, 0.22, 0.45, 0.68, 0.86] if tall else None, colors=(MASS_FOOT, MASS_TIP))
    # a high mound of lumps under them, so the mass stands on solid rock rather than on the floor
    for index in range(7):
        angle = 2.0 * math.pi * index / 7 + random.range(-0.4, 0.4)
        out = radius * (0.0 if index == 0 else random.range(0.3, 0.42))
        size = radius * random.range(0.36, 0.42)
        add_lump(mesh, (math.cos(angle) * out, math.sin(angle) * out, 0.0), (size, size, size * 0.65), MASS_FOOT, seed * 19 + index,
                 subdivisions=2, bumps=0.25)
    return mesh


def crystal_mass(seed, count, small_count):
    """A druse of giant teal crystals splayed from a mound of rock, the longest near the middle and nearly upright, with smaller ones around its
    foot"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(count):
        out = 0.0 if index == 0 else random.range(0.2, 1.0)
        angle = random.range(0.0, 2.0 * math.pi)
        lean = math.radians(lerp((5.0,), (38.0,), out)[0] * random.range(0.8, 1.1))
        direction = (math.sin(lean) * math.cos(angle), math.sin(lean) * math.sin(angle), math.cos(lean))
        base_out = out * 30.0
        size = 165.0 if index == 0 else lerp((135.0,), (55.0,), out)[0] * random.range(0.8, 1.1)
        radius = max(5.0, min(16.0, size * random.range(0.08, 0.1)))
        # the tip within the reach
        room = MASS_REACH - 2.0 - base_out - radius
        if math.sin(lean) * size > room:
            size = room / max(math.sin(lean), 1e-6)
        add_prism(mesh, (math.cos(angle) * base_out, math.sin(angle) * base_out, -6.0), direction, size, radius, seed * 47 + index)
    for index in range(small_count):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(36.0, 50.0)
        lean = math.radians(random.range(25.0, 50.0))
        direction = (math.sin(lean) * math.cos(angle), math.sin(lean) * math.sin(angle), math.cos(lean))
        add_prism(mesh, (math.cos(angle) * out, math.sin(angle) * out, -1.0), direction, random.range(12.0, 24.0), random.range(2.6, 3.8),
                  seed * 53 + index, alpha_root=0.3)
    # a mound of the rock of the cave, as the dark base of the small clusters drew a black disc under a druse this size
    for index in range(5):
        angle = 2.0 * math.pi * index / 5 + random.range(-0.4, 0.4)
        out = 0.0 if index == 0 else random.range(16.0, 26.0)
        size = random.range(26.0, 32.0)
        add_lump(mesh, (math.cos(angle) * out, math.sin(angle) * out, -2.0), (size, size, size * 0.5), ROCK_FOOT, seed * 5 + 7 + index, subdivisions=2,
                 bumps=0.3)
    for index in range(8):
        angle = random.range(0.0, 2.0 * math.pi)
        add_cut_stone(mesh, (math.cos(angle) * 54.0, math.sin(angle) * 54.0, 0.0), random.range(5.0, 8.0), seed * 59 + index, RIM_ROCK, subdivisions=0)
    return mesh


def rockfall(seed, count, small_count):
    """A heap of fallen blocks of rock that fills a cell: big split blocks at its foot, smaller ones stacked on them towards the middle up to a few
    on its top, and rubble around it"""
    mesh = Mesh()
    random = Random(seed)
    # a broad ring of angular blocks at its foot, so it fills the cell rather than standing as a cairn of round stones in its middle, as the
    # first one did, and dark as the rock of the walls
    layers = ((33.0, 46.0, 19.0, 25.0, 0.0), (16.0, 28.0, 16.0, 21.0, 24.0), (6.0, 14.0, 13.0, 17.0, 46.0), (0.0, 6.0, 11.0, 14.0, 66.0))
    shares = (0.5, 0.78, 0.93, 1.0)
    for index in range(count):
        layer = next(each for each, share in enumerate(shares) if index < count * share)
        low_out, high_out, low_size, high_size, z = layers[layer]
        out = random.range(low_out, high_out)
        size = random.range(low_size, high_size)
        angle = random.range(0.0, 2.0 * math.pi)
        color = lerp(MASS_FOOT, MASS_TIP, 0.35 + 0.2 * layer)
        add_cut_stone(mesh, (math.cos(angle) * out, math.sin(angle) * out, z + random.range(-4.0, 4.0)), size, seed * 61 + index, color,
                      subdivisions=1, squash=random.range(0.65, 0.9), cuts=8, depth=(0.42, 0.7))
    for index in range(small_count):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(48.0, 62.0)
        add_cut_stone(mesh, (math.cos(angle) * out, math.sin(angle) * out, 0.0), random.range(3.0, 5.5), seed * 67 + index, RIM_ROCK, subdivisions=0)
    return mesh


# --- output -------------------------------------------------------------------------------------------------------------------------------------

# each mesh: its name, how it is made, its material (cave: M_HexPlanetCave by its vertex colors, rock: M_HexPlanetBoulder in the rock of its cell)
# and its table of LODs (import_cave_objects.py): small for the meshes of many small parts, which keep more of their triangles and switch later
MESHES = [
    ('Stalagmites_0', lambda: stalagmites(301, 6, 18.0, (15.0, 45.0)), 'cave', 'default'),
    ('Stalagmites_1', lambda: stalagmites(307, 9, 24.0, (18.0, 58.0)), 'cave', 'default'),
    ('Stalagmites_2', lambda: stalagmites(311, 13, 30.0, (20.0, 70.0)), 'cave', 'default'),
    ('StalagmiteField_0', lambda: stalagmite_field(313, 40, 50.0), 'cave', 'default'),
    ('StalagmiteField_1', lambda: stalagmite_field(317, 48, 58.0), 'cave', 'default'),
    ('Mushrooms_0', lambda: mushrooms(331, [8.0, 3.5, 3.0, 4.0, 3.2], 12.0), 'cave', 'default'),
    ('Mushrooms_1', lambda: mushrooms(337, [4.0, 3.5, 3.0, 3.8, 3.2, 3.0, 3.6], 9.0), 'cave', 'small'),
    ('Mushrooms_2', lambda: mushrooms(347, [6.0, 4.5, 5.0], 8.0), 'cave', 'default'),
    ('Pebbles_0', lambda: pebbles(349, 8), 'cave', 'small'),
    ('Pebbles_1', lambda: pebbles(353, 10), 'cave', 'small'),
    ('BrokenSlab_0', lambda: broken_slab(359), 'cave', 'default'),
    ('BrokenSlab_1', lambda: broken_slab(367), 'cave', 'default'),
    ('Crystals_0', lambda: crystals(373, 8, 6), 'cave', 'default'),
    ('Crystals_1', lambda: crystals(379, 11, 8), 'cave', 'default'),
    ('Crystals_2', lambda: crystals(383, 14, 10), 'cave', 'default'),
    ('Ribcage_0', lambda: ribcage(389), 'cave', 'default'),
    ('Skull_0', lambda: horned_skull(397), 'cave', 'default'),
    ('BoneScatter_0', lambda: bone_scatter(401), 'cave', 'small'),
    ('RuneStone_0', lambda: rune_stone(409, RUNES[0]), 'cave', 'default'),
    ('RuneStone_1', lambda: rune_stone(419, RUNES[1]), 'cave', 'default'),
    # the rims of the chasms; the spires, stalactites and the glyphs of the blocks are thin parts, which keep more of their triangles. Two meshes a
    # kind, as each mesh is a component, and so draw calls, in every chunk with passages over voids (about 300 more draws on Terra with four rocks,
    # three spires and three stalactites); turned, tilted and sized at random along the lip, more of them did not show
    ('RimRock_0', lambda: rim_rock(431), 'cave', 'default'),
    ('RimRock_1', lambda: rim_rock(439), 'cave', 'default'),
    ('RimSpire_0', lambda: rim_spires(449, 1), 'cave', 'small'),
    ('RimSpire_1', lambda: rim_spires(461, 3), 'cave', 'small'),
    ('Stalactites_0', lambda: stalactites(467, 5), 'cave', 'small'),
    ('Stalactites_1', lambda: stalactites(479, 7), 'cave', 'small'),
    ('RuneBlock_0', lambda: rune_block(487, RIM_RUNES[0]), 'cave', 'small'),
    ('RuneBlock_1', lambda: rune_block(491, RIM_RUNES[1]), 'cave', 'small'),
    # the masses that fill a cell and block it
    ('StalagmiteMass_0', lambda: stalagmite_mass(503, 44, 62.0), 'cave', 'default'),
    ('StalagmiteMass_1', lambda: stalagmite_mass(509, 52, 66.0), 'cave', 'default'),
    ('CrystalMass_0', lambda: crystal_mass(521, 13, 18), 'cave', 'default'),
    ('CrystalMass_1', lambda: crystal_mass(523, 16, 22), 'cave', 'default'),
    ('Rockfall_0', lambda: rockfall(541, 30, 12), 'cave', 'default'),
    ('Rockfall_1', lambda: rockfall(547, 34, 14), 'cave', 'default'),
]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    for old in os.listdir(OUTPUT_FOLDER):
        if old.endswith('.glb'):
            os.remove(os.path.join(OUTPUT_FOLDER, old))

    manifest = {'meshes': []}
    for name, make, material, lods in MESHES:
        asset = 'SM_CaveObject_' + name
        mesh = make()
        triangles = write_glb(mesh, os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset)
        reach = max(math.hypot(p[0], p[1]) for p in mesh.positions)
        top = max(p[2] for p in mesh.positions)
        manifest['meshes'].append({'name': asset, 'material': material, 'triangles': triangles, 'lods': lods})
        print('%s: %d triangles, reach %.1f, height %.1f' % (asset, triangles, reach, top))
    with open(os.path.join(OUTPUT_FOLDER, 'objects.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=2)
    print('Wrote %d meshes to %s' % (len(MESHES), OUTPUT_FOLDER))


if __name__ == '__main__':
    main()
