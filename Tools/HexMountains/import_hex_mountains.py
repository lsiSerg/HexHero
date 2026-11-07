"""Imports what blender_build_mountains.py made and reports whether the meshes came through as the game expects them.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
"""

import glob
import json
import math
import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexMountains', 'Game')
DESTINATION = '/Game/HexMap/Meshes/Mountains'
TEXTURES = '/Game/HexMap/Textures'

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mountain_params.json'), encoding='utf-8') as params_file:
    PARAMS = json.load(params_file)
SADDLE_HEIGHT = PARAMS['saddle_height']
FOOT_HEIGHT = PARAMS['foot_height']


def report(mesh, name):
    """Checks the middle of every side, which shows if the mesh is mirrored or turned, and that every triangle faces up."""
    description = mesh.get_static_mesh_description(0)
    positions = {}
    for index in range(description.get_vertex_count()):
        position = description.get_vertex_position(unreal.VertexID(index))
        positions[(round(position.x), round(position.y))] = position.z

    wrong_sides = 0
    digits = name.split('_')[2]
    for direction, digit in enumerate(digits):
        angle = math.radians(30.0 + 60.0 * direction)
        height = positions.get((round(math.cos(angle) * PARAMS['hex_radius'] * 0.866025), round(math.sin(angle) * PARAMS['hex_radius'] * 0.866025)))
        # the base goes on past a side next to a notch, so that side can have any height
        if digit == '0' and (digits[direction - 1] == '1' or digits[(direction + 1) % 6] == '1'):
            continue
        expected = SADDLE_HEIGHT if digit == '1' else FOOT_HEIGHT
        if height is None or abs(height - expected) > 0.5:
            wrong_sides += 1

    facing_up = 0
    for index in range(description.get_triangle_count()):
        triangle = unreal.TriangleID(index)
        a, b, c = [description.get_vertex_position(description.get_vertex_instance_vertex(description.get_triangle_vertex_instance(triangle, corner))) for corner in range(3)]
        if (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x) > 0.0:
            facing_up += 1

    bounds = mesh.get_bounding_box()
    unreal.log('MOUNTAIN %s triangles=%d z=%.1f..%.1f wrong_sides=%d facing_up=%d' % (
        name, description.get_triangle_count(), bounds.min.z, bounds.max.z, wrong_sides, facing_up))


def import_textures():
    tasks = []
    for name in ('T_Mountain_Masks', 'T_Mountain_Normal'):
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, name + '.png'))
        task.set_editor_property('destination_path', TEXTURES)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    normal = unreal.load_asset(TEXTURES + '/T_Mountain_Normal')
    normal.set_editor_property('srgb', False)
    normal.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
    unreal.EditorAssetLibrary.save_asset(TEXTURES + '/T_Mountain_Normal', only_if_is_dirty=False)
    masks = unreal.load_asset(TEXTURES + '/T_Mountain_Masks')
    masks.set_editor_property('srgb', False)
    masks.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
    unreal.EditorAssetLibrary.save_asset(TEXTURES + '/T_Mountain_Masks', only_if_is_dirty=False)
    if unreal.EditorAssetLibrary.does_asset_exist(TEXTURES + '/T_Mountain_Color'):
        unreal.EditorAssetLibrary.delete_asset(TEXTURES + '/T_Mountain_Color')


def main():
    paths = sorted(glob.glob(os.path.join(SOURCE_FOLDER, '*.glb')))
    if len(paths) != 30:
        raise RuntimeError('Expected 30 meshes in %s, found %d' % (SOURCE_FOLDER, len(paths)))

    import_textures()

    # the normals and UVs come from Blender, so the import must keep them
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)

    if unreal.EditorAssetLibrary.does_directory_exist(DESTINATION):
        unreal.EditorAssetLibrary.delete_directory(DESTINATION)

    tasks = []
    for path in paths:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', path)
        task.set_editor_property('destination_path', DESTINATION)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        target = '%s/%s' % (DESTINATION, name)

        # the glTF importer gives every file its own folders
        unreal.EditorAssetLibrary.rename_asset('%s/%s/StaticMeshes/%s' % (DESTINATION, name, name), target)
        unreal.EditorAssetLibrary.delete_directory('%s/%s' % (DESTINATION, name))

        mesh = unreal.load_asset(target)
        if mesh:
            report(mesh, name)
        else:
            unreal.log_error('MOUNTAIN could not load %s' % name)


main()
