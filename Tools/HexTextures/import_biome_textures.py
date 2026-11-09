"""Imports what build_biome_textures.py made and collects it into the two texture arrays that the hex materials sample.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
With the argument appended (-script="<this file> appended") it imports only the layers of APPENDED_LAYERS, adds to DA_HexTerrainLayers those it
lacks, by name, past the photo-scanned ones, and builds the arrays, leaving the other layers and T_HexNoise as they are.
"""

import os
import sys

import unreal

SOURCE_FOLDER = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()), 'HexTextures')
FOLDER = '/Game/HexMap/Textures/Biomes'
# the order is the layer number in the biome assets
LAYERS = ['Grass', 'Sand', 'GrayRock', 'Snow', 'Swamp', 'RedRock', 'OchreRock', 'Basalt', 'Tundra', 'Steppe']
# the layers past the photo-scanned ones, found by name (APPENDED_LAYERS of build_biome_textures.py), with the values a layer added for them takes
APPENDED_LAYERS = [('CaveFlagstones', {'tile_size': 140.0, 'normal_strength': 1.0, 'height_contrast': 1.0})]


CONFIG = '/Game/HexMap/Textures/DA_HexTerrainLayers'


def generated_layers():
    """The layers that still draw the generated textures: all of them before DA_HexTerrainLayers exists, then those whose color is still a
    T_Biome_ one, so a layer that a photo-scanned set or an artist replaced gets no generated textures again, which nothing would read"""
    if not unreal.EditorAssetLibrary.does_asset_exist(CONFIG):
        return LAYERS
    layers = unreal.load_asset(CONFIG).get_editor_property('layers')
    kept = []
    for index, name in enumerate(LAYERS):
        color = layers[index].get_editor_property('color') if index < len(layers) else None
        if color is None or color.get_path_name().startswith(FOLDER + '/'):
            kept.append(name)
    return kept


def import_layers(kind, names):
    tasks = []
    for layer in names:
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, 'T_Biome_%s_%s.png' % (layer, kind)))
        task.set_editor_property('destination_path', FOLDER)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', False)
        tasks.append(task)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks(tasks)

    textures = []
    for layer in names:
        path = '%s/T_Biome_%s_%s' % (FOLDER, layer, kind)
        texture = unreal.load_asset(path)
        if kind == 'Normal':
            texture.set_editor_property('srgb', False)
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
        textures.append(texture)
    return textures


def import_noise():
    """T_HexNoise, the noise of water and ice that the materials sample (HexNoise), once the alpha of the snow layer"""
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', os.path.join(SOURCE_FOLDER, 'T_HexNoise.png'))
    task.set_editor_property('destination_path', '/Game/HexMap/Textures')
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', False)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset('/Game/HexMap/Textures/T_HexNoise')
    texture.set_editor_property('srgb', False)
    texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_GRAYSCALE)
    unreal.EditorAssetLibrary.save_asset('/Game/HexMap/Textures/T_HexNoise', only_if_is_dirty=False)


def make_array(name, textures, srgb, compression):
    assets = unreal.EditorAssetLibrary
    path = '%s/%s' % (FOLDER, name)
    array = unreal.load_asset(path) if assets.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Texture2DArray, unreal.Texture2DArrayFactory())
    array.set_editor_property('srgb', srgb)
    array.set_editor_property('compression_settings', compression)
    # the factory makes an array without mips, which shimmers from far away and reads slowly
    array.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_FROM_TEXTURE_GROUP)
    # the same list again would not count as a change, and the array would keep its old pixels
    array.set_editor_property('source_textures', [])
    array.set_editor_property('source_textures', textures)
    assets.save_asset(path, only_if_is_dirty=False)

    check = unreal.load_asset(path)
    unreal.log('BIOMETEX %s sources=%d bytes=%s' % (name, len(check.get_editor_property('source_textures')), check.blueprint_get_texture_source_disk_and_memory_size()[1]))


def import_appended():
    """The appended layers alone: their textures are imported, a layer DA_HexTerrainLayers lacks is added at its end with its values, a layer that
    is there takes the new textures only while it still holds the generated ones, so what an artist set stays, and the arrays are built"""
    assets = unreal.EditorAssetLibrary
    if not assets.does_asset_exist(CONFIG):
        unreal.log_error('BIOMETEX %s is missing, run import_texture_sources.py first' % CONFIG)
        return
    names = [name for name, _ in APPENDED_LAYERS]
    colors = import_layers('Color', names)
    normals = import_layers('Normal', names)
    config = unreal.load_asset(CONFIG)
    layers = list(config.get_editor_property('layers'))
    for (name, values), color, normal in zip(APPENDED_LAYERS, colors, normals):
        found = [index for index, layer in enumerate(layers) if str(layer.get_editor_property('name')) == name]
        if found:
            layer = layers[found[0]]
            current = layer.get_editor_property('color')
            if current is not None and not current.get_path_name().startswith(FOLDER + '/'):
                unreal.log('BIOMETEX %s keeps its textures' % name)
                continue
        else:
            layer = unreal.HexTerrainLayer()
            layer.set_editor_property('name', name)
            for key, value in values.items():
                layer.set_editor_property(key, value)
            layers.append(layer)
            found = [len(layers) - 1]
        layer.set_editor_property('color', color)
        layer.set_editor_property('normal', normal)
        layers[found[0]] = layer
        unreal.log('BIOMETEX appended layer %s is layer %d' % (name, found[0]))
    config.set_editor_property('layers', layers)
    config.build_texture_arrays()
    assets.save_asset(CONFIG, only_if_is_dirty=False)
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    unreal.log('BIOMETEX appended done')


def main():
    if 'appended' in sys.argv[1:]:
        import_appended()
        return
    names = generated_layers()
    colors = import_layers('Color', names)
    normals = import_layers('Normal', names)
    import_noise()
    # once DA_HexTerrainLayers exists the arrays hold its layers, photo-scanned ones too (import_texture_sources.py), so it builds them
    if unreal.EditorAssetLibrary.does_asset_exist(CONFIG):
        unreal.load_asset(CONFIG).build_texture_arrays()
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
        return
    make_array('TA_Biome_Color', colors, True, unreal.TextureCompressionSettings.TC_DEFAULT)
    make_array('TA_Biome_Normal', normals, False, unreal.TextureCompressionSettings.TC_NORMALMAP)


main()
