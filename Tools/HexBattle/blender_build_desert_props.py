"""Models the props of the field of a battle that no download had, as the developer asked on 2026-11-13 for cacti and skeletons of dinosaurs in
a desert and some bones lying about on any field: cacti (saguaros with arms, a prickly pear of pads, a barrel cactus), the skeleton of a great
beast half sunk into the sand (a spine, a ribcage and a skull with horns), the skull of a beast and a few bones lying together. Their
textures are drawn here too, 512 a side: cactus skin in ribs with spines and bone with cracks and stains, a color, a normal (DirectX) and an
ARM each, as the scanned rocks have, for M_HexBattleScan. Run with Blender (numpy):

  blender --background --factory-startup --python blender_build_desert_props.py [-- preview]

It writes Saved/BattleProps/SM_BattleProp_<Name>.glb, T_BattleProp_<Skin>_{C,N,ARM}.png and props.json, which import_battle_obstacles.py
imports with the rocks.
"""
import json
import math
import os
import random
import sys

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'Saved', 'BattleProps')
SIZE = 512


# ---- textures ----------------------------------------------------------------------------------------------------------------------------

def noise(size, scale, seed, octaves=4):
    """A tileable value noise of 0 to 1"""
    rng = np.random.default_rng(seed)
    total = np.zeros((size, size))
    amplitude = 1.0
    weight = 0.0
    cells = scale
    for _ in range(octaves):
        grid = rng.random((cells, cells))
        x = np.linspace(0, cells, size, endpoint=False)
        x0 = np.floor(x).astype(int)
        t = x - x0
        t = t * t * (3 - 2 * t)
        x1 = (x0 + 1) % cells
        rows = grid[x0][:, x0] * (1 - t)[None, :] + grid[x0][:, x1] * t[None, :]
        rows1 = grid[x1][:, x0] * (1 - t)[None, :] + grid[x1][:, x1] * t[None, :]
        total += (rows * (1 - t)[:, None] + rows1 * t[:, None]) * amplitude
        weight += amplitude
        amplitude *= 0.5
        cells *= 2
    return total / weight


def normal_from_height(height, strength):
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    n = np.stack([-dx, dy, np.ones_like(height)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def save(name, rgb):
    image = bpy.data.images.new(name, SIZE, SIZE, alpha=True)
    rgba = np.concatenate([np.clip(rgb, 0, 1), np.ones((SIZE, SIZE, 1))], axis=-1)
    image.pixels = rgba[::-1].reshape(-1).tolist()
    image.filepath_raw = os.path.join(OUT, name + '.png')
    image.file_format = 'PNG'
    image.save()


def cactus_skin():
    """U around the stem in 12 ribs, V up it: green ribs, darker grooves, spine clusters along the crests, dust towards the foot"""
    u = np.linspace(0, 1, SIZE, endpoint=False)[None, :].repeat(SIZE, 0)
    v = np.linspace(0, 1, SIZE, endpoint=False)[:, None].repeat(SIZE, 1)
    rib = 0.5 + 0.5 * np.cos(u * 12 * 2 * math.pi)
    grain = noise(SIZE, 8, 1)
    fine = noise(SIZE, 64, 2, 2)
    green = np.array([0.16, 0.3, 0.1])
    light = np.array([0.32, 0.45, 0.18])
    color = green[None, None, :] * (1 - rib[..., None]) + light[None, None, :] * rib[..., None]
    color *= (0.8 + 0.4 * grain)[..., None] * (0.9 + 0.2 * fine)[..., None]
    # spine clusters, pale, on the crests every so often up the stem
    phase_u = (u * 12) % 1.0
    phase_v = (v * 40) % 1.0
    spot = np.exp(-(((phase_u - 0.5) * 6) ** 2 + ((phase_v - 0.5) * 5) ** 2))
    color = color * (1 - spot[..., None] * 0.8) + np.array([0.85, 0.8, 0.62])[None, None, :] * spot[..., None] * 0.8
    height = rib * 0.6 + spot * 0.5 + fine * 0.1
    arm = np.stack([0.75 + 0.25 * rib, 0.55 - 0.2 * spot + 0.1 * fine, np.zeros_like(rib)], axis=-1)
    normal = normal_from_height(height, 6.0)
    normal[..., 1] = 1.0 - normal[..., 1]
    return color, normal, arm


def bone_skin():
    """Old bone: ivory with stains, pits and cracks"""
    stain = noise(SIZE, 6, 3)
    fine = noise(SIZE, 48, 4, 3)
    crack = noise(SIZE, 20, 5, 3)
    lines = np.clip(1 - np.abs(crack - 0.5) * 40, 0, 1)
    ivory = np.array([0.78, 0.72, 0.6])
    brown = np.array([0.45, 0.36, 0.26])
    t = np.clip((stain - 0.45) * 2.5, 0, 1)
    color = ivory[None, None, :] * (1 - t[..., None]) + brown[None, None, :] * t[..., None]
    color *= (0.85 + 0.25 * fine)[..., None]
    color *= (1 - lines * 0.55)[..., None]
    height = fine * 0.5 - lines * 0.8
    arm = np.stack([0.85 - lines * 0.4, 0.75 + 0.15 * fine, np.zeros_like(fine)], axis=-1)
    normal = normal_from_height(height, 5.0)
    normal[..., 1] = 1.0 - normal[..., 1]
    return color, normal, arm


# ---- meshes ------------------------------------------------------------------------------------------------------------------------------

class Mesh:
    def __init__(self, name):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.name = name

    def tube(self, points, radii, ribs=0, rib_depth=0.0, segments=16, cap_end=True, cap_start=False, u_scale=1.0):
        """A tube along a polyline, ribbed when ribs > 0, U around it and V along it in metres"""
        rings = []
        length = 0.0
        lengths = [0.0]
        for a, b in zip(points, points[1:]):
            length += (Vector(b) - Vector(a)).length
            lengths.append(length)
        for index, point in enumerate(points):
            point = Vector(point)
            ahead = Vector(points[min(index + 1, len(points) - 1)]) - Vector(points[max(index - 1, 0)])
            ahead.normalize()
            side = ahead.orthogonal().normalized()
            if abs(ahead.z) > 0.9:
                side = Vector((1.0, 0.0, 0.0))
            other = ahead.cross(side).normalized()
            side = other.cross(ahead).normalized()
            ring = []
            for step in range(segments):
                angle = 2 * math.pi * step / segments
                r = radii[index] * (1.0 - rib_depth * (0.5 - 0.5 * math.cos(angle * ribs)) if ribs else 1.0)
                ring.append(self.bm.verts.new(point + (side * math.cos(angle) + other * math.sin(angle)) * r))
            rings.append(ring)
        for index in range(len(rings) - 1):
            for step in range(segments):
                nxt = (step + 1) % segments
                face = self.bm.faces.new((rings[index][step], rings[index][nxt], rings[index + 1][nxt], rings[index + 1][step]))
                face.smooth = True
                for k, loop in enumerate(face.loops):
                    s = step + (1 if k in (1, 2) else 0)
                    row = index + (1 if k in (2, 3) else 0)
                    loop[self.uv].uv = (s / segments * u_scale, lengths[row])
        for cap, ring in ((cap_end, rings[-1]), (cap_start, rings[0])):
            if cap:
                middle = sum((v.co for v in ring), Vector()) / len(ring)
                tip = self.bm.verts.new(middle + (middle - sum((v.co for v in rings[-2 if ring is rings[-1] else 1]), Vector()) / len(ring)).normalized()
                    * radii[-1 if ring is rings[-1] else 0] * 0.5)
                for step in range(segments):
                    nxt = (step + 1) % segments
                    face = self.bm.faces.new((ring[step], ring[nxt], tip) if ring is rings[-1] else (ring[nxt], ring[step], tip))
                    face.smooth = True
                    for loop in face.loops:
                        loop[self.uv].uv = ((step + 0.5) / segments * u_scale, lengths[-1] + 0.1)

    def blob(self, centre, size, seed, subdivisions=2, flat=1.0):
        """A lumpy ellipsoid, UV by its angles"""
        made = bmesh.ops.create_icosphere(self.bm, subdivisions=subdivisions, radius=1.0)
        rng = random.Random(seed)
        bumps = [(Vector((rng.uniform(-1, 1), rng.uniform(-1, 1), rng.uniform(-1, 1))).normalized(), rng.uniform(0.05, 0.15)) for _ in range(5)]
        for vert in made['verts']:
            d = vert.co.normalized()
            lump = 1.0 + sum(h * max(d.dot(b), 0.0) ** 3 for b, h in bumps)
            vert.co = Vector(centre) + Vector((d.x * size[0], d.y * size[1], d.z * size[2] * flat)) * lump
        for face in {f for v in made['verts'] for f in v.link_faces}:
            face.smooth = True
            for loop in face.loops:
                d = (loop.vert.co - Vector(centre)).normalized()
                loop[self.uv].uv = (math.atan2(d.y, d.x) / (2 * math.pi) + 0.5, d.z * 0.5 + 0.5)

    def finish(self, material):
        mesh = bpy.data.meshes.new(self.name)
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        self.bm.to_mesh(mesh)
        self.bm.free()
        mesh.materials.append(material)
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def arc(start, end, rise, steps):
    s, e = Vector(start), Vector(end)
    return [tuple(s.lerp(e, t / steps) + Vector((0.0, 0.0, math.sin(math.pi * t / steps) * rise))) for t in range(steps + 1)]


def saguaro(seed, height, arms):
    rng = random.Random(seed)
    m = Mesh('SM_BattleProp_Saguaro%d' % seed)
    trunk = [(0.0, 0.0, -0.3)] + [(rng.uniform(-0.03, 0.03), rng.uniform(-0.03, 0.03), height * t / 8) for t in range(1, 9)]
    m.tube(trunk, [0.24] + [0.24 - 0.03 * t / 8 for t in range(1, 9)], ribs=12, rib_depth=0.12, segments=36, u_scale=1.0)
    for arm in range(arms):
        angle = rng.uniform(0, 2 * math.pi)
        out = Vector((math.cos(angle), math.sin(angle), 0.0))
        base = height * rng.uniform(0.35, 0.6)
        reach = rng.uniform(0.45, 0.7)
        top = base + rng.uniform(0.6, 1.2)
        points = [tuple(out * 0.1 + Vector((0, 0, base)))]
        for t in range(1, 5):
            a = t / 4 * math.pi / 2
            points.append(tuple(out * (0.1 + reach * math.sin(a)) + Vector((0, 0, base + 0.25 * (1 - math.cos(a))))))
        for t in range(1, 5):
            points.append(tuple(out * (0.1 + reach) + Vector((0, 0, base + 0.25 + (top - base - 0.25) * t / 4))))
        m.tube(points, [0.13 + 0.02 * (1 - i / len(points)) for i in range(len(points))], ribs=10, rib_depth=0.12, segments=24, u_scale=0.8)
    return m


def prickly_pear(seed):
    rng = random.Random(seed)
    m = Mesh('SM_BattleProp_PricklyPear')
    pads = [((0.0, 0.0, 0.3), 0.0, 0.0)]
    for _ in range(9):
        parent = pads[rng.randrange(len(pads))]
        turn = rng.uniform(0, 360)
        lean = rng.uniform(-35, 35)
        d = Vector((math.cos(math.radians(turn)), math.sin(math.radians(turn)), 0.0))
        pads.append((tuple(Vector(parent[0]) + d * 0.22 + Vector((0.0, 0.0, 0.3))), turn, lean))
    for index, (centre, turn, lean) in enumerate(pads):
        before = len(m.bm.verts)
        m.blob(centre, (0.22, 0.06, 0.3), seed + index, 2)
        rot = Matrix.Rotation(math.radians(turn), 4, 'Z') @ Matrix.Rotation(math.radians(lean), 4, 'Y')
        m.bm.verts.ensure_lookup_table()
        for vert in list(m.bm.verts)[before:]:
            vert.co = Vector(centre) + (rot @ (vert.co - Vector(centre)).to_4d()).to_3d()
    return m


def barrel(seed):
    m = Mesh('SM_BattleProp_Barrel')
    points = [(0.0, 0.0, -0.1)] + [(0.0, 0.0, 0.55 * t / 6) for t in range(1, 7)]
    radii = [0.32] + [0.36 * math.sin(math.pi * (0.35 + 0.6 * t / 6)) for t in range(1, 7)]
    m.tube(points, radii, ribs=18, rib_depth=0.1, segments=54, u_scale=1.5)
    return m


def vertebra_chain(m, points, size, seed):
    rng = random.Random(seed)
    for index, point in enumerate(points):
        s = size * (1.0 - 0.6 * index / len(points))
        m.blob(point, (s * 0.7, s, s * 0.9), seed + index, 1)
        # a spine up from each
        m.tube([point, tuple(Vector(point) + Vector((0.0, 0.0, s * rng.uniform(1.4, 2.2))))], [s * 0.3, s * 0.12], segments=6)


def dino_skeleton(seed):
    """A spine along the sand, ribs arching up out of it on both sides and over, as of a beast on its back, a skull with horns at its front"""
    m = Mesh('SM_BattleProp_DinoSkeleton')
    spine = [(x, 0.1 * math.sin(x), 0.12 + 0.08 * math.sin((x + 2.2) / 4.4 * math.pi)) for x in [-2.2 + 4.4 * t / 14 for t in range(15)]]
    vertebra_chain(m, spine, 0.16, seed)
    for index in range(2, 10):
        x, y, z = spine[index]
        span = 1.0 - abs(index - 5.5) / 6.0
        for side in (-1.0, 1.0):
            points = []
            for t in range(9):
                a = t / 8 * math.pi * 0.85
                points.append((x + 0.08 * t / 8, y + side * math.sin(a) * (0.55 + 0.5 * span), z - 0.05 + (1 - math.cos(a)) * (0.55 + 0.5 * span)))
            m.tube(points, [0.06 - 0.035 * t / 8 for t in range(9)], segments=8)
    # the skull: a long snout, a crest and two horns
    head = Vector((2.55, 0.0, 0.25))
    m.blob(head, (0.55, 0.32, 0.3), seed + 50, 2)
    m.blob(head + Vector((0.55, 0.0, -0.08)), (0.4, 0.2, 0.17), seed + 51, 2)
    for side in (-1.0, 1.0):
        m.tube([tuple(head + Vector((-0.1, side * 0.22, 0.2))), tuple(head + Vector((0.25, side * 0.5, 0.7))), tuple(head + Vector((0.7, side * 0.55, 0.85)))],
            [0.09, 0.05, 0.012], segments=8)
    # a leg bone and a few loose bones by it
    m.tube(arc((0.6, 1.3, 0.05), (1.6, 1.0, 0.05), 0.15, 6), [0.1, 0.08, 0.07, 0.07, 0.08, 0.09, 0.11], segments=8, cap_start=True)
    m.tube(arc((-0.8, -1.4, 0.05), (0.1, -1.6, 0.05), 0.1, 6), [0.09, 0.07, 0.06, 0.06, 0.07, 0.08, 0.1], segments=8, cap_start=True)
    return m


def beast_skull(seed):
    m = Mesh('SM_BattleProp_Skull')
    m.blob((0.0, 0.0, 0.15), (0.32, 0.22, 0.18), seed, 2)
    m.blob((0.3, 0.0, 0.08), (0.24, 0.12, 0.1), seed + 1, 2)
    for side in (-1.0, 1.0):
        m.tube([(-0.05, side * 0.18, 0.25), (-0.05, side * 0.45, 0.3), (0.1, side * 0.65, 0.5)], [0.06, 0.04, 0.01], segments=8)
    return m


def bones(seed):
    rng = random.Random(seed)
    m = Mesh('SM_BattleProp_Bones%d' % seed)
    for index in range(4):
        angle = rng.uniform(0, 2 * math.pi)
        d = Vector((math.cos(angle), math.sin(angle), 0.0))
        middle = Vector((rng.uniform(-0.25, 0.25), rng.uniform(-0.25, 0.25), 0.04))
        length = rng.uniform(0.35, 0.6)
        a = middle - d * length * 0.5
        b = middle + d * length * 0.5
        m.tube([tuple(a), tuple(a.lerp(b, 0.15)), tuple(a.lerp(b, 0.85)), tuple(b)], [0.05, 0.03, 0.03, 0.05], segments=8, cap_start=True)
        for end in (a, b):
            m.blob(end, (0.05, 0.05, 0.04), seed + index * 7 + int(end.x * 100), 1)
    m.blob((0.3, -0.2, 0.08), (0.12, 0.09, 0.08), seed + 99, 2)
    return m


def main():
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    skins = {'Cactus': cactus_skin(), 'Bone': bone_skin()}
    for name, (color, normal, arm) in skins.items():
        save('T_BattleProp_%s_C' % name, color)
        save('T_BattleProp_%s_N' % name, normal)
        save('T_BattleProp_%s_ARM' % name, arm)
    materials = {name: bpy.data.materials.new(name) for name in skins}
    props = [(saguaro(1, 3.2, 2), 'Cactus', ['rocky']), (saguaro(2, 2.4, 1), 'Cactus', ['rocky']), (prickly_pear(3), 'Cactus', ['rocky']),
        (barrel(4), 'Cactus', ['rocky']), (dino_skeleton(5), 'Bone', ['landmark']), (beast_skull(6), 'Bone', ['rocky', 'field']),
        (bones(7), 'Bone', ['rocky', 'field']), (bones(8), 'Bone', ['field'])]
    entries = []
    x = 0.0
    for mesh, skin, places in props:
        obj = mesh.finish(materials[skin])
        name = obj.name
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name + '.glb'), export_format='GLB', use_selection=True, export_materials='NONE',
            export_texcoords=True, export_normals=True, export_yup=True)
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        size = obj.dimensions
        entries.append({'name': name.replace('SM_BattleProp_', ''), 'skin': skin, 'places': places, 'triangles': tris, 'size': [size.x, size.y, size.z]})
        print('PROP %s: %d triangles, %.2f x %.2f x %.2f m' % (name, tris, size.x, size.y, size.z))
        low = min((obj.matrix_world @ Vector(c)).x for c in obj.bound_box)
        obj.location.x = x - low
        x += size.x + 0.8
    with open(os.path.join(OUT, 'props.json'), 'w', encoding='utf-8') as file:
        json.dump({'props': entries}, file, indent=1)
    if 'preview' in args:
        for name, material in materials.items():
            material.use_nodes = True
            tree = material.node_tree
            image = tree.nodes.new('ShaderNodeTexImage')
            image.image = bpy.data.images.load(os.path.join(OUT, 'T_BattleProp_%s_C.png' % name))
            tree.links.new(image.outputs['Color'], tree.nodes['Principled BSDF'].inputs['Base Color'])
        scene = bpy.context.scene
        scene.render.engine = 'BLENDER_EEVEE'
        scene.render.resolution_x = 2000
        scene.render.resolution_y = 700
        world = bpy.data.worlds.new('World')
        world.color = (0.6, 0.55, 0.45)
        scene.world = world
        bpy.ops.object.light_add(type='SUN', rotation=(math.radians(50), 0.0, math.radians(30)))
        bpy.context.object.data.energy = 4.0
        bpy.ops.object.camera_add(location=(x * 0.5, -14.0, 7.0), rotation=(math.radians(68), 0.0, 0.0))
        camera = bpy.context.object
        camera.data.type = 'ORTHO'
        camera.data.ortho_scale = x + 1.0
        scene.camera = camera
        scene.render.filepath = os.path.join(OUT, 'preview.png')
        bpy.ops.render.render(write_still=True)
    print('PROPS DONE')


main()
