"""Imports the figures of the creatures that blender_build_units.py wrote to Saved/BattleUnits into /Game/HexPlanet/Battle/Units, each with
its skeleton and its animations, draws them with M_HexBattleUnit, and gives DA_HexBattle the look of each creature that has none yet and the
grounds of the field while they have no obstacles, run with the commandlet after create_battle_assets.py:

    UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexBattle/import_battle_units.py [Name Name ...] [reset]" -unattended -nullrhi

Words after the file name import only those units; reset sets the looks of the units imported again from LOOKS, as tuned by hand they stay."""

import os
import sys
import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SOURCE = os.path.join(ROOT, 'Saved', 'BattleUnits')
FOLDER = '/Game/HexPlanet/Battle/Units'
CONFIG = '/Game/HexPlanet/Battle/DA_HexBattle'
UNIT_MATERIAL = '/Game/HexPlanet/Battle/M_HexBattleUnit'
RIDER = '/Game/HexPlanet/Heroes/SK_PlanetRider/SkeletalMeshes/SK_PlanetRider'

# the look of each creature: the unit it is drawn as, its height on the field, how high a flyer floats, its tint and that of its upgraded
# version; the riders are the rider of the heroes
# the heights follow the level and the might of a creature, as the developer asked on 2026-11-13 for figures in proportion to each other and
# to their power: the foot soldiers of the first levels the smallest, the riders of the sixth large, the dragon of the seventh the largest
LOOKS = {
    'Sentinel': ('Sentinel', 52, 0, (1.0, 1.0, 1.0), (1.05, 1.0, 0.85)),
    'Huntsman': ('Huntsman', 54, 0, (1.0, 1.0, 1.0), (0.95, 1.05, 0.9)),
    'Glowwing': ('Glowwing', 66, 26, (1.25, 1.05, 0.55), (1.3, 1.25, 0.95)),
    'Oathsworn': ('Oathsworn', 60, 0, (0.9, 0.92, 1.0), (1.1, 1.05, 0.9)),
    'Pilgrim': ('Pilgrim', 62, 0, (1.0, 1.0, 1.0), (0.85, 0.9, 1.15)),
    'DawnRider': ('rider', 90, 0, (1.0, 1.0, 1.0), (1.1, 1.08, 0.95)),
    'SkyWarden': ('SkyWarden', 82, 0, (1.05, 1.05, 1.1), (1.25, 1.2, 1.0)),
    'BoneWarrior': ('BoneWarrior', 52, 0, (1.0, 1.0, 1.0), (0.85, 0.88, 0.95)),
    'Decayed': ('Decayed', 56, 0, (0.75, 0.85, 0.7), (0.6, 0.7, 0.62)),
    'Shade': ('Shade', 60, 7, (1.0, 1.0, 1.0), (0.8, 0.85, 1.1)),
    'BloodDrinker': ('BloodDrinker', 64, 0, (1.0, 1.0, 1.0), (1.1, 0.85, 0.9)),
    'AshMage': ('AshMage', 64, 0, (0.8, 0.78, 0.75), (0.7, 0.72, 0.85)),
    'DecayRider': ('rider', 90, 0, (0.32, 0.3, 0.36), (0.22, 0.2, 0.28)),
    'BoneSerpent': ('BoneSerpent', 80, 34, (1.0, 1.0, 1.0), (0.82, 0.76, 0.9)),
}
# the surface of each, as the developer found them like plasticine: how large its grain is, how much it changes the tone and the relief, and
# its roughness (DetailScale, DetailTone, DetailBump, Roughness of FHexUnitLook); the rest take the defaults of the class
DETAILS = {
    'Glowwing': (9.0, 0.16, 0.6, 0.45), 'BoneSerpent': (10.0, 0.08, 0.15, 0.7), 'Decayed': (9.0, 0.2, 0.6, 0.8), 'Shade': (12.0, 0.15, 0.3, 0.9),
    'BoneWarrior': (6.0, 0.16, 0.5, 0.6), 'AshMage': (6.0, 0.16, 0.5, 0.7), 'Sentinel': (10.0, 0.1, 0.3, 0.45), 'Oathsworn': (10.0, 0.1, 0.3, 0.45),
    'SkyWarden': (10.0, 0.1, 0.3, 0.45),
    # the rider, whose mesh is about 13 times smaller in its own units than the figures of the models (581 high against 7577), so its noise
    # is 13 times finer to match theirs; at 10 the texels of the noise showed as blocks on the horse, which the developer saw on 2026-11-14
    'DawnRider': (0.8, 0.06, 0.1, 0.55), 'DecayRider': (0.8, 0.06, 0.1, 0.55),
}
# the foot of a unit whose measure in units.json does not hold in the engine: the skeleton stands with its foot at its origin there, though
# Blender measured it 163 below, and floated so until 2026-11-12
FOOT_OVERRIDES = {}
RIDER_ANIMATIONS = {'Idle': 'Idle', 'Move': 'Gallop', 'Attack': 'Gallop_Jump', 'Hit': 'Idle_HitReact_Left'}
ACTIONS = ['Idle', 'Move', 'Attack', 'Shoot', 'Hit', 'Death', 'Walk', 'Fly']

OPEN_OBSTACLES = ['Rock_0', 'Rock_1', 'Rock_2', 'Stone_1', 'Stone_2', 'Stump_0', 'Broadleaf_0', 'Pine_0', 'Log_0']
OPEN_DECOR = ['Broadleaf_0', 'Broadleaf_1', 'Broadleaf_2', 'Pine_0', 'Pine_1', 'Spruce_0', 'Birch_0', 'Oak_0', 'Bush_0', 'Bush_1', 'Bush_2', 'Rock_1',
    'Stone_0', 'Flowers_0', 'Grass_0']
CAVE_OBSTACLES = ['Stalagmites_0', 'Stalagmites_1', 'Stalagmites_2', 'Rockfall_0', 'CrystalMass_0', 'StalagmiteMass_0']
CAVE_DECOR = ['Stalagmites_0', 'Stalagmites_1', 'StalagmiteField_0', 'Crystals_0', 'Crystals_1', 'Mushrooms_0', 'Mushrooms_1', 'RimSpire_0', 'Rockfall_1',
    'Pebbles_0']

assets = unreal.EditorAssetLibrary


def import_unit(name, material):
    """Imports SK_Unit<Name>.glb anew, as a skeleton with other bones does not take the place of the one before. Returns the mesh and the
    animations by their names"""
    source = os.path.join(SOURCE, 'SK_Unit%s.glb' % name)
    if not os.path.exists(source):
        unreal.log_warning('BATTLE UNITS %s is missing: run Tools/HexBattle/blender_build_units.py' % source)
        return None, {}
    folder = '%s/SK_Unit%s' % (FOLDER, name)
    if assets.does_directory_exist(folder):
        assets.delete_directory(folder)
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', source)
    task.set_editor_property('destination_path', FOLDER)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    task.set_editor_property('options', pipeline)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = None
    animations = {}
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(folder, recursive=True):
        asset = data.get_asset()
        if isinstance(asset, unreal.MaterialInterface):
            assets.delete_asset(str(data.package_name))
        elif isinstance(asset, unreal.SkeletalMesh):
            asset.set_editor_property('materials', [unreal.SkeletalMaterial(material_interface=material, material_slot_name='Unit')])
            assets.save_loaded_asset(asset, only_if_is_dirty=False)
            mesh = asset
        elif isinstance(asset, unreal.AnimSequence):
            for action in ACTIONS:
                if asset.get_name().endswith(action):
                    animations[action] = asset
    unreal.log('BATTLE UNITS %s: mesh %s, animations %s' % (name, mesh.get_path_name() if mesh else None, sorted(animations)))
    return mesh, animations


def rider_parts(material, again):
    """The rider of the battles: SK_UnitRider, the rider of the heroes with a lance in place of the banner, which only a hero carries, as the
    developer asked on 2026-11-13 (blender_build_rider.py lance), imported anew when asked for or missing; else the rider of the heroes"""
    source = os.path.join(ROOT, 'Saved', 'HexPlanetHeroes', 'SK_UnitRider.glb')
    folder = '%s/SK_UnitRider' % FOLDER
    if os.path.exists(source) and (again or not assets.does_directory_exist(folder)):
        if assets.does_directory_exist(folder):
            assets.delete_directory(folder)
        pipeline = unreal.InterchangeGenericAssetsPipeline()
        pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
        pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', source)
        task.set_editor_property('destination_path', FOLDER)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = None
    animations = {}
    for data in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(folder, recursive=True):
        asset = data.get_asset()
        if isinstance(asset, unreal.MaterialInterface):
            assets.delete_asset(str(data.package_name))
        elif isinstance(asset, unreal.SkeletalMesh):
            asset.set_editor_property('materials', [unreal.SkeletalMaterial(material_interface=material, material_slot_name='Unit')])
            assets.save_loaded_asset(asset, only_if_is_dirty=False)
            mesh = asset
        elif isinstance(asset, unreal.AnimSequence):
            for wanted, ending in RIDER_ANIMATIONS.items():
                if asset.get_name().endswith('SK_UnitRider' + ending):
                    animations[wanted] = asset
    if mesh:
        unreal.log('BATTLE UNITS rider: mesh %s, animations %s' % (mesh.get_path_name(), sorted(animations)))
        return mesh, animations
    mesh = unreal.load_asset(RIDER)
    folder = RIDER.rsplit('/', 1)[0]
    for wanted, ending in RIDER_ANIMATIONS.items():
        sequence = unreal.load_asset('%s/SK_PlanetRider%s' % (folder, ending))
        if sequence:
            animations[wanted] = sequence
    return mesh, animations


def make_look(creature, mesh, animations, spec, measure):
    look = unreal.HexUnitLook()
    if measure:
        look.set_editor_property('mesh_height', float(measure['height']))
        look.set_editor_property('mesh_foot', float(FOOT_OVERRIDES.get(creature, measure['foot'])))
    look.set_editor_property('creature', creature)
    look.set_editor_property('mesh', mesh)
    look.set_editor_property('height', float(spec[1]))
    look.set_editor_property('hover', float(spec[2]))
    # every figure comes in facing its -Y, the rider and those built from the models alike (until 2026-11-12 these took 0 and stood side on)
    look.set_editor_property('yaw', -90.0)
    look.set_editor_property('tint', unreal.LinearColor(spec[3][0], spec[3][1], spec[3][2], 1.0))
    look.set_editor_property('upgraded_tint', unreal.LinearColor(spec[4][0], spec[4][1], spec[4][2], 1.0))
    look.set_editor_property('side_color_share', 0.75)
    if creature in DETAILS:
        scale, tone, bump, roughness = DETAILS[creature]
        look.set_editor_property('detail_scale', scale)
        look.set_editor_property('detail_tone', tone)
        look.set_editor_property('detail_bump', bump)
        look.set_editor_property('roughness', roughness)
    for action in ACTIONS:
        if action in animations:
            each = unreal.HexUnitAnimation()
            each.set_editor_property('sequence', animations[action])
            each.set_editor_property('rate', 1.0)
            look.set_editor_property(action.lower(), each)
    return look


def mesh_list(prefix, names):
    found = []
    for name in names:
        path = '%s%s' % (prefix, name)
        if assets.does_asset_exist(path):
            found.append(unreal.load_asset(path))
    return found


def main():
    words = [word for word in sys.argv[1:] if not word.startswith('-')]
    reset = 'reset' in words
    only = [word for word in words if word != 'reset']
    material = unreal.load_asset(UNIT_MATERIAL)
    if not material:
        raise RuntimeError('%s is missing: run Tools/HexBattle/create_battle_assets.py first' % UNIT_MATERIAL)
    config = unreal.load_asset(CONFIG)
    if not config:
        raise RuntimeError('%s is missing: run Tools/HexBattle/create_battle_assets.py first' % CONFIG)
    built = {}
    for creature, spec in LOOKS.items():
        unit = spec[0]
        if unit in built:
            continue
        if unit == 'rider':
            built[unit] = rider_parts(material, 'rider' in only)
        elif not only or unit in only:
            built[unit] = import_unit(unit, material)
    measures = {}
    if os.path.exists(os.path.join(SOURCE, 'units.json')):
        import json
        with open(os.path.join(SOURCE, 'units.json'), encoding='utf-8') as file:
            measures = json.load(file)
    looks = list(config.get_editor_property('unit_looks'))
    for creature, spec in LOOKS.items():
        if spec[0] not in built or not built[spec[0]][0]:
            continue
        index = next((i for i, each in enumerate(looks) if str(each.get_editor_property('creature')) == creature), -1)
        if index >= 0 and not reset and not only:
            continue
        look = make_look(creature, built[spec[0]][0], built[spec[0]][1], spec, measures.get(spec[0]))
        if index >= 0:
            looks[index] = look
        else:
            looks.append(look)
    config.set_editor_property('unit_looks', looks)
    # the grounds of the field, while they have nothing on them
    grounds = list(config.get_editor_property('grounds'))
    for ground in grounds:
        if ground.get_editor_property('obstacles'):
            continue
        cave = ground.get_editor_property('underground')
        if cave:
            ground.set_editor_property('obstacles', mesh_list('/Game/HexPlanet/Objects/Cave/SM_CaveObject_', CAVE_OBSTACLES))
            ground.set_editor_property('decor', mesh_list('/Game/HexPlanet/Objects/Cave/SM_CaveObject_', CAVE_DECOR))
        else:
            ground.set_editor_property('obstacles', mesh_list('/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_', OPEN_OBSTACLES))
            ground.set_editor_property('decor', mesh_list('/Game/HexPlanet/Objects/Meshes/SM_PlanetObject_', OPEN_DECOR))
    config.set_editor_property('grounds', grounds)
    assets.save_loaded_asset(config, only_if_is_dirty=False)
    unreal.log('BATTLE UNITS done: %d looks' % len(looks))


main()
