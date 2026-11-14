"""Imports the catapult blender_build_catapult.py modelled (Saved/BattleCatapult) to /Game/HexPlanet/Battle/Catapult and gives DA_HexBattle its
frame (CatapultMesh), its arm (CatapultArmMesh) and where the arm turns in the frame (CatapultArmPivot, in units of the mesh); the field gives its
material slots Wood, Iron and Rope the planks of M_HexBattleProp. Run with the commandlet after the module is built:

  UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexBattle/import_battle_catapult.py" -unattended -nullrhi
"""
import json
import os

import unreal

SOURCE = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'BattleCatapult'))
FOLDER = '/Game/HexPlanet/Battle/Catapult'
CONFIG = '/Game/HexPlanet/Battle/DA_HexBattle'
assets = unreal.EditorAssetLibrary


def find_imported(name):
    target = '%s/%s' % (FOLDER, name)
    nested = '%s/%s/StaticMeshes/%s' % (FOLDER, name, name)
    if assets.does_asset_exist(nested):
        if assets.does_asset_exist(target):
            assets.delete_asset(target)
        assets.rename_asset(nested, target)
        assets.delete_directory('%s/%s' % (FOLDER, name))
    mesh = unreal.load_asset(target)
    if not mesh:
        raise RuntimeError('could not import %s' % name)
    return mesh


def main():
    with open(os.path.join(SOURCE, 'catapult.json'), encoding='utf-8') as file:
        info = json.load(file)
    if not assets.does_directory_exist(FOLDER):
        assets.make_directory(FOLDER)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    tasks = []
    for name in ('SM_BattleCatapult', 'SM_BattleCatapultArm'):
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE, name + '.glb'))
        task.set_editor_property('destination_path', FOLDER)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    meshes = []
    for name in ('SM_BattleCatapult', 'SM_BattleCatapultArm'):
        mesh = find_imported(name)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        slots = [str(each.get_editor_property('material_slot_name')) for each in mesh.get_editor_property('static_materials')]
        bounds = mesh.get_bounds()
        unreal.log('CATAPULT %s slots %s, bounds %s %s' % (name, slots, bounds.origin, bounds.box_extent))
        meshes.append(mesh)
    config = unreal.load_asset(CONFIG)
    config.set_editor_property('catapult_mesh', meshes[0])
    config.set_editor_property('catapult_arm_mesh', meshes[1])
    pivot = info['pivot']
    # the importer keeps X and turns Y of glTF the other way
    config.set_editor_property('catapult_arm_pivot', unreal.Vector(pivot[0], -pivot[1], pivot[2]))
    config.set_editor_property('catapult_hit_chance', 1.0)
    assets.save_loaded_asset(config, only_if_is_dirty=False)
    unreal.log('CATAPULT done')


main()
