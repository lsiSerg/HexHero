"""Draws the atlas of the plants of the hex planet: sprays of leaves and needles, tufts of grass, ears of wheat and flowers, each with an alpha.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_foliage_atlas.py
Writes T_HexFoliage.png to Saved/HexPlanetObjects, which import_planet_objects.py imports to /Game/HexPlanet/Objects. The meshes of
generate_planet_objects.py are cards that show one tile each (foliage_tiles.py gives its place), and M_HexPlanetFoliage cuts them by its alpha.

A tile grows from the middle of its lower edge upwards, where a card joins its twig. A tinted tile is nearly white, so the color of the vertices of
a card is the color of its leaves, a little lighter, darker, warmer or cooler by leaf; a colored tile holds its own colors, as a flower has several,
and its cards are white. Under the alpha every tile keeps the color of its leaves, so the mips do not darken its edges.
"""

import math
import os
import struct
import zlib

from foliage_tiles import ATLAS_NAME, ATLAS_SIZE as SIZE, TILES
from generate_planet_objects import Random

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetObjects'))
# pixels of each tile kept clear along its edges, so the mips of one tile do not bleed into the next
GUTTER = 8


class Tile:
    """A picture with an alpha, drawn in pixels with y growing upwards from its lower edge; colors are sRGB values from 0 to 1"""

    def __init__(self, name, background):
        _, _, self.width, self.height, self.colored = TILES[name]
        self.name = name
        count = self.width * self.height
        self.red = [background[0]] * count
        self.green = [background[1]] * count
        self.blue = [background[2]] * count
        self.alpha = [0.0] * count
        self.low = GUTTER
        self.high_x = self.width - GUTTER
        self.high_y = self.height - GUTTER

    def base(self):
        return self.width * 0.5, float(GUTTER)

    def inside(self, x, y, margin=0.0):
        return self.low + margin <= x <= self.high_x - margin and self.low + margin <= y <= self.high_y - margin

    def box(self, x0, y0, x1, y1):
        """The rows and columns of the pixels of a box, kept inside the gutter"""
        return (max(int(math.floor(x0)), self.low), min(int(math.ceil(x1)), self.high_x),
                max(int(math.floor(y0)), self.low), min(int(math.ceil(y1)), self.high_y))

    def put(self, column, row, color, cover):
        """Lays a color over a pixel by its share of cover"""
        index = (self.height - 1 - row) * self.width + column
        under = self.alpha[index] * (1.0 - cover)
        total = cover + under
        if total <= 0.0:
            return
        self.red[index] = (color[0] * cover + self.red[index] * under) / total
        self.green[index] = (color[1] * cover + self.green[index] * under) / total
        self.blue[index] = (color[2] * cover + self.blue[index] * under) / total
        self.alpha[index] = total

    def line(self, start, end, start_width, end_width, start_color, end_color=None, opacity=1.0):
        """A stem, blade or needle: a line that narrows from one width to another, its color blending along it"""
        end_color = end_color or start_color
        reach = max(start_width, end_width) * 0.5 + 1.0
        x0, x1, y0, y1 = self.box(min(start[0], end[0]) - reach, min(start[1], end[1]) - reach, max(start[0], end[0]) + reach, max(start[1], end[1]) + reach)
        dx, dy = end[0] - start[0], end[1] - start[1]
        squared = dx * dx + dy * dy
        if squared < 1e-9:
            return
        for row in range(y0, y1):
            py = row + 0.5 - start[1]
            for column in range(x0, x1):
                px = column + 0.5 - start[0]
                t = (px * dx + py * dy) / squared
                t = 0.0 if t < 0.0 else 1.0 if t > 1.0 else t
                ox, oy = px - dx * t, py - dy * t
                distance = math.sqrt(ox * ox + oy * oy) - (start_width + (end_width - start_width) * t) * 0.5
                if distance >= 0.5:
                    continue
                cover = (0.5 - distance if distance > -0.5 else 1.0) * opacity
                color = (start_color[0] + (end_color[0] - start_color[0]) * t, start_color[1] + (end_color[1] - start_color[1]) * t,
                         start_color[2] + (end_color[2] - start_color[2]) * t)
                self.put(column, row, color, cover)

    def path(self, points, start_width, end_width, start_color, end_color=None):
        """A line through several points, narrowing along all of it"""
        end_color = end_color or start_color
        count = len(points) - 1
        for index in range(count):
            t0, t1 = index / count, (index + 1) / count
            self.line(points[index], points[index + 1], start_width + (end_width - start_width) * t0, start_width + (end_width - start_width) * t1,
                      mix(start_color, end_color, t0), mix(start_color, end_color, t1))

    def leaf(self, base, angle, length, width, color, shape='ovate', vein=0.12, edge=0.22):
        """A leaf from its stalk along an angle, widest where its shape says, lighter along its midrib and darker at its edges and its stalk"""
        cosine, sine = math.cos(angle), math.sin(angle)
        tip = (base[0] + cosine * length, base[1] + sine * length)
        reach = width * 0.5 + 1.0
        x0, x1, y0, y1 = self.box(min(base[0], tip[0]) - reach, min(base[1], tip[1]) - reach, max(base[0], tip[0]) + reach, max(base[1], tip[1]) + reach)
        profile = LEAF_SHAPES[shape]
        for row in range(y0, y1):
            py = row + 0.5 - base[1]
            for column in range(x0, x1):
                px = column + 0.5 - base[0]
                u = (px * cosine + py * sine) / length
                if u <= 0.0 or u >= 1.0:
                    continue
                v = abs(py * cosine - px * sine)
                half = width * 0.5 * profile(u)
                distance = v - half
                if distance >= 0.5:
                    continue
                cover = 0.5 - distance if distance > -0.5 else 1.0
                across = v / max(half, 0.75)
                shade = (1.0 - edge * across * across) * (0.82 + 0.18 * u) + vein * math.exp(-v * v * 1.2)
                self.put(column, row, (color[0] * shade, color[1] * shade, color[2] * shade), cover)

    def disc(self, center, radius, color, squash=1.0, shade=0.25):
        """A round blob, lighter towards its upper left, flattened by squash: the middle of a flower, a berry, a seed head"""
        x0, x1, y0, y1 = self.box(center[0] - radius - 1.0, center[1] - radius * squash - 1.0, center[0] + radius + 1.0, center[1] + radius * squash + 1.0)
        for row in range(y0, y1):
            py = (row + 0.5 - center[1]) / squash
            for column in range(x0, x1):
                px = column + 0.5 - center[0]
                distance = math.sqrt(px * px + py * py) - radius
                if distance >= 0.5:
                    continue
                cover = 0.5 - distance if distance > -0.5 else 1.0
                light = 1.0 + shade * ((py - px) / max(radius, 1.0)) * 0.5
                self.put(column, row, (color[0] * light, color[1] * light, color[2] * light), cover)

    def coverage(self):
        return sum(1 for value in self.alpha if value > 0.5) / len(self.alpha)


def mix(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t)


def scale(color, factor):
    return (min(color[0] * factor, 1.0), min(color[1] * factor, 1.0), min(color[2] * factor, 1.0))


# half the width of a leaf along it, from its stalk (0) to its tip (1), as a share of its widest
LEAF_SHAPES = {
    'ovate': lambda u: math.sin(math.pi * u ** 0.75) ** 0.8,
    'lance': lambda u: math.sin(math.pi * u ** 0.6) ** 1.25,
    'round': lambda u: math.sqrt(max(1.0 - (2.0 * u - 1.0) ** 2, 0.0)),
    'birch': lambda u: (u / 0.28 if u < 0.28 else ((1.0 - u) / 0.72) ** 0.85) * (0.9 + 0.1 * math.sin(u * 44.0)),
    'oak': lambda u: math.sin(math.pi * u ** 0.85) ** 0.6 * (0.62 + 0.38 * abs(math.sin(u * math.pi * 4.5 + 0.4))),
    'petal': lambda u: math.sin(math.pi * u ** 1.6) ** 0.7,
}

# the nearly white of a tinted tile, which the color of the vertices tints: a leaf is lighter or darker and a little warmer or cooler than the next
LEAF_TONES = [(1.0, 0.99, 0.88), (0.95, 1.0, 0.92), (0.99, 1.0, 0.85), (0.93, 0.98, 0.94), (1.0, 0.95, 0.83)]
STEM_TONE = (0.5, 0.42, 0.34)


def leaf_tone(random, low=0.68, high=1.0):
    return scale(random.pick(LEAF_TONES), random.range(low, high))


def grow(random, base, targets, link):
    """Joins points into twigs: each, nearest the stalk first, hangs from the nearest point before it that lies nearer the stalk, so the twigs
    fork as a branch does and fill the shape the points lie in. Returns (parent, point) for each"""
    def distance(a, b):
        return math.hypot(a[0] - b[0], a[1] - b[1])

    nodes = [base]
    twigs = []
    for target in sorted(targets, key=lambda point: distance(point, base)):
        best, best_distance = 0, 1e9
        for index, node in enumerate(nodes):
            each = distance(node, target)
            # a twig grows away from the stalk, and a longer way round along a twig is better than a straight line from the stalk
            if distance(node, base) < distance(target, base) and each * (1.0 if index else 1.6) < best_distance:
                best, best_distance = index, each * (1.0 if index else 1.6)
        if best_distance > link * 2.2 and best != 0:
            best = 0
        twigs.append((nodes[best], target))
        nodes.append(target)
    return twigs


def scatter(random, tile, count, center, radii, margin):
    """Points spread evenly in an ellipse of a tile, each kept apart from the others"""
    points = []
    apart = math.sqrt(math.pi * radii[0] * radii[1] / max(count, 1)) * 0.72
    tries = 0
    while len(points) < count and tries < count * 60:
        tries += 1
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit())
        point = (center[0] + math.cos(angle) * radii[0] * out, center[1] + math.sin(angle) * radii[1] * out)
        if not tile.inside(point[0], point[1], margin):
            continue
        if all(math.hypot(point[0] - other[0], point[1] - other[1]) > apart for other in points):
            points.append(point)
    return points


def draw_spray(name, seed, count, leaf_length, leaf_width, shape, fill=(0.42, 0.41), middle=0.54, pairs=1, spread=0.9, stem=3.2, low=0.68):
    """A branch end of a broadleaf tree or a bush: twigs that fork from the stalk and carry leaves that point away from it"""
    tile = Tile(name, (0.9, 0.93, 0.8))
    random = Random(seed)
    base = tile.base()
    center = (tile.width * 0.5, tile.height * middle)
    radii = (tile.width * fill[0], tile.height * fill[1])
    targets = scatter(random, tile, count, center, radii, leaf_length * 0.45)
    twigs = grow(random, base, targets, math.sqrt(radii[0] * radii[1]) * 0.5)
    for parent, point in twigs:
        long = math.hypot(point[0] - parent[0], point[1] - parent[1])
        tile.line(parent, point, max(stem * min(long / 60.0, 1.0), 1.3), 1.2, STEM_TONE)

    leaves = []
    for index, (parent, point) in enumerate(twigs):
        along = math.atan2(point[1] - parent[1], point[0] - parent[0])
        # the leaf at the end of a twig goes on along it, and those before it stand out to its sides, one after the other
        leaves.append((point, along + random.range(-0.35, 0.35), 1.0))
        long = math.hypot(point[0] - parent[0], point[1] - parent[1])
        for pair in range(pairs if long > leaf_length * 0.5 else 0):
            t = (pair + 0.5 + random.range(-0.2, 0.2)) / pairs
            side = 1.0 if (index + pair) % 2 else -1.0
            at = (parent[0] + (point[0] - parent[0]) * t, parent[1] + (point[1] - parent[1]) * t)
            leaves.append((at, along + side * spread * random.range(0.7, 1.2), 0.85))
    # the leaves nearest the stalk first, so those at the ends lie on top and are the lightest
    leaves.sort(key=lambda leaf: math.hypot(leaf[0][0] - base[0], leaf[0][1] - base[1]))
    for index, (at, angle, size) in enumerate(leaves):
        depth = 0.86 + 0.14 * index / max(len(leaves) - 1, 1)
        length = leaf_length * size * random.range(0.8, 1.15)
        tile.leaf(at, angle, length, leaf_width * size * random.range(0.85, 1.15), scale(leaf_tone(random, low), depth), shape)
    return tile


def draw_spruce(name, seed):
    """A bough of a spruce: a stalk with side twigs that shorten towards its tip, all of them thick with short needles, lighter at their ends"""
    tile = Tile(name, (0.82, 0.9, 0.8))
    random = Random(seed)
    base = tile.base()
    top = tile.height - GUTTER - 6.0
    spine = [(base[0] + math.sin(t * 2.2 + seed) * 5.0 * t, base[1] + (top - base[1]) * t) for t in (0.0, 0.2, 0.4, 0.6, 0.8, 1.0)]
    twigs = [(spine, 1.0)]
    count = 13
    for index in range(count):
        t = (index + 0.6) / (count + 0.3)
        at = (base[0] + math.sin(t * 2.2 + seed) * 5.0 * t, base[1] + (top - base[1]) * t)
        for side in (-1.0, 1.0):
            long = (tile.width * 0.5 - GUTTER - 14.0) * (1.0 - t) ** 0.7 * random.range(0.82, 1.0) + 12.0
            angle = math.pi * 0.5 - side * random.range(1.0, 1.25)
            points = [at]
            for step in range(1, 4):
                # a side twig sweeps forwards towards the tip of the bough
                bent = angle + side * 0.1 * step
                points.append((points[-1][0] + math.cos(bent) * long / 3.0, points[-1][1] + math.sin(bent) * long / 3.0))
            twigs.append((points, 0.8))
            if long > 44.0:
                # a twiglet half way out, so the wide lower part has no gaps
                middle = points[1]
                for fork in (-1.0, 1.0):
                    bent = angle + fork * 0.75
                    twigs.append(([middle, (middle[0] + math.cos(bent) * long * 0.32, middle[1] + math.sin(bent) * long * 0.32)], 0.6))
    for points, size in twigs:
        tile.path(points, 3.0 * size, 1.2, STEM_TONE)
    for points, size in twigs:
        total = sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:]))
        walked = 0.0
        for a, b in zip(points, points[1:]):
            long = math.hypot(b[0] - a[0], b[1] - a[1])
            along = math.atan2(b[1] - a[1], b[0] - a[0])
            steps = max(int(long / 1.9), 1)
            for step in range(steps):
                t = (step + random.unit()) / steps
                at = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                tip = (walked + long * t) / max(total, 1.0)
                tone = scale(random.pick(LEAF_TONES), random.range(0.6, 0.85) + 0.25 * tip * tip)
                for side in (-1.0, 1.0):
                    angle = along + side * random.range(0.5, 1.0)
                    needle = random.range(9.0, 14.0) * (0.75 + 0.25 * size)
                    tile.line(at, (at[0] + math.cos(angle) * needle, at[1] + math.sin(angle) * needle), 2.6, 1.1, tone)
            walked += long
    return tile


def draw_pine(name, seed):
    """The end of a branch of a pine: a few twigs, each ending in a round tuft of long needles"""
    tile = Tile(name, (0.82, 0.9, 0.8))
    random = Random(seed)
    base = tile.base()
    tufts = scatter(random, tile, 13, (tile.width * 0.5, tile.height * 0.57), (tile.width * 0.38, tile.height * 0.36), 30.0)
    twigs = grow(random, base, tufts, 60.0)
    for parent, point in twigs:
        tile.line(parent, point, 4.0, 2.2, STEM_TONE)
    for index, (parent, point) in enumerate(twigs):
        along = math.atan2(point[1] - parent[1], point[0] - parent[0])
        for needle in range(110):
            angle = along + random.range(-2.1, 2.1)
            long = random.range(18.0, 36.0)
            tone = scale(random.pick(LEAF_TONES), random.range(0.6, 1.0))
            start = (point[0] + random.range(-3.0, 3.0), point[1] + random.range(-3.0, 3.0))
            tile.line(start, (start[0] + math.cos(angle) * long, start[1] + math.sin(angle) * long), 2.6, 1.0, tone)
    return tile


def draw_willow(name, seed):
    """Strands of a weeping willow: long thin twigs from the stalk, lined with narrow leaves"""
    tile = Tile(name, (0.9, 0.93, 0.8))
    random = Random(seed)
    base = tile.base()
    top = tile.height - GUTTER - 4.0
    strands = 9
    for strand in range(strands):
        f = (strand + 0.5) / strands
        end_x = GUTTER + 14.0 + (tile.width - 2.0 * GUTTER - 28.0) * f + random.range(-8.0, 8.0)
        long = (top - base[1]) * random.range(0.72, 1.0)
        points = []
        for step in range(9):
            t = step / 8.0
            sway = math.sin(t * 5.0 + strand * 1.7) * 5.0 * t
            points.append((base[0] + (end_x - base[0]) * t ** 0.55 + sway, base[1] + long * t))
        tile.path(points, 2.2, 1.0, STEM_TONE)
        for index, (a, b) in enumerate(zip(points, points[1:])):
            along = math.atan2(b[1] - a[1], b[0] - a[0])
            for leaf in range(4):
                t = (leaf + random.unit()) / 4.0
                at = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                side = 1.0 if (index + leaf) % 2 else -1.0
                tile.leaf(at, along + side * random.range(0.25, 0.6), random.range(24.0, 34.0), random.range(6.5, 9.0), leaf_tone(random), 'lance', edge=0.1)
    return tile


def draw_acacia(name, seed):
    """A spray of an acacia: twigs that fan out flat, each a feather of many small leaflets"""
    tile = Tile(name, (0.9, 0.93, 0.8))
    random = Random(seed)
    base = tile.base()
    targets = scatter(random, tile, 15, (tile.width * 0.5, tile.height * 0.56), (tile.width * 0.39, tile.height * 0.38), 26.0)
    twigs = grow(random, base, targets, 70.0)
    for parent, point in twigs:
        tile.line(parent, point, 2.6, 1.2, STEM_TONE)
    for parent, point in twigs:
        along = math.atan2(point[1] - parent[1], point[0] - parent[0])
        for fan in (-0.8, 0.0, 0.8):
            angle = along + fan + random.range(-0.15, 0.15)
            long = random.range(42.0, 58.0)
            end = (point[0] + math.cos(angle) * long, point[1] + math.sin(angle) * long)
            tile.line(point, end, 1.4, 0.8, STEM_TONE)
            steps = int(long / 4.6)
            for step in range(1, steps + 1):
                t = step / steps
                at = (point[0] + (end[0] - point[0]) * t, point[1] + (end[1] - point[1]) * t)
                size = math.sin(math.pi * (0.12 + 0.88 * t) ** 0.8) * random.range(0.85, 1.1)
                for side in (-1.0, 1.0):
                    tile.leaf(at, angle + side * 1.15, 12.0 * size + 2.0, 5.4, leaf_tone(random, 0.72), 'round', vein=0.0, edge=0.1)
    return tile


def draw_dry_twig(name, seed):
    """Bare twigs that fork again and again, of a dead tree or a bush of dry land, with a few withered leaves"""
    tile = Tile(name, (0.75, 0.7, 0.62))
    random = Random(seed)

    def fork(start, angle, long, width, depth):
        bends = [random.range(-0.25, 0.25) for _ in range(3)]
        for attempt in range(5):
            points = [start]
            turned = angle
            for bend in bends:
                turned += bend
                points.append((points[-1][0] + math.cos(turned) * long / 3.0, points[-1][1] + math.sin(turned) * long / 3.0))
            if tile.inside(points[-1][0], points[-1][1], 2.0):
                break
            long *= 0.7
        else:
            return
        angle = turned
        tone = scale((0.95, 0.9, 0.82), random.range(0.6, 1.0))
        tile.path(points, width, max(width * 0.55, 1.0), tone)
        if depth <= 0:
            if random.unit() < 0.25:
                tile.leaf(points[-1], angle + random.range(-0.6, 0.6), random.range(9.0, 14.0), 5.0, scale((1.0, 0.85, 0.6), random.range(0.6, 0.9)), 'ovate')
            return
        for child in range(random.pick([2, 3, 3])):
            at = points[random.pick([1, 2, 3])]
            fork(at, angle + random.range(0.35, 0.95) * (1.0 if child % 2 else -1.0), long * random.range(0.55, 0.78), max(width * 0.62, 1.1), depth - 1)

    base = tile.base()
    for trunk in range(4):
        fork((base[0], base[1] + 2.0), math.pi * 0.5 + (trunk - 1.5) * 0.5 + random.range(-0.12, 0.12), random.range(80.0, 105.0), 4.6, 4)
    return tile


def draw_blades(tile, random, count, heights, spread, low, high, width=3.0, droop=0.3, heads=0.0, head_color=None, base_spread=0.3):
    """Blades of grass from the foot of a tile, fanning out and bending over under their weight, some with a seed head"""
    base = tile.base()
    for index in range(count):
        f = random.range(-1.0, 1.0)
        start = (base[0] + f * tile.width * base_spread, base[1] + 1.0)
        lean = f * spread + random.range(-0.18, 0.18)
        height = (tile.height - 2.0 * GUTTER - 6.0) * random.range(*heights)
        points = [start]
        angle = math.pi * 0.5 - lean
        for step in range(5):
            angle -= lean * droop * 0.6 + math.copysign(droop * 0.12, lean) * step
            points.append((points[-1][0] + math.cos(angle) * height / 5.0, points[-1][1] + math.sin(angle) * height / 5.0))
        # a blade that would leave the tile is cut where it does
        while len(points) > 2 and not tile.inside(points[-1][0], points[-1][1]):
            points.pop()
        shade = random.range(0.72, 1.0)
        tile.path(points, width * random.range(0.8, 1.2), 0.9, scale(low, shade), scale(high, shade))
        if heads > 0.0 and random.unit() < heads:
            tip, before = points[-1], points[-2]
            along = math.atan2(tip[1] - before[1], tip[0] - before[0])
            for seed in range(7):
                at = (tip[0] - math.cos(along) * seed * 2.6, tip[1] - math.sin(along) * seed * 2.6)
                side = 1.0 if seed % 2 else -1.0
                tile.leaf(at, along + side * 0.7, random.range(6.0, 9.0), 2.6, scale(head_color, random.range(0.8, 1.1)), 'lance', vein=0.0)


def draw_grass(name, seed):
    tile = Tile(name, (0.9, 0.95, 0.8))
    draw_blades(tile, Random(seed), 80, (0.4, 1.0), 0.6, (0.62, 0.7, 0.55), (1.0, 1.0, 0.86), width=5.5)
    return tile


def draw_dry_grass(name, seed):
    tile = Tile(name, (0.74, 0.67, 0.42))
    draw_blades(tile, Random(seed), 84, (0.4, 1.0), 0.55, (0.5, 0.46, 0.26), (0.85, 0.78, 0.52), width=4.6, heads=0.4, head_color=(0.78, 0.67, 0.45))
    return tile


def draw_wheat(name, seed):
    """Stalks of wheat, green gold at the foot and gold above, each under a bearded ear"""
    tile = Tile(name, (0.84, 0.75, 0.44))
    random = Random(seed)
    base = tile.base()
    stalks = 15
    for index in range(stalks):
        f = (index + 0.5) / stalks * 2.0 - 1.0 + random.range(-0.08, 0.08)
        start = (base[0] + f * tile.width * 0.3, base[1])
        lean = f * 0.16 + random.range(-0.1, 0.1)
        height = (tile.height - 2.0 * GUTTER - 44.0) * random.range(0.74, 1.0)
        angle = math.pi * 0.5 - lean
        points = [start]
        for step in range(4):
            angle -= lean * 0.2
            points.append((points[-1][0] + math.cos(angle) * height / 4.0, points[-1][1] + math.sin(angle) * height / 4.0))
        shade = random.range(0.8, 1.0)
        tile.path(points, 3.0, 1.8, scale((0.54, 0.52, 0.26), shade), scale((0.84, 0.74, 0.42), shade))
        # a long leaf on the stalk
        middle = points[2]
        tile.leaf(middle, angle + random.pick([-1.0, 1.0]) * random.range(0.5, 0.9), random.range(26.0, 38.0), 4.0, scale((0.7, 0.64, 0.32), shade), 'lance', vein=0.0)
        # the ear: grains in two rows, each with a long awn
        tip = points[-1]
        ear = random.range(26.0, 34.0)
        gold = scale((0.94, 0.84, 0.54), random.range(0.85, 1.05))
        for grain in range(8):
            t = grain / 7.0
            at = (tip[0] + math.cos(angle) * ear * t, tip[1] + math.sin(angle) * ear * t)
            if not tile.inside(at[0], at[1], 2.0):
                break
            side = 1.0 if grain % 2 else -1.0
            tile.line(at, (at[0] + math.cos(angle + side * 0.3) * 22.0, at[1] + math.sin(angle + side * 0.3) * 22.0), 1.3, 0.7, scale(gold, 0.95))
            tile.leaf(at, angle + side * 0.45, 11.0, 7.0, gold, 'ovate', vein=0.0, edge=0.3)
    return tile


def draw_reeds(name, seed):
    """Reeds and cattails of a swamp: tall blades, yellowing at their tips, and brown heads on straight stalks"""
    tile = Tile(name, (0.5, 0.55, 0.25))
    random = Random(seed)
    draw_blades(tile, random, 30, (0.5, 1.0), 0.32, (0.33, 0.42, 0.16), (0.62, 0.6, 0.27), width=5.6, droop=0.2)
    base = tile.base()
    for index in range(4):
        f = (index + 0.5) / 4.0 * 2.0 - 1.0
        start = (base[0] + f * tile.width * 0.22, base[1])
        height = (tile.height - 2.0 * GUTTER - 14.0) * random.range(0.62, 0.92)
        lean = f * 0.1 + random.range(-0.05, 0.05)
        end = (start[0] + math.sin(lean) * height, start[1] + math.cos(lean) * height)
        tile.line(start, end, 2.2, 1.5, (0.4, 0.45, 0.2), (0.55, 0.5, 0.25))
        head = random.range(24.0, 32.0)
        head_start = (end[0] - math.sin(lean) * (head + 8.0), end[1] - math.cos(lean) * (head + 8.0))
        head_end = (end[0] - math.sin(lean) * 8.0, end[1] - math.cos(lean) * 8.0)
        tile.line(head_start, head_end, 7.5, 7.0, scale((0.36, 0.2, 0.1), random.range(0.85, 1.1)))
    return tile


def draw_blossom(tile, random, center, radius, kind, squash=1.0):
    """A flower seen from above (squash 1) or from its side: petals around a middle"""
    if kind == 'daisy':
        petals, petal_color, middle = random.pick([11, 12, 13]), (0.97, 0.97, 0.93), (0.95, 0.75, 0.1)
    elif kind == 'yellow':
        petals, petal_color, middle = 5, (0.98, 0.82, 0.1), (0.85, 0.55, 0.05)
    elif kind == 'poppy':
        petals, petal_color, middle = 4, (0.86, 0.1, 0.06), (0.1, 0.07, 0.07)
    elif kind == 'blue':
        petals, petal_color, middle = 5, (0.3, 0.4, 0.9), (0.95, 0.9, 0.6)
    else:
        petals, petal_color, middle = 6, (0.62, 0.3, 0.8), (0.95, 0.85, 0.4)
    turn = random.range(0.0, 2.0 * math.pi)
    wide = {'daisy': 0.42, 'yellow': 0.95, 'poppy': 1.25, 'blue': 0.9}.get(kind, 0.75)
    for petal in range(petals):
        angle = turn + 2.0 * math.pi * petal / petals
        # a flower seen from its side is flattened, so its petals shorten as they point up or down
        long = radius * (1.0 if squash >= 1.0 else math.hypot(math.cos(angle), math.sin(angle) * squash))
        direction = math.atan2(math.sin(angle) * squash, math.cos(angle))
        tile.leaf(center, direction, long, radius * wide, scale(petal_color, random.range(0.86, 1.0)), 'petal', vein=0.0, edge=0.25)
    tile.disc(center, radius * (0.3 if kind != 'poppy' else 0.24), middle, squash)


def draw_flowers(name, seed, kinds, spike=None):
    """Flowers of a meadow from their side: grass, stems with a few leaves, and a blossom on each"""
    tile = Tile(name, (0.4, 0.5, 0.2))
    random = Random(seed)
    green_low, green_high = (0.2, 0.3, 0.1), (0.42, 0.55, 0.2)
    draw_blades(tile, random, 34, (0.3, 0.75), 0.55, green_low, green_high, width=4.0)
    base = tile.base()
    stems = 9
    blossoms = []
    for index in range(stems):
        f = (index + 0.5) / stems * 2.0 - 1.0 + random.range(-0.1, 0.1)
        start = (base[0] + f * tile.width * 0.24, base[1])
        height = (tile.height - 2.0 * GUTTER - 26.0) * random.range(0.5, 1.0)
        lean = f * 0.32 + random.range(-0.1, 0.1)
        points = [start]
        angle = math.pi * 0.5 - lean
        for step in range(4):
            angle -= lean * 0.15
            points.append((points[-1][0] + math.cos(angle) * height / 4.0, points[-1][1] + math.sin(angle) * height / 4.0))
        tile.path(points, 2.2, 1.4, green_low, green_high)
        for leaf in (1, 2):
            tile.leaf(points[leaf], angle + (1.0 if (index + leaf) % 2 else -1.0) * random.range(0.6, 1.0), random.range(14.0, 22.0), 6.0,
                      scale(green_high, random.range(0.7, 1.0)), 'lance')
        blossoms.append((points[-1], angle, random.pick(kinds)))
    for tip, angle, kind in blossoms:
        if not tile.inside(tip[0], tip[1], 14.0):
            continue
        if kind == spike:
            # a spike of small bells up the end of the stem, as a lupine or a bluebell
            for bell in range(9):
                at = (tip[0] - math.cos(angle) * bell * 4.6 + random.range(-3.0, 3.0), tip[1] - math.sin(angle) * bell * 4.6)
                tile.disc(at, random.range(4.0, 5.6), scale((0.42, 0.33, 0.85) if bell % 3 else (0.6, 0.4, 0.9), random.range(0.85, 1.1)), 0.9)
        else:
            draw_blossom(tile, random, tip, random.range(12.0, 16.0), kind, squash=0.6)
    return tile


def draw_blossoms_top(name, seed, kinds, count):
    """A patch of flowers seen from above: a few leaves under blossoms that lie open to the sky"""
    tile = Tile(name, (0.4, 0.5, 0.2))
    random = Random(seed)
    center = (tile.width * 0.5, tile.height * 0.5)
    for leaf in range(22):
        angle = random.range(0.0, 2.0 * math.pi)
        out = random.range(0.0, tile.width * 0.3)
        at = (center[0] + math.cos(angle) * out, center[1] + math.sin(angle) * out)
        tile.leaf(at, angle + random.range(-0.8, 0.8), random.range(14.0, 24.0), random.range(5.0, 8.0), scale((0.36, 0.5, 0.18), random.range(0.6, 1.0)), 'lance')
    for point in scatter(random, tile, count, center, (tile.width * 0.36, tile.height * 0.36), 12.0):
        draw_blossom(tile, random, point, random.range(8.0, 11.5), random.pick(kinds))
    return tile


def draw_field_top(name, seed, low, high, background, count=190, width=4.0, long=(16.0, 30.0), turn=3.14, heads=None, spread=0.82):
    """Grass or a field seen from above: blades lying over a round patch with a ragged edge, every way as tufts do (turn of pi) or mostly one
    way as the wind lays a field (a small turn), some under a seed head"""
    tile = Tile(name, background)
    random = Random(seed)
    center = (tile.width * 0.5, tile.height * 0.5)
    reach = tile.width * 0.5 - GUTTER - 2.0
    flow = random.range(0.0, 2.0 * math.pi)
    for blade in range(count):
        angle = random.range(0.0, 2.0 * math.pi)
        out = math.sqrt(random.unit()) * reach * spread
        start = (center[0] + math.cos(angle) * out, center[1] + math.sin(angle) * out)
        way = flow + random.range(-turn, turn)
        size = random.range(*long)
        bend = way + random.range(-0.4, 0.4)
        middle = (start[0] + math.cos(way) * size * 0.5, start[1] + math.sin(way) * size * 0.5)
        end = (middle[0] + math.cos(bend) * size * 0.5, middle[1] + math.sin(bend) * size * 0.5)
        if math.hypot(end[0] - center[0], end[1] - center[1]) > reach:
            continue
        shade = random.range(0.62, 1.0)
        tile.path([start, middle, end], width, 0.9, scale(low, shade), scale(high, shade))
        if heads and random.unit() < 0.7:
            tile.leaf(end, bend, random.range(10.0, 14.0), 5.4, scale(heads, random.range(0.85, 1.05)), 'ovate', vein=0.0, edge=0.3)
    return tile


def draw_heather(name, seed):
    """A mat of heather and dwarf shrubs of the tundra from above: tiny leaves, olive and rusty, dotted with pink"""
    tile = Tile(name, (0.4, 0.35, 0.2))
    random = Random(seed)
    center = (tile.width * 0.5, tile.height * 0.5)
    for point in scatter(random, tile, 150, center, (tile.width * 0.4, tile.height * 0.4), 4.0):
        color = random.pick([(0.42, 0.4, 0.2), (0.34, 0.42, 0.2), (0.55, 0.33, 0.18), (0.48, 0.3, 0.16)])
        tile.leaf(point, random.range(0.0, 2.0 * math.pi), random.range(7.0, 11.0), 4.0, scale(color, random.range(0.7, 1.1)), 'ovate', vein=0.0)
    for point in scatter(random, tile, 26, center, (tile.width * 0.36, tile.height * 0.36), 5.0):
        tile.disc(point, random.range(1.6, 2.6), random.pick([(0.8, 0.45, 0.6), (0.72, 0.4, 0.66), (0.86, 0.6, 0.7)]))
    return tile


def draw_rosette(name, seed):
    """Broad leaves lying out from a middle, seen from above: the low plants between the grass"""
    tile = Tile(name, (0.9, 0.93, 0.8))
    random = Random(seed)
    center = (tile.width * 0.5, tile.height * 0.5)
    for ring, (count, long) in enumerate(((9, 50.0), (7, 38.0), (5, 24.0))):
        for leaf in range(count):
            angle = 2.0 * math.pi * (leaf + random.range(-0.25, 0.25)) / count + ring * 0.4
            tile.leaf(center, angle, long * random.range(0.8, 1.05), long * 0.42, scale(leaf_tone(random), 0.8 + 0.1 * ring), 'ovate')
    return tile


def draw_tiles():
    return [
        draw_spray('LeafA', 3, 34, 46.0, 23.0, 'ovate', pairs=2),
        draw_spray('LeafB', 5, 44, 34.0, 25.0, 'round', pairs=2, spread=1.0),
        draw_spray('Oak', 7, 26, 56.0, 32.0, 'oak', pairs=2, spread=0.8, stem=4.0),
        draw_spray('Birch', 11, 52, 27.0, 20.0, 'birch', fill=(0.41, 0.42), pairs=2, spread=1.1, stem=2.2, low=0.72),
        draw_spruce('Spruce', 13),
        draw_pine('Pine', 17),
        draw_willow('Willow', 19),
        draw_acacia('Acacia', 23),
        draw_spray('Bush', 29, 84, 22.0, 15.0, 'round', fill=(0.43, 0.42), pairs=2, spread=1.0, stem=2.4),
        draw_dry_twig('DryTwig', 31),
        draw_grass('Grass', 37),
        draw_dry_grass('DryGrass', 41),
        draw_wheat('Wheat', 43),
        draw_reeds('Reeds', 47),
        draw_flowers('FlowersA', 53, ['daisy', 'daisy', 'yellow']),
        draw_flowers('FlowersB', 59, ['poppy', 'poppy', 'spike', 'violet'], spike='spike'),
        draw_blossoms_top('BlossomWhite', 61, ['daisy'], 9),
        draw_blossoms_top('BlossomYellow', 67, ['yellow'], 10),
        draw_blossoms_top('BlossomRed', 71, ['poppy'], 7),
        draw_blossoms_top('BlossomBlue', 73, ['blue', 'violet'], 10),
        draw_field_top('GrassTop', 79, (0.6, 0.7, 0.52), (1.0, 1.0, 0.86), (0.9, 0.95, 0.8)),
        draw_field_top('WheatTop', 83, (0.66, 0.59, 0.32), (0.86, 0.77, 0.46), (0.84, 0.75, 0.44), count=330, width=2.8, long=(10.0, 18.0), turn=0.5, spread=0.98,
                       heads=(0.94, 0.84, 0.54)),
        draw_heather('Heather', 89),
        draw_rosette('Rosette', 97),
    ]


def write_png(path, width, height, pixels):
    def chunk(kind, data):
        body = kind + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body) & 0xFFFFFFFF)

    rows = b''.join(b'\0' + bytes(pixels[row * width * 4:(row + 1) * width * 4]) for row in range(height))
    with open(path, 'wb') as file:
        file.write(b'\x89PNG\r\n\x1a\n')
        file.write(chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)))
        file.write(chunk(b'IDAT', zlib.compress(rows, 9)))
        file.write(chunk(b'IEND', b''))


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    pixels = bytearray(SIZE * SIZE * 4)
    for tile in draw_tiles():
        x, y, width, height, _ = TILES[tile.name]
        for row in range(height):
            for column in range(width):
                source = row * width + column
                target = ((y + row) * SIZE + x + column) * 4
                pixels[target] = int(min(max(tile.red[source], 0.0), 1.0) * 255.0 + 0.5)
                pixels[target + 1] = int(min(max(tile.green[source], 0.0), 1.0) * 255.0 + 0.5)
                pixels[target + 2] = int(min(max(tile.blue[source], 0.0), 1.0) * 255.0 + 0.5)
                pixels[target + 3] = int(min(max(tile.alpha[source], 0.0), 1.0) * 255.0 + 0.5)
        print('%s: %.0f%% covered' % (tile.name, tile.coverage() * 100.0))
    path = os.path.join(OUTPUT_FOLDER, ATLAS_NAME + '.png')
    write_png(path, SIZE, SIZE, pixels)
    print('Wrote %s' % path)


if __name__ == '__main__':
    main()
