"""Generates the meshes of the mines, piles and mills of the hex planet (UHexSites).

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_sites.py
Writes GLB files and sites.json to Saved/HexPlanetSites, then run import_planet_sites.py with the commandlet, after create_planet_assets.py
(or its argument heroes) made M_HexPlanetSite. The GLB files open in Blender, so a mesh can be reworked by hand; all are placeholders for the
meshes of an artist: MineMesh, PileMesh and MillMesh of DA_HexSites, or Mesh of a kind there.

A mesh stands on its origin, X forward and Z up, in world units, and reaches a little under it, so it does not hang where the ground falls away.
The color of a vertex is its color, and its alpha what the material does with it (M_HexPlanetSite): 0 nothing, 0.3 (GLOW) it glows a little in
its own color, as what a pile holds, 0.5 (RESOURCE) it takes the color of the resource, as the heap of a mine and the sacks of a mill, 0.75
(SPARK) it is a sparkle, and 1 (OWNER) it takes the color of the owner, as the flag of a mine.

One mine serves every resource: a mound of rock with a timbered mouth, a heap of what it digs beside it and a flag. A pile is a mesh of its
own for each resource, named after its kind in DA_HexSites, and many things at once, as the developer asked on 2026-11-11, so nobody takes it
for a bush or a lone stone of the decor: a chest and heaps of coins, a stack of logs, a cart of ore, flasks of mercury, sacks and lumps of
sulfur, a cluster of crystals, a chest of gems. Each lies on a patch of bare earth, and sparkles twinkle and rise around it: small stars of a
few triangles, whose red is when each twinkles, green how fast and blue how far it rises, and whose UV is where a corner lies in the star,
which the material turns to the camera.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'HexPlanetObjects')))
from generate_planet_objects import Mesh, add, add_blob, add_tube, cross, frame_of, hash_unit, lerp, mul, scale_color, sub, unit, write_glb  # noqa: E402

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetSites'))

ROCK = (0.2, 0.18, 0.16)
DARK = (0.012, 0.011, 0.01)
WOOD = (0.22, 0.14, 0.08)
WOOD_CUT = (0.62, 0.44, 0.24)
PLANK = (0.3, 0.2, 0.11)
IRON = (0.07, 0.07, 0.08)
EARTH = (0.13, 0.095, 0.06)
CLOTH = (0.42, 0.33, 0.2)
GOLD = (0.95, 0.6, 0.08)
GOLD_DEEP = (0.6, 0.33, 0.04)
ORE = (0.15, 0.16, 0.2)
ORE_RUST = (0.42, 0.17, 0.07)
ORE_BRIGHT = (0.55, 0.56, 0.6)
SILVER = (0.8, 0.83, 0.88)
GLASS = (0.3, 0.5, 0.52)
SULFUR = (0.95, 0.8, 0.07)
SULFUR_DEEP = (0.75, 0.55, 0.03)
CRYSTAL_ROOT = (0.3, 0.015, 0.03)
CRYSTAL_TIP = (1.0, 0.12, 0.17)
GEMS = ((0.06, 0.75, 0.3), (0.06, 0.75, 0.3), (0.1, 0.9, 0.45), (0.1, 0.35, 0.95), (0.6, 0.12, 0.85), (0.9, 0.9, 0.95))
# what the material tints is nearly white, so the color of the resource or the owner shows as it is
TINTED = (0.9, 0.9, 0.9)
GLOW = 0.3
RESOURCE = 0.5
SPARK = 0.75
OWNER = 1.0
# how far a mesh reaches under its origin
SKIRT = 9.0


def rotated(point, yaw=0.0, pitch=0.0, roll=0.0):
    """A point turned around X by roll, then around Y by pitch, then around Z by yaw"""
    x, y, z = point
    y, z = y * math.cos(roll) - z * math.sin(roll), y * math.sin(roll) + z * math.cos(roll)
    x, z = x * math.cos(pitch) + z * math.sin(pitch), -x * math.sin(pitch) + z * math.cos(pitch)
    x, y = x * math.cos(yaw) - y * math.sin(yaw), x * math.sin(yaw) + y * math.cos(yaw)
    return (x, y, z)


def add_box(mesh, center, size, color, alpha=0.0, yaw=0.0, pitch=0.0, roll=0.0, seed=0):
    """A box of full sizes along its own axes, turned and put at its center, each face its own tone"""
    mesh.begin_part(flat=True)
    half = (size[0] * 0.5, size[1] * 0.5, size[2] * 0.5)
    faces = (((-1, -1, -1), (-1, 1, -1), (-1, 1, 1), (-1, -1, 1)), ((1, -1, -1), (1, 1, -1), (1, 1, 1), (1, -1, 1)),
        ((-1, -1, -1), (1, -1, -1), (1, -1, 1), (-1, -1, 1)), ((-1, 1, -1), (1, 1, -1), (1, 1, 1), (-1, 1, 1)),
        ((-1, -1, -1), (1, -1, -1), (1, 1, -1), (-1, 1, -1)), ((-1, -1, 1), (1, -1, 1), (1, 1, 1), (-1, 1, 1)))
    for index, face in enumerate(faces):
        tone = 0.82 + 0.3 * hash_unit(seed, index, 9)
        corners = [mesh.vertex(add(center, rotated((corner[0] * half[0], corner[1] * half[1], corner[2] * half[2]), yaw, pitch, roll)),
            scale_color(color, tone), alpha) for corner in face]
        mesh.quad(corners[0], corners[1], corners[2], corners[3], center)


def add_cylinder(mesh, start, end, radius, sides, color, alpha=0.0, cap_color=None, seed=0, taper=1.0):
    """A round bar from a point to another, smooth around and flat at both ends: a log, a coin, a wheel"""
    axis, side, across = frame_of(sub(end, start))
    middle = mul(add(start, end), 0.5)
    cap_color = cap_color or color
    mesh.begin_part()
    rings = []
    for center, reach in ((start, radius), (end, radius * taper)):
        ring = []
        for step in range(sides):
            angle = 2.0 * math.pi * step / sides
            position = add(center, add(mul(side, math.cos(angle) * reach), mul(across, math.sin(angle) * reach)))
            ring.append(mesh.vertex(position, scale_color(color, 0.8 + 0.4 * hash_unit(seed, step, 2)), alpha))
        rings.append(ring)
    for step in range(sides):
        following = (step + 1) % sides
        mesh.quad(rings[0][step], rings[0][following], rings[1][following], rings[1][step], middle)
    mesh.begin_part(flat=True)
    for (center, reach), tone in zip(((start, radius), (end, radius * taper)), (0.85, 1.0)):
        hub = mesh.vertex(center, scale_color(cap_color, tone), alpha)
        ring = []
        for step in range(sides):
            angle = 2.0 * math.pi * step / sides
            ring.append(mesh.vertex(add(center, add(mul(side, math.cos(angle) * reach), mul(across, math.sin(angle) * reach))), scale_color(cap_color, tone), alpha))
        for step in range(sides):
            mesh.triangle(hub, ring[step], ring[(step + 1) % sides], middle)


def add_crystal(mesh, base, direction, long, radius, root, tip_color, alpha, seed):
    """A six sided crystal with a pointed tip, from a little under its base along a direction, lighter towards its tip"""
    mesh.begin_part(flat=True)
    axis, side, across = frame_of(direction)
    twist = hash_unit(seed, 1) * math.pi
    rings = []
    for share, color in ((-0.12, root), (0.74, lerp(root, tip_color, 0.75))):
        center = add(base, mul(axis, long * share))
        ring = []
        for step in range(6):
            angle = twist + 2.0 * math.pi * step / 6
            offset = add(mul(side, math.cos(angle) * radius), mul(across, math.sin(angle) * radius))
            ring.append(mesh.vertex(add(center, offset), scale_color(color, 0.8 + 0.4 * hash_unit(seed, step, 3)), alpha))
        rings.append((ring, center))
    tip = mesh.vertex(add(base, mul(axis, long)), tip_color, alpha)
    (lower, lower_center), (upper, upper_center) = rings
    middle = mul(add(lower_center, upper_center), 0.5)
    for step in range(6):
        following = (step + 1) % 6
        mesh.quad(lower[step], lower[following], upper[following], upper[step], middle)
        mesh.triangle(upper[step], upper[following], tip, upper_center)


def add_gem(mesh, center, radius, color, alpha, seed):
    """A cut gem: a flat table over a crown, a girdle and a pointed pavilion, lying any way"""
    mesh.begin_part(flat=True)
    yaw, pitch, roll = hash_unit(seed, 1) * 6.2832, (hash_unit(seed, 2) - 0.5) * 1.6, (hash_unit(seed, 3) - 0.5) * 1.6

    def at(point):
        return add(center, rotated(mul(point, radius), yaw, pitch, roll))

    sides = 6
    girdle = [mesh.vertex(at((math.cos(2.0 * math.pi * step / sides), math.sin(2.0 * math.pi * step / sides), 0.0)),
        scale_color(color, 0.7 + 0.5 * hash_unit(seed, step, 4)), alpha) for step in range(sides)]
    crown = [mesh.vertex(at((0.58 * math.cos(2.0 * math.pi * (step + 0.5) / sides), 0.58 * math.sin(2.0 * math.pi * (step + 0.5) / sides), 0.42)),
        scale_color(color, 1.0 + 0.3 * hash_unit(seed, step, 5)), alpha) for step in range(sides)]
    table = mesh.vertex(at((0.0, 0.0, 0.42)), scale_color(color, 1.3), alpha)
    culet = mesh.vertex(at((0.0, 0.0, -0.75)), scale_color(color, 0.6), alpha)
    for step in range(sides):
        following = (step + 1) % sides
        mesh.triangle(girdle[step], girdle[following], crown[step], center)
        mesh.triangle(crown[step], girdle[following], crown[following], center)
        mesh.triangle(crown[step], crown[following], table, center)
        mesh.triangle(girdle[step], girdle[following], culet, center)


def add_sparkle(mesh, position, seed, rise=1.0):
    """A sparkle: a star of four long points, drawn tiny where it starts, which the material grows, turns to the camera, twinkles and lifts.
    Its red is when it twinkles, its green how fast and its blue how far it rises; its UV is where each corner lies in the star"""
    mesh.begin_part()
    color = (hash_unit(seed, 1), hash_unit(seed, 2), rise)
    tiny = 0.3

    def corner(u, v):
        return mesh.vertex((position[0] + u * tiny, position[1], position[2] + v * tiny), color, SPARK, uv=(0.5 + u * 0.5, 0.5 + v * 0.5), normal=(0.0, 0.0, 1.0))

    hub = corner(0.0, 0.0)
    points = []
    for step in range(8):
        angle = math.pi * step / 4.0
        reach = 1.0 if step % 2 == 0 else 0.2
        points.append(corner(math.cos(angle) * reach, math.sin(angle) * reach))
    for step in range(8):
        mesh.sheet(hub, points[step], points[(step + 1) % 8])


def add_sparkles(mesh, count, reach, height, seed, top):
    """Sparkles around a pile, out to a reach and up to a height, and one that stays at the top, so the bounds of the mesh hold them as they rise"""
    for index in range(count):
        angle = 2.0 * math.pi * (index + hash_unit(seed, index, 1)) / count
        out = reach * (0.25 + 0.75 * hash_unit(seed, index, 2))
        add_sparkle(mesh, (out * math.cos(angle), out * math.sin(angle), height[0] + (height[1] - height[0]) * hash_unit(seed, index, 3)), seed * 53 + index,
            0.35 + 0.65 * hash_unit(seed, index, 4))
    add_sparkle(mesh, (0.0, 0.0, top), seed * 53 + count, 0.0)


def add_earth(mesh, reach, seed):
    """The patch of bare earth a pile lies on, which goes down under the ground"""
    add_blob(mesh, (0.0, 0.0, -SKIRT * 0.5), (reach, reach * 0.94, SKIRT * 0.5 + 1.6), EARTH, 0.0, seed, 2, bumps=0.12, flatten=1.0, shade=0.3)


def timber(mesh, start, end, radius, seed):
    add_tube(mesh, [start, end], [radius, radius], WOOD, [0.0, 0.0], seed, 6, True, bark=0.12)


def chunks(mesh, center, count, reach, sizes, seed, color=TINTED, alpha=RESOURCE):
    """A heap of chunks of a resource around a point, the larger low and in the middle"""
    for index in range(count):
        angle = hash_unit(seed, index, 1) * 6.2832
        out = reach * (hash_unit(seed, index, 2) ** 0.7) * (0.25 + 0.75 * index / max(count - 1, 1))
        size = sizes[1] - (sizes[1] - sizes[0]) * index / max(count - 1, 1)
        position = (center[0] + out * math.cos(angle), center[1] + out * math.sin(angle), center[2] + size * 0.55)
        add_blob(mesh, position, (size, size * (0.8 + 0.4 * hash_unit(seed, index, 3)), size * (0.75 + 0.4 * hash_unit(seed, index, 4))), color, alpha,
            seed * 31 + index, 1, bumps=0.3, flatten=0.7, shade=0.45, flat=True)


def add_chest(mesh, center, yaw, seed, size=(17.0, 11.5, 8.0)):
    """An open chest of planks with iron bands, its lid thrown back; what it holds is heaped over it by the caller"""
    def at(point):
        return add(center, rotated(point, yaw))

    length, width, height = size
    wall = 1.2
    add_box(mesh, at((0.0, 0.0, wall * 0.5)), (length, width, wall), PLANK, yaw=yaw, seed=seed)
    for side in (-1.0, 1.0):
        add_box(mesh, at((0.0, side * (width - wall) * 0.5, height * 0.5)), (length, wall, height), PLANK, yaw=yaw, seed=seed + 1)
        add_box(mesh, at((side * (length - wall) * 0.5, 0.0, height * 0.5)), (wall, width, height), PLANK, yaw=yaw, seed=seed + 2)
        # an iron band around each end
        add_box(mesh, at((side * length * 0.3, 0.0, height * 0.5)), (1.6, width + 0.8, height + 0.6), IRON, yaw=yaw, seed=seed + 3)
    # the lid, thrown back on its hinge along the far side
    add_box(mesh, at((0.0, width * 0.5 + 2.6, height + 3.4)), (length, 1.4, width * 0.92), PLANK, yaw=yaw, roll=-0.5, seed=seed + 4)
    for side in (-1.0, 1.0):
        add_box(mesh, at((side * length * 0.3, width * 0.5 + 2.6, height + 3.4)), (1.6, 2.0, width * 0.96), IRON, yaw=yaw, roll=-0.5, seed=seed + 5)


def add_sack(mesh, center, size, seed, top_color=None):
    """A sack of cloth, tied at its neck, open with what it holds showing when top_color is given"""
    add_blob(mesh, add(center, (0.0, 0.0, size * 0.8)), (size, size * 0.9, size * 1.1), CLOTH, 0.0, seed, 1, bumps=0.25, flatten=0.75, shade=0.4)
    if top_color:
        add_blob(mesh, add(center, (0.0, 0.0, size * 1.75)), (size * 0.62, size * 0.58, size * 0.3), top_color, GLOW, seed + 1, 1, bumps=0.2, flatten=0.6, shade=0.2)
    else:
        add_blob(mesh, add(center, (0.0, 0.0, size * 1.95)), (size * 0.3, size * 0.3, size * 0.32), CLOTH, 0.0, seed + 1, 1, bumps=0.2, flatten=0.9, shade=0.3)


# where the hero that took a mine or a mill stands, before it, in the units of its cell (FHexSiteKind::HeroPlace times the radius of a
# cell), which is HERO_PLACE over the scale of the mesh in its own units (MineScale 1.3, MillScale 1.5)
HERO_PLACE = 60.0

# the height of the deck of a boat over the water, in its own units, which BoatDeckLift of DA_HexHeroes repeats
BOAT_DECK = 4.0


def mine():
    """A mound of rock with a dark mouth framed by timbers, a heap of what is dug beside it and a flag on a pole. It stands in the back of
    its cell: the hero that takes it stands before its mouth, on a trodden patch around HERO_PLACE, which nothing of it reaches"""
    mesh = Mesh()
    # the rock it stands on goes down under the ground
    add_blob(mesh, (-24.0, 0.0, -8.0), (48.0, 50.0, 14.0), ROCK, 0.0, 40, 2, bumps=0.2, flatten=1.0, shade=0.4, flat=True)
    add_blob(mesh, (-30.0, 0.0, 6.0), (36.0, 36.0, 32.0), ROCK, 0.0, 41, 2, bumps=0.3, flatten=0.4, shade=0.5, flat=True)
    add_blob(mesh, (-40.0, 20.0, 4.0), (22.0, 20.0, 22.0), ROCK, 0.0, 42, 2, bumps=0.3, flatten=0.4, shade=0.5, flat=True)
    add_blob(mesh, (-32.0, -24.0, 3.0), (20.0, 18.0, 18.0), ROCK, 0.0, 43, 2, bumps=0.3, flatten=0.4, shade=0.5, flat=True)
    # the mouth, a dark hollow in the front of the mound, and its frame
    add_blob(mesh, (5.0, 0.0, 9.0), (9.0, 10.5, 13.5), DARK, 0.0, 44, 1, bumps=0.0, flatten=0.7, shade=0.0)
    for side in (-1.0, 1.0):
        timber(mesh, (12.0, side * 12.5, -SKIRT), (12.0, side * 11.5, 25.0), 2.1, 45)
        timber(mesh, (12.5, side * 12.0, 12.0), (-1.0, side * 14.0, 20.0), 1.5, 46)
    timber(mesh, (12.0, -15.5, 25.5), (12.0, 15.5, 25.5), 2.4, 47)
    # the heap of its resource, and a few chunks on the ground beside the mouth
    chunks(mesh, (10.0, -38.0, 0.0), 9, 13.0, (3.5, 7.5), 48)
    chunks(mesh, (16.0, 34.0, 0.0), 3, 6.0, (3.0, 4.5), 49)
    # the place of the hero
    add_blob(mesh, (HERO_PLACE / 1.3, 0.0, -SKIRT * 0.5), (25.0, 23.0, SKIRT * 0.5 + 1.2), EARTH, 0.0, 51, 2, bumps=0.1, flatten=1.0, shade=0.3)
    # the flag of its owner
    add_tube(mesh, [(-10.0, 34.0, 14.0), (-10.0, 34.0, 78.0)], [1.2, 0.9], WOOD, [0.0, 0.0], 50, 5, True, bark=0.06)
    mesh.begin_part()
    flag = [((-10.0, 34.0, 76.0), 1.0), ((-10.0, 34.0, 58.0), 1.0), ((-36.0, 34.5, 61.0), 0.85), ((-28.0, 34.0, 67.0), 0.8), ((-36.0, 33.5, 73.0), 0.85)]
    indices = [mesh.vertex(point, (TINTED[0] * tone, TINTED[1] * tone, TINTED[2] * tone), OWNER, normal=(0.0, 1.0, 0.25)) for point, tone in flag]
    for index in range(1, len(indices) - 1):
        mesh.sheet(indices[0], indices[index], indices[index + 1])
    return mesh


def pile():
    """A heap of chunks of a resource lying on the ground, for a kind without a mesh of its own"""
    mesh = Mesh()
    add_earth(mesh, 26.0, 60)
    chunks(mesh, (0.0, 0.0, 0.0), 16, 20.0, (4.0, 9.5), 61)
    add_sparkles(mesh, 7, 24.0, (8.0, 20.0), 62, 34.0)
    return mesh


def gold_pile():
    """A heap of coins with stacks of them and ingots around it, and an open chest that overflows"""
    mesh = Mesh()
    add_earth(mesh, 31.0, 100)
    add_blob(mesh, (3.0, -2.0, 0.5), (15.0, 14.0, 8.5), GOLD, GLOW, 101, 2, bumps=0.22, flatten=0.2, shade=0.5)
    add_chest(mesh, (-13.0, 9.0, 0.0), 0.5, 102)
    add_blob(mesh, add((-13.0, 9.0, 0.0), (0.0, 0.0, 6.5)), (7.4, 4.6, 3.6), GOLD, GLOW, 103, 1, bumps=0.25, flatten=0.4, shade=0.4)
    # coins lying on the heap and around it, and stacks of them
    for index in range(26):
        angle = hash_unit(104, index, 1) * 6.2832
        out = 4.0 + 23.0 * hash_unit(104, index, 2) ** 0.8
        x, y = 3.0 + out * math.cos(angle), -2.0 + out * math.sin(angle)
        on_heap = max(1.0 - (out / 15.0) ** 2, 0.0) ** 0.5 * 8.0
        tilt = rotated((0.0, 0.0, 1.0), angle, (hash_unit(104, index, 3) - 0.3) * (1.1 if on_heap > 0.5 else 0.4))
        base = (x, y, 1.6 + on_heap)
        add_cylinder(mesh, base, add(base, mul(tilt, 0.9)), 2.7, 8, GOLD, GLOW, lerp(GOLD, (1.0, 0.85, 0.3), 0.5), 105 + index)
    for index in range(6):
        angle = 0.9 + index * 1.05 + hash_unit(106, index) * 0.5
        out = 19.0 + 6.0 * hash_unit(106, index, 2)
        base = (3.0 + out * math.cos(angle), -2.0 + out * math.sin(angle), 1.0)
        add_cylinder(mesh, base, add(base, (0.6 * hash_unit(106, index, 3), 0.0, 3.0 + 5.5 * hash_unit(106, index, 4))), 2.7, 8, GOLD_DEEP, GLOW, GOLD, 107 + index)
    # ingots stacked beside the chest
    for index, (x, y, z, yaw) in enumerate(((16.0, 14.0, 2.6, 0.3), (20.0, 9.0, 2.6, 0.45), (18.0, 11.5, 5.4, 1.9), (9.0, 20.0, 2.6, -0.4))):
        add_box(mesh, (x, y, z), (8.0, 3.4, 2.8), GOLD, GLOW, yaw=yaw, seed=108 + index)
    add_sparkles(mesh, 10, 28.0, (7.0, 22.0), 109, 36.0)
    return mesh


def wood_pile():
    """A stack of logs between stakes, split logs and planks beside it, and a block with an axe"""
    mesh = Mesh()
    add_earth(mesh, 32.0, 120)
    radius = 3.7
    index = 0
    for row, count in enumerate((5, 4, 3, 2)):
        for place in range(count):
            x = (place - (count - 1) * 0.5) * radius * 2.02 - 4.0
            z = 1.0 + radius + row * radius * 1.76
            slide = (hash_unit(121, index, 1) - 0.5) * 5.0
            thick = radius * (0.88 + 0.2 * hash_unit(121, index, 2))
            add_cylinder(mesh, (x, -17.0 + slide, z), (x, 17.0 + slide, z), thick, 8, WOOD, GLOW, WOOD_CUT, 122 + index)
            index += 1
    for side in (-1.0, 1.0):
        for along in (-9.0, 9.0):
            add_cylinder(mesh, (-4.0 + side * radius * 5.4, along, -SKIRT), (-4.0 + side * radius * 5.0, along, 15.0), 1.4, 6, WOOD, 0.0, WOOD_CUT, 140)
    # split logs lying about and planks leaning on the stack
    for number, (x, y, yaw) in enumerate(((21.0, 14.0, 0.7), (24.0, 4.0, -0.3), (17.0, 22.0, 1.3), (-25.0, -16.0, 0.4), (10.0, -24.0, 1.5))):
        end = add((x, y, 2.6), rotated((13.0, 0.0, 0.0), yaw))
        add_cylinder(mesh, (x, y, 2.6), end, 2.6, 7, WOOD, GLOW, WOOD_CUT, 141 + number)
    for number, (x, y, yaw) in enumerate(((-23.0, 6.0, 0.15), (-25.0, 12.0, -0.1))):
        add_box(mesh, (x, y, 7.0), (2.0, 26.0, 5.0), WOOD_CUT, GLOW, yaw=yaw, pitch=0.55, seed=146 + number)
    # the block with an axe in it
    add_cylinder(mesh, (21.0, -13.0, -SKIRT), (21.0, -13.0, 7.0), 5.5, 9, WOOD, 0.0, WOOD_CUT, 148)
    add_cylinder(mesh, (21.0, -13.0, 6.0), (27.0, -19.0, 20.0), 0.9, 5, PLANK, 0.0, PLANK, 149)
    add_box(mesh, (21.6, -13.6, 8.6), (6.5, 1.6, 4.4), ORE_BRIGHT, 0.0, yaw=-0.78, pitch=0.5, seed=150)
    add_sparkles(mesh, 7, 28.0, (8.0, 24.0), 151, 36.0)
    return mesh


def ore_chunks(mesh, center, count, reach, sizes, seed, heap=0.0):
    """Angular lumps of ore, dark with rusty and bright ones among them; heap lifts those in the middle"""
    for index in range(count):
        angle = hash_unit(seed, index, 1) * 6.2832
        out = reach * hash_unit(seed, index, 2) ** 0.6
        size = sizes[0] + (sizes[1] - sizes[0]) * hash_unit(seed, index, 3)
        pick = hash_unit(seed, index, 4)
        color = ORE_BRIGHT if pick > 0.82 else ORE_RUST if pick > 0.6 else ORE
        position = (center[0] + out * math.cos(angle), center[1] + out * math.sin(angle), center[2] + size * 0.5 + heap * max(1.0 - out / max(reach, 0.01), 0.0))
        add_blob(mesh, position, (size, size * (0.75 + 0.4 * hash_unit(seed, index, 5)), size * (0.7 + 0.4 * hash_unit(seed, index, 6))), color, GLOW,
            seed * 31 + index, 1, bumps=0.42, flatten=0.75, shade=0.5, flat=True)


def ore_pile():
    """A cart heaped with ore, a heap of lumps beside it and a pick"""
    mesh = Mesh()
    add_earth(mesh, 31.0, 160)
    yaw = 0.35

    def at(point):
        return add((-9.0, 5.0, 0.0), rotated(point, yaw))

    # the cart: a box of planks on four wheels, bound with iron
    add_box(mesh, at((0.0, 0.0, 6.0)), (22.0, 13.0, 1.4), PLANK, yaw=yaw, seed=161)
    for side in (-1.0, 1.0):
        add_box(mesh, at((0.0, side * 6.2, 10.5)), (22.0, 1.3, 8.0), PLANK, yaw=yaw, seed=162)
        add_box(mesh, at((side * 10.6, 0.0, 10.5)), (1.3, 13.0, 8.0), PLANK, yaw=yaw, seed=163)
        add_box(mesh, at((side * 6.0, 0.0, 10.5)), (1.5, 13.8, 8.6), IRON, yaw=yaw, seed=164)
        for along in (-6.5, 6.5):
            add_cylinder(mesh, at((along, side * 6.6, 4.4)), at((along, side * 8.6, 4.4)), 4.4, 9, IRON, 0.0, PLANK, 165)
    ore_chunks(mesh, at((0.0, 0.0, 11.5)), 11, 7.0, (2.6, 3.8), 166, heap=3.5)
    # the heap beside it, and lumps fallen about
    ore_chunks(mesh, (14.0, -12.0, 0.5), 12, 11.0, (3.6, 7.0), 167, heap=6.0)
    ore_chunks(mesh, (0.0, 0.0, 0.5), 8, 27.0, (2.4, 3.8), 168)
    # a pick leaning on the cart
    add_cylinder(mesh, (9.0, 17.0, 0.5), (4.0, 12.0, 19.0), 0.9, 5, PLANK, 0.0, PLANK, 169)
    add_box(mesh, (4.0, 12.0, 19.0), (12.0, 1.5, 2.0), ORE_BRIGHT, 0.0, yaw=-0.8, pitch=0.25, seed=170)
    add_sparkles(mesh, 7, 28.0, (8.0, 24.0), 171, 34.0)
    return mesh


def add_flask(mesh, base, size, seed, lying=None):
    """A round flask of glass, quicksilver in its belly and a cork in its neck; lying, it points along a direction on the ground"""
    profile = ((0.0, 0.5), (0.3, 0.92), (0.75, 1.0), (1.25, 0.86), (1.62, 0.45), (1.85, 0.3), (2.5, 0.3), (2.6, 0.4))
    axis = unit(lying) if lying else (0.0, 0.0, 1.0)
    start = add(base, (0.0, 0.0, size * 0.95)) if lying else base
    path = [add(start, mul(axis, share * size)) for share, _ in profile]
    radii = [reach * size for _, reach in profile]

    def color_at(position, t):
        return SILVER if t < 0.45 else GLASS

    add_tube(mesh, path, radii, GLASS, [GLOW] * len(path), seed, 8, True, bark=0.0, color_at=color_at)
    # the bottom, which a tube leaves open
    add_blob(mesh, add(start, mul(axis, size * 0.15)), (size * 0.6, size * 0.6, size * 0.3), SILVER, GLOW, seed + 1, 1, bumps=0.0, flatten=1.0, shade=0.2)
    add_cylinder(mesh, path[-1], add(path[-1], mul(axis, size * 0.45)), size * 0.3, 6, WOOD_CUT, 0.0, WOOD_CUT, seed + 2)


def mercury_pile():
    """Flasks of quicksilver, large and small, on a tray of planks and beside it, one fallen in a puddle of it"""
    mesh = Mesh()
    add_earth(mesh, 29.0, 180)
    add_box(mesh, (-2.0, 2.0, 1.6), (30.0, 21.0, 2.4), PLANK, yaw=0.25, seed=181)
    for index, (x, y, size) in enumerate(((-9.0, 5.0, 5.6), (1.0, 7.0, 4.6), (8.0, 4.0, 5.2), (-4.0, -3.0, 4.4), (5.0, -4.0, 4.0), (-12.0, -3.0, 3.6), (12.0, -3.0, 3.4))):
        add_flask(mesh, (x, y, 2.6), size, 182 + index * 3)
    for index, (x, y, size) in enumerate(((19.0, 12.0, 4.4), (-21.0, 12.0, 4.0), (-17.0, -15.0, 4.8), (22.0, -9.0, 3.6))):
        add_flask(mesh, (x, y, 0.4), size, 205 + index * 3)
    # one fallen, and what ran out of it
    add_flask(mesh, (4.0, -19.0, 0.6), 4.6, 220, lying=(0.8, -0.5, 0.0))
    add_blob(mesh, (13.0, -22.0, 0.3), (8.0, 5.5, 1.2), SILVER, GLOW, 223, 2, bumps=0.3, flatten=0.3, shade=0.1)
    add_blob(mesh, (20.0, -18.0, 0.3), (3.0, 2.4, 0.9), SILVER, GLOW, 224, 1, bumps=0.3, flatten=0.3, shade=0.1)
    add_sparkles(mesh, 8, 26.0, (8.0, 24.0), 225, 34.0)
    return mesh


def sulfur_pile():
    """A heap of yellow sulfur with lumps of it, open sacks of it and a shovel stuck in the heap"""
    mesh = Mesh()
    add_earth(mesh, 30.0, 240)
    add_blob(mesh, (2.0, 0.0, 0.5), (17.0, 15.5, 11.0), SULFUR, GLOW, 241, 2, bumps=0.2, flatten=0.15, shade=0.45)
    add_blob(mesh, (-9.0, -9.0, 0.5), (10.0, 9.0, 6.0), SULFUR_DEEP, GLOW, 242, 2, bumps=0.25, flatten=0.15, shade=0.45)
    for index in range(20):
        angle = hash_unit(243, index, 1) * 6.2832
        out = 5.0 + 22.0 * hash_unit(243, index, 2) ** 0.8
        size = 2.4 + 2.8 * hash_unit(243, index, 3)
        on_heap = max(1.0 - (out / 17.0) ** 2, 0.0) ** 0.5 * 9.5
        position = (2.0 + out * math.cos(angle), out * math.sin(angle), 0.8 + on_heap + size * 0.4)
        add_blob(mesh, position, (size, size * 0.85, size * 0.8), lerp(SULFUR, SULFUR_DEEP, hash_unit(243, index, 4)), GLOW, 244 + index, 1,
            bumps=0.4, flatten=0.8, shade=0.4, flat=True)
    add_sack(mesh, (-20.0, 9.0, 0.0), 5.6, 270, SULFUR)
    add_sack(mesh, (-14.0, 18.0, 0.0), 5.0, 272, SULFUR)
    add_sack(mesh, (-24.0, -3.0, 0.0), 4.6, 274)
    # the shovel
    add_cylinder(mesh, (12.0, 6.0, 8.0), (22.0, 14.0, 27.0), 0.9, 5, PLANK, 0.0, PLANK, 276)
    add_box(mesh, (10.5, 4.8, 5.6), (6.0, 1.0, 7.5), ORE_BRIGHT, 0.0, yaw=0.68, pitch=-0.45, seed=277)
    add_sparkles(mesh, 7, 27.0, (9.0, 24.0), 278, 36.0)
    return mesh


def crystal_pile():
    """A cluster of red crystals of many sizes on dark rock, with shards of them scattered around"""
    mesh = Mesh()
    add_earth(mesh, 29.0, 300)
    for index, (x, y, size) in enumerate(((0.0, 0.0, 12.0), (-9.0, 7.0, 8.0), (9.0, -6.0, 8.5), (7.0, 10.0, 6.5))):
        add_blob(mesh, (x, y, 0.0), (size, size * 0.9, size * 0.6), ROCK, 0.0, 301 + index, 1, bumps=0.35, flatten=0.5, shade=0.5, flat=True)
    # the tall ones in the middle, leaning out more the further from it they stand
    for index in range(17):
        angle = index * 2.4 + hash_unit(305, index, 1) * 0.8
        out = 13.0 * (index / 16.0) ** 0.75
        base = (out * math.cos(angle), out * math.sin(angle), 1.5)
        lean = 0.12 + 0.75 * (out / 13.0)
        direction = (math.cos(angle) * lean, math.sin(angle) * lean, 1.0)
        long = (30.0 - 17.0 * (out / 13.0)) * (0.75 + 0.5 * hash_unit(305, index, 2))
        add_crystal(mesh, base, direction, long, long * 0.17 + 1.0, CRYSTAL_ROOT, CRYSTAL_TIP, GLOW, 306 + index)
    # small ones around their feet, and shards lying about
    for index in range(16):
        angle = hash_unit(330, index, 1) * 6.2832
        out = 12.0 + 14.0 * hash_unit(330, index, 2)
        base = (out * math.cos(angle), out * math.sin(angle), 0.6)
        lying = hash_unit(330, index, 3) > 0.5
        direction = (math.cos(angle + 1.0), math.sin(angle + 1.0), 0.12) if lying else (math.cos(angle) * 0.5, math.sin(angle) * 0.5, 1.0)
        long = 5.0 + 6.0 * hash_unit(330, index, 4)
        add_crystal(mesh, add(base, (0.0, 0.0, 1.2 if lying else 0.0)), direction, long, long * 0.2 + 0.7, CRYSTAL_ROOT, CRYSTAL_TIP, GLOW, 331 + index)
    add_sparkles(mesh, 10, 26.0, (9.0, 30.0), 350, 42.0)
    return mesh


def gem_pile():
    """An open chest heaped with cut gems, more of them spilled on a cloth before it, green ones most"""
    mesh = Mesh()
    add_earth(mesh, 29.0, 360)
    add_chest(mesh, (-6.0, 8.0, 0.0), -0.3, 361, size=(19.0, 12.5, 8.5))
    add_blob(mesh, (-6.0, 8.0, 6.6), (8.2, 5.0, 3.4), GEMS[0], GLOW, 362, 1, bumps=0.25, flatten=0.4, shade=0.4, flat=True)
    for index in range(9):
        angle = hash_unit(363, index, 1) * 6.2832
        out = 5.5 * hash_unit(363, index, 2)
        add_gem(mesh, add((-6.0, 8.0, 10.2), rotated((out * 1.3 * math.cos(angle), out * 0.7 * math.sin(angle), -out * 0.25), -0.3)),
            2.6 + 1.6 * hash_unit(363, index, 3), GEMS[int(hash_unit(363, index, 4) * len(GEMS)) % len(GEMS)], GLOW, 364 + index)
    # the cloth they spilled on
    add_blob(mesh, (6.0, -10.0, 0.4), (19.0, 13.0, 1.5), (0.3, 0.05, 0.07), 0.0, 380, 2, bumps=0.2, flatten=0.3, shade=0.2)
    for index in range(30):
        angle = hash_unit(381, index, 1) * 6.2832
        out = 20.0 * hash_unit(381, index, 2) ** 0.7
        size = 2.2 + 2.6 * hash_unit(381, index, 3) ** 2
        add_gem(mesh, (6.0 + out * math.cos(angle), -10.0 + out * 0.7 * math.sin(angle), 1.6 + size * 0.5), size,
            GEMS[int(hash_unit(381, index, 4) * len(GEMS)) % len(GEMS)], GLOW, 382 + index)
    # a pouch of them
    add_sack(mesh, (18.0, 10.0, 0.0), 4.4, 420, GEMS[2])
    add_sparkles(mesh, 11, 27.0, (7.0, 22.0), 421, 34.0)
    return mesh


def treasure_chest():
    """A treasure chest, shut, as the developer asked on 2026-11-15 for chests that give gold or experience: a box of planks with iron bands and
    corners, a rounded lid of planks over it, a gilded lock, gold glinting at the seam of its lid, and the sparkles of a pile around it"""
    mesh = Mesh()
    add_earth(mesh, 22.0, 140)
    yaw = 0.35
    length, width, height = 22.0, 14.0, 10.0

    def at(point):
        return rotated(point, yaw)

    add_box(mesh, at((0.0, 0.0, height * 0.5)), (length, width, height), PLANK, yaw=yaw, seed=141)
    # the lid: planks laid around a half round over the box
    sides = 7
    for step in range(sides):
        angle = math.pi * (step + 0.5) / sides
        y = -math.cos(angle) * width * 0.5
        z = height + math.sin(angle) * width * 0.42
        add_box(mesh, at((0.0, y, z)), (length + 0.6, width * 0.25, 1.6), PLANK, yaw=yaw, roll=angle - math.pi * 0.5, seed=142 + step)
    # iron bands over box and lid, and the corners
    for x in (-length * 0.33, length * 0.33):
        add_box(mesh, at((x, 0.0, height * 0.5)), (1.8, width + 0.8, height + 0.4), IRON, yaw=yaw, seed=150)
        for step in range(sides):
            angle = math.pi * (step + 0.5) / sides
            add_box(mesh, at((x, -math.cos(angle) * (width * 0.5 + 0.4), height + math.sin(angle) * (width * 0.42 + 0.4))), (2.0, width * 0.26, 1.0), IRON, yaw=yaw,
                roll=angle - math.pi * 0.5, seed=151 + step)
    for x in (-1.0, 1.0):
        for y in (-1.0, 1.0):
            add_box(mesh, at((x * (length * 0.5 - 0.6), y * (width * 0.5 - 0.6), height * 0.5)), (2.0, 2.0, height + 0.6), IRON, yaw=yaw, seed=160)
    # a gilded lock on the front, and gold glinting out at the seam of the lid
    add_box(mesh, at((0.0, -width * 0.5 - 0.7, height * 0.82)), (3.6, 1.2, 4.2), GOLD, GLOW, yaw=yaw, seed=161)
    add_box(mesh, at((0.0, -width * 0.5 - 0.2, height + 0.3)), (length * 0.86, 0.6, 0.8), GOLD, GLOW, yaw=yaw, seed=162)
    # a few coins spilled before it
    for index in range(7):
        angle = -1.9 + hash_unit(163, index, 1) * 1.6
        out = 13.0 + 7.0 * hash_unit(163, index, 2)
        base = (out * math.cos(angle), out * math.sin(angle), 1.6)
        add_cylinder(mesh, base, add(base, (0.0, 0.0, 0.8)), 2.4, 8, GOLD, GLOW, lerp(GOLD, (1.0, 0.85, 0.3), 0.5), 164 + index)
    add_sparkles(mesh, 9, 24.0, (8.0, 24.0), 170, 34.0)
    return mesh


def mill():
    """A windmill: a round stone tower under a pointed roof, four sails on its front and sacks of what it gives at its foot. It stands in the
    back of its cell, as a mine does, and the hero that takes it stands before it, around HERO_PLACE"""
    mesh = Mesh()
    stone = (0.34, 0.32, 0.29)
    back = -16.0
    # its footing goes down under the ground
    add_blob(mesh, (back, 0.0, -7.0), (26.0, 25.0, 9.0), ROCK, 0.0, 70, 2, bumps=0.15, flatten=1.0, shade=0.4, flat=True)
    add_tube(mesh, [(back, 0.0, -SKIRT), (back, 0.0, 24.0), (back, 0.0, 48.0)], [15.5, 12.5, 10.0], stone, [0.0, 0.0, 0.0], 71, 10, False, bark=0.08)
    add_tube(mesh, [(back, 0.0, 47.0), (back, 0.0, 56.0), (back, 0.0, 66.0)], [12.5, 7.5, 0.6], WOOD, [0.0, 0.0, 0.0], 72, 10, True, bark=0.1)
    add_blob(mesh, (back + 14.0, 0.0, 6.0), (3.0, 4.5, 8.0), DARK, 0.0, 73, 1, bumps=0.0, flatten=0.8, shade=0.0)
    # the place of the hero
    add_blob(mesh, (HERO_PLACE / 1.5, 0.0, -SKIRT * 0.5), (22.0, 20.0, SKIRT * 0.5 + 1.2), EARTH, 0.0, 76, 2, bumps=0.1, flatten=1.0, shade=0.3)
    # the sails, flat on the front of the tower around a hub
    hub = (back + 13.0, 0.0, 42.0)
    timber(mesh, (back + 9.0, 0.0, 42.0), (back + 15.0, 0.0, 42.0), 1.6, 74)
    mesh.begin_part()
    cloth = (0.62, 0.58, 0.5)
    for turn in range(4):
        angle = math.radians(20.0 + 90.0 * turn)
        along = (0.0, math.cos(angle), math.sin(angle))
        across = (0.0, -math.sin(angle), math.cos(angle))
        corners = []
        for reach, side in ((5.0, -0.6), (36.0, -0.6), (36.0, 6.5), (5.0, 6.5)):
            corners.append(mesh.vertex((hub[0] + 1.5, hub[1] + along[1] * reach + across[1] * side, hub[2] + along[2] * reach + across[2] * side),
                cloth, 0.0, normal=(1.0, 0.0, 0.3)))
        mesh.sheet(corners[0], corners[1], corners[2])
        mesh.sheet(corners[0], corners[2], corners[3])
    # the sacks of its resource
    chunks(mesh, (back + 16.0, -26.0, 0.0), 6, 9.0, (3.5, 6.0), 75)
    return mesh


STONE = (0.36, 0.34, 0.31)
STONE_DARK = (0.2, 0.19, 0.18)
SAIL = (0.85, 0.82, 0.74)
ROPE = (0.45, 0.38, 0.25)


def add_ring_of(mesh, center, radius, count, size, color, alpha, seed):
    """Stones in a ring around a point, each turned its own way"""
    for index in range(count):
        angle = 2.0 * math.pi * (index + 0.3 * hash_unit(seed, index, 1)) / count
        add_box(mesh, (center[0] + radius * math.cos(angle), center[1] + radius * math.sin(angle), center[2] + size[2] * 0.5), size, color, alpha,
            yaw=angle + 0.3 * hash_unit(seed, index, 2), seed=seed + index)


def artifact():
    """An artifact that lies on the map, as the developer asked on 2026-11-15: a low pedestal of worked stone in a ring of standing stones, on it
    a gilded casket whose open lid shows a glow in the color of the artifact (RESOURCE), a cloth under it, and the sparkles of a pile"""
    mesh = Mesh()
    add_earth(mesh, 20.0, 180)
    add_cylinder(mesh, (0.0, 0.0, -SKIRT), (0.0, 0.0, 3.0), 10.5, 8, STONE_DARK, 0.0, STONE, 181)
    add_cylinder(mesh, (0.0, 0.0, 3.0), (0.0, 0.0, 9.0), 7.0, 8, STONE, 0.0, STONE, 182, taper=0.85)
    add_cylinder(mesh, (0.0, 0.0, 9.0), (0.0, 0.0, 10.5), 8.0, 8, STONE, 0.0, STONE, 183)
    add_box(mesh, (0.0, 0.0, 10.8), (13.0, 9.0, 0.6), (0.45, 0.08, 0.08), 0.0, yaw=0.4, seed=184)
    # the casket, gilded, its lid open
    add_box(mesh, rotated((0.0, 0.0, 13.2), 0.4), (8.5, 5.5, 4.2), GOLD_DEEP, GLOW, yaw=0.4, seed=185)
    add_box(mesh, rotated((0.0, 3.3, 17.0), 0.4), (8.5, 1.0, 5.0), GOLD, GLOW, yaw=0.4, roll=-0.4, seed=186)
    # what it holds glows in the color of the artifact
    add_blob(mesh, rotated((0.0, 0.0, 15.6), 0.4), (3.4, 2.2, 2.0), TINTED, RESOURCE, 187, 1, bumps=0.15, flatten=0.4, shade=0.1)
    add_gem(mesh, rotated((0.0, -2.9, 13.6), 0.4), 1.2, (0.7, 0.1, 0.15), GLOW, 188)
    add_ring_of(mesh, (0.0, 0.0, -2.0), 16.0, 5, (2.6, 2.6, 9.0), STONE, 0.0, 190)
    add_sparkles(mesh, 9, 18.0, (12.0, 30.0), 195, 36.0)
    return mesh


def great_artifact():
    """A great artifact: a stepped altar between two columns under a lintel, on it a sword thrust into a block of stone, its blade glowing in the
    color of the artifact (RESOURCE), and more sparkles"""
    mesh = Mesh()
    add_earth(mesh, 26.0, 200)
    for step, (reach, top) in enumerate(((15.0, 2.5), (11.5, 5.0), (8.0, 8.0))):
        tall = SKIRT + 2.5 if step == 0 else 2.5
        add_box(mesh, (0.0, 0.0, top - tall * 0.5), (reach * 2.0, reach * 1.6, tall), STONE, 0.0, seed=201 + step)
    # four columns at its corners under a frame of lintels, so it reads as a shrine from any side
    for x in (-1.0, 1.0):
        for y in (-1.0, 1.0):
            add_cylinder(mesh, (x * 12.5, y * 12.5, -2.0), (x * 12.5, y * 12.5, 26.0), 2.0, 8, STONE, 0.0, STONE, 205)
            add_box(mesh, (x * 12.5, y * 12.5, 26.8), (5.0, 5.0, 1.6), STONE, 0.0, seed=206)
    for side in (-1.0, 1.0):
        add_box(mesh, (side * 12.5, 0.0, 28.6), (4.0, 30.0, 2.4), STONE, 0.0, seed=207)
        add_box(mesh, (0.0, side * 12.5, 28.6), (30.0, 4.0, 2.4), STONE, 0.0, seed=208)
    # the block and the sword
    add_box(mesh, (0.0, 0.0, 11.5), (7.0, 6.0, 7.0), STONE_DARK, 0.0, yaw=0.2, seed=218)
    add_box(mesh, (0.0, 0.0, 22.0), (0.8, 2.2, 16.0), TINTED, RESOURCE, yaw=0.2, seed=209)
    add_box(mesh, (0.0, 0.0, 30.3), (1.2, 8.0, 1.2), GOLD, GLOW, yaw=0.2, seed=210)
    add_cylinder(mesh, (0.0, 0.0, 30.8), (0.0, 0.0, 33.8), 0.7, 6, (0.25, 0.12, 0.06), 0.0, None, 211)
    add_sparkles(mesh, 14, 22.0, (14.0, 40.0), 215, 44.0)
    return mesh


def shipyard():
    """A shipyard on a coast, as the developer asked on 2026-11-15 for boats: a timber slipway that runs down at its front (+X), the ribs of a
    hull on it, a shed of planks under a roof at its back, a crane of beams and stacks of timber. It stands in the back of its cell, as a mine
    does, and the hero that visits it stands before it"""
    mesh = Mesh()
    back = -14.0
    add_blob(mesh, (back, 0.0, -6.0), (28.0, 26.0, 8.0), ROCK, 0.0, 220, 2, bumps=0.12, flatten=1.0, shade=0.4, flat=True)
    # the shed
    add_box(mesh, (back - 6.0, -14.0, 8.0 - SKIRT * 0.5), (16.0, 14.0, 16.0 + SKIRT), PLANK, 0.0, seed=221)
    for side in (-1.0, 1.0):
        add_box(mesh, (back - 6.0, -14.0 + side * 4.0, 18.0), (18.0, 9.0, 1.2), WOOD, 0.0, roll=side * 0.55, seed=222)
    add_box(mesh, (back + 2.2, -14.0, 6.0), (0.6, 5.0, 8.0), DARK, 0.0, seed=223)
    # the slipway, down at its front
    for side in (-1.0, 1.0):
        timber(mesh, (back - 6.0, 8.0 + side * 5.0, 2.0), (34.0, 8.0 + side * 5.0, -6.0), 1.2, 224)
    for index in range(7):
        x = back - 4.0 + index * 6.5
        add_box(mesh, (x, 8.0, 2.2 - index * 0.9), (1.4, 13.0, 1.0), PLANK, 0.0, seed=225 + index)
    # the ribs of a hull that is being built on it
    for index in range(6):
        x = back + 1.0 + index * 4.2
        half = 6.0 * math.sin(math.pi * (index + 0.8) / 6.6)
        path = [(x, 8.0 - half, 9.0), (x, 8.0 - half * 0.8, 4.0), (x, 8.0, 2.8), (x, 8.0 + half * 0.8, 4.0), (x, 8.0 + half, 9.0)]
        add_tube(mesh, path, [0.6] * 5, WOOD_CUT, [0.0] * 5, 230 + index, 5, False, bark=0.05)
    timber(mesh, (back, 8.0, 3.0), (back + 23.0, 8.0, 3.0), 0.9, 236)
    # a crane of beams over it, a rope hanging
    timber(mesh, (back + 4.0, -2.0, -2.0), (back + 4.0, -2.0, 26.0), 1.1, 237)
    timber(mesh, (back + 4.0, -2.0, 24.0), (back + 16.0, 8.0, 24.0), 0.8, 238)
    add_tube(mesh, [(back + 16.0, 8.0, 24.0), (back + 16.0, 8.0, 12.0)], [0.25, 0.25], ROPE, [0.0, 0.0], 239, 4, False, bark=0.0)
    # timber stacked beside the shed
    for index in range(5):
        y = -2.0 + (index % 3) * 1.9
        z = 1.2 + (index // 3) * 1.9
        timber(mesh, (back - 14.0, y + (index // 3) * 0.95, z), (back + 4.0, y + (index // 3) * 0.95, z), 0.95, 240 + index)
    return mesh


def boat():
    """A boat a hero sails, as the developer asked on 2026-11-15: a hull of planks lofted from cross sections, its bow along +X and its waterline
    at 0, a deck at BOAT_DECK where the hero stands, a mast with a square sail in the color of its owner (OWNER), its gunwale, ropes and a
    steering oar at the stern. A boat at a coast has the sail of nobody"""
    mesh = Mesh()
    length, beam, deck = 20.0, 6.5, BOAT_DECK
    hull_color = (0.3, 0.19, 0.1)
    # cross sections from the stern to the bow: where along, half the beam, the bottom, and how high the gunwale stands
    sections = []
    count = 11
    for index in range(count):
        t = index / (count - 1)
        x = -length + 2.0 * length * t
        width = beam * math.sin(math.pi * (0.12 + 0.82 * t)) ** 0.7
        rise = deck + 1.6 + 2.2 * max(t - 0.75, 0.0) / 0.25 + 1.2 * max(0.2 - t, 0.0) / 0.2
        sections.append((x, max(width, 0.4), -2.8 * math.sin(math.pi * (0.05 + 0.9 * t)) ** 0.5, rise))
    mesh.begin_part()
    rings = []
    for index, (x, width, bottom, rise) in enumerate(sections):
        ring = []
        for step, (side, height) in enumerate(((-1.0, rise), (-0.95, deck * 0.5), (-0.6, bottom * 0.7), (0.0, bottom), (0.6, bottom * 0.7), (0.95, deck * 0.5), (1.0, rise))):
            tone = 0.75 + 0.35 * hash_unit(250, index, step) - (0.2 if height < 0.0 else 0.0)
            ring.append(mesh.vertex((x, side * width, height), scale_color(hull_color, tone), 0.0))
        rings.append(ring)
    for index in range(count - 1):
        axis = ((sections[index][0] + sections[index + 1][0]) * 0.5, 0.0, deck * 0.3)
        for step in range(6):
            mesh.quad(rings[index][step], rings[index][step + 1], rings[index + 1][step + 1], rings[index + 1][step], axis)
    # the transom at the stern and the stem at the bow
    for end, outside in ((0, (sections[0][0] - 5.0, 0.0, deck * 0.3)), (count - 1, (sections[-1][0] + 5.0, 0.0, deck * 0.3))):
        for step in range(6):
            mesh.triangle(rings[end][3], rings[end][step], rings[end][step + 1], outside)
    # the deck, a little below the gunwale
    mesh.begin_part(flat=True)
    planks = []
    for index, (x, width, _, _) in enumerate(sections):
        for side in (-1.0, 1.0):
            planks.append(mesh.vertex((x, side * width * 0.92, deck), scale_color(WOOD_CUT, 0.8 + 0.2 * hash_unit(251, index)), 0.0))
    for index in range(count - 1):
        mesh.quad(planks[index * 2], planks[index * 2 + 1], planks[index * 2 + 3], planks[index * 2 + 2], (sections[index][0], 0.0, deck - 5.0))
    # the gunwale, a rail along each side
    for side in (-1.0, 1.0):
        path = [(x, side * width, rise + 0.3) for x, width, _, rise in sections]
        add_tube(mesh, path, [0.45] * count, WOOD, [0.0] * count, 252, 4, False, bark=0.0)
    # the mast and its yard, a square sail in the color of the owner, a pennant at its top
    # towards the bow, as the developer asked on 2026-11-15, the hero right behind it (BoatHeroForward of DA_HexHeroes)
    mast = 9.0
    add_cylinder(mesh, (mast, 0.0, deck), (mast, 0.0, deck + 30.0), 0.7, 6, WOOD, 0.0, None, 253, taper=0.6)
    timber(mesh, (mast + 0.8, -10.0, deck + 26.0), (mast + 0.8, 10.0, deck + 26.0), 0.45, 254)
    mesh.begin_part()
    corners = []
    for row in range(4):
        for column in range(5):
            y = -9.5 + 19.0 * column / 4
            z = deck + 26.0 - 16.0 * row / 3
            belly = 2.2 * math.sin(math.pi * column / 4) * math.sin(math.pi * (0.15 + 0.7 * row / 3))
            corners.append(mesh.vertex((mast + 1.2 + belly, y, z), SAIL, OWNER, normal=(1.0, 0.0, 0.0)))
    for row in range(3):
        for column in range(4):
            a = corners[row * 5 + column]
            mesh.sheet(a, corners[row * 5 + column + 1], corners[(row + 1) * 5 + column + 1])
            mesh.sheet(a, corners[(row + 1) * 5 + column + 1], corners[(row + 1) * 5 + column])
    mesh.begin_part()
    tip = [mesh.vertex((mast, 0.0, deck + 30.0), SAIL, OWNER, normal=(0.0, 1.0, 0.0)), mesh.vertex((mast, 0.0, deck + 27.5), SAIL, OWNER, normal=(0.0, 1.0, 0.0)),
        mesh.vertex((mast - 7.0, 0.0, deck + 28.8), SAIL, OWNER, normal=(0.0, 1.0, 0.0))]
    mesh.sheet(tip[0], tip[1], tip[2])
    # ropes from the mast to the gunwale, a steering oar at the stern and a carved prow
    for side in (-1.0, 1.0):
        add_tube(mesh, [(mast, 0.0, deck + 29.0), (mast - 14.0, side * 5.0, deck + 1.8)], [0.15, 0.15], ROPE, [0.0, 0.0], 255, 3, False, bark=0.0)
        add_tube(mesh, [(mast, 0.0, deck + 29.0), (length - 3.0, side * 2.5, deck + 3.5)], [0.15, 0.15], ROPE, [0.0, 0.0], 256, 3, False, bark=0.0)
    timber(mesh, (-length + 1.0, 3.2, deck + 3.0), (-length - 4.0, 5.0, -2.5), 0.5, 257)
    add_box(mesh, (-length - 4.2, 5.2, -2.0), (3.5, 0.5, 2.0), WOOD, 0.0, yaw=0.3, seed=258)
    top = sections[-1][3]
    add_tube(mesh, [(length - 1.0, 0.0, top), (length + 1.5, 0.0, top + 3.5), (length + 0.5, 0.0, top + 6.0)], [0.9, 0.7, 0.4], WOOD, [0.0, 0.0, 0.0], 259, 6, True, bark=0.1)
    return mesh


def wreck():
    """A wreck on the sea, as the developer asked on 2026-11-15 for treasure on the water: the stern of a broken hull lying on its side with its
    planks torn, a mast snapped short with rags of a sail, planks, barrels and a crate afloat around it, and a chest on the wreck with gold
    glinting at its lid and the sparkles of a pile, its waterline at 0, at the scale of the boat"""
    mesh = Mesh()
    hull_color = (0.26, 0.17, 0.1)
    roll = 0.5
    # the broken hull: ribs and planks of half a boat, leaning over, its torn end ragged
    for index in range(7):
        x = -14.0 + 4.0 * index
        width = 6.0 * math.sin(math.pi * (0.2 + 0.08 * index)) ** 0.6
        for step, (side, height) in enumerate(((-1.0, 5.0), (-0.7, -1.5), (0.0, -2.8), (0.7, -1.5), (1.0, 5.0))):
            if index == 6 and step in (0, 1):
                continue
            torn = 1.5 * hash_unit(300, index, step) if index >= 5 else 0.0
            y = side * width
            z = height - torn
            add_box(mesh, rotated((x, y, z), 0.0, 0.0, roll), (4.2, 1.2 + abs(side) * 1.6, 1.0 + (1.0 - abs(side)) * 1.4), scale_color(hull_color, 0.75 + 0.35 * hash_unit(301, index, step)),
                yaw=0.0, roll=roll + side * 0.9, seed=302 + index * 7 + step)
    # its deck, broken off at the middle
    add_box(mesh, rotated((-6.0, 0.0, 4.0), 0.0, 0.0, roll), (18.0, 10.0, 0.8), scale_color(WOOD_CUT, 0.7), roll=roll, seed=320)
    # the mast snapped short, a yard hanging from it and rags of the sail
    base = rotated((-4.0, 0.0, 4.0), 0.0, 0.0, roll)
    top = rotated((-4.5, -1.0, 15.0), 0.0, 0.0, roll)
    add_cylinder(mesh, base, top, 0.7, 6, WOOD, 0.0, WOOD_CUT, 321, taper=0.8)
    timber(mesh, add(top, (0.5, -6.0, -3.0)), add(top, (0.0, 5.0, -8.5)), 0.4, 322)
    mesh.begin_part()
    rag = [mesh.vertex(add(top, (0.8, -5.0, -3.4)), SAIL, 0.0, normal=(1.0, 0.0, 0.0)), mesh.vertex(add(top, (0.8, 2.0, -6.5)), SAIL, 0.0, normal=(1.0, 0.0, 0.0)),
        mesh.vertex(add(top, (1.4, -2.5, -10.0)), SAIL, 0.0, normal=(1.0, 0.0, 0.0)), mesh.vertex(add(top, (1.2, 1.0, -9.0)), SAIL, 0.0, normal=(1.0, 0.0, 0.0))]
    mesh.sheet(rag[0], rag[1], rag[3])
    mesh.sheet(rag[0], rag[3], rag[2])
    # planks, barrels and a crate afloat around it
    for index in range(9):
        angle = 2.0 * math.pi * hash_unit(330, index, 1)
        out = 16.0 + 10.0 * hash_unit(330, index, 2)
        add_box(mesh, (out * math.cos(angle), out * math.sin(angle), 0.1), (7.0 + 4.0 * hash_unit(330, index, 3), 1.6, 0.6), scale_color(WOOD_CUT, 0.6 + 0.3 * hash_unit(330, index, 4)),
            yaw=angle * 1.7, seed=331 + index)
    for index, (x, y) in enumerate(((14.0, 9.0), (-12.0, -13.0), (19.0, -6.0))):
        add_cylinder(mesh, (x, y - 2.0, 0.6), (x, y + 2.0, 0.6), 1.8, 8, scale_color(WOOD, 1.4), 0.0, WOOD_CUT, 340 + index)
        add_cylinder(mesh, (x, y - 1.2, 0.6), (x, y - 0.9, 0.6), 1.95, 8, (0.2, 0.2, 0.22), 0.0, None, 344 + index)
    add_box(mesh, (-17.0, 8.0, 0.4), (4.0, 4.0, 3.0), PLANK, yaw=0.6, seed=350)
    # the chest on the deck, gold at its seam
    chest = rotated((-8.0, 1.0, 4.8), 0.0, 0.0, roll)
    add_box(mesh, add(chest, (0.0, 0.0, 2.2)), (7.0, 4.5, 4.4), PLANK, yaw=0.3, roll=roll, seed=351)
    add_box(mesh, add(chest, (0.0, 0.0, 4.6)), (7.4, 4.9, 1.0), IRON, yaw=0.3, roll=roll, seed=352)
    add_box(mesh, add(chest, (0.0, -2.6, 3.2)), (1.4, 0.6, 1.6), GOLD, GLOW, yaw=0.3, roll=roll, seed=353)
    add_sparkles(mesh, 7, 20.0, (6.0, 18.0), 360, 26.0)
    return mesh


# A mesh of its own for each mine, as the developer asked on 2026-11-15 ("лісопильня має виглядати як лісопильня а не шахта ... покращ меш
# шахти до чогось більш зрозумілого реалістичного, навколо шахти розкидай купки ресурсів які вона дає - як декор"): a sawmill, an adit in
# rock with rails and a cart for gold and gems, a quarry cut into a cliff for ore, an alchemist's lab for mercury, an adit in yellow crusted
# rock with vents for sulfur and a cave of crystals. Heaps of what each gives lie about it as decor: plain colors, no sparkles and no glow,
# so they do not read as the piles a hero takes. Each stands in the back of its cell, the hero before it around HERO_PLACE


def add_owner_flag(mesh, x, y, base, top, seed):
    """A pole with the flag of the owner of a mine at its top"""
    add_tube(mesh, [(x, y, base), (x, y, top)], [1.2, 0.9], WOOD, [0.0, 0.0], seed, 5, True, bark=0.06)
    mesh.begin_part()
    flag = [((x, y, top - 2.0), 1.0), ((x, y, top - 20.0), 1.0), ((x - 26.0, y + 0.5, top - 17.0), 0.85), ((x - 18.0, y, top - 11.0), 0.8),
        ((x - 26.0, y - 0.5, top - 5.0), 0.85)]
    indices = [mesh.vertex(point, (TINTED[0] * tone, TINTED[1] * tone, TINTED[2] * tone), OWNER, normal=(0.0, 1.0, 0.25)) for point, tone in flag]
    for index in range(1, len(indices) - 1):
        mesh.sheet(indices[0], indices[index], indices[index + 1])


def add_hero_patch(mesh, seed):
    """The trodden patch before a mine where the hero that takes it stands"""
    add_blob(mesh, (HERO_PLACE / 1.3, 0.0, -SKIRT * 0.5), (25.0, 23.0, SKIRT * 0.5 + 1.2), EARTH, 0.0, seed, 2, bumps=0.1, flatten=1.0, shade=0.3)


def add_mound(mesh, color, seed, crust=None):
    """A mound of rock in the back of the cell, going down under the ground, crusted here and there with a color"""
    add_blob(mesh, (-26.0, 0.0, -8.0), (50.0, 52.0, 14.0), color, 0.0, seed, 2, bumps=0.2, flatten=1.0, shade=0.4, flat=True)
    add_blob(mesh, (-32.0, 0.0, 6.0), (36.0, 38.0, 34.0), color, 0.0, seed + 1, 2, bumps=0.3, flatten=0.4, shade=0.5, flat=True)
    add_blob(mesh, (-42.0, 22.0, 4.0), (22.0, 20.0, 24.0), color, 0.0, seed + 2, 2, bumps=0.3, flatten=0.4, shade=0.5, flat=True)
    add_blob(mesh, (-34.0, -26.0, 3.0), (20.0, 18.0, 20.0), color, 0.0, seed + 3, 2, bumps=0.3, flatten=0.4, shade=0.5, flat=True)
    if crust:
        for index in range(7):
            angle = -1.8 + 3.6 * hash_unit(seed, index, 1)
            out = 26.0 + 10.0 * hash_unit(seed, index, 2)
            add_blob(mesh, (-32.0 + out * math.cos(angle) * 0.7, out * math.sin(angle), 14.0 + 16.0 * hash_unit(seed, index, 3)),
                (6.0, 5.0, 3.0), crust, 0.0, seed + 10 + index, 1, bumps=0.3, flatten=0.5, shade=0.3, flat=True)


def add_adit(mesh, seed):
    """The mouth of a mine in the front of the mound: a dark hollow in a frame of timbers under a little roof of planks, a lamp beside it,
    and rails from it on sleepers towards the place of the hero"""
    add_blob(mesh, (5.0, 0.0, 10.0), (9.0, 11.0, 14.0), DARK, 0.0, seed, 1, bumps=0.0, flatten=0.7, shade=0.0)
    for side in (-1.0, 1.0):
        timber(mesh, (12.0, side * 12.5, -SKIRT), (12.0, side * 11.5, 26.0), 2.2, seed + 1)
        timber(mesh, (12.5, side * 12.0, 12.0), (-1.0, side * 14.0, 21.0), 1.5, seed + 2)
    timber(mesh, (12.0, -16.0, 26.5), (12.0, 16.0, 26.5), 2.5, seed + 3)
    for side in (-1.0, 1.0):
        add_box(mesh, (9.0, side * 8.0, 31.0), (14.0, 17.0, 1.4), PLANK, yaw=0.0, roll=side * 0.45, seed=seed + 4)
    # a lamp on a hook beside the mouth, which glows
    add_cylinder(mesh, (15.0, 14.5, 18.0), (15.0, 14.5, 22.0), 1.3, 6, (1.0, 0.7, 0.3), GLOW, IRON, seed + 5)
    # the rails, two bars on sleepers
    for index in range(6):
        x = 14.0 + index * 3.4
        add_box(mesh, (x, 0.0, 0.4), (1.6, 11.0, 0.9), WOOD, yaw=0.0, seed=seed + 6 + index)
    for side in (-1.0, 1.0):
        add_box(mesh, (22.5, side * 3.6, 1.2), (21.0, 0.7, 0.8), IRON, seed=seed + 12)


def add_cart(mesh, center, yaw, seed, fill):
    """A mine cart of planks bound with iron on four wheels; fill(top) heaps what it carries over its middle"""
    def at(point):
        return add(center, rotated(point, yaw))

    add_box(mesh, at((0.0, 0.0, 6.0)), (16.0, 10.0, 1.2), PLANK, yaw=yaw, seed=seed)
    for side in (-1.0, 1.0):
        add_box(mesh, at((0.0, side * 4.8, 9.6)), (16.0, 1.1, 6.6), PLANK, yaw=yaw, seed=seed + 1)
        add_box(mesh, at((side * 7.6, 0.0, 9.6)), (1.1, 10.0, 6.6), PLANK, yaw=yaw, seed=seed + 2)
        add_box(mesh, at((side * 4.0, 0.0, 9.6)), (1.3, 10.6, 7.1), IRON, yaw=yaw, seed=seed + 3)
        for along in (-4.8, 4.8):
            add_cylinder(mesh, at((along, side * 5.0, 3.4)), at((along, side * 6.6, 3.4)), 3.4, 9, IRON, 0.0, PLANK, seed + 4)
    fill(at((0.0, 0.0, 12.0)))


def add_heap(mesh, center, color, seed, count=9, reach=9.0, sizes=(2.6, 5.6)):
    """A heap of lumps of a color lying about as decor: plain, no glow and no sparkles"""
    chunks(mesh, center, count, reach, sizes, seed, color=color, alpha=0.0)


def add_log_stack(mesh, center, yaw, seed, rows=(4, 3, 2), length=30.0, radius=3.4):
    """A stack of logs between stakes"""
    def at(point):
        return add(center, rotated(point, yaw))

    index = 0
    for row, count in enumerate(rows):
        for place in range(count):
            x = (place - (count - 1) * 0.5) * radius * 2.02
            z = radius + row * radius * 1.76
            slide = (hash_unit(seed, index, 1) - 0.5) * 4.0
            add_cylinder(mesh, at((x, -length * 0.5 + slide, z)), at((x, length * 0.5 + slide, z)), radius * (0.88 + 0.2 * hash_unit(seed, index, 2)), 8, WOOD, 0.0,
                WOOD_CUT, seed + index)
            index += 1
    for side in (-1.0, 1.0):
        add_cylinder(mesh, at((side * radius * (rows[0] + 0.3), 0.0, -SKIRT)), at((side * radius * (rows[0] + 0.1), 0.0, radius * 2.0 * len(rows))), 1.2, 6, WOOD, 0.0, WOOD_CUT,
            seed + 40)


def gold_mine():
    """An adit in a mound of rock with veins of gold, a cart of gold ore on its rails and heaps of gold ore and nuggets beside it"""
    mesh = Mesh()
    add_mound(mesh, ROCK, 300, crust=GOLD_DEEP)
    add_adit(mesh, 310)
    add_cart(mesh, (26.0, 0.0, 1.6), 0.0, 330, lambda top: chunks(mesh, top, 8, 4.5, (2.0, 3.4), 335, color=GOLD, alpha=0.0))
    add_heap(mesh, (6.0, -36.0, 0.0), ROCK, 340, 8, 9.0, (3.0, 5.5))
    chunks(mesh, (6.0, -36.0, 4.0), 6, 7.0, (1.6, 2.8), 341, color=GOLD, alpha=0.0)
    add_heap(mesh, (14.0, 32.0, 0.0), GOLD, 342, 7, 7.0, (2.0, 3.6))
    # a few ingots stacked by the heap
    for index, (x, y, z, yaw) in enumerate(((22.0, -30.0, 1.6, 0.3), (25.0, -26.0, 1.6, 0.45), (23.5, -28.0, 4.2, 1.9))):
        add_box(mesh, (x, y, z), (7.0, 3.0, 2.4), GOLD, 0.0, yaw=yaw, seed=345 + index)
    add_hero_patch(mesh, 350)
    add_owner_flag(mesh, -10.0, 36.0, 14.0, 80.0, 351)
    return mesh


def rich_gold_mine():
    """The gold mine of a pentagon, which gives far more: the mound gilded with veins, two carts heaped high, chests and stacks of ingots"""
    mesh = Mesh()
    add_mound(mesh, ROCK, 360, crust=GOLD)
    add_adit(mesh, 370)
    add_cart(mesh, (26.0, 0.0, 1.6), 0.0, 390, lambda top: chunks(mesh, top, 10, 5.0, (2.0, 3.8), 395, color=GOLD, alpha=GLOW))
    add_cart(mesh, (12.0, -28.0, 0.0), 0.7, 400, lambda top: chunks(mesh, top, 10, 5.0, (2.0, 3.8), 405, color=GOLD, alpha=GLOW))
    add_heap(mesh, (14.0, 32.0, 0.0), GOLD, 410, 10, 9.0, (2.4, 4.2))
    for index in range(8):
        x = 24.0 + (index % 4) * 3.2
        y = 22.0 + (index // 4) * 6.0
        add_box(mesh, (x, y, 1.6 + (index % 2) * 2.5), (7.0, 3.0, 2.4), GOLD, GLOW, yaw=0.2 * index, seed=415 + index)
    add_chest(mesh, (-6.0, -40.0, 0.0), 0.6, 425)
    add_blob(mesh, (-6.0, -40.0, 6.5), (7.4, 4.6, 3.6), GOLD, GLOW, 430, 1, bumps=0.25, flatten=0.4, shade=0.4)
    add_hero_patch(mesh, 431)
    add_owner_flag(mesh, -10.0, 36.0, 14.0, 88.0, 432)
    return mesh


def gem_mine():
    """An adit, a cart heaped with cut gems on its rails and little heaps of gems of many colors on a sorting table and beside it"""
    mesh = Mesh()
    add_mound(mesh, (0.22, 0.2, 0.23), 440)
    add_adit(mesh, 450)

    def gems(top, count, reach, seed):
        for index in range(count):
            angle = hash_unit(seed, index, 1) * 6.2832
            out = reach * hash_unit(seed, index, 2) ** 0.6
            add_gem(mesh, (top[0] + out * math.cos(angle), top[1] + out * math.sin(angle), top[2] + 1.6 + 1.2 * hash_unit(seed, index, 3)), 1.8 + 0.8 * hash_unit(seed, index, 4),
                GEMS[index % len(GEMS)], 0.0, seed * 7 + index)

    add_cart(mesh, (26.0, 0.0, 1.6), 0.0, 470, lambda top: gems(top, 14, 4.5, 475))
    # a sorting table of planks with gems on it, and some on the ground
    add_box(mesh, (10.0, -34.0, 7.0), (18.0, 10.0, 1.2), PLANK, yaw=0.3, seed=480)
    for x, y in ((3.0, -38.0), (17.0, -30.0), (4.0, -30.0), (16.0, -38.0)):
        add_box(mesh, (x, y, 3.2), (1.2, 1.2, 7.0), WOOD, seed=481)
    gems((10.0, -34.0, 7.6), 10, 6.0, 482)
    gems((16.0, 32.0, 0.0), 12, 7.0, 483)
    add_heap(mesh, (24.0, 34.0, 0.0), (0.3, 0.28, 0.3), 484, 6, 6.0, (2.4, 4.2))
    add_hero_patch(mesh, 490)
    add_owner_flag(mesh, -10.0, 36.0, 14.0, 80.0, 491)
    return mesh


def crystal_cave():
    """A cave in a mound of dark rock, its mouth ringed with clusters of red crystals that glow, crystals heaped in a cart and on the ground"""
    mesh = Mesh()
    add_mound(mesh, (0.17, 0.14, 0.16), 500)
    add_blob(mesh, (4.0, 0.0, 11.0), (10.0, 14.0, 16.0), DARK, 0.0, 510, 1, bumps=0.1, flatten=0.6, shade=0.0)
    for index in range(9):
        angle = math.pi * (0.1 + 0.8 * index / 8.0)
        base = (9.0 + 2.0 * math.sin(angle), -17.0 * math.cos(angle), 2.0 + 19.0 * math.sin(angle))
        direction = (0.55, -0.6 * math.cos(angle), 0.6 * math.sin(angle) + 0.2)
        add_crystal(mesh, base, direction, 9.0 + 7.0 * hash_unit(511, index), 2.2 + 1.2 * hash_unit(512, index), CRYSTAL_ROOT, CRYSTAL_TIP, GLOW, 513 + index)
    add_cart(mesh, (26.0, 2.0, 0.0), 0.2, 530, lambda top: [add_crystal(mesh, add(top, (hash_unit(535, i) * 6.0 - 3.0, hash_unit(536, i) * 4.0 - 2.0, -1.0)),
        (hash_unit(537, i) - 0.5, hash_unit(538, i) - 0.5, 1.0), 6.0, 1.6, CRYSTAL_ROOT, CRYSTAL_TIP, 0.0, 540 + i) for i in range(6)])
    for index in range(7):
        x = 10.0 + 14.0 * hash_unit(550, index, 1)
        y = -34.0 + 8.0 * hash_unit(550, index, 2)
        add_crystal(mesh, (x, y, 0.0), (hash_unit(551, index) - 0.5, hash_unit(552, index) - 0.5, 1.0), 5.0 + 4.0 * hash_unit(553, index), 1.4, CRYSTAL_ROOT, CRYSTAL_TIP, 0.0,
            560 + index)
    add_heap(mesh, (14.0, 32.0, 0.0), (0.45, 0.05, 0.08), 570, 7, 7.0, (2.0, 3.8))
    add_hero_patch(mesh, 575)
    add_owner_flag(mesh, -10.0, 36.0, 14.0, 80.0, 576)
    return mesh


def sulfur_mine():
    """An adit in rock crusted yellow, vents that ring with sulfur, heaps of it and open sacks"""
    mesh = Mesh()
    add_mound(mesh, (0.28, 0.25, 0.17), 580, crust=SULFUR)
    add_adit(mesh, 590)
    for index, (x, y) in enumerate(((-6.0, 30.0), (-14.0, -30.0), (0.0, -44.0))):
        add_blob(mesh, (x, y, 0.4), (6.0, 6.0, 1.6), SULFUR_DEEP, 0.0, 600 + index, 1, bumps=0.3, flatten=0.4, shade=0.3)
        add_blob(mesh, (x, y, 1.2), (2.6, 2.6, 1.0), DARK, 0.0, 610 + index, 1, bumps=0.0, flatten=0.4, shade=0.0)
    add_cart(mesh, (26.0, 0.0, 1.6), 0.0, 620, lambda top: chunks(mesh, top, 9, 4.5, (2.0, 3.4), 625, color=SULFUR, alpha=0.0))
    add_heap(mesh, (10.0, -34.0, 0.0), SULFUR, 630, 10, 9.0, (3.0, 6.0))
    add_heap(mesh, (16.0, 32.0, 0.0), SULFUR_DEEP, 631, 7, 7.0, (2.4, 4.4))
    for index, (x, y) in enumerate(((24.0, -26.0), (26.0, -18.0))):
        add_sack(mesh, (x, y, 0.0), 3.4, 632 + index * 2, top_color=SULFUR)
    add_hero_patch(mesh, 640)
    add_owner_flag(mesh, -10.0, 38.0, 14.0, 80.0, 641)
    return mesh


def quarry():
    """A quarry where ore is dug: a cliff cut in steps in the back of the cell, blocks of stone stacked, a wooden crane with its rope, a cart
    of ore and heaps of rusty ore"""
    mesh = Mesh()
    stone = (0.3, 0.27, 0.24)
    add_blob(mesh, (-30.0, 0.0, -8.0), (46.0, 52.0, 14.0), stone, 0.0, 650, 2, bumps=0.15, flatten=1.0, shade=0.4, flat=True)
    # the steps of the cut face, each lower and further forward
    for step, (x, z, height) in enumerate(((-46.0, 18.0, 44.0), (-34.0, 12.0, 30.0), (-22.0, 6.0, 18.0), (-12.0, 2.0, 8.0))):
        add_box(mesh, (x, 0.0, z), (14.0, 64.0 - step * 6.0, height), stone, yaw=0.0, seed=651 + step)
        # rust in the cut, the vein of ore
        add_box(mesh, (x + 7.1, 6.0 - step * 4.0, z + height * 0.15), (0.6, 10.0, 4.0), ORE_RUST, yaw=0.0, seed=660 + step)
    # blocks cut from it, stacked
    for index, (x, y, z) in enumerate(((2.0, -32.0, 3.5), (10.0, -32.0, 3.5), (6.0, -32.0, 10.5), (2.0, -24.0, 3.5))):
        add_box(mesh, (x, y, z), (7.6, 7.0, 7.0), lerp(stone, (0.45, 0.42, 0.38), 0.5), yaw=0.05 * index, seed=670 + index)
    # the crane: an A-frame with a boom, a rope and a stone hanging from it
    timber(mesh, (-8.0, 22.0, -SKIRT), (-4.0, 26.0, 40.0), 1.6, 680)
    timber(mesh, (0.0, 30.0, -SKIRT), (-4.0, 26.0, 40.0), 1.6, 681)
    timber(mesh, (-4.0, 26.0, 38.0), (22.0, 20.0, 34.0), 1.3, 682)
    add_tube(mesh, [(22.0, 20.0, 34.0), (22.0, 20.0, 16.0)], [0.35, 0.35], ROPE, [0.0, 0.0], 683, 4, False, bark=0.0)
    add_box(mesh, (22.0, 20.0, 13.0), (6.0, 6.0, 5.0), stone, seed=684)
    add_cart(mesh, (24.0, -6.0, 0.0), 0.15, 690, lambda top: ore_chunks(mesh, top, 9, 4.5, (2.0, 3.2), 695, heap=2.0))
    for index, (x, y) in enumerate(((14.0, 36.0), (28.0, -30.0))):
        center = (x, y, 0.5)
        for chunk in range(9):
            angle = hash_unit(700 + index, chunk, 1) * 6.2832
            out = 8.0 * hash_unit(700 + index, chunk, 2) ** 0.6
            size = 2.6 + 3.0 * hash_unit(700 + index, chunk, 3)
            pick = hash_unit(700 + index, chunk, 4)
            color = ORE_BRIGHT if pick > 0.8 else ORE_RUST if pick > 0.45 else ORE
            add_blob(mesh, (center[0] + out * math.cos(angle), center[1] + out * math.sin(angle), size * 0.5 + 4.0 * max(1.0 - out / 8.0, 0.0)),
                (size, size * 0.85, size * 0.8), color, 0.0, 710 + index * 20 + chunk, 1, bumps=0.42, flatten=0.75, shade=0.5, flat=True)
    add_hero_patch(mesh, 740)
    add_owner_flag(mesh, -20.0, -30.0, 14.0, 84.0, 741)
    return mesh


def mercury_lab():
    """An alchemist's lab: a hut of stone with a chimney under a roof of planks, a table with an alembic and flasks of quicksilver before it,
    barrels and a silver puddle"""
    mesh = Mesh()
    stone = (0.34, 0.32, 0.3)
    add_blob(mesh, (-26.0, 0.0, -6.0), (34.0, 36.0, 9.0), ROCK, 0.0, 750, 2, bumps=0.15, flatten=1.0, shade=0.4, flat=True)
    add_box(mesh, (-26.0, 0.0, 12.0), (30.0, 36.0, 24.0), stone, seed=751)
    add_blob(mesh, (-10.6, 0.0, 9.0), (1.6, 5.0, 8.0), DARK, 0.0, 752, 1, bumps=0.0, flatten=0.9, shade=0.0)
    for side in (-1.0, 1.0):
        add_box(mesh, (-26.0, side * 9.5, 30.0), (34.0, 21.0, 1.6), PLANK, roll=side * 0.6, seed=753)
        # a window that glows
        add_box(mesh, (-10.8, side * 11.0, 14.0), (0.8, 4.0, 4.0), (0.9, 0.85, 0.6), GLOW, seed=754)
    add_box(mesh, (-34.0, 12.0, 36.0), (6.0, 6.0, 20.0), STONE_DARK, seed=755)
    # the table with an alembic: a round still, a long neck and a flask that catches what it gives
    add_box(mesh, (4.0, -24.0, 8.0), (16.0, 10.0, 1.2), PLANK, yaw=0.2, seed=760)
    for x, y in ((-2.0, -28.0), (10.0, -21.0), (-2.0, -20.0), (10.0, -28.0)):
        add_box(mesh, (x, y, 3.6), (1.2, 1.2, 8.0), WOOD, seed=761)
    add_blob(mesh, (0.0, -24.0, 12.0), (3.6, 3.6, 3.6), (0.55, 0.4, 0.25), 0.0, 762, 1, bumps=0.0, flatten=0.9, shade=0.3)
    add_tube(mesh, [(0.0, -24.0, 15.0), (5.0, -24.5, 18.0), (9.0, -25.0, 13.0)], [0.6, 0.5, 0.45], GLASS, [0.0, 0.0, 0.0], 763, 5, True, bark=0.0)
    add_flask(mesh, (9.0, -25.0, 8.6), 2.4, 764)
    for index, (x, y, size) in enumerate(((14.0, 30.0, 3.4), (19.0, 33.0, 2.8), (11.0, 36.0, 3.0), (22.0, 27.0, 2.6))):
        add_flask(mesh, (x, y, 0.4), size, 770 + index * 3)
    for index, (x, y) in enumerate(((-6.0, 24.0), (-2.0, 30.0))):
        add_cylinder(mesh, (x, y, -1.0), (x, y, 9.0), 3.6, 10, WOOD, 0.0, WOOD_CUT, 790 + index)
    add_blob(mesh, (20.0, -36.0, 0.3), (7.0, 4.5, 1.0), SILVER, 0.0, 795, 2, bumps=0.3, flatten=0.3, shade=0.1)
    add_hero_patch(mesh, 800)
    add_owner_flag(mesh, -16.0, -22.0, 24.0, 90.0, 801)
    return mesh


def sawmill():
    """A sawmill: a hut of logs under a roof of planks, a bench before it with a log on it under a great round saw, a water wheel at its side,
    stacks of logs and planks and a patch of sawdust"""
    mesh = Mesh()
    add_blob(mesh, (-28.0, 0.0, -6.0), (34.0, 38.0, 9.0), EARTH, 0.0, 810, 2, bumps=0.1, flatten=1.0, shade=0.3, flat=True)
    # the walls of logs, laid in courses
    for course in range(6):
        z = 2.5 + course * 4.4
        for side in (-1.0, 1.0):
            add_cylinder(mesh, (-44.0, side * 15.0, z), (-12.0, side * 15.0, z), 2.3, 7, WOOD, 0.0, WOOD_CUT, 811 + course)
        for x in (-44.0, -12.0):
            add_cylinder(mesh, (x, -17.0, z + 2.2), (x, 17.0, z + 2.2), 2.3, 7, WOOD, 0.0, WOOD_CUT, 820 + course)
    add_blob(mesh, (-11.0, 0.0, 9.0), (1.4, 6.0, 9.0), DARK, 0.0, 830, 1, bumps=0.0, flatten=0.9, shade=0.0)
    for side in (-1.0, 1.0):
        add_box(mesh, (-28.0, side * 9.0, 33.0), (40.0, 22.0, 1.6), PLANK, roll=side * 0.55, seed=831)
    # the water wheel at its side, paddles around a hub
    hub = (-28.0, 22.0, 14.0)
    add_cylinder(mesh, (hub[0], hub[1] - 2.0, hub[2]), (hub[0], hub[1] + 3.0, hub[2]), 1.6, 8, IRON, 0.0, IRON, 832)
    for paddle in range(8):
        angle = paddle * math.pi / 4.0
        end = (hub[0] + 13.0 * math.cos(angle), hub[1] + 0.5, hub[2] + 13.0 * math.sin(angle))
        add_box(mesh, (hub[0] + 6.5 * math.cos(angle), hub[1] + 0.5, hub[2] + 6.5 * math.sin(angle)), (13.0, 1.0, 1.0), WOOD, pitch=-angle, seed=833 + paddle)
        add_box(mesh, end, (4.0, 5.0, 1.0), PLANK, pitch=-angle + math.pi * 0.5, seed=841 + paddle)
    # the bench with a log on it and the great round saw through it
    add_box(mesh, (6.0, -18.0, 6.0), (26.0, 7.0, 1.6), PLANK, seed=850)
    for x, y in ((-6.0, -21.0), (18.0, -15.0), (-6.0, -15.0), (18.0, -21.0)):
        add_box(mesh, (x, y, 2.6), (1.4, 1.4, 6.0), WOOD, seed=851)
    add_cylinder(mesh, (-6.0, -18.0, 9.2), (18.0, -18.0, 9.2), 2.8, 8, WOOD, 0.0, WOOD_CUT, 852)
    add_cylinder(mesh, (6.0, -19.0, 11.0), (6.0, -17.0, 11.0), 7.5, 16, ORE_BRIGHT, 0.0, ORE_BRIGHT, 853)
    add_cylinder(mesh, (6.0, -19.6, 11.0), (6.0, -16.4, 11.0), 1.4, 8, IRON, 0.0, IRON, 854)
    # stacks of logs and of planks, sawdust
    add_log_stack(mesh, (14.0, 30.0, 0.0), 0.15, 860)
    for index in range(6):
        add_box(mesh, (24.0, -36.0 + 0.0 * index, 1.2 + index * 1.5), (28.0, 6.0, 1.3), WOOD_CUT, yaw=0.1 + 0.04 * index, seed=880 + index)
    add_blob(mesh, (14.0, -10.0, 0.0), (9.0, 6.0, 0.8), (0.72, 0.58, 0.38), 0.0, 890, 1, bumps=0.2, flatten=0.4, shade=0.2)
    add_hero_patch(mesh, 891)
    add_owner_flag(mesh, -28.0, 0.0, 34.0, 96.0, 892)
    return mesh


def flotsam_wood():
    """Timber that floats: logs lashed together and loose planks around them, a coil of rope on the logs, its waterline at 0 and at the scale
    of the boat; the sparkles of a pile, as a hero takes it"""
    mesh = Mesh()
    for index in range(4):
        y = (index - 1.5) * 3.2
        slide = (hash_unit(900, index, 1) - 0.5) * 4.0
        add_cylinder(mesh, (-8.0 + slide, y, 0.4), (8.0 + slide, y, 0.4), 1.5 + 0.3 * hash_unit(900, index, 2), 8, WOOD, GLOW, WOOD_CUT, 901 + index)
    for x in (-5.0, 5.0):
        add_tube(mesh, [(x, -6.5, 1.6), (x, 6.5, 1.6)], [0.3, 0.3], ROPE, [0.0, 0.0], 910, 4, False, bark=0.0)
    for index in range(5):
        angle = hash_unit(920, index, 1) * 6.2832
        out = 11.0 + 5.0 * hash_unit(920, index, 2)
        add_box(mesh, (out * math.cos(angle), out * math.sin(angle), 0.1), (8.0, 1.6, 0.5), WOOD_CUT, GLOW, yaw=angle * 1.3, seed=921 + index)
    add_cylinder(mesh, (2.0, 1.0, 1.8), (2.0, 1.0, 2.6), 2.2, 8, ROPE, 0.0, ROPE, 930)
    add_sparkles(mesh, 6, 14.0, (3.0, 12.0), 931, 16.0)
    return mesh


def flotsam_gold():
    """Cargo that floats: barrels bobbing half under, a crate broken open with gold glinting in it and coins on its lid, its waterline at 0 and
    at the scale of the boat; the sparkles of a pile"""
    mesh = Mesh()
    for index, (x, y, tilt) in enumerate(((-7.0, 4.0, 0.4), (6.0, 6.0, -0.3), (-2.0, -8.0, 0.2))):
        start = (x, y, -1.5)
        end = add(start, rotated((0.0, 0.0, 6.5), 0.0, tilt, 0.3 * index))
        add_cylinder(mesh, start, end, 2.6, 10, WOOD, 0.0, WOOD_CUT, 940 + index)
        for share in (0.2, 0.8):
            band = add(start, mul(sub(end, start), share))
            add_cylinder(mesh, add(band, (0.0, 0.0, -0.3)), add(band, (0.0, 0.0, 0.3)), 2.75, 10, IRON, 0.0, IRON, 945 + index)
    add_box(mesh, (3.0, -2.0, 0.6), (7.0, 6.0, 4.0), PLANK, yaw=0.4, seed=950)
    add_blob(mesh, (3.0, -2.0, 2.8), (3.0, 2.6, 1.4), GOLD, GLOW, 951, 1, bumps=0.25, flatten=0.4, shade=0.4)
    for index in range(5):
        base = (1.0 + 4.0 * hash_unit(952, index, 1), -4.0 + 4.0 * hash_unit(952, index, 2), 2.7)
        add_cylinder(mesh, base, add(base, (0.0, 0.0, 0.4)), 1.0, 8, GOLD, GLOW, GOLD, 953 + index)
    add_sparkles(mesh, 7, 13.0, (3.0, 12.0), 960, 16.0)
    return mesh


# a pile of a kind of DA_HexSites is named after the Id of that kind, so import_planet_sites.py gives it to the kind
MESHES = [('Mine', mine), ('Pile', pile), ('Mill', mill), ('GoldPile', gold_pile), ('WoodPile', wood_pile), ('OrePile', ore_pile),
    ('MercuryPile', mercury_pile), ('SulfurPile', sulfur_pile), ('CrystalPile', crystal_pile), ('GemPile', gem_pile), ('Chest', treasure_chest),
    ('Artifact', artifact), ('GreatArtifact', great_artifact), ('Shipyard', shipyard), ('Boat', boat), ('Wreck', wreck),
    ('GoldMine', gold_mine), ('RichGoldMine', rich_gold_mine), ('Sawmill', sawmill), ('OreMine', quarry), ('MercuryLab', mercury_lab),
    ('SulfurMine', sulfur_mine), ('CrystalCave', crystal_cave), ('GemMine', gem_mine), ('FlotsamWood', flotsam_wood), ('FlotsamGold', flotsam_gold)]


def main():
    # words after the file name write only the meshes whose names hold one
    only = sys.argv[1:]
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    manifest = {'meshes': []}
    for name, make in MESHES:
        if only and not any(word.lower() in name.lower() for word in only):
            continue
        asset = 'SM_Planet' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, True)
        manifest['meshes'].append({'name': asset, 'kind': name, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'sites.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=1)
    print('written to', OUTPUT_FOLDER)


if __name__ == '__main__':
    main()
