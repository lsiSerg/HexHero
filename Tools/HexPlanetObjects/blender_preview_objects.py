"""Renders meshes of generate_planet_objects.py with the atlas of generate_foliage_atlas.py, for judging their shapes without the engine.

Run with Blender, the picture first, then the names of the meshes without SM_PlanetObject_, and optionally pitch=<degrees> of the camera and
spacing=<meters> between the meshes, less for small plants:
    blender --background --factory-startup --python blender_preview_objects.py -- <picture.png> Broadleaf_0 Spruce_0 Birch_0 pitch=45
The meshes stand in a row on a green ground under a sun, drawn as M_HexPlanetFoliage draws them: the color of the vertices times the tile of a
card, cut by its alpha, and the color of the vertices alone where the UV lies outside the atlas.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetObjects'))
SPACING = 0.34


def make_material():
    material = bpy.data.materials.new('Foliage')
    material.use_nodes = True
    nodes = material.node_tree.nodes
    links = material.node_tree.links
    shader = nodes['Principled BSDF']
    shader.inputs['Roughness'].default_value = 0.85

    image = nodes.new('ShaderNodeTexImage')
    image.image = bpy.data.images.load(os.path.join(FOLDER, 'T_HexFoliage.png'))
    uv = nodes.new('ShaderNodeUVMap')
    split = nodes.new('ShaderNodeSeparateXYZ')
    solid = nodes.new('ShaderNodeMath')
    solid.operation = 'GREATER_THAN'
    solid.inputs[1].default_value = 1.5
    links.new(uv.outputs['UV'], image.inputs['Vector'])
    links.new(uv.outputs['UV'], split.inputs['Vector'])
    links.new(split.outputs['X'], solid.inputs[0])

    tile = nodes.new('ShaderNodeMix')
    tile.data_type = 'RGBA'
    tile.inputs['B'].default_value = (1.0, 1.0, 1.0, 1.0)
    links.new(solid.outputs[0], tile.inputs['Factor'])
    links.new(image.outputs['Color'], tile.inputs['A'])
    colors = nodes.new('ShaderNodeVertexColor')
    color = nodes.new('ShaderNodeMix')
    color.data_type = 'RGBA'
    color.blend_type = 'MULTIPLY'
    color.inputs['Factor'].default_value = 1.0
    links.new(colors.outputs['Color'], color.inputs['A'])
    links.new(tile.outputs['Result'], color.inputs['B'])
    links.new(color.outputs['Result'], shader.inputs['Base Color'])

    alpha = nodes.new('ShaderNodeMath')
    alpha.operation = 'MAXIMUM'
    links.new(image.outputs['Alpha'], alpha.inputs[0])
    links.new(solid.outputs[0], alpha.inputs[1])
    cut = nodes.new('ShaderNodeMath')
    cut.operation = 'GREATER_THAN'
    cut.inputs[1].default_value = 0.33
    links.new(alpha.outputs[0], cut.inputs[0])
    links.new(cut.outputs[0], shader.inputs['Alpha'])
    # the engine lights a card by the normals of its vertices from both its sides, where Blender turns them to the camera on the back
    geometry = nodes.new('ShaderNodeNewGeometry')
    turn = nodes.new('ShaderNodeMath')
    turn.operation = 'MULTIPLY_ADD'
    turn.inputs[1].default_value = -2.0
    turn.inputs[2].default_value = 1.0
    links.new(geometry.outputs['Backfacing'], turn.inputs[0])
    normal = nodes.new('ShaderNodeVectorMath')
    normal.operation = 'SCALE'
    links.new(geometry.outputs['Normal'], normal.inputs[0])
    links.new(turn.outputs[0], normal.inputs['Scale'])
    links.new(normal.outputs['Vector'], shader.inputs['Normal'])
    return material


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:]
    picture = arguments[0]
    pitch = 45.0
    spacing = SPACING
    names = []
    for argument in arguments[1:]:
        if argument.startswith('pitch='):
            pitch = float(argument[6:])
        elif argument.startswith('spacing='):
            spacing = float(argument[8:])
        else:
            names.append(argument)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    material = make_material()
    for index, name in enumerate(names):
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.join(FOLDER, 'SM_PlanetObject_%s.glb' % name))
        for each in set(bpy.data.objects) - before:
            each.location.x += (index - (len(names) - 1) * 0.5) * spacing
            if each.type == 'MESH':
                each.data.materials.clear()
                each.data.materials.append(material)

    bpy.ops.mesh.primitive_plane_add(size=20.0)
    ground = bpy.context.object
    earth = bpy.data.materials.new('Ground')
    earth.use_nodes = True
    earth.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.09, 0.15, 0.04, 1.0)
    earth.node_tree.nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.9
    ground.data.materials.append(earth)

    sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN'))
    sun.data.energy = 4.0
    sun.data.angle = math.radians(2.0)
    sun.rotation_euler = (math.radians(50.0), 0.0, math.radians(-35.0))
    scene.collection.objects.link(sun)
    world = bpy.data.worlds.new('World')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.68, 0.9, 1.0)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.8
    scene.world = world

    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    scene.camera = camera
    width = max(len(names), 1) * spacing
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = width + spacing * 0.3
    target = Vector((0.0, 0.0, spacing * 0.38))
    back = Vector((0.0, -math.cos(math.radians(pitch)), math.sin(math.radians(pitch))))
    camera.location = target + back * 6.0
    camera.rotation_euler = (-back).to_track_quat('-Z', 'Y').to_euler()

    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = 24
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1900
    scene.render.resolution_y = int(1900 * min(max(1.8 / max(len(names), 1), 0.3), 1.0))
    scene.render.filepath = picture
    scene.view_settings.view_transform = 'Standard'
    bpy.ops.render.render(write_still=True)


main()
