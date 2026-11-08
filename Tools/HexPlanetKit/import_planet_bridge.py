"""Imports the bridge over rivers that generate_planet_kit.py wrote to Saved/HexPlanetBridge, draws it with M_HexPlanetBoulder of
create_planet_assets.py, and gives it to BridgeMesh of DA_HexPlanet when that names none, so a bridge an artist chose stays.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py.
It lives apart from the kit, whose import deletes its folder.
"""

import os

import unreal

SOURCE = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetBridge', 'SM_PlanetBridge.glb')
DESTINATION = '/Game/HexPlanet/Bridges'
NAME = 'SM_PlanetBridge'
MATERIAL = '/Game/HexPlanet/M_HexPlanetBoulder'
CONFIG = '/Game/HexPlanet/DA_HexPlanet'


def main():
    if not os.path.exists(SOURCE):
        raise RuntimeError('%s is missing, run generate_planet_kit.py first' % SOURCE)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', SOURCE)
    task.set_editor_property('destination_path', DESTINATION)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    task.set_editor_property('options', pipeline)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    # the glTF importer gives the file its own folders
    target = '%s/%s' % (DESTINATION, NAME)
    nested = '%s/%s/StaticMeshes/%s' % (DESTINATION, NAME, NAME)
    if unreal.EditorAssetLibrary.does_asset_exist(nested):
        if unreal.EditorAssetLibrary.does_asset_exist(target):
            unreal.EditorAssetLibrary.delete_asset(target)
        unreal.EditorAssetLibrary.rename_asset(nested, target)
        unreal.EditorAssetLibrary.delete_directory('%s/%s' % (DESTINATION, NAME))
    mesh = unreal.load_asset(target)
    if not mesh:
        raise RuntimeError('could not load %s' % target)

    material = unreal.load_asset(MATERIAL)
    if material:
        mesh.set_material(0, material)
    else:
        unreal.log_warning('PLANETBRIDGE %s is missing, run Tools/create_planet_assets.py and this script again' % MATERIAL)
    settings = mesh.get_editor_property('nanite_settings')
    settings.set_editor_property('enabled', False)
    mesh.set_editor_property('nanite_settings', settings)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

    config = unreal.load_asset(CONFIG)
    if config and not config.get_editor_property('bridge_mesh'):
        config.set_editor_property('bridge_mesh', mesh)
        unreal.EditorAssetLibrary.save_loaded_asset(config, only_if_is_dirty=False)
    unreal.log('PLANETBRIDGE imported %s' % target)


main()
