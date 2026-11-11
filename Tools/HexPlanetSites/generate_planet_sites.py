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


# a pile of a kind of DA_HexSites is named after the Id of that kind, so import_planet_sites.py gives it to the kind
MESHES = [('Mine', mine), ('Pile', pile), ('Mill', mill), ('GoldPile', gold_pile), ('WoodPile', wood_pile), ('OrePile', ore_pile),
    ('MercuryPile', mercury_pile), ('SulfurPile', sulfur_pile), ('CrystalPile', crystal_pile), ('GemPile', gem_pile)]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    manifest = {'meshes': []}
    for name, make in MESHES:
        asset = 'SM_Planet' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, True)
        manifest['meshes'].append({'name': asset, 'kind': name, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'sites.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=1)
    print('written to', OUTPUT_FOLDER)


if __name__ == '__main__':
    main()
