"""Generates the set of mountain meshes that join across the sides of hexes.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_hex_mountains.py

A mesh is chosen by which of the 6 neighbors are also mountains. Bit N of a mask means the neighbor in direction N of
FHexCoord, at an angle of 30 + 60 * N degrees from +X towards +Y. Turned copies cover the rest, so 64 masks need 14 meshes.

What neighbors must agree on depends only on the masks:
- A side towards a mountain has a saddle in its middle. Its height depends only on the distance from that middle,
  because the neighbor walks along the same side in the opposite direction.
- A corner between two mountain neighbors is raised, as all three hexes know.
- A corner between one mountain neighbor and anything else is a notch in the outline of the range. It is raised to NOTCH_HEIGHT.
- Every other corner is at the foot.
  A mesh goes on past its side there, up to halfway to the center of the lower hex, so a range stands on a straight line. The side towards the
  other mountain goes on as a seam that both meshes share, falling from the notch to the foot.
- A side towards anything else, and the outer edge of what lies past a side, is at the foot and faces straight up. The material moves it onto the terrain,
  so it meets the terrain without a gap whatever level the neighbor has.
  JoinedMeshFootHeight in the mountain level asset repeats it.
"""

import json
import math
import os
import struct
import sys
import zlib

# the values that the game shares, see mountain_params.json
PARAMS_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mountain_params.json')
with open(PARAMS_PATH, encoding='utf-8') as params_file:
    PARAMS = json.load(params_file)

HEX_RADIUS = PARAMS['hex_radius']
SUBDIVISIONS = 24  # must be even, so the middle of a side is a vertex
PEAK_HEIGHT = PARAMS['peak_height']
SADDLE_HEIGHT = PARAMS['saddle_height']
FOOT_HEIGHT = PARAMS['foot_height']
NOTCH_HEIGHT = PARAMS['notch_height']
# how far the base reaches into a lower hex where two mountains meet it, as a part of the way from the corner to the center of that hex
NOTCH_FILL = PARAMS['notch_fill']
# how far a side towards a mountain goes on past a notch, falling to the foot. The base of a straight range has this profile all along it
NOTCH_REACH = NOTCH_FILL * HEX_RADIUS
# how far the slopes near an edge at the foot fall to it, which matters only where the base does not reach past the side
FOOT_FADE = PARAMS['foot_fade']
RAISED_CORNER_HEIGHT = NOTCH_HEIGHT
TOP_RADIUS = 48.0
# the slope of a mountain away from the sides towards a mountain: its height on the ridge line before a top is added, how far out it reaches
# the foot and how concave it is
FREE_HEIGHT = SADDLE_HEIGHT + 25.0
FREE_RADIUS = HEX_RADIUS * 0.97
FREE_POWER = 1.3
SMOOTH_PASSES = 3

# how the top of each mesh looks, by mesh mask. Tops stay inside the hex, so they do not affect how meshes join
PEAK_TOP, FLAT_TOP, HORNS_TOP = 'peak', 'flat', 'horns'
TOP_STYLES = {0: HORNS_TOP, 1: FLAT_TOP, 3: PEAK_TOP, 5: HORNS_TOP, 7: FLAT_TOP, 9: PEAK_TOP, 11: HORNS_TOP, 13: FLAT_TOP, 15: HORNS_TOP, 21: FLAT_TOP, 23: PEAK_TOP, 27: HORNS_TOP, 31: FLAT_TOP, 63: HORNS_TOP}
TOP_CYCLE = [PEAK_TOP, HORNS_TOP, FLAT_TOP]
PLATEAU_RADIUS = 44.0
PLATEAU_HEIGHT_SCALE = 0.72
HORN_BASE_HEIGHT_SCALE = 0.75
HORN_RADIUS = 36.0
HORN_HEIGHT_SCALE = 0.4

CORNER_STEPS = [(1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1), (1, -1)]
DIRECTION_OFFSETS = [(1, -1), (1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1)]

FOOT_COLOR = (0.20, 0.13, 0.07)
ROCK_COLOR = (0.17, 0.14, 0.12)
DARK_ROCK_COLOR = (0.07, 0.06, 0.055)
SNOW_COLOR = (0.85, 0.87, 0.92)

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexMountains'))


def rotate_mask(mask, turns):
    turns %= 6
    return ((mask << turns) | (mask >> (6 - turns))) & 63


def get_canonical(mask):
    """Returns the mesh mask of a neighbor mask and the turns of 60 degrees that place that mesh."""
    best = min(rotate_mask(mask, turns) for turns in range(6))
    return best, next(turns for turns in range(6) if rotate_mask(best, turns) == mask)


def get_mesh_name(mask, variant=0):
    name = 'SM_HexMountain_' + ''.join('1' if mask >> bit & 1 else '0' for bit in range(6))
    return name + '_V%d' % variant if variant else name


def lattice_to_position(a, b):
    step = HEX_RADIUS / SUBDIVISIONS
    return (a + b * 0.5) * step, b * math.sqrt(3.0) * 0.5 * step


def position_to_lattice(x, y):
    step = HEX_RADIUS / SUBDIVISIONS
    fractional_b = y / (math.sqrt(3.0) * 0.5 * step)
    fractional_a = x / step - fractional_b * 0.5
    return round(fractional_a), round(fractional_b)


def get_hex_distance(a, b):
    return max(abs(a), abs(b), abs(a + b))


def smoothstep(low, high, value):
    t = min(1.0, max(0.0, (value - low) / (high - low)))
    return t * t * (3.0 - 2.0 * t)


def lerp(a, b, t):
    return a + (b - a) * t


def hash_unit(x, y, seed):
    value = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    value = ((value ^ (value >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((value ^ (value >> 16)) & 0xFFFFFF) / 0xFFFFFF


def value_noise(x, y, seed):
    floor_x, floor_y = math.floor(x), math.floor(y)
    u, v = smoothstep(0.0, 1.0, x - floor_x), smoothstep(0.0, 1.0, y - floor_y)
    bottom = lerp(hash_unit(floor_x, floor_y, seed), hash_unit(floor_x + 1, floor_y, seed), u)
    top = lerp(hash_unit(floor_x, floor_y + 1, seed), hash_unit(floor_x + 1, floor_y + 1, seed), u)
    return lerp(bottom, top, v) * 2.0 - 1.0


def ridged_noise(x, y, seed):
    """Sharp crests from about -1 to 1."""
    result, amplitude, total = 0.0, 1.0, 0.0
    for octave in range(4):
        crest = 1.0 - abs(value_noise(x, y, seed + octave))
        result += (crest * crest * 2.0 - 1.0) * amplitude
        total += amplitude
        amplitude *= 0.42
        x, y = x * 2.03 + 3.1, y * 2.03 + 1.7
    return result / total


def get_side_height(distance_from_middle, end_height):
    """Height along a side towards a mountain. distance_from_middle is 0 in the middle and 1 at a corner."""
    closeness = 1.0 - distance_from_middle
    wobble = math.sin(distance_from_middle * 9.0) * 7.0 + math.sin(distance_from_middle * 23.0 + 1.0) * 3.0
    # the wobble has no slope in the middle, where the two walks along the side meet head on
    # the side stays high until close to its end, so neighbors do not look like separate peaks with a deep valley between them
    return lerp(end_height, SADDLE_HEIGHT, smoothstep(0.0, 1.0, closeness ** 0.6)) + wobble * 2.0 * closeness * distance_from_middle * distance_from_middle


def get_notch_side_height(distance_from_notch):
    """Height along the half of an open side next to a notch. distance_from_notch is 0 at the notch and 1 at NOTCH_REACH."""
    return FOOT_HEIGHT + (NOTCH_HEIGHT - FOOT_HEIGHT) * (1.0 - smoothstep(0.0, 1.0, distance_from_notch))


def get_ridge_profile(distance):
    """Height at a distance from a ridge. It repeats the sides: the middle of a side towards a mountain is on the ridge, and the raised half of an open side
    goes from half a side to three quarters of a side away from it."""
    half_side = HEX_RADIUS * 0.5
    if distance <= half_side:
        return get_side_height(distance / half_side, NOTCH_HEIGHT)
    if distance <= half_side + NOTCH_REACH:
        return get_notch_side_height((distance - half_side) / NOTCH_REACH)
    return FOOT_HEIGHT


def get_free_profile(distance):
    """Height at a distance from a ridge far from the sides towards a mountain, where nothing has to match. A slope steeper at the top and gentler
    at the bottom, which reaches the foot only at FREE_RADIUS, so a lone mountain fills its hex and has no shelf at the height of a notch."""
    t = min(1.0, distance / FREE_RADIUS)
    return FOOT_HEIGHT + (FREE_HEIGHT - FOOT_HEIGHT) * (1.0 - t) ** FREE_POWER


def get_notch_side_normal(distance_from_notch, walk_x, walk_y):
    """Normal there. No slope across the side, so the mountain and the notch mesh give it the same normal. Walk is the direction away from the notch."""
    # both ends are flat, and every mesh that reaches them must agree whichever way it comes
    if distance_from_notch <= 0.0 or distance_from_notch >= 1.0:
        return (0.0, 0.0, 1.0)

    delta = 0.002
    low = max(0.0, distance_from_notch - delta)
    high = min(1.0, distance_from_notch + delta)
    slope = (get_notch_side_height(high) - get_notch_side_height(low)) / ((high - low) * NOTCH_REACH)
    length = math.hypot(walk_x, walk_y)
    normal = (-slope * walk_x / length, -slope * walk_y / length, 1.0)
    size = math.sqrt(sum(value * value for value in normal))
    return tuple(value / size for value in normal)


def get_side_slope(distance_from_middle, end_height):
    """Height change per world unit along a side, walking towards the corner."""
    half_side = HEX_RADIUS * 0.5
    delta = 0.002
    low = max(0.0, distance_from_middle - delta)
    high = min(1.0, distance_from_middle + delta)
    return (get_side_height(high, end_height) - get_side_height(low, end_height)) / ((high - low) * half_side)


def get_notch_steps():
    """Lattice steps from a notch to the far end of the base in the lower hex. A whole number, so that end is a vertex"""
    return max(1, round(SUBDIVISIONS * NOTCH_FILL))


def get_lattice_reach():
    """How far a mesh reaches on its lattice, counted along either axis, as the far end of a notch fill lies that many steps away"""
    return SUBDIVISIONS + get_notch_steps()


def get_mesh_radius():
    """The farthest that a mesh reaches from the center of its hex, at the far end of a notch fill"""
    fill = NOTCH_FILL * HEX_RADIUS
    return math.sqrt(HEX_RADIUS * HEX_RADIUS + fill * fill + HEX_RADIUS * fill)


def get_line_distance(x, y, start, end):
    along_x, along_y = end[0] - start[0], end[1] - start[1]
    alpha = max(0.0, min(1.0, ((x - start[0]) * along_x + (y - start[1]) * along_y) / (along_x * along_x + along_y * along_y)))
    return math.hypot(x - start[0] - along_x * alpha, y - start[1] - along_y * alpha)


class Mountain:
    def __init__(self, mask, variant=0):
        self.mask = mask
        self.variant = variant
        self.seed = 17 + mask * 31 + variant * 101
        self.indices = {}
        self.points = []
        self.fixed = {}
        self.normals_on_sides = {}
        self.top_style = TOP_CYCLE[(TOP_CYCLE.index(TOP_STYLES[mask]) + variant) % len(TOP_CYCLE)]
        self.plateau = set()
        self.horns = []

        self.extensions = self.get_extensions()
        reach = get_lattice_reach()
        for a in range(-reach, reach + 1):
            for b in range(-reach, reach + 1):
                if self.contains(a, b):
                    self.indices[(a, b)] = len(self.points)
                    self.points.append((a, b))

        self.add_sides()
        self.add_tops()
        self.shape()
        self.smooth()
        self.add_relief()
        self.compute_normals()
        self.compute_colors()

    def is_connected(self, direction):
        return self.mask >> (direction % 6) & 1 == 1

    def is_corner_raised(self, corner):
        return self.is_connected(corner - 1) and self.is_connected(corner)

    def is_corner_notch(self, corner):
        return self.is_connected(corner - 1) != self.is_connected(corner)

    def get_corner_height(self, corner):
        if self.is_corner_raised(corner):
            return RAISED_CORNER_HEIGHT
        return NOTCH_HEIGHT if self.is_corner_notch(corner) else FOOT_HEIGHT

    def get_side_notches(self, direction):
        """Returns which ends of a side towards something else are notches."""
        return self.is_corner_notch(direction), self.is_corner_notch(direction + 1)

    def get_extensions(self):
        """The parts of the base past the sides, as outlines of lattice points. In a notch of the lower hex the base reaches halfway to its center, so a range stands on a straight line."""
        extensions = []
        # the far edges of what lies past the sides. That of a triangle does not follow the lattice, so a mesh needs its vertices moved onto it
        self.outer_edges = []
        for direction in range(6):
            if self.is_connected(direction):
                continue
            at_start, at_end = self.get_side_notches(direction)
            start, end = CORNER_STEPS[direction], CORNER_STEPS[(direction + 1) % 6]
            half = get_notch_steps()
            first = (start[0] * SUBDIVISIONS, start[1] * SUBDIVISIONS)
            second = (end[0] * SUBDIVISIONS, end[1] * SUBDIVISIONS)
            outline = [first, second]
            if at_end:
                outline.append((second[0] + start[0] * half, second[1] + start[1] * half))
            if at_start:
                outline.append((first[0] + end[0] * half, first[1] + end[1] * half))
            if len(outline) > 2:
                extensions.append([lattice_to_position(*point) for point in outline])
            if len(outline) == 3:
                self.outer_edges.append((extensions[-1][2], extensions[-1][0]) if at_end else (extensions[-1][1], extensions[-1][2]))
            elif len(outline) == 4:
                self.outer_edges.append((extensions[-1][2], extensions[-1][3]))
        return extensions

    def contains(self, a, b):
        if get_hex_distance(a, b) <= SUBDIVISIONS:
            return True
        x, y = lattice_to_position(a, b)
        for outline in self.extensions:
            crosses = [(outline[(i + 1) % len(outline)][0] - outline[i][0]) * (y - outline[i][1]) - (outline[(i + 1) % len(outline)][1] - outline[i][1]) * (x - outline[i][0]) for i in range(len(outline))]
            if min(crosses) >= -0.001 or max(crosses) <= 0.001:
                return True
        return False

    def add_sides(self):
        for direction in range(6):
            start = CORNER_STEPS[direction]
            end = CORNER_STEPS[(direction + 1) % 6]

            for step in range(SUBDIVISIONS + 1):
                point = (start[0] * SUBDIVISIONS + (end[0] - start[0]) * step, start[1] * SUBDIVISIONS + (end[1] - start[1]) * step)

                along = step * 2.0 / SUBDIVISIONS - 1.0
                corner = direction if along < 0.0 else direction + 1

                # past a side with a notch the base goes on, so the side itself is nothing special
                if not self.is_connected(direction):
                    if not any(self.get_side_notches(direction)):
                        self.fixed[point] = FOOT_HEIGHT
                        self.normals_on_sides[point] = (0.0, 0.0, 1.0)
                    continue

                end_height = self.get_corner_height(corner)
                self.fixed[point] = get_side_height(abs(along), end_height)

                # no slope across the side, so both hexes give it the same normal
                walk_x, walk_y = lattice_to_position(end[0] - start[0], end[1] - start[1])
                length = math.hypot(walk_x, walk_y)
                slope = get_side_slope(abs(along), end_height) * (1.0 if along >= 0.0 else -1.0)
                normal = (-slope * walk_x / length, -slope * walk_y / length, 1.0)

                # the middle of a side and every corner face up whichever way they are reached
                if along == 0.0 or abs(along) == 1.0:
                    normal = (0.0, 0.0, 1.0)

                size = math.sqrt(sum(value * value for value in normal))
                self.normals_on_sides[point] = tuple(value / size for value in normal)

        # a side towards a mountain goes on through a notch, and both mountains share that line
        half = get_notch_steps()
        for corner in range(6):
            if not self.is_corner_notch(corner):
                continue
            start = CORNER_STEPS[corner % 6]
            walk = CORNER_STEPS[(corner + 1) % 6] if self.is_connected(corner - 1) else CORNER_STEPS[(corner - 1) % 6]
            walk_x, walk_y = lattice_to_position(*walk)
            for step in range(half + 1):
                point = (start[0] * SUBDIVISIONS + walk[0] * step, start[1] * SUBDIVISIONS + walk[1] * step)
                self.fixed[point] = get_notch_side_height(step / half)
                self.normals_on_sides[point] = get_notch_side_normal(step / half, walk_x, walk_y)

        # the rest of the outer edge lies at the foot
        for point in self.points:
            if point not in self.fixed and any((point[0] + step[0], point[1] + step[1]) not in self.indices for step in CORNER_STEPS):
                self.fixed[point] = FOOT_HEIGHT
                self.normals_on_sides[point] = (0.0, 0.0, 1.0)

        # measured to the lines themselves, as distances to separate points would leave ripples along an edge
        corners = [lattice_to_position(step[0] * SUBDIVISIONS, step[1] * SUBDIVISIONS) for step in CORNER_STEPS]
        foot_lines = list(self.outer_edges)
        other_lines = []
        for direction in range(6):
            line = (corners[direction], corners[(direction + 1) % 6])
            if self.is_connected(direction):
                other_lines.append(line)
            elif not any(self.get_side_notches(direction)):
                foot_lines.append(line)
        half = get_notch_steps()
        for corner in range(6):
            if self.is_corner_notch(corner):
                walk = CORNER_STEPS[(corner + 1) % 6] if self.is_connected(corner - 1) else CORNER_STEPS[(corner - 1) % 6]
                start = CORNER_STEPS[corner % 6]
                other_lines.append((corners[corner % 6], lattice_to_position(start[0] * SUBDIVISIONS + walk[0] * half, start[1] * SUBDIVISIONS + walk[1] * half)))

        self.foot_distance = []
        self.edge_distance = []
        for point in self.points:
            x, y = lattice_to_position(*point)
            to_foot = min((get_line_distance(x, y, *line) for line in foot_lines), default=HEX_RADIUS * 2.0)
            self.foot_distance.append(to_foot)
            self.edge_distance.append(0.0 if point in self.fixed else min([to_foot] + [get_line_distance(x, y, *line) for line in other_lines]))

    def get_skeleton_distance(self, x, y):
        """Distance to the ridge line, which runs from the center of the hex to the middle of every side towards a mountain."""
        best = math.hypot(x, y)
        for direction in range(6):
            if self.is_connected(direction):
                angle = math.radians(30.0 + 60.0 * direction)
                along_x, along_y = math.cos(angle), math.sin(angle)
                along = min(HEX_RADIUS * math.sqrt(3.0) * 0.5, max(0.0, x * along_x + y * along_y))
                best = min(best, math.hypot(x - along_x * along, y - along_y * along))
        return best

    def get_distance_to_joined_sides(self, x, y):
        """Distance to the nearest side towards a mountain, where the height is shared with the neighbor."""
        apothem = HEX_RADIUS * math.sqrt(3.0) * 0.5
        best = apothem * 2.0
        for direction in range(6):
            if self.is_connected(direction):
                angle = math.radians(30.0 + 60.0 * direction)
                best = min(best, apothem - x * math.cos(angle) - y * math.sin(angle))
        return best

    def add_tops(self):
        self.peak = PEAK_HEIGHT + hash_unit(self.mask, 3, self.seed) * 20.0 - 10.0
        self.top_height = self.peak
        if self.top_style == FLAT_TOP:
            self.top_height = self.peak * PLATEAU_HEIGHT_SCALE
            self.add_plateau()
        elif self.top_style == HORNS_TOP:
            self.top_height = self.peak * HORN_BASE_HEIGHT_SCALE
            self.add_horns()

    def add_plateau(self):
        """Chooses an uneven round area with a little relief, from which the slopes fall away."""
        for point in self.points:
            x, y = lattice_to_position(*point)
            angle = math.atan2(y, x)
            edge = PLATEAU_RADIUS * (1.0 + 0.22 * math.sin(angle * 3.0 + self.seed) + 0.1 * math.sin(angle * 7.0 + self.seed * 2.0))
            if math.hypot(x, y) <= edge:
                self.plateau.add(point)

    def add_horns(self):
        """Chooses a few separate sharp tops around the center. Their heights are added with the relief."""
        count = 2 + int(hash_unit(self.mask, 11, self.seed) * 3.0)
        start = hash_unit(self.mask, 13, self.seed) * math.tau
        for horn in range(count):
            angle = start + math.tau * (horn + (hash_unit(horn, 17, self.seed) - 0.5) * 0.4) / count
            distance = lerp(26.0, 44.0, hash_unit(horn, 19, self.seed))
            height = self.peak * HORN_HEIGHT_SCALE * lerp(0.7, 1.15, hash_unit(horn, 23, self.seed))
            self.horns.append((math.cos(angle) * distance, math.sin(angle) * distance, height))

    def shape(self):
        """Every cut across a ridge has the same profile as a side towards a mountain, so a straight range has no seams to show.
        A top rises from the ridge away from those sides, which gives peaks and saddles along the range."""
        self.heights = [0.0] * len(self.points)
        for index, point in enumerate(self.points):
            if point in self.fixed:
                self.heights[index] = self.fixed[point]
                continue
            x, y = lattice_to_position(*point)
            distance = self.get_skeleton_distance(x, y)
            away = smoothstep(0.0, HEX_RADIUS * 0.6, self.get_distance_to_joined_sides(x, y))
            top = max(0.0, self.top_height - SADDLE_HEIGHT) * away
            profile = lerp(get_ridge_profile(distance), get_free_profile(distance), away)
            height = profile + top * (1.0 - smoothstep(0.0, TOP_RADIUS, distance))
            self.heights[index] = FOOT_HEIGHT + (height - FOOT_HEIGHT) * smoothstep(0.0, FOOT_FADE, self.foot_distance[index])

    def smooth(self):
        """Rounds the crests a little, because a single fixed vertex would stand out as a needle."""
        for _ in range(SMOOTH_PASSES):
            heights = list(self.heights)
            for index, point in enumerate(self.points):
                if point not in self.fixed:
                    around = [heights[self.indices[(point[0] + step[0], point[1] + step[1])]] for step in CORNER_STEPS]
                    self.heights[index] = lerp(heights[index], sum(around) / 6.0, 0.6)

    def add_relief(self):
        for index, point in enumerate(self.points):
            distance_from_side = self.edge_distance[index] / HEX_RADIUS
            if distance_from_side <= 0.0:
                continue

            x, y = lattice_to_position(*point)
            sample_x, sample_y = x / 75.0 + (self.mask + self.variant * 11) * 7.3, y / 75.0 + (self.mask + self.variant * 11) * 3.9
            warp = value_noise(sample_x * 0.7, sample_y * 0.7, self.seed + 40) * 0.5
            relief = 12.0 * (0.25 if point in self.plateau else 1.0)
            self.heights[index] += ridged_noise(sample_x + warp, sample_y - warp, self.seed + 50) * relief * smoothstep(0.0, 0.35, distance_from_side)

            for horn_x, horn_y, horn_height in self.horns:
                closeness = 1.0 - math.hypot(x - horn_x, y - horn_y) / HORN_RADIUS
                if closeness > 0.0:
                    self.heights[index] += horn_height * closeness ** 1.4

    def get_triangles(self):
        """Returns lattice triangles whose winding faces up in the renderer: (C - A) x (B - A) points up."""
        triangles = []
        for (a, b) in self.points:
            right, up, both = (a + 1, b), (a, b + 1), (a + 1, b + 1)
            left_up = (a - 1, b + 1)
            if right in self.indices and up in self.indices:
                triangles.append((self.indices[(a, b)], self.indices[up], self.indices[right]))
            if up in self.indices and left_up in self.indices:
                triangles.append((self.indices[(a, b)], self.indices[left_up], self.indices[up]))
        return triangles

    def get_position(self, index):
        x, y = lattice_to_position(*self.points[index])
        return x, y, self.heights[index]

    def compute_normals(self):
        self.normals = [[0.0, 0.0, 0.0] for _ in self.points]
        for triangle in self.get_triangles():
            a, b, c = [self.get_position(index) for index in triangle]
            u = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            v = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            cross = (v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0])
            for index in triangle:
                for axis in range(3):
                    self.normals[index][axis] += cross[axis]

        for index, point in enumerate(self.points):
            if point in self.normals_on_sides:
                self.normals[index] = list(self.normals_on_sides[point])
            else:
                size = math.sqrt(sum(value * value for value in self.normals[index])) or 1.0
                self.normals[index] = [value / size for value in self.normals[index]]

    def compute_colors(self):
        """A color depends only on height and normal, which neighbors share along a side."""
        self.colors = []
        for index in range(len(self.points)):
            height = self.heights[index]
            up = self.normals[index][2]
            strata = 0.5 + 0.5 * math.sin(height * 0.16 + math.sin(height * 0.05) * 2.0)
            rock = [lerp(DARK_ROCK_COLOR[axis], ROCK_COLOR[axis], lerp(0.55 + strata * 0.45, 1.0, smoothstep(0.45, 0.85, up))) for axis in range(3)]
            foot = 1.0 - smoothstep(0.0, 30.0, height)
            snow = smoothstep(78.0, 105.0, height + (up - 0.6) * 40.0) * smoothstep(0.35, 0.6, up)
            if self.points[index] in self.plateau:
                snow *= 0.2
            color = [lerp(lerp(rock[axis], FOOT_COLOR[axis], foot), SNOW_COLOR[axis], snow) for axis in range(3)]
            self.colors.append(color)


def to_file_axes(x, y, z):
    """The engine imports glTF (x, y, z) as (x, z, y). The import script reports this, so it can be checked."""
    return x, z, y


def transform(point, turns, offset=(0.0, 0.0)):
    angle = math.radians(60.0 * turns)
    cos, sin = math.cos(angle), math.sin(angle)
    return point[0] * cos - point[1] * sin + offset[0], point[0] * sin + point[1] * cos + offset[1], point[2]


def hex_to_world(q, r):
    return -1.5 * HEX_RADIUS * r, math.sqrt(3.0) * HEX_RADIUS * (q + r * 0.5)


def write_glb(mountain, path, name=None):
    """Writes a binary glTF, which keeps normals and vertex colors. Positions are in meters, as the format requires."""
    name = name or get_mesh_name(mountain.mask)
    positions = [mountain.get_position(index) for index in range(len(mountain.points))]
    positions = [to_file_axes(x * 0.01, y * 0.01, z * 0.01) for x, y, z in positions]
    normals = [to_file_axes(*normal) for normal in mountain.normals]
    indices = [index for triangle in mountain.get_triangles() for index in triangle]
    count = len(positions)

    position_data = b''.join(struct.pack('<3f', *value) for value in positions)
    normal_data = b''.join(struct.pack('<3f', *value) for value in normals)
    color_data = b''.join(struct.pack('<3f', *value) for value in mountain.colors)
    index_data = struct.pack('<%dI' % len(indices), *indices)
    binary = position_data + normal_data + color_data + index_data

    views = []
    offset = 0
    for data, target in ((position_data, 34962), (normal_data, 34962), (color_data, 34962), (index_data, 34963)):
        views.append({'buffer': 0, 'byteOffset': offset, 'byteLength': len(data), 'target': target})
        offset += len(data)

    # the file stores single precision, so the limits must be taken from what was stored
    stored = [struct.unpack('<3f', struct.pack('<3f', *value)) for value in positions]
    document = {
        'asset': {'version': '2.0', 'generator': 'generate_hex_mountains.py'},
        'scene': 0,
        'scenes': [{'nodes': [0]}],
        'nodes': [{'mesh': 0, 'name': name}],
        'meshes': [{'name': name, 'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'COLOR_0': 2}, 'indices': 3}]}],
        'accessors': [
            {'bufferView': 0, 'componentType': 5126, 'count': count, 'type': 'VEC3', 'min': [min(value[axis] for value in stored) for axis in range(3)], 'max': [max(value[axis] for value in stored) for axis in range(3)]},
            {'bufferView': 1, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
            {'bufferView': 2, 'componentType': 5126, 'count': count, 'type': 'VEC3'},
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

PREVIEW_HEXES = [
    (0, 0),                                                             # alone
    (3, 0), (4, 0),                                                     # a pair
    (7, -1), (8, -1), (9, -1), (10, -1),                                # a line
    (0, 3), (1, 3), (0, 4),                                             # a triangle
    (5, 3), (6, 3), (4, 4), (5, 4), (6, 4), (4, 5), (5, 5),             # a full ring
    (9, 2), (9, 3), (10, 3), (11, 3), (10, 4), (9, 5),                  # a branching range
]


def get_map_mask(hexes, hex_coord):
    return sum(1 << direction for direction, offset in enumerate(DIRECTION_OFFSETS) if (hex_coord[0] + offset[0], hex_coord[1] + offset[1]) in hexes)


def check_seams(meshes, hexes):
    """Compares every pair of neighbors on a map, placed the way the game places them, and returns the largest differences."""
    largest = [0.0, 0.0, 0.0]
    pairs = 0

    for hex_coord in hexes:
        for direction, offset in enumerate(DIRECTION_OFFSETS):
            other = (hex_coord[0] + offset[0], hex_coord[1] + offset[1])
            if other not in hexes:
                continue

            pairs += 1
            placed = []
            for coord in (hex_coord, other):
                mesh, turns = get_canonical(get_map_mask(hexes, coord))
                placed.append((meshes[mesh], turns, hex_to_world(*coord)))

            side = {}
            mountain, turns, world = placed[1]
            for index, point in enumerate(mountain.points):
                x, y, z = transform(mountain.get_position(index), turns, world)
                side[(round(x, 2), round(y, 2))] = (z, transform(mountain.normals[index], turns), mountain.colors[index])

            mountain, turns, world = placed[0]
            matched = 0
            for index, point in enumerate(mountain.points):
                x, y, z = transform(mountain.get_position(index), turns, world)
                shared = side.get((round(x, 2), round(y, 2)))
                if not shared:
                    continue
                matched += 1
                normal = transform(mountain.normals[index], turns)
                largest[0] = max(largest[0], abs(z - shared[0]))
                largest[1] = max(largest[1], max(abs(normal[axis] - shared[1][axis]) for axis in range(3)))
                largest[2] = max(largest[2], max(abs(mountain.colors[index][axis] - shared[2][axis]) for axis in range(3)))

            if matched < SUBDIVISIONS + 1:
                raise RuntimeError('%s and %s share %d vertices' % (hex_coord, other, matched))

    return pairs, largest


def write_png(path, width, height, pixels):
    rows = b''.join(b'\0' + pixels[row * width * 3:(row + 1) * width * 3] for row in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    with open(path, 'wb') as file:
        file.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def render_preview(meshes, hexes, path, width=1500, height=900):
    """Draws the map from an oblique camera with a small z-buffer, with north up on the image and +Y to the right."""
    ground = (0.20, 0.27, 0.12)
    pixels = bytearray(bytes((int(ground[0] * 255 + .5), int(ground[1] * 255 + .5), int(ground[2] * 255 + .5))) * (width * height))
    depth = [1e18] * (width * height)

    worlds = [hex_to_world(*coord) for coord in hexes]
    center_x = sum(world[0] for world in worlds) / len(worlds)
    center_y = sum(world[1] for world in worlds) / len(worlds)
    span = max(max(world[1] for world in worlds) - min(world[1] for world in worlds), 1.0) + 2.0 * HEX_RADIUS
    scale = width * 0.95 / span
    tilt = math.radians(50.0)
    sin_tilt, cos_tilt = math.sin(tilt), math.cos(tilt)
    light = (0.45, -0.6, 0.66)

    def project(x, y, z):
        screen_x = (y - center_y) * scale + width * 0.5
        screen_y = height * 0.5 - ((x - center_x) * sin_tilt + z * cos_tilt) * scale
        return screen_x, screen_y, -(x - center_x) * cos_tilt + z * sin_tilt * -1.0

    for coord in hexes:
        mesh, turns = get_canonical(get_map_mask(hexes, coord))
        mountain = meshes[mesh]
        world = hex_to_world(*coord)
        projected = []
        shades = []
        for index in range(len(mountain.points)):
            projected.append(project(*transform(mountain.get_position(index), turns, world)))
            normal = transform(mountain.normals[index], turns)
            lit = 0.28 + 0.95 * max(0.0, normal[0] * light[0] + normal[1] * light[1] + normal[2] * light[2])
            shades.append([min(1.0, value * lit) ** (1 / 2.2) for value in mountain.colors[index]])

        for triangle in mountain.get_triangles():
            heights = [mountain.heights[index] for index in triangle]
            ax, ay, az = projected[triangle[0]]
            bx, by, bz = projected[triangle[1]]
            cx, cy, cz = projected[triangle[2]]
            area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
            if abs(area) < 1e-6:
                continue
            for y in range(max(0, int(min(ay, by, cy))), min(height, int(max(ay, by, cy)) + 2)):
                for x in range(max(0, int(min(ax, bx, cx))), min(width, int(max(ax, bx, cx)) + 2)):
                    w1 = ((x - ax) * (cy - ay) - (y - ay) * (cx - ax)) / area
                    w2 = ((bx - ax) * (y - ay) - (by - ay) * (x - ax)) / area
                    w0 = 1.0 - w1 - w2
                    if w0 < -0.001 or w1 < -0.001 or w2 < -0.001:
                        continue
                    z = w0 * az + w1 * bz + w2 * cz
                    at = y * width + x
                    if z >= depth[at]:
                        continue
                    depth[at] = z
                    for axis in range(3):
                        value = w0 * shades[triangle[0]][axis] + w1 * shades[triangle[1]][axis] + w2 * shades[triangle[2]][axis]
                        pixels[at * 3 + axis] = int(value * 255.0)

    write_png(path, width, height, bytes(pixels))


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    masks = sorted({get_canonical(mask)[0] for mask in range(64)})
    print('%d meshes' % len(masks))

    meshes = {}
    for mask in masks:
        meshes[mask] = Mountain(mask)
        write_glb(meshes[mask], os.path.join(OUTPUT_FOLDER, get_mesh_name(mask) + '.glb'))
        print(get_mesh_name(mask), 'height %.0f to %.0f' % (min(meshes[mask].heights), max(meshes[mask].heights)))

    hexes = set(PREVIEW_HEXES)
    pairs, largest = check_seams(meshes, hexes)
    print('checked %d sides on the preview map: height %.5f, normal %.5f, color %.5f' % (pairs, largest[0], largest[1], largest[2]))

    # every combination of mountains around one hex, including the sides between the neighbors themselves
    largest_all = [0.0, 0.0, 0.0]
    for ring in range(64):
        around = {(0, 0)} | {DIRECTION_OFFSETS[direction] for direction in range(6) if ring >> direction & 1}
        _, largest = check_seams(meshes, around)
        largest_all = [max(largest_all[axis], largest[axis]) for axis in range(3)]
    print('checked all 64 neighbor sets: height %.5f, normal %.5f, color %.5f' % tuple(largest_all))

    if max(largest_all) > 0.01:
        sys.exit('The meshes do not join')

    render_preview(meshes, PREVIEW_HEXES, os.path.join(OUTPUT_FOLDER, 'Preview.png'))
    print('wrote', OUTPUT_FOLDER)


if __name__ == '__main__':
    main()
