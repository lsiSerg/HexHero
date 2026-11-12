"""Generates the meshes of the heroes of the hex planet (UHexHeroes): a rider with a banner and the marks of its way.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_heroes.py
Writes GLB files and heroes.json to Saved/HexPlanetHeroes, then run import_planet_heroes.py with the commandlet, after create_planet_assets.py
(or its argument heroes) made the materials. The GLB files open in Blender, so a mesh can be reworked by hand, and the hero is a placeholder
for one an artist makes: HeroMesh of DA_HexHeroes, or Mesh of a class there.

The hero stands on its origin, X forward and Z up, in world units. The color of a vertex is its color, and its alpha how much of the color of
its player it takes (M_HexPlanetHero): the cloak of the rider, the cloth under the saddle, the plume and the banner. A mark is flat, of radius 1
around its origin, seen from above: the red of a vertex is how bright it is, 0 on the dark rim that sets it off the ground, and its alpha how
much it covers (M_HexPlanetHeroMark).
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'HexPlanetObjects')))
from generate_planet_objects import Mesh, add_blob, add_tube, write_glb  # noqa: E402

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetHeroes'))
UP = (0.0, 0.0, 1.0)

HORSE = (0.2, 0.115, 0.065)
DARK_HAIR = (0.035, 0.028, 0.022)
STEEL = (0.4, 0.42, 0.46)
LEATHER = (0.11, 0.065, 0.04)
WOOD = (0.24, 0.16, 0.09)
GOLD = (0.75, 0.52, 0.14)
# the cloth is nearly white, so the color of the player shows as it is
CLOTH = (0.92, 0.92, 0.92)


def smooth_tube(mesh, path, radii, color, alpha, seed, segments=8, cap=True):
    add_tube(mesh, path, radii, color, [alpha] * len(path), seed, segments, cap, bark=0.06)


def hero():
    """A rider in armor on a horse, a cloak on the shoulders and a long banner in the right hand"""
    mesh = Mesh()

    # the horse: a barrel, a neck that rises to the head, four legs and a tail
    add_blob(mesh, (0.0, 0.0, 13.2), (9.2, 4.3, 4.7), HORSE, 0.0, 3, 2, bumps=0.05, flatten=1.0, shade=0.55)
    add_blob(mesh, (6.0, 0.0, 13.8), (4.4, 4.0, 4.9), HORSE, 0.0, 4, 2, bumps=0.05, flatten=1.0, shade=0.55)
    add_blob(mesh, (-5.8, 0.0, 13.6), (4.8, 4.4, 5.0), HORSE, 0.0, 5, 2, bumps=0.05, flatten=1.0, shade=0.55)
    smooth_tube(mesh, [(7.0, 0.0, 15.0), (9.6, 0.0, 19.2), (11.6, 0.0, 23.0)], [3.6, 2.9, 2.2], HORSE, 0.0, 6)
    smooth_tube(mesh, [(10.6, 0.0, 23.6), (13.4, 0.0, 22.0), (16.2, 0.0, 19.4)], [2.3, 2.0, 1.3], HORSE, 0.0, 7)
    for side in (-1.0, 1.0):
        # ears
        smooth_tube(mesh, [(10.6, side * 1.2, 25.0), (10.2, side * 1.5, 26.8)], [0.6, 0.15], HORSE, 0.0, 8, 5)
        # front and hind legs, bent a little at the knee and the hock
        smooth_tube(mesh, [(6.6, side * 2.5, 11.5), (7.2, side * 2.6, 6.0), (6.8, side * 2.6, 0.9)], [1.9, 1.15, 1.0], HORSE, 0.0, 9, 6)
        smooth_tube(mesh, [(-6.6, side * 2.7, 12.0), (-8.0, side * 2.8, 6.4), (-7.0, side * 2.8, 0.9)], [2.3, 1.25, 1.0], HORSE, 0.0, 10, 6)
        for x in (6.8, -7.0):
            add_blob(mesh, (x + 0.3, side * 2.7, 0.7), (1.4, 1.2, 0.8), DARK_HAIR, 0.0, 11, 1, bumps=0.0, flatten=0.9, shade=0.3)
    # the mane and the tail
    smooth_tube(mesh, [(7.2, 0.0, 18.6), (9.4, 0.0, 21.6), (10.9, 0.0, 25.2)], [0.9, 1.2, 0.8], DARK_HAIR, 0.0, 12, 5)
    smooth_tube(mesh, [(-9.6, 0.0, 15.4), (-12.4, 0.0, 13.0), (-13.4, 0.0, 7.4), (-12.8, 0.0, 3.4)], [1.1, 1.6, 1.5, 0.5], DARK_HAIR, 0.0, 13, 6)

    # the cloth under the saddle, in the color of the player, hanging down both flanks
    add_blob(mesh, (-0.8, 0.0, 13.4), (6.0, 4.75, 4.9), CLOTH, 1.0, 14, 2, bumps=0.03, flatten=0.62, shade=0.5)
    add_blob(mesh, (-0.4, 0.0, 17.6), (3.4, 2.6, 1.3), LEATHER, 0.0, 15, 1, bumps=0.05, flatten=1.0, shade=0.4)

    # the rider: legs down the flanks, a breastplate, arms and a helmet with a plume
    for side in (-1.0, 1.0):
        smooth_tube(mesh, [(-0.2, side * 2.2, 18.6), (2.2, side * 4.6, 14.2), (1.6, side * 4.9, 9.6)], [1.7, 1.35, 1.0], STEEL, 0.0, 16, 6)
        add_blob(mesh, (2.3, side * 4.9, 9.0), (1.7, 0.9, 0.8), LEATHER, 0.0, 17, 1, bumps=0.0, flatten=0.9, shade=0.3)
    smooth_tube(mesh, [(-0.6, 0.0, 17.8), (-0.2, 0.0, 21.6), (0.2, 0.0, 25.6), (0.3, 0.0, 26.8)], [2.7, 2.9, 3.3, 1.6], STEEL, 0.0, 18, 8)
    add_blob(mesh, (0.5, 0.0, 28.8), (1.95, 1.8, 2.05), STEEL, 0.0, 19, 2, bumps=0.0, flatten=1.0, shade=0.45)
    smooth_tube(mesh, [(0.2, 0.0, 30.4), (-0.9, 0.0, 32.4), (-2.6, 0.0, 32.0)], [0.75, 1.0, 0.35], CLOTH, 1.0, 20, 6)
    # the left hand on the reins, the right on the pole of the banner
    smooth_tube(mesh, [(0.3, 3.2, 25.2), (2.4, 3.4, 21.6), (5.2, 1.6, 20.4)], [1.25, 1.0, 0.85], STEEL, 0.0, 21, 6)
    smooth_tube(mesh, [(0.3, -3.2, 25.2), (2.0, -4.3, 22.4), (3.9, -4.6, 21.8)], [1.25, 1.0, 0.85], STEEL, 0.0, 22, 6)

    # the cloak, in the color of the player, from the shoulders down over the back of the horse
    smooth_tube(mesh, [(-1.2, 0.0, 26.2), (-2.6, 0.0, 23.4), (-4.6, 0.0, 19.4), (-6.6, 0.0, 16.6)], [2.9, 3.6, 4.2, 4.5], CLOTH, 1.0, 23, 9)

    # the banner: a pole with a gilded tip and a long swallow-tailed flag, seen from both sides
    smooth_tube(mesh, [(3.9, -4.6, 11.5), (3.9, -4.6, 44.5)], [0.42, 0.36], WOOD, 0.0, 24, 5)
    add_blob(mesh, (3.9, -4.6, 45.6), (0.75, 0.75, 1.5), GOLD, 0.0, 25, 1, bumps=0.0, flatten=1.0, shade=0.3)
    mesh.begin_part()
    flag = [((3.9, -4.6, 43.6), 1.0), ((-2.2, -4.9, 44.0), 0.94), ((-9.0, -4.4, 43.2), 0.86), ((-5.6, -4.8, 38.9), 0.8), ((-9.4, -4.5, 34.4), 0.86),
        ((-2.2, -4.3, 34.0), 0.94), ((3.9, -4.6, 34.2), 1.0), ((0.6, -4.7, 38.9), 0.97)]
    normal = (0.0, -1.0, 0.25)
    indices = [mesh.vertex(point, (CLOTH[0] * tone, CLOTH[1] * tone, CLOTH[2] * tone), 1.0, normal=normal) for point, tone in flag]
    center = indices[7]
    rim = indices[:7]
    for index in range(len(rim) - 1):
        mesh.sheet(center, rim[index], rim[index + 1])
    mesh.sheet(center, rim[-1], rim[0])
    return mesh


def add_rings(mesh, rows, segments):
    """Flat bands between circles around the origin, each row (radius, brightness, alpha); a radius of 0 is the middle of a disc"""
    mesh.begin_part()
    rings = []
    for radius, bright, alpha in rows:
        if radius <= 0.0:
            rings.append([mesh.vertex((0.0, 0.0, 0.0), (bright, bright, bright), alpha, normal=UP)] * segments)
            continue
        rings.append([mesh.vertex((math.cos(2.0 * math.pi * step / segments) * radius, math.sin(2.0 * math.pi * step / segments) * radius, 0.0),
            (bright, bright, bright), alpha, normal=UP) for step in range(segments)])
    for inner, outer in zip(rings, rings[1:]):
        for step in range(segments):
            following = (step + 1) % segments
            if inner[step] != inner[following]:
                mesh.sheet(inner[step], outer[step], inner[following])
            mesh.sheet(inner[following], outer[step], outer[following])


def dot():
    """A step of a way: a bright disc with a dark rim"""
    mesh = Mesh()
    add_rings(mesh, [(0.0, 1.0, 1.0), (0.68, 1.0, 1.0), (0.82, 0.0, 0.75), (1.0, 0.0, 0.0)], 20)
    return mesh


def target():
    """The end of a way: a disc in a ring, as a target"""
    mesh = Mesh()
    add_rings(mesh, [(0.0, 1.0, 1.0), (0.26, 1.0, 1.0), (0.34, 0.0, 0.7), (0.42, 0.0, 0.0)], 28)
    add_rings(mesh, [(0.56, 0.0, 0.0), (0.64, 0.0, 0.7), (0.7, 1.0, 1.0), (0.86, 1.0, 1.0), (0.92, 0.0, 0.7), (1.0, 0.0, 0.0)], 40)
    return mesh


def portal():
    """A way through an entrance: a hoop with a swirl of three arms in it, which stands upright in the mouth, as a gate to another place"""
    mesh = Mesh()
    add_rings(mesh, [(0.74, 0.0, 0.0), (0.8, 0.0, 0.7), (0.85, 1.0, 1.0), (0.94, 1.0, 1.0), (0.97, 0.0, 0.7), (1.0, 0.0, 0.0)], 40)
    mesh.begin_part()
    steps = 14
    for arm in range(3):
        rows = []
        for step in range(steps + 1):
            share = step / steps
            radius = 0.1 + 0.56 * share
            angle = arm * 2.0 * math.pi / 3.0 + share * 2.6
            half = 0.085 * (1.0 - 0.75 * share) + 0.012
            along = (math.cos(angle), math.sin(angle))
            rows.append([mesh.vertex(((radius + side * half) * along[0], (radius + side * half) * along[1], 0.0), (1.0, 1.0, 1.0), 1.0, normal=UP) for side in (-1.0, 1.0)])
        for near, far in zip(rows, rows[1:]):
            mesh.sheet(near[0], near[1], far[1])
            mesh.sheet(near[0], far[1], far[0])
    return mesh


def portal_ring():
    """The hoop of a way through an entrance alone, without its swirl: the hoops that run from it along the way"""
    mesh = Mesh()
    add_rings(mesh, [(0.74, 0.0, 0.0), (0.8, 0.0, 0.7), (0.85, 1.0, 1.0), (0.94, 1.0, 1.0), (0.97, 0.0, 0.7), (1.0, 0.0, 0.0)], 40)
    return mesh


def line():
    """A piece of the line of a way: a strip one unit long along X and one wide, bright in its middle with a dark rim along both sides"""
    mesh = Mesh()
    mesh.begin_part()
    rows = []
    for y, bright, alpha in ((-0.5, 0.0, 0.0), (-0.36, 0.0, 0.7), (-0.24, 1.0, 1.0), (0.24, 1.0, 1.0), (0.36, 0.0, 0.7), (0.5, 0.0, 0.0)):
        rows.append([mesh.vertex((x, y, 0.0), (bright, bright, bright), alpha, normal=UP) for x in (-0.5, 0.5)])
    for near, far in zip(rows, rows[1:]):
        mesh.sheet(near[0], near[1], far[1])
        mesh.sheet(near[0], far[1], far[0])
    return mesh


# the ring under the hero chosen is no mesh of these: UHexHeroes::BuildRing lays it on the ground around the hero
MESHES = [('Hero', hero, 'hero'), ('HeroDot', dot, 'path'), ('HeroTarget', target, 'path'), ('HeroLine', line, 'path'), ('HeroPortal', portal, 'path'), ('HeroPortalRing', portal_ring, 'path')]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    manifest = {'meshes': []}
    for name, make, material in MESHES:
        asset = 'SM_Planet' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, False)
        manifest['meshes'].append({'name': asset, 'material': material, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'heroes.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=1)
    print('written to', OUTPUT_FOLDER)


if __name__ == '__main__':
    main()
