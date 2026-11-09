"""Imports the meshes that generate_cave_objects.py wrote, and creates the cave kinds of objects of the underground (UHexPlanetObjectSet) that are
missing.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py underground made
M_HexPlanetCave. The meshes go to /Game/HexPlanet/Objects/Cave, replaced each run, with two reduced LODs, CPU access (so UHexPlanetObjects reads
how far each reaches for UHexPlanetObjectSet::bClipFootprint) and the material their kind needs; the sets go to /Game/HexPlanet/Objects/Sets
(PlanetObjectsFolder of HexMapSettings) beside those of the surface, only when an asset of that name is missing, as they belong to the artist once
they exist: delete one to have it made again from SETS. It never touches the meshes or sets of the surface (import_planet_objects.py).

Words after the file name (-script="<file> Rim Stalactites RuneBlock") import only the meshes whose names hold one of them, so the meshes imported
before stay as they are; the missing sets are created either way. A word reset=<Id>,<Id> sets every value of those sets again from SETS and
RIM_SETS, for a set whose values here changed and that nobody tuned by hand (reset= alone imports no mesh).
"""

import json
import os
import sys

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexCaveObjects')
MESHES = '/Game/HexPlanet/Objects/Cave'
SETS_FOLDER = '/Game/HexPlanet/Objects/Sets'
MATERIALS = {'cave': '/Game/HexPlanet/M_HexPlanetCave', 'rock': '/Game/HexPlanet/M_HexPlanetBoulder'}
# the share of triangles of each LOD and the screen size from which it is drawn, as for the surface: a mesh of a bounds radius of about 8 reaches the
# last at about 710 units. A mesh of many small parts (pebbles, scattered bones, small mushrooms) falls apart there, so it keeps more of its triangles
# and switches later, a lower screen size: its last LOD from about 1,800 units at that radius
LODS = {'default': [(1.0, 1.0), (0.35, 0.05), (0.12, 0.02)], 'small': [(1.0, 1.0), (0.6, 0.02), (0.3, 0.008)]}
# the bit of EHexPlanetLayer::Underground in PlanetLayers
UNDERGROUND_LAYER = 2

# the caps glow by their vertices, most at the rims, so the lit dome shows; at 1.4 they drew flat cyan discs. The crystals are teal and glow less
# than the 1.8 and (0.5, 0.95, 0.9) first made, which drew them as white cut-outs without faces
MUSHROOM_GLOW = {'color': (0.12, 0.9, 1.0), 'strength': 0.7, 'pulse': 0.25, 'speed': 1.1, 'pool': 0.8, 'spores': 6}
CRYSTAL_GLOW = {'color': (0.25, 0.85, 0.8), 'strength': 0.9, 'pulse': 0.1, 'speed': 0.6, 'pool': 0.6, 'spores': 0}
RUNE_GLOW = {'color': (0.8, 0.85, 0.9), 'strength': 0.4, 'pulse': 0.3, 'speed': 0.7, 'pool': 0.0, 'spores': 0}

# the rims of the chasms (EHexObjectLayer::Rim): placed along every side of a passage towards a void, never stored, painted or generated; sorted
# after every other kind, so the indices of the cave kinds stay. 'counts' is the fewest and the most on a side (RimCountMin, CountByDensity),
# 'share' the share of the sides that take the kind, 'inset' how far onto the passage a rim stands (RimInset) and 'out' how far past the side the
# face of the cliff is looked up for one that hangs (RimHangOut). They reach about CoarseDistance, as at 4000 the ground in view lies 3,700 to 6,000
# away and a shorter cull cut the rims off the top of the screen where the straight edges show most; the low rocks cast no shadow worth drawing
RIM_SETS = [
    {'id': 'CaveRim', 'name': 'Chasm rim', 'order': 310, 'color': (0.3, 0.28, 0.26), 'counts': (2, 4), 'share': 1.0, 'scale': (1.0, 1.6),
     'giant': (0.05, 1.6), 'spacing': 8.0, 'inset': 0.16, 'turn': 12.0, 'align': 0.6, 'tilt': 8.0, 'sink': 0.8, 'shadow': False, 'cull': 9000.0,
     'rings': 0, 'meshes': ['RimRock_0', 'RimRock_1']},
    {'id': 'CaveRimSpires', 'name': 'Rim spires', 'order': 320, 'color': (0.32, 0.29, 0.26), 'counts': (1, 2), 'share': 1.0, 'scale': (0.9, 1.5),
     'giant': (0.1, 1.6), 'spacing': 10.0, 'inset': 0.08, 'turn': 25.0, 'align': 0.2, 'tilt': 8.0, 'sink': 1.5, 'shadow': False, 'cull': 9000.0,
     'rings': 0, 'meshes': ['RimSpire_0', 'RimSpire_1']},
    {'id': 'CaveStalactites', 'name': 'Stalactites', 'order': 330, 'color': (0.28, 0.26, 0.24), 'counts': (0, 2), 'share': 1.0, 'scale': (1.0, 1.6),
     'giant': (0.1, 1.8), 'spacing': 9.0, 'out': 2.0, 'turn': 6.0, 'align': 0.0, 'tilt': 6.0, 'sink': 1.0, 'shadow': False, 'cull': 7000.0,
     'rings': 0, 'hang': True, 'meshes': ['Stalactites_0', 'Stalactites_1']},
    {'id': 'CaveRimRunes', 'name': 'Carved rim block', 'order': 340, 'color': (0.5, 0.5, 0.56), 'counts': (1, 1), 'share': 0.05, 'scale': (1.4, 1.7),
     'giant': (0.0, 1.0), 'spacing': 12.0, 'inset': 0.06, 'turn': 5.0, 'align': 0.8, 'tilt': 3.0, 'sink': 0.5, 'shadow': False, 'cull': 6000.0,
     'rings': 2, 'glow': RUNE_GLOW, 'meshes': ['RuneBlock_0', 'RuneBlock_1']},
]

# each kind: its values, its meshes, the share of the passages the generation gives it, and its group, patches and the rings of an exit it keeps
# clear of; all stand on passages (level 0) of every cave floor, block nothing and keep their meshes off the voids. Glowing and tall kinds keep
# two rings from an exit, so its marker stays the brightest thing of its hall
SETS = [
    {'id': 'CaveMushrooms', 'name': 'Mushrooms', 'order': 110, 'color': (0.1, 0.7, 0.8), 'layer': 'cover', 'counts': [4, 8, 14], 'scale': (0.8, 1.3),
     'giant': (0.08, 2.0), 'spacing': 12.0, 'reach': 1.0, 'min_up': 0.6, 'align': 0.3, 'tilt': 8.0, 'sink': 0.3, 'shadow': False, 'cull': 3500.0,
     'group': 'Fungus', 'patch': 3.5, 'share': 0.07, 'rings': 2, 'glow': MUSHROOM_GLOW, 'meshes': ['Mushrooms_0', 'Mushrooms_1', 'Mushrooms_2']},
    {'id': 'CaveStalagmites', 'name': 'Stalagmites', 'order': 120, 'color': (0.35, 0.3, 0.25), 'layer': 'cover', 'counts': [1, 2, 4], 'scale': (0.8, 1.25),
     'giant': (0.06, 1.6), 'spacing': 30.0, 'reach': 0.9, 'min_up': 0.6, 'align': 0.1, 'tilt': 4.0, 'sink': 1.0, 'shadow': True, 'cull': 6000.0,
     'group': 'Stalagmites', 'patch': 4.0, 'share': 0.14, 'rings': 1, 'meshes': ['Stalagmites_0', 'Stalagmites_1', 'Stalagmites_2']},
    {'id': 'CaveRubble', 'name': 'Rubble', 'order': 130, 'color': (0.42, 0.37, 0.3), 'layer': 'cover', 'counts': [3, 6, 10], 'scale': (0.8, 1.3),
     'giant': (0.05, 1.6), 'spacing': 12.0, 'reach': 1.0, 'min_up': 0.5, 'align': 0.9, 'tilt': 12.0, 'sink': 0.4, 'shadow': False, 'cull': 2500.0,
     'group': '', 'patch': 2.0, 'share': 0.28, 'rings': 0, 'meshes': ['Pebbles_0', 'Pebbles_1', 'BrokenSlab_0', 'BrokenSlab_1']},
    {'id': 'CaveCrystals', 'name': 'Crystals', 'order': 210, 'color': (0.6, 0.95, 0.95), 'layer': 'decor', 'counts': [2], 'scale': (0.7, 1.4),
     'giant': (0.25, 1.8), 'spacing': 14.0, 'reach': 0.8, 'min_up': 0.6, 'align': 0.2, 'tilt': 10.0, 'sink': 1.0, 'shadow': False, 'cull': 7000.0,
     'group': 'Crystals', 'patch': 3.0, 'share': 0.035, 'rings': 2, 'glow': CRYSTAL_GLOW, 'meshes': ['Crystals_0', 'Crystals_1', 'Crystals_2']},
    {'id': 'CaveStalagmiteField', 'name': 'Spire field', 'order': 215, 'color': (0.3, 0.26, 0.22), 'layer': 'decor', 'counts': [1], 'scale': (0.9, 1.1),
     'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.15, 'min_up': 0.6, 'align': 0.1, 'tilt': 2.0, 'sink': 1.0, 'shadow': True, 'cull': 7000.0,
     'group': 'Stalagmites', 'patch': 4.0, 'share': 0.012, 'rings': 2, 'meshes': ['StalagmiteField_0', 'StalagmiteField_1']},
    {'id': 'CaveBones', 'name': 'Bones', 'order': 220, 'color': (0.75, 0.7, 0.55), 'layer': 'decor', 'counts': [1], 'scale': (0.9, 1.2),
     'giant': (0.15, 2.2), 'spacing': 10.0, 'reach': 0.5, 'min_up': 0.7, 'align': 0.8, 'tilt': 5.0, 'sink': 0.8, 'shadow': True, 'cull': 4000.0,
     'group': 'Bones', 'patch': 2.5, 'share': 0.012, 'rings': 1, 'meshes': ['Ribcage_0', 'Skull_0', 'BoneScatter_0']},
    # larger than the 1.0 to 1.25 first planned, as at 900 to 1200 a stone 9 wide showed as a speck of a few pixels and its glyph not at all
    {'id': 'CaveRuneStone', 'name': 'Rune stone', 'order': 230, 'color': (0.5, 0.5, 0.56), 'layer': 'decor', 'counts': [1], 'scale': (1.6, 2.0),
     'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.35, 'min_up': 0.7, 'align': 0.2, 'tilt': 4.0, 'sink': 1.0, 'shadow': True, 'cull': 4500.0,
     'group': '', 'patch': 0.0, 'share': 0.008, 'rings': 2, 'glow': RUNE_GLOW, 'meshes': ['RuneStone_0', 'RuneStone_1']},
]


def get_resets():
    """The Ids of the sets that reset=<Id>,<Id> after the file name makes again"""
    return set(id for word in sys.argv[1:] if word.startswith('reset=') for id in word[len('reset='):].split(',') if id)


def is_chosen(name):
    """Returns true if a mesh is imported in this run: every mesh, or those whose names hold a word given after the file name; none when only
    reset= is given"""
    words = [word for word in sys.argv[1:] if not word.startswith('reset=')]
    if not words and get_resets():
        return False
    return not words or any(word in name for word in words)


def take_set(entry):
    """Returns the path of the set of an entry and the set: a new one, the existing one when reset= names it, whose values are all set again,
    or None when it exists and keeps its values"""
    assets = unreal.EditorAssetLibrary
    asset_name = 'DA_HexObject_%s' % entry['id']
    path = '%s/%s' % (SETS_FOLDER, asset_name)
    if assets.does_asset_exist(path):
        if entry['id'] not in get_resets():
            unreal.log('CAVE %s exists and keeps its values' % path)
            return path, None
        unreal.log('CAVE %s takes the values of the script again' % path)
        return path, unreal.load_asset(path)
    return path, unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, SETS_FOLDER, unreal.HexPlanetObjectSet, unreal.DataAssetFactory())


def import_meshes(manifest):
    assets = unreal.EditorAssetLibrary
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    try:
        pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as error:
        unreal.log_warning('CAVE could not set the import of vertex colors: %s' % error)

    tasks = []
    for entry in manifest['meshes']:
        if not is_chosen(entry['name']):
            continue
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
        if not is_chosen(name):
            continue
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
        mesh.set_editor_property('allow_cpu_access', True)
        mesh.set_material(0, unreal.load_asset(MATERIALS[entry['material']]))

        # the editor subsystem that sets LODs is missing in a commandlet, so the module does it
        table = LODS[entry.get('lods', 'default')]
        lods = unreal.HexPlanetObjectSet.set_mesh_lods(mesh, [share for share, _ in table], [screen for _, screen in table])
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        unreal.log('CAVE %s: %d triangles, %d LODs (%s)' % (name, entry['triangles'], lods, entry.get('lods', 'default')))


def make_set(entry):
    assets = unreal.EditorAssetLibrary
    path, config = take_set(entry)
    if not config:
        return
    config.set_editor_property('id', entry['id'])
    config.set_editor_property('display_name', entry['name'])
    config.set_editor_property('sort_order', entry['order'])
    config.set_editor_property('palette_color', unreal.LinearColor(*entry['color'], 1.0))
    config.set_editor_property('layer', unreal.HexObjectLayer.COVER if entry['layer'] == 'cover' else unreal.HexObjectLayer.DECOR)
    config.set_editor_property('planet_layers', UNDERGROUND_LAYER)
    config.set_editor_property('blocks_movement', False)
    config.set_editor_property('allowed_levels', [0])
    config.set_editor_property('count_by_density', entry['counts'])
    config.set_editor_property('scale_min', entry['scale'][0])
    config.set_editor_property('scale_max', entry['scale'][1])
    config.set_editor_property('giant_chance', entry['giant'][0])
    config.set_editor_property('giant_scale', entry['giant'][1])
    config.set_editor_property('spacing', entry['spacing'])
    config.set_editor_property('reach', entry['reach'])
    config.set_editor_property('min_up', entry['min_up'])
    config.set_editor_property('slope_align', entry['align'])
    config.set_editor_property('tilt', entry['tilt'])
    config.set_editor_property('sink', entry['sink'])
    config.set_editor_property('min_height', 0.0)
    config.set_editor_property('clip_footprint', True)
    config.set_editor_property('cast_shadow', entry['shadow'])
    config.set_editor_property('cull_distance', entry['cull'])
    config.set_editor_property('generate_group', entry['group'])
    config.set_editor_property('generate_patch_cells', entry['patch'])
    config.set_editor_property('exit_clear_rings', entry['rings'])
    glow = entry.get('glow')
    if glow:
        config.set_editor_property('glow_color', unreal.LinearColor(*glow['color'], 1.0))
        config.set_editor_property('glow_strength', glow['strength'])
        config.set_editor_property('glow_pulse', glow['pulse'])
        config.set_editor_property('glow_pulse_speed', glow['speed'])
        config.set_editor_property('glow_pool', glow['pool'])
        config.set_editor_property('spores', glow['spores'])
    variant = unreal.HexObjectVariant()
    variant.set_editor_property('biomes', '')
    variant.set_editor_property('meshes', [unreal.load_asset('%s/SM_CaveObject_%s' % (MESHES, mesh)) for mesh in entry['meshes']])
    variant.set_editor_property('generate_share', entry['share'])
    config.set_editor_property('variants', [variant])
    assets.save_asset(path, only_if_is_dirty=False)
    unreal.log('CAVE made %s' % path)


def make_rim_set(entry):
    assets = unreal.EditorAssetLibrary
    path, config = take_set(entry)
    if not config:
        return
    config.set_editor_property('id', entry['id'])
    config.set_editor_property('display_name', entry['name'])
    config.set_editor_property('sort_order', entry['order'])
    config.set_editor_property('palette_color', unreal.LinearColor(*entry['color'], 1.0))
    config.set_editor_property('layer', unreal.HexObjectLayer.RIM)
    config.set_editor_property('planet_layers', UNDERGROUND_LAYER)
    config.set_editor_property('blocks_movement', False)
    config.set_editor_property('allowed_levels', [0])
    config.set_editor_property('rim_min_drop', 2)
    config.set_editor_property('rim_count_min', entry['counts'][0])
    config.set_editor_property('count_by_density', [entry['counts'][1]])
    config.set_editor_property('rim_side_share', entry['share'])
    config.set_editor_property('rim_hang', entry.get('hang', False))
    config.set_editor_property('rim_inset', entry.get('inset', 0.12))
    config.set_editor_property('rim_hang_out', entry.get('out', 2.0))
    config.set_editor_property('rim_turn', entry['turn'])
    config.set_editor_property('scale_min', entry['scale'][0])
    config.set_editor_property('scale_max', entry['scale'][1])
    config.set_editor_property('giant_chance', entry['giant'][0])
    config.set_editor_property('giant_scale', entry['giant'][1])
    config.set_editor_property('spacing', entry['spacing'])
    # a rim stands on the flat lip of a passage; one that hangs finds its own face
    config.set_editor_property('min_up', 0.5)
    config.set_editor_property('slope_align', entry['align'])
    config.set_editor_property('tilt', entry['tilt'])
    config.set_editor_property('sink', entry['sink'])
    config.set_editor_property('min_height', 0.0)
    config.set_editor_property('cast_shadow', entry['shadow'])
    config.set_editor_property('cull_distance', entry['cull'])
    config.set_editor_property('exit_clear_rings', entry['rings'])
    glow = entry.get('glow')
    if glow:
        config.set_editor_property('glow_color', unreal.LinearColor(*glow['color'], 1.0))
        config.set_editor_property('glow_strength', glow['strength'])
        config.set_editor_property('glow_pulse', glow['pulse'])
        config.set_editor_property('glow_pulse_speed', glow['speed'])
    variant = unreal.HexObjectVariant()
    variant.set_editor_property('biomes', '')
    variant.set_editor_property('meshes', [unreal.load_asset('%s/SM_CaveObject_%s' % (MESHES, mesh)) for mesh in entry['meshes']])
    config.set_editor_property('variants', [variant])
    assets.save_asset(path, only_if_is_dirty=False)
    unreal.log('CAVE made %s' % path)


def main():
    with open(os.path.join(SOURCE_FOLDER, 'objects.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    known = set(entry['id'] for entry in SETS + RIM_SETS)
    unknown = get_resets() - known
    if unknown:
        raise RuntimeError('reset= names no set of SETS or RIM_SETS: %s' % ', '.join(sorted(unknown)))
    import_meshes(manifest)
    for entry in SETS:
        make_set(entry)
    for entry in RIM_SETS:
        make_rim_set(entry)
    unreal.log('CAVE DONE')


main()
