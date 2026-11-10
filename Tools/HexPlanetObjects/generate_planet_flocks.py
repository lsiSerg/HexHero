"""Generates the meshes of the birds and bats of the hex planet (UHexPlanetFlocks): a gull, a small bird, a raptor and a bat.

Run with any Python 3 without packages, for example the one in the engine:
    <Engine>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe generate_planet_flocks.py
Writes GLB files and flocks.json to Saved/HexPlanetFlocks, then run import_planet_flocks.py with the commandlet, after
create_planet_assets.py (or its argument flocks) made M_HexPlanetFlock. The GLB files open in Blender, so a mesh can be reworked by hand.

An animal is seen from above and small, so it is a flat shape of a few triangles, seen from both sides: a body, a tail and two wings. It flies
along its X with its Z up, in world units, and its normals point up, so the sun lights it as it banks. The first UV of a vertex is how far out
on a wing it lies, 0 on the body and 1 at a tip, by which M_HexPlanetFlock beats the wings, and the color of a vertex is its color.
"""

import json
import os

from generate_planet_objects import Mesh, lerp, scale_color, write_glb

OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetFlocks'))
UP = (0.0, 0.0, 1.0)


def add_sheet(mesh, points, colors, weights):
    """A flat polygon as a fan of triangles from its first point, each point with its color and how far out on a wing it lies"""
    mesh.begin_part()
    indices = [mesh.vertex(point, color, 0.0, (weight, 0.5), UP) for point, color, weight in zip(points, colors, weights)]
    for index in range(1, len(indices) - 1):
        mesh.sheet(indices[0], indices[index], indices[index + 1])


def add_wing(mesh, side, root, half_span, stations):
    """A wing as strips between stations along its span, each (share of the span, x of its leading edge, x of its trailing edge, height, color)"""
    mesh.begin_part()
    rows = []
    for share, lead, trail, height, color in stations:
        y = side * (root + (half_span - root) * share)
        rows.append((mesh.vertex((lead, y, height), color, 0.0, (share, 0.5), UP), mesh.vertex((trail, y, height), scale_color(color, 0.9), 0.0, (share, 0.5), UP)))
    for (a, b), (c, d) in zip(rows, rows[1:]):
        mesh.sheet(a, b, d)
        mesh.sheet(a, d, c)


def add_body(mesh, long, wide, high, color, head_color=None, tail=(1.6, 1.0), tail_color=None):
    """A body from above, a long diamond with a ridge down its back, and a tail that fans out behind it"""
    head_color = head_color or color
    tail_color = tail_color or color
    nose = (long * 0.5, 0.0, 0.0)
    back = (-long * 0.32, 0.0, 0.0)
    ridge = (long * 0.08, 0.0, high)
    for side in (-1.0, 1.0):
        flank = (0.0, side * wide, 0.0)
        add_sheet(mesh, [nose, flank, ridge], [head_color, color, scale_color(color, 1.1)], [0.0, 0.0, 0.0])
        add_sheet(mesh, [flank, back, ridge], [color, scale_color(color, 0.9), scale_color(color, 1.1)], [0.0, 0.0, 0.0])
    end = back[0] - tail[0]
    add_sheet(mesh, [back, (end, -tail[1], 0.0), (end, tail[1], 0.0)], [color, tail_color, tail_color], [0.0, 0.0, 0.0])


def gull():
    """A gull: long narrow wings, grey above and black at their tips, bent back at the wrist, and a white body"""
    mesh = Mesh()
    white = (0.78, 0.78, 0.76)
    grey = (0.36, 0.38, 0.42)
    black = (0.03, 0.03, 0.035)
    add_body(mesh, 4.4, 0.75, 0.5, white, tail=(1.3, 0.9))
    for side in (-1.0, 1.0):
        add_wing(mesh, side, 0.4, 4.8, [(0.0, 0.9, -0.7, 0.1, grey), (0.45, 1.25, -0.45, 0.15, grey), (0.8, 0.35, -0.75, 0.1, scale_color(grey, 0.8)),
                                        (1.0, -0.7, -1.0, 0.05, black)])
    return mesh


def bird():
    """A small dark bird of the forests, as a starling or a swallow: short pointed wings swept back and a forked tail"""
    mesh = Mesh()
    dark = (0.035, 0.032, 0.04)
    add_body(mesh, 2.8, 0.5, 0.35, dark, tail=(0.9, 0.7))
    for side in (-1.0, 1.0):
        add_wing(mesh, side, 0.3, 2.7, [(0.0, 0.6, -0.6, 0.05, dark), (0.5, 0.55, -0.5, 0.08, scale_color(dark, 1.3)), (1.0, -0.75, -0.95, 0.05, scale_color(dark, 0.8))])
    return mesh


def raptor():
    """A raptor: broad long wings held flat, with spread dark primaries at their ends, a pale head and a wide tail"""
    mesh = Mesh()
    brown = (0.13, 0.085, 0.05)
    dark = (0.045, 0.03, 0.022)
    pale = (0.42, 0.36, 0.28)
    add_body(mesh, 5.6, 1.0, 0.7, brown, head_color=pale, tail=(2.1, 1.7), tail_color=scale_color(brown, 1.4))
    for side in (-1.0, 1.0):
        add_wing(mesh, side, 0.6, 7.0, [(0.0, 1.4, -1.6, 0.1, brown), (0.5, 1.7, -1.5, 0.14, scale_color(brown, 1.15)), (0.85, 1.3, -1.3, 0.1, brown),
                                        (1.0, 0.5, -0.9, 0.06, dark)])
        # the primaries: three fingers out past the end of the wing
        for finger, (lead, reach) in enumerate(((1.2, 0.9), (0.4, 1.1), (-0.4, 0.9))):
            y0 = side * (0.6 + (7.0 - 0.6) * 0.92)
            y1 = side * (7.0 + reach)
            add_sheet(mesh, [(lead, y0, 0.08), (lead - 0.55, y0, 0.08), (lead - 0.4, y1, 0.05)], [dark, dark, dark], [0.92, 0.92, 1.0])
    return mesh


def bat():
    """A bat: a small furred body with ears, and wings of skin stretched between long fingers, scalloped along their trailing edge"""
    mesh = Mesh()
    fur = (0.06, 0.042, 0.032)
    skin = (0.085, 0.06, 0.05)
    add_body(mesh, 2.2, 0.55, 0.45, fur, tail=(0.5, 0.35))
    for side in (-1.0, 1.0):
        add_sheet(mesh, [(1.0, side * 0.12, 0.2), (0.75, side * 0.5, 0.25), (0.6, side * 0.2, 0.75)], [fur, fur, scale_color(fur, 1.3)], [0.0, 0.0, 0.0])
        # the wrist stands forwards, and the skin between the fingers hangs back in between them
        add_wing(mesh, side, 0.3, 3.2, [(0.0, 0.5, -0.8, 0.05, skin), (0.2, 0.9, -0.45, 0.1, skin), (0.4, 1.15, -1.1, 0.12, scale_color(skin, 1.15)),
                                        (0.6, 0.8, -0.55, 0.1, skin), (0.8, 0.3, -1.0, 0.07, scale_color(skin, 0.9)), (1.0, -0.4, -0.6, 0.04, skin)])
    return mesh


MESHES = [('Gull', gull), ('Bird', bird), ('Raptor', raptor), ('Bat', bat)]


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    manifest = {'meshes': []}
    for name, make in MESHES:
        asset = 'SM_Planet' + name
        triangles = write_glb(make(), os.path.join(OUTPUT_FOLDER, asset + '.glb'), asset, True)
        manifest['meshes'].append({'name': asset, 'triangles': triangles})
        print('%s: %d triangles' % (asset, triangles))
    with open(os.path.join(OUTPUT_FOLDER, 'flocks.json'), 'w', encoding='utf-8') as file:
        json.dump(manifest, file, indent=2)
    print('Wrote %d meshes to %s' % (len(MESHES), OUTPUT_FOLDER))


if __name__ == '__main__':
    main()
