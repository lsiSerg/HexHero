"""Builds the figures of the creatures of the battles from CC0 models of Quaternius (unit_model_sources.json, which fetch_unit_models.ps1
downloads to Saved/ModelSources/units), run with Blender 5.2:

    blender --background --factory-startup --python Tools/HexBattle/blender_build_units.py [-- Name Name ... | -- preview]

For each unit of UNITS it imports its model, puts the colors of its materials, or of its texture where one is, into the colors of its
corners, the alpha 1 on what takes the color of its side (M_HexBattleUnit), adds what it carries (a spear, a sword, a shield, a bow, a staff,
wings), bound to a bone, keeps the animations it plays under the names the battles read (Idle, Move, Attack, Shoot, Hit, Death) and writes
Saved/BattleUnits/SK_Unit<Name>.glb, which import_battle_units.py imports. The riders of the field are the rider of the heroes, not built here.
With preview it renders a frame of each animation of each unit to Saved/BattleUnits/preview.
"""

import bpy
import bmesh
import math
import os
import sys
from mathutils import Matrix, Quaternion, Vector, noise

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SOURCES = os.path.join(ROOT, 'Saved', 'ModelSources', 'units')
HERO_SOURCES = os.path.join(ROOT, 'Saved', 'ModelSources', 'hero')
OUTPUT = os.path.join(ROOT, 'Saved', 'BattleUnits')

# what each creature is built from: its model, the materials that take the color of its side, the colors some materials take in its place,
# what it carries, bound to which bone and how (an offset and a turn in the space of the bone at rest, a size), and its animations by the
# name the battles read, each the action of the model whose name ends so
KNIGHT_ACTIONS = {'Idle': 'Idle_Sword', 'Move': 'Run', 'Attack': 'Sword_Slash', 'Hit': 'HitRecieve', 'Death': 'Death'}
ADVENTURER_ACTIONS = {'Idle': 'Idle', 'Move': 'Run', 'Attack': 'Sword_Slash', 'Shoot': 'Gun_Shoot', 'Hit': 'HitRecieve', 'Death': 'Death'}
# what a creature carries in a hand points along a direction of the world in its stance, as the developer found on 2026-11-13 the spear of
# the sentinel through its body and the sword of the oathsworn on its side: the +Y of the prop along `y` (made square to the hand with
# `square`), its +Z towards `z`, held `grip` metres along the hand from the wrist, moved `shift` in the world; forward is -Y, up +Z, the
# right of a figure -X
FORWARD = (0.0, -1.0, 0.0)
UP = (0.0, 0.0, 1.0)
RIGHT = (-1.0, 0.0, 0.0)
# the hand of a stance hangs down and forward, so a weapon held square to it lies level: they lean forward instead, off the forearm
SPEAR = {'kind': 'spear', 'bone': 'Wrist.R', 'grip': 0.06, 'y': (0.0, -0.45, 1.0), 'z': FORWARD, 'size': 1.0}
SWORD = {'kind': 'sword', 'bone': 'Wrist.R', 'grip': 0.06, 'y': (0.0, -0.6, 0.8), 'z': RIGHT, 'size': 1.0}
SHIELD = {'kind': 'shield', 'bone': 'Wrist.L', 'offset': (0.0, 0.05, 0.05), 'turn': (0.0, 0.0, 90.0), 'size': 0.9}
STAFF = {'kind': 'staff', 'bone': 'Wrist.R', 'grip': 0.06, 'y': (0.0, -0.35, 1.0), 'z': FORWARD, 'size': 1.0}
UNITS = {
    'Sentinel': {'source': ('hero', 'knight'), 'side': ['Blue'], 'actions': KNIGHT_ACTIONS, 'props': [SPEAR, SHIELD]},
    'Huntsman': {'source': ('units', 'y9KWOVG21R'), 'side': ['Brown2'], 'actions': dict(ADVENTURER_ACTIONS, Shoot='BowShoot'), 'bow': True,
        'props': [{'kind': 'bow', 'bone': 'Wrist.L', 'grip': 0.06, 'y': FORWARD, 'z': (0.0, 0.0, -1.0), 'square': True, 'size': 1.0}]},
    'Oathsworn': {'source': ('hero', 'knight'), 'side': ['Blue'], 'actions': KNIGHT_ACTIONS, 'props': [SWORD, dict(SHIELD, size=1.0)]},
    'Pilgrim': {'source': ('units', 'y9KWOVG21R'), 'side': ['Brown2'], 'recolor': {'Brown': (0.32, 0.22, 0.14), 'LightBrown': (0.45, 0.33, 0.22)},
        'actions': ADVENTURER_ACTIONS, 'props': [STAFF]},
    # the flyers with legs walk where their way runs over the ground and fly over what is in the way (2026-11-14): Walk and Fly
    'SkyWarden': {'source': ('hero', 'knight'), 'side': ['Blue'], 'recolor': {'Metal': (0.85, 0.82, 0.7)}, 'actions': dict(KNIGHT_ACTIONS, Walk='Walk', Fly='Idle_Neutral'),
        'props': [dict(SWORD, size=1.15), {'kind': 'wings', 'bone': 'Chest', 'grip': 0.0, 'shift': (0.0, 0.14, 0.02), 'y': FORWARD, 'z': UP, 'size': 1.0}]},
    # the skeletons are made on the full rig of the adventurer, as the skeleton of the model has twelve bones and no arms that move, which
    # stood in a T-pose (2026-11-13)
    'BoneWarrior': {'source': ('units', 'y9KWOVG21R'), 'body': 'skeleton', 'side': [], 'actions': ADVENTURER_ACTIONS,
        'props': [dict(SWORD, kind='rusty_sword'), dict(SHIELD, kind='buckler')]},
    'Decayed': {'source': ('units', 'VlXjG0N8Eg'), 'side': [], 'actions': {'Idle': 'Idle', 'Move': 'Walk', 'Attack': 'Punch', 'Hit': 'HitReact', 'Death': 'Death'},
        'props': []},
    # a wraith in a hooded robe that floats, its face dark under the hood and its eyes glowing, in place of the ghost of a cartoon
    'Shade': {'source': ('units', 'y9KWOVG21R'), 'side': ['White'], 'recolor': {'Skin': (0.02, 0.02, 0.03), 'Brown': (0.07, 0.07, 0.09),
        'Brown2': (0.11, 0.11, 0.14), 'LightBrown': (0.13, 0.13, 0.16), 'DarkBrown': (0.05, 0.05, 0.06), 'Black': (0.03, 0.03, 0.04),
        'Gold': (0.25, 0.27, 0.3), 'Metal': (0.2, 0.22, 0.25), 'Metal_Dark': (0.1, 0.1, 0.12), 'White': (0.3, 0.35, 0.4)},
        'actions': {'Idle': 'Idle', 'Move': 'Walk', 'Attack': 'Punch_Right', 'Hit': 'HitRecieve', 'Death': 'Death', 'Walk': 'Walk', 'Fly': 'Idle_Neutral'},
        'props': [{'kind': 'robe', 'bone': 'Hips', 'grip': 0.0, 'y': FORWARD, 'z': UP, 'size': 1.0},
            {'kind': 'eyes', 'bone': 'Head', 'grip': 0.0, 'shift': (0.0, -0.1, 0.07), 'y': FORWARD, 'z': UP, 'size': 1.0}]},
    'BloodDrinker': {'source': ('units', 'y9KWOVG21R'), 'side': ['Brown2'], 'recolor': {'Skin': (0.78, 0.76, 0.8), 'Brown': (0.12, 0.05, 0.07),
        'LightBrown': (0.35, 0.05, 0.08), 'DarkBrown': (0.08, 0.04, 0.05), 'Brown2': (0.5, 0.05, 0.08)}, 'actions': dict(ADVENTURER_ACTIONS, Walk='Walk', Fly='Idle_Neutral'),
        'props': [{'kind': 'bat_wings', 'bone': 'Chest', 'grip': 0.0, 'shift': (0.0, 0.12, 0.0), 'y': FORWARD, 'z': UP, 'size': 0.9}]},
    'AshMage': {'source': ('units', 'y9KWOVG21R'), 'body': 'skeleton', 'side': [], 'actions': ADVENTURER_ACTIONS,
        'props': [dict(STAFF, kind='skull_staff'), {'kind': 'robe', 'bone': 'Hips', 'grip': 0.0, 'y': FORWARD, 'z': UP, 'size': 1.0, 'color': (0.2, 0.18, 0.17)},
            {'kind': 'hood', 'bone': 'Head', 'grip': 0.0, 'y': FORWARD, 'z': UP, 'size': 1.0},
            {'kind': 'eyes', 'bone': 'Head', 'grip': 0.0, 'shift': (0.0, -0.1, 0.08), 'y': FORWARD, 'z': UP, 'size': 1.0}]},
    'Glowwing': {'source': ('units', 'LlwD0QNUPj'), 'side': ['Dragon_Secondary'], 'recolor': {'Eye_White': (0.85, 0.55, 0.08), 'Eye_Black': (0.02, 0.01, 0.0)},
        'actions': {'Idle': 'Flying_Idle', 'Move': 'Fast_Flying', 'Attack': 'Headbutt', 'Hit': 'HitReact', 'Death': 'Death'}, 'props': []},
    'BoneSerpent': {'source': ('units', 'VBvzjFIYws'), 'side': [], 'recolor': {'Main': (0.82, 0.78, 0.68), 'Belly': (0.6, 0.56, 0.48), 'Wings': (0.45, 0.42, 0.38)},
        'bone_look': {'Main': 'bone', 'Belly': 'bone', 'Claws': 'horn', 'Wings': 'membrane'},
        'actions': {'Idle': 'Dragon_Flying', 'Move': 'Dragon_Flying', 'Attack': 'Dragon_Attack', 'Hit': 'Dragon_Hit', 'Death': 'Dragon_Death'}, 'props': []},
}
SIDE_COLOR = (0.6, 0.6, 0.6)
MEASURES = {}
PROP_WOOD = (0.32, 0.2, 0.1)
PROP_METAL = (0.7, 0.72, 0.75)
PROP_GOLD = (0.85, 0.65, 0.2)


def corner_colors(obj, side, recolor):
    """The colors of the materials of a mesh, or of their textures at the UV of a corner, in the colors of its corners, the alpha 1 on those
    of its side"""
    mesh = obj.data
    colors = mesh.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    uv = mesh.uv_layers.active.data if mesh.uv_layers.active else None
    images = {}
    for index, material in enumerate(mesh.materials):
        image = None
        if material and material.use_nodes:
            for node in material.node_tree.nodes:
                if node.type == 'TEX_IMAGE' and node.image:
                    image = node.image
                    break
        if image:
            pixels = list(image.pixels[:])
            images[index] = (image.size[0], image.size[1], pixels)
    for polygon in mesh.polygons:
        material = mesh.materials[polygon.material_index] if mesh.materials else None
        name = material.name.split('.')[0] if material else ''
        base = tuple(material.diffuse_color[:3]) if material else (0.5, 0.5, 0.5)
        if material and material.use_nodes:
            for node in material.node_tree.nodes:
                if node.type == 'BSDF_PRINCIPLED':
                    base = tuple(node.inputs['Base Color'].default_value[:3])
        owned = name in side
        for loop in polygon.loop_indices:
            color = base
            if name in recolor:
                color = recolor[name]
            elif polygon.material_index in images and uv:
                width, height, pixels = images[polygon.material_index]
                u, v = uv[loop].uv
                x = min(max(int((u % 1.0) * width), 0), width - 1)
                y = min(max(int((v % 1.0) * height), 0), height - 1)
                at = (y * width + x) * 4
                color = (pixels[at], pixels[at + 1], pixels[at + 2])
            colors.data[loop].color = (SIDE_COLOR + (1.0,)) if owned else (tuple(color) + (0.0,))


# the bone dragon looked untextured to the developer on 2026-11-14, one flat tone on a model of 1104 faces: so its faces are split
# (BONE_SUBDIVISIONS levels of a simple subdivision, which keeps its faceted shape, as Catmull-Clark made it rubber, and the weights of its
# bones) and a look of old bone goes into the colors of its corners, by where each lies on the body at rest: ivory yellowed and browned in
# patches (BONE_IVORY, BONE_AGED), a grain along the body, grooves across its back like ribs (BONE_RIBS), cracks and dirt in its hollows
# (BONE_CAVITY, BONE_CRACK); the claws dark horn, the wings a dark leathery membrane with veins (BONE_MEMBRANE, BONE_VEIN)
BONE_SUBDIVISIONS = 2
BONE_RIBS = 20.0
BONE_PATCHES = 4.5
BONE_GRAIN = 22.0
BONE_CRACKS = 9.0
BONE_CRACK_WIDTH = 0.08
BONE_VEINS = 8.0
BONE_IVORY = (0.86, 0.81, 0.68)
BONE_AGED = (0.55, 0.45, 0.31)
BONE_DIRT = (0.2, 0.16, 0.12)
BONE_CAVITY = 2.5
BONE_CRACK = 0.55
BONE_HORN = (0.16, 0.13, 0.11)
BONE_HORN_TIP = (0.42, 0.37, 0.3)
BONE_MEMBRANE = (0.24, 0.2, 0.19)
BONE_VEIN = (0.1, 0.08, 0.08)


def smooth_for_look(obj, levels):
    """Smooths a mesh before its armature by Catmull-Clark, applied, its weights interpolated onto the new vertices; the vertices the glTF
    split at every edge for its flat faces are joined first, as split each face shrank into an oval of its own"""
    build = bmesh.new()
    build.from_mesh(obj.data)
    bmesh.ops.remove_doubles(build, verts=build.verts, dist=1.0e-4)
    build.to_mesh(obj.data)
    build.free()
    bpy.context.view_layer.objects.active = obj
    modifier = obj.modifiers.new('Smooth', 'SUBSURF')
    modifier.subdivision_type = 'SIMPLE'
    modifier.levels = levels
    modifier.render_levels = levels
    bpy.ops.object.modifier_move_to_index(modifier='Smooth', index=0)
    bpy.ops.object.modifier_apply(modifier='Smooth')
    # smooth normals over the faceted shape, as its flat faces, lit each its own way as it moved, shimmered on the field (2026-11-14)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True


def paint_bone_look(obj, kinds):
    """The look of old bone in the colors of the corners of a mesh, by the kind of each material (bone, horn, membrane)"""
    mesh = obj.data
    colors = mesh.color_attributes.get('Col')
    if not colors:
        return
    world = obj.matrix_world
    points = [world @ vertex.co for vertex in mesh.vertices]
    low = Vector((min(point.x for point in points), min(point.y for point in points), min(point.z for point in points)))
    high = Vector((max(point.x for point in points), max(point.y for point in points), max(point.z for point in points)))
    size = max((high - low).length, 1.0e-4)
    # how hollow each vertex lies: its neighbours stand out along its normal
    sums = [Vector((0.0, 0.0, 0.0)) for _ in mesh.vertices]
    counts = [0] * len(mesh.vertices)
    for edge in mesh.edges:
        a, b = edge.vertices
        sums[a] += mesh.vertices[b].co
        sums[b] += mesh.vertices[a].co
        counts[a] += 1
        counts[b] += 1
    hollow = []
    for index, vertex in enumerate(mesh.vertices):
        if counts[index] == 0:
            hollow.append(0.0)
            continue
        mean = sums[index] / counts[index]
        hollow.append((mean - vertex.co).dot(vertex.normal))
    spread = max(max(abs(value) for value in hollow), 1.0e-6)

    def mix(a, b, t):
        t = min(max(t, 0.0), 1.0)
        return tuple(x + (y - x) * t for x, y in zip(a, b))

    for polygon in mesh.polygons:
        material = mesh.materials[polygon.material_index] if mesh.materials else None
        kind = kinds.get(material.name.split('.')[0] if material else '', None)
        if not kind:
            continue
        for loop in polygon.loop_indices:
            index = mesh.loops[loop].vertex_index
            at = (points[index] - low) / size
            cavity = min(max(hollow[index] / spread * BONE_CAVITY, 0.0), 1.0)
            if kind == 'bone':
                # as coarse as the field shows it: finer grain and cracks shimmered on it as it moved (2026-11-14)
                patches = noise.fractal(at * BONE_PATCHES, 1.0, 2.0, 3) * 0.5 + 0.5
                grain = noise.noise(Vector((at.x * BONE_GRAIN, at.y * 4.0, at.z * BONE_GRAIN))) * 0.5 + 0.5
                crack = 1.0 - min(abs(noise.noise(at * BONE_CRACKS + Vector((3.1, 7.7, 1.3)))) / BONE_CRACK_WIDTH, 1.0)
                # grooves across the body, wavering, deeper on its back than on its belly
                wave = math.sin(at.y * BONE_RIBS + noise.noise(at * 5.0) * 2.0)
                rib = max(wave - 0.7, 0.0) / 0.3 * min(max((at.z - 0.25) * 3.0, 0.0), 1.0)
                color = mix(BONE_IVORY, BONE_AGED, (patches - 0.35) * 1.6 + (grain - 0.5) * 0.5)
                color = mix(color, BONE_DIRT, crack * BONE_CRACK + rib * 0.55 + cavity * 0.8)
            elif kind == 'horn':
                streak = noise.noise(Vector((at.x * 80.0, at.y * 80.0, at.z * 8.0))) * 0.5 + 0.5
                color = mix(BONE_HORN, BONE_HORN_TIP, streak * 0.6)
                color = mix(color, BONE_DIRT, cavity * 0.5)
            else:
                veins = 1.0 - min(abs(noise.noise(at * BONE_VEINS + Vector((5.3, 1.7, 9.1)))) / 0.1, 1.0)
                blotch = noise.fractal(at * 9.0, 1.0, 2.0, 3) * 0.5 + 0.5
                color = mix(BONE_MEMBRANE, BONE_AGED, blotch * 0.35)
                color = mix(color, BONE_VEIN, veins * 0.8 + cavity * 0.4)
            alpha = colors.data[loop].color[3]
            colors.data[loop].color = tuple(color) + (alpha,)


def add_box(bm, center, size, color, colors):
    made = bmesh.ops.create_cube(bm, size=1.0)
    for vert in made['verts']:
        vert.co = Vector((vert.co.x * size[0], vert.co.y * size[1], vert.co.z * size[2])) + Vector(center)
    for face in {f for v in made['verts'] for f in v.link_faces}:
        for loop in face.loops:
            loop[colors] = color + (0.0,)


def add_cylinder(bm, start, end, radius, color, colors, segments=8, end_radius=None):
    axis = Vector(end) - Vector(start)
    made = bmesh.ops.create_cone(bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius if end_radius is None else end_radius, depth=axis.length)
    rotation = axis.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
    middle = (Vector(start) + Vector(end)) * 0.5
    for vert in made['verts']:
        vert.co = rotation @ vert.co + middle
    for face in {f for v in made['verts'] for f in v.link_faces}:
        for loop in face.loops:
            loop[colors] = color + (0.0,)


def add_sphere(bm, center, size, color, colors, glow=False):
    """A small ellipsoid; glow marks it with the alpha 1, which M_HexBattleUnit draws in the color of the side, glowing a little"""
    made = bmesh.ops.create_uvsphere(bm, u_segments=10, v_segments=6, radius=1.0)
    for vert in made['verts']:
        vert.co = Vector((vert.co.x * size[0], vert.co.y * size[1], vert.co.z * size[2])) + Vector(center)
    for face in {f for v in made['verts'] for f in v.link_faces}:
        for loop in face.loops:
            loop[colors] = tuple(color) + (1.0 if glow else 0.0,)


# how far the limbs of the bow bend back from its grip, where its string runs
BOW_DEPTH = 0.17


def make_prop(kind, color=(0.18, 0.17, 0.17)):
    """What a creature carries, along +Y of its bone, in units of the model (the models are about 2 high)"""
    bm = bmesh.new()
    colors = bm.loops.layers.float_color.new('Col')
    if kind == 'spear':
        add_cylinder(bm, (0, -0.55, 0), (0, 0.85, 0), 0.025, PROP_WOOD, colors)
        add_cylinder(bm, (0, 0.85, 0), (0, 1.1, 0), 0.05, PROP_METAL, colors, 4, 0.0)
    elif kind == 'sword':
        add_box(bm, (0, 0.0, 0), (0.05, 0.16, 0.05), PROP_WOOD, colors)
        add_box(bm, (0, 0.1, 0), (0.22, 0.04, 0.05), PROP_GOLD, colors)
        add_box(bm, (0, 0.5, 0), (0.07, 0.75, 0.018), PROP_METAL, colors)
    elif kind == 'shield':
        add_cylinder(bm, (0, 0, -0.03), (0, 0, 0.03), 0.32, PROP_WOOD, colors, 12)
        add_cylinder(bm, (0, 0, 0.03), (0, 0, 0.06), 0.09, PROP_METAL, colors, 8)
    elif kind == 'staff':
        add_cylinder(bm, (0, -0.7, 0), (0, 0.9, 0), 0.03, PROP_WOOD, colors)
        add_box(bm, (0, 0.98, 0), (0.12, 0.12, 0.12), PROP_GOLD, colors)
    elif kind == 'bow':
        # the grip at the origin, its limbs along Y bending back towards -Z, the archer; the string is apart (make_bow_string)
        steps = 12
        for step in range(steps):
            a0 = -1.0 + 2.0 * step / steps
            a1 = -1.0 + 2.0 * (step + 1) / steps
            p0 = (0.0, a0 * 0.62, -BOW_DEPTH * a0 * a0)
            p1 = (0.0, a1 * 0.62, -BOW_DEPTH * a1 * a1)
            add_cylinder(bm, p0, p1, 0.024 - 0.01 * abs(a0), PROP_WOOD, colors, 6)
        add_cylinder(bm, (0, -0.07, 0), (0, 0.07, 0), 0.032, (0.2, 0.12, 0.08), colors, 8)
    elif kind == 'rusty_sword':
        add_box(bm, (0, 0.0, 0), (0.05, 0.16, 0.05), (0.2, 0.14, 0.1), colors)
        add_box(bm, (0, 0.1, 0), (0.2, 0.04, 0.05), (0.35, 0.25, 0.18), colors)
        add_box(bm, (0, 0.48, 0), (0.07, 0.7, 0.018), (0.42, 0.33, 0.25), colors)
    elif kind == 'buckler':
        add_cylinder(bm, (0, 0, -0.03), (0, 0, 0.03), 0.26, (0.28, 0.2, 0.13), colors, 12)
        add_cylinder(bm, (0, 0, 0.03), (0, 0, 0.06), 0.08, (0.35, 0.28, 0.2), colors, 8)
    elif kind == 'skull_staff':
        add_cylinder(bm, (0, -0.75, 0), (0, 0.85, 0), 0.028, (0.16, 0.12, 0.1), colors, 6)
        for twist in range(3):
            angle = twist * 2.0 * math.pi / 3.0
            add_cylinder(bm, (0, 0.78, 0), (math.cos(angle) * 0.09, 1.0, math.sin(angle) * 0.09), 0.012, (0.16, 0.12, 0.1), colors, 4)
        add_sphere(bm, (0, 1.02, 0), (0.085, 0.095, 0.1), (0.8, 0.76, 0.64), colors)
        add_sphere(bm, (0, 1.02, -0.075), (0.05, 0.05, 0.03), (1.0, 0.45, 0.15), colors, glow=True)
    elif kind == 'robe':
        # a long robe from the hips to the ground, its hem torn
        rings = 7
        segments = 18
        verts = []
        for ring in range(rings):
            t = ring / (rings - 1)
            row = []
            for step in range(segments):
                angle = 2.0 * math.pi * step / segments
                ragged = 0.0 if ring < rings - 1 else (0.06 if step % 2 else -0.04)
                r = 0.17 + 0.17 * t
                row.append(bm.verts.new((math.cos(angle) * r, math.sin(angle) * r * 0.85 - 0.02, 0.12 - t * 1.0 + ragged)))
            verts.append(row)
        for ring in range(rings - 1):
            for step in range(segments):
                nxt = (step + 1) % segments
                face = bm.faces.new((verts[ring][step], verts[ring][nxt], verts[ring + 1][nxt], verts[ring + 1][step]))
                for loop in face.loops:
                    loop[colors] = tuple(color) + (0.0,)
    elif kind == 'hood':
        # open in front, so the skull shows in it (a front closed flat read as the back of the head, the mage turned round, 2026-11-13)
        made = bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=10, radius=1.0)
        faces = {f for v in made['verts'] for f in v.link_faces}
        opening = [f for f in faces if f.calc_center_median().y < -0.35 and f.calc_center_median().z < 0.55]
        bmesh.ops.delete(bm, geom=opening, context='FACES')
        for vert in bm.verts:
            vert.co = Vector((vert.co.x * 0.15, vert.co.y * 0.16 + 0.03, vert.co.z * 0.17 + 0.1))
        for face in bm.faces:
            for loop in face.loops:
                loop[colors] = (0.15, 0.13, 0.12, 0.0)
    elif kind == 'eyes':
        for side in (-1.0, 1.0):
            add_sphere(bm, (side * 0.035, 0.0, 0.0), (0.016, 0.01, 0.012), (1.0, 0.5, 0.2), colors, glow=True)
    elif kind == 'bat_wings':
        # the arm of a wing up and out from the back, its fingers fanning down and the membrane between them, leathery and dark
        bone = (0.16, 0.07, 0.07)
        skin = (0.1, 0.03, 0.04)
        for side in (-1.0, 1.0):
            root = Vector((side * 0.08, -0.02, 0.05))
            elbow = Vector((side * 0.42, -0.08, 0.4))
            hand = Vector((side * 0.78, -0.12, 0.58))
            tips = [Vector((side * 1.2, -0.16, 0.35)), Vector((side * 1.1, -0.15, -0.05)), Vector((side * 0.8, -0.12, -0.32)),
                Vector((side * 0.42, -0.08, -0.42))]
            for a, b in ((root, elbow), (elbow, hand)):
                add_cylinder(bm, tuple(a), tuple(b), 0.022, bone, colors, 5)
            for tip in tips[:3]:
                add_cylinder(bm, tuple(hand), tuple(tip), 0.01, bone, colors, 4, 0.004)
            # the membrane, a fan from the hand and the root, sagging between the fingers
            edge = [hand] + tips + [Vector((side * 0.1, -0.03, -0.3)), root, elbow]
            for index in range(1, len(edge) - 2):
                for a, b, c in ((hand, edge[index], edge[index + 1]),):
                    sag = (b + c) * 0.5 + Vector((0.0, 0.04, 0.0))
                    for tri in ((a, b, sag), (a, sag, c)):
                        face = bm.faces.new([bm.verts.new(tuple(v)) for v in tri])
                        for loop in face.loops:
                            loop[colors] = skin + (0.0,)
            face = bm.faces.new([bm.verts.new(tuple(v)) for v in (hand, elbow, root)])
            for loop in face.loops:
                loop[colors] = skin + (0.0,)
    elif kind == 'wings':
        for side in (-1.0, 1.0):
            for feather in range(6):
                angle = math.radians(20 + feather * 14)
                tip = Vector((side * math.cos(angle) * 1.1, -0.15 - 0.05 * feather, math.sin(angle) * 0.9))
                add_cylinder(bm, (side * 0.08, -0.1, 0.0), tuple(tip), 0.06 - feather * 0.005, (0.95, 0.93, 0.85), colors, 4, 0.012)
    mesh = bpy.data.meshes.new('Prop_' + kind)
    bm.to_mesh(mesh)
    bm.free()
    return bpy.data.objects.new('Prop_' + kind, mesh)


def to_armature(armature, direction):
    return (armature.matrix_world.to_3x3().inverted() @ Vector(direction)).normalized()


def hand_direction(armature, name):
    """Where a bone points in the world as posed now: towards the child furthest from it (the tails of imported bones point anywhere)"""
    bone = armature.pose.bones[name]
    if not bone.children:
        return (armature.matrix_world.to_3x3() @ (bone.matrix.to_3x3() @ Vector((0.0, 1.0, 0.0)))).normalized()
    child = max(bone.children, key=lambda each: (each.matrix.translation - bone.matrix.translation).length)
    return (armature.matrix_world.to_3x3() @ (child.matrix.translation - bone.matrix.translation)).normalized()


def turn_bone(armature, name, rotation):
    """Turns a bone, and what hangs on it, around its head by a rotation in the space of the skeleton"""
    bone = armature.pose.bones[name]
    pivot = Matrix.Translation(bone.matrix.translation)
    bone.matrix = pivot @ rotation.to_matrix().to_4x4() @ pivot.inverted() @ bone.matrix
    bpy.context.view_layer.update()


def aim_bone(armature, name, direction):
    """Points a bone towards a direction of the world, what hangs on it with it"""
    along = to_armature(armature, hand_direction(armature, name))
    turn_bone(armature, name, along.rotation_difference(to_armature(armature, direction)))


def set_action(armature, action, frame):
    armature.animation_data_create()
    armature.animation_data.action = action
    if action and hasattr(armature.animation_data, 'action_slot') and action.slots:
        armature.animation_data.action_slot = action.slots[0]
    bpy.context.scene.frame_set(int(frame))
    bpy.context.view_layer.update()


def aimed_matrix(armature, spec):
    """Where a prop held by aiming stands in the world in the stance shown now: its +Y along y, its +Z towards z"""
    bone = armature.pose.bones[spec['bone']]
    posed = armature.matrix_world @ bone.matrix
    hand = hand_direction(armature, spec['bone'])
    grip = posed.translation + hand * spec.get('grip', 0.0) + Vector(spec.get('shift', (0.0, 0.0, 0.0)))
    y = Vector(spec['y']).normalized()
    if spec.get('square'):
        y = (y - hand * y.dot(hand)).normalized()
    z = Vector(spec['z'])
    z = (z - y * z.dot(y)).normalized()
    x = y.cross(z)
    turn = Matrix((x, y, z)).transposed().to_4x4()
    return posed, Matrix.Translation(grip) @ turn @ Matrix.Scale(spec.get('size', 1.0), 4)


def bind_to(prop, armature, bone_name, weights=None):
    """Binds a prop to a bone, or its vertices by weights {vertex: bone}"""
    if weights:
        for vertex, name in weights.items():
            group = prop.vertex_groups.get(name) or prop.vertex_groups.new(name=name)
            group.add([vertex], 1.0, 'REPLACE')
    else:
        group = prop.vertex_groups.new(name=bone_name)
        group.add([vert.index for vert in prop.data.vertices], 1.0, 'REPLACE')
    modifier = prop.modifiers.new('Armature', 'ARMATURE')
    modifier.object = armature


SKELETON_BONE = (0.8, 0.75, 0.62)
SKELETON_JOINT = (0.55, 0.5, 0.4)
SKELETON_SOCKET = (0.04, 0.03, 0.03)


def make_skeleton_body(armature):
    """A skeleton of bones, made on the rig at rest, each piece bound to its bone: skull, spine, ribs, pelvis, limbs, hands and feet"""
    bm = bmesh.new()
    colors = bm.loops.layers.float_color.new('Col')
    # the bone of each vertex is marked on it as it is made, as the operators of bmesh reuse the slots of vertices they free, so the order of
    # making is not the order of the mesh (which bound pieces to the wrong bones until 2026-11-13: triangles stretched to where the hands rest)
    marks = bm.verts.layers.int.new('owner')
    names = [bone.name for bone in armature.data.bones]
    world = armature.matrix_world
    head = {bone.name: world @ bone.head_local for bone in armature.data.bones}

    def piece(name, build):
        build()
        for vert in bm.verts:
            if vert[marks] == 0:
                vert[marks] = names.index(name) + 1

    def limb(a, b, radius, name, end=None):
        piece(name, lambda: add_cylinder(bm, tuple(head[a]), tuple(head[b] if end is None else end), radius, SKELETON_BONE, colors, 8, radius * 0.8))
        piece(name, lambda: add_sphere(bm, tuple(head[a]), (radius * 1.5, radius * 1.5, radius * 1.5), SKELETON_JOINT, colors))

    # the spine and the pelvis
    chain = ['Hips', 'Abdomen', 'Torso', 'Chest', 'Neck', 'Head']
    for a, b in zip(chain, chain[1:]):
        for t in (0.25, 0.75):
            point = head[a].lerp(head[b], t) + Vector((0.0, 0.04, 0.0))
            piece(a, lambda point=point: add_sphere(bm, tuple(point), (0.03, 0.028, 0.022), SKELETON_BONE, colors))
    piece('Hips', lambda: add_sphere(bm, tuple(head['Hips'] + Vector((0.0, 0.0, 0.02))), (0.15, 0.08, 0.07), SKELETON_BONE, colors))
    # the ribs, arcs from the spine around to the breastbone
    for index, height in enumerate((1.25, 1.3, 1.35, 1.4, 1.45)):
        name = 'Torso' if index < 2 else 'Chest'
        spine = Vector((0.0, head['Chest'].y + 0.04, height))
        width = 0.16 - abs(index - 2) * 0.012
        for side in (-1.0, 1.0):
            points = [spine + Vector((side * math.sin(a) * width, -math.cos(a) * 0.11 + 0.0, -0.02 * math.sin(a))) for a in [k * math.pi * 0.9 / 6 for k in range(7)]]
            for p0, p1 in zip(points, points[1:]):
                piece(name, lambda p0=p0, p1=p1: add_cylinder(bm, tuple(p0), tuple(p1), 0.011, SKELETON_BONE, colors, 5))
    piece('Chest', lambda: add_cylinder(bm, (0.0, head['Chest'].y - 0.1, 1.24), (0.0, head['Chest'].y - 0.11, 1.46), 0.014, SKELETON_BONE, colors, 5))
    # the skull with its jaw and dark sockets
    skull = head['Head'] + Vector((0.0, -0.01, 0.11))
    piece('Head', lambda: add_sphere(bm, tuple(skull), (0.095, 0.11, 0.11), SKELETON_BONE, colors))
    piece('Head', lambda: add_sphere(bm, tuple(skull + Vector((0.0, -0.05, -0.08))), (0.07, 0.06, 0.035), SKELETON_BONE, colors))
    for side in (-1.0, 1.0):
        piece('Head', lambda side=side: add_sphere(bm, tuple(skull + Vector((side * 0.038, -0.095, 0.01))), (0.026, 0.014, 0.022), SKELETON_SOCKET, colors))
    # the arms and the legs
    for s in ('L', 'R'):
        limb('Shoulder.' + s, 'UpperArm.' + s, 0.014, 'Chest')
        limb('UpperArm.' + s, 'LowerArm.' + s, 0.02, 'UpperArm.' + s)
        limb('LowerArm.' + s, 'Wrist.' + s, 0.016, 'LowerArm.' + s)
        for finger in ('Index1', 'Middle1', 'Ring1', 'Pinky1', 'Thumb1'):
            name = '%s.%s' % (finger, s)
            if name in head:
                tip = head[name] + (head[name] - head['Wrist.' + s]).normalized() * 0.06
                piece(name, lambda name=name, tip=tip: add_cylinder(bm, tuple(head[name]), tuple(tip), 0.007, SKELETON_BONE, colors, 4))
                piece('Wrist.' + s, lambda name=name: add_cylinder(bm, tuple(head['Wrist.' + s]), tuple(head[name]), 0.008, SKELETON_BONE, colors, 4))
        limb('UpperLeg.' + s, 'LowerLeg.' + s, 0.024, 'UpperLeg.' + s)
        limb('LowerLeg.' + s, 'Foot.' + s, 0.019, 'LowerLeg.' + s)
        foot = head['Foot.' + s]
        piece('Foot.' + s, lambda foot=foot: add_sphere(bm, tuple(foot + Vector((0.0, -0.07, 0.02))), (0.04, 0.09, 0.025), SKELETON_BONE, colors))
    owner = {vert.index: names[vert[marks] - 1] for vert in bm.verts if vert[marks] > 0}
    bm.verts.index_update()
    owner = {vert.index: names[vert[marks] - 1] for vert in bm.verts if vert[marks] > 0}
    mesh = bpy.data.meshes.new('Skeleton')
    bm.to_mesh(mesh)
    bm.free()
    body = bpy.data.objects.new('Skeleton', mesh)
    bpy.context.scene.collection.objects.link(body)
    groups = {}
    for index, name in owner.items():
        if name not in groups:
            groups[name] = body.vertex_groups.new(name=name)
        groups[name].add([index], 1.0, 'REPLACE')
    body.modifiers.new('Armature', 'ARMATURE').object = armature
    return body


def make_bow_string(armature, bow_world):
    """The string of the bow, from tip to tip through its middle, where a bone of its own (BowString, on the hand of the bow) holds it, so it
    follows the other hand as the drawn shot pulls it"""
    top = bow_world @ Vector((0.0, 0.62, -BOW_DEPTH))
    bottom = bow_world @ Vector((0.0, -0.62, -BOW_DEPTH))
    middle = (top + bottom) * 0.5
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.mode_set(mode='EDIT')
    edit = armature.data.edit_bones.new('BowString')
    local = armature.matrix_world.inverted()
    edit.head = local @ middle
    edit.tail = local @ (middle + Vector((0.0, 0.0, 0.05)))
    edit.parent = armature.data.edit_bones['Wrist.L']
    bpy.ops.object.mode_set(mode='OBJECT')
    bm = bmesh.new()
    colors = bm.loops.layers.float_color.new('Col')
    weights = {}
    for a, b in ((top, middle), (middle, bottom)):
        before = len(bm.verts)
        add_cylinder(bm, tuple(a), tuple(b), 0.004, (0.85, 0.82, 0.72), colors, 4)
        bm.verts.ensure_lookup_table()
        for index in range(before, len(bm.verts)):
            weights[index] = 'BowString' if (bm.verts[index].co - middle).length < 0.05 else 'Wrist.L'
    mesh = bpy.data.meshes.new('Prop_bowstring')
    bm.to_mesh(mesh)
    bm.free()
    string = bpy.data.objects.new('Prop_bowstring', mesh)
    bpy.context.scene.collection.objects.link(string)
    bind_to(string, armature, None, weights)
    return string


def roll_upright(armature, name, local):
    """Rolls a hand about its forearm until what it holds (at local of the hand) stands with its +Y up"""
    hand = hand_direction(armature, name)
    held = (armature.matrix_world @ armature.pose.bones[name].matrix @ local).to_3x3() @ Vector((0.0, 1.0, 0.0))
    now = (held - hand * held.dot(hand)).normalized()
    up = Vector((0.0, 0.0, 1.0))
    want = (up - hand * up.dot(hand)).normalized()
    if now.length < 0.5 or want.length < 0.5:
        return
    angle = math.atan2(now.cross(want).dot(hand), now.dot(want))
    turn_bone(armature, name, Quaternion(to_armature(armature, hand), angle))


def make_bow_shot(armature, idle, bow_local, length=34):
    """The shot of a bow from its idle: the bow arm comes up towards the target, the other hand takes the string and draws it back to the
    chin, holds, lets go and goes back; BowString follows the drawing hand while it holds the string"""
    action = bpy.data.actions.new('BowShoot')
    bones = [bone.name for bone in armature.pose.bones]
    start, end = idle.frame_range
    # the share of the drawn pose at each key, and whether the hand holds the string
    keys = [(0, 0.0, False), (7, 0.6, False), (11, 1.0, True), (19, 1.0, True), (24, 1.0, True), (25, 0.92, False), (length, 0.0, False)]
    for frame, share, holds in keys:
        # the idle at this frame, then posed by hand with no action on the skeleton, as an action evaluated again overwrites a pose
        set_action(armature, idle, start + (frame % max(int(end - start), 1)))
        rest = {name: (armature.pose.bones[name].location.copy(), armature.pose.bones[name].rotation_quaternion.copy()) for name in bones}
        armature.animation_data.action = None
        for name in bones:
            armature.pose.bones[name].location, armature.pose.bones[name].rotation_quaternion = rest[name]
        bpy.context.view_layer.update()
        drawn = 11 <= frame <= 24
        aim_bone(armature, 'UpperArm.L', (0.2, -1.0, 0.08))
        aim_bone(armature, 'LowerArm.L', (0.1, -1.0, 0.1))
        aim_bone(armature, 'Wrist.L', (0.05, -1.0, 0.1))
        roll_upright(armature, 'Wrist.L', bow_local)
        if drawn:
            aim_bone(armature, 'UpperArm.R', (-0.95, 0.15, 0.08))
            aim_bone(armature, 'LowerArm.R', (0.95, -0.35, 0.2))
        else:
            aim_bone(armature, 'UpperArm.R', (-0.25, -1.0, 0.05))
            aim_bone(armature, 'LowerArm.R', (0.35, -1.0, 0.1))
        aimed = {name: (armature.pose.bones[name].location.copy(), armature.pose.bones[name].rotation_quaternion.copy()) for name in bones}
        for name in bones:
            bone = armature.pose.bones[name]
            bone.location = rest[name][0].lerp(aimed[name][0], share)
            bone.rotation_quaternion = rest[name][1].slerp(aimed[name][1], share)
        string = armature.pose.bones['BowString']
        string.location = Vector()
        string.rotation_quaternion = Quaternion()
        bpy.context.view_layer.update()
        if holds:
            # the string where the drawing hand is
            target = string.matrix.copy()
            target.translation = armature.pose.bones['Wrist.R'].matrix.translation
            string.matrix = target
            bpy.context.view_layer.update()
        values = {name: (armature.pose.bones[name].location.copy(), armature.pose.bones[name].rotation_quaternion.copy()) for name in bones}
        armature.animation_data.action = action
        for name in bones:
            bone = armature.pose.bones[name]
            bone.location, bone.rotation_quaternion = values[name]
            bone.keyframe_insert('location', frame=frame)
            bone.keyframe_insert('rotation_quaternion', frame=frame)
    return action


def import_source(where, name):
    before = set(bpy.data.objects)
    folder = HERO_SOURCES if where == 'hero' else SOURCES
    bpy.ops.import_scene.gltf(filepath=os.path.join(folder, name + '.glb'))
    return [obj for obj in bpy.data.objects if obj not in before]


def build(name, unit, preview):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    added = import_source(*unit['source'])
    armature = next(obj for obj in added if obj.type == 'ARMATURE')
    meshes = [obj for obj in added if obj.type == 'MESH' and obj.find_armature() == armature]
    for obj in added:
        if obj.type == 'MESH' and obj not in meshes:
            bpy.data.objects.remove(obj)
    if unit.get('body') == 'skeleton':
        for obj in meshes:
            bpy.data.objects.remove(obj)
        meshes = [make_skeleton_body(armature)]
    else:
        for obj in meshes:
            if unit.get('bone_look') and len(obj.data.polygons) > 400:
                smooth_for_look(obj, BONE_SUBDIVISIONS)
            corner_colors(obj, unit.get('side', []), unit.get('recolor', {}))
            if unit.get('bone_look'):
                paint_bone_look(obj, unit['bone_look'])
    for bone in armature.pose.bones:
        bone.rotation_mode = 'QUATERNION'
    idle_name = unit['actions'].get('Idle')
    idle = next((action for action in bpy.data.actions if action.name.split('|')[-1] == idle_name or action.name.endswith(idle_name)), None)
    # what it carries, at the rest pose of its bone, bound to that bone alone
    bpy.context.view_layer.update()
    bow_world = None
    for spec in unit.get('props', []):
        kind = spec['kind']
        bone_name = spec['bone']
        bone = armature.data.bones.get(bone_name)
        if not bone:
            print('UNITS %s has no bone %s for its %s' % (name, bone_name, kind))
            continue
        prop = make_prop(kind, spec.get('color', (0.18, 0.17, 0.17)))
        bpy.context.scene.collection.objects.link(prop)
        rest = armature.matrix_world @ bone.matrix_local
        if 'y' in spec:
            # aimed in the idle stance, then taken back to where its bone rests
            set_action(armature, idle, idle.frame_range[0] if idle else 0)
            posed, held = aimed_matrix(armature, spec)
            prop.matrix_world = rest @ posed.inverted() @ held
            if kind == 'bow':
                bow_world = (rest, posed, held)
        else:
            offset, turn, size = spec['offset'], spec['turn'], spec['size']
            local = Matrix.Translation(Vector(offset)) @ (Matrix.Rotation(math.radians(turn[0]), 4, 'X') @ Matrix.Rotation(math.radians(turn[1]), 4, 'Y')
                @ Matrix.Rotation(math.radians(turn[2]), 4, 'Z')) @ Matrix.Scale(size, 4)
            # the props are made in units of a model about 2 high, the bone in those of the model
            scale = armature.matrix_world.to_scale()
            prop.matrix_world = rest @ Matrix.Diagonal((1.0 / scale.x, 1.0 / scale.y, 1.0 / scale.z, 1.0)) @ local
        bind_to(prop, armature, bone_name)
        meshes.append(prop)
    if armature.animation_data:
        armature.animation_data.action = None
    bpy.context.view_layer.update()
    if unit.get('bow') and bow_world:
        # the string at rest, where the bow rests with its hand
        rest, posed, held = bow_world
        meshes.append(make_bow_string(armature, rest @ posed.inverted() @ held))
        for bone in armature.pose.bones:
            bone.rotation_mode = 'QUATERNION'
        make_bow_shot(armature, idle, posed.inverted() @ held)
        if armature.animation_data:
            armature.animation_data.action = None
    # one mesh with one material; the vertices of the body come first in it
    body_vertices = sum(len(obj.data.vertices) for obj in meshes if not obj.name.startswith('Prop_'))
    meshes.sort(key=lambda obj: obj.name.startswith('Prop_'))
    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes:
        obj.select_set(True)
        obj.data.materials.clear()
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = 'SK_Unit' + name
    body.data.name = 'SK_Unit' + name
    body.data.materials.append(bpy.data.materials.new('Unit'))
    colors = body.data.color_attributes['Col']
    body.data.color_attributes.active_color = colors
    body.data.color_attributes.render_color_index = list(body.data.color_attributes).index(colors)
    while body.data.uv_layers:
        body.data.uv_layers.remove(body.data.uv_layers[0])
    # the animations the battles read, by their names, the rest dropped
    kept = {}
    for wanted, ending in unit['actions'].items():
        found = next((action for action in bpy.data.actions if action.name.split('|')[-1] == ending or action.name.endswith(ending)), None)
        if not found:
            print('UNITS %s has no action %s for %s' % (name, ending, wanted))
            continue
        if found.name in [each.name for each in kept.values()]:
            found = found.copy()
        kept[wanted] = found
    for action in list(bpy.data.actions):
        if action not in kept.values():
            bpy.data.actions.remove(action)
    for wanted, action in kept.items():
        action.name = wanted
        action.use_fake_user = True
    body.data.calc_loop_triangles()
    # how high it is drawn at rest and where its foot is, in the units of the engine, for import_battle_units.py
    if armature.animation_data:
        armature.animation_data.action = None
    armature.data.pose_position = 'REST'
    bpy.context.view_layer.update()
    evaluated = body.evaluated_get(bpy.context.evaluated_depsgraph_get())
    # the body alone, without what it carries, which the colors of its corners tell apart: the props are made with the alpha 0 and colors of
    # their own, so the height is that of the vertices of the meshes of the model, counted before the props were joined
    heights = [(evaluated.matrix_world @ evaluated.data.vertices[index].co).z for index in range(min(body_vertices, len(evaluated.data.vertices)))]
    armature.data.pose_position = 'POSE'
    MEASURES[name] = {'height': (max(heights) - min(heights)) * 100.0, 'foot': min(heights) * 100.0}
    print('UNITS %s: %d triangles, %d bones, actions %s, %.0f high' % (name, len(body.data.loop_triangles), len(armature.data.bones), sorted(kept),
        MEASURES[name]['height']))
    if preview:
        render_preview(name, armature, kept)
    if armature.animation_data:
        armature.animation_data.action = None
    os.makedirs(OUTPUT, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    body.select_set(True)
    armature.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUTPUT, 'SK_Unit%s.glb' % name), export_format='GLB', use_selection=True, export_animations=True,
        export_animation_mode='ACTIONS', export_vertex_color='ACTIVE', export_all_vertex_colors=False, export_materials='EXPORT', export_yup=True,
        export_skins=True, export_def_bones=False, export_force_sampling=True)


def render_preview(name, armature, actions):
    scene = bpy.context.scene
    folder = os.path.join(OUTPUT, 'preview')
    os.makedirs(folder, exist_ok=True)
    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 4.4
    camera.location = (4.0, -6.0, 2.6)
    direction = Vector((0.0, 0.0, 0.9)) - camera.location
    camera.rotation_euler = direction.to_track_quat('-Z', 'Y').to_euler()
    scene.camera = camera
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'VERTEX'
    scene.render.resolution_x = 390
    scene.render.resolution_y = 450
    for wanted, action in actions.items():
        armature.animation_data_create()
        armature.animation_data.action = action
        if hasattr(armature.animation_data, 'action_slot') and action.slots:
            armature.animation_data.action_slot = action.slots[0]
        start, end = action.frame_range
        scene.frame_set(int(start + (end - start) * 0.45))
        scene.render.filepath = os.path.join(folder, '%s_%s.png' % (name, wanted))
        bpy.ops.render.render(write_still=True)


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    preview = 'preview' in arguments
    only = [word for word in arguments if word != 'preview']
    import json
    path = os.path.join(OUTPUT, 'units.json')
    if os.path.exists(path):
        with open(path, encoding='utf-8') as file:
            MEASURES.update(json.load(file))
    for name, unit in UNITS.items():
        if only and name not in only:
            continue
        build(name, unit, preview)
    os.makedirs(OUTPUT, exist_ok=True)
    with open(path, 'w', encoding='utf-8') as file:
        json.dump(MEASURES, file, indent=1, sort_keys=True)
    print('UNITS DONE')


main()
