"""Imports the masonry and the meshes of the castles (UHexCastles) and creates DA_HexCastles when it is missing.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, in three steps: with the argument
textures (-script="<this file> textures") it imports only the masonry, T_CastleWall_<Style> and its normal: the photo-scanned set of each
style that fetch_castle_textures.ps1 downloaded to Saved/TextureSources/castle (castle_texture_sources.json, CC0, 1K), else what
generate_castle_textures.py wrote; then create_planet_assets.py (or its argument heroes) builds M_HexPlanetCastle, which draws them; then, without an argument, it imports
the meshes that generate_planet_castles.py wrote, with that material. All go to /Game/HexPlanet/Castles, replaced each run, and the meshes
named as old in the manifest are deleted. DA_HexCastles (UHexCastleConfig) holds the factions, their creatures and buildings and every rule of
the castles, and belongs to whoever tunes it, so it is only created, with the defaults of its class, and never changed here.
"""

import json
import os
import sys

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexPlanetCastles')
FOLDER = '/Game/HexPlanet/Castles'
CONFIG = 'DA_HexCastles'
MATERIAL = '/Game/HexPlanet/M_HexPlanetCastle'
TEXTURES = ['T_CastleWall_Human', 'T_CastleWall_Undead', 'T_CastleRoof']
DOWNLOAD_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'TextureSources', 'castle')
SOURCES_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'castle_texture_sources.json')


def find_sources():
    """The file of the color and of the normal of each texture: the downloaded set of its style when both its files are there, else the
    generated ones"""
    with open(SOURCES_FILE, encoding='utf-8') as file:
        styles = {entry['style']: entry['id'] for entry in json.load(file)['sets']}
    found = {}
    for name in TEXTURES:
        source = styles.get(name.split('_')[-1].replace('Castle', ''), '')
        color = os.path.join(DOWNLOAD_FOLDER, source, source + '_color.jpg')
        normal = os.path.join(DOWNLOAD_FOLDER, source, source + '_normal.jpg')
        if source and os.path.exists(color) and os.path.exists(normal):
            found[name] = (color, normal)
            unreal.log('CASTLES %s from %s' % (name, source))
        else:
            found[name] = (os.path.join(SOURCE_FOLDER, name + '.png'), os.path.join(SOURCE_FOLDER, name + '_N.png'))
            unreal.log('CASTLES %s from the generated masonry' % name)
    return found


def import_textures():
    """The masonry of the two styles and the tiles of the roofs: a color and a normal each, which repeat"""
    assets = unreal.EditorAssetLibrary
    sources = find_sources()
    tasks = []
    for name in TEXTURES:
        for index, suffix in enumerate(('', '_N')):
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', sources[name][index])
            task.set_editor_property('destination_path', FOLDER)
            task.set_editor_property('destination_name', name + suffix)
            task.set_editor_property('automated', True)
            task.set_editor_property('replace_existing', True)
            task.set_editor_property('save', True)
            tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)
    for name in TEXTURES:
        for suffix in ('', '_N'):
            path = '%s/%s%s' % (FOLDER, name, suffix)
            texture = unreal.load_asset(path)
            if not texture:
                raise RuntimeError('could not import %s' % path)
            if suffix:
                texture.set_editor_property('srgb', False)
                texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property('address_x', unreal.TextureAddress.TA_WRAP)
            texture.set_editor_property('address_y', unreal.TextureAddress.TA_WRAP)
            assets.save_loaded_asset(texture, only_if_is_dirty=False)
            unreal.log('CASTLES %s imported' % path)
    unreal.log('CASTLES TEXTURES DONE')


def main():
    if 'textures' in sys.argv[1:]:
        import_textures()
        return
    assets = unreal.EditorAssetLibrary
    with open(os.path.join(SOURCE_FOLDER, 'castles.json'), encoding='utf-8') as file:
        manifest = json.load(file)
    material = unreal.load_asset(MATERIAL)
    if not material:
        raise RuntimeError('%s is missing: run Tools/create_planet_assets.py heroes first' % MATERIAL)
    for name in manifest.get('old', []):
        if assets.does_asset_exist('%s/%s' % (FOLDER, name)):
            assets.delete_asset('%s/%s' % (FOLDER, name))
            unreal.log('CASTLES %s deleted' % name)

    pipeline = unreal.InterchangeGenericAssetsPipeline()
    pipeline.get_editor_property('material_pipeline').set_editor_property('import_materials', False)
    try:
        pipeline.get_editor_property('common_meshes_properties').set_editor_property('vertex_color_import_option', unreal.InterchangeVertexColorImportOption.IVCIO_REPLACE)
    except Exception as error:
        unreal.log_warning('CASTLES could not set the import of vertex colors: %s' % error)
    tasks = []
    for entry in manifest['meshes']:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, entry['name'] + '.glb'))
        task.set_editor_property('destination_path', FOLDER)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', True)
        task.set_editor_property('options', pipeline)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    for entry in manifest['meshes']:
        name = entry['name']
        target = '%s/%s' % (FOLDER, name)
        # the glTF importer gives every file its own folders
        nested = '%s/%s/StaticMeshes/%s' % (FOLDER, name, name)
        if assets.does_asset_exist(nested):
            if assets.does_asset_exist(target):
                assets.delete_asset(target)
            assets.rename_asset(nested, target)
            assets.delete_directory('%s/%s' % (FOLDER, name))
        mesh = unreal.load_asset(target)
        if not mesh:
            raise RuntimeError('could not import %s' % name)
        nanite = mesh.get_editor_property('nanite_settings')
        nanite.set_editor_property('enabled', False)
        mesh.set_editor_property('nanite_settings', nanite)
        mesh.set_material(0, material)
        # one LOD drawn at every size, as the sites have: a castle is seen from afar, and reduced its walls and roofs fall apart
        if unreal.HexPlanetObjectSet.set_mesh_lods(mesh, [1.0], [1.0]) != 1:
            raise RuntimeError('%s has not one LOD' % name)
        assets.save_loaded_asset(mesh, only_if_is_dirty=False)
        unreal.log('CASTLES %s: %d triangles' % (name, entry['triangles']))

    config_path = '%s/%s' % (FOLDER, CONFIG)
    if not assets.does_asset_exist(config_path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(CONFIG, FOLDER, unreal.HexCastleConfig, unreal.DataAssetFactory())
        assets.save_asset(config_path, only_if_is_dirty=False)
        unreal.log('CASTLES %s created with the defaults of its class' % CONFIG)
    unreal.log('CASTLES DONE')


main()
