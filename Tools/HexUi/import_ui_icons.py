"""Imports the cursors and icons of generate_ui_icons.py (Saved/HexUi) to /Game/HexPlanet/UI/Cursors and /Game/HexPlanet/UI/Icons and creates
DA_HexUi when it is missing, giving it those pictures while its lists are empty, so what an artist set stays. Run it with the commandlet once
the module is built:

    UnrealEditor-Cmd.exe HexHero.uproject -run=pythonscript -script="Tools/HexUi/import_ui_icons.py" -unattended -nullrhi

The word reset after the file name sets the lists of DA_HexUi again from CURSORS and ICONS."""

import json
import os
import sys

import unreal

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
SOURCE = os.path.join(ROOT, 'Saved', 'HexUi')
FOLDER = '/Game/HexPlanet/UI'
CONFIG = FOLDER + '/DA_HexUi'

# each cursor of EHexCursor, the picture it shows and its size in pixels of Slate; the pictures are drawn at twice that
CURSORS = [
    ('DEFAULT', 'Default', 32), ('CHOOSE', 'Choose', 32), ('MOVE', 'Move', 42), ('MOVE_LATER', 'MoveLater', 42), ('BLOCKED', 'Blocked', 30),
    ('ATTACK', 'Attack', 36), ('CASTLE', 'Castle', 36), ('VISIT', 'Visit', 34), ('PORTAL', 'Portal', 36), ('SWAP', 'Swap', 34),
    ('BATTLE_WALK', 'Move', 42), ('BATTLE_FLY', 'Fly', 38), ('BATTLE_SHOOT', 'Shoot', 36), ('BATTLE_SHOOT_FAR', 'ShootFar', 36),
    ('BATTLE_STRIKE', 'Strike', 40), ('BATTLE_CAST', 'Cast', 36), ('BATTLE_BLOCKED', 'Blocked', 30),
]

ICONS = [('FLIES', 'IconFlies'), ('ATTACK', 'IconAttack'), ('DEFENSE', 'IconDefense'), ('DAMAGE', 'IconDamage'), ('HEALTH', 'IconHealth'), ('SPEED', 'IconSpeed'),
         ('SHOTS', 'IconShots'), ('MORALE', 'IconMorale'), ('LUCK', 'IconLuck'), ('MANA', 'IconMana'), ('KILLS', 'IconKills'), ('COUNT', 'IconCount')]

assets = unreal.EditorAssetLibrary


def import_picture(entry):
    folder = FOLDER + ('/Cursors' if entry['cursor'] else '/Icons')
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', os.path.join(SOURCE, entry['file']))
    task.set_editor_property('destination_path', folder)
    task.set_editor_property('destination_name', os.path.splitext(entry['file'])[0])
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('automated', True)
    task.set_editor_property('save', False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    path = folder + '/' + os.path.splitext(entry['file'])[0]
    texture = unreal.load_asset(path)
    if not texture:
        unreal.log_error('UI: could not import %s' % entry['file'])
        return None
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    texture.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    # shown at half their size, so the first mip is what shows
    texture.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_SIMPLE_AVERAGE)
    texture.set_editor_property('never_stream', True)
    texture.set_editor_property('srgb', True)
    assets.save_asset(path, only_if_is_dirty=False)
    return texture


def main():
    reset = 'reset' in sys.argv[1:] or any('reset' in arg for arg in sys.argv)
    with open(os.path.join(SOURCE, 'ui_icons.json')) as file:
        manifest = json.load(file)
    pictures = {}
    hot_spots = {}
    frames = {}
    for entry in manifest:
        texture = import_picture(entry)
        if texture:
            pictures[entry['name']] = texture
            hot_spots[entry['name']] = entry['hotspot']
            frames[entry['name']] = (entry.get('frames', 1), entry.get('rate', 8.0))
    if not assets.does_asset_exist(CONFIG):
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.HexUiConfig)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_HexUi', FOLDER, unreal.HexUiConfig, factory)
        unreal.log('UI: created %s' % CONFIG)
    config = unreal.load_asset(CONFIG)
    if reset or len(config.get_editor_property('cursors')) == 0:
        cursors = []
        for enum_name, picture, size in CURSORS:
            if picture not in pictures:
                continue
            look = unreal.HexCursorLook()
            look.set_editor_property('cursor', getattr(unreal.HexCursor, enum_name))
            look.set_editor_property('texture', pictures[picture])
            look.set_editor_property('hot_spot', unreal.Vector2D(hot_spots[picture][0], hot_spots[picture][1]))
            look.set_editor_property('size', float(size))
            look.set_editor_property('frames', int(frames[picture][0]))
            look.set_editor_property('frames_per_second', float(frames[picture][1]))
            cursors.append(look)
        config.set_editor_property('cursors', cursors)
    if reset or len(config.get_editor_property('stat_icons')) == 0:
        icons = []
        for enum_name, picture in ICONS:
            if picture not in pictures:
                continue
            look = unreal.HexStatIconLook()
            look.set_editor_property('icon', getattr(unreal.HexStatIcon, enum_name))
            look.set_editor_property('texture', pictures[picture])
            look.set_editor_property('frames', int(frames[picture][0]))
            look.set_editor_property('frames_per_second', float(frames[picture][1]))
            icons.append(look)
        config.set_editor_property('stat_icons', icons)
    assets.save_asset(CONFIG, only_if_is_dirty=False)
    unreal.log('UI: %d pictures imported, DA_HexUi has %d cursors and %d icons' % (len(pictures), len(config.get_editor_property('cursors')),
                                                                                   len(config.get_editor_property('stat_icons'))))


main()
