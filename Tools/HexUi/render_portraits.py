"""Renders the pictures of the panel of a castle, as the developer asked on 2026-11-13 for the creatures to hire shown as renders and the
buildings as a tree of pictures: a portrait of each figure of the battles (Saved/BattleUnits/SK_Unit<Name>.glb, the rider of the heroes for
the riders) in its idle, three quarters from the front, and a picture of each building of the castles (Saved/HexPlanetCastles), its stone
drawn with the masonry of men; on a clear background, 256 a side, to Saved/HexUi/Portraits/T_Portrait_<Name>.png and T_Building_<Name>.png,
which import_portraits.py imports. Run with Blender:

  blender --background --factory-startup --python render_portraits.py
"""
import math
import os

import bpy
from mathutils import Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
UNITS = os.path.join(ROOT, 'Saved', 'BattleUnits')
# the rider of the battles, with its lance, else that of the heroes
RIDER = os.path.join(ROOT, 'Saved', 'HexPlanetHeroes', 'SK_UnitRider.glb')
if not os.path.exists(RIDER):
    RIDER = os.path.join(ROOT, 'Saved', 'HexPlanetHeroes', 'SK_PlanetRider.glb')
CASTLES = os.path.join(ROOT, 'Saved', 'HexPlanetCastles')
OUT = os.path.join(ROOT, 'Saved', 'HexUi', 'Portraits')
SIZE = 256
FIGURES = ['Sentinel', 'Huntsman', 'Glowwing', 'Oathsworn', 'Pilgrim', 'SkyWarden', 'BoneWarrior', 'Decayed', 'Shade', 'BloodDrinker', 'AshMage', 'BoneSerpent']
BUILDINGS = ['Hall1', 'Hall2', 'Hall3', 'Fort1', 'Fort2', 'Fort3'] + ['Dwelling%d' % level for level in range(1, 8)] + ['Upgrade']


def scene_setup():
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = SIZE
    scene.render.resolution_y = SIZE
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    world = bpy.data.worlds.new('World')
    world.color = (0.35, 0.37, 0.42)
    scene.world = world
    bpy.ops.object.light_add(type='SUN', rotation=(math.radians(50), math.radians(10), math.radians(-35)))
    bpy.context.object.data.energy = 3.5
    bpy.ops.object.light_add(type='SUN', rotation=(math.radians(70), 0.0, math.radians(150)))
    bpy.context.object.data.energy = 1.0
    return scene


def vertex_material(texture=None):
    """The colors of the vertices, and for the stone of a building (its vertex alpha about 0.1) the masonry times them"""
    material = bpy.data.materials.new('Vertex')
    material.use_nodes = True
    tree = material.node_tree
    shader = tree.nodes['Principled BSDF']
    shader.inputs['Roughness'].default_value = 0.7
    attribute = tree.nodes.new('ShaderNodeVertexColor')
    if texture and os.path.exists(texture):
        image = tree.nodes.new('ShaderNodeTexImage')
        image.image = bpy.data.images.load(texture)
        coords = tree.nodes.new('ShaderNodeUVMap')
        tree.links.new(coords.outputs['UV'], image.inputs['Vector'])
        stone = tree.nodes.new('ShaderNodeMix')
        stone.data_type = 'RGBA'
        stone.blend_type = 'MULTIPLY'
        stone.inputs['Factor'].default_value = 1.0
        tree.links.new(attribute.outputs['Color'], stone.inputs[6])
        tree.links.new(image.outputs['Color'], stone.inputs[7])
        bright = tree.nodes.new('ShaderNodeMix')
        bright.data_type = 'RGBA'
        bright.blend_type = 'MULTIPLY'
        bright.inputs['Factor'].default_value = 1.0
        tree.links.new(stone.outputs[2], bright.inputs[6])
        bright.inputs[7].default_value = (2.4, 2.4, 2.4, 1.0)
        # stone where the alpha is low, the plain colors elsewhere
        less = tree.nodes.new('ShaderNodeMath')
        less.operation = 'LESS_THAN'
        less.inputs[1].default_value = 0.2
        tree.links.new(attribute.outputs['Alpha'], less.inputs[0])
        choose = tree.nodes.new('ShaderNodeMix')
        choose.data_type = 'RGBA'
        tree.links.new(less.outputs['Value'], choose.inputs['Factor'])
        tree.links.new(attribute.outputs['Color'], choose.inputs[6])
        tree.links.new(bright.outputs[2], choose.inputs[7])
        # the roofs (the alpha about 0.5) in the clay of their tiles
        roof = tree.nodes.new('ShaderNodeMath')
        roof.operation = 'COMPARE'
        roof.inputs[1].default_value = 0.5
        roof.inputs[2].default_value = 0.2
        tree.links.new(attribute.outputs['Alpha'], roof.inputs[0])
        clay = tree.nodes.new('ShaderNodeMix')
        clay.data_type = 'RGBA'
        tree.links.new(roof.outputs['Value'], clay.inputs['Factor'])
        tree.links.new(choose.outputs[2], clay.inputs[6])
        clay.inputs[7].default_value = (0.42, 0.18, 0.1, 1.0)
        tree.links.new(clay.outputs[2], shader.inputs['Base Color'])
    else:
        tree.links.new(attribute.outputs['Color'], shader.inputs['Base Color'])
    return material


def frame(scene, objects, low_share=0.0, top_share=1.0, yaw=35.0, pitch=12.0, margin=1.15, by_height=False):
    """An orthographic camera three quarters from the front (-Y), framing the part of the objects between low_share and top_share of their height;
    by_height frames that part by its height alone, so what a figure holds out to its side does not make the figure small"""
    bpy.context.view_layer.update()
    points = []
    depsgraph = bpy.context.evaluated_depsgraph_get()
    for obj in objects:
        evaluated = obj.evaluated_get(depsgraph)
        mesh = evaluated.to_mesh()
        points += [evaluated.matrix_world @ v.co for v in mesh.vertices]
        evaluated.to_mesh_clear()
    low = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    high = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    z0 = low.z + (high.z - low.z) * low_share
    z1 = low.z + (high.z - low.z) * top_share
    middle = Vector(((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, (z0 + z1) * 0.5))
    span = (z1 - z0 if by_height else max(high.x - low.x, high.y - low.y, z1 - z0)) * margin
    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = span
    direction = Vector((math.sin(math.radians(yaw)) * math.cos(math.radians(pitch)), -math.cos(math.radians(yaw)) * math.cos(math.radians(pitch)),
        math.sin(math.radians(pitch))))
    camera.location = middle + direction * span * 4.0
    camera.rotation_euler = (middle - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.clip_end = span * 20.0
    scene.camera = camera


def render(scene, name):
    os.makedirs(OUT, exist_ok=True)
    scene.render.filepath = os.path.join(OUT, name + '.png')
    bpy.ops.render.render(write_still=True)
    print('PORTRAIT', name)


def figure(name, path, idle_names):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = scene_setup()
    bpy.ops.import_scene.gltf(filepath=path)
    # the shapes the importer makes for the bones (an icosphere of no collection) are no part of the figure
    meshes = [o for o in bpy.data.objects if o.type == 'MESH' and o.users_collection and not o.name.startswith('Icosphere')]
    armature = next((o for o in bpy.data.objects if o.type == 'ARMATURE'), None)
    material = vertex_material()
    for mesh in meshes:
        mesh.data.materials.clear()
        mesh.data.materials.append(material)
    if armature:
        action = next((a for a in bpy.data.actions if a.name.split('|')[-1] in idle_names), None)
        if action:
            armature.animation_data_create()
            armature.animation_data.action = action
            if action.slots:
                armature.animation_data.action_slot = action.slots[0]
            scene.frame_set(int(action.frame_range[0]))
    # the upper part of a figure that stands, the whole of a beast
    whole = name in ('Glowwing', 'BoneSerpent', 'Rider')
    # a figure from its knees up, its head near the top
    # (the zombie, short and with its arms out, whole by its height)
    if name == 'Decayed':
        frame(scene, meshes, 0.0, 1.0, 30.0, 10.0, 1.1, True)
    else:
        frame(scene, meshes, 0.0 if whole else 0.3, 0.97 if not whole else 1.0, 30.0, 10.0, 1.1 if whole else 1.05, not whole)
    render(scene, 'T_Portrait_' + name)


def building(name):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = scene_setup()
    bpy.ops.import_scene.gltf(filepath=os.path.join(CASTLES, 'SM_PlanetCastle%s.glb' % name))
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    material = vertex_material(os.path.join(CASTLES, 'T_CastleWall_Human.png'))
    for mesh in meshes:
        mesh.data.materials.clear()
        mesh.data.materials.append(material)
    frame(scene, meshes, 0.0, 1.0, 35.0, 25.0, 1.1)
    render(scene, 'T_Building_' + name)


def main():
    for name in FIGURES:
        path = os.path.join(UNITS, 'SK_Unit%s.glb' % name)
        if os.path.exists(path):
            figure(name, path, ('Idle',))
    if os.path.exists(RIDER):
        figure('Rider', RIDER, ('Idle',))
    for name in BUILDINGS:
        if os.path.exists(os.path.join(CASTLES, 'SM_PlanetCastle%s.glb' % name)):
            building(name)
    print('PORTRAITS DONE')


main()
