"""Generates the meshes of the castles of the hex planet (UHexCastles).

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_castles.py
Writes GLB files and castles.json to Saved/HexPlanetCastles, then run import_planet_castles.py with the commandlet. The GLB files open in
Blender, so a mesh can be reworked by hand; all are placeholders for the meshes of an artist, named in DA_HexCastles.

A castle takes three cells that share a corner and is nearly round, as the developer asked on 2026-11-11, when it was a trefoil. It has no
ground of its own: it stands on the ground of its cells and follows it, so its wall is not one mesh but pieces, each of which UHexCastles
stands on the ground where it lies along the rim: Wall<Level> a stretch of the wall, with +X outwards and Y along the wall, Tower<Level> a
tower in it, Gate<Level> its gate, by the level of the fort, 0 a fence before one is built, 1 a palisade, 2 and 3 stone. Each building is a
mesh of its own, standing on its origin with its front towards +X, which is stood on the ground at the place of the building once it is
built. Every mesh reaches SKIRT under its origin, so it stands in the ground where that falls away under it.

They are drawn with M_HexPlanetCastle: the color of a vertex is its color, and its alpha 0.1 (MASONRY) is stone, which takes the masonry of
the style of its faction by its UV, in tiles of TILE units, as light as the color of the vertex, which is the shade baked into a mesh: the
plinths darker, the merlons and courses lighter, a tower darker where the wall runs into it (the material adds the shade of the ground and
of the corners of the wall); 0.3 (GLOW) glows by itself, as a fire or a lit window, 0.5 takes the color of the
owner, as the roofs, 0.75 is an ember, a sparkle as those of the piles, and 1 the flag. Both factions share the meshes for now and differ in
their masonry.

A hero, 2.5 times its mesh, is about 125 units high, so since 2026-11-11, when the developer found the castle low, the walls are about as
high as a rider, the towers and gates higher, and the halls and dwellings are stretched HEIGHT times above the ground after they are
modelled (stretched), their masonry keeping the size of its blocks.

A castle is alive by what the material moves, marked by a V of 4 or more in the UV of a vertex: the leaves of a gate (DOOR_V, plain, with U
how far a vertex lies from the hinge of its leaf along Y, signed) swing into the yard, towards -X, when a hero goes through; cloth of the
owner (CLOTH_V and a hundredth of its length, with U how far from its pole, 0 to 1), which lies along -X of its mesh, is laid along the wind
and waves; and a flame (FLAME_V, which glows, with U how far up it) dances. A roof takes the tiles of T_CastleRoof by its UV, in tiles of
ROOF_TILE units, along its eaves and up its slope, with a V under 4: under PLAIN_ROOF_V for a roof in the color of the owner, as those of a
hall, and from it up for the others, which keep the color of their tiles.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'HexPlanetObjects')))
sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'HexPlanetSites')))
from generate_planet_objects import Mesh, add, add_blob, hash_unit, scale_color, write_glb  # noqa: E402
from generate_planet_sites import add_box, add_cylinder, add_sparkle, rotated  # noqa: E402

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetCastles'))

# the tone of stone, by which the material lights its masonry: 0.42 draws the texture as it is
STONE = (0.42, 0.42, 0.42)
TIMBER = (0.27, 0.17, 0.09)
DARK = (0.02, 0.018, 0.015)
GOLD = (0.95, 0.65, 0.1)
# what the material tints is nearly white, so the color of the owner shows as it is
TINTED = (0.9, 0.9, 0.9)
MASONRY = 0.1
GLOW = 0.3
ROOF = 0.5
OWNER = 1.0
# a tile of the masonry is this many units, eight courses of blocks
TILE = 36.0
# how far a mesh reaches under its origin
SKIRT = 30.0
# a stretch of the wall is this long, a little more than the rim divided by the number of stretches, so they overlap where it bends
WALL_LENGTH = 30.0
# how many times higher the halls and dwellings stand than they are modelled
HEIGHT = 1.8
# what the material moves, by the V of the UV of a vertex
DOOR_V = 5.0
CLOTH_V = 7.0
FLAME_V = 9.0
# a tile of the texture of the roofs is this many units, about twenty channels of clay tiles
ROOF_TILE = 70.0
# the V of the UV of a roof that keeps the color of its tiles starts here, and that of one in the color of the owner at 0: the developer
# found too much of that color on a castle on 2026-11-11, so only its flags and pennants and the roofs of its hall take it
PLAIN_ROOF_V = 2.0
# a tower is this much darker where the wall runs into it, up to the height of the wall, and lighter again this far above it
JUNCTION_SHADE = 0.5
JUNCTION_FADE = 10.0
# what stands out of the stone, lighter, and its foot, darker, so a castle is not one tone: its merlons and string courses, and its plinths
TRIM_TONE = 1.18
PLINTH_TONE = 0.78
# how far a roof reaches out over the long walls of its house, so it casts a shadow on them
EAVES = 3.5
# stone is this much as light where it meets other stone in a corner, as under a crown or at the foot of a merlon, where dirt gathers, and
# its own tone again this far from the corner
GRIME_SHADE = 0.5
GRIME_BAND = 5.0
WINDOW = (0.5, 0.36, 0.15)
FLAME_LOW = (1.0, 0.3, 0.04)
FLAME_HIGH = (1.0, 0.78, 0.25)


def note_vertical(mesh, index, shift):
    """Remembers that the V of the masonry of a vertex runs up with its height, from a shift, so stretched can keep its blocks their size"""
    mesh.__dict__.setdefault('vertical', {})[index] = shift


def stretched(mesh, factor=HEIGHT):
    """Makes a mesh that many times higher above its origin, as the buildings are modelled low: what is under the ground stays, and masonry
    whose courses run up a face keeps the size of its blocks"""
    for index, position in enumerate(mesh.positions):
        if position[2] > 0.0:
            mesh.positions[index] = (position[0], position[1], position[2] * factor)
    for index, shift in mesh.__dict__.get('vertical', {}).items():
        mesh.uvs[index] = (mesh.uvs[index][0], mesh.positions[index][2] / TILE + shift)
    # and the tiles of a roof theirs up its slope, which is steeper now
    for index, (eave, run, base) in mesh.__dict__.get('slopes', {}).items():
        rise = mesh.positions[index][2] - (eave * factor if eave > 0.0 else eave)
        mesh.uvs[index] = (mesh.uvs[index][0], base + math.hypot(run, rise) / ROOF_TILE)
    return mesh


def roof_vertex(mesh, point, along, eave, run, tone, own=False):
    """A vertex of a roof: its U along the eaves and its V up the slope from them, by how high the eaves lie and how far in from them it
    lies over the ground. A roof is clay tiles in their own color, its V from PLAIN_ROOF_V up, and only one that is the owner's, as that
    of a hall, takes the color of the owner, its V from 0"""
    base = 0.0 if own else PLAIN_ROOF_V
    index = mesh.vertex(point, scale_color(TINTED, tone), ROOF, uv=(along / ROOF_TILE, base + math.hypot(run, point[2] - eave) / ROOF_TILE))
    mesh.__dict__.setdefault('slopes', {})[index] = (eave, run, base)
    return index


def marked_box(mesh, low, high, color, alpha, uv_of, seed=0):
    """A box between two corners along the axes, each face its own tone, with a UV the material reads, a function of the place of a vertex"""
    mesh.begin_part(flat=True)
    center = ((low[0] + high[0]) * 0.5, (low[1] + high[1]) * 0.5, (low[2] + high[2]) * 0.5)
    faces = (((0, 0, 0), (0, 1, 0), (0, 1, 1), (0, 0, 1)), ((1, 0, 0), (1, 1, 0), (1, 1, 1), (1, 0, 1)), ((0, 0, 0), (1, 0, 0), (1, 0, 1), (0, 0, 1)),
        ((0, 1, 0), (1, 1, 0), (1, 1, 1), (0, 1, 1)), ((0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)), ((0, 0, 1), (1, 0, 1), (1, 1, 1), (0, 1, 1)))
    for index, face in enumerate(faces):
        tone = 0.82 + 0.3 * hash_unit(seed, index, 9)
        corners = []
        for corner in face:
            point = tuple(high[axis] if corner[axis] else low[axis] for axis in range(3))
            corners.append(mesh.vertex(point, scale_color(color, tone), alpha, uv=uv_of(point)))
        mesh.quad(corners[0], corners[1], corners[2], corners[3], center)


def add_leaf(mesh, hinge, toward, width, bottom, top, x=1.5, seed=0, planks=4):
    """A leaf of a gate: planks from its hinge at a Y towards the middle of the gate, bound with two bands of iron, which the material swings
    around the hinge into the yard"""
    def uv_of(point):
        return (point[1] - hinge, DOOR_V)

    for plank in range(planks):
        start = hinge + toward * width * plank / planks
        end = hinge + toward * width * (plank + 1) / planks
        marked_box(mesh, (x - 1.2, min(start, end), bottom), (x + 1.2, max(start, end), top), scale_color(TIMBER, 0.85 + 0.4 * hash_unit(seed, plank)), 0.0, uv_of, seed + plank)
    far = hinge + toward * width
    for share in (0.22, 0.78):
        height = bottom + (top - bottom) * share
        marked_box(mesh, (x - 1.7, min(hinge, far), height - 1.6), (x + 1.7, max(hinge, far), height + 1.6), DARK, 0.0, uv_of, seed + 11)


def add_rail_leaf(mesh, hinge, toward, width, bottom, top, x=0.0, seed=0):
    """A leaf of the gate of a fence: rails between two stiles, which swings as any leaf"""
    def uv_of(point):
        return (point[1] - hinge, DOOR_V)

    far = hinge + toward * width
    for share in (0.12, 0.5, 0.88):
        height = bottom + (top - bottom) * share
        marked_box(mesh, (x - 0.6, min(hinge, far), height - 0.9), (x + 0.6, max(hinge, far), height + 0.9), TIMBER, 0.0, uv_of, seed)
    for edge in (hinge, far - toward * 1.6):
        marked_box(mesh, (x - 0.8, min(edge, edge + toward * 1.6), bottom), (x + 0.8, max(edge, edge + toward * 1.6), top), TIMBER, 0.0, uv_of, seed + 3)


def add_flame(mesh, base, radius, height, seed=0, sides=6):
    """A flame: two cones that glow, red at the foot and yellow at the tip, which the material makes dance by how far up a vertex is"""
    for scale, low, high in ((1.0, FLAME_LOW, FLAME_HIGH), (0.55, FLAME_HIGH, (1.0, 0.95, 0.6))):
        mesh.begin_part()
        tip = mesh.vertex(add(base, (0.0, 0.0, height * scale)), high, GLOW, uv=(1.0, FLAME_V))
        ring = []
        for step in range(sides):
            angle = 2.0 * math.pi * (step + hash_unit(seed, step) * 0.5) / sides
            ring.append(mesh.vertex(add(base, (radius * scale * math.cos(angle), radius * scale * math.sin(angle), 0.0)), low, GLOW, uv=(0.0, FLAME_V)))
        for step in range(sides):
            mesh.triangle(ring[step], ring[(step + 1) % sides], tip, add(base, (0.0, 0.0, height * 0.3)))


def add_brazier(mesh, foot, seed=0, stand=20.0):
    """A brazier on a stand: an iron bowl with a fire in it, and embers that rise from it"""
    add_cylinder(mesh, add(foot, (0.0, 0.0, -SKIRT * 0.4)), add(foot, (0.0, 0.0, stand)), 1.5, 5, DARK, 0.0, DARK, seed)
    add_cylinder(mesh, add(foot, (0.0, 0.0, stand - 1.0)), add(foot, (0.0, 0.0, stand + 4.0)), 3.4, 7, DARK, 0.0, FLAME_LOW, seed + 1, taper=1.8)
    add_flame(mesh, add(foot, (0.0, 0.0, stand + 3.5)), 5.0, 15.0, seed + 2)
    for index in range(3):
        angle = 2.0 * math.pi * hash_unit(seed, index, 5)
        add_sparkle(mesh, add(foot, (3.0 * math.cos(angle), 3.0 * math.sin(angle), stand + 12.0 + 8.0 * hash_unit(seed, index, 6))), seed * 31 + index,
            0.6 + 0.4 * hash_unit(seed, index, 7))


def stone_box(mesh, center, size, yaw=0.0, tone=1.0, seed=0, foot=1.0, top=1.0):
    """A box of stone, of full sizes along its own axes, turned and put at its center, with the UV of its masonry running along each face.
    Where it stands on other stone, or carries some, dirt has gathered in the corner between the two, as the developer asked on
    2026-11-11 ("наче грязь скопилась від дощу за роки"): its sides are foot times as light at their foot and top times at their top,
    back to their own tone GRIME_BAND further"""
    if foot != 1.0 or top != 1.0:
        banded_box(mesh, center, size, yaw, tone, seed, foot, top)
        return
    mesh.begin_part(flat=True)
    half = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
    shift = (hash_unit(seed, 1), hash_unit(seed, 2))
    # each face: its four corners, and which of the axes of the box run along it and up it
    faces = ((((-1, -1, -1), (-1, 1, -1), (-1, 1, 1), (-1, -1, 1)), 1, 2), (((1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)), 1, 2),
        (((-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)), 0, 2), (((-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1)), 0, 2),
        (((-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)), 0, 1), (((-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)), 0, 1))
    for index, (face, along, up) in enumerate(faces):
        shade = tone * (0.9 + 0.2 * hash_unit(seed, index, 9))
        corners = []
        for corner in face:
            local = (corner[0] * half[0], corner[1] * half[1], corner[2] * half[2])
            uv = (local[along] / TILE + shift[0], (center[2] + local[2] if up == 2 else local[up]) / TILE + shift[1])
            corners.append(mesh.vertex(add(center, rotated(local, yaw)), scale_color(STONE, shade), MASONRY, uv=uv))
            if up == 2:
                note_vertical(mesh, corners[-1], shift[1])
        mesh.quad(corners[0], corners[1], corners[2], corners[3], center)


def banded_box(mesh, center, size, yaw, tone, seed, foot, top):
    """A box of stone whose sides are in bands, darker at their foot and at their top"""
    mesh.begin_part(flat=True)
    half = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
    shift = (hash_unit(seed, 1), hash_unit(seed, 2))
    band = min(GRIME_BAND, size[2] * 0.45)
    levels = [(-half[2], foot)]
    if foot != 1.0:
        levels.append((-half[2] + band, 1.0))
    if top != 1.0:
        levels.append((half[2] - band, 1.0))
    levels.append((half[2], top))
    for index, (fixed, sign, along) in enumerate(((0, -1, 1), (0, 1, 1), (1, -1, 0), (1, 1, 0))):
        shade = tone * (0.9 + 0.2 * hash_unit(seed, index, 9))
        for (low, low_shade), (high, high_shade) in zip(levels, levels[1:]):
            corners = []
            for end, height, part in ((-1, low, low_shade), (1, low, low_shade), (1, high, high_shade), (-1, high, high_shade)):
                local = [0.0, 0.0, height]
                local[fixed] = sign * half[fixed]
                local[along] = end * half[along]
                corners.append(mesh.vertex(add(center, rotated(tuple(local), yaw)), scale_color(STONE, shade * part), MASONRY,
                    uv=(local[along] / TILE + shift[0], (center[2] + height) / TILE + shift[1])))
                note_vertical(mesh, corners[-1], shift[1])
            mesh.quad(corners[0], corners[1], corners[2], corners[3], center)
    for index, sign in ((4, -1), (5, 1)):
        shade = tone * (0.9 + 0.2 * hash_unit(seed, index, 9))
        corners = [mesh.vertex(add(center, rotated((x * half[0], y * half[1], sign * half[2]), yaw)), scale_color(STONE, shade), MASONRY,
            uv=(x * half[0] / TILE + shift[0], y * half[1] / TILE + shift[1])) for x, y in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
        mesh.quad(corners[0], corners[1], corners[2], corners[3], center)


def stone_cylinder(mesh, foot, radius, height, sides=10, top_radius=None, tone=1.0, seed=0, junction=None, grime=False):
    """A round tower of stone from a foot up, its masonry running around it. Up to a junction, a height over the ground, the wall of the
    castle runs into it along Y, and it is darker there, the more the nearer the wall, as the corner between the two is (JUNCTION_SHADE);
    the developer asked for darker corners on 2026-11-11"""
    top_radius = radius if top_radius is None else top_radius
    middle = add(foot, (0.0, 0.0, height * 0.5))
    mesh.begin_part()
    shares = [0.0, 1.0]
    if junction is not None and foot[2] + 1.0 < junction < foot[2] + height - JUNCTION_FADE - 1.0:
        shares = [0.0, (junction - foot[2]) / height, (junction + JUNCTION_FADE - foot[2]) / height, 1.0]
    # dirt under what it carries: its top GRIME_SHADE as light, back to its own tone GRIME_BAND lower
    under = None
    if grime and height > GRIME_BAND * 2.0:
        under = 1.0 - GRIME_BAND / height
        if under > shares[-2] + 0.01:
            shares.insert(len(shares) - 1, under)
    rings = []
    for share in shares:
        reach = radius + (top_radius - radius) * share
        ring = []
        for step in range(sides + 1):
            angle = 2.0 * math.pi * step / sides
            position = add(foot, (reach * math.cos(angle), reach * math.sin(angle), height * share))
            shade = 1.0 - JUNCTION_SHADE * math.sin(angle) ** 4 if junction is not None and position[2] <= junction + 0.01 else 1.0
            if under is not None and share >= 1.0:
                shade *= GRIME_SHADE
            ring.append(mesh.vertex(position, scale_color(STONE, tone * shade * (0.92 + 0.16 * hash_unit(seed, step % sides))), MASONRY,
                uv=(angle * radius / TILE, position[2] / TILE)))
            note_vertical(mesh, ring[-1], 0.0)
        rings.append(ring)
    for lower, upper in zip(rings, rings[1:]):
        for step in range(sides):
            mesh.quad(lower[step], lower[step + 1], upper[step + 1], upper[step], middle)
    mesh.begin_part(flat=True)
    top = add(foot, (0.0, 0.0, height))
    hub = mesh.vertex(top, scale_color(STONE, tone * 0.8), MASONRY, uv=(0.0, 0.0))
    cap = [mesh.vertex(add(top, (top_radius * math.cos(2.0 * math.pi * step / sides), top_radius * math.sin(2.0 * math.pi * step / sides), 0.0)),
        scale_color(STONE, tone * 0.8), MASONRY, uv=(top_radius * math.cos(2.0 * math.pi * step / sides) / TILE, top_radius * math.sin(2.0 * math.pi * step / sides) / TILE))
        for step in range(sides)]
    for step in range(sides):
        mesh.triangle(hub, cap[step], cap[(step + 1) % sides], middle)


def add_roof(mesh, center, size, yaw=0.0, seed=0, own=False):
    """A gabled roof over a box of a length and a width, in the color of the owner: its ridge runs along its length, at a height over its foot"""
    mesh.begin_part(flat=True)
    length, width, height = size
    half_l, half_w = length * 0.5 + 1.5, width * 0.5 + EAVES

    def at(point):
        return add(center, rotated(point, yaw))

    eaves = [at((-half_l, -half_w, 0.0)), at((half_l, -half_w, 0.0)), at((half_l, half_w, 0.0)), at((-half_l, half_w, 0.0))]
    ridge = [at((-half_l, 0.0, height)), at((half_l, 0.0, height))]
    inside = add(center, (0.0, 0.0, height * 0.3))
    shift = hash_unit(seed, 31) * ROOF_TILE
    for index, corners in enumerate(((eaves[0], eaves[1], ridge[1], ridge[0]), (eaves[3], eaves[2], ridge[1], ridge[0]))):
        quad = [roof_vertex(mesh, point, shift + (-half_l if corner in (0, 3) else half_l), center[2], 0.0 if corner < 2 else half_w,
            (0.85 + 0.25 * index) * (0.8 if corner < 2 else 1.12), own)
            for corner, point in enumerate(corners)]
        mesh.quad(quad[0], quad[1], quad[2], quad[3], inside)
    # its gables, over the walls of the house and not out with the eaves
    inner = [at((-half_l + 1.3, -width * 0.5, 0.0)), at((half_l - 1.3, -width * 0.5, 0.0)), at((half_l - 1.3, width * 0.5, 0.0)), at((-half_l + 1.3, width * 0.5, 0.0))]
    peak = height * (width * 0.5) / half_w
    tops = [at((-half_l + 1.3, 0.0, peak)), at((half_l - 1.3, 0.0, peak))]
    for corners in ((inner[0], inner[3], tops[0]), (inner[1], inner[2], tops[1])):
        triangle = [mesh.vertex(point, scale_color(STONE, 0.8 if index < 2 else 1.0), MASONRY, uv=(point[1] / TILE, point[2] / TILE)) for index, point in enumerate(corners)]
        for index in triangle:
            note_vertical(mesh, index, 0.0)
        mesh.triangle(triangle[0], triangle[1], triangle[2], inside)


def add_cone(mesh, foot, radius, height, seed=0, sides=10, own=False):
    """A pointed roof of a tower, in the color of the owner, its tiles running around it and up to its tip"""
    mesh.begin_part()
    tip = radius * 0.04
    rings = []
    for share, reach in ((0.0, radius), (1.0, tip)):
        ring = []
        for step in range(sides + 1):
            angle = 2.0 * math.pi * step / sides
            point = add(foot, (reach * math.cos(angle), reach * math.sin(angle), height * share))
            ring.append(roof_vertex(mesh, point, angle * radius, foot[2], radius - reach, (0.8 + 0.4 * hash_unit(seed, step % sides, 2)) * (0.78 + 0.4 * share), own))
        rings.append(ring)
    middle = add(foot, (0.0, 0.0, height * 0.4))
    for step in range(sides):
        mesh.quad(rings[0][step], rings[0][step + 1], rings[1][step + 1], rings[1][step], middle)


def add_house(mesh, center, size, yaw=0.0, roof=None, seed=0, door=True, windows=True, own=False):
    """A house of stone under a gabled roof, from under the ground, with a dark door in its front, towards +X, and lit windows along its sides"""
    length, width, height = size
    stone_box(mesh, add(center, (0.0, 0.0, (height - SKIRT) * 0.5)), (length, width, height + SKIRT), yaw=yaw, tone=1.08, seed=seed, top=GRIME_SHADE + 0.1)
    stone_box(mesh, add(center, (0.0, 0.0, (2.2 - SKIRT) * 0.5)), (length + 1.6, width + 1.6, 2.2 + SKIRT), yaw=yaw, tone=PLINTH_TONE, seed=seed + 3)
    add_roof(mesh, add(center, (0.0, 0.0, height)), (length, width, roof if roof else width * 0.55), yaw, seed=seed, own=own)
    if door:
        add_box(mesh, add(center, rotated((length * 0.5 + 0.2, 0.0, height * 0.28), yaw)), (0.8, min(width * 0.3, 6.0), height * 0.56), DARK, yaw=yaw, seed=seed + 1)
    if windows:
        count = max(int(length / 11.0), 1)
        for side in (-1.0, 1.0):
            for index in range(count):
                along = -length * 0.5 + length * (index + 0.5) / count
                add_box(mesh, add(center, rotated((along, side * (width * 0.5 + 0.2), height * 0.62), yaw)), (2.6, 0.8, height * 0.24), WINDOW, GLOW, yaw=yaw, seed=seed + 5)


def add_tower(mesh, foot, radius, height, roof=0.0, seed=0, sides=10, slits=3, flare=0.0, own=False, junction=None):
    """A round tower from under the ground, with a crown of merlons, which stands out by a flare on corbels, or under a pointed roof in the
    color of the owner, and lit slits high in it"""
    stone_cylinder(mesh, add(foot, (0.0, 0.0, -SKIRT)), radius, height + SKIRT, sides, seed=seed, junction=junction, grime=roof <= 0.0)
    # a plinth that spreads at its foot, and a course that stands out two thirds up it
    stone_cylinder(mesh, add(foot, (0.0, 0.0, -SKIRT)), radius * 1.16 + 0.8, SKIRT + max(height * 0.1, 3.0), sides, top_radius=radius + 0.2, tone=PLINTH_TONE, seed=seed + 4)
    if radius >= 6.0:
        stone_cylinder(mesh, add(foot, (0.0, 0.0, height * 0.6)), radius + 0.9, 2.0, sides, tone=TRIM_TONE, seed=seed + 5)
    for slit in range(slits):
        angle = 2.0 * math.pi * (slit + 0.25 + 0.5 * hash_unit(seed, slit, 4)) / slits
        add_box(mesh, add(foot, (radius * 0.97 * math.cos(angle), radius * 0.97 * math.sin(angle), height * 0.72)), (1.2, 2.0, min(height * 0.1, 8.0)), WINDOW, GLOW, yaw=angle,
            seed=seed + 7)
    if roof > 0.0:
        add_cone(mesh, add(foot, (0.0, 0.0, height)), radius * 1.25, roof, seed + 1, sides, own)
        return
    crown = radius * 1.15
    if flare > 0.0:
        # the corbels under a crown that stands out
        stone_cylinder(mesh, add(foot, (0.0, 0.0, height)), radius, flare * 2.4, sides, top_radius=radius + flare, tone=0.85, seed=seed + 2)
        height += flare * 2.4
        crown = radius + flare + 0.6
    stone_cylinder(mesh, add(foot, (0.0, 0.0, height)), crown, 2.5, sides, tone=TRIM_TONE, seed=seed + 1)
    reach = crown - radius * 0.13
    size = max(radius / 9.0, 0.8)
    for step in range(sides):
        if step % 2 == 0:
            angle = 2.0 * math.pi * (step + 0.5) / sides
            stone_box(mesh, add(foot, (reach * math.cos(angle), reach * math.sin(angle), height + 2.5 + 2.0 * size)), (2.6 * size, 3.4 * size, 4.0 * size), yaw=angle,
                tone=TRIM_TONE, seed=seed + step, foot=GRIME_SHADE)


def add_flag(mesh, foot, height, alpha, seed=0, size=16.0):
    """A pole from under the ground with a swallow-tailed flag that takes the color of the owner and waves, more the further from the pole"""
    add_cylinder(mesh, add(foot, (0.0, 0.0, -SKIRT * 0.5)), add(foot, (0.0, 0.0, height)), 0.9, 5, TIMBER, 0.0, TIMBER, seed)
    mesh.begin_part()
    top = foot[2] + height - 1.0
    drop = size * 0.62

    def cloth(along, down):
        return mesh.vertex((foot[0] - size * along, foot[1], top - drop * down - size * 0.06 * along), scale_color(TINTED, 1.0 - 0.2 * along + 0.1 * down), alpha,
            uv=(along, CLOTH_V + size * 0.01), normal=(0.0, 1.0, 0.25))

    columns = [(cloth(along, 0.0), cloth(along, 1.0)) for along in (0.0, 0.3, 0.6)]
    for (upper, lower), (next_upper, next_lower) in zip(columns, columns[1:]):
        mesh.sheet(upper, lower, next_lower)
        mesh.sheet(upper, next_lower, next_upper)
    # its two tails
    notch = cloth(0.72, 0.5)
    mesh.sheet(columns[-1][0], notch, cloth(1.0, 0.08))
    mesh.sheet(columns[-1][1], notch, cloth(1.0, 0.92))
    mesh.sheet(columns[-1][0], columns[-1][1], notch)


def wall(level):
    """A stretch of the wall of a castle, WALL_LENGTH along Y, its outer face towards +X: a fence, a palisade, a stone wall as high as a
    rider, a higher one"""
    mesh = Mesh()
    half = WALL_LENGTH * 0.5
    if level == 0:
        for side in (-1.0, 1.0):
            add_cylinder(mesh, (0.0, side * half * 0.5, -SKIRT * 0.5), (0.0, side * half * 0.5, 24.0), 1.6, 5, TIMBER, 0.0, TIMBER, 1)
        for height in (6.0, 13.0, 20.0):
            add_box(mesh, (0.0, 0.0, height), (1.2, WALL_LENGTH, 1.8), TIMBER, seed=2)
        return mesh
    if level == 1:
        stakes = 7
        for index in range(stakes):
            y = -half + WALL_LENGTH * (index + 0.5) / stakes
            height = 42.0 + 8.0 * hash_unit(3, index)
            add_cylinder(mesh, (0.0, y, -SKIRT), (0.0, y, height), 2.6, 6, TIMBER, 0.0, TIMBER, 4 + index)
            add_cylinder(mesh, (0.0, y, height), (0.0, y, height + 7.0), 2.6, 6, TIMBER, 0.0, TIMBER, 20 + index, taper=0.1)
        for height in (14.0, 32.0):
            add_box(mesh, (-2.4, 0.0, height), (1.4, WALL_LENGTH, 2.4), TIMBER, seed=5)
        return mesh
    height = 62.0 if level == 2 else 78.0
    thick = 11.0 if level == 2 else 13.0
    stone_box(mesh, (0.0, 0.0, (height - SKIRT) * 0.5), (thick, WALL_LENGTH, height + SKIRT), seed=6, top=GRIME_SHADE + 0.15)
    # the merlons along its outer edge, and the walk behind them
    merlons = 3
    for index in range(merlons):
        y = -half + WALL_LENGTH * (index + 0.5) / merlons
        stone_box(mesh, (thick * 0.5 - 1.8, y, height + 3.5), (3.6, WALL_LENGTH / merlons * 0.55, 7.0), tone=TRIM_TONE, seed=7 + index, foot=GRIME_SHADE)
    # a plinth along its foot on both sides, and a buttress against its outer face
    stone_box(mesh, (0.0, 0.0, (7.0 - SKIRT) * 0.5), (thick + 4.0, WALL_LENGTH, 7.0 + SKIRT), tone=PLINTH_TONE, seed=14)
    stone_box(mesh, (thick * 0.5 + 2.0, 0.0, (height * 0.62 - SKIRT) * 0.5), (4.0, 5.5, height * 0.62 + SKIRT), tone=0.95, seed=15, top=GRIME_SHADE + 0.1)
    stone_box(mesh, (thick * 0.5 + 1.2, 0.0, height * 0.62 + 1.5), (2.4, 5.5, 3.0), tone=TRIM_TONE, seed=16)
    if level == 3:
        # a course that stands out under the merlons
        stone_box(mesh, (thick * 0.5 + 0.6, 0.0, height - 3.0), (2.0, WALL_LENGTH, 4.0), tone=TRIM_TONE, seed=10)
    return mesh


def tower(level):
    """A tower in the wall of a castle, with a crown of merlons and a pennant of the owner on it: at the highest level slimmer and higher,
    its crown standing out on corbels. It had a pointed roof there until the developer found a castle had too many roofs on 2026-11-11"""
    mesh = Mesh()
    if level == 2:
        add_tower(mesh, (0.0, 0.0, 0.0), 14.0, 96.0, seed=11, sides=12, junction=62.0)
        add_flag(mesh, (0.0, 0.0, 96.0), 30.0, ROOF, 13, size=15.0)
    else:
        add_tower(mesh, (0.0, 0.0, 0.0), 12.0, 112.0, seed=12, sides=12, flare=4.0, junction=78.0)
        add_flag(mesh, (0.0, 0.0, 121.0), 30.0, ROOF, 14, size=17.0)
    return mesh


def gate(level):
    """A gate of a castle, across Y with its front towards +X: posts, a gate of timber, a gatehouse of stone, a larger one under roofs. Its
    two leaves swing into the yard, and a brazier burns on each side of the way in"""
    mesh = Mesh()
    if level == 0:
        for side in (-1.0, 1.0):
            add_cylinder(mesh, (0.0, side * 24.0, -SKIRT * 0.5), (0.0, side * 24.0, 44.0), 2.2, 6, TIMBER, 0.0, TIMBER, 21)
            # the fence goes on to the posts of the gate
            add_cylinder(mesh, (0.0, side * 35.0, -SKIRT * 0.5), (0.0, side * 35.0, 24.0), 1.6, 5, TIMBER, 0.0, TIMBER, 20)
            for height in (6.0, 13.0, 20.0):
                add_box(mesh, (0.0, side * 30.0, height), (1.2, 12.0, 1.8), TIMBER, seed=2)
            add_rail_leaf(mesh, side * 22.5, -side, 22.0, 3.0, 24.0, seed=30)
            add_brazier(mesh, (16.0, side * 30.0, 0.0), 33 + int(side), stand=16.0)
        add_box(mesh, (0.0, 0.0, 42.0), (1.8, 50.0, 2.4), TIMBER, seed=22)
        return mesh
    if level == 1:
        for side in (-1.0, 1.0):
            add_box(mesh, (0.0, side * 30.0, 38.0 - SKIRT * 0.5), (13.0, 13.0, 76.0 + SKIRT), TIMBER, seed=23)
            add_roof(mesh, (0.0, side * 30.0, 76.0), (13.0, 13.0, 12.0), seed=24)
            add_leaf(mesh, side * 23.0, -side, 22.6, -6.0, 60.0, seed=40)
            add_brazier(mesh, (20.0, side * 30.0, 0.0), 43 + int(side))
        add_box(mesh, (0.0, 0.0, 66.0), (7.0, 50.0, 8.0), TIMBER, seed=25)
        return mesh
    height = 104.0 if level == 2 else 124.0
    radius = 13.0 if level == 2 else 14.5
    way = 80.0 if level == 2 else 88.0
    for side in (-1.0, 1.0):
        add_tower(mesh, (0.0, side * 30.0, 0.0), radius, height, seed=26, sides=12, flare=3.5 if level == 3 else 0.0, junction=height * 0.88)
        add_leaf(mesh, side * (30.0 - radius), -side, 30.0 - radius - 0.4, -6.0, way - 1.0, seed=50)
        add_brazier(mesh, (radius + 12.0, side * 26.0, 0.0), 53 + int(side))
    # the house over the way in
    top = height * 0.88
    stone_box(mesh, (0.0, 0.0, (way + top) * 0.5), (15.0, 40.0, top - way), seed=27, foot=GRIME_SHADE + 0.1, top=GRIME_SHADE + 0.15)
    for side in (-1.0, 1.0):
        add_box(mesh, (7.7, side * 6.0, (way + top) * 0.5), (0.8, 2.2, 7.0), WINDOW, GLOW, seed=28)
    if level == 3:
        # its ridge along the wall: the length of a roof runs along its own X, which the turn lays along Y (it lay across until 2026-11-11)
        add_roof(mesh, (0.0, 0.0, top), (40.0, 15.0, 14.0), yaw=math.pi * 0.5, seed=29)
    else:
        for index in range(3):
            stone_box(mesh, (5.8, -11.0 + index * 11.0, top + 3.5), (3.4, 6.0, 7.0), tone=TRIM_TONE, seed=60 + index, foot=GRIME_SHADE)
    return mesh


def yard():
    """The place of the hero that guards a castle, in its yard: a low paved round with a ring of stone posts, from under the ground"""
    mesh = Mesh()
    stone_cylinder(mesh, (0.0, 0.0, -SKIRT), 46.0, SKIRT + 4.0, sides=24, top_radius=41.0, tone=0.85, seed=33)
    stone_cylinder(mesh, (0.0, 0.0, 4.0), 38.0, 3.0, sides=24, top_radius=35.0, tone=1.1, seed=34)
    for index in range(6):
        angle = 2.0 * math.pi * (index + 0.5) / 6
        stone_box(mesh, (39.5 * math.cos(angle), 39.5 * math.sin(angle), 9.0), (4.5, 4.5, 12.0), yaw=angle, tone=0.8, seed=35 + index)
    return mesh


def flag():
    """The flag of the owner of a castle, over its walls"""
    mesh = Mesh()
    add_flag(mesh, (0.0, 0.0, 0.0), 150.0, OWNER, 31, size=46.0)
    return mesh


def hall(level):
    """The hall at the back of the yard of a castle: a house, a larger one with a tower, a great hall with a spire"""
    mesh = Mesh()
    if level == 1:
        add_house(mesh, (0.0, 0.0, 0.0), (32.0, 22.0, 15.0), seed=41, own=True)
        stone_box(mesh, (-8.0, 7.0, 22.0), (4.0, 4.0, 10.0), tone=0.7, seed=42)
        stretched(mesh)
        add_brazier(mesh, (22.0, 9.0, 0.0), 51, stand=16.0)
        return mesh
    if level == 2:
        add_house(mesh, (0.0, 0.0, 0.0), (42.0, 26.0, 22.0), seed=43, own=True)
        add_house(mesh, (4.0, -18.0, 0.0), (20.0, 16.0, 14.0), yaw=math.pi * 0.5, seed=44, door=False, own=True)
        add_tower(mesh, (-22.0, 14.0, 0.0), 7.0, 34.0, roof=14.0, seed=45, own=True)
        stretched(mesh)
        add_flag(mesh, (-22.0, 14.0, 48.0 * HEIGHT - 4.0), 24.0, ROOF, 52, size=13.0)
        for side in (-1.0, 1.0):
            add_brazier(mesh, (27.0, side * 9.0, 0.0), 53 + int(side))
        return mesh
    add_house(mesh, (0.0, 0.0, 0.0), (52.0, 30.0, 28.0), roof=20.0, seed=46, own=True)
    add_house(mesh, (0.0, 0.0, 0.0), (26.0, 46.0, 24.0), roof=14.0, seed=47, door=False, own=True)
    add_tower(mesh, (-28.0, 0.0, 0.0), 9.0, 52.0, roof=26.0, seed=48, own=True)
    for side in (-1.0, 1.0):
        add_tower(mesh, (26.0, side * 17.0, 0.0), 3.8, 36.0, seed=49, sides=8, slits=1)
    add_cylinder(mesh, (-28.0, 0.0, 78.0), (-28.0, 0.0, 84.0), 2.2, 6, GOLD, GLOW, GOLD, 50)
    stretched(mesh)
    for side in (-1.0, 1.0):
        add_flag(mesh, (26.0, side * 17.0, 38.0 * HEIGHT), 22.0, ROOF, 56 + int(side), size=12.0)
        add_brazier(mesh, (36.0, side * 7.0, 0.0), 58 + int(side))
    return mesh


def dwelling(level):
    """The dwelling of the creatures of a level: each a shape of its own, larger the higher the level"""
    mesh = Mesh()
    if level == 1:
        # a guardhouse: a long low house with a rack of spears before it
        add_house(mesh, (-4.0, 0.0, 0.0), (22.0, 34.0, 11.0), yaw=math.pi * 0.5, seed=61)
        for index in range(5):
            add_cylinder(mesh, (16.0, -10.0 + index * 5.0, -8.0), (18.5, -10.0 + index * 5.0, 17.0), 0.5, 4, TIMBER, 0.0, STONE, 62)
        add_box(mesh, (17.0, 0.0, 8.0), (1.2, 24.0, 1.2), TIMBER, seed=63)
    elif level == 2:
        # a lodge with a lookout and a butt to shoot at
        add_house(mesh, (-6.0, -6.0, 0.0), (22.0, 20.0, 13.0), seed=64)
        add_cylinder(mesh, (-8.0, 14.0, -SKIRT * 0.5), (-8.0, 14.0, 30.0), 2.2, 6, TIMBER, 0.0, TIMBER, 65)
        add_box(mesh, (-8.0, 14.0, 31.0), (12.0, 12.0, 1.6), TIMBER, seed=66)
        add_roof(mesh, (-8.0, 14.0, 40.0), (11.0, 11.0, 6.0), seed=67)
        add_cylinder(mesh, (18.0, 12.0, 8.0), (19.0, 12.0, 8.0), 6.5, 10, TINTED, 0.0, (0.75, 0.15, 0.1), 68)
        add_cylinder(mesh, (18.5, 12.0, -8.0), (18.5, 12.0, 8.0), 0.8, 4, TIMBER, 0.0, TIMBER, 69)
    elif level == 3:
        # an aerie: a tall slim tower with a nest on a platform
        add_tower(mesh, (0.0, 0.0, 0.0), 8.0, 56.0, seed=70)
        add_cylinder(mesh, (0.0, 0.0, 56.0), (0.0, 0.0, 58.5), 15.0, 10, TIMBER, 0.0, TIMBER, 71)
        add_blob(mesh, (0.0, 0.0, 60.5), (11.0, 11.0, 4.0), (0.45, 0.36, 0.2), 0.0, 72, 1, bumps=0.3, flatten=0.4, shade=0.3, flat=True)
        add_blob(mesh, (0.0, 0.0, 62.5), (4.0, 4.5, 3.0), TINTED, ROOF, 73, 1, bumps=0.1, flatten=0.6, shade=0.2)
    elif level == 4:
        # a hall of arms: a square hall with two banners before its door
        add_house(mesh, (-4.0, 0.0, 0.0), (30.0, 28.0, 20.0), roof=16.0, seed=74)
        for side in (-1.0, 1.0):
            add_flag(mesh, (16.0, side * 10.0, 0.0), 34.0, ROOF, 75, size=11.0)
        stone_box(mesh, (13.5, 0.0, 2.0 - SKIRT * 0.25), (6.0, 14.0, 4.0 + SKIRT * 0.5), seed=76)
    elif level == 5:
        # a cloister: a chapel with a spire, and a walled yard
        add_house(mesh, (-4.0, 0.0, 0.0), (34.0, 18.0, 20.0), roof=14.0, seed=77)
        add_tower(mesh, (-24.0, 0.0, 0.0), 6.5, 44.0, roof=24.0, seed=78)
        stone_box(mesh, (10.0, 18.0, 4.0 - SKIRT * 0.25), (26.0, 2.0, 10.0 + SKIRT * 0.5), seed=79)
        stone_box(mesh, (22.0, 9.0, 4.0 - SKIRT * 0.25), (2.0, 18.0, 10.0 + SKIRT * 0.5), seed=80)
    elif level == 6:
        # stables: a long house with open bays, and a fenced yard
        add_house(mesh, (-10.0, 0.0, 0.0), (46.0, 22.0, 15.0), yaw=math.pi * 0.5, seed=81, door=False, windows=False)
        for index in range(4):
            add_box(mesh, (1.3, -17.0 + index * 11.5, 5.0), (0.8, 8.0, 10.0), DARK, seed=82)
        for index in range(5):
            add_cylinder(mesh, (22.0, -20.0 + index * 10.0, -SKIRT * 0.5), (22.0, -20.0 + index * 10.0, 8.0), 0.8, 4, TIMBER, 0.0, TIMBER, 83)
        add_box(mesh, (22.0, 0.0, 6.0), (1.0, 42.0, 1.0), TIMBER, seed=84)
        add_box(mesh, (22.0, 0.0, 3.0), (1.0, 42.0, 1.0), TIMBER, seed=85)
    else:
        # a great gate: two pillars and an arch, and a light between them
        for side in (-1.0, 1.0):
            stone_box(mesh, (0.0, side * 19.0, 26.0 - SKIRT * 0.5), (11.0, 9.0, 64.0 + SKIRT), seed=86)
            add_tower(mesh, (0.0, side * 30.0, 0.0), 4.5, 42.0, seed=87, sides=8, slits=1)
        stone_box(mesh, (0.0, 0.0, 61.0), (12.0, 46.0, 9.0), seed=88)
        add_roof(mesh, (0.0, 0.0, 65.5), (46.0, 12.0, 10.0), yaw=math.pi * 0.5, seed=89)
        add_blob(mesh, (0.0, 0.0, 30.0), (1.5, 13.0, 26.0), (0.16, 0.2, 0.3), GLOW, 90, 2, bumps=0.05, flatten=1.0, shade=0.1)
    return stretched(mesh)


def upgrade():
    """The mark beside a building that was built further than its mesh shows: a tall pole with a pennant of the owner and a gilded top"""
    mesh = Mesh()
    add_flag(mesh, (14.0, -15.0, 0.0), 84.0, ROOF, 95, size=17.0)
    add_blob(mesh, (14.0, -15.0, 86.0), (2.6, 2.6, 2.6), scale_color(GOLD, 0.4), GLOW, 96, 1, bumps=0.0, flatten=1.0, shade=0.2)
    return mesh


MESHES = [('Flag', flag), ('Yard', yard), ('Upgrade', upgrade)] + [('Wall%d' % level, lambda level=level: wall(level)) for level in range(4)] + [
    ('Tower%d' % level, lambda level=level: tower(level)) for level in (2, 3)] + [('Gate%d' % level, lambda level=level: gate(level)) for level in range(4)] + [
    ('Hall%d' % level, lambda level=level: hall(level)) for level in (1, 2, 3)] + [('Dwelling%d' % level, lambda level=level: dwelling(level)) for level in range(1, 8)]
# the meshes of the castle of one piece that came before, which the import deletes
OLD_MESHES = ['Base', 'Fort1', 'Fort2', 'Fort3']


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    manifest = {'meshes': [], 'old': ['SM_PlanetCastle' + name for name in OLD_MESHES]}
    for name, make in MESHES:
        asset = 'SM_PlanetCastle' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, True)
        manifest['meshes'].append({'name': asset, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'castles.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=1)
    print('written to', OUTPUT_FOLDER)


if __name__ == '__main__':
    main()
