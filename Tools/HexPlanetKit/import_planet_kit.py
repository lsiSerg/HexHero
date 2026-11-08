"""Imports the land kit that generate_planet_kit.py wrote, and writes DA_HexPlanetKit with it and the mountains of the flat map.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
The planet bends the pieces on the CPU, so every mesh gets CPU access, and the land pieces are not Nanite.
"""

import glob
import json
import math
import os

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetKit')
DESTINATION = '/Game/HexPlanet/Kit'
CONFIG = DESTINATION + '/DA_HexPlanetKit'
MOUNTAINS = '/Game/HexMap/Meshes/Mountains'
MOUNTAIN_PARAMS = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'HexMountains', 'mountain_params.json')

MOUNTAIN_LEVEL = 2
BOULDER_MATERIAL = '/Game/HexPlanet/M_HexPlanetBoulder'
FLOE_MATERIAL = '/Game/HexPlanet/M_HexPlanetFloe'
MOUNTAIN_COLOR = (0.30, 0.27, 0.24)
MOUNTAIN_SLOPE_COLOR = (0.17, 0.14, 0.12)
# the planet samples the mountains on a coarser lattice than their 24 steps, as it shows a whole hemisphere at once
MOUNTAIN_SUBDIVISIONS = 12
# a massif grows less than on the flat map, as the planet shows mountains against the sky at its edge
MOUNTAIN_MASSIF_STEP = 0.2
MOUNTAIN_MASSIF_RISE = 30.0
MOUNTAIN_MASSIF_MAX_STEPS = 3

# how each level varies by cell (FHexPlanetKit, Variety): mirrored pieces, and a factor of the height above scale_base from a noise of the place
# of about noise_cells cells, a jitter and now and then a peak, so a chain rises and falls and neighbors differ
VARIETY = {
    1: {'mirror': True, 'height_scale_min': 0.55, 'height_scale_max': 1.3, 'height_noise_cells': 3.0, 'height_jitter': 0.2,
        'peak_chance': 0.0, 'peak_scale': 0.0, 'scale_base': -15.0, 'center_shift': 0.3,
        # hills next to hills rise together into an upland that swallows part of their knolls, rather than each standing as a mound
        'plateau_lift': 16.0, 'plateau_fill': 0.45},
    # a mountain never goes below height_scale_floor, so it reads as one, and a lone one is a broad tall dome in the middle of its cell
    MOUNTAIN_LEVEL: {'mirror': True, 'height_scale_min': 0.65, 'height_scale_max': 1.35, 'height_noise_cells': 3.5, 'height_jitter': 0.2,
                     # a spur, the end of a chain with one neighbor, steps down instead of standing as a tower
                     'height_scale_floor': 0.75, 'lone_neighbors': 1, 'spur_height_scale': 0.8, 'lone_height_min': 0.95, 'lone_sharpness': 0.5, 'lone_shift': 0.25, 'lone_base': 0.6,
                     # lone mountains also take clusters of two or three summits of unlike heights
                     # a full base out towards the sides, and crests that split into two blades along parts of the ranges
                     'bulk_lift': 30.0, 'bulk_reach': 0.65, 'split_depth': 0.3, 'split_blade': 0.1, 'split_width': 0.24,
                     'split_noise_cells': 2.5, 'split_threshold': 0.0,
                     'lone_summit_styles': 3, 'summit_base': 0.45, 'summit_height': 1.15, 'summit_width': 1.3,
                     'peak_chance': 0.14, 'peak_scale': 0.6, 'scale_base': -45.0, 'center_shift': 0.35,
                     # a dome to a needle by cell, summits and notches, ridges turned, and faces of split stone on the high ground of big mountains
                     'shape_height': 105.0, 'sharpness_min': 0.7, 'sharpness_max': 1.4,
                     # ridges over high saddles to the neighbors, so a range reads as one chain with peaks on it
                     'ridge_lift': 30.0, 'ridge_width': 0.3, 'crag_height': 14.0, 'crag_scale': 45.0, 'twist': 35.0,
                     'jagged_height': 7.0, 'jagged_scale': 34.0, 'facet_height_start': 70.0, 'facet_height_end': 140.0,
                     # gullies down the slopes and ledges, so the sides are rock and not cloth
                     'gully_height': 16.0, 'gully_count': 9.0, 'gully_length': 1.2, 'ledge_step': 18.0, 'ledge_strength': 0.5},
}


def get_mask(digits):
    return sum(1 << bit for bit, digit in enumerate(digits) if digit == '1')


def report(mesh, name, foot_depth, hex_radius):
    """The middle of a side towards a lower neighbor lies at the foot, which shows a mirrored or turned piece, and every triangle faces up"""
    description = mesh.get_static_mesh_description(0)
    positions = {}
    for index in range(description.get_vertex_count()):
        position = description.get_vertex_position(unreal.VertexID(index))
        positions[(round(position.x), round(position.y))] = position.z

    wrong_sides = 0
    digits = name.split('_')[3]
    for direction, digit in enumerate(digits):
        angle = math.radians(30.0 + 60.0 * direction)
        height = positions.get((round(math.cos(angle) * hex_radius * 0.866025), round(math.sin(angle) * hex_radius * 0.866025)))
        if height is None or (digit == '0' and abs(height + foot_depth) > 0.5) or (digit == '1' and not -foot_depth - 0.5 <= height <= 0.5):
            wrong_sides += 1

    facing_up = 0
    for index in range(description.get_triangle_count()):
        triangle = unreal.TriangleID(index)
        a, b, c = [description.get_vertex_position(description.get_vertex_instance_vertex(description.get_triangle_vertex_instance(triangle, corner))) for corner in range(3)]
        if (c.x - a.x) * (b.y - a.y) - (c.y - a.y) * (b.x - a.x) > 0.0:
            facing_up += 1

    unreal.log('PLANETKIT %s triangles=%d wrong_sides=%d facing_up=%d' % (name, description.get_triangle_count(), wrong_sides, facing_up))
    return wrong_sides == 0 and facing_up == description.get_triangle_count()


def prepare(mesh, nanite):
    """The planet reads the vertices of every piece"""
    mesh.set_editor_property('allow_cpu_access', True)
    if not nanite:
        settings = mesh.get_editor_property('nanite_settings')
        settings.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', settings)
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)


def import_land(manifest):
    paths = sorted(glob.glob(os.path.join(SOURCE_FOLDER, '*.glb')))
    if not paths:
        raise RuntimeError('No pieces in %s, run generate_planet_kit.py first' % SOURCE_FOLDER)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)

    if unreal.EditorAssetLibrary.does_directory_exist(DESTINATION):
        unreal.EditorAssetLibrary.delete_directory(DESTINATION)

    tasks = []
    for path in paths:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', path)
        task.set_editor_property('destination_path', DESTINATION)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    good = True
    for path in paths:
        name = os.path.splitext(os.path.basename(path))[0]
        target = '%s/%s' % (DESTINATION, name)
        # the glTF importer gives every file its own folders
        unreal.EditorAssetLibrary.rename_asset('%s/%s/StaticMeshes/%s' % (DESTINATION, name, name), target)
        unreal.EditorAssetLibrary.delete_directory('%s/%s' % (DESTINATION, name))
        mesh = unreal.load_asset(target)
        if not mesh:
            unreal.log_error('PLANETKIT could not load %s' % name)
            good = False
            continue
        if name not in manifest.get('boulders', []) + manifest.get('floes', []) + manifest.get('shards', []):
            good = report(mesh, name, manifest['foot_depth'], manifest['hex_radius']) and good
        prepare(mesh, False)
    if not good:
        raise RuntimeError('Some pieces came through wrong, see the PLANETKIT lines in the log')


def make_kit(level, mesh_radius, foot_depth, fade_height, follows_corners, color, slope_color, meshes, variants):
    kit = unreal.HexPlanetKit()
    kit.set_editor_property('level', level)
    kit.set_editor_property('mesh_radius', mesh_radius)
    kit.set_editor_property('foot_depth', foot_depth)
    kit.set_editor_property('fade_height', fade_height)
    kit.set_editor_property('top_follows_corners', follows_corners)
    kit.set_editor_property('color', unreal.LinearColor(color[0], color[1], color[2], 1.0))
    kit.set_editor_property('slope_color', unreal.LinearColor(slope_color[0], slope_color[1], slope_color[2], 1.0))
    mesh_map = unreal.Map(int, unreal.StaticMesh)
    for mask, mesh in meshes.items():
        mesh_map[mask] = mesh
    kit.set_editor_property('meshes', mesh_map)
    variant_map = unreal.Map(int, unreal.HexJoinedVariants)
    for mask, meshes_of_mask in variants.items():
        entry = unreal.HexJoinedVariants()
        entry.set_editor_property('meshes', meshes_of_mask)
        variant_map[mask] = entry
    kit.set_editor_property('variants', variant_map)
    for name, value in VARIETY.get(level, {}).items():
        kit.set_editor_property(name, value)
    return kit


def main():
    with open(os.path.join(SOURCE_FOLDER, 'kit.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    import_land(manifest)

    kits = []
    for entry in manifest['kits']:
        meshes = {}
        variants = {}
        for mask, names in entry['meshes'].items():
            loaded = [unreal.load_asset('%s/%s' % (DESTINATION, name)) for name in names]
            meshes[int(mask)] = loaded[0]
            if len(loaded) > 1:
                variants[int(mask)] = loaded[1:]
        # land models the sides towards its own level anywhere between the edge and 0, so its whole height between them follows the frame
        kits.append(make_kit(entry['level'], manifest['hex_radius'], manifest['foot_depth'], manifest['foot_depth'], True,
                             entry['color'], entry['slope_color'], meshes, variants))

    # the mountains of the flat map, by the same masks. Their sides towards mountains match by themselves, so only their low part follows the side lines
    with open(MOUNTAIN_PARAMS, encoding='utf-8') as file:
        params = json.load(file)
    meshes = {}
    variants = {}
    for path in unreal.EditorAssetLibrary.list_assets(MOUNTAINS, recursive=False):
        name = path.split('.')[-1]
        parts = name.split('_')
        mesh = unreal.load_asset(path)
        prepare(mesh, True)
        if len(parts) > 3:
            variants.setdefault(get_mask(parts[2]), []).append(mesh)
        else:
            meshes[get_mask(parts[2])] = mesh
    if len(meshes) != 14:
        raise RuntimeError('Expected 14 mountain meshes in %s' % MOUNTAINS)
    mountains = make_kit(MOUNTAIN_LEVEL, params['hex_radius'], -params['foot_height'], params['rim_fade_height'], False,
                         MOUNTAIN_COLOR, MOUNTAIN_SLOPE_COLOR, meshes, variants)
    mountains.set_editor_property('subdivisions', MOUNTAIN_SUBDIVISIONS)
    mountains.set_editor_property('massif_step', MOUNTAIN_MASSIF_STEP)
    mountains.set_editor_property('massif_rise', MOUNTAIN_MASSIF_RISE)
    mountains.set_editor_property('massif_max_steps', MOUNTAIN_MASSIF_MAX_STEPS)
    kits.append(mountains)

    config = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_HexPlanetKit', DESTINATION, unreal.HexPlanetKitConfig, unreal.DataAssetFactory())
    config.set_editor_property('kits', kits)

    # the boulders at the foot of mountains, drawn with M_HexPlanetBoulder of create_planet_assets.py; the other values keep the defaults of the class
    boulders = unreal.HexPlanetBoulders()
    boulders.set_editor_property('meshes', [unreal.load_asset('%s/%s' % (DESTINATION, name)) for name in manifest.get('boulders', [])])
    boulder_material = unreal.load_asset(BOULDER_MATERIAL)
    if boulder_material:
        boulders.set_editor_property('material', boulder_material)
    else:
        unreal.log_warning('PLANETKIT %s is missing, run Tools/create_planet_assets.py and this script again' % BOULDER_MATERIAL)
    config.set_editor_property('boulders', boulders)

    # the floes of sea ice, drawn with M_HexPlanetFloe of create_planet_assets.py; where they go is in DA_HexPlanet
    floes = unreal.HexPlanetFloes()
    floes.set_editor_property('meshes', [unreal.load_asset('%s/%s' % (DESTINATION, name)) for name in manifest.get('floes', [])])
    floes.set_editor_property('shard_meshes', [unreal.load_asset('%s/%s' % (DESTINATION, name)) for name in manifest.get('shards', [])])
    floe_material = unreal.load_asset(FLOE_MATERIAL)
    if floe_material:
        floes.set_editor_property('material', floe_material)
    else:
        unreal.log_warning('PLANETKIT %s is missing, run Tools/create_planet_assets.py and this script again' % FLOE_MATERIAL)
    config.set_editor_property('floes', floes)
    unreal.EditorAssetLibrary.save_asset(CONFIG, only_if_is_dirty=False)

    config = unreal.load_asset(CONFIG)
    for kit in config.get_editor_property('kits'):
        unreal.log('PLANETKIT level %d: %d pieces, %d with variants' % (kit.get_editor_property('level'), len(kit.get_editor_property('meshes')), len(kit.get_editor_property('variants'))))


main()
