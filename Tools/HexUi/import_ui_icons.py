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
SITES = '/Game/HexPlanet/Sites/DA_HexSites'

# each cursor of EHexCursor, the picture it shows and its size in pixels of Slate; the pictures are drawn at twice that
CURSORS = [
    ('DEFAULT', 'Default', 32), ('CHOOSE', 'Choose', 32), ('MOVE', 'Move', 42), ('MOVE_LATER', 'MoveLater', 42), ('BLOCKED', 'Blocked', 30),
    ('ATTACK', 'Attack', 36), ('CASTLE', 'Castle', 36), ('VISIT', 'Visit', 34), ('PORTAL', 'Portal', 36), ('SWAP', 'Swap', 34), ('EXCHANGE', 'Exchange', 36),
    ('BOARD', 'Board', 38), ('LAND', 'Land', 38), ('LAND_ATTACK', 'LandAttack', 38),
    ('TELEPORT', 'Teleport', 38),
    ('BATTLE_WALK', 'Move', 42), ('BATTLE_FLY', 'Fly', 38), ('BATTLE_SHOOT', 'Shoot', 36), ('BATTLE_SHOOT_FAR', 'ShootFar', 36),
    ('BATTLE_STRIKE', 'Strike', 40), ('BATTLE_CAST', 'Cast', 36), ('BATTLE_BLOCKED', 'Blocked', 30),
]

ICONS = [('FLIES', 'IconFlies'), ('ATTACK', 'IconAttack'), ('DEFENSE', 'IconDefense'), ('DAMAGE', 'IconDamage'), ('HEALTH', 'IconHealth'), ('SPEED', 'IconSpeed'),
         ('SHOTS', 'IconShots'), ('MORALE', 'IconMorale'), ('LUCK', 'IconLuck'), ('MANA', 'IconMana'), ('KILLS', 'IconKills'), ('COUNT', 'IconCount'), ('LEVEL_UP', 'IconLevelUp'),
         ('SPELL_POWER', 'IconSpellPower'), ('KNOWLEDGE', 'IconKnowledge'), ('EXPERIENCE', 'IconExperience'), ('MOVEMENT', 'IconMovement'),
         ('BUILD', 'IconBuild')]

# the pictures of the spells and the skills, which FHexSpell::Icon and FHexSkillInfo::Icon name by their Ids
KIND_FOLDERS = {'spell': '/Spells', 'skill': '/Skills', 'artifact': '/Artifacts'}

assets = unreal.EditorAssetLibrary


def import_picture(entry):
    folder = FOLDER + KIND_FOLDERS.get(entry.get('kind', ''), '/Cursors' if entry['cursor'] else '/Icons')
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
    # words after the file name (but reset) import only the pictures whose names hold one, so the others are not saved again
    only = [word for word in sys.argv[1:] if word != 'reset' and not word.startswith('-')]
    for entry in manifest:
        if only and not any(word in entry['name'] for word in only):
            continue
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
    else:
        # cursors added since the asset was made go in without touching the others
        cursors = list(config.get_editor_property('cursors'))
        present = [look.get_editor_property('cursor') for look in cursors]
        for enum_name, picture, size in CURSORS:
            if picture not in pictures or getattr(unreal.HexCursor, enum_name) in present:
                continue
            look = unreal.HexCursorLook()
            look.set_editor_property('cursor', getattr(unreal.HexCursor, enum_name))
            look.set_editor_property('texture', pictures[picture])
            look.set_editor_property('hot_spot', unreal.Vector2D(hot_spots[picture][0], hot_spots[picture][1]))
            look.set_editor_property('size', float(size))
            look.set_editor_property('frames', int(frames[picture][0]))
            look.set_editor_property('frames_per_second', float(frames[picture][1]))
            cursors.append(look)
            unreal.log('UI: cursor %s added' % picture)
        config.set_editor_property('cursors', cursors)
    # icons of stats added since the asset was made go in without touching the others
    present = [look.get_editor_property('icon') for look in config.get_editor_property('stat_icons')]
    added = []
    for enum_name, picture in ICONS:
        if picture in pictures and present and getattr(unreal.HexStatIcon, enum_name) not in present:
            look = unreal.HexStatIconLook()
            look.set_editor_property('icon', getattr(unreal.HexStatIcon, enum_name))
            look.set_editor_property('texture', pictures[picture])
            look.set_editor_property('frames', int(frames[picture][0]))
            look.set_editor_property('frames_per_second', float(frames[picture][1]))
            added.append(look)
    if added:
        config.set_editor_property('stat_icons', list(config.get_editor_property('stat_icons')) + added)
        unreal.log('UI: %d icons of stats added' % len(added))
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
    # the icons of the resources go to their resources in DA_HexSites, while a resource has none
    sites = unreal.load_asset(SITES)
    if sites:
        resources = sites.get_editor_property('resources')
        given = 0
        # an element of an array is a copy, so each changed one is put back
        for index in range(len(resources)):
            resource = resources[index]
            picture = 'Res' + str(resource.get_editor_property('id'))
            if picture in pictures and (reset or not resource.get_editor_property('icon')):
                resource.set_editor_property('icon', pictures[picture])
                resources[index] = resource
                given += 1
        sites.modify()
        sites.set_editor_property('resources', resources)
        assets.save_asset(SITES, only_if_is_dirty=False)
        unreal.log('UI: %d resources of DA_HexSites took an icon' % given)
    unreal.log('UI: %d pictures imported, DA_HexUi has %d cursors and %d icons' % (len(pictures), len(config.get_editor_property('cursors')),
                                                                                   len(config.get_editor_property('stat_icons'))))


main()
