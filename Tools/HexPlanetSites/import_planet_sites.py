"""Imports the meshes of the mines and piles that generate_planet_sites.py wrote (UHexSites) and creates DA_HexSites when it is missing.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py (or its
argument heroes) made M_HexPlanetSite. The meshes go to /Game/HexPlanet/Sites, replaced each run, with that material. DA_HexSites
(UHexSiteConfig) holds the resources and the kinds of sites and belongs to whoever tunes it, so it is only created, with the defaults of its
class, and never changed here.
"""

import json
import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetSites')
FOLDER = '/Game/HexPlanet/Sites'
CONFIG = 'DA_HexSites'
MATERIAL = '/Game/HexPlanet/M_HexPlanetSite'


def main():
    assets = unreal.EditorAssetLibrary
    with open(os.path.join(SOURCE_FOLDER, 'sites.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    material = unreal.load_asset(MATERIAL)
    if not material:
        raise RuntimeError('%s is missing: run Tools/create_planet_assets.py heroes first' % MATERIAL)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    try:
        pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as error:
        unreal.log_warning('SITES could not set the import of vertex colors: %s' % error)
    tasks = []
    for entry in manifest['meshes']:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, entry['name'] + '.glb'))
        task.set_editor_property('destination_path', FOLDER)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for entry in manifest['meshes']:
        name = entry['name']
        target = '%s/%s' % (FOLDER, name)
        # the glTF importer gives every file its own folders
        nested = '%s/%s/StaticMeshes/%s' % (FOLDER, name, name)
        if assets.does_asset_exist(nested):
            if assets.does_asset_exist(target):
                assets.delete_asset(target)
            assets.rename_asset(nested, target)
            assets.delete_directory('%s/%s' % (FOLDER, name))
        mesh = unreal.load_asset(target)
        if not mesh:
            raise RuntimeError('could not import %s' % name)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        mesh.set_material(0, material)
        # one LOD drawn at every size: reduced, a mine lost its timbers, its heap and its flag, which are what tell it from a rock
        if unreal.HexPlanetObjectSet.set_mesh_lods(mesh, [1.0], [1.0]) != 1:
            raise RuntimeError('%s has not one LOD' % name)
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        unreal.log('SITES %s: %d triangles' % (name, entry['triangles']))

    config_path = '%s/%s' % (FOLDER, CONFIG)
    if not assets.does_asset_exist(config_path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(CONFIG, FOLDER, unreal.HexSiteConfig, unreal.DataAssetFactory())
        assets.save_asset(config_path, only_if_is_dirty=False)
        unreal.log('SITES %s created with the defaults of its class' % CONFIG)
    unreal.log('SITES DONE')


main()
