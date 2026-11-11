"""Imports the meshes that generate_planet_objects.py wrote, and creates the kinds of objects of the planet (UHexPlanetObjectSet) that are missing.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py (or its
argument foliage) imported the atlas T_HexFoliage and made M_HexPlanetFoliage and M_HexPlanetBoulder. The meshes go to
/Game/HexPlanet/Objects/Meshes, replaced each run, with the material their kind needs and two more LODs: those of a mesh of cards, a tree or a
tuft, are the meshes the generator wrote for them with fewer and larger cards, and those of a solid mesh, a stone or a log, are reduced from its
triangles. The sets go to /Game/HexPlanet/Objects/Sets (PlanetObjectsFolder of HexMapSettings) only when an asset of that name is missing, as
they belong to the artist once they exist: delete one to have it made again from SETS. A set that is there takes the values of MIGRATIONS only
while it still holds their old values, so tuning by hand stays.
"""

import json
import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetObjects')
MESHES = '/Game/HexPlanet/Objects/Meshes'
# the meshes of the LODs of the cards are imported here, copied into the LODs of their mesh, and deleted again
LOD_MESHES = MESHES + '/Lods'
SETS_FOLDER = '/Game/HexPlanet/Objects/Sets'
MATERIALS = {'foliage': '/Game/HexPlanet/M_HexPlanetFoliage', 'stone': '/Game/HexPlanet/M_HexPlanetBoulder'}
# for a solid mesh: the share of triangles of each LOD and the screen size from which it is drawn; a tree 30 high covers about 0.04 of the
# screen 600 away. A mesh of cards has its screen sizes in the manifest of the generator
LODS = [(1.0, 1.0), (0.35, 0.05), (0.12, 0.02)]

# biome letters: G grass, D sand, N snow, M swamp, T tundra, P steppe, R rock
TREES = {'G': ['Broadleaf_0', 'Broadleaf_1', 'Broadleaf_2', 'Spruce_0', 'Pine_0', 'Birch_0'], 'N': ['Spruce_0', 'Spruce_1', 'Spruce_2'],
         'T': ['Spruce_1', 'Spruce_2', 'Birch_0', 'Birch_1'], 'M': ['Snag_0', 'Snag_1', 'Willow_0', 'Birch_1'], 'P': ['Acacia_0', 'Acacia_1', 'Broadleaf_1'],
         'D': ['Acacia_0', 'Acacia_1']}

# each kind: its values, then its variants as (biomes, meshes, snow, scale, count, generated share)
SETS = [
    {'id': 'Forest', 'name': 'Forest', 'order': 10, 'color': (0.05, 0.2, 0.06), 'layer': 'cover', 'blocks': True, 'levels': [0, 1],
     'counts': [10, 24, 46], 'scale': (1.15, 1.7), 'giant': (0.03, 1.8), 'spacing': 10.0, 'reach': 1.0, 'min_up': 0.72, 'align': 0.1, 'tilt': 4.0,
     'sink': 1.0, 'shadow': True, 'cull': 5000.0, 'far': (0.9, (0.03, 0.065, 0.025)), 'patch': 7.0,
     'variants': [('G', TREES['G'], 0.0, 1.0, 1.0, 0.33), ('N', TREES['N'], 0.85, 0.9, 1.0, 0.25), ('T', TREES['T'], 0.0, 0.75, 0.7, 0.3),
                  ('M', TREES['M'], 0.0, 0.9, 0.6, 0.3), ('P', TREES['P'], 0.0, 0.9, 0.45, 0.07), ('D', TREES['D'], 0.0, 0.85, 0.3, 0.03)]},
    {'id': 'Bushes', 'name': 'Bushes', 'order': 20, 'color': (0.3, 0.4, 0.1), 'layer': 'cover', 'blocks': False, 'levels': [0, 1],
     'counts': [10, 22, 40], 'scale': (1.0, 1.6), 'giant': (0.0, 1.0), 'spacing': 9.0, 'reach': 1.0, 'min_up': 0.6, 'align': 0.4, 'tilt': 6.0,
     'sink': 0.5, 'shadow': True, 'cull': 3500.0, 'far': (0.35, (0.07, 0.085, 0.03)), 'patch': 4.0,
     'variants': [('G', ['Bush_0', 'Bush_1', 'Bush_2'], 0.0, 1.0, 1.0, 0.12), ('N', ['Bush_0', 'Bush_2'], 0.9, 0.9, 1.0, 0.08),
                  ('T', ['TundraShrub_0', 'TundraShrub_1'], 0.0, 1.0, 1.0, 0.25), ('M', ['Reeds_0', 'Reeds_1', 'Bush_2'], 0.0, 1.0, 1.0, 0.3),
                  ('P', ['DryBush_0', 'DryBush_1', 'Bush_1'], 0.0, 1.0, 1.0, 0.25), ('D', ['DryBush_0', 'DryBush_1'], 0.0, 0.9, 0.5, 0.15)]},
    {'id': 'Flowers', 'name': 'Flowers', 'order': 30, 'color': (0.75, 0.6, 0.1), 'layer': 'cover', 'blocks': False, 'levels': [0, 1],
     'counts': [10, 20, 36], 'scale': (1.1, 1.6), 'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 1.0, 'min_up': 0.8, 'align': 0.8, 'tilt': 0.0,
     'sink': 0.2, 'shadow': False, 'cull': 3500.0, 'far': (0.3, (0.1, 0.14, 0.045)), 'patch': 5.0,
     'variants': [('G', ['Flowers_0', 'Flowers_1', 'Flowers_2'], 0.0, 1.0, 1.0, 0.14), ('P', ['Flowers_0', 'Flowers_2'], 0.0, 1.0, 1.0, 0.08),
                  ('T', ['Flowers_1'], 0.0, 0.8, 1.0, 0.06)]},
    {'id': 'TallGrass', 'name': 'Tall grass', 'order': 34, 'color': (0.5, 0.55, 0.2), 'layer': 'cover', 'blocks': False, 'levels': [0, 1],
     'counts': [12, 26, 44], 'scale': (1.0, 1.5), 'giant': (0.0, 1.0), 'spacing': 9.0, 'reach': 1.0, 'min_up': 0.75, 'align': 0.8, 'tilt': 0.0,
     'sink': 0.2, 'shadow': False, 'cull': 3500.0, 'far': (0.45, (0.2, 0.17, 0.08)), 'patch': 6.0,
     'variants': [('GM', ['Grass_0', 'Grass_1'], 0.0, 1.0, 1.0, 0.06), ('PD', ['DryGrass_0', 'DryGrass_1'], 0.0, 1.0, 1.0, 0.3),
                  ('T', ['DryGrass_0', 'Grass_1'], 0.0, 0.85, 0.8, 0.1)]},
    {'id': 'Wheat', 'name': 'Wheat', 'order': 37, 'color': (0.85, 0.65, 0.2), 'layer': 'cover', 'blocks': False, 'levels': [0, 1],
     'counts': [38, 44, 50], 'scale': (1.05, 1.35), 'giant': (0.0, 1.0), 'spacing': 9.0, 'reach': 1.0, 'min_up': 0.85, 'align': 0.9, 'tilt': 0.0,
     'sink': 0.2, 'shadow': False, 'cull': 4000.0, 'far': (0.8, (0.34, 0.27, 0.11)), 'patch': 2.5,
     'variants': [('GP', ['Wheat_0', 'Wheat_1'], 0.0, 1.0, 1.0, 0.02)]},
    {'id': 'Stones', 'name': 'Stones', 'order': 40, 'color': (0.45, 0.45, 0.42), 'layer': 'cover', 'blocks': False, 'levels': [0, 1, 2],
     'counts': [5, 10, 18], 'scale': (0.7, 1.4), 'giant': (0.05, 1.8), 'spacing': 7.0, 'reach': 1.0, 'min_up': 0.4, 'align': 0.9, 'tilt': 25.0,
     'sink': 0.6, 'shadow': False, 'cull': 3000.0,
     'variants': [('', ['Stone_0', 'Stone_1', 'Stone_2', 'Stone_3'], 0.0, 1.0, 1.0, 0.03)]},
    {'id': 'Rocks', 'name': 'Rocks', 'order': 50, 'color': (0.3, 0.28, 0.26), 'layer': 'cover', 'blocks': False, 'levels': [0, 1, 2],
     'counts': [3, 6, 10], 'scale': (0.9, 1.6), 'giant': (0.05, 1.6), 'spacing': 20.0, 'reach': 0.9, 'min_up': 0.3, 'align': 0.8, 'tilt': 20.0,
     'sink': 2.0, 'shadow': True, 'cull': 6000.0,
     'variants': [('', ['Rock_0', 'Rock_1', 'Rock_2'], 0.0, 1.0, 1.0, 0.015)]},
    {'id': 'FallenTree', 'name': 'Fallen tree', 'order': 60, 'color': (0.3, 0.2, 0.1), 'layer': 'decor', 'blocks': False, 'levels': [0, 1],
     'counts': [1], 'scale': (1.3, 1.7), 'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.5, 'min_up': 0.6, 'align': 0.9, 'tilt': 3.0,
     'sink': 0.6, 'shadow': True, 'cull': 4000.0,
     'variants': [('GTMP', ['Log_0', 'Log_1'], 0.0, 1.0, 1.0, 0.01), ('N', ['Log_0', 'Log_1'], 0.8, 1.0, 1.0, 0.01)]},
    {'id': 'GiantTree', 'name': 'Giant tree', 'order': 70, 'color': (0.1, 0.35, 0.1), 'layer': 'decor', 'blocks': False, 'levels': [0, 1],
     'counts': [1], 'scale': (2.2, 3.0), 'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.3, 'min_up': 0.6, 'align': 0.05, 'tilt': 2.0,
     'sink': 1.5, 'shadow': True, 'cull': 9000.0,
     'variants': [('G', ['Oak_0'], 0.0, 1.0, 1.0, 0.006), ('N', ['Spruce_0'], 0.85, 1.3, 1.0, 0.006), ('T', ['Spruce_1'], 0.0, 1.2, 1.0, 0.006),
                  ('M', ['Willow_0'], 0.0, 1.0, 1.0, 0.006), ('PD', ['Acacia_0'], 0.0, 1.0, 1.0, 0.006)]},
    {'id': 'Boulder', 'name': 'Boulder', 'order': 80, 'color': (0.4, 0.38, 0.35), 'layer': 'decor', 'blocks': True, 'levels': [0, 1, 2],
     'counts': [1], 'scale': (2.4, 3.4), 'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.4, 'min_up': 0.3, 'align': 0.6, 'tilt': 15.0,
     'sink': 2.5, 'shadow': True, 'cull': 7000.0,
     'variants': [('', ['Rock_0', 'Rock_1', 'Rock_2'], 0.0, 1.0, 1.0, 0.008)]},
    {'id': 'DeadTree', 'name': 'Dead tree', 'order': 90, 'color': (0.35, 0.32, 0.28), 'layer': 'decor', 'blocks': False, 'levels': [0, 1],
     'counts': [1], 'scale': (1.4, 1.9), 'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.5, 'min_up': 0.6, 'align': 0.05, 'tilt': 6.0,
     'sink': 1.0, 'shadow': True, 'cull': 5000.0,
     'variants': [('GTMPDN', ['Snag_0', 'Snag_1'], 0.0, 1.0, 1.0, 0.006)]},
    {'id': 'Stump', 'name': 'Stump', 'order': 100, 'color': (0.4, 0.28, 0.15), 'layer': 'decor', 'blocks': False, 'levels': [0, 1],
     'counts': [1], 'scale': (1.3, 1.8), 'giant': (0.0, 1.0), 'spacing': 10.0, 'reach': 0.5, 'min_up': 0.6, 'align': 0.5, 'tilt': 3.0,
     'sink': 0.5, 'shadow': False, 'cull': 3000.0,
     'variants': [('GTM', ['Stump_0'], 0.0, 1.0, 1.0, 0.005), ('N', ['Stump_0'], 0.8, 1.0, 1.0, 0.004)]},
]

# values of sets that are there, as (set, property, old value, new value): a set takes the new one only while it still holds the old one. The
# flowers became meadows and the bushes thickets, larger than the tufts of before, so they stand further apart, show from further and, for the
# flowers, from far away as a tint of the ground; and each kind got patches of its own size, wider for forests and meadows
MIGRATIONS = [
    ('Forest', 'generate_patch_cells', 0.0, 7.0),
    ('Bushes', 'generate_patch_cells', 0.0, 4.0), ('Bushes', 'spacing', 7.0, 9.0), ('Bushes', 'cast_shadow', False, True),
    ('Flowers', 'generate_patch_cells', 0.0, 5.0), ('Flowers', 'spacing', 8.0, 10.0), ('Flowers', 'cull_distance', 2500.0, 3500.0),
    ('Flowers', 'count_by_density', [8, 16, 28], [10, 20, 36]), ('Flowers', 'far_cover', 0.0, 0.3),
    ('Flowers', 'far_color', (0.035, 0.07, 0.03), (0.1, 0.14, 0.045)),
    # a few rocks on open ground read as ground one can walk over, so they block no hero (the developer, 2026-11-11)
    ('Rocks', 'blocks_movement', True, False),
]
# the shares the variants of sets that are there generate, as (set, biomes of the variant, old share, new share)
SHARE_MIGRATIONS = [('Flowers', 'G', 0.08, 0.14), ('Flowers', 'P', 0.06, 0.08), ('Flowers', 'T', 0.05, 0.06)]


def find_imported(folder, name):
    """Moves a mesh the glTF importer put into folders of its own to its folder, and returns it"""
    assets = unreal.EditorAssetLibrary
    target = '%s/%s' % (folder, name)
    nested = '%s/%s/StaticMeshes/%s' % (folder, name, name)
    if assets.does_asset_exist(nested):
        if assets.does_asset_exist(target):
            assets.delete_asset(target)
        assets.rename_asset(nested, target)
        assets.delete_directory('%s/%s' % (folder, name))
    mesh = unreal.load_asset(target)
    if not mesh:
        raise RuntimeError('could not import %s' % name)
    return mesh


def import_meshes(manifest):
    assets = unreal.EditorAssetLibrary
    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    try:
        pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as error:
        unreal.log_warning('OBJECTS could not set the import of vertex colors: %s' % error)

    tasks = []
    for entry in manifest['meshes']:
        for name, folder in [(entry['name'], MESHES)] + [(lod, LOD_MESHES) for lod in entry.get('lods', [])]:
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, name + '.glb'))
            task.set_editor_property('destination_path', folder)
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', True)
            task.set_editor_property('save', True)
            task.set_editor_property('options', pipeline)
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for entry in manifest['meshes']:
        name = entry['name']
        mesh = find_imported(MESHES, name)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        mesh.set_material(0, unreal.load_asset(MATERIALS[entry['material']]))

        # the editor subsystem that sets LODs is missing in a commandlet, so the module does it
        if entry.get('lods'):
            lods = unreal.HexPlanetObjectSet.set_mesh_lods_from(mesh, [find_imported(LOD_MESHES, lod) for lod in entry['lods']], entry['screens'])
        else:
            lods = unreal.HexPlanetObjectSet.set_mesh_lods(mesh, [share for share, _ in LODS], [screen for _, screen in LODS])
        if lods != 3:
            raise RuntimeError('%s has %d LODs' % (name, lods))
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        unreal.log('OBJECTS %s: %d triangles, %d LODs' % (name, entry['triangles'], lods))
    if assets.does_directory_exist(LOD_MESHES):
        assets.delete_directory(LOD_MESHES)


def make_set(entry):
    assets = unreal.EditorAssetLibrary
    asset_name = 'DA_HexObject_%s' % entry['id']
    path = '%s/%s' % (SETS_FOLDER, asset_name)
    if assets.does_asset_exist(path):
        unreal.log('OBJECTS %s exists and keeps its values' % path)
        return
    config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(asset_name, SETS_FOLDER, unreal.HexPlanetObjectSet, unreal.DataAssetFactory())
    config.set_editor_property('id', entry['id'])
    config.set_editor_property('display_name', entry['name'])
    config.set_editor_property('sort_order', entry['order'])
    config.set_editor_property('palette_color', unreal.LinearColor(*entry['color'], 1.0))
    config.set_editor_property('layer', unreal.HexObjectLayer.COVER if entry['layer'] == 'cover' else unreal.HexObjectLayer.DECOR)
    config.set_editor_property('blocks_movement', entry['blocks'])
    config.set_editor_property('allowed_levels', entry['levels'])
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
    config.set_editor_property('cast_shadow', entry['shadow'])
    config.set_editor_property('cull_distance', entry['cull'])
    # how wide the patches of a cover are in a generated planet, in cells
    config.set_editor_property('generate_patch_cells', entry.get('patch', 0.0))
    # how the cover tints its ground from far away, where the objects are culled (M_HexPlanet, CANOPY_CODE)
    if 'far' in entry:
        config.set_editor_property('far_cover', entry['far'][0])
        config.set_editor_property('far_color', unreal.LinearColor(*entry['far'][1], 1.0))
    variants = []
    for biomes, meshes, snow, scale, count, share in entry['variants']:
        variant = unreal.HexObjectVariant()
        variant.set_editor_property('biomes', biomes)
        variant.set_editor_property('meshes', [unreal.load_asset('%s/SM_PlanetObject_%s' % (MESHES, mesh)) for mesh in meshes])
        variant.set_editor_property('snow', snow)
        variant.set_editor_property('scale_factor', scale)
        variant.set_editor_property('count_factor', count)
        variant.set_editor_property('generate_share', share)
        variants.append(variant)
    config.set_editor_property('variants', variants)
    assets.save_asset(path, only_if_is_dirty=False)
    unreal.log('OBJECTS created %s' % path)


def holds(value, old):
    """Returns true if a property holds an old value, a float to a few digits, as the asset keeps it in single precision"""
    if isinstance(value, unreal.LinearColor):
        return all(abs(a - b) <= 1e-4 for a, b in zip((value.r, value.g, value.b), old))
    if isinstance(old, float):
        return abs(float(value) - old) <= 1e-4 * max(1.0, abs(old))
    if isinstance(old, list):
        return list(value) == old
    return value == old


def migrate_sets():
    assets = unreal.EditorAssetLibrary
    changed = {}
    for set_id, name, old, new in MIGRATIONS:
        path = '%s/DA_HexObject_%s' % (SETS_FOLDER, set_id)
        config = unreal.load_asset(path) if assets.does_asset_exist(path) else None
        if config and holds(config.get_editor_property(name), old):
            config.set_editor_property(name, unreal.LinearColor(*new, 1.0) if isinstance(new, tuple) else new)
            changed[path] = config
            unreal.log('OBJECTS %s: %s from %s to %s' % (path, name, old, new))
    for set_id, biomes, old, new in SHARE_MIGRATIONS:
        path = '%s/DA_HexObject_%s' % (SETS_FOLDER, set_id)
        config = unreal.load_asset(path) if assets.does_asset_exist(path) else None
        if not config:
            continue
        variants = list(config.get_editor_property('variants'))
        for variant in variants:
            if variant.get_editor_property('biomes') == biomes and holds(variant.get_editor_property('generate_share'), old):
                variant.set_editor_property('generate_share', new)
                config.set_editor_property('variants', variants)
                changed[path] = config
                unreal.log('OBJECTS %s: the share of %s from %s to %s' % (path, biomes, old, new))
    for path in changed:
        assets.save_asset(path, only_if_is_dirty=False)


def main():
    with open(os.path.join(SOURCE_FOLDER, 'objects.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    import_meshes(manifest)
    for entry in SETS:
        make_set(entry)
    migrate_sets()
    unreal.log('OBJECTS DONE')


main()
