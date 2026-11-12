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
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SOURCES = os.path.join(ROOT, 'Saved', 'ModelSources', 'units')
HERO_SOURCES = os.path.join(ROOT, 'Saved', 'ModelSources', 'hero')
OUTPUT = os.path.join(ROOT, 'Saved', 'BattleUnits')

# what each creature is built from: its model, the materials that take the color of its side, the colors some materials take in its place,
# what it carries, bound to which bone and how (an offset and a turn in the space of the bone at rest, a size), and its animations by the
# name the battles read, each the action of the model whose name ends so
KNIGHT_ACTIONS = {'Idle': 'Idle_Sword', 'Move': 'Run', 'Attack': 'Sword_Slash', 'Hit': 'HitRecieve', 'Death': 'Death'}
ADVENTURER_ACTIONS = {'Idle': 'Idle', 'Move': 'Run', 'Attack': 'Sword_Slash', 'Shoot': 'Gun_Shoot', 'Hit': 'HitRecieve', 'Death': 'Death'}
UNITS = {
    'Sentinel': {'source': ('hero', 'knight'), 'side': ['Blue'], 'actions': KNIGHT_ACTIONS,
        'props': [('spear', 'Wrist.R', (0.0, 0.08, 0.0), (90.0, 0.0, 0.0), 1.0), ('shield', 'Wrist.L', (0.0, 0.05, 0.05), (0.0, 0.0, 90.0), 0.9)]},
    'Huntsman': {'source': ('units', 'y9KWOVG21R'), 'side': ['Brown2'], 'actions': ADVENTURER_ACTIONS,
        'props': [('bow', 'Wrist.L', (0.0, 0.06, 0.0), (0.0, 0.0, 0.0), 1.0)]},
    'Oathsworn': {'source': ('hero', 'knight'), 'side': ['Blue'], 'actions': KNIGHT_ACTIONS,
        'props': [('sword', 'Wrist.R', (0.0, 0.08, 0.0), (90.0, 0.0, 0.0), 1.0), ('shield', 'Wrist.L', (0.0, 0.05, 0.05), (0.0, 0.0, 90.0), 1.0)]},
    'Pilgrim': {'source': ('units', 'y9KWOVG21R'), 'side': ['Brown2'], 'recolor': {'Brown': (0.32, 0.22, 0.14), 'LightBrown': (0.45, 0.33, 0.22)},
        'actions': ADVENTURER_ACTIONS, 'props': [('staff', 'Wrist.R', (0.0, 0.07, 0.0), (90.0, 0.0, 0.0), 1.0)]},
    'SkyWarden': {'source': ('hero', 'knight'), 'side': ['Blue'], 'recolor': {'Metal': (0.85, 0.82, 0.7)}, 'actions': KNIGHT_ACTIONS,
        'props': [('sword', 'Wrist.R', (0.0, 0.08, 0.0), (90.0, 0.0, 0.0), 1.15), ('wings', 'Chest', (0.0, 0.0, -0.18), (0.0, 0.0, 0.0), 1.0)]},
    'BoneWarrior': {'source': ('units', 'wODZYCgX5Z'), 'side': [], 'actions': {'Idle': 'Skeleton_Idle', 'Move': 'Skeleton_Running', 'Attack': 'Skeleton_Attack',
        'Death': 'Skeleton_Death'}, 'props': []},
    'Decayed': {'source': ('units', 'VlXjG0N8Eg'), 'side': [], 'actions': {'Idle': 'Idle', 'Move': 'Walk', 'Attack': 'Punch', 'Hit': 'HitReact', 'Death': 'Death'},
        'props': []},
    'Shade': {'source': ('units', 'Iip30bDHmu'), 'side': [], 'actions': {'Idle': 'Flying_Idle', 'Move': 'Fast_Flying', 'Attack': 'Punch', 'Hit': 'HitReact',
        'Death': 'Death'}, 'props': []},
    'BloodDrinker': {'source': ('units', 'y9KWOVG21R'), 'side': ['Brown2'], 'recolor': {'Skin': (0.78, 0.76, 0.8), 'Brown': (0.12, 0.05, 0.07),
        'LightBrown': (0.35, 0.05, 0.08), 'DarkBrown': (0.08, 0.04, 0.05), 'Brown2': (0.5, 0.05, 0.08)}, 'actions': ADVENTURER_ACTIONS,
        'props': [('wings', 'Chest', (0.0, 0.0, -0.16), (0.0, 0.0, 0.0), 0.8)]},
    'AshMage': {'source': ('units', 'wODZYCgX5Z'), 'side': [], 'actions': {'Idle': 'Skeleton_Idle', 'Move': 'Skeleton_Running', 'Attack': 'Skeleton_Attack',
        'Shoot': 'Skeleton_Attack', 'Death': 'Skeleton_Death'}, 'props': []},
    'Glowwing': {'source': ('units', 'LlwD0QNUPj'), 'side': ['Dragon_Secondary'], 'actions': {'Idle': 'Flying_Idle', 'Move': 'Fast_Flying', 'Attack': 'Headbutt',
        'Hit': 'HitReact', 'Death': 'Death'}, 'props': []},
    'BoneSerpent': {'source': ('units', 'VBvzjFIYws'), 'side': [], 'recolor': {'Main': (0.82, 0.78, 0.68), 'Belly': (0.6, 0.56, 0.48), 'Wings': (0.45, 0.42, 0.38)},
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


def make_prop(kind):
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
        steps = 10
        for step in range(steps):
            a0 = -1.0 + 2.0 * step / steps
            a1 = -1.0 + 2.0 * (step + 1) / steps
            p0 = (0.0, a0 * 0.6, 0.18 * (1.0 - a0 * a0))
            p1 = (0.0, a1 * 0.6, 0.18 * (1.0 - a1 * a1))
            add_cylinder(bm, p0, p1, 0.022, PROP_WOOD, colors, 6)
        add_cylinder(bm, (0, -0.6, 0), (0, 0.6, 0), 0.006, (0.85, 0.85, 0.8), colors, 4)
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
    for obj in meshes:
        corner_colors(obj, unit.get('side', []), unit.get('recolor', {}))
    # what it carries, at the rest pose of its bone, bound to that bone alone
    bpy.context.view_layer.update()
    for kind, bone_name, offset, turn, size in unit.get('props', []):
        bone = armature.data.bones.get(bone_name)
        if not bone:
            print('UNITS %s has no bone %s for its %s' % (name, bone_name, kind))
            continue
        prop = make_prop(kind)
        bpy.context.scene.collection.objects.link(prop)
        rest = armature.matrix_world @ bone.matrix_local
        local = Matrix.Translation(Vector(offset)) @ (Matrix.Rotation(math.radians(turn[0]), 4, 'X') @ Matrix.Rotation(math.radians(turn[1]), 4, 'Y')
            @ Matrix.Rotation(math.radians(turn[2]), 4, 'Z')) @ Matrix.Scale(size, 4)
        # the props are made in units of a model about 2 high, the bone in those of the model
        scale = armature.matrix_world.to_scale()
        prop.matrix_world = rest @ Matrix.Diagonal((1.0 / scale.x, 1.0 / scale.y, 1.0 / scale.z, 1.0)) @ local
        group = prop.vertex_groups.new(name=bone_name)
        group.add([vert.index for vert in prop.data.vertices], 1.0, 'REPLACE')
        modifier = prop.modifiers.new('Armature', 'ARMATURE')
        modifier.object = armature
        meshes.append(prop)
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
    scene.render.resolution_x = 260
    scene.render.resolution_y = 300
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
