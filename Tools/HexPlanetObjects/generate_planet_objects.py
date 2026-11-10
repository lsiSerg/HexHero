"""Generates the meshes of the objects of the hex planet: trees, bushes, reeds, flowers, grass, wheat, stones, rocks, fallen logs and stumps.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_objects.py
Writes GLB files and objects.json to Saved/HexPlanetObjects, then run generate_foliage_atlas.py, which draws the atlas their leaves show, and
import_planet_objects.py with the commandlet. The GLB files open in Blender, so a mesh can be reworked by hand, or replaced by any other in its
UHexPlanetObjectSet.

Every mesh stands on its pivot at height 0, in world units (a cell is about 200 across, a hill 25 high), and is drawn by UHexPlanetObjects as instances.
A plant is wood and cards. The wood, a trunk that forks into limbs and twigs, is solid: its UV lies outside the atlas (SOLID_UV), where
M_HexPlanetFoliage draws the color of the vertices alone. A card is a small sheet that shows one tile of the atlas T_HexFoliage (foliage_tiles.py),
a spray of leaves, a bough of needles or a tuft of grass, cut by its alpha: the cards of a crown lie like shingles on the lobes of its limbs, and
their normals point away from the middle of the crown, so it is lit as one round volume and not as sheets. The color of a vertex is its color in
linear space, which the tile of a card multiplies, darker inside and under a crown, where little light reaches, and M_HexPlanetFoliage varies by
instance; its alpha says how much it sways in the wind: 0 at the foot of a trunk, 1 at the tips of the leaves.

A mesh of cards is written three times, the whole one and two with fewer and larger cards (_LOD1, _LOD2), which the import gives it as its LODs,
as reducing the triangles of cards tears the crowns apart. Stones and rocks are drawn with M_HexPlanetBoulder, in the rock of the biome of their
cell, so their color is not used, and they and the logs keep LODs reduced from their triangles.
"""

import json
import math
import os
import struct

from foliage_tiles import tile_uv

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetObjects'))

# the UV of every vertex that is no card: outside the atlas, where M_HexPlanetFoliage draws the color of the vertex alone, whole
SOLID_UV = (2.5, 0.5)


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


def smooth(t):
    t = min(max(t, 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


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

    def jitter(self, amount):
        return (self.range(-amount, amount), self.range(-amount, amount), self.range(-amount, amount))


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
    """Vertices with a color, an alpha and a UV each, and triangles, each turned to face away from a reference point; normals are smooth within
    a part and flat for a faceted part, but for a vertex given its own, as those of a card"""

    def __init__(self):
        self.positions = []
        self.colors = []
        self.uvs = []
        self.own_normals = {}
        self.triangles = []
        self.flat = []
        self.part_of = []
        self.parts = 0
        self.current_flat = False

    def begin_part(self, flat=False):
        self.parts += 1
        self.current_flat = flat
        return self.parts

    def vertex(self, position, color, alpha, uv=SOLID_UV, normal=None):
        self.positions.append(tuple(position))
        self.colors.append((color[0], color[1], color[2], alpha))
        self.uvs.append(uv)
        self.part_of.append(self.parts)
        if normal is not None:
            self.own_normals[len(self.positions) - 1] = normal
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

    def sheet(self, a, b, c):
        """A triangle of a card, seen from both its sides, so which way it winds does not matter"""
        self.triangles.append((a, b, c))
        self.flat.append(False)

    def build(self):
        """Returns positions, normals, colors, UVs and indices, flat parts with a vertex per corner of each triangle"""
        normals = [(0.0, 0.0, 0.0)] * len(self.positions)
        face_normals = []
        for a, b, c in self.triangles:
            pa, pb, pc = self.positions[a], self.positions[b], self.positions[c]
            normal = cross(sub(pc, pa), sub(pb, pa))
            face_normals.append(unit(normal))
            for index in (a, b, c):
                normals[index] = add(normals[index], normal)
        normals = [unit(normal) for normal in normals]
        for index, normal in self.own_normals.items():
            normals[index] = unit(normal)

        positions, out_normals, colors, uvs, indices = [], [], [], [], []
        remap = {}
        for triangle, flat, face_normal in zip(self.triangles, self.flat, face_normals):
            for index in triangle:
                if flat:
                    positions.append(self.positions[index])
                    out_normals.append(face_normal)
                    colors.append(self.colors[index])
                    uvs.append(self.uvs[index])
                    indices.append(len(positions) - 1)
                else:
                    if index not in remap:
                        remap[index] = len(positions)
                        positions.append(self.positions[index])
                        out_normals.append(normals[index])
                        colors.append(self.colors[index])
                        uvs.append(self.uvs[index])
                    indices.append(remap[index])
        return positions, out_normals, colors, uvs, indices


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
    """A lumpy ellipsoid, a stone or a clump of moss: darker underneath, as little light reaches there, and lighter and darker in patches"""
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


def add_card(mesh, base, along, face, long, width, tile, color, sway=(0.5, 1.0), bend=0.0, rows=2, round_to=None, normal_to=None, rounding=0.6,
             shades=(0.82, 1.0), mirror=False):
    """A card that shows a tile of the atlas: from the foot of the tile at base it runs along a direction, as wide as width across it, and faces
    the direction face, away from which its far end droops by bend. Its normals turn by rounding towards the way from the point round_to, the
    middle of a crown, or towards the direction normal_to, as up for grass, so many cards are lit as one soft volume. Its color goes from the
    first of shades at its foot to the second at its end, and its sway alike."""
    along = unit(along)
    side = cross(face, along)
    if length(side) < 1e-4:
        _, side, _ = frame_of(along)
    side = unit(side)
    normal = unit(cross(along, side))
    u0, v0, u1, v1 = tile_uv(tile)
    if mirror:
        u0, u1 = u1, u0
    mesh.begin_part()
    grid = []
    for row in range(rows + 1):
        t = row / rows
        middle = add(add(base, mul(along, long * t)), mul(normal, -bend * long * t * t))
        for across in (-0.5, 0.5):
            position = add(middle, mul(side, width * across))
            own = normal
            if round_to is not None:
                own = unit(lerp(normal, unit(sub(position, round_to)), rounding))
            elif normal_to is not None:
                own = unit(lerp(normal, normal_to, rounding))
            shade = shades[0] + (shades[1] - shades[0]) * t
            grid.append(mesh.vertex(position, scale_color(color, shade), sway[0] + (sway[1] - sway[0]) * t,
                                    (u0 + (u1 - u0) * (across + 0.5), v1 + (v0 - v1) * t), own))
    for row in range(rows):
        a, b, c, d = grid[row * 2], grid[row * 2 + 1], grid[row * 2 + 3], grid[row * 2 + 2]
        mesh.sheet(a, b, c)
        mesh.sheet(a, c, d)


UP = (0.0, 0.0, 1.0)


def add_standing_card(mesh, foot, yaw, height, width, tile, color, lean=0.0, rows=1, shades=(0.7, 1.0), mirror=False, sway=(0.0, 1.0)):
    """A card that stands on the ground and leans the way it faces, as a tuft of grass; its normals point mostly up, so the sun lights it as
    it lights the ground under it, whichever way it faces"""
    facing = (math.cos(yaw), math.sin(yaw), 0.0)
    along = (facing[0] * math.sin(lean), facing[1] * math.sin(lean), math.cos(lean))
    add_card(mesh, foot, along, facing, height, width, tile, color, sway=sway, rows=rows, normal_to=UP, rounding=0.8, shades=shades, mirror=mirror)


def add_lying_card(mesh, center, size, tile, color, yaw=0.0, tilt=(0.0, 0.0), sway=0.0, shades=(1.0, 1.0), mirror=False):
    """A card that lies over the ground, facing up, as a tuft of grass or blossoms seen from above, leaning a little by tilt"""
    face = unit((tilt[0], tilt[1], 1.0))
    along = (math.cos(yaw), math.sin(yaw), 0.0)
    along = unit(sub(along, mul(face, dot(along, face))))
    add_card(mesh, sub(center, mul(along, size * 0.5)), along, face, size, size, tile, color, sway=(sway, sway), rows=1, normal_to=UP, rounding=0.5,
             shades=shades, mirror=mirror)


def sphere_points(count, seed):
    """Directions spread evenly over a sphere, turned by the seed"""
    golden = math.pi * (3.0 - math.sqrt(5.0))
    turn = hash_unit(seed, 71) * 2.0 * math.pi
    points = []
    for index in range(count):
        z = 1.0 - 2.0 * (index + 0.5) / count
        ring = math.sqrt(max(1.0 - z * z, 0.0))
        angle = golden * index + turn
        points.append((math.cos(angle) * ring, math.sin(angle) * ring, z))
    return points


# --- trees ----------------------------------------------------------------------------------------------------------------------------------------

BARK = (0.075, 0.052, 0.034)
DARK_BARK = (0.05, 0.038, 0.028)
PINE_BARK = (0.14, 0.07, 0.04)
DEAD_WOOD = (0.16, 0.145, 0.125)
# the colors of leaves are lighter than the leaves come out, as the tiles of the atlas, nearly white, darken them by about a third
NEEDLES = (0.058, 0.115, 0.064)
PINE_NEEDLES = (0.07, 0.135, 0.082)
LEAVES = (0.125, 0.225, 0.056)
LINDEN_LEAVES = (0.105, 0.205, 0.062)
BIRCH_LEAVES = (0.19, 0.285, 0.07)
OAK_LEAVES = (0.1, 0.19, 0.05)
WILLOW_LEAVES = (0.145, 0.205, 0.072)
ACACIA_LEAVES = (0.145, 0.19, 0.066)
BUSH_LEAVES = (0.12, 0.21, 0.058)
GRASS = (0.22, 0.36, 0.08)
WHITE = (0.85, 0.85, 0.85)

# how many of its cards a mesh keeps at each LOD, as one in so many, and how much larger those are, so a crown stays as full
LOD_STRIDE = (1, 2, 5)
LOD_GROWTH = (1.0, 1.4, 2.1)
TREE_SCREENS = [1.0, 0.045, 0.02]
PLANT_SCREENS = [1.0, 0.03, 0.012]


def at_height(path, height):
    """The point of a path that rises, such as a trunk, at a height"""
    for a, b in zip(path, path[1:]):
        if b[2] >= height and b[2] > a[2]:
            return lerp(a, b, min(max((height - a[2]) / (b[2] - a[2]), 0.0), 1.0))
    return path[-1]


def add_lobe_cards(mesh, lod, seed, lobes, index, crown_center, crown_bottom, crown_top, tiles, color, card, per_lobe, hang, axis, twig_color,
                   droop=0.12, hide=0.66):
    """The leaves of one lobe of a crown: cards over the part of its surface that no other lobe hides, darker low in the crown. Two of three
    lie on it like shingles, pointing out and a little down, and the third stands out of it as the end of a twig does, so the outline of a
    crown is leaves and not a smooth ball. A few dark ones inside close the view through it, and thin twigs run to some"""
    center, radius, tone = lobes[index]
    random = Random(seed * 31 + index * 7 + 5)
    cards = []
    for direction in sphere_points(per_lobe, seed + index * 3):
        out = unit(add(direction, random.jitter(0.25)))
        reach = random.range(0.78, 1.04)
        point = (center[0] + out[0] * radius * reach, center[1] + out[1] * radius * reach, center[2] + out[2] * radius * reach * 0.88)
        size = random.range(card[0] * 0.85, card[1] * 1.2)
        spin = random.jitter(0.6)
        tint = random.range(0.88, 1.12)
        mirror = random.unit() < 0.5
        tile = random.pick(tiles)
        if out[2] < -0.6:
            continue
        if any(other != index and length(sub(point, lobes[other][0])) < lobes[other][1] * hide for other in range(len(lobes))):
            continue
        cards.append((out, point, size, spin, tint, mirror, tile))

    stride = LOD_STRIDE[lod]
    growth = LOD_GROWTH[lod]
    for number, (out, point, size, spin, tint, mirror, tile) in enumerate(cards):
        if number % stride != stride // 2:
            continue
        size *= growth
        if number % 3 == 1 and lod < 2:
            # the end of a twig, standing out of the lobe
            along = unit(add(add(out, (0.0, 0.0, -hang * 0.6)), mul(spin, 0.5)))
            face = add(sub(UP, mul(along, along[2])), mul(spin, 0.7))
            base = sub(point, mul(along, size * 0.62))
        else:
            face = unit(add(add(out, (0.0, 0.0, 0.3)), mul(spin, 0.75)))
            outward = unit((point[0] - axis[0], point[1] - axis[1], 0.0))
            wish = add(add(mul(outward, 0.7), (0.0, 0.0, -hang)), spin)
            along = unit(sub(wish, mul(face, dot(wish, face))))
            base = sub(point, mul(along, size * 0.45))
        # little light reaches the lower and inner leaves of a crown
        high = smooth((point[2] - crown_bottom) / max(crown_top - crown_bottom, 1.0))
        outer = smooth(length(sub(point, crown_center)) / max(radius * 2.2, 1.0))
        light = (0.66 + 0.4 * high) * (0.86 + 0.14 * outer)
        add_card(mesh, base, along, face, size, size * 0.92, tile, scale_color(color, tone * tint * light), sway=(0.55, 1.0), bend=droop,
                 rows=2 if lod == 0 else 1, round_to=lerp(center, crown_center, 0.55), rounding=0.7, mirror=mirror)
        if lod == 0 and number % 3 == 0:
            add_tube(mesh, [center, lerp(center, base, 0.6), base], [0.2, 0.14, 0.08], twig_color, [0.5, 0.6, 0.7], seed * 3 + number, segments=3,
                     cap=False, bark=0.1)
    if lod < 2:
        # cards inside the lobe, dark, so the sky does not show through the gaps between its leaves
        for inner in range(2):
            face = unit(random.jitter(1.0))
            along = unit(cross(face, (0.3, 0.5, 0.8)))
            size = radius * 1.5
            add_card(mesh, sub(center, mul(along, size * 0.5)), along, face, size, size, tiles[0], scale_color(color, tone * 0.55), sway=(0.6, 0.6),
                     rows=1, round_to=crown_center, rounding=0.5, shades=(1.0, 1.0))


def leafy_tree(seed, lod, height=(23.0, 28.0), fork=(0.26, 0.34), spread=(9.0, 10.5), lobe_count=8, lobe_size=(0.4, 0.5), tiles=('LeafA',), leaves=LEAVES,
               bark=BARK, trunk_radius=1.15, card=(5.0, 6.4), per_lobe=14, hang=0.3, leader=0.45, lean=0.08, bark_at=None, flare=1.5, top_lobe=0.5,
               lobe_out=(0.52, 0.68), hide=0.66):
    """A broadleaf tree: a trunk that forks into limbs, each carrying a lobe of leaves, which together make a lumpy crown with gaps, as a real
    crown has, and not a ball"""
    mesh = Mesh()
    random = Random(seed)
    total = random.range(*height)
    radius = random.range(*spread)
    bottom = total * random.range(*fork)
    half = (total - bottom) * 0.5
    middle = bottom + half

    # the trunk up to where it forks, and thinner on through the crown as its leader
    leader_top = middle + half * leader
    direction = (random.range(-lean, lean), random.range(-lean, lean), 1.0)
    steps = 5 if lod == 0 else 3
    trunk = bent_path((0.0, 0.0, -1.0), direction, leader_top + 1.0, steps, 0.16, seed)
    radii = [trunk_radius * (flare if step == 0 else 1.0 - 0.75 * step / steps) for step in range(steps + 1)]
    add_tube(mesh, trunk, radii, bark, [min(0.5 * step / steps, 0.5) for step in range(steps + 1)], seed, segments=8 if lod == 0 else 6 if lod == 1 else 4,
             bark=0.22, color_at=bark_at)
    axis = at_height(trunk, middle)
    crown_center = (axis[0], axis[1], middle)

    # the lobes: one at the end of each limb, spread around the trunk and up the crown, and one on its top
    lobes = []
    golden = math.pi * (3.0 - math.sqrt(5.0))
    turn = random.range(0.0, 2.0 * math.pi)
    for index in range(lobe_count):
        level = ((index * 0.618034 + 0.3) % 1.0) * 1.05 - 0.5
        angle = golden * index + turn + random.range(-0.3, 0.3)
        out = radius * random.range(*lobe_out) * math.sqrt(max(1.0 - level * level * 1.6, 0.15))
        center = (axis[0] + math.cos(angle) * out, axis[1] + math.sin(angle) * out, middle + level * half * 1.05)
        lobes.append((center, radius * random.range(*lobe_size), random.range(0.88, 1.12)))
    lobes.append(((axis[0] + random.range(-1.0, 1.0), axis[1] + random.range(-1.0, 1.0), middle + half * 0.55), radius * top_lobe, random.range(0.95, 1.1)))

    for index, (center, lobe_radius, tone) in enumerate(lobes):
        if lod < 2 and index < lobe_count:
            start = at_height(trunk, min(max(center[2] - lobe_radius * 0.9, bottom * 0.8), leader_top - 0.5))
            bow = lerp(start, center, 0.55)
            bow = (bow[0], bow[1], bow[2] + length(sub(center, start)) * 0.12)
            add_tube(mesh, [start, bow, center], [trunk_radius * 0.5, trunk_radius * 0.34, trunk_radius * 0.16], bark, [0.3, 0.45, 0.6], seed * 7 + index,
                     segments=5 if lod == 0 else 4, cap=False, color_at=bark_at)
        add_lobe_cards(mesh, lod, seed, lobes, index, crown_center, bottom, total, tiles, leaves, card, per_lobe, hang, axis, bark, hide=hide)
    return mesh


def broadleaf(seed, lod, tiles=('LeafA',), leaves=LEAVES):
    return leafy_tree(seed, lod, tiles=tiles, leaves=leaves)


def birch(seed, lod):
    """A birch: a slender white trunk with dark marks under a narrow light crown whose twigs hang"""
    def bark_at(position, t):
        band = value_noise(position[0] * 2.0, position[1] * 2.0, position[2] * 0.9, seed + 3)
        return (0.62, 0.6, 0.56) if band > 0.36 else (0.08, 0.07, 0.06)

    return leafy_tree(seed, lod, height=(24.0, 29.0), fork=(0.3, 0.38), spread=(4.6, 5.4), lobe_count=7, lobe_size=(0.55, 0.7), tiles=('Birch',),
                      leaves=BIRCH_LEAVES, bark=(0.62, 0.6, 0.56), trunk_radius=0.7, card=(4.2, 5.4), per_lobe=12, hang=0.8, leader=0.85, lean=0.07,
                      bark_at=bark_at, flare=1.2, top_lobe=0.6, lobe_out=(0.45, 0.6), hide=0.45)


def oak(seed, lod):
    """A giant old tree: a thick short trunk with a wide foot and a broad crown of many lobes, for a lone landmark"""
    return leafy_tree(seed, lod, height=(33.0, 37.0), fork=(0.22, 0.28), spread=(13.5, 15.0), lobe_count=11, lobe_size=(0.36, 0.46), tiles=('Oak',),
                      leaves=OAK_LEAVES, bark=DARK_BARK, trunk_radius=2.3, card=(6.6, 8.4), per_lobe=14, hang=0.25, leader=0.3, lean=0.08, flare=1.45,
                      top_lobe=0.45)


def spruce(seed, lod):
    """A spruce or fir: a straight trunk with whorls of boughs that droop and shorten towards its spike, each bough a fan of needles"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(27.0, 33.0)
    radius = random.range(6.8, 8.2)
    steps = 4 if lod < 2 else 2
    add_tube(mesh, bent_path((0.0, 0.0, -1.0), (0.0, 0.0, 1.0), height * 0.96 + 1.0, steps, 0.04, seed), [1.1 - 0.95 * step / steps for step in range(steps + 1)],
             DARK_BARK, [0.6 * step / steps for step in range(steps + 1)], seed, segments=6 if lod == 0 else 4)

    whorls = (12, 8, 5)[lod]
    boughs = (6, 5, 4)[lod]
    for whorl in range(whorls):
        f = whorl / (whorls - 1)
        level = height * (0.13 + 0.78 * f)
        reach = radius * (1.0 - f) ** 0.85 + 0.9
        droop = 0.5 - 0.3 * f
        turn = random.range(0.0, 2.0 * math.pi)
        for bough in range(boughs):
            angle = turn + 2.0 * math.pi * (bough + random.range(-0.25, 0.25)) / boughs
            long = reach * random.range(0.86, 1.14) * (1.0 + 0.25 * lod)
            along = unit((math.cos(angle), math.sin(angle), -droop * random.range(0.7, 1.2)))
            face = unit((along[0] * droop, along[1] * droop, 1.0))
            base = (math.cos(angle) * 0.2, math.sin(angle) * 0.2, level + random.range(-0.5, 0.5))
            # the boughs under others get less light, and the needles at their tips are the young, lighter ones
            tone = (0.62 + 0.42 * f) * random.range(0.9, 1.1)
            add_card(mesh, base, along, face, long, long * random.range(0.8, 0.95), 'Spruce', scale_color(NEEDLES, tone), sway=(0.25, 0.8), bend=-0.1,
                     rows=2 if lod == 0 else 1, normal_to=unit((along[0] * 0.8, along[1] * 0.8, 1.0)), rounding=0.75, shades=(0.6, 1.1),
                     mirror=random.unit() < 0.5)
    # the spike: three small boughs standing up around the tip
    for blade in range(3 if lod < 2 else 2):
        angle = 2.0 * math.pi * blade / 3.0 + seed
        face = (math.cos(angle), math.sin(angle), 0.0)
        long = height * 0.15
        add_card(mesh, (0.0, 0.0, height * 0.88), UP, face, long, long * 0.6, 'Spruce', scale_color(NEEDLES, 1.05), sway=(0.6, 1.0), rows=1,
                 normal_to=unit((face[0], face[1], 0.6)), rounding=0.7)
    return mesh


def pine(seed, lod):
    """A pine: a tall bare trunk, reddish higher up, with a few limbs near its top that carry flat plates of long needles"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(28.0, 34.0)
    steps = 5 if lod < 2 else 3
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.1, 0.1), random.range(-0.1, 0.1), 1.0), height * 0.9, steps, 0.12, seed)

    def bark_at(position, t):
        return lerp(BARK, PINE_BARK, min(t * 1.4, 1.0))

    add_tube(mesh, trunk, [1.25 - 0.9 * step / steps for step in range(steps + 1)], BARK, [0.7 * step / steps for step in range(steps + 1)], seed,
             segments=7 if lod == 0 else 5 if lod == 1 else 4, color_at=bark_at)
    top = trunk[-1]
    limbs = 8
    plates = [(add(top, (0.0, 0.0, 0.6)), 4.2, 1.1)]
    turn = random.range(0.0, 2.0 * math.pi)
    for limb in range(limbs):
        f = limb / (limbs - 1)
        angle = turn + limb * 2.4 + random.range(-0.3, 0.3)
        start = at_height(trunk, height * (0.52 + 0.34 * f))
        out = random.range(5.5, 8.5) * (1.0 - 0.4 * f)
        end = (start[0] + math.cos(angle) * out, start[1] + math.sin(angle) * out, start[2] + random.range(1.5, 4.0))
        if lod < 2:
            bow = lerp(start, end, 0.5)
            add_tube(mesh, [start, (bow[0], bow[1], bow[2] - 0.6), end], [0.5, 0.36, 0.2], PINE_BARK, [0.4, 0.6, 0.8], seed + limb,
                     segments=5 if lod == 0 else 4, cap=False)
        plates.append((end, random.range(3.0, 4.0), 0.8 + 0.3 * f))
        plates.append((add(lerp(start, end, 0.55), (0.0, 0.0, 0.8)), random.range(2.4, 3.2), 0.7 + 0.3 * f))
    for number, (center, size, tone) in enumerate(plates):
        cards = (5, 3, 2)[lod]
        grown = size * (1.0, 1.25, 1.7)[lod]
        spin = random.range(0.0, 2.0 * math.pi)
        for card in range(cards):
            angle = spin + 2.0 * math.pi * card / cards
            along = unit((math.cos(angle), math.sin(angle), random.range(0.1, 0.75)))
            face = unit((along[0] * -0.5 + random.range(-0.3, 0.3), along[1] * -0.5 + random.range(-0.3, 0.3), 1.0))
            long = grown * random.range(1.3, 1.6)
            add_card(mesh, sub(center, mul(along, long * 0.2)), along, face, long, long * 0.95, 'Pine', scale_color(PINE_NEEDLES, tone * random.range(0.9, 1.1)),
                     sway=(0.5, 1.0), bend=0.08, rows=1, round_to=sub(center, (0.0, 0.0, 3.0)), rounding=0.6, shades=(0.7, 1.05), mirror=card % 2 == 0)
    return mesh


def willow(seed, lod):
    """A swamp willow: a short leaning trunk whose limbs arch out, and from them curtains of long leaves hang"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(17.0, 21.0)
    lean = (random.range(-0.3, 0.3), random.range(-0.3, 0.3), 1.0)
    steps = 3
    trunk = bent_path((0.0, 0.0, -1.0), lean, height * 0.55, steps, 0.3, seed)
    add_tube(mesh, trunk, [1.9, 1.4, 1.1, 0.8], DARK_BARK, [0.0, 0.1, 0.25, 0.4], seed, segments=7 if lod == 0 else 5, bark=0.45)
    top = trunk[-1]
    crown = add(top, (0.0, 0.0, height * 0.2))
    limbs = 7
    turn = random.range(0.0, 2.0 * math.pi)
    stride = LOD_STRIDE[lod]
    for limb in range(limbs):
        angle = turn + 2.0 * math.pi * (limb + random.range(-0.2, 0.2)) / limbs
        out = random.range(5.0, 7.5)
        end = (top[0] + math.cos(angle) * out, top[1] + math.sin(angle) * out, top[2] + height * random.range(0.14, 0.3))
        bow = lerp(top, end, 0.5)
        bow = (bow[0], bow[1], bow[2] + 1.6)
        if lod < 2:
            add_tube(mesh, [top, bow, end], [0.5, 0.32, 0.15], DARK_BARK, [0.4, 0.55, 0.7], seed + limb, segments=5 if lod == 0 else 4, cap=False)
        tone = random.range(0.88, 1.1)
        # the top of the limb: sprays that lie on the dome of the crown
        for card in range(4):
            long = random.range(6.0, 7.6) * LOD_GROWTH[lod]
            spray = angle + random.range(-0.9, 0.9)
            along = unit((math.cos(spray), math.sin(spray), -0.15))
            point = add(lerp(top, end, random.range(0.0, 1.0)), (0.0, 0.0, random.range(1.6, 2.6)))
            mirror = random.unit() < 0.5
            if (limb * 4 + card) % stride == stride // 2:
                add_card(mesh, sub(point, mul(along, long * 0.4)), along, unit((along[0] * 0.3, along[1] * 0.3, 1.0)), long, long * 0.9, 'Willow',
                         scale_color(WILLOW_LEAVES, tone * 1.05), sway=(0.5, 1.0), bend=0.25, rows=2 if lod == 0 else 1, round_to=crown, rounding=0.6, mirror=mirror)
        # the curtains: strands that hang from along the limb, longer at its end
        for card in range(4):
            t = 0.35 + 0.65 * card / 3.0
            point = lerp(bow, end, t) if t > 0.5 else lerp(top, bow, t * 2.0)
            point = add(point, (random.range(-1.0, 1.0), random.range(-1.0, 1.0), 0.0))
            long = min(random.range(7.0, 11.0) * (0.7 + 0.5 * t), point[2] - 1.0) * (1.0 + 0.1 * lod)
            face = unit((point[0] - top[0] + random.range(-1.5, 1.5), point[1] - top[1] + random.range(-1.5, 1.5), 0.0))
            mirror = random.unit() < 0.5
            width = long * random.range(0.6, 0.75) * LOD_GROWTH[lod]
            if (limb * 4 + card) % stride == stride // 2:
                add_card(mesh, point, unit((face[0] * 0.15, face[1] * 0.15, -1.0)), face, long, width, 'Willow', scale_color(WILLOW_LEAVES, tone * (0.6 + 0.4 * t)),
                         sway=(0.4, 1.0), bend=-0.06, rows=2 if lod == 0 else 1, normal_to=unit((face[0], face[1], 0.8)), rounding=0.7, shades=(1.0, 0.75),
                         mirror=mirror)
    return mesh


def acacia(seed, lod):
    """A tree of dry land: a trunk forking low into spreading limbs under a flat wide crown of feathery leaves"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(15.0, 19.0)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.2, 0.2), random.range(-0.2, 0.2), 1.0), height * 0.35, 2, 0.2, seed)
    add_tube(mesh, trunk, [1.25, 0.9, 0.75], BARK, [0.0, 0.1, 0.2], seed, segments=6 if lod < 2 else 4)
    stride = LOD_STRIDE[lod]
    number = 0
    for limb in range(4):
        angle = 2.0 * math.pi * limb / 4 + random.range(-0.4, 0.4)
        branch = bent_path(trunk[-1], (math.cos(angle) * 0.85, math.sin(angle) * 0.85, 1.0), height * 0.72, 3, 0.15, seed * 3 + limb)
        if lod < 2:
            add_tube(mesh, branch, [0.62, 0.46, 0.34, 0.2], BARK, [0.2, 0.4, 0.6, 0.8], seed * 5 + limb, segments=5 if lod == 0 else 4, cap=False)
        top = branch[-1]
        tone = random.range(0.9, 1.1)
        # an umbrella over the end of each limb: sprays lying flat, pointing out from it
        for card in range(12):
            spray = 2.0 * math.pi * card / 12.0 * 2.0 + random.range(-0.3, 0.3)
            out = random.range(0.0, 3.2)
            long = random.range(5.0, 6.4) * LOD_GROWTH[lod]
            along = unit((math.cos(spray), math.sin(spray), random.range(-0.12, 0.05)))
            point = (top[0] + math.cos(spray) * out, top[1] + math.sin(spray) * out, top[2] + random.range(-0.4, 0.7) - (1.2 if card >= 6 else 0.0))
            mirror = random.unit() < 0.5
            number += 1
            if number % stride != stride // 2:
                continue
            add_card(mesh, sub(point, mul(along, long * 0.25)), along, unit((random.range(-0.15, 0.15), random.range(-0.15, 0.15), 1.0)), long, long * 0.95,
                     'Acacia', scale_color(ACACIA_LEAVES, tone * random.range(0.88, 1.1)), sway=(0.5, 1.0), bend=0.08, rows=1,
                     round_to=(trunk[-1][0], trunk[-1][1], top[2] - 6.0), rounding=0.5, shades=(0.8, 1.05), mirror=mirror)
    return mesh


def snag(seed, lod):
    """A dead tree: a gnarled bare trunk broken at its top and bare branches that end in fine twigs"""
    mesh = Mesh()
    random = Random(seed)
    height = random.range(18.0, 24.0)
    trunk = bent_path((0.0, 0.0, -1.0), (random.range(-0.2, 0.2), random.range(-0.2, 0.2), 1.0), height, 6, 0.35, seed)
    add_tube(mesh, trunk, [1.4, 1.0, 0.85, 0.7, 0.55, 0.45, 0.4], DEAD_WOOD, [0.0, 0.05, 0.1, 0.15, 0.2, 0.25, 0.3], seed, segments=6 if lod < 2 else 4, bark=0.4)
    for index in range(random.pick([4, 5, 6])):
        start = trunk[2 + index % 4]
        angle = random.range(0.0, 2.0 * math.pi)
        branch = bent_path(start, (math.cos(angle), math.sin(angle), random.range(0.4, 1.0)), random.range(4.0, 8.0), 3, 0.5, seed * 3 + index)
        if lod < 2:
            add_tube(mesh, branch, [0.42, 0.3, 0.2, 0.1], DEAD_WOOD, [0.2, 0.25, 0.3, 0.35], seed * 5 + index, segments=4 if lod == 0 else 3)
        along = unit(sub(branch[-1], branch[-2]))
        long = random.range(4.5, 6.5) * (1.0 + 0.3 * lod)
        if lod == 2 and index % 2:
            continue
        add_card(mesh, branch[-2], along, unit(cross(along, random.jitter(1.0))), long, long, 'DryTwig', scale_color(DEAD_WOOD, 1.4), sway=(0.3, 0.6), rows=1,
                 normal_to=UP, rounding=0.5, shades=(1.0, 1.0), mirror=index % 2 == 0)
    return mesh


# --- low plants -----------------------------------------------------------------------------------------------------------------------------------

def bush(seed, lod, color=BUSH_LEAVES, size=1.0, tile='Bush'):
    """A thicket: a few shrubs grown together, each a low lobe of small leaves on thin stems"""
    mesh = Mesh()
    random = Random(seed)
    lobes = []
    for index in range(random.pick([4, 5, 5])):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, 5.0) * size
        radius = random.range(2.6, 3.8) * size
        lobes.append(((math.cos(angle) * out, math.sin(angle) * out, radius * random.range(0.85, 1.15)), radius, random.range(0.85, 1.15)))
    crown = (0.0, 0.0, 1.0)
    for index, (center, radius, tone) in enumerate(lobes):
        if lod == 0:
            foot = (center[0] * 0.5, center[1] * 0.5, -0.3)
            add_tube(mesh, [foot, lerp(foot, center, 0.6), center], [0.22, 0.16, 0.08], BARK, [0.1, 0.3, 0.5], seed + index, segments=3, cap=False)
        add_lobe_cards(mesh, lod, seed, lobes, index, crown, 0.0, 7.0 * size, (tile,), color, (3.8 * size, 4.8 * size), 10, 0.2, (0.0, 0.0, 0.0), BARK)
    return mesh


def dry_bush(seed, lod):
    """A bush of dry land: bare twigs fanning out of the ground with a few dusty leaves"""
    mesh = Mesh()
    random = Random(seed)
    cards = (7, 4, 3)[lod]
    for index in range(cards):
        yaw = 2.0 * math.pi * index / cards + random.range(-0.4, 0.4)
        high = random.range(4.5, 6.5) * (1.0 + 0.2 * lod)
        foot = (random.range(-0.8, 0.8), random.range(-0.8, 0.8), -0.3)
        add_standing_card(mesh, foot, yaw, high, high * 1.1, 'DryTwig', scale_color((0.22, 0.17, 0.11), random.range(0.85, 1.2)), lean=random.range(0.3, 0.7),
                          shades=(0.8, 1.0), mirror=index % 2 == 0, sway=(0.0, 0.7))
    for index in range((4, 2, 0)[lod]):
        yaw = random.range(0.0, 2.0 * math.pi)
        out = random.range(1.0, 2.6)
        add_lying_card(mesh, (math.cos(yaw) * out, math.sin(yaw) * out, random.range(2.2, 3.6)), random.range(2.4, 3.2), 'Bush', (0.13, 0.13, 0.06), yaw,
                       tilt=(random.range(-0.4, 0.4), random.range(-0.4, 0.4)), sway=0.6)
    return mesh


def tundra_shrub(seed, lod):
    """Low mats of heather and dwarf birch of the tundra, olive and rusty, with a few twigs standing out of them"""
    mesh = Mesh()
    random = Random(seed)
    for index in range((6, 4, 2)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, 3.2) * (1.0 if lod < 2 else 0.4)
        size = random.range(5.0, 7.0) * (1.0 + 0.35 * lod)
        add_lying_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, random.range(0.5, 1.4)), size, 'Heather', scale_color(WHITE, random.range(0.75, 1.0)),
                       random.range(0.0, 6.28), tilt=(random.range(-0.25, 0.25), random.range(-0.25, 0.25)), sway=0.2)
    for index in range((4, 2, 0)[lod]):
        yaw = random.range(0.0, 2.0 * math.pi)
        high = random.range(2.4, 3.4)
        add_standing_card(mesh, (random.range(-2.5, 2.5), random.range(-2.5, 2.5), -0.2), yaw, high, high * 1.2, 'Bush',
                          random.pick([(0.3, 0.19, 0.08), (0.2, 0.24, 0.1), (0.34, 0.17, 0.08)]), lean=random.range(0.1, 0.4), sway=(0.0, 0.6))
    return mesh


def reeds(seed, lod):
    """A clump of reeds and cattails of a swamp: tall blades leaning out of the water"""
    mesh = Mesh()
    random = Random(seed)
    cards = (7, 4, 3)[lod]
    for index in range(cards):
        yaw = math.pi * index / cards + random.range(-0.3, 0.3)
        high = random.range(8.5, 11.5)
        foot = (random.range(-1.6, 1.6), random.range(-1.6, 1.6), -0.4)
        add_standing_card(mesh, foot, yaw, high, high * 0.5 * (1.0 + 0.25 * lod), 'Reeds', scale_color(WHITE, random.range(0.8, 1.0)), lean=random.range(-0.2, 0.2),
                          rows=1, shades=(0.65, 1.0), mirror=index % 2 == 0)
    if lod < 2:
        add_lying_card(mesh, (0.0, 0.0, 0.5), 7.0, 'GrassTop', scale_color(GRASS, 0.8), random.range(0.0, 6.28))
    return mesh


BLOSSOMS = [
    ('FlowersA', ['BlossomWhite', 'BlossomYellow', 'BlossomWhite']),
    ('FlowersB', ['BlossomBlue', 'BlossomWhite', 'BlossomBlue']),
    ('FlowersB', ['BlossomRed', 'BlossomYellow', 'BlossomRed']),
]


def flowers(seed, lod, palette):
    """A patch of a meadow in flower: tufts of grass, stems of flowers among them and blossoms open to the sky over them"""
    mesh = Mesh()
    random = Random(seed)
    side_tile, tops = BLOSSOMS[palette]
    reach = 9.5
    # the grass, lying out over the ground and standing in tufts
    for index in range((4, 3, 2)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, reach * 0.6)
        add_lying_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, random.range(0.4, 0.9)), random.range(9.0, 12.0) * (1.0 + 0.2 * lod), 'GrassTop',
                       scale_color(GRASS, random.range(0.75, 1.0)), random.range(0.0, 6.28))
    for index in range((11, 6, 0)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach
        high = random.range(3.4, 5.2)
        add_standing_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, -0.2), random.range(0.0, 6.28), high, high * 1.35, 'Grass',
                          scale_color(GRASS, random.range(0.8, 1.1)), lean=random.range(0.05, 0.35), mirror=index % 2 == 0)
    # the flowers from their side, for a low camera
    for index in range((8, 4, 0)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach
        high = random.range(5.0, 6.8)
        add_standing_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, -0.2), random.range(0.0, 6.28), high, high * 0.5, side_tile,
                          scale_color(WHITE, random.range(0.85, 1.0)), lean=random.range(0.0, 0.3), mirror=index % 2 == 0, shades=(0.8, 1.0))
    # and from above, where a camera high up sees only these
    for index in range((11, 7, 4)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach
        size = random.range(3.6, 5.2) * (1.0 + 0.3 * lod)
        add_lying_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, random.range(3.2, 5.4)), size, random.pick(tops), scale_color(WHITE, random.range(0.9, 1.0)),
                       random.range(0.0, 6.28), tilt=(random.range(-0.3, 0.3), random.range(-0.3, 0.3)), sway=0.8)
    return mesh


def tall_grass(seed, lod, tile='Grass', color=GRASS, top_color=None, high=(4.5, 7.0), reach=7.5):
    """A patch of tall grass: tufts standing and leaning every way over blades that lie out on the ground"""
    mesh = Mesh()
    random = Random(seed)
    top_color = top_color or color
    for index in range((4, 3, 2)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, reach * 0.6)
        add_lying_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, random.range(0.5, 1.6)), random.range(9.0, 12.0) * (1.0 + 0.2 * lod), 'GrassTop',
                       scale_color(top_color, random.range(0.75, 1.0)), random.range(0.0, 6.28), sway=0.2)
    for index in range((14, 8, 4)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach
        tall = random.range(*high) * (1.0 + 0.12 * lod)
        add_standing_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, -0.2), random.range(0.0, 6.28), tall, tall * 1.3 * (1.0 + 0.2 * lod), tile,
                          scale_color(color, random.range(0.8, 1.1)), lean=random.range(0.1, 0.45), mirror=index % 2 == 0)
    return mesh


def wheat(seed, lod):
    """A patch of a field of wheat: stalks standing close in rows, leaning one way in the wind, and their ears from above"""
    mesh = Mesh()
    random = Random(seed)
    reach = 12.0
    wind = random.range(0.0, 2.0 * math.pi)
    for index in range((30, 15, 6)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach
        tall = random.range(8.0, 9.6)
        # in three directions, so the field is full from every side
        yaw = wind + (index % 3) * math.pi / 3.0 + random.range(-0.2, 0.2)
        add_standing_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, -0.2), yaw, tall, tall * 0.56 * (1.0 + 0.35 * lod), 'Wheat',
                          scale_color(WHITE, random.range(0.82, 1.0)), lean=random.range(0.05, 0.22), mirror=index % 2 == 0, shades=(0.7, 1.0))
    for index in range((18, 11, 6)[lod]):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach * 0.9
        add_lying_card(mesh, (math.cos(angle) * out, math.sin(angle) * out, random.range(7.0, 7.8)), random.range(9.5, 12.5) * (1.0 + 0.3 * lod), 'WheatTop',
                       scale_color(WHITE, random.range(0.9, 1.0)), wind + random.range(-0.3, 0.3), tilt=(random.range(-0.06, 0.06), random.range(-0.06, 0.06)), sway=0.9)
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


def write_glb(mesh, path, name, with_uvs=False):
    """Writes a mesh as a GLB file and returns the number of its triangles; with_uvs adds the UVs, which the cards of the plants need"""
    positions, normals, colors, uvs, indices = mesh.build()
    positions = [to_file_axes(*(value * 0.01 for value in position)) for position in positions]
    normals = [to_file_axes(*normal) for normal in normals]
    count = len(positions)

    position_data = b''.join(struct.pack('<3f', *value) for value in positions)
    normal_data = b''.join(struct.pack('<3f', *value) for value in normals)
    color_data = b''.join(struct.pack('<4f', *value) for value in colors)
    uv_data = b''.join(struct.pack('<2f', *value) for value in uvs) if with_uvs else b''
    index_data = struct.pack('<%dI' % len(indices), *indices)
    binary = position_data + normal_data + color_data + uv_data + index_data

    views = []
    offset = 0
    for data, target in ((position_data, 34962), (normal_data, 34962), (color_data, 34962), (uv_data, 34962), (index_data, 34963)):
        if not data:
            continue
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
        offset += len(data)

    stored = [struct.unpack('<3f', struct.pack('<3f', *value)) for value in positions]
    attributes = {'POSITION': 0, 'NORMAL': 1, 'COLOR_0': 2}
    accessors = [
        {'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': [min(value[axis] for value in stored) for axis in range(3)], 'max': [max(value[axis] for value in stored) for axis in range(3)]},
        {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
        {'bufferView': 2, 'componentType': 5126, 'count': count, 'type': 'VEC4'},
    ]
    if with_uvs:
        attributes['TEXCOORD_0'] = 3
        accessors.append({'bufferView': 3, 'componentType': 5126, 'count': count, 'type': 'VEC2'})
    accessors.append({'bufferView': len(accessors), 'componentType': 5125, 'count': len(indices), 'type': 'SCALAR'})
    document = {
        'asset': {'version': '2.0', 'generator': 'generate_planet_objects.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': name}],
        'meshes': [{'name': name, 'primitives': [{'attributes': attributes, 'indices': len(accessors) - 1}]}],
        'accessors': accessors,
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


# each mesh: its name, how it is made at a LOD, its material (foliage for plants and wood, stone for stones and rocks), and whether it is made of
# cards, so it is written at three LODs, or solid, with LODs reduced from its triangles by the import
MESHES = [
    ('Spruce_0', lambda lod: spruce(11, lod), 'foliage', True),
    ('Spruce_1', lambda lod: spruce(23, lod), 'foliage', True),
    ('Spruce_2', lambda lod: spruce(37, lod), 'foliage', True),
    ('Pine_0', lambda lod: pine(41, lod), 'foliage', True),
    ('Pine_1', lambda lod: pine(53, lod), 'foliage', True),
    ('Broadleaf_0', lambda lod: broadleaf(61, lod), 'foliage', True),
    ('Broadleaf_1', lambda lod: broadleaf(73, lod, ('LeafB',), LINDEN_LEAVES), 'foliage', True),
    ('Broadleaf_2', lambda lod: broadleaf(89, lod, ('LeafA', 'LeafB'), scale_color(LEAVES, 0.9)), 'foliage', True),
    ('Birch_0', lambda lod: birch(97, lod), 'foliage', True),
    ('Birch_1', lambda lod: birch(101, lod), 'foliage', True),
    ('Oak_0', lambda lod: oak(107, lod), 'foliage', True),
    ('Snag_0', lambda lod: snag(113, lod), 'foliage', True),
    ('Snag_1', lambda lod: snag(127, lod), 'foliage', True),
    ('Willow_0', lambda lod: willow(131, lod), 'foliage', True),
    ('Acacia_0', lambda lod: acacia(137, lod), 'foliage', True),
    ('Acacia_1', lambda lod: acacia(149, lod), 'foliage', True),
    ('Bush_0', lambda lod: bush(151, lod), 'foliage', True),
    ('Bush_1', lambda lod: bush(163, lod, scale_color(BUSH_LEAVES, 1.15), 1.1), 'foliage', True),
    ('Bush_2', lambda lod: bush(173, lod, scale_color(BUSH_LEAVES, 0.88), 0.9, 'LeafB'), 'foliage', True),
    ('DryBush_0', lambda lod: dry_bush(179, lod), 'foliage', True),
    ('DryBush_1', lambda lod: dry_bush(181, lod), 'foliage', True),
    ('TundraShrub_0', lambda lod: tundra_shrub(191, lod), 'foliage', True),
    ('TundraShrub_1', lambda lod: tundra_shrub(193, lod), 'foliage', True),
    ('Reeds_0', lambda lod: reeds(197, lod), 'foliage', True),
    ('Reeds_1', lambda lod: reeds(199, lod), 'foliage', True),
    ('Flowers_0', lambda lod: flowers(211, lod, 0), 'foliage', True),
    ('Flowers_1', lambda lod: flowers(223, lod, 1), 'foliage', True),
    ('Flowers_2', lambda lod: flowers(227, lod, 2), 'foliage', True),
    ('Grass_0', lambda lod: tall_grass(281, lod), 'foliage', True),
    ('Grass_1', lambda lod: tall_grass(283, lod), 'foliage', True),
    ('DryGrass_0', lambda lod: tall_grass(293, lod, 'DryGrass', WHITE, (0.5, 0.44, 0.24)), 'foliage', True),
    ('DryGrass_1', lambda lod: tall_grass(307, lod, 'DryGrass', WHITE, (0.5, 0.44, 0.24)), 'foliage', True),
    ('Wheat_0', lambda lod: wheat(311, lod), 'foliage', True),
    ('Wheat_1', lambda lod: wheat(313, lod), 'foliage', True),
    ('Stone_0', lambda lod: stone(229, 2.2), 'stone', False),
    ('Stone_1', lambda lod: stone(233, 2.0, 3), 'stone', False),
    ('Stone_2', lambda lod: stone(239, 2.6), 'stone', False),
    ('Stone_3', lambda lod: stone(241, 1.8, 5), 'stone', False),
    ('Rock_0', lambda lod: stone(251, 7.0, 5), 'stone', False),
    ('Rock_1', lambda lod: stone(257, 6.5, 6), 'stone', False),
    ('Rock_2', lambda lod: stone(263, 7.5, 4), 'stone', False),
    ('Log_0', lambda lod: log(269), 'foliage', False),
    ('Log_1', lambda lod: log(271), 'foliage', False),
    ('Stump_0', lambda lod: stump(277), 'foliage', False),
]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    for old in os.listdir(OUTPUT_FOLDER):
        if old.endswith('.glb'):
            os.remove(os.path.join(OUTPUT_FOLDER, old))

    manifest = {'meshes': []}
    for name, make, material, cards in MESHES:
        asset = 'SM_PlanetObject_' + name
        whole = make(0)
        triangles = [write_glb(whole, os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, True)]
        lods = []
        for lod in (1, 2) if cards else ():
            lods.append('%s_LOD%d' % (asset, lod))
            triangles.append(write_glb(make(lod), os.path.join(OUTPUT_FOLDER, lods[-1] + '.glb'), lods[-1], True))
        # the screen sizes from which the LODs of the cards are drawn: a tree keeps its whole mesh to about 1000 from the camera and its second
        # to about 2200, and a low plant, smaller on the screen at the same distance, switches at smaller sizes, about as far away
        tall = max(position[2] for position in whole.positions) > 12.0
        manifest['meshes'].append({'name': asset, 'material': material, 'triangles': triangles[0], 'lods': lods,
                                   'screens': TREE_SCREENS if tall else PLANT_SCREENS})
        print('%s: %s triangles' % (asset, ', '.join(str(count) for count in triangles)))
    with open(os.path.join(OUTPUT_FOLDER, 'objects.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=2)
    print('Wrote %d meshes to %s' % (len(MESHES), OUTPUT_FOLDER))


# generate_cave_objects.py imports the helpers above without writing these meshes again
if __name__ == '__main__':
    main()
