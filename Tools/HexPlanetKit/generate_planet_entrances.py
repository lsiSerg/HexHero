"""Writes the meshes of the entrances and exits of the underground to Saved/HexPlanetMarkers as GLB files, which import_planet_markers.py imports.

As the developer asked on 2026-11-12, an entrance is a rock with a cave mouth and stalactites in it, in place of the pit of before, with a
few versions of it and of the exit below, modelled and darker in the corners of their geometry so their volume shows. Each is a mound of rock
that stands in the back of its cell with its mouth towards the middle, where the hero stands (EntranceHeroPlace of the config of the layer):
the mouth faces +X of the mesh, which AHexPlanet::UpdateMarkers turns towards the open side of the cell, and the origin is the middle of the
cell on the ground. A unit is MarkerSize of the config, 70 by default, so a cell reaches about 1.24 from the origin to its sides.

The colors of the vertices are what M_HexPlanetMarker reads (create_planet_assets.py): red how much of the daylight of an exit falls there,
green how much it is worked stone rather than the rock of the cell, blue how much darker it is, the shade baked into corners, feet and the
inside of the mouth, and alpha how much it glows in the color of its network. The rock itself is the texture of the rock layer of the cell.

Run with any Python 3: python generate_planet_entrances.py
"""

import math
import os

import generate_planet_kit as kit

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetMarkers'))

# columns around a mound
COLUMNS = 32
# the rings of a mound from its foot to under its peak, as shares of its radius and its height
PROFILE = [(1.0, -0.06), (1.0, 0.09), (0.97, 0.24), (0.93, 0.39), (0.85, 0.54), (0.73, 0.68), (0.57, 0.81), (0.38, 0.92), (0.17, 0.99)]
# how dark the foot of anything that stands on the ground is, and up to which height above it
FOOT_SHADE = 0.75
FOOT_HEIGHT = 0.22
# how dark a dent of the rock is, by how far it lies behind the rock around it, and the rock around the mouth
DENT_SHADE = 9.0
LIP_SHADE = 0.5
# how deep the mouth of a rock is, from its edge to its dark end
DEPTH = 0.14


def mix(a, b, share):
    return a + (b - a) * share


class Mound(kit.MarkerMesh):
    """A mound of rock with a cave mouth towards +X, or several around it, as the developer asked for versions that open to two or three
    sides: mouths are the directions they open to, as angles from +X. Triangles are (corners, normal, glows, reds, greens, blues)"""

    def __init__(self, seed, radius=0.66, height=1.7, center=-0.58, lean=0.12, mouth_half=0.62, arch=0.95, sill=0.0, exit=False, mouths=(0.0,), profile=None, stretch=1.0, flat_top=False, tail=1.0, tail_rise=0.0):
        super().__init__()
        self.seed = seed
        self.radius = radius
        self.height = height
        self.center = center
        self.lean = lean
        self.mouth_half = mouth_half
        self.arch = arch
        self.sill = sill
        self.exit = exit
        self.mouths = list(mouths)
        # the rings of the rock, how much wider it is across its mouth than deep, and whether its top is flat, as that of a wall, or a peak
        self.profile = profile or PROFILE
        self.stretch = stretch
        self.flat_top = flat_top
        # a spur: how many times longer the rock is behind its middle than before it, and how much higher it gets towards its back, so it
        # runs into the mountain behind it and rises with it
        self.tail = tail
        self.tail_rise = tail_rise
        # the shade a corner takes when none is given: the foot of what stands on the ground
        self.shade = lambda point: FOOT_SHADE * (1.0 - kit.smoothstep(0.0, FOOT_HEIGHT, point[2]))
        self.phases = [kit.hash_unit(index, 91, seed) * 2.0 * math.pi for index in range(4)]
        self.build_rock()

    def add(self, a, b, c, outward, glow=0.0, red=0.0, green=0.0, blue=None):
        result = kit.oriented(a, b, c, outward)
        if not result:
            return
        corners, normal = result
        if blue is None:
            blue = (self.shade(a), self.shade(b), self.shade(c))
        channels = [value if isinstance(value, (list, tuple)) else (value, value, value) for value in (glow, red, green, blue)]
        # the winding swapped the last two corners if it had to
        if corners[1] is not b:
            channels = [(value[0], value[2], value[1]) for value in channels]
        self.triangles.append((corners, normal) + tuple(list(value) for value in channels))

    def add_quad(self, a, b, c, d, outward, glow=0.0, red=0.0, green=0.0, blue=None):
        split = lambda value, order: [value[index] for index in order] if isinstance(value, (list, tuple)) else value
        self.add(a, b, c, outward, split(glow, (0, 1, 2)), split(red, (0, 1, 2)), split(green, (0, 1, 2)), split(blue, (0, 1, 2)) if blue is not None else None)
        self.add(a, c, d, outward, split(glow, (0, 2, 3)), split(red, (0, 2, 3)), split(green, (0, 2, 3)), split(blue, (0, 2, 3)) if blue is not None else None)

    # the rock

    def rock_point(self, ring, column):
        share, level = self.profile[ring]
        angle = column / COLUMNS * 2.0 * math.pi
        # an uneven foot, a ridge that is higher to one side, and broken faces
        lobes = 1.0 + 0.1 * math.sin(2.0 * angle + self.phases[0]) + 0.07 * math.sin(3.0 * angle + self.phases[1])
        ridge = 1.0 + 0.14 * math.sin(angle + self.phases[2]) * kit.smoothstep(0.3, 0.9, level)
        # buttresses and clefts that run up the rock, turning a little as they rise, and broken faces
        ribs = 0.075 * math.sin(4.0 * angle + self.phases[3] + ring * 0.35) + 0.055 * math.sin(7.0 * angle + self.phases[0] * 2.0 - ring * 0.5)             + 0.035 * math.sin(11.0 * angle + self.phases[1] * 3.0 + ring * 0.8)
        r = self.radius * share * lobes * (1.0 + ribs * (0.4 + 0.6 * kit.smoothstep(0.0, 0.3, level)) + kit.ring_noise(column, ring, self.seed, 0.03 if ring else 0.01))
        z = self.height * level * ridge + (kit.ring_noise(column, ring + 20, self.seed, 0.035 * self.height) if ring else 0.0)
        along = r * math.cos(angle)
        if along < 0.0:
            z *= 1.0 + self.tail_rise * min(-along / max(self.radius, 1e-6), 1.0) * (1.0 if level > 0.0 else 0.0)
            along *= self.tail
        return (self.center - self.lean * max(level, 0.0) + along, r * math.sin(angle) * self.stretch, z)

    @staticmethod
    def angle_between(angle, other):
        return (angle - other + math.pi) % (2.0 * math.pi) - math.pi

    def find_mouth(self, ring, column):
        """Returns which mouth a face of the rock lies in, or None"""
        if ring < 0 or ring >= len(self.profile) - 1:
            return None
        angle = ((column % COLUMNS) + 0.5) / COLUMNS * 2.0 * math.pi
        level = 0.5 * (self.profile[ring][1] + self.profile[ring + 1][1]) * self.height
        if level <= self.sill or level >= self.arch:
            return None
        half = self.mouth_half * math.sqrt(1.0 - ((level - self.sill) / (self.arch - self.sill)) ** 2)
        for index, mouth in enumerate(self.mouths):
            if abs(self.angle_between(angle, mouth)) < half:
                return index
        return None

    def in_mouth(self, ring, column):
        return self.find_mouth(ring, column) is not None

    def near_mouth(self, angle, margin):
        return any(abs(self.angle_between(angle, mouth)) < self.mouth_half + margin for mouth in self.mouths)

    def turn(self, point, angle):
        """A point turned around the axis of the rock"""
        x, y = point[0] - self.center, point[1]
        return (self.center + x * math.cos(angle) - y * math.sin(angle), x * math.sin(angle) + y * math.cos(angle), point[2])

    def inner(self, point, mouth=0.0):
        """Where a point of the edge of a mouth lies at the end of the tunnel behind it"""
        front = self.turn(point, -mouth)
        # behind the point, also high on a face that leans back, where the edge of the mouth lies behind the middle of the rock: a tunnel
        # that ran forwards from there turned its walls inside out, which showed as holes
        # a shallow recess that follows the face, closed by its dark end: the ground of a mountain rises inside a rock built into it, and
        # showed through a deeper mouth in place of the dark (DEPTH)
        return self.turn((front[0] - DEPTH, front[1] * 0.8, self.sill + (point[2] - self.sill) * 0.86), mouth)

    def each_mouth(self, build):
        """Builds what stands in the mouth towards +X and turns a copy of it into every mouth"""
        for mouth in self.mouths:
            start = len(self.triangles)
            build()
            for index in range(start, len(self.triangles)):
                triangle = self.triangles[index]
                normal = self.turn((triangle[1][0] + self.center, triangle[1][1], triangle[1][2]), mouth)
                self.triangles[index] = ([self.turn(corner, mouth) for corner in triangle[0]], [normal[0] - self.center, normal[1], normal[2]]) + tuple(triangle[2:])

    def build_rock(self):
        rings = len(self.profile)
        points = [[self.rock_point(ring, column) for column in range(COLUMNS)] for ring in range(rings)]
        self.points = points

        def radial(point, ring):
            return math.hypot(point[0] - (self.center - self.lean * max(self.profile[ring][1], 0.0)), point[1])

        # the shade of each point: its foot, and how far it lies behind the points around it, a dent
        shades = []
        for ring in range(rings):
            row = []
            for column in range(COLUMNS):
                point = points[ring][column]
                around = [points[ring][(column - 1) % COLUMNS], points[ring][(column + 1) % COLUMNS]]
                dent = sum(radial(each, ring) for each in around) / 2.0 - radial(point, ring)
                if 0 < ring < rings - 1:
                    dent += 0.5 * ((radial(points[ring - 1][column], ring - 1) + radial(points[ring + 1][column], ring + 1)) / 2.0 - radial(point, ring))
                row.append(min(max(self.shade(point), min(max(dent * DENT_SHADE, 0.0), 0.7)), 1.0))
            shades.append(row)
        # a flat top is in the shade, as the tops of the walls are
        if self.flat_top:
            shades[-1] = [max(value, 0.55) for value in shades[-1]]
            shades[-2] = [max(value, 0.3) for value in shades[-2]]
        # and around the mouth
        for ring in range(rings - 1):
            for column in range(COLUMNS):
                if self.in_mouth(ring, column):
                    for r, c in ((ring - 1, column), (ring + 2, column), (ring, column - 1), (ring + 1, column - 1), (ring, column + 2), (ring + 1, column + 2),
                                 (ring, column), (ring + 1, column), (ring, column + 1), (ring + 1, column + 1)):
                        if 0 <= r < rings:
                            shades[r][c % COLUMNS] = max(shades[r][c % COLUMNS], LIP_SHADE)

        # the glow of the network fills the depth of the mouth of an entrance, so it shows from afar where the way down is
        glow_deep = 0.0 if self.exit else 0.35
        day_lip, day_deep = (0.25, 0.95) if self.exit else (0.0, 0.0)
        dark_lip, dark_deep = (0.45, 0.1) if self.exit else (0.6, 1.0)
        inner_points = [[] for mouth in self.mouths]
        for ring in range(rings - 1):
            for column in range(COLUMNS):
                next_column = (column + 1) % COLUMNS
                a, b = points[ring][column], points[ring][next_column]
                c, d = points[ring + 1][column], points[ring + 1][next_column]
                sa, sb, sc, sd = shades[ring][column], shades[ring][next_column], shades[ring + 1][column], shades[ring + 1][next_column]
                found = self.find_mouth(ring, column)
                if found is None:
                    middle = [(a[axis] + d[axis]) * 0.5 for axis in range(3)]
                    outward = (middle[0] - self.center, middle[1], 0.35)
                    self.add(a, c, d, outward, blue=(sa, sc, sd))
                    self.add(a, d, b, outward, blue=(sa, sd, sb))
                    continue
                # the walls of the tunnel behind each edge of the mouth, darker inwards, or lit by the day at an exit
                for (p, q), (r, c2) in (((a, b), (ring - 1, column)), ((c, d), (ring + 1, column)), ((a, c), (ring, column - 1)), ((b, d), (ring, column + 1))):
                    if self.in_mouth(r, c2):
                        continue
                    mouth = self.mouths[found]
                    pi, qi = self.inner(p, mouth), self.inner(q, mouth)
                    if r < 0 and self.sill <= 0.0:
                        # a mouth that opens at the ground has no wall under it, but its end is closed down to the ground: without this
                        # edge the fan of the end lacked its lowest triangle, through which the mountain behind showed
                        inner_points[found].append((pi, qi))
                        continue
                    middle = [(p[axis] + qi[axis]) * 0.5 for axis in range(3)]
                    # towards the middle line of the tunnel
                    front = self.turn(middle, -mouth)
                    axis_point = self.turn((front[0], 0.0, self.sill + (self.arch - self.sill) * 0.4), mouth)
                    outward = [axis_point[axis] - middle[axis] for axis in range(3)]
                    self.add_quad(p, q, qi, pi, outward, glow=[0.0, 0.0, glow_deep, glow_deep], red=[day_lip, day_lip, day_deep, day_deep],
                                  blue=[dark_lip, dark_lip, dark_deep, dark_deep])
                    inner_points[found].append((pi, qi))
        # the peak
        # over the middle of the last ring and above all of it, or the faces around it would fold where the rock is longer or higher behind
        top = points[-1]
        peak = (sum(point[0] for point in top) / len(top), sum(point[1] for point in top) / len(top),
                sum(point[2] for point in top) / len(top) if self.flat_top else max(point[2] for point in top) + 0.05 * self.height)
        for column in range(COLUMNS):
            a, b = points[-1][column], points[-1][(column + 1) % COLUMNS]
            self.add(a, peak, b, (a[0] - self.center, a[1], 1.0), blue=(shades[-1][column], 0.65 if self.flat_top else 0.0, shades[-1][(column + 1) % COLUMNS]))
        # the end of the tunnel: the dark with the glow of the network deep in it, or the daylight of an exit
        for found, pairs in enumerate(inner_points):
            if not pairs:
                continue
            count = 2 * len(pairs)
            middle = [sum(point[axis] for pair in pairs for point in pair) / count for axis in range(3)]
            facing = (math.cos(self.mouths[found]), math.sin(self.mouths[found]), 0.0)
            for pi, qi in pairs:
                if self.exit:
                    self.add(pi, qi, middle, facing, red=(0.9, 0.9, 1.0), blue=(0.1, 0.1, 0.0))
                else:
                    self.add(pi, qi, middle, facing, glow=(0.6, 0.6, 1.0), blue=1.0)
        # the floor of each, over the ground that would show in it
        lip = self.center + self.radius * 1.02
        width = self.radius * math.sin(self.mouth_half) * 1.05 * self.stretch
        deep = lip - DEPTH - 0.06
        level = self.sill + 0.02
        self.each_mouth(lambda: self.add_quad((lip, -width, level), (lip, width, level), (deep, width * 0.5, level), (deep, -width * 0.5, level), (0.0, 0.0, 1.0),
                                              red=[day_lip, day_lip, day_deep, day_deep], blue=[dark_lip * 0.8, dark_lip * 0.8, dark_deep, dark_deep]))

    def surface_point(self, angle, level):
        """A point on the rock in a direction around it at a height, without its broken faces"""
        share_at = self.profile[-1][0]
        for (s0, l0), (s1, l1) in zip(PROFILE, self.profile[1:]):
            if level <= l1 * self.height:
                share_at = mix(s0, s1, (level - l0 * self.height) / max((l1 - l0) * self.height, 1e-6))
                break
        r = self.radius * share_at
        return (self.center - self.lean * level / self.height + r * math.cos(angle), r * math.sin(angle) * self.stretch, level)

    # what stands on it and around it

    def add_cone(self, base, tip, radius, sides=5, glow=0.0, red=0.0, green=0.0, base_shade=0.7, tip_shade=0.05, bend=0.0):
        """A spike from a base to a tip, in two segments so it is not a plain cone, darker where it grows out of the rock"""
        axis = [tip[index] - base[index] for index in range(3)]
        length = math.sqrt(sum(value * value for value in axis)) or 1.0
        axis = [value / length for value in axis]
        side = (1.0, 0.0, 0.0) if abs(axis[0]) < 0.8 else (0.0, 1.0, 0.0)
        u = [axis[1] * side[2] - axis[2] * side[1], axis[2] * side[0] - axis[0] * side[2], axis[0] * side[1] - axis[1] * side[0]]
        norm = math.sqrt(sum(value * value for value in u))
        u = [value / norm for value in u]
        v = [axis[1] * u[2] - axis[2] * u[1], axis[2] * u[0] - axis[0] * u[2], axis[0] * u[1] - axis[1] * u[0]]
        self.cone_index = getattr(self, 'cone_index', 0) + 1
        turn = kit.hash_unit(self.cone_index, 71, self.seed) * 6.28

        def ring(along, size, offset):
            center = [base[index] + axis[index] * length * along + u[index] * offset for index in range(3)]
            return [tuple(center[index] + (u[index] * math.cos(turn + k * 2.0 * math.pi / sides) + v[index] * math.sin(turn + k * 2.0 * math.pi / sides))
                          * size * (1.0 + kit.ring_noise(k, self.cone_index, self.seed, 0.2)) for index in range(3)) for k in range(sides)]

        low, middle = ring(-0.08, radius, 0.0), ring(0.45, radius * 0.55, bend * length)
        mid_shade = mix(base_shade, tip_shade, 0.7)
        for k in range(sides):
            n = (k + 1) % sides
            out = [(low[k][index] + low[n][index]) * 0.5 - base[index] for index in range(3)]
            self.add_quad(low[k], low[n], middle[n], middle[k], out, glow, red, green, blue=[base_shade, base_shade, mid_shade, mid_shade])
            out = [(middle[k][index] + middle[n][index]) * 0.5 - (base[index] + axis[index] * length * 0.45) for index in range(3)]
            self.add(middle[k], middle[n], tip, out, glow, red, green, blue=(mid_shade, mid_shade, tip_shade))

    def add_teeth(self, count, length, bottom=0):
        """Stalactites that hang in every mouth from its arch, and stalagmites that stand in it"""
        self.each_mouth(lambda: self.add_front_teeth(count, length, bottom))

    def add_front_teeth(self, count, length, bottom):
        for tooth in range(count):
            share = (tooth + 0.5 + (kit.hash_unit(tooth, 61, self.seed) - 0.5) * 0.5) / count
            angle = (share * 2.0 - 1.0) * self.mouth_half * 0.9
            level = self.sill + (self.arch - self.sill) * math.sqrt(max(1.0 - (angle / self.mouth_half) ** 2, 0.02))
            top = self.surface_point(angle, level)
            top = (top[0] - 0.05, top[1], top[2] + 0.03)
            size = length * (0.55 + 0.9 * kit.hash_unit(tooth, 62, self.seed)) * (0.6 + 0.4 * math.cos(angle / self.mouth_half * 1.4))
            self.add_cone(top, (top[0] + 0.02, top[1] * 0.97, top[2] - size), 0.035 + size * 0.13, base_shade=0.75, tip_shade=0.2, red=0.15 if self.exit else 0.0)
        for tooth in range(bottom):
            share = (tooth + 0.5 + (kit.hash_unit(tooth, 63, self.seed) - 0.5) * 0.6) / bottom
            angle = (share * 2.0 - 1.0) * self.mouth_half * 0.85
            if abs(angle) < self.mouth_half * 0.3:
                continue
            foot = self.surface_point(angle, self.sill + 0.02)
            size = length * (0.5 + 0.7 * kit.hash_unit(tooth, 64, self.seed))
            self.add_cone((foot[0] + 0.03, foot[1], self.sill), (foot[0] + 0.05, foot[1], self.sill + size), 0.04 + size * 0.14, base_shade=0.8, tip_shade=0.15)

    def add_spires(self, count, tall):
        """Stalagmites that stand around the rock and on its shoulders"""
        for spire in range(count):
            angle = math.pi * (0.35 + 1.3 * (spire + kit.hash_unit(spire, 51, self.seed) * 0.7) / count)
            if self.near_mouth(angle, 0.12):
                continue
            level = self.height * (0.05 + 0.5 * kit.hash_unit(spire, 52, self.seed)) if spire % 2 else 0.0
            foot = self.surface_point(angle, level)
            out = 0.05 if spire % 2 else 0.12
            foot = (foot[0] + out * math.cos(angle), foot[1] + out * math.sin(angle), level - 0.04)
            size = tall * (0.5 + 0.8 * kit.hash_unit(spire, 53, self.seed))
            lean = 0.12 * size
            self.add_cone(foot, (foot[0] + lean * math.cos(angle), foot[1] + lean * math.sin(angle), foot[2] + size), 0.07 + size * 0.14, sides=6,
                          base_shade=0.8, tip_shade=0.0, bend=0.03)

    def add_drips(self, count, length, level=0.9, reach=1.25):
        """Stalactites that hang from the edge that overhangs at the top of a rock in the style of a wall"""
        for drip in range(count):
            angle = (2.0 * (drip + 0.5 + (kit.hash_unit(drip, 55, self.seed) - 0.5) * 0.7) / count - 1.0) * reach
            top = self.surface_point(angle, self.height * (level + 0.04 * (kit.hash_unit(drip, 56, self.seed) - 0.5)))
            size = length * (0.45 + 1.0 * kit.hash_unit(drip, 57, self.seed))
            self.add_cone((top[0] - 0.03 * math.cos(angle), top[1], top[2] + 0.04), (top[0], top[1], top[2] - size), 0.04 + size * 0.12, base_shade=0.7, tip_shade=0.15)

    def add_stones(self, count, seed):
        """Stones fallen at the foot of the rock, on both sides of its mouth, each darker where it lies on the ground"""
        for stone in range(count):
            side = 1.0 if stone % 2 else -1.0
            angle = side * (self.mouth_half + 0.25 + 1.6 * kit.hash_unit(stone, 41, seed))
            if self.near_mouth(angle, 0.1):
                continue
            foot = self.surface_point(angle, 0.0)
            reach = 0.06 + 0.2 * kit.hash_unit(stone, 42, seed)
            x, y = foot[0] + reach * math.cos(angle), foot[1] + reach * math.sin(angle)
            size = 0.06 + 0.09 * kit.hash_unit(stone, 43, seed)
            self.add_rock(seed * 19 + stone * 3 + 5, size, 0.8, math.atan2(y, x), math.hypot(x, y), -0.02)

    def add_path(self, count, reach=0.85):
        """A path of flat stones from the ground before the mouth into it"""
        lip = self.center + self.radius * 0.8
        for stone in range(count):
            x = lip + reach * (stone + 0.5) / count
            y = (kit.hash_unit(stone, 45, self.seed) - 0.5) * 0.18
            size = 0.15 + 0.05 * kit.hash_unit(stone, 46, self.seed)
            self.add_rock(self.seed * 23 + stone * 7 + 1, size, 0.24, math.atan2(y, x), math.hypot(x, y), 0.015, stretch=1.2 + 0.4 * kit.hash_unit(stone, 47, self.seed))

    def add_crystals(self, clusters, glow=1.0):
        """Crystals beside the mouth that glow in the color of the network"""
        for cluster in range(clusters):
            side = 1.0 if cluster % 2 else -1.0
            angle = self.mouths[0] + side * (self.mouth_half + 0.12 + 0.3 * kit.hash_unit(cluster, 21, self.seed))
            if any(abs(self.angle_between(angle, mouth)) < self.mouth_half + 0.04 for mouth in self.mouths):
                continue
            foot = self.surface_point(angle, self.sill + 0.05 + 0.3 * (cluster // 2))
            for crystal in range(3):
                x = foot[0] + 0.03 + 0.05 * kit.hash_unit(cluster * 3 + crystal, 22, self.seed)
                y = foot[1] + (kit.hash_unit(cluster * 3 + crystal, 23, self.seed) - 0.5) * 0.12
                self.add_crystal(math.atan2(y, x), math.hypot(x, y), foot[2] - 0.03, 0.022 + 0.014 * kit.hash_unit(cluster * 3 + crystal, 24, self.seed),
                                 0.1 + 0.12 * kit.hash_unit(cluster * 3 + crystal, 25, self.seed), 0.2 + 0.4 * kit.hash_unit(cluster * 3 + crystal, 26, self.seed),
                                 kit.hash_unit(cluster * 3 + crystal, 27, self.seed), glow)

    def add_box(self, low, high, green=0.8, red=0.0, glow=0.0, shade_low=0.65, shade_high=0.1):
        """A block of worked stone between two corners, darker at its foot"""
        (x0, y0, z0), (x1, y1, z1) = low, high
        middle = ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5)
        corners = [(x, y, z) for z in (z0, z1) for y in (y0, y1) for x in (x0, x1)]
        faces = [((0, 1, 3, 2), (0, 0, -1)), ((4, 5, 7, 6), (0, 0, 1)), ((0, 1, 5, 4), (0, -1, 0)), ((2, 3, 7, 6), (0, 1, 0)), ((0, 2, 6, 4), (-1, 0, 0)), ((1, 3, 7, 5), (1, 0, 0))]
        for indices, outward in faces[1:]:
            quad = [corners[index] for index in indices]
            self.add_quad(*quad, outward, glow, red, green, blue=[shade_low if point[2] < middle[2] else shade_high for point in quad])

    def add_portal(self, glyph=True):
        """A doorway of worked stone before every mouth: two pillars, a lintel across them and a glyph on it that glows"""
        self.each_mouth(lambda: self.add_front_portal(glyph))

    def add_front_portal(self, glyph):
        # two standing stones and one laid across them, rough boulders of the rock of the cell: straight beams of worked stone, built first,
        # the developer asked away on 2026-11-12 ("прибрати балки та замінити чимось природнішим або з валунів відповідної форми")
        lip = self.center + self.radius * 1.0
        half = self.radius * math.sin(self.mouth_half) * 0.92 * self.stretch
        top = self.arch * 0.92
        red = 0.25 if self.exit else 0.0
        for side in (-1.0, 1.0):
            x, y = lip - 0.02, side * (half + 0.1)
            size = 0.22 + 0.04 * kit.hash_unit(int(side) + 2, 81, self.seed)
            self.add_rock(self.seed * 31 + int(side) + 2, size, (top - self.sill + 0.12) / (0.85 * size), math.atan2(y, x), math.hypot(x, y), self.sill - 0.08, red=red)
        size = 0.32
        self.add_rock(self.seed * 37 + 5, size, 0.9, math.atan2(0.0, lip), abs(lip), top - 0.04, red=red, stretch=(2.0 * half + 0.4) / size)
        if glyph:
            # a crystal on the stone laid across, which glows in the color of the network
            x = lip + 0.04
            self.add_crystal(math.atan2(0.0, x), abs(x), top + 0.14, 0.035, 0.13, 0.0, kit.hash_unit(7, 82, self.seed), 1.0)

    def add_steps(self, count, rise, depth=0.13):
        """Steps of worked stone from the ground before every mouth up to its sill, lit by the day of an exit"""
        self.each_mouth(lambda: self.add_front_steps(count, rise, depth))

    def add_front_steps(self, count, rise, depth):
        # flat rough stones, each lower than the one behind it, in place of straight blocks
        lip = self.center + self.radius * 0.98
        half = self.radius * math.sin(self.mouth_half) * 0.9 * self.stretch
        for step in range(count):
            top = self.sill + 0.02 - step * rise
            x = lip + (step + 0.4) * depth
            for stone in range(2):
                index = step * 2 + stone
                y = (stone - 0.5) * half * 1.05 + (kit.hash_unit(index, 34, self.seed) - 0.5) * 0.08
                size = 0.24 + 0.06 * kit.hash_unit(index, 35, self.seed)
                self.add_rock(self.seed * 41 + index, size, max(top + 0.07, 0.06) / (0.85 * size), math.atan2(y, x), math.hypot(x, y), -0.05,
                              red=(0.6 - 0.08 * step) if self.exit else 0.0, stretch=1.15 + 0.3 * kit.hash_unit(index, 36, self.seed))

    def get_triangles(self):
        return self.triangles


def crag(seed):
    """A high crag with a pointed mouth, stalactites in it, stalagmites around it and crystals beside it"""
    mound = Mound(seed, radius=0.64, height=1.9, mouth_half=0.6, arch=1.0)
    mound.add_teeth(5, 0.3)
    mound.add_spires(5, 0.7)
    mound.add_stones(6, seed)
    mound.add_crystals(3)
    return mound


def maw(seed):
    """A low, broad rock with a wide mouth full of teeth from above and below"""
    mound = Mound(seed, radius=0.7, height=1.25, center=-0.56, lean=0.2, mouth_half=0.85, arch=0.72)
    mound.add_teeth(8, 0.26, bottom=7)
    mound.add_spires(3, 0.5)
    mound.add_stones(8, seed)
    mound.add_crystals(2)
    return mound


def gate(seed):
    """A rock with a doorway of worked stone before its mouth and a threshold of two steps"""
    mound = Mound(seed, radius=0.66, height=1.6, mouth_half=0.55, arch=0.95, sill=0.1)
    mound.add_portal()
    mound.add_steps(2, 0.06)
    mound.add_teeth(3, 0.2)
    mound.add_spires(4, 0.6)
    mound.add_stones(5, seed)
    return mound


def exit_stairs(seed):
    """The way up out of the underground: stairs up into a mouth in a rock, out of which the day falls"""
    mound = Mound(seed, radius=0.66, height=1.75, mouth_half=0.6, arch=1.1, sill=0.3, exit=True)
    mound.add_steps(4, 0.075)
    mound.add_teeth(4, 0.22)
    mound.add_spires(5, 0.75)
    mound.add_stones(6, seed)
    mound.add_crystals(2, glow=0.8)
    return mound


def exit_portal(seed):
    """A high pillar of rock with a doorway of worked stone and stairs into it"""
    mound = Mound(seed, radius=0.6, height=2.3, center=-0.6, lean=0.05, mouth_half=0.58, arch=1.05, sill=0.22, exit=True)
    mound.add_portal()
    mound.add_steps(3, 0.075)
    mound.add_spires(6, 0.9)
    mound.add_stones(5, seed)
    return mound


def spur(seed, wide=False):
    """An entrance built into a mountain, as the developer asked on 2026-11-12: an arch a path of flat stones runs into from the ground, in the
    face of a spur of rock that runs back into the mountain and rises with it (AHexPlanet::UpdateMarkers stands it towards the mountain from
    the middle of the cell, EmbedShift), so the rock behind the arch is part of the mountain. A dome set against the mountain and a round
    hollow in its slope, tried first, did not please"""
    if wide:
        # the face with the arch leans back and the arch reaches high up it, so the camera of the game, which looks down, sees into it, as the
        # developer asked ("трохи привідкрий арку зверху"), but not so far that it reads as the way down into a cellar
        mound = Mound(seed, radius=0.7, height=1.25, center=-0.6, lean=0.3, mouth_half=0.78, arch=0.95, tail=1.7, tail_rise=0.35)
        mound.add_teeth(6, 0.16, bottom=4)
    else:
        mound = Mound(seed, radius=0.64, height=1.5, center=-0.6, lean=0.34, mouth_half=0.62, arch=1.2, tail=1.8, tail_rise=0.3)
        mound.add_teeth(4, 0.18)
    mound.add_path(5)
    mound.add_stones(6, seed)
    mound.add_crystals(3)
    return mound


class Doorway(Mound):
    """What stands in the niche the wall of the underground has for an exit built into it (HexPlanetWalls, FParams::Niche*), as the developer
    asked on 2026-11-12 for an exit that is one with the wall: steps of rough stone up to the back of the niche, where the way up is, an arch
    full of daylight, with stalagmites beside it. Blocks of worked stone and a doorway of them, built first, looked square to the developer
    ("прибери брили квадратні на вході - має бути більш природнє"). Its origin is the point of the line of the wall the niche is around, the
    middle of a side or a corner of the cell, and +X the way out of the niche"""

    def __init__(self, seed, crystals=False):
        kit.MarkerMesh.__init__(self)
        self.seed = seed
        self.exit = True
        self.mouths = [0.0]
        self.stretch = 1.0
        self.center, self.radius, self.mouth_half = -1.0, 0.66, 0.74
        self.sill, self.arch = 0.2, 1.55
        self.shade = lambda point: FOOT_SHADE * (1.0 - kit.smoothstep(0.0, FOOT_HEIGHT, point[2]))
        # The arch stands before the back of the niche, which the wall has NicheDepth (48) behind its face around the niche, where the origin
        # of this mesh is: 0.69 of a unit of 70. At 1.05 the arch lay 25 behind that back, in the wall, which hid most of it, as the
        # developer found on 2026-11-12 ("знов прохід закритий чимось"); and it is narrower than the flat back of the niche
        back = -0.6
        half = 0.38
        # steps of flat rough stones, each higher than the one before it, from the floor before the niche up into it, lit by the day
        for step in range(4):
            x = 0.05 - step * 0.15
            for stone in range(2):
                y = (stone - 0.5) * 0.36 + (kit.hash_unit(step * 2 + stone, 31, seed) - 0.5) * 0.1
                size = 0.2 + 0.06 * kit.hash_unit(step * 2 + stone, 32, seed)
                self.add_rock(seed * 29 + step * 5 + stone, size, 0.3, math.atan2(y, x), math.hypot(x, y), 0.02 + step * 0.055, red=0.25 + 0.12 * step,
                              stretch=1.1 + 0.3 * kit.hash_unit(step * 2 + stone, 33, seed))
        # the way up at the back of the niche, an arch full of daylight
        top = self.arch
        outline = [(-half, 0.1), (half, 0.1), (half, top * 0.7), (half * 0.6, top * 0.93), (0.0, top), (-half * 0.6, top * 0.93), (-half, top * 0.7)]
        middle = (back + 0.02, 0.0, top * 0.5)
        for index in range(len(outline)):
            (y0, z0), (y1, z1) = outline[index], outline[(index + 1) % len(outline)]
            # it glows, in the color of the network too, so the way up shows as a lit opening in the wall from afar
            self.add(middle, (back + 0.02, y0, z0), (back + 0.02, y1, z1), (1.0, 0.0, 0.0), glow=(0.75, 0.35, 0.35), red=(1.0, 0.8, 0.8), blue=0.0)
        # dark rock around and behind it, wider than the niche, so nothing shows between the arch and the wall
        wide, high = half + 0.3, top + 0.12
        self.add_quad((back - 0.03, -wide, -0.05), (back - 0.03, wide, -0.05), (back - 0.03, wide, high), (back - 0.03, -wide, high), (1.0, 0.0, 0.0), blue=0.92)
        # stalagmites on both sides of it and stalactites over it, of the rock of the wall
        for side in (-1.0, 1.0):
            for spike in range(3):
                index = int(side + 1) * 3 + spike
                x = -0.05 - 0.17 * spike + 0.08 * kit.hash_unit(index, 34, seed)
                y = side * (half + 0.1 + 0.06 * kit.hash_unit(index, 35, seed))
                tall = 0.35 + 0.5 * kit.hash_unit(index, 36, seed)
                self.add_cone((x, y, -0.04), (x - 0.03, y * 0.97, tall), 0.07 + tall * 0.1, sides=6, base_shade=0.8, tip_shade=0.05, red=0.2)
        for drip in range(4):
            y = (drip - 1.5) * 0.2 + (kit.hash_unit(drip, 37, seed) - 0.5) * 0.08
            long = 0.18 + 0.2 * kit.hash_unit(drip, 38, seed)
            self.add_cone((back + 0.15, y, top + 0.12), (back + 0.17, y, top + 0.12 - long), 0.05, base_shade=0.7, tip_shade=0.2, red=0.3)
        if crystals:
            for crystal in range(4):
                side = 1.0 if crystal % 2 else -1.0
                x = -0.05 - 0.2 * (crystal // 2)
                y = side * (half + 0.02)
                self.add_crystal(math.atan2(y, x), math.hypot(x, y), 0.0, 0.03, 0.14 + 0.1 * kit.hash_unit(crystal, 39, seed), side * 0.3, kit.hash_unit(crystal, 40, seed), 0.9)


def twin(seed):
    """A rock that opens to two sides, with a pillar of rock between its mouths"""
    mound = Mound(seed, radius=0.7, height=1.55, center=-0.5, lean=0.1, mouth_half=0.5, arch=0.85, mouths=(0.68, -0.68))
    mound.add_teeth(4, 0.24, bottom=3)
    mound.add_spires(5, 0.65)
    mound.add_stones(6, seed)
    mound.add_crystals(4)
    return mound


def trident(seed):
    """A broad rock that opens to three sides, forwards and to both flanks"""
    mound = Mound(seed, radius=0.7, height=1.75, center=-0.42, lean=0.04, mouth_half=0.46, arch=0.88, mouths=(0.0, 1.4, -1.4))
    mound.add_teeth(4, 0.24)
    mound.add_spires(4, 0.7)
    mound.add_stones(6, seed)
    mound.add_crystals(4)
    return mound


def exit_twin(seed):
    """A way up with two mouths, stairs into each"""
    mound = Mound(seed, radius=0.68, height=1.9, center=-0.5, lean=0.06, mouth_half=0.46, arch=1.05, sill=0.24, exit=True, mouths=(0.7, -0.7))
    mound.add_steps(3, 0.075)
    mound.add_teeth(3, 0.2)
    mound.add_spires(5, 0.8)
    mound.add_stones(5, seed)
    return mound


# the meshes: the entrances of the surface and the exits of the underground, which EntranceMeshes and ExitMeshes of the config name
MESHES = [('SM_PlanetEntranceCrag', crag, 11), ('SM_PlanetEntranceMaw', maw, 23), ('SM_PlanetEntranceGate', gate, 37),
          ('SM_PlanetEntranceTwin', twin, 61), ('SM_PlanetEntranceTrident', trident, 73),
          ('SM_PlanetEntranceSpur', spur, 91), ('SM_PlanetEntranceSpurWide', lambda seed: spur(seed, True), 97),
          ('SM_PlanetExitNiche', lambda seed: Doorway(seed), 101), ('SM_PlanetExitNicheCrystal', lambda seed: Doorway(seed, True), 107),
          ('SM_PlanetExitCave', exit_stairs, 41), ('SM_PlanetExitGate', exit_portal, 53), ('SM_PlanetExitTwin', exit_twin, 83)]


def write_mesh(triangles, path, name):
    positions, normals, colors = [], [], []
    for triangle in triangles:
        corners, normal, glows, reds, greens = triangle[:5]
        # what the helpers of the kit add has no shade of its own: the foot of each stone
        blues = triangle[5] if len(triangle) > 5 else [0.0, 0.0, 0.0]
        for corner, glow, red, green, blue in zip(corners, glows, reds, greens, blues):
            positions.append(kit.to_file_axes(*(value * 0.01 for value in corner)))
            normals.append(kit.to_file_axes(*normal))
            colors.append((red, green, blue, glow))
    kit.write_glb_arrays(path, name, positions, normals, list(range(len(positions))), colors)
    return len(triangles)


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    for name, make, seed in MESHES:
        count = write_mesh(make(seed).get_triangles(), os.path.join(OUTPUT_FOLDER, name + '.glb'), name)
        print('%s: %d triangles' % (name, count))
    # the shaft of daylight over an exit, as before
    kit.write_shaft(kit.Shaft(), os.path.join(OUTPUT_FOLDER, 'SM_PlanetExitShaft.glb'), 'SM_PlanetExitShaft')
    print('wrote %d meshes to %s' % (len(MESHES) + 1, OUTPUT_FOLDER))


if __name__ == '__main__':
    main()
