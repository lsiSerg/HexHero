"""Makes the seamless surface textures of the biomes: one color with height in alpha and one normal per layer.

Run with: blender --background --factory-startup --python build_biome_textures.py [-- <Layer> ...]
Any Python with numpy works too. Writes PNG files to Saved/HexTextures. Then run import_biome_textures.py with the commandlet.
The order of LAYERS is the TerrainLayer and MountainRockLayer numbers in the biome assets. APPENDED_LAYERS are layers that DA_HexTerrainLayers
finds by name, past the photo-scanned ones, such as the flagstones of the underground. Names after -- write only those layers, so the others
and T_HexNoise stay byte for byte as they are (import_biome_textures.py appended then imports only the appended ones).
"""

import os
import sys
import struct
import zlib

import numpy as np

SIZE = 1024
OUTPUT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexTextures'))
NORMAL_STRENGTH = 5.0


def spectrum_noise(rng, power, low=1.0, high=None, stretch=1.0):
    """Noise that repeats exactly at the edges, as it is built from whole waves. Returns values from about -1 to 1. Stretch above one makes long shapes along the second axis."""
    frequency = np.fft.fftfreq(SIZE) * SIZE
    first, second = np.meshgrid(frequency, frequency, indexing='ij')
    radius = np.hypot(first, second * stretch)
    radius[0, 0] = 1.0
    filter_values = radius ** -power
    filter_values[radius < low] = 0.0
    if high:
        filter_values[radius > high] = 0.0
    noise = np.fft.ifft2(np.fft.fft2(rng.standard_normal((SIZE, SIZE))) * filter_values).real
    return noise / (np.abs(noise).max() + 1e-9)


def unit(noise):
    return noise * 0.5 + 0.5


def smooth(low, high, value):
    t = np.clip((value - low) / (high - low), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def mix(a, b, amount):
    return np.array(a)[None, None, :] * (1.0 - amount[..., None]) + np.array(b)[None, None, :] * amount[..., None]


def make_grass(rng):
    large, blades, flowers = spectrum_noise(rng, 1.1, 2.0), spectrum_noise(rng, 0.25, 80.0), spectrum_noise(rng, 0.5, 40.0)
    color = mix((0.055, 0.14, 0.022), (0.13, 0.20, 0.035), unit(large))
    color = color * (0.72 + 0.56 * unit(blades))[..., None]
    color = mix(color[0, 0] * 0 + (0.0, 0.0, 0.0), (0.0, 0.0, 0.0), np.zeros((SIZE, SIZE))) + color
    dry = smooth(0.25, 0.6, flowers)
    color = color * (1.0 - dry[..., None] * 0.35) + np.array((0.17, 0.15, 0.05)) * dry[..., None] * 0.35
    return color, unit(blades) * 0.6 + unit(large) * 0.4, 0.55


def make_sand(rng):
    large, grain = spectrum_noise(rng, 1.3, 2.0), spectrum_noise(rng, 0.1, 150.0)
    frequency = np.fft.fftfreq(SIZE) * SIZE
    x, y = np.meshgrid(np.arange(SIZE) / SIZE, np.arange(SIZE) / SIZE, indexing='ij')
    ripples = np.sin((x * 14.0 + y * 6.0 + large * 1.6) * 2.0 * np.pi)
    color = mix((0.50, 0.38, 0.21), (0.70, 0.56, 0.34), unit(large))
    color = color * (0.9 + 0.1 * unit(ripples) + 0.1 * grain)[..., None]
    return color, unit(ripples) * 0.45 + unit(large) * 0.45 + unit(grain) * 0.1, 0.5


def make_rock_height(rng):
    large, medium, fine = spectrum_noise(rng, 1.2, 2.0), spectrum_noise(rng, 0.9, 10.0), spectrum_noise(rng, 0.45, 60.0)
    cracks = 1.0 - np.abs(spectrum_noise(rng, 1.0, 6.0)) ** 0.5
    return np.clip(unit(large) * 0.55 + unit(medium) * 0.3 + unit(fine) * 0.15 - cracks * 0.35 + 0.15, 0.0, 1.0)


def make_rock(rng, dark, light):
    height = make_rock_height(rng)
    tone = spectrum_noise(rng, 1.3, 2.0)
    color = mix(dark, light, smooth(0.15, 0.75, height) * 0.75 + unit(tone) * 0.25)
    return color, height, 1.0


def make_snow(rng):
    large, grain = spectrum_noise(rng, 1.4, 2.0), spectrum_noise(rng, 0.1, 200.0)
    # below white, so the sun does not burn it out and its relief and grain still show
    color = mix((0.56, 0.62, 0.72), (0.78, 0.8, 0.84), unit(large))
    color = color * (0.98 + 0.03 * grain)[..., None]
    # the height in alpha stays smooth, as the materials use it as noise for water and ice. The relief is only in the normal:
    # wide drifts, ridges carved by wind and a fine crust
    drifts = spectrum_noise(rng, 1.3, 3.0, 40.0)
    ridges = 1.0 - np.abs(spectrum_noise(rng, 1.1, 4.0, 36.0, 2.5)) ** 0.6
    crust = spectrum_noise(rng, 0.9, 30.0, 140.0)
    # drawn after the noise above, so the alpha keeps its values: wind ripples across the drifts and a broken crust, which also shade the color,
    # so close up the snow has a grain and does not look blurred
    ripples = spectrum_noise(rng, 0.7, 40.0, 160.0, 3.0)
    flakes = spectrum_noise(rng, 0.4, 120.0)
    relief = (unit(drifts) * 0.62 + ridges * 0.3 * smooth(0.25, 0.7, unit(drifts)) + unit(crust) * 0.12 + unit(ripples) * 0.06
        + unit(flakes) * 0.025)
    shade = (unit(crust) - 0.5) * 0.12 + (unit(ripples) - 0.5) * 0.1 + (ridges - 0.5) * 0.06 + (unit(flakes) - 0.5) * 0.08
    color = color * (1.0 + shade)[..., None]
    return color, unit(large) * 0.9 + unit(grain) * 0.1, 1.0, relief


def make_swamp(rng):
    large, medium, fine = spectrum_noise(rng, 1.2, 2.0), spectrum_noise(rng, 0.8, 12.0), spectrum_noise(rng, 0.35, 90.0)
    ground = unit(large) * 0.6 + unit(medium) * 0.4
    color = mix((0.035, 0.04, 0.018), (0.075, 0.085, 0.03), unit(fine))
    moss = smooth(0.5, 0.75, ground)
    color = color * (1.0 - moss[..., None]) + np.array((0.06, 0.11, 0.025)) * moss[..., None]
    puddles = 1.0 - smooth(0.34, 0.42, ground)
    color = color * (1.0 - puddles[..., None]) + np.array((0.018, 0.028, 0.03)) * puddles[..., None]
    return color, np.maximum(ground, 0.34) * 0.8 + unit(fine) * 0.2 * (1.0 - puddles), 0.45


def make_tundra(rng):
    # cold low ground seen from above: grey-green moss and brown heath in patches, pale lichen and bare grey stones between
    large, medium, fine = spectrum_noise(rng, 1.2, 2.0), spectrum_noise(rng, 0.8, 14.0), spectrum_noise(rng, 0.35, 90.0)
    color = mix((0.06, 0.085, 0.04), (0.15, 0.13, 0.07), smooth(0.35, 0.65, unit(large)))
    heath = smooth(0.5, 0.6, unit(medium))
    color = color * (1.0 - heath[..., None] * 0.5) + np.array((0.12, 0.065, 0.05)) * heath[..., None] * 0.5
    lichen = smooth(0.56, 0.64, unit(fine) * 0.5 + unit(medium) * 0.5)
    color = color * (1.0 - lichen[..., None] * 0.55) + np.array((0.3, 0.3, 0.25)) * lichen[..., None] * 0.55
    stones = smooth(0.64, 0.7, unit(spectrum_noise(rng, 0.7, 30.0)))
    color = color * (1.0 - stones[..., None]) + np.array((0.2, 0.2, 0.2)) * stones[..., None]
    color = color * (0.85 + 0.3 * unit(fine))[..., None]
    return color, unit(large) * 0.4 + unit(medium) * 0.35 + stones * 0.25, 0.6


def make_steppe(rng):
    # dry grassland: straw-yellow and olive grass in wide patches, with bare dusty earth between
    large, blades, patches = spectrum_noise(rng, 1.1, 2.0), spectrum_noise(rng, 0.25, 80.0), spectrum_noise(rng, 0.8, 10.0)
    color = mix((0.16, 0.14, 0.05), (0.3, 0.24, 0.1), unit(large))
    olive = smooth(0.4, 0.65, unit(patches))
    color = color * (1.0 - olive[..., None] * 0.5) + np.array((0.1, 0.12, 0.035)) * olive[..., None] * 0.5
    bare = smooth(0.7, 0.82, unit(spectrum_noise(rng, 0.6, 30.0)))
    color = color * (1.0 - bare[..., None] * 0.6) + np.array((0.3, 0.22, 0.14)) * bare[..., None] * 0.6
    color = color * (0.78 + 0.44 * unit(blades))[..., None]
    return color, unit(blades) * 0.5 + unit(large) * 0.3 + (1.0 - bare) * 0.2, 0.55


def make_flagstones(rng):
    """Worn flagstones of the paths of the underground, seen from above: irregular slabs of a periodic Voronoi of 7 x 7 jittered seeds, in a low
    region of about a third of the tile hexagonal tiles of a regular hex lattice instead, each slab of its own tone with a fine grain, bevelled,
    a few cracked and about one in eight missing, filled with the dark earth of the joints"""
    coords = (np.arange(SIZE) + 0.5) / SIZE
    x, y = np.meshgrid(coords, coords, indexing='ij')
    # where the hex tiles lie, a low noise of the whole tile
    region = unit(spectrum_noise(rng, 1.6, 1.0, 3.0))
    hex_mask = region > np.quantile(region, 0.7)

    # the seeds: a jittered grid of 7 x 7 outside the hex region, and a hex lattice of 7 x 8 inside it, both periodic in the tile
    seeds = []
    for i in range(7):
        for j in range(7):
            seeds.append(((i + 0.5 + rng.uniform(-0.38, 0.38)) / 7.0, (j + 0.5 + rng.uniform(-0.38, 0.38)) / 7.0, False))
    for j in range(8):
        for i in range(7):
            seeds.append(((i + 0.25 + 0.5 * (j % 2)) / 7.0, (j + 0.5) / 8.0, True))
    mask_at = lambda sx, sy: hex_mask[int(sx * SIZE) % SIZE, int(sy * SIZE) % SIZE]
    seeds = [(sx % 1.0, sy % 1.0) for sx, sy, hexagonal in seeds if mask_at(sx, sy) == hexagonal]

    # the nearest and second nearest seed of every pixel, the edges wrapping around, so the tile repeats
    first = np.full((SIZE, SIZE), 9.0)
    second = np.full((SIZE, SIZE), 9.0)
    nearest = np.zeros((SIZE, SIZE), np.int32)
    for index, (sx, sy) in enumerate(seeds):
        dx = np.abs(x - sx)
        dx = np.minimum(dx, 1.0 - dx)
        dy = np.abs(y - sy)
        dy = np.minimum(dy, 1.0 - dy)
        distance = np.sqrt(dx * dx + dy * dy)
        closer = distance < first
        second = np.where(closer, first, np.minimum(second, distance))
        nearest = np.where(closer, index, nearest)
        first = np.where(closer, distance, first)
    edge = second - first

    # joints of dark earth, their width wobbled by noise, and bevelled slabs inside them
    wobble = spectrum_noise(rng, 1.0, 6.0, 40.0)
    joint_width = 0.008 * (1.0 + 0.5 * wobble)
    slab = smooth(joint_width, joint_width + 0.012, edge)
    tones = rng.uniform(0.0, 1.0, len(seeds))
    heights = rng.uniform(0.7, 0.9, len(seeds))
    missing = rng.uniform(0.0, 1.0, len(seeds)) < 0.12
    tone = tones[nearest]
    slab = slab * (~missing[nearest])
    grain = spectrum_noise(rng, 0.3, 90.0)
    stone = mix((0.16, 0.14, 0.12), (0.30, 0.27, 0.22), tone) * (0.9 + 0.2 * unit(grain))[..., None]
    # cracks of a ridged noise across some slabs, darker and lower
    cracks = smooth(0.975, 0.993, 1.0 - np.abs(spectrum_noise(rng, 1.3, 4.0, 30.0))) * (unit(spectrum_noise(rng, 1.2, 2.0)) > 0.6)
    stone = stone * (1.0 - 0.45 * cracks[..., None])
    dirt_noise = unit(spectrum_noise(rng, 0.8, 20.0))
    dirt = mix((0.08, 0.06, 0.045), (0.11, 0.085, 0.06), dirt_noise)
    color = dirt * (1.0 - slab[..., None]) + stone * slab[..., None]
    height = (0.1 + 0.15 * dirt_noise) * (1.0 - slab) + (heights[nearest] + 0.04 * grain - 0.15 * cracks) * slab
    return color, np.clip(height, 0.0, 1.0), 0.8


LAYERS = [
    ('Grass', make_grass),
    ('Sand', make_sand),
    ('GrayRock', lambda rng: make_rock(rng, (0.045, 0.045, 0.05), (0.27, 0.26, 0.25))),
    ('Snow', make_snow),
    ('Swamp', make_swamp),
    ('RedRock', lambda rng: make_rock(rng, (0.055, 0.022, 0.014), (0.36, 0.15, 0.085))),
    ('OchreRock', lambda rng: make_rock(rng, (0.09, 0.055, 0.025), (0.47, 0.33, 0.17))),
    ('Basalt', lambda rng: make_rock(rng, (0.012, 0.012, 0.014), (0.085, 0.08, 0.08))),
    ('Tundra', make_tundra),
    ('Steppe', make_steppe),
]
# layers past the photo-scanned ones, which DA_HexTerrainLayers finds by name (import_biome_textures.py appended); their seeds follow LAYERS
APPENDED_LAYERS = [
    ('CaveFlagstones', make_flagstones),
]


def write_png(path, pixels):
    """Writes 8 bit RGBA, so the height in alpha is kept as it is."""
    height, width, _ = pixels.shape
    rows = b''.join(b'\0' + pixels[row].tobytes() for row in range(height))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    with open(path, 'wb') as file:
        file.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def to_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * linear ** (1.0 / 2.4) - 0.055)


def to_bytes(values):
    return (np.clip(values, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)


def get_normal(height, strength):
    """X goes with the first image axis. The edges wrap, as the texture repeats."""
    slope_y = (np.roll(height, -1, 0) - np.roll(height, 1, 0)) * 0.5 * SIZE / 64.0 * strength * NORMAL_STRENGTH / 5.0
    slope_x = (np.roll(height, -1, 1) - np.roll(height, 1, 1)) * 0.5 * SIZE / 64.0 * strength * NORMAL_STRENGTH / 5.0
    normal = np.stack([-slope_x, -slope_y, np.ones_like(height)], -1)
    return normal / np.linalg.norm(normal, axis=-1, keepdims=True)


def main():
    os.makedirs(OUTPUT, exist_ok=True)
    # names after -- (as Blender passes the arguments of the script) write only those layers
    chosen = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    every = LAYERS + APPENDED_LAYERS
    unknown = [name for name in chosen if name not in [each for each, _ in every]]
    if unknown:
        raise SystemExit('Unknown layers %s, known are %s' % (unknown, [each for each, _ in every]))
    for index, (name, make) in enumerate(every):
        if chosen and name not in chosen:
            continue
        made = make(np.random.default_rng(500 + index))
        color, height, strength = made[:3]
        height = np.clip(height, 0.0, 1.0)
        if name == 'Snow':
            # the noise of water and ice of the materials (T_HexNoise), kept apart from the snow, so the snow layer can be replaced
            write_png(os.path.join(OUTPUT, 'T_HexNoise.png'), np.concatenate([np.repeat(to_bytes(height)[..., None], 3, -1), np.full((SIZE, SIZE, 1), 255, np.uint8)], -1))
        write_png(os.path.join(OUTPUT, 'T_Biome_%s_Color.png' % name), np.concatenate([to_bytes(to_srgb(color)), to_bytes(height)[..., None]], -1))
        normal = get_normal(made[3] if len(made) > 3 else height, strength)
        write_png(os.path.join(OUTPUT, 'T_Biome_%s_Normal.png' % name), np.concatenate([to_bytes(normal * 0.5 + 0.5), np.full((SIZE, SIZE, 1), 255, np.uint8)], -1))
        print('LAYER', index, name, 'color %.3f height %.3f' % (color.mean(), height.mean()), flush=True)
    print('DONE', flush=True)


main()
