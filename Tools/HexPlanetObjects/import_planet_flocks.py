"""Imports the meshes of the birds and bats that generate_planet_flocks.py wrote (UHexPlanetFlocks, the Flocks of UHexPlanetConfig).

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py (or its
argument flocks) made M_HexPlanetFlock. The meshes go to /Game/HexPlanet/Flocks, replaced each run, with that material; they are a few triangles
each, so they have no LODs.
"""

import json
import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetFlocks')
MESHES = '/Game/HexPlanet/Flocks'
MATERIAL = '/Game/HexPlanet/M_HexPlanetFlock'


def main():
    assets = unreal.EditorAssetLibrary
    with open(os.path.join(SOURCE_FOLDER, 'flocks.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    material = unreal.load_asset(MATERIAL)
    if not material:
        raise RuntimeError('%s is missing: run Tools/create_planet_assets.py flocks first' % MATERIAL)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    try:
        pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as error:
        unreal.log_warning('FLOCKS could not set the import of vertex colors: %s' % error)
    tasks = []
    for entry in manifest['meshes']:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, entry['name'] + '.glb'))
        task.set_editor_property('destination_path', MESHES)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for entry in manifest['meshes']:
        name = entry['name']
        target = '%s/%s' % (MESHES, name)
        # the glTF importer gives every file its own folders
        nested = '%s/%s/StaticMeshes/%s' % (MESHES, name, name)
        if assets.does_asset_exist(nested):
            if assets.does_asset_exist(target):
                assets.delete_asset(target)
            assets.rename_asset(nested, target)
            assets.delete_directory('%s/%s' % (MESHES, name))
        mesh = unreal.load_asset(target)
        if not mesh:
            raise RuntimeError('could not import %s' % name)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        mesh.set_material(0, material)
        # one LOD drawn at every size: with the screen sizes the engine computes by itself, an instance smaller on the screen than where a
        # second LOD would begin was not drawn at all, so an animal showed only when it was made several times larger
        if unreal.HexPlanetObjectSet.set_mesh_lods(mesh, [1.0], [1.0]) != 1:
            raise RuntimeError('%s has not one LOD' % name)
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        unreal.log('FLOCKS %s: %d triangles' % (name, entry['triangles']))
    unreal.log('FLOCKS DONE')


main()
