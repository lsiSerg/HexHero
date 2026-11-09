"""Generates the meshes of the objects of the hex planet: trees, bushes, reeds, flowers, stones, rocks, fallen logs and stumps.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_objects.py
Writes GLB files and objects.json to Saved/HexPlanetObjects, then run import_planet_objects.py with the commandlet. The GLB files open in Blender,
so a mesh can be reworked by hand, or replaced by any other in its UHexPlanetObjectSet.

Every mesh stands on its pivot at height 0, in world units (a cell is about 200 across, a hill 25 high), and is drawn by UHexPlanetObjects as instances.
The color of a vertex is its color in linear space, which M_HexPlanetFoliage varies by instance and breaks by a noise, and its alpha says how leafy
a vertex is and how much it sways in the wind: 0 at the foot of a trunk, 1 in the leaves. Stones and rocks are drawn with M_HexPlanetBoulder, in the
rock of the biome of their cell, so their color is not used. The shapes aim at a realistic look from the camera of the planet, a few hundred units
away: layered skirts of needles for spruces, clumps of leaves on branches for broadleaf trees, split faces for rocks.
"""

import json
import math
import os
import struct

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetObjects'))


# --- small vector helpers -------------------------------------------------------------------------------------------------------------------------

def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def unit(a):
    size = length(a)
    return (a[0] / size, a[1] / size, a[2] / size) if size > 1e-12 else (0.0, 0.0, 1.0)


def lerp(a, b, t):
    return tuple(x + (y - x) * t for x, y in zip(a, b))


def scale_color(color, factor):
    return tuple(min(value * factor, 1.0) for value in color)


def hash_unit(*values):
    """A number in [0, 1) from integers, the same in every run"""
    h = 2166136261
    for value in values:
        h = ((h ^ (int(value) & 0xFFFFFFFF)) * 16777619) & 0xFFFFFFFF
        h ^= h >> 13
        h = (h * 0x5BD1E995) & 0xFFFFFFFF
        h ^= h >> 15
    return h / 4294967296.0


class Random:
    """A small stream of numbers from a seed, so every mesh comes out the same"""

    def __init__(self, seed):
        self.seed = seed
        self.count = 0

    def unit(self):
        self.count += 1
        return hash_unit(self.seed, self.count, 977)

    def range(self, low, high):
        return low + (high - low) * self.unit()

    def pick(self, items):
        return items[min(int(self.unit() * len(items)), len(items) - 1)]


def value_noise(x, y, z, seed):
    """Smooth noise in [0, 1) of a point, from a lattice of hashed values"""
    ix, iy, iz = math.floor(x), math.floor(y), math.floor(z)
    fx, fy, fz = x - ix, y - iy, z - iz
    fx, fy, fz = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy), fz * fz * (3 - 2 * fz)
    result = 0.0
    for dx in (0, 1):
        for dy in (0, 1):
            for dz in (0, 1):
                weight = (fx if dx else 1 - fx) * (fy if dy else 1 - fy) * (fz if dz else 1 - fz)
                result += weight * hash_unit(ix + dx, iy + dy, iz + dz, seed)
    return result


def fractal_noise(point, seed, octaves=3):
    total = 0.0
    amplitude = 1.0
    norm = 0.0
    for octave in range(octaves):
        scale = 2.0 ** octave
        total += amplitude * value_noise(point[0] * scale, point[1] * scale, point[2] * scale, seed + octave * 31)
        norm += amplitude
        amplitude *= 0.5
    return total / norm


# --- mesh -----------------------------------------------------------------------------------------------------------------------------------------

class Mesh:
    """Vertices with a color and alpha each, and triangles, each turned to face away from a reference point; normals are smooth within a part
    and flat for a faceted part"""

    def __init__(self):
        self.positions = []
        self.colors = []
        self.triangles = []
        self.flat = []
        self.part_of = []
        self.parts = 0

    def begin_part(self, flat=False):
        self.parts += 1
        self.current_flat = flat
        return self.parts

    def vertex(self, position, color, alpha):
        self.positions.append(tuple(position))
        self.colors.append((color[0], color[1], color[2], alpha))
        self.part_of.append(self.parts)
        return len(self.positions) - 1

    def triangle(self, a, b, c, outside_of):
        """A triangle whose front faces away from the point outside_of: a triangle shows where (C - A) x (B - A) points"""
        pa, pb, pc = self.positions[a], self.positions[b], self.positions[c]
        normal = cross(sub(pc, pa), sub(pb, pa))
        if length(normal) < 1e-9:
            return
        middle = mul(add(add(pa, pb), pc), 1.0 / 3.0)
        if dot(normal, sub(middle, outside_of)) < 0.0:
            b, c = c, b
        self.triangles.append((a, b, c))
        self.flat.append(self.current_flat)

    def quad(self, a, b, c, d, outside_of):
        self.triangle(a, b, c, outside_of)
        self.triangle(a, c, d, outside_of)

    def build(self):
        """Returns positions, normals, colors and indices, flat parts with a vertex per corner of each triangle"""
        normals = [(0.0, 0.0, 0.0)] * len(self.positions)
        face_normals = []
        for a, b, c in self.triangles:
            pa, pb, pc = self.positions[a], self.positions[b], self.positions[c]
            normal = cross(sub(pc, pa), sub(pb, pa))
            face_normals.append(unit(normal))
            for index in (a, b, c):
                normals[index] = add(normals[index], normal)
        normals = [unit(normal) for normal in normals]

        positions, out_normals, colors, indices = [], [], [], []
        remap = {}
        for triangle, flat, face_normal in zip(self.triangles, self.flat, face_normals):
            for index in triangle:
                if flat:
                    positions.append(self.positions[index])
                    out_normals.append(face_normal)
                    colors.append(self.colors[index])
                    indices.append(len(positions) - 1)
                else:
                    if index not in remap:
                        remap[index] = len(positions)
                        positions.append(self.positions[index])
                        out_normals.append(normals[index])
                        colors.append(self.colors[index])
                    indices.append(remap[index])
        return positions, out_normals, colors, indices


ICOSAHEDRON_POINTS = None


def icosphere(subdivisions):
    golden = (1.0 + math.sqrt(5.0)) * 0.5
    points = [unit(p) for p in [(-1, golden, 0), (1, golden, 0), (-1, -golden, 0), (1, -golden, 0), (0, -1, golden), (0, 1, golden),
                                (0, -1, -golden), (0, 1, -golden), (golden, 0, -1), (golden, 0, 1), (-golden, 0, -1), (-golden, 0, 1)]]
    faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
             (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
    for _ in range(subdivisions):
        middles = {}

        def middle(i, j):
            key = (min(i, j), max(i, j))
            if key not in middles:
                middles[key] = len(points)
                points.append(unit(mul(add(points[i], points[j]), 0.5)))
            return middles[key]

        split = []
        for a, b, c in faces:
            ab, bc, ca = middle(a, b), middle(b, c), middle(c, a)
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        faces = split
    return points, faces


# --- parts ----------------------------------------------------------------------------------------------------------------------------------------

def add_blob(mesh, center, radii, color, alpha, seed, subdivisions=1, bumps=0.28, bump_scale=1.7, flatten=0.55, shade=0.45, flat=False):
    """A lumpy ellipsoid, a clump of leaves or a stone: darker underneath, as little light reaches there, and lighter and darker in patches"""
    mesh.begin_part(flat)
    points, faces = icosphere(subdivisions)
    indices = []
    for point in points:
        noise = fractal_noise(add(mul(point, bump_scale), (seed * 1.37, seed * 0.71, seed * 2.13)), seed, 3)
        stretch = 1.0 + bumps * (noise * 2.0 - 1.0)
        position = (center[0] + point[0] * radii[0] * stretch, center[1] + point[1] * radii[1] * stretch, center[2] + point[2] * radii[2] * stretch)
        position = (position[0], position[1], max(position[2], center[2] - radii[2] * flatten))
        light = (1.0 - shade) + shade * (point[2] * 0.5 + 0.5)
        tone = 0.85 + 0.3 * value_noise(point[0] * 2.5 + seed, point[1] * 2.5, point[2] * 2.5, seed + 5)
        indices.append(mesh.vertex(position, scale_color(color, light * tone), alpha))
    for a, b, c in faces:
        mesh.triangle(indices[a], indices[b], indices[c], center)


def frame_of(direction):
    direction = unit(direction)
    side = unit(cross(direction, (0.0, 0.0, 1.0) if abs(direction[2]) < 0.95 else (1.0, 0.0, 0.0)))
    return direction, side, cross(direction, side)


def add_tube(mesh, path, radii, color, alphas, seed, segments=7, cap=True, bark=0.25, color_at=None):
    """A branch or trunk along a path of points, a ring of vertices at each, with a little noise in the radius as bark, closed at its end"""
    mesh.begin_part()
    rings = []
    for index, point in enumerate(path):
        before = path[max(index - 1, 0)]
        after = path[min(index + 1, len(path) - 1)]
        _, side, across = frame_of(sub(after, before))
        ring = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            wobble = 1.0 + bark * (hash_unit(seed, index, step) - 0.5)
            offset = add(mul(side, math.cos(angle) * radii[index] * wobble), mul(across, math.sin(angle) * radii[index] * wobble))
            position = add(point, offset)
            tone = 0.8 + 0.4 * hash_unit(seed, index, step, 3)
            vertex_color = color_at(position, index / max(len(path) - 1, 1)) if color_at else color
            ring.append(mesh.vertex(position, scale_color(vertex_color, tone), alphas[index]))
        rings.append(ring)
    for index in range(len(path) - 1):
        axis = mul(add(path[index], path[index + 1]), 0.5)
        for step in range(segments):
            following = (step + 1) % segments
            mesh.quad(rings[index][step], rings[index][following], rings[index + 1][following], rings[index + 1][step], axis)
    if cap:
        end = path[-1]
        before = path[-2]
        tip = mesh.vertex(add(end, mul(unit(sub(end, before)), radii[-1] * 0.5)), color, alphas[-1])
        for step in range(segments):
            mesh.triangle(rings[-1][step], rings[-1][(step + 1) % segments], tip, sub(end, mul(unit(sub(end, before)), radii[-1] * 2.0)))


def bent_path(start, direction, total, steps, bend, seed, droop=0.0):
    """Points along a direction that wanders a little, as a trunk or a branch grows, dropping by droop towards its end"""
    points = [start]
    direction = unit(direction)
    for step in range(1, steps + 1):
        t = step / steps
        wander = ((hash_unit(seed, step, 1) - 0.5) * bend, (hash_unit(seed, step, 2) - 0.5) * bend, -droop * t)
        direction = unit(add(direction, wander))
        points.append(add(points[-1], mul(direction, total / steps)))
    return points


def add_skirt(mesh, center, bottom, height, radius, color, seed, segments=10, droop=0.18):
    """A layer of a spruce: a jagged cone of needles from its tip down to a drooping rim, with a hollow underneath"""
    mesh.begin_part()
    tip = mesh.vertex((center[0], center[1], bottom + height), scale_color(color, 1.15), 1.0)
    middle_ring, rim_ring = [], []
    for step in range(segments):
        angle = 2.0 * math.pi * (step + 0.35 * hash_unit(seed, step, 1)) / segments
        reach = radius * (0.72 + 0.4 * hash_unit(seed, step, 2))
        rim_z = bottom - radius * droop * (0.6 + 0.8 * hash_unit(seed, step, 3))
        middle_z = bottom + height * 0.45 + height * 0.06
        middle_ring.append(mesh.vertex((center[0] + math.cos(angle) * reach * 0.5, center[1] + math.sin(angle) * reach * 0.5, middle_z),
                                       scale_color(color, 1.0 + 0.2 * hash_unit(seed, step, 4)), 1.0))
        rim_ring.append(mesh.vertex((center[0] + math.cos(angle) * reach, center[1] + math.sin(angle) * reach, rim_z),
                                    scale_color(color, 0.85 + 0.25 * hash_unit(seed, step, 5)), 1.0))
    inside = (center[0], center[1], bottom + height * 0.15)
    for step in range(segments):
        following = (step + 1) % segments
        mesh.triangle(tip, middle_ring[step], middle_ring[following], inside)
        mesh.quad(middle_ring[step], rim_ring[step], rim_ring[following], middle_ring[following], inside)
    # the underside, darker, as the layers shade each other
    mesh.begin_part()
    under = mesh.vertex((center[0], center[1], bottom + height * 0.3), scale_color(color, 0.45), 1.0)
    under_ring = [mesh.vertex(mesh.positions[index], scale_color(color, 0.5), 1.0) for index in rim_ring]
    above = (center[0], center[1], bottom + height)
    for step in range(segments):
        mesh.triangle(under_ring[step], under_ring[(step + 1) % segments], under, above)


def add_crown(mesh, random, center, spread, count, radius, color, seed, stretch=(1.0, 1.0, 0.85), flat_top=False, droop=0.0):
    """Clumps of leaves around a middle, on the outside of an ellipsoid, each a little other in tone; returns their centers for branches"""
    centers = []
    for index in range(count):
        angle = 2.0 * math.pi * (index + random.range(-0.3, 0.3)) / count
        out = random.range(0.45, 1.0)
        height = random.range(-0.55, 0.6) if not flat_top else random.range(-0.15, 0.2)
        clump = (center[0] + math.cos(angle) * spread[0] * out, center[1] + math.sin(angle) * spread[1] * out, center[2] + height * spread[2] - droop * out)
        size = radius * random.range(0.8, 1.15)
        tone = scale_color(color, random.range(0.85, 1.15))
        add_blob(mesh, clump, (size * stretch[0], size * stretch[1], size * stretch[2]), tone, 1.0, seed * 13 + index, bumps=0.32)
        centers.append(clump)
    # one in the middle on top, so the crown has no hole
    add_blob(mesh, add(center, (0.0, 0.0, spread[2] * (0.45 if not flat_top else 0.25))), mul(stretch, radius * 1.1), color, 1.0, seed * 13 + 99, bumps=0.3)
    return centers


# --- trees ----------------------------------------------------------------------------------------------------------------------------------------

BARK = (0.075, 0.052, 0.034)
DARK_BARK = (0.05, 0.038, 0.028)
PINE_BARK = (0.14, 0.07, 0.04)
DEAD_WOOD = (0.16, 0.145, 0.125)
NEEDLES = (0.04, 0.078, 0.042)
PINE_NEEDLES = (0.042, 0.08, 0.05)
LEAVES = (0.075, 0.13, 0.036)
BIRCH_LEAVES = (0.12, 0.18, 0.05)
OAK_LEAVES = (0.06, 0.11, 0.032)
WILLOW_LEAVES = (0.075, 0.1, 0.04)
ACACIA_LEAVES = (0.085, 0.105, 0.042)


def spruce(seed):
    """A spruce or fir: a straight trunk under layered drooping skirts of needles, narrowing to a spike"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(27.0, 33.0)
    radius = random.range(6.5, 8.0)
    add_tube(mesh, bent_path((0.0, 0.0, -1.0), (0.0, 0.0, 1.0), height * 0.95 + 1.0, 4, 0.04, seed), [1.1, 0.8, 0.55, 0.35, 0.2], DARK_BARK,
             [0.0, 0.1, 0.25, 0.45, 0.6], seed, segments=6)
    layers = 8
    for layer in range(layers):
        f = layer / (layers - 1)
        bottom = height * (0.13 + 0.74 * f)
        add_skirt(mesh, (0.0, 0.0, 0.0), bottom, height * (0.24 - 0.1 * f), radius * (1.0 - f) ** 0.9 + 0.9,
                  scale_color(NEEDLES, 0.85 + 0.35 * f), seed * 17 + layer)
    add_skirt(mesh, (0.0, 0.0, 0.0), height * 0.9, height * 0.12, 0.9, NEEDLES, seed * 17 + 50, segments=6, droop=0.05)
    return mesh


def pine(seed):
    """A pine: a tall bare trunk, reddish higher up, with a few flat dark clumps of needles on its top"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(28.0, 34.0)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.1, 0.1), random.range(-0.1, 0.1), 1.0), height * 0.85, 5, 0.12, seed)

    def bark_at(position, t):
        return lerp(BARK, PINE_BARK, min(t * 1.4, 1.0))

    add_tube(mesh, trunk, [1.2, 1.0, 0.85, 0.7, 0.55, 0.4], BARK, [0.0, 0.05, 0.15, 0.3, 0.5, 0.7], seed, segments=7, color_at=bark_at)
    top = trunk[-1]
    centers = add_crown(mesh, random, add(top, (0.0, 0.0, -1.0)), (5.5, 5.5, 3.0), 5, 3.4, PINE_NEEDLES, seed, stretch=(1.25, 1.25, 0.55))
    for index, clump in enumerate(centers):
        start = trunk[3 + index % 2]
        add_tube(mesh, [start, lerp(start, clump, 0.5), clump], [0.45, 0.3, 0.2], PINE_BARK, [0.4, 0.7, 0.9], seed + index, segments=4, cap=False)
    return mesh


def broadleaf(seed, height_range=(22.0, 27.0), leaves=LEAVES, clumps=8, spread=7.5, clump_radius=5.4, trunk_radius=1.4):
    """A broadleaf tree: a trunk forking into branches that carry clumps of leaves in a round crown"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(*height_range)
    fork = height * random.range(0.22, 0.3)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.15, 0.15), random.range(-0.15, 0.15), 1.0), fork + 1.0, 3, 0.15, seed)
    add_tube(mesh, trunk, [trunk_radius * 1.25, trunk_radius, trunk_radius * 0.85, trunk_radius * 0.7], BARK, [0.0, 0.05, 0.15, 0.3], seed, segments=8)
    crown_middle = (trunk[-1][0], trunk[-1][1], height - spread * 0.95)
    centers = add_crown(mesh, random, crown_middle, (spread, spread, spread * 0.85), clumps, clump_radius, leaves, seed)
    for index, clump in enumerate(centers):
        middle = add(lerp(trunk[-1], clump, 0.5), (0.0, 0.0, 1.5))
        add_tube(mesh, [trunk[-1], middle, lerp(clump, middle, 0.3)], [trunk_radius * 0.6, trunk_radius * 0.4, trunk_radius * 0.25], BARK,
                 [0.3, 0.6, 0.85], seed * 7 + index, segments=5, cap=False)
    return mesh


def birch(seed):
    """A birch: a slender white trunk with dark bands and a narrow light crown"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(22.0, 27.0)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.12, 0.12), random.range(-0.12, 0.12), 1.0), height * 0.8, 5, 0.1, seed)

    def bark_at(position, t):
        band = value_noise(0.0, 0.0, position[2] * 0.9, seed + 3)
        return (0.55, 0.53, 0.5) if band > 0.35 else (0.08, 0.07, 0.06)

    add_tube(mesh, trunk, [0.75, 0.65, 0.55, 0.45, 0.35, 0.25], (0.55, 0.53, 0.5), [0.0, 0.1, 0.2, 0.35, 0.55, 0.75], seed, segments=6, color_at=bark_at)
    middle = (trunk[3][0], trunk[3][1], height * 0.66)
    add_crown(mesh, random, middle, (3.5, 3.5, height * 0.25), 6, 3.0, BIRCH_LEAVES, seed, stretch=(0.9, 0.9, 1.25))
    return mesh


def oak(seed):
    """A giant old tree: a thick short trunk and a broad crown of many clumps, for a lone landmark"""
    return broadleaf(seed, height_range=(33.0, 37.0), leaves=OAK_LEAVES, clumps=9, spread=11.0, clump_radius=6.2, trunk_radius=2.4)


def snag(seed):
    """A dead tree: a gnarled bare trunk broken at its top and a few bare branches"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(18.0, 24.0)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.2, 0.2), random.range(-0.2, 0.2), 1.0), height, 6, 0.35, seed)
    add_tube(mesh, trunk, [1.2, 1.0, 0.85, 0.7, 0.55, 0.45, 0.4], DEAD_WOOD, [0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3], seed, segments=6, bark=0.4)
    for index in range(random.pick([3, 4, 5])):
        start = trunk[2 + index % 4]
        angle = random.range(0.0, 2.0 * math.pi)
        branch = bent_path(start, (math.cos(angle), math.sin(angle), random.range(0.4, 1.0)), random.range(4.0, 8.0), 3, 0.5, seed * 3 + index)
        add_tube(mesh, branch, [0.4, 0.3, 0.2, 0.1], DEAD_WOOD, [0.2, 0.25, 0.3, 0.35], seed * 5 + index, segments=4)
    return mesh


def willow(seed):
    """A swamp willow: a short leaning trunk under a dark crown whose clumps hang down"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(17.0, 21.0)
    lean = (random.range(-0.35, 0.35), random.range(-0.35, 0.35), 1.0)
    trunk = bent_path((0.0, 0.0, -1.0), lean, height * 0.55, 3, 0.3, seed)
    add_tube(mesh, trunk, [1.6, 1.3, 1.0, 0.8], DARK_BARK, [0.0, 0.1, 0.25, 0.4], seed, segments=7, bark=0.45)
    middle = add(trunk[-1], (0.0, 0.0, height * 0.18))
    add_crown(mesh, random, middle, (7.0, 7.0, 3.0), 7, 3.6, WILLOW_LEAVES, seed, stretch=(0.85, 0.85, 1.6), droop=3.0)
    return mesh


def acacia(seed):
    """A tree of dry land: a trunk forking low into spreading branches under a flat wide crown"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(15.0, 19.0)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.2, 0.2), random.range(-0.2, 0.2), 1.0), height * 0.35, 2, 0.2, seed)
    add_tube(mesh, trunk, [1.0, 0.85, 0.7], BARK, [0.0, 0.1, 0.2], seed, segments=6)
    tops = []
    for index in range(3):
        angle = 2.0 * math.pi * index / 3 + random.range(-0.4, 0.4)
        branch = bent_path(trunk[-1], (math.cos(angle) * 0.8, math.sin(angle) * 0.8, 1.0), height * 0.7, 3, 0.15, seed * 3 + index)
        add_tube(mesh, branch, [0.6, 0.45, 0.35, 0.25], BARK, [0.2, 0.4, 0.6, 0.8], seed * 5 + index, segments=5, cap=False)
        tops.append(branch[-1])
    middle = mul(add(add(tops[0], tops[1]), tops[2]), 1.0 / 3.0)
    add_crown(mesh, random, add(middle, (0.0, 0.0, 0.5)), (7.5, 7.5, 1.2), 5, 4.0, ACACIA_LEAVES, seed, stretch=(1.4, 1.4, 0.38), flat_top=True)
    return mesh


# --- low plants -----------------------------------------------------------------------------------------------------------------------------------

def bush(seed, color=(0.05, 0.09, 0.03), size=1.0):
    """A bush: a few clumps of leaves on the ground"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(random.pick([3, 4, 5])):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, 2.6) * size
        radius = random.range(2.0, 3.2) * size
        center = (math.cos(angle) * out, math.sin(angle) * out, radius * random.range(0.55, 0.85))
        add_blob(mesh, center, (radius, radius, radius * 0.85), scale_color(color, random.range(0.8, 1.2)), 0.6, seed * 11 + index, bumps=0.35)
    return mesh


def dry_bush(seed):
    """A bush of dry land: bare twigs fanning out of the ground with a few small dusty clumps"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(random.pick([6, 7, 8])):
        angle = random.range(0.0, 2.0 * math.pi)
        twig = bent_path((0.0, 0.0, -0.3), (math.cos(angle) * 0.7, math.sin(angle) * 0.7, 1.0), random.range(3.5, 5.5), 3, 0.6, seed * 3 + index)
        add_tube(mesh, twig, [0.22, 0.16, 0.1, 0.05], (0.15, 0.11, 0.07), [0.1, 0.4, 0.7, 1.0], seed * 7 + index, segments=4)
        if index % 2 == 0:
            add_blob(mesh, twig[-1], (1.3, 1.3, 1.0), (0.11, 0.1, 0.05), 0.8, seed * 13 + index, bumps=0.4)
    return mesh


def tundra_shrub(seed):
    """Low flat clumps of dwarf birch and heather, olive and rusty"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(random.pick([4, 5, 6])):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, 3.0)
        radius = random.range(1.8, 2.8)
        color = random.pick([(0.085, 0.065, 0.032), (0.06, 0.07, 0.03), (0.1, 0.055, 0.03)])
        add_blob(mesh, (math.cos(angle) * out, math.sin(angle) * out, radius * 0.25), (radius, radius, radius * 0.45), color, 0.4, seed * 11 + index)
    return mesh


def reeds(seed):
    """A clump of reeds and sedge of a swamp: thin blades leaning out of the water"""
    mesh = Mesh()
    random = Random(seed)
    for index in range(random.pick([14, 16, 18])):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, 2.2)
        base = (math.cos(angle) * out, math.sin(angle) * out, -0.3)
        lean = (math.cos(angle) * random.range(0.05, 0.3), math.sin(angle) * random.range(0.05, 0.3), 1.0)
        blade = bent_path(base, lean, random.range(4.5, 8.0), 3, 0.15, seed * 3 + index, droop=0.15)
        color = lerp((0.1, 0.12, 0.04), (0.2, 0.17, 0.07), random.unit())
        add_tube(mesh, blade, [0.18, 0.13, 0.08, 0.02], color, [0.1, 0.4, 0.75, 1.0], seed * 7 + index, segments=3, cap=False, bark=0.1)
        if random.unit() < 0.3:
            add_blob(mesh, blade[-2], (0.25, 0.25, 0.7), (0.09, 0.05, 0.03), 0.9, seed * 13 + index, subdivisions=0, bumps=0.1)
    return mesh


FLOWER_COLORS = [
    [(0.8, 0.58, 0.04), (0.8, 0.8, 0.75)],
    [(0.32, 0.08, 0.5), (0.14, 0.22, 0.65), (0.8, 0.8, 0.75)],
    [(0.68, 0.06, 0.04), (0.8, 0.62, 0.05), (0.8, 0.8, 0.75)],
]


def flowers(seed, palette):
    """A patch of wild flowers: low leafy clumps scattered with blossoms"""
    mesh = Mesh()
    random = Random(seed)
    clumps = []
    for index in range(4):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, 3.0)
        radius = random.range(2.0, 3.0)
        center = (math.cos(angle) * out, math.sin(angle) * out, 0.1)
        clumps.append((center, radius))
        add_blob(mesh, center, (radius, radius, 0.7), (0.06, 0.1, 0.03), 0.3, seed * 11 + index, bumps=0.3)
    for index in range(26):
        center, radius = random.pick(clumps)
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, radius * 0.9)
        point = (center[0] + math.cos(angle) * out, center[1] + math.sin(angle) * out, center[2] + random.range(0.5, 0.95))
        size = random.range(0.3, 0.45)
        add_blob(mesh, point, (size, size, size * 0.6), random.pick(palette), 0.5, seed * 17 + index, subdivisions=0, bumps=0.1, shade=0.2)
    return mesh


# --- stones and wood ------------------------------------------------------------------------------------------------------------------------------

def stone(seed, radius, cuts=4):
    """A stone or rock: a lumpy ball squashed and cut by a few planes, so it has broad split faces, with a normal per face"""
    mesh = Mesh()
    mesh.begin_part(flat=True)
    random = Random(seed)
    points, faces = icosphere(2 if radius > 4.0 else 1)
    planes = []
    for _ in range(cuts):
        normal = unit((random.range(-1.0, 1.0), random.range(-1.0, 1.0), random.range(-0.3, 1.0)))
        planes.append((normal, random.range(0.55, 0.85)))
    squash = random.range(0.55, 0.8)
    stretch = random.range(1.0, 1.4)
    indices = []
    for index, point in enumerate(points):
        bump = 0.85 + 0.3 * fractal_noise(mul(point, 2.2), seed, 2)
        p = (point[0] * bump * stretch, point[1] * bump, point[2] * bump * squash)
        for normal, distance in planes:
            over = dot(p, normal) - distance
            if over > 0.0:
                p = sub(p, mul(normal, over))
        p = (p[0], p[1], max(p[2], -0.35 * squash))
        indices.append(mesh.vertex(mul(p, radius), (0.25, 0.24, 0.22), 0.0))
    for a, b, c in faces:
        mesh.triangle(indices[a], indices[b], indices[c], (0.0, 0.0, 0.0))
    return mesh


def log(seed):
    """A fallen tree: a trunk lying on the ground with its root plate torn out at one end, broken stubs of branches and moss on top"""
    mesh = Mesh()
    random = Random(seed)
    total = random.range(24.0, 30.0)
    radius = random.range(1.4, 1.8)
    path = bent_path((-total * 0.5, 0.0, radius * 0.7), (1.0, 0.0, 0.0), total, 6, 0.1, seed)
    path = [(x, y, radius * 0.7) for x, y, _ in path]

    def mossy(position, t):
        return BARK

    add_tube(mesh, path, [radius * (1.0 - 0.35 * step / 6) for step in range(7)], BARK, [0.0] * 7, seed, segments=8, bark=0.35, color_at=mossy)
    # moss on the upper side: recolor the vertices of the trunk that face up
    for index in range(len(mesh.positions)):
        x, y, z = mesh.positions[index]
        if z > radius * 1.2:
            mesh.colors[index] = (0.05, 0.08, 0.025, 0.0)
    # the root plate, a jagged disc of earth and roots standing up at the thick end
    mesh.begin_part()
    end = path[0]
    rim = []
    plate = random.range(4.0, 5.5)
    for step in range(12):
        angle = 2.0 * math.pi * step / 12
        reach = plate * (0.7 + 0.45 * hash_unit(seed, step, 9))
        rim.append(mesh.vertex((end[0] - 0.6, math.cos(angle) * reach, max(end[2] + math.sin(angle) * reach, -0.5)), (0.09, 0.07, 0.05), 0.0))
    front = mesh.vertex((end[0] - 1.6, 0.0, end[2]), (0.07, 0.055, 0.04), 0.0)
    back = mesh.vertex((end[0] + 0.4, 0.0, end[2]), (0.08, 0.06, 0.045), 0.0)
    for step in range(12):
        following = (step + 1) % 12
        mesh.triangle(rim[step], rim[following], front, (end[0] - 0.6, 0.0, end[2]))
        mesh.triangle(rim[step], rim[following], back, (end[0] - 0.6, 0.0, end[2]))
    for index in range(3):
        start = path[2 + index]
        angle = random.range(0.0, 2.0 * math.pi)
        stub = bent_path(start, (random.range(-0.3, 0.3), math.cos(angle), abs(math.sin(angle)) + 0.2), random.range(2.0, 3.5), 2, 0.3, seed + index)
        add_tube(mesh, stub, [0.45, 0.3, 0.15], BARK, [0.0, 0.0, 0.0], seed * 5 + index, segments=4)
    return mesh


def stump(seed):
    """A stump: a short trunk with its cut, lighter top and roots spreading into the ground"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(3.0, 4.0)
    add_tube(mesh, [(0.0, 0.0, -0.8), (0.0, 0.0, height * 0.5), (0.0, 0.0, height)], [2.6, 2.2, 2.0], BARK, [0.0, 0.0, 0.0], seed, segments=9,
             cap=False, bark=0.3)
    mesh.begin_part()
    top = mesh.vertex((0.0, 0.0, height + 0.15), (0.32, 0.22, 0.12), 0.0)
    rim = []
    for step in range(9):
        angle = 2.0 * math.pi * step / 9
        rim.append(mesh.vertex((math.cos(angle) * 2.0, math.sin(angle) * 2.0, height + random.range(-0.3, 0.3)), (0.26, 0.18, 0.1), 0.0))
    for step in range(9):
        mesh.triangle(rim[step], rim[(step + 1) % 9], top, (0.0, 0.0, 0.0))
    for index in range(5):
        angle = 2.0 * math.pi * index / 5 + random.range(-0.3, 0.3)
        root = bent_path((0.0, 0.0, 0.5), (math.cos(angle), math.sin(angle), -0.35), random.range(3.0, 4.5), 2, 0.3, seed * 3 + index)
        add_tube(mesh, root, [0.8, 0.5, 0.2], DARK_BARK, [0.0, 0.0, 0.0], seed * 7 + index, segments=5)
    return mesh


# --- output ---------------------------------------------------------------------------------------------------------------------------------------

def to_file_axes(x, y, z):
    """The engine imports glTF (x, y, z) as (x, z, y), as for the kit"""
    return x, z, y


def write_glb(mesh, path, name):
    positions, normals, colors, indices = mesh.build()
    positions = [to_file_axes(*(value * 0.01 for value in position)) for position in positions]
    normals = [to_file_axes(*normal) for normal in normals]
    count = len(positions)

    position_data = b''.join(struct.pack('<3f', *value) for value in positions)
    normal_data = b''.join(struct.pack('<3f', *value) for value in normals)
    color_data = b''.join(struct.pack('<4f', *value) for value in colors)
    index_data = struct.pack('<%dI' % len(indices), *indices)
    binary = position_data + normal_data + color_data + index_data

    views = []
    offset = 0
    for data, target in ((position_data, 34962), (normal_data, 34962), (color_data, 34962), (index_data, 34963)):
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
        offset += len(data)

    stored = [struct.unpack('<3f', struct.pack('<3f', *value)) for value in positions]
    document = {
        'asset': {'version': '2.0', 'generator': 'generate_planet_objects.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': name}],
        'meshes': [{'name': name, 'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'COLOR_0': 2}, 'indices': 3}]}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': [min(value[axis] for value in stored) for axis in range(3)], 'max': [max(value[axis] for value in stored) for axis in range(3)]},
            {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': count, 'type': 'VEC4'},
            {'bufferView': 3, 'componentType': 5125, 'count': len(indices), 'type': 'SCALAR'},
        ],
        'bufferViews': views,
        'buffers': [{'byteLength': len(binary)}],
    }

    header = json.dumps(document, separators=(',', ':')).encode()
    header += b' ' * (-len(header) % 4)
    binary += b'\0' * (-len(binary) % 4)
    with open(path, 'wb') as file:
        file.write(struct.pack('<III', 0x46546C67, 2, 28 + len(header) + len(binary)))
        file.write(struct.pack('<II', len(header), 0x4E4F534A) + header)
        file.write(struct.pack('<II', len(binary), 0x004E4942) + binary)
    return len(indices) // 3


# each mesh: its name, how it is made, and its material: foliage for plants and wood, stone for stones and rocks
MESHES = [
    ('Spruce_0', lambda: spruce(11), 'foliage'),
    ('Spruce_1', lambda: spruce(23), 'foliage'),
    ('Spruce_2', lambda: spruce(37), 'foliage'),
    ('Pine_0', lambda: pine(41), 'foliage'),
    ('Pine_1', lambda: pine(53), 'foliage'),
    ('Broadleaf_0', lambda: broadleaf(61), 'foliage'),
    ('Broadleaf_1', lambda: broadleaf(73), 'foliage'),
    ('Broadleaf_2', lambda: broadleaf(89), 'foliage'),
    ('Birch_0', lambda: birch(97), 'foliage'),
    ('Birch_1', lambda: birch(101), 'foliage'),
    ('Oak_0', lambda: oak(107), 'foliage'),
    ('Snag_0', lambda: snag(113), 'foliage'),
    ('Snag_1', lambda: snag(127), 'foliage'),
    ('Willow_0', lambda: willow(131), 'foliage'),
    ('Acacia_0', lambda: acacia(137), 'foliage'),
    ('Acacia_1', lambda: acacia(149), 'foliage'),
    ('Bush_0', lambda: bush(151), 'foliage'),
    ('Bush_1', lambda: bush(163, (0.06, 0.095, 0.035), 1.1), 'foliage'),
    ('Bush_2', lambda: bush(173, (0.045, 0.075, 0.028), 0.9), 'foliage'),
    ('DryBush_0', lambda: dry_bush(179), 'foliage'),
    ('DryBush_1', lambda: dry_bush(181), 'foliage'),
    ('TundraShrub_0', lambda: tundra_shrub(191), 'foliage'),
    ('TundraShrub_1', lambda: tundra_shrub(193), 'foliage'),
    ('Reeds_0', lambda: reeds(197), 'foliage'),
    ('Reeds_1', lambda: reeds(199), 'foliage'),
    ('Flowers_0', lambda: flowers(211, FLOWER_COLORS[0]), 'foliage'),
    ('Flowers_1', lambda: flowers(223, FLOWER_COLORS[1]), 'foliage'),
    ('Flowers_2', lambda: flowers(227, FLOWER_COLORS[2]), 'foliage'),
    ('Stone_0', lambda: stone(229, 2.2), 'stone'),
    ('Stone_1', lambda: stone(233, 2.0, 3), 'stone'),
    ('Stone_2', lambda: stone(239, 2.6), 'stone'),
    ('Stone_3', lambda: stone(241, 1.8, 5), 'stone'),
    ('Rock_0', lambda: stone(251, 7.0, 5), 'stone'),
    ('Rock_1', lambda: stone(257, 6.5, 6), 'stone'),
    ('Rock_2', lambda: stone(263, 7.5, 4), 'stone'),
    ('Log_0', lambda: log(269), 'foliage'),
    ('Log_1', lambda: log(271), 'foliage'),
    ('Stump_0', lambda: stump(277), 'foliage'),
]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    for old in os.listdir(OUTPUT_FOLDER):
        if old.endswith('.glb'):
            os.remove(os.path.join(OUTPUT_FOLDER, old))

    manifest = {'meshes': []}
    for name, make, material in MESHES:
        asset = 'SM_PlanetObject_' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset)
        manifest['meshes'].append({'name': asset, 'material': material, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'objects.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=2)
    print('Wrote %d meshes to %s' % (len(MESHES), OUTPUT_FOLDER))


# generate_cave_objects.py imports the helpers above without writing these meshes again
if __name__ == '__main__':
    main()
