"""Imports the pictures of render_portraits.py (Saved/HexUi/Portraits) to /Game/HexPlanet/UI/Portraits and gives them to the data assets
while they name none, so what an artist set stays: the Portrait of each look of DA_HexBattle by the name of its mesh (SK_Unit<Name>, the rider
of the heroes for SK_PlanetRider), and the Picture of each level of a building of DA_HexCastles by the name of its mesh (SM_PlanetCastle<Name>),
the forts as Fort1 to Fort3. Run it with the commandlet once the module is built:

    UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexUi/import_portraits.py" -unattended -nullrhi

The word reset after the file name sets them again whatever they hold."""

import os
import sys

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SOURCE = os.path.join(ROOT, 'Saved', 'HexUi', 'Portraits')
FOLDER = '/Game/HexPlanet/UI/Portraits'
BATTLE = '/Game/HexPlanet/Battle/DA_HexBattle'
CASTLES = '/Game/HexPlanet/Castles/DA_HexCastles'

assets = unreal.EditorAssetLibrary


def import_picture(file):
    name = os.path.splitext(file)[0]
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', os.path.join(SOURCE, file))
    task.set_editor_property('destination_path', FOLDER)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = FOLDER + '/' + name
    texture = unreal.load_asset(path)
    if not texture:
        unreal.log_error('Portraits: could not import %s' % file)
        return None
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('srgb', True)
    assets.save_asset(path, only_if_is_dirty=False)
    return texture


def main():
    reset = any('reset' in arg for arg in sys.argv)
    pictures = {}
    for file in sorted(os.listdir(SOURCE)):
        if file.endswith('.png'):
            texture = import_picture(file)
            if texture:
                pictures[os.path.splitext(file)[0]] = texture

    battle = unreal.load_asset(BATTLE)
    looks = list(battle.get_editor_property('unit_looks'))
    given = 0
    for look in looks:
        if not reset and look.get_editor_property('portrait'):
            continue
        mesh = look.get_editor_property('mesh')
        name = mesh.get_name() if mesh else ''
        key = 'T_Portrait_Rider' if name == 'SK_PlanetRider' else 'T_Portrait_' + name.replace('SK_Unit', '')
        if key in pictures:
            look.set_editor_property('portrait', pictures[key])
            given += 1
    battle.set_editor_property('unit_looks', looks)
    assets.save_asset(BATTLE, only_if_is_dirty=False)

    castles = unreal.load_asset(CASTLES)
    factions = list(castles.get_editor_property('factions'))
    built = 0
    for faction in factions:
        buildings = list(faction.get_editor_property('buildings'))
        for building in buildings:
            levels = list(building.get_editor_property('levels'))
            for index, level in enumerate(levels):
                if not reset and level.get_editor_property('picture'):
                    continue
                mesh = level.get_editor_property('mesh')
                name = mesh.get_name().replace('SM_PlanetCastle', '') if mesh else ''
                if str(building.get_editor_property('id')) == 'Fort':
                    name = 'Fort%d' % (index + 1)
                key = 'T_Building_' + name
                if name and key in pictures:
                    level.set_editor_property('picture', pictures[key])
                    built += 1
            building.set_editor_property('levels', levels)
        faction.set_editor_property('buildings', buildings)
    castles.set_editor_property('factions', factions)
    assets.save_asset(CASTLES, only_if_is_dirty=False)
    unreal.log('Portraits: %d pictures imported, %d portraits of looks and %d pictures of buildings given' % (len(pictures), given, built))


main()
