"""Lists the mountain meshes and that material in the mountain level asset.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
"""

import json
import os

import unreal

SOURCE = '/Game/HexMap/Meshes/Mountains'
MOUNTAIN_LEVEL = '/Game/HexMap/Levels/DA_Level_Mountains'
MOUNTAIN_MATERIAL = '/Game/HexMap/Materials/M_HexMountain'

def main():
    joined = {}
    variants = {}
    for path in unreal.EditorAssetLibrary.list_assets(SOURCE, recursive=False):
        name = path.split('.')[-1]
        parts = name.split('_')
        mask = sum(1 << bit for bit, digit in enumerate(parts[2]) if digit == '1')
        if len(parts) > 3:
            variants.setdefault(mask, []).append(unreal.load_asset(path))
        else:
            joined[mask] = unreal.load_asset(path)

    unreal.log('MOUNTAIN found %d meshes' % len(joined))
    if len(joined) != 14:
        raise RuntimeError('Expected 14 mountain meshes')

    # the debug wireframe turns Nanite off and draws the fallback mesh, which keeps every triangle so that it shows the real mesh
    for mesh in list(joined.values()) + [mesh for meshes_of_mask in variants.values() for mesh in meshes_of_mask]:
        settings = mesh.get_editor_property('nanite_settings')
        settings.set_editor_property('fallback_target', unreal.NaniteFallbackTarget.PERCENT_TRIANGLES)
        settings.set_editor_property('fallback_percent_triangles', 1.0)
        mesh.set_editor_property('nanite_settings', settings)
        # the hex planet reads their vertices to bend them onto its cells (Tools/HexPlanetKit/import_planet_kit.py)
        mesh.set_editor_property('allow_cpu_access', True)
        unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)

    material = unreal.load_asset(MOUNTAIN_MATERIAL)

    level = unreal.load_asset(MOUNTAIN_LEVEL)
    meshes = unreal.Map(int, unreal.StaticMesh)
    for mask, mesh in joined.items():
        meshes[mask] = mesh
    level.set_editor_property('joined_meshes', meshes)
    variant_map = unreal.Map(int, unreal.HexJoinedVariants)
    for mask, meshes_of_mask in variants.items():
        entry = unreal.HexJoinedVariants()
        entry.set_editor_property('meshes', meshes_of_mask)
        variant_map[mask] = entry
    level.set_editor_property('joined_mesh_variants', variant_map)
    # the edges of mountains lie on the terrain, so no corner needs filling
    level.set_editor_property('joined_notch_mesh', None)
    level.set_editor_property('feature_material', material)

    # what the game needs of the values the meshes were built with
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'mountain_params.json'), encoding='utf-8') as params_file:
        params = json.load(params_file)
    level.set_editor_property('joined_mesh_radius', params['hex_radius'])
    level.set_editor_property('joined_mesh_foot_height', params['foot_height'])
    level.set_editor_property('joined_mesh_rim_fade_height', params['rim_fade_height'])
    level.set_editor_property('joined_mesh_massif_step', params['massif_step'])
    level.set_editor_property('joined_mesh_massif_rise', params['massif_rise'])
    level.set_editor_property('joined_mesh_massif_max_steps', params['massif_max_steps'])
    unreal.EditorAssetLibrary.save_asset(MOUNTAIN_LEVEL, only_if_is_dirty=False)

    level = unreal.load_asset(MOUNTAIN_LEVEL)
    unreal.log('MOUNTAIN variants %s' % sorted(level.get_editor_property('joined_mesh_variants').keys()))
    unreal.log('MOUNTAIN assigned %s material %s' % (sorted(level.get_editor_property('joined_meshes').keys()), level.get_editor_property('feature_material').get_name()))


main()
