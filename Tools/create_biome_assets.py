"""Creates the biome assets in Content/HexMap/Biomes and resets their values.

Run with UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script=<this file> -unattended -nullrhi
"""

import unreal

FOLDER = '/Game/HexMap/Biomes'
WATER_LEVELS = [-2, -1]
LAND_LEVELS = [0, 1, 2]

# layers of the biome textures, in the order of LAYERS in HexTextures/build_biome_textures.py
GRASS, SAND, GRAY_ROCK, SNOW, SWAMP, RED_ROCK, OCHRE_ROCK, BASALT, TUNDRA, STEPPE = range(10)
# the seabed of a layer under the water, where it is not its beach: sand everywhere, so the bottom does not change with ice or snow
SEABEDS = {SNOW: SAND}
NO_BEACH = -1

# the layers of the cave biomes are found by name in DA_HexTerrainLayers, as import_texture_sources.py appends the scree and gravel of the rocks
TERRAIN_LAYERS = '/Game/HexMap/Textures/DA_HexTerrainLayers'
CAVE_FLOOR_LAYER = ('GrayGravel', 11)
CAVE_ROCK_LAYER = ('Basalt', BASALT)
CAVE_FLOOR, CAVE_ROCK = 'cave floor', 'cave rock'

# asset name, display name, file color, planet letter, underground letter, allowed levels, default for levels, move cost multiplier, blocks
# everything, terrain layer, mountain rock, beach of a planet, snow line, water opacity, frozen. A biome of the surface has a planet letter and
# one of the underground an underground letter, as the letters of the two layers are apart; the underground biomes are the default of no level,
# as the flat map takes one default per level from all biomes, and the underground fits them by their allowed levels
BIOMES = [
    ('DA_Biome_Water', 'Water', (20, 80, 170), 'W', '', WATER_LEVELS, WATER_LEVELS, 1.0, False, SAND, GRAY_ROCK, SAND, 120.0, 0.72, False),
    ('DA_Biome_Shoals', 'Shoals', (90, 190, 220), 'S', '', [-1], [], 1.0, False, SAND, GRAY_ROCK, SAND, 120.0, 0.4, False),
    ('DA_Biome_Ice', 'Ice', (210, 235, 245), 'I', '', WATER_LEVELS, [], 1.0, True, SAND, GRAY_ROCK, NO_BEACH, 40.0, 0.72, True),
    ('DA_Biome_Grass', 'Grass', (70, 160, 50), 'G', '', LAND_LEVELS, LAND_LEVELS, 1.0, False, GRASS, GRAY_ROCK, SAND, 110.0, 0.5, False),
    ('DA_Biome_Sand', 'Sand', (225, 200, 120), 'D', '', LAND_LEVELS, [], 1.5, False, SAND, OCHRE_ROCK, SAND, 140.0, 0.5, False),
    ('DA_Biome_Rock', 'Rock', (130, 125, 120), 'R', '', LAND_LEVELS, [], 1.25, False, GRAY_ROCK, RED_ROCK, GRAY_ROCK, 125.0, 0.5, False),
    ('DA_Biome_Snow', 'Snow', (245, 245, 250), 'N', '', LAND_LEVELS, [], 2.0, False, SNOW, GRAY_ROCK, SNOW, 55.0, 0.5, False),
    ('DA_Biome_Swamp', 'Swamp', (70, 90, 50), 'M', '', [0], [], 2.5, False, SWAMP, BASALT, SAND, 130.0, 0.5, False),
    ('DA_Biome_Tundra', 'Tundra', (125, 130, 95), 'T', '', LAND_LEVELS, [], 1.5, False, TUNDRA, GRAY_ROCK, SAND, 80.0, 0.5, False),
    ('DA_Biome_Steppe', 'Steppe', (190, 170, 90), 'P', '', LAND_LEVELS, [], 1.0, False, STEPPE, OCHRE_ROCK, SAND, 130.0, 0.5, False),
    # the underground: the floor of its passages, of gravel under walls of dark rock, and the abyss of its void, which nothing crosses
    ('DA_Biome_CaveFloor', 'Cave floor', (105, 100, 92), '', 'F', LAND_LEVELS, [], 1.0, False, CAVE_FLOOR, CAVE_ROCK, NO_BEACH, 310.0, 0.5, False),
    ('DA_Biome_Abyss', 'Abyss', (25, 45, 50), '', 'A', WATER_LEVELS, [], 1.0, True, CAVE_ROCK, CAVE_ROCK, NO_BEACH, 310.0, 0.5, False),
]


def find_layer(name, fallback):
    """Returns the index of the texture layer of a name in DA_HexTerrainLayers, or the fallback when the asset or the layer is missing."""
    config = unreal.load_asset(TERRAIN_LAYERS) if unreal.EditorAssetLibrary.does_asset_exist(TERRAIN_LAYERS) else None
    if config:
        for index, layer in enumerate(config.get_editor_property('layers')):
            if str(layer.get_editor_property('name')) == name:
                return index
    unreal.log_warning('BIOME no texture layer %s in %s, taking layer %d' % (name, TERRAIN_LAYERS, fallback))
    return fallback


def main():
    assets = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()

    cave_layers = {CAVE_FLOOR: find_layer(*CAVE_FLOOR_LAYER), CAVE_ROCK: find_layer(*CAVE_ROCK_LAYER)}

    for name, display_name, color, letter, underground_letter, allowed, defaults, cost, blocks, layer, rock, beach, snow, opacity, frozen in BIOMES:
        layer = cave_layers.get(layer, layer)
        rock = cave_layers.get(rock, rock)
        path = '%s/%s' % (FOLDER, name)
        biome = unreal.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, FOLDER, unreal.HexBiomeConfig, unreal.DataAssetFactory())
        biome.set_editor_property('display_name', display_name)
        biome.set_editor_property('file_color', unreal.Color(r=color[0], g=color[1], b=color[2], a=255))
        biome.set_editor_property('planet_letter', letter)
        biome.set_editor_property('underground_letter', underground_letter)
        biome.set_editor_property('allowed_levels', allowed)
        biome.set_editor_property('default_for_levels', defaults)
        biome.set_editor_property('terrain_layer', layer)
        biome.set_editor_property('mountain_rock_layer', rock)
        biome.set_editor_property('planet_beach_layer', beach)
        biome.set_editor_property('planet_seabed_layer', SEABEDS.get(layer, -1))
        biome.set_editor_property('snow_line', snow)
        biome.set_editor_property('water_opacity', opacity)
        biome.set_editor_property('frozen', frozen)
        biome.set_editor_property('move_cost_multiplier', cost)
        biome.set_editor_property('blocks_ships', blocks)
        biome.set_editor_property('blocks_heroes', blocks)
        biome.set_editor_property('blocks_units', blocks)
        assets.save_asset(path, only_if_is_dirty=False)
        unreal.log('BIOME %s' % name)


main()
