"""Reduces the photo-scanned obstacles of battle_model_sources.json to about the triangles of their lines and writes each as
Saved/BattleObstacles/SM_BattleObstacle_<Name>.glb with its UV, standing on its origin (the middle of its foot), with obstacles.json, what was written; the
textures stay the downloaded ones, which import_battle_obstacles.py imports beside the meshes. Run with Blender:

  blender --background --factory-startup --python blender_build_obstacles.py [-- preview] [-- <Name> ...]

`preview` also renders the reduced meshes with their textures in a row to Saved/BattleObstacles/preview.png, for judging them without the engine.
"""
import json
import math
import os
import sys

import bpy
import mathutils

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SOURCES = os.path.join(HERE, 'battle_model_sources.json')
DOWNLOADS = os.path.join(ROOT, 'Saved', 'ModelSources', 'battle')
OUT = os.path.join(ROOT, 'Saved', 'BattleObstacles')
# the shares of the triangles of the LODs after the first, and the screen sizes from which each LOD draws
LOD_SHARES = [0.4, 0.15]
LOD_SCREENS = [1.0, 0.12, 0.04]


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def triangles(objects):
    count = 0
    for each in objects:
        for polygon in each.data.polygons:
            count += len(polygon.vertices) - 2
    return count


def build(model, resolution):
    clear()
    source = os.path.join(DOWNLOADS, model['id'], '%s.gltf' % model['id'])
    if not os.path.exists(source):
        raise RuntimeError('%s is missing: run fetch_battle_models.ps1 first' % source)
    bpy.ops.import_scene.gltf(filepath=source)
    meshes = [each for each in bpy.context.scene.objects if each.type == 'MESH']
    if not meshes:
        raise RuntimeError('%s has no mesh' % model['id'])
    bpy.ops.object.select_all(action='DESELECT')
    for each in meshes:
        each.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    if len(meshes) > 1:
        bpy.ops.object.join()
    mesh = bpy.context.view_layer.objects.active
    # free of its parents, with its transform applied
    bpy.ops.object.parent_clear(type='CLEAR_KEEP_TRANSFORM')
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    # the glTF splits the vertices along the seams of its UV, where a collapse cannot go, so they are joined again (the UV is per corner)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.remove_doubles(threshold=0.00005)
    bpy.ops.object.mode_set(mode='OBJECT')
    before = triangles([mesh])
    target = model['triangles']
    # a collapse stops short where the seams of the UV hold it, so it is done again on what is left
    for _ in range(6):
        now = triangles([mesh])
        if now <= target * 1.05:
            break
        modifier = mesh.modifiers.new('Reduce', 'DECIMATE')
        modifier.decimate_type = 'COLLAPSE'
        modifier.ratio = target / now
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    # smooth normals across the reduced faces, as the scan is a smooth surface whose detail its normal map holds
    for polygon in mesh.data.polygons:
        polygon.use_smooth = True
    # standing on its origin, the middle of its foot
    corners = [mesh.matrix_world @ mathutils.Vector(corner) for corner in mesh.bound_box]
    low = mathutils.Vector((min(c.x for c in corners), min(c.y for c in corners), min(c.z for c in corners)))
    high = mathutils.Vector((max(c.x for c in corners), max(c.y for c in corners), max(c.z for c in corners)))
    middle = mathutils.Vector(((low.x + high.x) * 0.5, (low.y + high.y) * 0.5, low.z))
    mesh.data.transform(mathutils.Matrix.Translation(-middle))
    mesh.location = (0.0, 0.0, 0.0)
    mesh.name = model['name']
    after = triangles([mesh])
    materials = [slot.material.name for slot in mesh.material_slots if slot.material]
    os.makedirs(OUT, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    mesh.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, 'SM_BattleObstacle_%s.glb' % model['name']), export_format='GLB', use_selection=True,
        export_materials='NONE', export_texcoords=True, export_normals=True, export_yup=True)
    # the LODs reduced here with smooth normals, as those the engine reduced showed hard facets and some came out black
    lods = []
    for level, share in enumerate(LOD_SHARES, start=1):
        copy = mesh.copy()
        copy.data = mesh.data.copy()
        bpy.context.scene.collection.objects.link(copy)
        bpy.ops.object.select_all(action='DESELECT')
        copy.select_set(True)
        bpy.context.view_layer.objects.active = copy
        modifier = copy.modifiers.new('Reduce', 'DECIMATE')
        modifier.decimate_type = 'COLLAPSE'
        modifier.ratio = share
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        for polygon in copy.data.polygons:
            polygon.use_smooth = True
        name = 'SM_BattleObstacle_%s_LOD%d' % (model['name'], level)
        bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name + '.glb'), export_format='GLB', use_selection=True,
            export_materials='NONE', export_texcoords=True, export_normals=True, export_yup=True)
        lods.append(name)
        bpy.data.objects.remove(copy)
    folder = os.path.join(DOWNLOADS, model['id'])
    size = high - low
    return {
        'name': model['name'], 'id': model['id'], 'lods': lods, 'screens': LOD_SCREENS, 'grounds': model['grounds'], 'triangles': after, 'source_triangles': before,
        'size': [round(size.x, 3), round(size.y, 3), round(size.z, 3)], 'materials': len(materials),
        'color': os.path.join(folder, 'textures', '%s_diff_%s.jpg' % (model['id'], resolution)),
        'normal': os.path.join(folder, '%s_nor_dx.jpg' % model['id']),
        'arm': os.path.join(folder, 'textures', '%s_arm_%s.jpg' % (model['id'], resolution)),
    }


def preview(entries):
    clear()
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 2400
    scene.render.resolution_y = 700
    world = bpy.data.worlds.new('World')
    world.color = (0.45, 0.5, 0.6)
    scene.world = world
    x = 0.0
    for entry in entries:
        bpy.ops.import_scene.gltf(filepath=os.path.join(OUT, 'SM_BattleObstacle_%s.glb' % entry['name']))
        mesh = [each for each in bpy.context.selected_objects if each.type == 'MESH'][0]
        material = bpy.data.materials.new(entry['name'])
        material.use_nodes = True
        nodes = material.node_tree.nodes
        shader = nodes['Principled BSDF']
        color = nodes.new('ShaderNodeTexImage')
        color.image = bpy.data.images.load(entry['color'])
        material.node_tree.links.new(color.outputs['Color'], shader.inputs['Base Color'])
        arm = nodes.new('ShaderNodeTexImage')
        arm.image = bpy.data.images.load(entry['arm'])
        arm.image.colorspace_settings.name = 'Non-Color'
        split = nodes.new('ShaderNodeSeparateColor')
        material.node_tree.links.new(arm.outputs['Color'], split.inputs['Color'])
        material.node_tree.links.new(split.outputs['Green'], shader.inputs['Roughness'])
        normal_image = nodes.new('ShaderNodeTexImage')
        normal_image.image = bpy.data.images.load(os.path.join(os.path.dirname(entry['color']), '%s_nor_gl_1k.jpg' % entry['id']))
        normal_image.image.colorspace_settings.name = 'Non-Color'
        normal = nodes.new('ShaderNodeNormalMap')
        material.node_tree.links.new(normal_image.outputs['Color'], normal.inputs['Color'])
        material.node_tree.links.new(normal.outputs['Normal'], shader.inputs['Normal'])
        mesh.data.materials.clear()
        mesh.data.materials.append(material)
        # every one about as large, as the field fits them to a hex
        extent = max(mesh.dimensions.x, mesh.dimensions.y, 0.01)
        scale = 1.6 / extent
        mesh.scale = (scale, scale, scale)
        mesh.location = (x, 0.0, 0.0)
        x += 2.0
    bpy.ops.object.light_add(type='SUN', rotation=(math.radians(50), 0.0, math.radians(30)))
    bpy.context.object.data.energy = 4.0
    bpy.ops.object.camera_add(location=((x - 2.0) * 0.5, -9.0, 5.5), rotation=(math.radians(60), 0.0, 0.0))
    camera = bpy.context.object
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = x + 0.5
    scene.camera = camera
    scene.render.filepath = os.path.join(OUT, 'preview.png')
    bpy.ops.render.render(write_still=True)


def main():
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    with open(SOURCES, encoding='utf-8') as file:
        sources = json.load(file)
    names = [each for each in args if each != 'preview']
    entries = []
    for model in sources['models']:
        if names and model['name'] not in names:
            continue
        entry = build(model, sources['resolution'])
        entries.append(entry)
        print('OBSTACLE %s: %d -> %d triangles, %s m, %d materials' % (entry['name'], entry['source_triangles'], entry['triangles'], entry['size'], entry['materials']))
    if not names:
        with open(os.path.join(OUT, 'obstacles.json'), 'w', encoding='utf-8') as file:
            json.dump({'obstacles': entries}, file, indent=1)
    if 'preview' in args:
        preview(entries)
    print('OBSTACLES DONE')


main()
