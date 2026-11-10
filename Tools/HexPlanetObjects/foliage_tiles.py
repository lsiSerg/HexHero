"""The layout of T_HexFoliage, the atlas of the plants of the hex planet, shared by generate_foliage_atlas.py, which draws its tiles, and
generate_planet_objects.py, whose cards show them."""

ATLAS_NAME = 'T_HexFoliage'
ATLAS_SIZE = 1024

# each tile: its place in the atlas as (x, y, width, height) in pixels, and whether it holds its own colors (a colored tile, whose cards are
# white) or is nearly white for the color of the vertices of its cards to tint (a tinted tile)
TILES = {
    'LeafA': (0, 0, 256, 256, False), 'LeafB': (256, 0, 256, 256, False), 'Oak': (512, 0, 256, 256, False), 'Birch': (768, 0, 256, 256, False),
    'Spruce': (0, 256, 256, 256, False), 'Pine': (256, 256, 256, 256, False), 'Willow': (512, 256, 256, 256, False), 'Acacia': (768, 256, 256, 256, False),
    'Bush': (0, 512, 256, 256, False), 'DryTwig': (256, 512, 256, 256, False), 'Grass': (512, 512, 256, 256, False), 'DryGrass': (768, 512, 256, 256, True),
    'Wheat': (0, 768, 128, 256, True), 'Reeds': (128, 768, 128, 256, True), 'FlowersA': (256, 768, 128, 256, True), 'FlowersB': (384, 768, 128, 256, True),
    'BlossomWhite': (512, 768, 128, 128, True), 'BlossomYellow': (640, 768, 128, 128, True), 'BlossomRed': (512, 896, 128, 128, True),
    'BlossomBlue': (640, 896, 128, 128, True), 'GrassTop': (768, 768, 128, 128, False), 'WheatTop': (896, 768, 128, 128, True),
    'Heather': (768, 896, 128, 128, True), 'Rosette': (896, 896, 128, 128, False),
}


def tile_uv(name):
    """The corners of a tile in the atlas as (u0, v0, u1, v1), v growing downwards as in the file"""
    x, y, width, height, _ = TILES[name]
    return x / ATLAS_SIZE, y / ATLAS_SIZE, (x + width) / ATLAS_SIZE, (y + height) / ATLAS_SIZE
