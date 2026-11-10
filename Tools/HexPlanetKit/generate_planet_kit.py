"""Generates the land kit of the hex planet: one mesh per set of neighbors for the seabed, shallows, plains and hills.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_kit.py
Writes GLB files and kit.json to Saved/HexPlanetKit. Then run import_planet_kit.py with the commandlet.
The GLB files open in Blender, so a piece can be reworked by hand and imported again.

A piece is chosen as the mountains of the flat map are (HexMeshBuilder::GetJoinedPiece): bit N of a mask means that the neighbor in
direction N of FHexCoord, at an angle of 30 + 60 * N degrees from +X towards +Y, lies on the same level. Turned copies cover the rest,
so 64 masks need 14 pieces. The top of a piece is at height 0 and its edge at -FOOT_DEPTH. AHexPlanet bends each piece onto its cell of the
sphere and puts the edge on the line of the side, halfway between the tops of the two cells, so the higher cell falls to it and the lower one
rises to it, and moves heights between the edge and 0 in proportion (HexPlanetKit::PlaceVertex). What neighbors must agree on depends only
on the masks:
- A side towards another level lies at the edge, and so does each corner at its ends. Its slope is steepest there and flattens inwards,
  so the halves of the two cells join into one smooth slope.
- A side towards the same level lies at 0, or between 0 and the edge where it comes near such a corner. The planet lays all of it on the
  line that both cells give that side, so its shape there does not matter.
- Inside, heights stay within the edge and 0, except for the bumps of hills, which fade to 0 before any side.
- The middle of every side is a vertex, as the planet bends sides at their middle.
"""

import json
import math
import os
import struct

HEX_RADIUS = 100.0
APOTHEM = HEX_RADIUS * math.sqrt(3.0) * 0.5
# the authored height of the edge, which the planet stretches to half the real drop. The planet moves every height between it and 0
FOOT_DEPTH = 30.0

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetKit'))

CORNER_STEPS = [(1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1), (1, -1)]

# each kit: its level, how wide its slopes are, how many lattice steps a piece has from its center to a corner when it has a slope and when
# it has none, its bumps and colors. A step count must be even, so the middle of each side is a vertex. The slopes of the sea and the coast
# have 6 steps, so the wobble of the coast (UHexPlanetConfig::CoastWobble) has vertices to wind along.
# Hills have a version per style of HILL_STYLES, so neighbors differ: knolls, a ridge, two big knolls, many small ones
KITS = [
    {'name': 'Deep', 'level': -2, 'slope_width': 0.6, 'steps': 6, 'flat_steps': 2, 'bump': 0.0, 'hills': 0, 'variants': 0,
     'color': (0.10, 0.11, 0.09), 'slope_color': (0.10, 0.11, 0.09)},
    {'name': 'Shallows', 'level': -1, 'slope_width': 0.45, 'steps': 6, 'flat_steps': 2, 'bump': 0.0, 'hills': 0, 'variants': 0,
     'color': (0.20, 0.19, 0.11), 'slope_color': (0.16, 0.15, 0.10)},
    {'name': 'Plains', 'level': 0, 'slope_width': 0.45, 'steps': 6, 'flat_steps': 2, 'bump': 0.0, 'hills': 0, 'variants': 0,
     'color': (0.18, 0.35, 0.08), 'slope_color': (0.30, 0.25, 0.13)},
    {'name': 'Hills', 'level': 1, 'slope_width': 0.55, 'steps': 6, 'flat_steps': 6, 'bump': 18.0, 'hills': 4, 'variants': 3,
     'color': (0.24, 0.33, 0.09), 'slope_color': (0.30, 0.26, 0.14)},
]

# styles of the bumps of hills by version: how many knolls, how far out, how wide and how high as shares of the bump, and a ridge of that length
HILL_STYLES = [
    {'count': 4, 'reach': (0.15, 0.5), 'radius': (0.4, 0.52), 'height': (0.55, 1.0), 'ridge': 0.0},
    {'count': 1, 'reach': (0.0, 0.1), 'radius': (0.34, 0.42), 'height': (0.85, 1.1), 'ridge': 0.55},
    {'count': 2, 'reach': (0.2, 0.35), 'radius': (0.48, 0.6), 'height': (1.0, 1.3), 'ridge': 0.0},
    {'count': 5, 'reach': (0.1, 0.5), 'radius': (0.34, 0.42), 'height': (0.4, 0.7), 'ridge': 0.0},
]


def rotate_mask(mask, turns):
    turns %= 6
    return ((mask << turns) | (mask >> (6 - turns))) & 63


def get_canonical_masks():
    return sorted({min(rotate_mask(mask, turns) for turns in range(6)) for mask in range(64)})


def get_mesh_name(kit, mask, variant=0):
    name = 'SM_PlanetKit_%s_%s' % (kit['name'], ''.join('1' if mask >> bit & 1 else '0' for bit in range(6)))
    return name + '_V%d' % variant if variant else name


def smoothstep(low, high, value):
    t = min(1.0, max(0.0, (value - low) / (high - low)))
    return t * t * (3.0 - 2.0 * t)


def hash_unit(x, y, seed):
    value = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((value ^ (value >> 16)) & 0xFFFFFF) / 0xFFFFFF


def side_distance(x, y, direction):
    """Distance from a point inside the hex to its side in a direction, 0 on the side"""
    angle = math.radians(30.0 + 60.0 * direction)
    return APOTHEM - x * math.cos(angle) - y * math.sin(angle)


class Piece:
    def __init__(self, kit, mask, variant):
        self.kit = kit
        self.mask = mask
        self.variant = variant
        self.steps = kit['flat_steps'] if mask == 63 else kit['steps']
        self.points = []
        self.indices = {}
        for a in range(-self.steps, self.steps + 1):
            for b in range(-self.steps, self.steps + 1):
                if max(abs(a), abs(b), abs(a + b)) <= self.steps:
                    self.indices[(a, b)] = len(self.points)
                    self.points.append((a, b))

        seed = kit['level'] * 1000 + mask * 7 + variant * 131 + 3
        # each knoll is a segment, of length 0 but on a ridge, with a radius and a height
        self.hills = []
        if kit['hills']:
            style = HILL_STYLES[variant % len(HILL_STYLES)]
            between = lambda pair, value: pair[0] + (pair[1] - pair[0]) * value
            for hill in range(style['count']):
                angle = math.tau * (hill + hash_unit(hill, 1, seed) * 0.6) / style['count']
                distance = between(style['reach'], hash_unit(hill, 2, seed)) * HEX_RADIUS
                if hill == 0 and style['count'] > 2:
                    distance *= 0.3
                radius = between(style['radius'], hash_unit(hill, 3, seed)) * HEX_RADIUS
                height = kit['bump'] * between(style['height'], hash_unit(hill, 4, seed))
                x, y = math.cos(angle) * distance, math.sin(angle) * distance
                half = style['ridge'] * HEX_RADIUS * 0.5
                along = math.tau * hash_unit(hill, 5, seed)
                dx, dy = math.cos(along) * half, math.sin(along) * half
                self.hills.append((x - dx, y - dy, x + dx, y + dy, radius, height))

        self.heights = [self.get_height(*self.get_xy(index)) for index in range(len(self.points))]

    def get_xy(self, index):
        a, b = self.points[index]
        step = HEX_RADIUS / self.steps
        return (a + b * 0.5) * step, b * math.sqrt(3.0) * 0.5 * step

    def get_height(self, x, y):
        width = self.kit['slope_width'] * APOTHEM
        # how much of the edge a point is: the slopes towards other levels joined smoothly, 1 on such a side and at its corners.
        # Each slope is steepest at the side and flat where it ends, so it meets the other half of the slope in the neighbor smoothly
        keep = 1.0
        for direction in range(6):
            if not self.mask >> direction & 1:
                inward = min(1.0, max(0.0, side_distance(x, y, direction) / width))
                keep *= 1.0 - (1.0 - inward) ** 2
        height = -FOOT_DEPTH * (1.0 - keep)

        if self.hills:
            # bumps fade to 0 well before every side, so sides lie at 0 or at the foot whatever the neighbors are
            inside = smoothstep(0.0, 0.45 * APOTHEM, min(side_distance(x, y, direction) for direction in range(6)))
            bumps = 0.0
            for from_x, from_y, to_x, to_y, radius, hill_height in self.hills:
                along_x, along_y = to_x - from_x, to_y - from_y
                length = along_x * along_x + along_y * along_y
                fraction = min(1.0, max(0.0, ((x - from_x) * along_x + (y - from_y) * along_y) / length)) if length > 0.0 else 0.0
                # a ridge sags a little between its ends, so it reads as a crest and not a wall
                sag = 1.0 - 0.25 * math.sin(math.pi * fraction) if length > 0.0 else 1.0
                closeness = max(0.0, 1.0 - math.hypot(x - from_x - along_x * fraction, y - from_y - along_y * fraction) / radius)
                closeness *= sag
                bumps = max(bumps, hill_height * closeness * closeness * (3.0 - 2.0 * closeness))
            height += bumps * inside * keep
        return height

    def get_position(self, index):
        x, y = self.get_xy(index)
        return x, y, self.heights[index]

    def get_triangles(self):
        """Returns lattice triangles whose winding faces up in the renderer: (C - A) x (B - A) points up."""
        triangles = []
        for (a, b) in self.points:
            right, up, left_up = (a + 1, b), (a, b + 1), (a - 1, b + 1)
            if right in self.indices and up in self.indices:
                triangles.append((self.indices[(a, b)], self.indices[up], self.indices[right]))
            if up in self.indices and left_up in self.indices:
                triangles.append((self.indices[(a, b)], self.indices[left_up], self.indices[up]))
        return triangles

    def compute_normals(self):
        normals = [[0.0, 0.0, 0.0] for _ in self.points]
        for triangle in self.get_triangles():
            a, b, c = [self.get_position(index) for index in triangle]
            u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            cross = (v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0])
            for index in triangle:
                for axis in range(3):
                    normals[index][axis] += cross[axis]
        result = []
        for normal in normals:
            length = math.sqrt(sum(value * value for value in normal)) or 1.0
            result.append([value / length for value in normal])
        return result

    def check_sides(self):
        """Sides follow the rules at the top: at the edge towards another level, else within the edge and 0"""
        for index, (a, b) in enumerate(self.points):
            if max(abs(a), abs(b), abs(a + b)) != self.steps:
                continue
            x, y = self.get_xy(index)
            on_open = any(not self.mask >> direction & 1 and abs(side_distance(x, y, direction)) < 1e-6 for direction in range(6))
            height = self.heights[index]
            good = abs(height + FOOT_DEPTH) < 1e-4 if on_open else -FOOT_DEPTH - 1e-4 <= height <= 1e-4
            if not good:
                raise RuntimeError('%s: side vertex (%d, %d) lies at %.4f' % (get_mesh_name(self.kit, self.mask, self.variant), a, b, height))


# boulders for the foot of mountains (FHexPlanetBoulders): rocks of radius 1, each an icosphere with bumps, squashed, flat underneath and cut by a few
# planes, so they have broad flat faces like split stone, with a normal per face
BOULDERS = 4
BOULDER_CUTS = 5


class Boulder:
    def __init__(self, seed):
        golden = (1.0 + math.sqrt(5.0)) * 0.5
        points = [(-1, golden, 0), (1, golden, 0), (-1, -golden, 0), (1, -golden, 0), (0, -1, golden), (0, 1, golden), (0, -1, -golden), (0, 1, -golden),
                  (golden, 0, -1), (golden, 0, 1), (-golden, 0, -1), (-golden, 0, 1)]
        faces = [(0, 11, 5), (0, 5, 1), (0, 1, 7), (0, 7, 10), (0, 10, 11), (1, 5, 9), (5, 11, 4), (11, 10, 2), (10, 7, 6), (7, 1, 8),
                 (3, 9, 4), (3, 4, 2), (3, 2, 6), (3, 6, 8), (3, 8, 9), (4, 9, 5), (2, 4, 11), (6, 2, 10), (8, 6, 7), (9, 8, 1)]
        points = [self.unit(point) for point in points]
        # one split of every triangle into four
        middles = {}
        split = []
        for a, b, c in faces:
            ab, bc, ca = [self.middle(points, middles, i, j) for i, j in ((a, b), (b, c), (c, a))]
            split += [(a, ab, ca), (b, bc, ab), (c, ca, bc), (ab, bc, ca)]
        self.faces = split

        cuts = []
        for cut in range(BOULDER_CUTS):
            normal = self.unit((hash_unit(cut, 1, seed) * 2 - 1, hash_unit(cut, 2, seed) * 2 - 1, hash_unit(cut, 3, seed) * 1.6 - 0.4))
            cuts.append((normal, 0.55 + 0.3 * hash_unit(cut, 4, seed)))
        squash = 0.75 + 0.25 * hash_unit(9, 5, seed)
        stretch = 1.0 + 0.4 * hash_unit(9, 6, seed)
        self.points = []
        for index, (x, y, z) in enumerate(points):
            bump = 0.85 + 0.3 * hash_unit(index, 7, seed)
            x, y, z = x * bump * stretch, y * bump, z * bump * squash
            for normal, distance in cuts:
                over = x * normal[0] + y * normal[1] + z * normal[2] - distance
                if over > 0.0:
                    x, y, z = x - normal[0] * over, y - normal[1] * over, z - normal[2] * over
            self.points.append((x, y, max(z, -0.45 * squash)))
        largest = max(math.sqrt(x * x + y * y + z * z) for x, y, z in self.points)
        self.points = [(x / largest, y / largest, z / largest) for x, y, z in self.points]

    @staticmethod
    def unit(point):
        length = math.sqrt(sum(value * value for value in point))
        return tuple(value / length for value in point)

    def middle(self, points, middles, i, j):
        key = (min(i, j), max(i, j))
        if key not in middles:
            middles[key] = len(points)
            points.append(self.unit(tuple((points[i][axis] + points[j][axis]) * 0.5 for axis in range(3))))
        return middles[key]

    def get_triangles(self):
        """Triangles with their own corners and a normal each, facing out: a triangle shows where (C - A) x (B - A) points"""
        result = []
        for face in self.faces:
            a, b, c = [self.points[index] for index in face]
            u = [c[axis] - a[axis] for axis in range(3)]
            v = [b[axis] - a[axis] for axis in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            center = [(a[axis] + b[axis] + c[axis]) / 3.0 for axis in range(3)]
            if sum(cross[axis] * center[axis] for axis in range(3)) < 0.0:
                b, c = c, b
                cross = tuple(-value for value in cross)
            length = math.sqrt(sum(value * value for value in cross))
            if length < 1e-9:
                continue
            result.append(((a, b, c), tuple(value / length for value in cross)))
        return result


# floes of sea ice (FHexPlanetFloes): slabs of radius 1 around their waterline at height 0, an outline of broken straight edges, a top a little
# above the water whose facets lean apart like rafted plates, a wall down to a wide ledge just under the water that reaches out past the top, which
# the water shows as a lighter skirt, and a bottom deeper in. Shards are smaller pieces with fewer, sharper corners standing a little higher.
# A normal per face, as on the boulders
FLOES = 5
SHARDS = 3


class Floe:
    def __init__(self, seed, shard):
        corners = 4 + int(hash_unit(1, 1, seed) * 3) if shard else 7 + int(hash_unit(1, 1, seed) * 4)
        angles = sorted((index + (hash_unit(index, 2, seed) - 0.5) * 0.8) / corners * 2.0 * math.pi for index in range(corners))
        low = 0.45 if shard else 0.6
        outline = [((low + (1.0 - low) * hash_unit(index, 3, seed)) * math.cos(angle), (low + (1.0 - low) * hash_unit(index, 3, seed)) * math.sin(angle))
                   for index, angle in enumerate(angles)]
        # stretched, and broken off along one or two straight lines, as a floe splits from the pack
        stretch = 1.0 + 0.6 * hash_unit(2, 9, seed)
        outline = [(x * stretch, y / stretch) for x, y in outline]
        for cut in range(1 + int(hash_unit(3, 9, seed) * 2)):
            angle = hash_unit(cut, 10, seed) * 2.0 * math.pi
            normal = (math.cos(angle), math.sin(angle))
            distance = 0.35 + 0.3 * hash_unit(cut, 11, seed)
            outline = [(x - normal[0] * max(0.0, x * normal[0] + y * normal[1] - distance), y - normal[1] * max(0.0, x * normal[0] + y * normal[1] - distance))
                       for x, y in outline]
        largest = max(math.hypot(x, y) for x, y in outline)
        outline = [(x / largest, y / largest) for x, y in outline]
        top = 0.2 if shard else 0.14
        edge = top - 0.035
        # the inner ring of the top, turned a little against the outline, at heights of its own, so the facets of the top lean apart
        twist = (hash_unit(4, 4, seed) - 0.5) * 0.8 / corners * 2.0 * math.pi
        inner = []
        for index, (x, y) in enumerate(outline):
            share = 0.45 + 0.2 * hash_unit(index, 5, seed)
            cos, sin = math.cos(twist), math.sin(twist)
            inner.append((share * (x * cos - y * sin), share * (x * sin + y * cos), top + (hash_unit(index, 6, seed) - 0.5) * (0.06 if shard else 0.04)))
        center = (0.0, 0.0, top + (hash_unit(7, 7, seed) - 0.3) * 0.03)
        rim = [(x, y, edge + (hash_unit(index, 8, seed) - 0.5) * 0.02) for index, (x, y) in enumerate(outline)]
        # wide and shallow, so the water over it is shallow enough for a ring of foam around the floe (the film of WATER_CODE)
        ledge = [(x * 1.3, y * 1.3, -0.06) for x, y in outline]
        bottom_depth = -0.2 if shard else -0.24
        bottom = [(x * 0.82, y * 0.82, bottom_depth) for x, y in outline]
        bottom_center = (0.0, 0.0, bottom_depth)

        self.faces = []
        for index in range(corners):
            following = (index + 1) % corners
            self.faces.append((center, inner[index], inner[following]))
            for upper, lower in ((inner, rim), (rim, ledge), (ledge, bottom)):
                self.faces.append((upper[index], lower[index], lower[following]))
                self.faces.append((upper[index], lower[following], upper[following]))
            self.faces.append((bottom_center, bottom[index], bottom[following]))

    def get_triangles(self):
        """Triangles with a normal each, facing out of the slab: as its outline is star shaped around its waterline at the origin, a face
        faces out where its normal points away from the origin"""
        result = []
        for a, b, c in self.faces:
            u = [c[axis] - a[axis] for axis in range(3)]
            v = [b[axis] - a[axis] for axis in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            middle = [(a[axis] + b[axis] + c[axis]) / 3.0 for axis in range(3)]
            if sum(cross[axis] * middle[axis] for axis in range(3)) < 0.0:
                b, c = c, b
                cross = tuple(-value for value in cross)
            length = math.sqrt(sum(value * value for value in cross))
            if length < 1e-9:
                continue
            result.append(((a, b, c), tuple(value / length for value in cross)))
        return result


# the bridge over a river (BridgeMesh of DA_HexPlanet, UHexPlanetRivers): a stone arch whose X runs across the river, BRIDGE_LENGTH long in world
# units, so it fills BridgeLength at a scale of 1, and Z up from the bottom of the valley of the river at 0. Its deck humps over the middle, an
# arch spans the water, the abutments at its ends go down under the ground of the banks, and a low parapet runs along each side. A normal per face
BRIDGE_LENGTH = 64.0
BRIDGE_WIDTH = 14.0
BRIDGE_STEPS = 24


class Bridge:
    def __init__(self):
        half = BRIDGE_LENGTH * 0.5
        width = BRIDGE_WIDTH * 0.5
        arch = 15.0
        rise = 4.6
        end_height = 3.2
        hump = 4.5
        parapet = 2.0
        wall = 1.2
        foot = -2.5
        xs = [-half + BRIDGE_LENGTH * index / BRIDGE_STEPS for index in range(BRIDGE_STEPS + 1)]
        # the arch springs from the ground, and its crown leaves a band of stone under the deck
        for edge in (-arch, arch):
            if edge not in xs:
                xs.append(edge)
        xs.sort()

        def top(x):
            return end_height + hump * (1.0 - (x / half) ** 2)

        def bottom(x):
            return rise * math.sqrt(max(0.0, 1.0 - (x / arch) ** 2)) if abs(x) < arch else foot

        self.faces = []

        def quad(a, b, c, d, outward):
            self.add(a, b, c, outward)
            self.add(a, c, d, outward)

        for left, right in zip(xs, xs[1:]):
            # the deck between the parapets, their tops, and their inner faces
            quad((left, -width + wall, top(left)), (right, -width + wall, top(right)), (right, width - wall, top(right)), (left, width - wall, top(left)), (0.0, 0.0, 1.0))
            for side in (-1.0, 1.0):
                outer = side * width
                inner = side * (width - wall)
                quad((left, inner, top(left) + parapet), (right, inner, top(right) + parapet), (right, outer, top(right) + parapet), (left, outer, top(left) + parapet), (0.0, 0.0, 1.0))
                quad((left, inner, top(left)), (right, inner, top(right)), (right, inner, top(right) + parapet), (left, inner, top(left) + parapet), (0.0, -side, 0.0))
                # the outer walls, from the underside up to the top of the parapet
                quad((left, outer, bottom(left)), (right, outer, bottom(right)), (right, outer, top(right) + parapet), (left, outer, top(left) + parapet), (0.0, side, 0.0))
            # the underside: the arch, and under the abutments their feet in the ground
            quad((left, -width, bottom(left)), (right, -width, bottom(right)), (right, width, bottom(right)), (left, width, bottom(left)), (0.0, 0.0, -1.0))
        # the ends
        for end in (-half, half):
            sign = 1.0 if end > 0.0 else -1.0
            quad((end, -width, foot), (end, width, foot), (end, width, top(end) + parapet), (end, -width, top(end) + parapet), (sign, 0.0, 0.0))

    def add(self, a, b, c, outward):
        self.faces.append((a, b, c, outward))

    def get_triangles(self):
        """Triangles wound and with a normal facing the outward direction each face was given"""
        result = []
        for a, b, c, outward in self.faces:
            u = [c[axis] - a[axis] for axis in range(3)]
            v = [b[axis] - a[axis] for axis in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            length = math.sqrt(sum(value * value for value in cross))
            if length < 1e-9:
                continue
            if sum(cross[axis] * outward[axis] for axis in range(3)) < 0.0:
                b, c = c, b
                cross = tuple(-value for value in cross)
            result.append(((a, b, c), tuple(value / length for value in cross)))
        return result


# the markers of the entrances of the underground (AHexPlanet::UpdateMarkers), of radius 1 around their foot at the origin, which the planet scales
# to MarkerSize: the alpha of the color of a vertex is how much it glows in the color of its network (M_HexPlanetMarker), its red how much of the
# daylight of the shaft over an exit falls on it, and on the shaft itself how far up it lies (M_HexPlanetShaft). A normal per face, as on the
# boulders, so the rock reads as broken stone. As the developer chose on 2026-11-10, each reads as the way into a cave from above, from any side: the
# entrance on the surface is a pit, a rim of broken rock round its mouth, a disc at HOLE_HEIGHT whose material draws the shaft below it with stairs
# winding down into the dark (M_HexPlanetPit), as the ground hides what lies under it, and the exit underground stone stairs winding up a rock to
# a glyph on its top, where the shaft of daylight falls
MARKER_SEGMENTS = 20
# the mouth of the pit, a disc this wide at this height, which the rim overhangs a little so its edge does not show
HOLE_RADIUS = 0.42
HOLE_HEIGHT = 0.12
# the top of the rock of an exit, where the shaft of daylight begins (AHexPlanet::UpdateMarkers places it at the top of the bounds of the mesh)
EXIT_TOP = 1.1


def ring_noise(index, ring, seed, amount):
    return (hash_unit(index, ring, seed) - 0.5) * 2.0 * amount


def oriented(a, b, c, outward):
    """The corners of a triangle wound to face the outward direction, as a triangle shows where (C - A) x (B - A) points, and its normal, or None if
    it has no area"""
    u = [c[axis] - a[axis] for axis in range(3)]
    v = [b[axis] - a[axis] for axis in range(3)]
    cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
    length = math.sqrt(sum(value * value for value in cross))
    if length < 1e-9:
        return None
    if sum(cross[axis] * outward[axis] for axis in range(3)) < 0.0:
        b, c = c, b
        cross = [-value for value in cross]
    return [a, b, c], [value / length for value in cross]


class MarkerMesh:
    """Triangles of a marker as (corners, normal, glows, reds, greens), each corner with how much it glows, how much daylight it takes and how much
    it is worked stone, as the steps of stairs, rather than the rock of the cell"""

    def __init__(self):
        self.triangles = []

    def add(self, a, b, c, outward, glow=0.0, red=0.0, green=0.0):
        result = oriented(a, b, c, outward)
        if result:
            corners, normal = result
            channels = [value if isinstance(value, (list, tuple)) else (value, value, value) for value in (glow, red, green)]
            # the winding swapped the last two corners if it had to
            if corners[1] is not b:
                channels = [(value[0], value[2], value[1]) for value in channels]
            self.triangles.append((corners, normal, list(channels[0]), list(channels[1]), list(channels[2])))

    def add_quad(self, a, b, c, d, outward, glow=0.0, red=0.0, green=0.0):
        self.add(a, b, c, outward, glow, red, green)
        self.add(a, c, d, outward, glow, red, green)

    def add_rock(self, seed, size, squash, angle, distance, height, lean=0.0, glow=0.0, red=0.0, stretch=1.0):
        """A boulder of the kit, size wide, squashed in height, leaning out by lean, standing at a distance from the middle in a direction"""
        cos, sin = math.cos(angle), math.sin(angle)
        for corners, normal in Boulder(seed).get_triangles():
            placed = []
            for x, y, z in corners:
                x, y, z = x * size, y * size * stretch, (z + 0.35) * size * squash
                x += lean * z
                placed.append((x * cos - y * sin + distance * cos, x * sin + y * cos + distance * sin, z + height))
            # the normal of the face as it stands now, from its corners
            a, b, c = placed
            u = [c[axis] - a[axis] for axis in range(3)]
            v = [b[axis] - a[axis] for axis in range(3)]
            cross = (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
            self.add(a, b, c, cross, glow, red)

    def add_crystal(self, angle, distance, height, size, tall, lean, turn, glow):
        """A crystal of six sides with a pointed tip, leaning out from the middle, which glows"""
        cos, sin = math.cos(angle), math.sin(angle)
        tip_height = tall * 1.25

        def place(x, y, z):
            # leaning out along the direction from the middle, then turned there
            x, z = x * math.cos(lean) + z * math.sin(lean), -x * math.sin(lean) + z * math.cos(lean)
            return (x * cos - y * sin + distance * cos, x * sin + y * cos + distance * sin, z + height)

        ring_low = [place(size * math.cos(turn + k * math.pi / 3.0), size * math.sin(turn + k * math.pi / 3.0), -0.02) for k in range(6)]
        ring_high = [place(size * 0.85 * math.cos(turn + k * math.pi / 3.0), size * 0.85 * math.sin(turn + k * math.pi / 3.0), tall) for k in range(6)]
        tip = place(0.0, 0.0, tip_height)
        axis_low = place(0.0, 0.0, 0.0)
        for k in range(6):
            n = (k + 1) % 6
            middle = [(ring_low[k][axis] + ring_high[n][axis]) * 0.5 - axis_low[axis] for axis in range(3)]
            self.add_quad(ring_low[k], ring_low[n], ring_high[n], ring_high[k], middle, glow)
            top = [(ring_high[k][axis] + ring_high[n][axis] + tip[axis]) / 3.0 - axis_low[axis] for axis in range(3)]
            self.add(ring_high[k], ring_high[n], tip, top, glow)

    def get_triangles(self):
        return self.triangles


class EntranceRim(MarkerMesh):
    """The rim of a pit on the surface: a low ring of broken rock round its mouth, its inner edge overhanging the mouth so its edge does not show and
    dropping below it, boulders on its crest and two clusters of crystals that glow in the color of the network"""

    def __init__(self, seed):
        super().__init__()
        count = MARKER_SEGMENTS * 2
        # rings from below the edge of the mouth, up its inner edge and over the crest, out to the foot: (radius, height)
        rings = [(HOLE_RADIUS - 0.05, HOLE_HEIGHT - 0.06), (HOLE_RADIUS - 0.05, HOLE_HEIGHT + 0.025), (HOLE_RADIUS + 0.03, HOLE_HEIGHT + 0.09),
                 (0.56, HOLE_HEIGHT + 0.1), (0.7, 0.12), (0.84, 0.05), (1.0, -0.04)]
        points = []
        for ring, (radius, height) in enumerate(rings):
            row = []
            for index in range(count):
                angle = (index + 0.5 * (ring % 2)) / count * 2.0 * math.pi
                # the inner edge stays round enough to cover the edge of the mouth, the crest and the slope are broken
                r = radius * (1.0 + ring_noise(index, ring if ring > 0 else 1, seed, 0.015 if ring < 2 else 0.07))
                h = height + ring_noise(index, ring + 20, seed, 0.01 if ring < 2 else 0.035)
                row.append((r * math.cos(angle), r * math.sin(angle), h))
            points.append(row)
        for ring in range(len(points) - 1):
            inner, outer = points[ring], points[ring + 1]
            for index in range(count):
                a, b = inner[index], inner[(index + 1) % count]
                c, d = outer[index], outer[(index + 1) % count]
                middle = [(a[axis] + d[axis]) * 0.5 for axis in range(3)]
                # the inner edge faces the middle of the mouth, the rest up and out
                outward = (-middle[0], -middle[1], 0.0) if ring == 0 else (middle[0] * 0.3, middle[1] * 0.3, 1.0)
                self.add(a, c, d, outward)
                self.add(a, d, b, outward)
        # boulders on the crest, some leaning in over the mouth, and smaller stones down the slope
        for stone in range(9):
            angle = (stone + (hash_unit(stone, 1, seed) - 0.5) * 0.6) / 9 * 2.0 * math.pi
            distance = 0.47 + 0.14 * hash_unit(stone, 2, seed)
            size = 0.06 + 0.06 * hash_unit(stone, 3, seed)
            self.add_rock(seed * 13 + stone * 7 + 1, size, 0.7 + 0.5 * hash_unit(stone, 4, seed), angle, distance, HOLE_HEIGHT + 0.06,
                          lean=-0.3 * hash_unit(stone, 5, seed))
        for stone in range(7):
            angle = hash_unit(stone, 11, seed) * 2.0 * math.pi
            distance = 0.7 + 0.22 * hash_unit(stone, 12, seed)
            size = 0.03 + 0.03 * hash_unit(stone, 13, seed)
            self.add_rock(seed * 17 + stone * 5 + 3, size, 0.6, angle, distance, 0.1 - 0.12 * (distance - 0.7) / 0.22)
        # the crystals, which glow in the color of the network, two clusters on the crest
        for cluster in range(2):
            base = (cluster + 0.25 * hash_unit(cluster, 21, seed)) * math.pi
            for crystal in range(3):
                angle = base + (hash_unit(cluster * 3 + crystal, 22, seed) - 0.5) * 0.35
                self.add_crystal(angle, 0.5 + 0.06 * hash_unit(cluster * 3 + crystal, 23, seed), HOLE_HEIGHT + 0.08,
                                 0.018 + 0.01 * hash_unit(cluster * 3 + crystal, 24, seed), 0.07 + 0.08 * hash_unit(cluster * 3 + crystal, 25, seed),
                                 0.35 + 0.35 * hash_unit(cluster * 3 + crystal, 26, seed), hash_unit(cluster * 3 + crystal, 27, seed), 1.0)


class EntranceHole(MarkerMesh):
    """The mouth of a pit, a flat disc HOLE_RADIUS wide at HOLE_HEIGHT facing up, whose material draws the shaft below it"""

    def __init__(self):
        super().__init__()
        count = MARKER_SEGMENTS * 3
        center = (0.0, 0.0, HOLE_HEIGHT)
        for index in range(count):
            a0, a1 = index / count * 2.0 * math.pi, (index + 1) / count * 2.0 * math.pi
            self.add(center, (HOLE_RADIUS * math.cos(a0), HOLE_RADIUS * math.sin(a0), HOLE_HEIGHT),
                     (HOLE_RADIUS * math.cos(a1), HOLE_RADIUS * math.sin(a1), HOLE_HEIGHT), (0.0, 0.0, 1.0))


class ExitStairs(MarkerMesh):
    """The exit of a network underground: stone stairs winding up a rock in its hall to a glyph on its flat top that glows in the color of the
    network, where the shaft of daylight falls; the red of a vertex is how much of that daylight it takes, all of it on the top"""

    def __init__(self, seed):
        super().__init__()
        count = MARKER_SEGMENTS * 2
        top = EXIT_TOP
        # the rock, rings up from its foot to the edge of its top: (radius, height)
        rings = [(0.72, -0.04), (0.69, 0.12), (0.64, 0.34), (0.57, 0.58), (0.51, 0.8), (0.47, 0.98), (0.44, top)]

        def daylight(height):
            return 0.6 * smoothstep(0.5, top, height)

        points = []
        for ring, (radius, height) in enumerate(rings):
            row = []
            last = ring == len(rings) - 1
            for index in range(count):
                angle = (index + 0.5 * (ring % 2)) / count * 2.0 * math.pi
                r = radius * (1.0 + ring_noise(index, ring, seed, 0.02 if last else 0.07))
                h = height if last or ring == 0 else height + ring_noise(index, ring + 20, seed, 0.03)
                row.append((r * math.cos(angle), r * math.sin(angle), h))
            points.append(row)
        for ring in range(len(points) - 1):
            inner, outer = points[ring], points[ring + 1]
            for index in range(count):
                a, b = inner[index], inner[(index + 1) % count]
                c, d = outer[index], outer[(index + 1) % count]
                middle = [(a[axis] + d[axis]) * 0.5 for axis in range(3)]
                outward = (middle[0], middle[1], 0.25)
                self.add(a, c, d, outward, red=(daylight(a[2]), daylight(c[2]), daylight(d[2])))
                self.add(a, d, b, outward, red=(daylight(a[2]), daylight(d[2]), daylight(b[2])))
        # its flat top, worked stone in the daylight
        edge = points[-1]
        for index in range(count):
            self.add((0.0, 0.0, top), edge[index], edge[(index + 1) % count], (0.0, 0.0, 1.0), red=1.0, green=0.7)

        # the stairs: steps of worked stone up along a helix from the foot to the top, each a block standing out of the rock down to the step below,
        # more of the daylight on the higher ones
        steps = 15
        sweep = 1.15 * 2.0 * math.pi
        start = hash_unit(1, 31, seed) * 2.0 * math.pi
        rise = top / steps

        def rock_radius(height):
            for (r0, h0), (r1, h1) in zip(rings, rings[1:]):
                if height <= h1:
                    return r0 + (r1 - r0) * (height - h0) / max(h1 - h0, 1e-6)
            return rings[-1][0]

        for step in range(steps):
            z1 = (step + 1) * rise * (1.0 + ring_noise(step, 32, seed, 0.04) if step < steps - 1 else 1.0)
            z0 = max(z1 - 2.0 * rise, -0.04)
            angle = start + (step + 0.5) / steps * sweep
            half = 0.5 * sweep / steps * 0.92
            r_in = rock_radius(z1) - 0.05
            r_out = rock_radius(z1) + 0.2 + ring_noise(step, 33, seed, 0.02)
            light = 0.35 + 0.65 * ((step + 1) / steps) ** 2
            # each step its own tone of the stone
            stone = 0.7 + 0.3 * hash_unit(step, 34, seed)

            def corner(radius, turn, height):
                return (radius * math.cos(turn), radius * math.sin(turn), height)

            lo, hi = angle - half, angle + half
            top_face = [corner(r_in, lo, z1), corner(r_out, lo, z1), corner(r_out, hi, z1), corner(r_in, hi, z1)]
            self.add_quad(*top_face, (0.0, 0.0, 1.0), red=light, green=stone)
            # its front, out from the rock, and its two ends along the helix
            self.add_quad(corner(r_out, lo, z0), corner(r_out, hi, z0), corner(r_out, hi, z1), corner(r_out, lo, z1),
                          (math.cos(angle), math.sin(angle), 0.0), red=light * 0.5, green=stone)
            for turn, sign in ((lo, -1.0), (hi, 1.0)):
                self.add_quad(corner(r_in, turn, z0), corner(r_out, turn, z0), corner(r_out, turn, z1), corner(r_in, turn, z1),
                              (-math.sin(turn) * sign, math.cos(turn) * sign, 0.0), red=light * 0.4, green=stone)

        # the glyph on the top: a flat ring and a disc a little above it, which glow
        glyph = MARKER_SEGMENTS * 2
        for index in range(glyph):
            a0, a1 = index / glyph * 2.0 * math.pi, (index + 1) / glyph * 2.0 * math.pi
            for inner, outer, glow in ((0.2, 0.27, 1.0), (0.0, 0.1, 0.7)):
                p = [(inner * math.cos(a0), inner * math.sin(a0), top + 0.012), (outer * math.cos(a0), outer * math.sin(a0), top + 0.012),
                     (outer * math.cos(a1), outer * math.sin(a1), top + 0.012), (inner * math.cos(a1), inner * math.sin(a1), top + 0.012)]
                self.add_quad(p[0], p[1], p[2], p[3], (0.0, 0.0, 1.0), glow, 1.0, 0.7)

        # stones fallen at the foot of the rock
        for stone in range(7):
            angle = hash_unit(stone, 41, seed) * 2.0 * math.pi
            distance = 0.78 + 0.16 * hash_unit(stone, 42, seed)
            size = 0.05 + 0.06 * hash_unit(stone, 43, seed)
            self.add_rock(seed * 19 + stone * 3 + 5, size, 0.7, angle, distance, -0.02)


class Shaft:
    """A shaft of light over an exit: an open cone, wider at the top, of height 1 and radius 0.55 at its foot, whose red is how far up it is"""

    def __init__(self):
        self.triangles = []
        rows = 6
        for row in range(rows):
            z0, z1 = row / rows, (row + 1) / rows
            r0, r1 = 0.55 + 0.35 * z0, 0.55 + 0.35 * z1
            for index in range(MARKER_SEGMENTS * 2):
                a0, a1 = index / (MARKER_SEGMENTS * 2) * 2.0 * math.pi, (index + 1) / (MARKER_SEGMENTS * 2) * 2.0 * math.pi
                p = [(r0 * math.cos(a0), r0 * math.sin(a0), z0), (r0 * math.cos(a1), r0 * math.sin(a1), z0),
                     (r1 * math.cos(a1), r1 * math.sin(a1), z1), (r1 * math.cos(a0), r1 * math.sin(a0), z1)]
                for corners in ((p[0], p[1], p[2]), (p[0], p[2], p[3])):
                    self.triangles.append(corners)

    def get_vertices(self):
        """By corner of each triangle: position, a smooth normal out from the axis, and the color with its height in red"""
        result = []
        for corners in self.triangles:
            a, b, c = corners
            u = [c[axis] - a[axis] for axis in range(3)]
            v = [b[axis] - a[axis] for axis in range(3)]
            cross = [u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]]
            middle = [(a[axis] + b[axis] + c[axis]) / 3.0 for axis in range(3)]
            if cross[0] * middle[0] + cross[1] * middle[1] < 0.0:
                corners = [a, c, b]
            for x, y, z in corners:
                length = math.hypot(x, y) or 1.0
                result.append(((x, y, z), (x / length, y / length, 0.0), (z, 0.0, 0.0, 1.0)))
        return result


def write_marker(triangles, path, name):
    """Writes triangles of (corners, normal, glows, reds, greens) as a binary glTF in meters, a radius of 1 world unit, with the glow in the
    alpha of the colors, the daylight in their red and the worked stone in their green"""
    positions, normals, colors = [], [], []
    for corners, normal, glows, reds, greens in triangles:
        for corner, glow, red, green in zip(corners, glows, reds, greens):
            positions.append(to_file_axes(*(value * 0.01 for value in corner)))
            normals.append(to_file_axes(*normal))
            colors.append((red, green, 0.0, glow))
    write_glb_arrays(path, name, positions, normals, list(range(len(positions))), colors)


def write_shaft(shaft, path, name):
    positions, normals, colors = [], [], []
    for position, normal, color in shaft.get_vertices():
        positions.append(to_file_axes(*(value * 0.01 for value in position)))
        normals.append(to_file_axes(*normal))
        colors.append(color)
    write_glb_arrays(path, name, positions, normals, list(range(len(positions))), colors)


def write_boulder(boulder, path, name):
    """Writes a boulder or a floe as a binary glTF with a vertex per corner of each triangle, in meters, a radius of 1 world unit"""
    positions = []
    normals = []
    for corners, normal in boulder.get_triangles():
        for corner in corners:
            positions.append(to_file_axes(*(value * 0.01 for value in corner)))
            normals.append(to_file_axes(*normal))
    write_glb_arrays(path, name, positions, normals, list(range(len(positions))))


def to_file_axes(x, y, z):
    """The engine imports glTF (x, y, z) as (x, z, y), as for the mountains"""
    return x, z, y


def write_glb(piece, path, name):
    """Writes a binary glTF with positions in meters, as the format requires"""
    positions = [to_file_axes(x * 0.01, y * 0.01, z * 0.01) for x, y, z in (piece.get_position(index) for index in range(len(piece.points)))]
    normals = [to_file_axes(*normal) for normal in piece.compute_normals()]
    indices = [index for triangle in piece.get_triangles() for index in triangle]
    write_glb_arrays(path, name, positions, normals, indices)


def write_glb_arrays(path, name, positions, normals, indices, colors=None):
    """Writes a binary glTF of positions, normals and indices, and with colors their linear RGBA as COLOR_0"""
    count = len(positions)

    position_data = b''.join(struct.pack('<3f', *value) for value in positions)
    normal_data = b''.join(struct.pack('<3f', *value) for value in normals)
    color_data = b''.join(struct.pack('<4f', *value) for value in colors) if colors else b''
    index_data = struct.pack('<%dI' % len(indices), *indices)
    binary = position_data + normal_data + color_data + index_data

    views = []
    offset = 0
    parts = [(position_data, 34962), (normal_data, 34962)] + ([(color_data, 34962)] if colors else []) + [(index_data, 34963)]
    for data, target in parts:
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
        offset += len(data)

    stored = [struct.unpack('<3f', struct.pack('<3f', *value)) for value in positions]
    accessors = [
        {'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': [min(value[axis] for value in stored) for axis in range(3)], 'max': [max(value[axis] for value in stored) for axis in range(3)]},
        {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
    ]
    attributes = {'POSITION': 0, 'NORMAL': 1}
    if colors:
        attributes['COLOR_0'] = len(accessors)
        accessors.append({'bufferView': len(accessors), 'componentType': 5126, 'count': count, 'type': 'VEC4'})
    accessors.append({'bufferView': len(accessors), 'componentType': 5125, 'count': len(indices), 'type': 'SCALAR'})
    document = {
        'asset': {'version': '2.0', 'generator': 'generate_planet_kit.py'},
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


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    for old in os.listdir(OUTPUT_FOLDER):
        if old.endswith('.glb'):
            os.remove(os.path.join(OUTPUT_FOLDER, old))

    manifest = {'hex_radius': HEX_RADIUS, 'foot_depth': FOOT_DEPTH, 'kits': []}
    triangles = 0
    for kit in KITS:
        entry = {'name': kit['name'], 'level': kit['level'], 'color': kit['color'], 'slope_color': kit['slope_color'], 'meshes': {}}
        for mask in kit.get('masks', get_canonical_masks()):
            names = []
            for variant in range(kit['variants'] + 1):
                piece = Piece(kit, mask, variant)
                piece.check_sides()
                name = get_mesh_name(kit, mask, variant)
                write_glb(piece, os.path.join(OUTPUT_FOLDER, name + '.glb'), name)
                names.append(name)
                triangles += len(piece.get_triangles())
            entry['meshes'][str(mask)] = names
        manifest['kits'].append(entry)

    manifest['boulders'] = []
    for index in range(BOULDERS):
        name = 'SM_PlanetBoulder_%d' % index
        write_boulder(Boulder(index * 17 + 5), os.path.join(OUTPUT_FOLDER, name + '.glb'), name)
        manifest['boulders'].append(name)

    manifest['floes'] = []
    manifest['shards'] = []
    for index in range(FLOES + SHARDS):
        shard = index >= FLOES
        name = ('SM_PlanetIceShard_%d' % (index - FLOES)) if shard else ('SM_PlanetFloe_%d' % index)
        write_boulder(Floe(index * 31 + 11, shard), os.path.join(OUTPUT_FOLDER, name + '.glb'), name)
        manifest['shards' if shard else 'floes'].append(name)

    # in a folder of its own, as import_planet_kit.py imports every file of the kit, and import_planet_bridge.py imports it apart
    bridge_folder = os.path.join(os.path.dirname(os.path.abspath(OUTPUT_FOLDER)), 'HexPlanetBridge')
    os.makedirs(bridge_folder, exist_ok=True)
    write_boulder(Bridge(), os.path.join(bridge_folder, 'SM_PlanetBridge.glb'), 'SM_PlanetBridge')

    # the markers of the entrances of the underground, in a folder of their own too, imported by import_planet_markers.py
    marker_folder = os.path.join(os.path.dirname(os.path.abspath(OUTPUT_FOLDER)), 'HexPlanetMarkers')
    os.makedirs(marker_folder, exist_ok=True)
    write_marker(EntranceRim(7).get_triangles(), os.path.join(marker_folder, 'SM_PlanetEntranceRim.glb'), 'SM_PlanetEntranceRim')
    write_marker(EntranceHole().get_triangles(), os.path.join(marker_folder, 'SM_PlanetEntranceHole.glb'), 'SM_PlanetEntranceHole')
    write_marker(ExitStairs(19).get_triangles(), os.path.join(marker_folder, 'SM_PlanetExitStairs.glb'), 'SM_PlanetExitStairs')
    write_shaft(Shaft(), os.path.join(marker_folder, 'SM_PlanetExitShaft.glb'), 'SM_PlanetExitShaft')

    with open(os.path.join(OUTPUT_FOLDER, 'kit.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=2)
    print('Wrote %d pieces with %d triangles to %s' % (sum(len(names) for kit in manifest['kits'] for names in kit['meshes'].values()), triangles, OUTPUT_FOLDER))


main()
