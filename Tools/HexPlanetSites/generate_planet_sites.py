"""Generates the meshes of the mines and piles of the hex planet (UHexSites).

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_sites.py
Writes GLB files and sites.json to Saved/HexPlanetSites, then run import_planet_sites.py with the commandlet, after create_planet_assets.py
(or its argument heroes) made M_HexPlanetSite. The GLB files open in Blender, so a mesh can be reworked by hand; both are placeholders for the
meshes of an artist: MineMesh and PileMesh of DA_HexSites, or Mesh of a kind there.

A mesh stands on its origin, X forward and Z up, in world units. The color of a vertex is its color, and its alpha what the material tints it
with (M_HexPlanetSite): 0 nothing, 0.5 the color of the resource, on the heap of a mine and the chunks of a pile, 1 the color of the owner, on
the flag of a mine. One mine serves every resource: a mound of rock with a timbered mouth, a heap of what it digs beside it and a flag.
"""

import json
import math
import os
import sys

sys.path.insert(0, os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'HexPlanetObjects')))
from generate_planet_objects import Mesh, add_blob, add_tube, hash_unit, write_glb  # noqa: E402

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetSites'))

ROCK = (0.2, 0.18, 0.16)
DARK = (0.012, 0.011, 0.01)
WOOD = (0.22, 0.14, 0.08)
# what the material tints is nearly white, so the color of the resource or the owner shows as it is
TINTED = (0.9, 0.9, 0.9)
RESOURCE = 0.5
OWNER = 1.0


def timber(mesh, start, end, radius, seed):
    add_tube(mesh, [start, end], [radius, radius], WOOD, [0.0, 0.0], seed, 6, True, bark=0.12)


def chunks(mesh, center, count, reach, sizes, seed):
    """A heap of chunks of a resource around a point, the larger low and in the middle"""
    for index in range(count):
        angle = hash_unit(seed, index, 1) * 6.2832
        out = reach * (hash_unit(seed, index, 2) ** 0.7) * (0.25 + 0.75 * index / max(count - 1, 1))
        size = sizes[1] - (sizes[1] - sizes[0]) * index / max(count - 1, 1)
        position = (center[0] + out * math.cos(angle), center[1] + out * math.sin(angle), center[2] + size * 0.55)
        add_blob(mesh, position, (size, size * (0.8 + 0.4 * hash_unit(seed, index, 3)), size * (0.75 + 0.4 * hash_unit(seed, index, 4))), TINTED, RESOURCE,
            seed * 31 + index, 1, bumps=0.3, flatten=0.7, shade=0.45, flat=True)


def mine():
    """A mound of rock with a dark mouth framed by timbers, a heap of what is dug beside it and a flag on a pole"""
    mesh = Mesh()
    add_blob(mesh, (-16.0, 0.0, 6.0), (42.0, 40.0, 34.0), ROCK, 0.0, 41, 2, bumps=0.3, flatten=0.18, shade=0.5, flat=True)
    add_blob(mesh, (-26.0, 20.0, 4.0), (24.0, 22.0, 24.0), ROCK, 0.0, 42, 2, bumps=0.3, flatten=0.18, shade=0.5, flat=True)
    add_blob(mesh, (-18.0, -24.0, 3.0), (22.0, 20.0, 20.0), ROCK, 0.0, 43, 2, bumps=0.3, flatten=0.18, shade=0.5, flat=True)
    # the mouth, a dark hollow in the front of the mound, and its frame
    add_blob(mesh, (26.0, 0.0, 9.0), (9.0, 10.5, 13.5), DARK, 0.0, 44, 1, bumps=0.0, flatten=0.7, shade=0.0)
    for side in (-1.0, 1.0):
        timber(mesh, (33.0, side * 12.5, 0.0), (33.0, side * 11.5, 25.0), 2.1, 45)
        timber(mesh, (33.5, side * 12.0, 12.0), (20.0, side * 14.0, 20.0), 1.5, 46)
    timber(mesh, (33.0, -15.5, 25.5), (33.0, 15.5, 25.5), 2.4, 47)
    # the heap of its resource, and a few chunks on the ground before the mouth
    chunks(mesh, (40.0, -30.0, 0.0), 9, 13.0, (3.5, 7.5), 48)
    chunks(mesh, (46.0, 22.0, 0.0), 3, 6.0, (3.0, 4.5), 49)
    # the flag of its owner
    add_tube(mesh, [(4.0, 30.0, 18.0), (4.0, 30.0, 78.0)], [1.2, 0.9], WOOD, [0.0, 0.0], 50, 5, True, bark=0.06)
    mesh.begin_part()
    flag = [((4.0, 30.0, 76.0), 1.0), ((4.0, 30.0, 58.0), 1.0), ((-22.0, 30.5, 61.0), 0.85), ((-14.0, 30.0, 67.0), 0.8), ((-22.0, 29.5, 73.0), 0.85)]
    indices = [mesh.vertex(point, (TINTED[0] * tone, TINTED[1] * tone, TINTED[2] * tone), OWNER, normal=(0.0, 1.0, 0.25)) for point, tone in flag]
    for index in range(1, len(indices) - 1):
        mesh.sheet(indices[0], indices[index], indices[index + 1])
    return mesh


def pile():
    """A heap of chunks of a resource lying on the ground"""
    mesh = Mesh()
    chunks(mesh, (0.0, 0.0, 0.0), 11, 17.0, (4.0, 9.5), 61)
    return mesh


def mill():
    """A windmill: a round stone tower under a pointed roof, four sails on its front and sacks of what it gives at its foot"""
    mesh = Mesh()
    stone = (0.34, 0.32, 0.29)
    add_tube(mesh, [(0.0, 0.0, 0.0), (0.0, 0.0, 24.0), (0.0, 0.0, 48.0)], [15.0, 12.5, 10.0], stone, [0.0, 0.0, 0.0], 71, 10, False, bark=0.08)
    add_tube(mesh, [(0.0, 0.0, 47.0), (0.0, 0.0, 56.0), (0.0, 0.0, 66.0)], [12.5, 7.5, 0.6], WOOD, [0.0, 0.0, 0.0], 72, 10, True, bark=0.1)
    add_blob(mesh, (14.0, 0.0, 6.0), (3.0, 4.5, 8.0), DARK, 0.0, 73, 1, bumps=0.0, flatten=0.8, shade=0.0)
    # the sails, flat on the front of the tower around a hub
    hub = (13.0, 0.0, 42.0)
    timber(mesh, (9.0, 0.0, 42.0), (15.0, 0.0, 42.0), 1.6, 74)
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
    chunks(mesh, (20.0, -20.0, 0.0), 6, 9.0, (3.5, 6.0), 75)
    return mesh


MESHES = [('Mine', mine), ('Pile', pile), ('Mill', mill)]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    manifest = {'meshes': []}
    for name, make in MESHES:
        asset = 'SM_Planet' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, False)
        manifest['meshes'].append({'name': asset, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'sites.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=1)
    print('written to', OUTPUT_FOLDER)


if __name__ == '__main__':
    main()
