"""Imports the texture sets that fetch_texture_sources.ps1 downloaded, puts them into DA_HexTerrainLayers and builds the texture arrays.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi, after import_biome_textures.py made the
generated layers. DA_HexTerrainLayers belongs to the artist once it exists: a set goes only into a layer that has no color yet or still the
generated T_Biome_ one, and a layer keeps its values otherwise, so running this again never undoes changes made by hand.
"""

import json
import os

import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCES = json.load(open(os.path.join(HERE, 'texture_sources.json')))
SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'TextureSources')
GENERATED_FOLDER = '/Game/HexMap/Textures/Biomes'
LAYERS_FOLDER = '/Game/HexMap/Textures/Layers'
CONFIG = '/Game/HexMap/Textures/DA_HexTerrainLayers'
# the order is the layer number in the biome assets, as in import_biome_textures.py
GENERATED_LAYERS = ['Grass', 'Sand', 'GrayRock', 'Snow', 'Swamp', 'RedRock', 'OchreRock', 'Basalt', 'Tundra', 'Steppe']
DEFAULT_TILE = 320.0
KINDS = [('color', 'Color'), ('normal', 'Normal'), ('height', 'Height')]


def import_set(entry):
    """Imports the color, normal and height of a set as T_<Layer>_<Kind> in a folder of the layer, and returns them by kind"""
    folder = '%s/%s' % (LAYERS_FOLDER, entry['layer'])
    tasks = []
    for kind, suffix in KINDS:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, entry.get('source', 'polyhaven'), entry['id'], '%s_%s.jpg' % (entry['id'], kind)))
        task.set_editor_property('destination_path', folder)
        task.set_editor_property('destination_name', 'T_%s_%s' % (entry['layer'], suffix))
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    textures = {}
    for kind, suffix in KINDS:
        path = '%s/T_%s_%s' % (folder, entry['layer'], suffix)
        texture = unreal.load_asset(path)
        if texture is None:
            raise RuntimeError('could not import %s' % path)
        if kind == 'normal':
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif kind == 'height':
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_GRAYSCALE)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        textures[kind] = texture
    return textures


def make_layer(name, tile):
    layer = unreal.HexTerrainLayer()
    layer.set_editor_property('name', name)
    layer.set_editor_property('tile_size', tile)
    return layer


def is_generated(texture):
    return texture is None or texture.get_path_name().startswith(GENERATED_FOLDER + '/')


def main():
    assets = unreal.EditorAssetLibrary
    if assets.does_asset_exist(CONFIG):
        config = unreal.load_asset(CONFIG)
    else:
        folder, name = CONFIG.rsplit('/', 1)
        config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.HexTerrainLayersConfig, unreal.DataAssetFactory())
    layers = list(config.get_editor_property('layers'))

    # the generated layers keep their numbers, which the biome assets name
    for index, name in enumerate(GENERATED_LAYERS):
        if index >= len(layers):
            layers.append(make_layer(name, DEFAULT_TILE))
        layer = layers[index]
        if layer.get_editor_property('color') is None:
            layer.set_editor_property('color', unreal.load_asset('%s/T_Biome_%s_Color' % (GENERATED_FOLDER, name)))
            layer.set_editor_property('normal', unreal.load_asset('%s/T_Biome_%s_Normal' % (GENERATED_FOLDER, name)))

    for entry in SOURCES['sets']:
        names = [str(layer.get_editor_property('name')) for layer in layers]
        if entry['layer'] in names:
            layer = layers[names.index(entry['layer'])]
            if not is_generated(layer.get_editor_property('color')):
                unreal.log('TEXSRC %s keeps its textures' % entry['layer'])
                continue
        else:
            layer = make_layer(entry['layer'], DEFAULT_TILE)
            layers.append(layer)
        textures = import_set(entry)
        layer.set_editor_property('color', textures['color'])
        layer.set_editor_property('normal', textures['normal'])
        layer.set_editor_property('height', textures['height'])
        layer.set_editor_property('tile_size', float(entry.get('tileSize', DEFAULT_TILE)))
        if 'tint' in entry:
            layer.set_editor_property('tint', unreal.LinearColor(*entry['tint'], 1.0))
        if 'scree' in entry:
            layer.set_editor_property('scree_layer', entry['scree'])
        if 'gravel' in entry:
            layer.set_editor_property('gravel_layer', entry['gravel'])
        unreal.log('TEXSRC %s from %s' % (entry['layer'], entry['id']))

    config.set_editor_property('layers', layers)
    if config.get_editor_property('color_array') is None:
        config.set_editor_property('color_array', unreal.load_asset(GENERATED_FOLDER + '/TA_Biome_Color'))
    if config.get_editor_property('normal_array') is None:
        config.set_editor_property('normal_array', unreal.load_asset(GENERATED_FOLDER + '/TA_Biome_Normal'))

    config.build_texture_arrays()
    assets.save_asset(CONFIG, only_if_is_dirty=False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    delete_unused_generated(layers)
    unreal.log('TEXSRC DONE')


def delete_unused_generated(layers):
    """Deletes the generated textures of a layer that a set replaced, as the arrays hold copies of the pixels and nothing else reads them"""
    used = set()
    for layer in layers:
        for kind in ('color', 'normal', 'height'):
            texture = layer.get_editor_property(kind)
            if texture is not None:
                used.add(texture.get_path_name().split('.')[0])
    for name in GENERATED_LAYERS:
        for suffix in ('Color', 'Normal'):
            path = '%s/T_Biome_%s_%s' % (GENERATED_FOLDER, name, suffix)
            if path not in used and unreal.EditorAssetLibrary.does_asset_exist(path):
                unreal.EditorAssetLibrary.delete_asset(path)
                unreal.log('TEXSRC deleted %s' % path)


main()
