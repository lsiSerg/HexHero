"""Renders the meshes of the entrances and exits that generate_planet_entrances.py wrote, in a row, for judging their shapes without the engine:
the colors of their vertices are drawn as the material of the game reads them, grey rock, worked stone by the green, darker by the blue, lit
by the red and glowing by the alpha.

    blender --background --factory-startup --python blender_preview_entrances.py -- <picture.png> [pitch=35] [yaw=30]
"""

import math
import os
import sys

import bpy
from mathutils import Vector

FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetMarkers'))
NAMES = ['SM_PlanetEntranceCrag', 'SM_PlanetEntranceMaw', 'SM_PlanetEntranceGate', 'SM_PlanetEntranceSpur', 'SM_PlanetEntranceSpurWide', 'SM_PlanetExitNiche', 'SM_PlanetExitNicheCrystal', 'SM_PlanetExitCave',
    'SM_PlanetExitGate', 'SM_PlanetExitTwin']


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    picture = arguments[0] if arguments else os.path.join(FOLDER, 'preview.png')
    options = dict(each.split('=') for each in arguments[1:] if '=' in each)
    pitch = math.radians(float(options.get('pitch', 35)))
    yaw = math.radians(float(options.get('yaw', 30)))

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    material = bpy.data.materials.new('Preview')
    material.use_nodes = True
    nodes = material.node_tree.nodes
    attribute = nodes.new('ShaderNodeAttribute')
    attribute.attribute_name = 'Shown'
    material.node_tree.links.new(attribute.outputs['Color'], nodes['Principled BSDF'].inputs['Base Color'])
    material.node_tree.links.new(attribute.outputs['Color'], nodes['Principled BSDF'].inputs['Emission Color'])
    nodes['Principled BSDF'].inputs['Emission Strength'].default_value = 0.15
    nodes['Principled BSDF'].inputs['Roughness'].default_value = 0.9

    for index, name in enumerate(NAMES):
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=os.path.join(FOLDER, name + '.glb'))
        for each in set(bpy.data.objects) - before:
            if each.type != 'MESH':
                continue
            each.location.y -= (index - (len(NAMES) - 1) / 2.0) * 0.024
            mesh = each.data
            source = mesh.color_attributes[0] if mesh.color_attributes else None
            shown = mesh.color_attributes.new('Shown', 'FLOAT_COLOR', 'CORNER')
            for loop in range(len(mesh.loops)):
                red, green, blue, alpha = source.data[loop if source.domain == 'CORNER' else mesh.loops[loop].vertex_index].color_srgb if source else (0, 0, 0, 0)
                rock = Vector((0.36, 0.33, 0.3)).lerp(Vector((0.5, 0.45, 0.38)), green) * (1.0 - 0.8 * blue)
                rock = rock + Vector((1.0, 0.9, 0.7)) * red * 0.5
                rock = rock.lerp(Vector((0.2, 1.2, 1.4)), alpha)
                shown.data[loop].color = (rock.x, rock.y, rock.z, 1.0)
            mesh.materials.clear()
            mesh.materials.append(material)

    bpy.ops.mesh.primitive_plane_add(size=1.0)
    ground = bpy.context.object
    ground_material = bpy.data.materials.new('Ground')
    ground_material.use_nodes = True
    ground_material.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value = (0.12, 0.2, 0.06, 1.0)
    ground.data.materials.append(ground_material)

    sun = bpy.data.objects.new('Sun', bpy.data.lights.new('Sun', 'SUN'))
    sun.data.energy = 3.0
    sun.rotation_euler = (math.radians(50), 0.0, math.radians(120))
    scene.collection.objects.link(sun)
    scene.world = bpy.data.worlds.new('World')
    scene.world.color = (0.25, 0.27, 0.3)

    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    scene.collection.objects.link(camera)
    distance = 0.2
    scene.view_settings.view_transform = 'Standard'
    target = Vector((0.0, 0.0, 0.006))
    camera.location = target + Vector((math.cos(yaw) * math.cos(pitch), math.sin(yaw) * math.cos(pitch), math.sin(pitch))) * distance
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera.data.lens = 35
    camera.data.clip_start = 0.001
    scene.camera = camera
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 1800
    scene.render.resolution_y = 700
    scene.render.filepath = picture
    bpy.ops.render.render(write_still=True)
    print('wrote', picture)


main()
