"""Imports the meshes that generate_planet_objects.py wrote, and creates the kinds of objects of the planet (UHexPlanetObjectSet) that are missing.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after create_planet_assets.py made
M_HexPlanetFoliage and M_HexPlanetBoulder. The meshes go to /Game/HexPlanet/Objects/Meshes, replaced each run, with two reduced LODs and the
material their kind needs; the sets go to /Game/HexPlanet/Objects/Sets (PlanetObjectsFolder of HexMapSettings) only when an asset of that name is
missing, as they belong to the artist once they exist: delete one to have it made again from SETS.
"""

import json
import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetObjects')
MESHES = '/Game/HexPlanet/Objects/Meshes'
SETS_FOLDER = '/Game/HexPlanet/Objects/Sets'
MATERIALS = {'foliage': '/Game/HexPlanet/M_HexPlanetFoliage', 'stone': '/Game/HexPlanet/M_HexPlanetBoulder'}
# the share of triangles of each LOD and the screen size from which it is drawn; a tree 30 high covers about 0.04 of the screen 600 away
LODS = [(1.0, 1.0), (0.35, 0.05), (0.12, 0.02)]

# biome letters: G grass, D sand, N snow, M swamp, T tundra, P steppe, R rock
TREES = {'G': ['Broadleaf_0', 'Broadleaf_1', 'Broadleaf_2', 'Spruce_0', 'Pine_0', 'Birch_0'], 'N': ['Spruce_0', 'Spruce_1', 'Spruce_2'],
         'T': ['Spruce_1', 'Spruce_2', 'Birch_0', 'Birch_1'], 'M': ['Snag_0', 'Snag_1', 'Willow_0', 'Birch_1'], 'P': ['Acacia_0', 'Acacia_1', 'Broadleaf_1'],
         'D': ['Acacia_0', 'Acacia_1']}

# each kind: its values, then its variants as (biomes, meshes, snow, scale, count, generated share)
SETS = [
    {'id': 'Forest', 'name': 'Forest', 'order': 10, 'color': (0.05, 0.2, 0.06), 'layer': 'cover', 'blocks': True, 'levels': [0, 1],
     'counts': [10, 24, 46], 'scale': (1.15, 1.7), 'giant': (0.03, 1.8), 'spacing': 10.0, 'reach': 1.0, 'min_up': 0.72, 'align': 0.1, 'tilt': 4.0,
     'sink': 1.0, 'shadow': True, 'cull': 5000.0, 'far': (0.9, (0.03, 0.065, 0.025)),
     'variants': [('G', TREES['G'], 0.0, 1.0, 1.0, 0.33), ('N', TREES['N'], 0.85, 0.9, 1.0, 0.25), ('T', TREES['T'], 0.0, 0.75, 0.7, 0.3),
                  ('M', TREES['M'], 0.0, 0.9, 0.6, 0.3), ('P', TREES['P'], 0.0, 0.9, 0.45, 0.07), ('D', TREES['D'], 0.0, 0.85, 0.3, 0.03)]},
    {'id': 'Bushes', 'name': 'Bushes', 'order': 20, 'color': (0.3, 0.4, 0.1), 'layer': 'cover', 'blocks': False, 'levels': [0, 1],
     'counts': [10, 22, 40], 'scale': (1.0, 1.6), 'giant': (0.0, 1.0), 'spacing': 7.0, 'reach': 1.0, 'min_up': 0.6, 'align': 0.4, 'tilt': 6.0,
     'sink': 0.5, 'shadow': False, 'cull': 3500.0, 'far': (0.35, (0.07, 0.085, 0.03)),
     'variants': [('G', ['Bush_0', 'Bush_1', 'Bush_2'], 0.0, 1.0, 1.0, 0.12), ('N', ['Bush_0', 'Bush_2'], 0.9, 0.9, 1.0, 0.08),
                  ('T', ['TundraShrub_0', 'TundraShrub_1'], 0.0, 1.0, 1.0, 0.25), ('M', ['Reeds_0', 'Reeds_1', 'Bush_2'], 0.0, 1.0, 1.0, 0.3),
                  ('P', ['DryBush_0', 'DryBush_1', 'Bush_1'], 0.0, 1.0, 1.0, 0.25), ('D', ['DryBush_0', 'DryBush_1'], 0.0, 0.9, 0.5, 0.15)]},
    {'id': 'Flowers', 'name': 'Flowers', 'order': 30, 'color': (0.75, 0.6, 0.1), 'layer': 'cover', 'blocks': False, 'levels': [0, 1],
     'counts': [8, 16, 28], 'scale': (1.1, 1.6), 'giant': (0.0, 1.0), 'spacing': 8.0, 'reach': 1.0, 'min_up': 0.8, 'align': 0.8, 'tilt': 0.0,
     'sink': 0.2, 'shadow': False, 'cull': 2500.0,
     'variants': [('G', ['Flowers_0', 'Flowers_1', 'Flowers_2'], 0.0, 1.0, 1.0, 0.08), ('P', ['Flowers_0', 'Flowers_2'], 0.0, 1.0, 1.0, 0.06),
                  ('T', ['Flowers_1'], 0.0, 0.8, 1.0, 0.05)]},
    {'id': 'Stones', 'name': 'Stones', 'order': 40, 'color': (0.45, 0.45, 0.42), 'layer': 'cover', 'blocks': False, 'levels': [0, 1, 2],
     'counts': [5, 10, 18], 'scale': (0.7, 1.4), 'giant': (0.05, 1.8), 'spacing': 7.0, 'reach': 1.0, 'min_up': 0.4, 'align': 0.9, 'tilt': 25.0,
     'sink': 0.6, 'shadow': False, 'cull': 3000.0,
     'variants': [('', ['Stone_0', 'Stone_1', 'Stone_2', 'Stone_3'], 0.0, 1.0, 1.0, 0.03)]},
    {'id': 'Rocks', 'name': 'Rocks', 'order': 50, 'color': (0.3, 0.28, 0.26), 'layer': 'cover', 'blocks': True, 'levels': [0, 1, 2],
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
        mesh.set_material(0, unreal.load_asset(MATERIALS[entry['material']]))

        # the editor subsystem that sets LODs is missing in a commandlet, so the module does it
        lods = unreal.HexPlanetObjectSet.set_mesh_lods(mesh, [share for share, _ in LODS], [screen for _, screen in LODS])
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        unreal.log('OBJECTS %s: %d triangles, %d LODs' % (name, entry['triangles'], lods))


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


def main():
    with open(os.path.join(SOURCE_FOLDER, 'objects.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    import_meshes(manifest)
    for entry in SETS:
        make_set(entry)
    unreal.log('OBJECTS DONE')


main()
