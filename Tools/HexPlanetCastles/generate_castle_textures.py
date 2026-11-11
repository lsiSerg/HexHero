"""Generates the masonry of the walls of the castles of the hex planet: a color and a normal texture for each style.

Run with Blender, which has numpy:
    blender --background --factory-startup --python generate_castle_textures.py
Writes T_CastleWall_<Style>.png and T_CastleWall_<Style>_N.png (the DirectX convention), and T_CastleRoof.png with its normal, the tiles of
the roofs, to Saved/HexPlanetCastles; import_planet_castles.py
imports them, and M_HexPlanetCastle draws them on the stone of a castle by the UV of its meshes. They repeat without a seam. They are generated
stand-ins, as the developer asked on 2026-11-11 for realistic masonry in two styles, one of men and one of the undead: photo-scanned sets would
take their place under the same names.

Human: courses of dressed blocks of warm grey stone in a running bond, with narrow joints of mortar. Undead: darker, rougher courses of uneven
heights with wide deep joints, cracks, blocks fallen out and green seep running down from the joints.
"""

import os
import struct
import sys
import zlib

import numpy as np

SIZE = 1024
OUTPUT_FOLDER = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'Saved', 'HexPlanetCastles'))


def write_png(path, pixels):
    """Writes an array of height x width x 3 of values from 0 to 1 as an 8 bit PNG"""
    data = (np.clip(pixels, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    rows = b''.join(b'\x00' + data[row].tobytes() for row in range(data.shape[0]))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, 'wb') as file:
        file.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', data.shape[1], data.shape[0], 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 6)) + chunk(b'IEND', b''))


def value_noise(cells, rng):
    """Smooth noise of a lattice of cells that repeats across the texture"""
    lattice = rng.random((cells, cells))
    at = np.arange(SIZE) * cells / SIZE
    low = at.astype(int) % cells
    high = (low + 1) % cells
    t = at - at.astype(int)
    t = t * t * (3.0 - 2.0 * t)
    top = lattice[low][:, low] * (1.0 - t)[None, :] + lattice[low][:, high] * t[None, :]
    bottom = lattice[high][:, low] * (1.0 - t)[None, :] + lattice[high][:, high] * t[None, :]
    return top * (1.0 - t)[:, None] + bottom * t[:, None]


def fbm(cells, octaves, rng):
    total = np.zeros((SIZE, SIZE))
    weight = 1.0
    norm = 0.0
    for octave in range(octaves):
        total += value_noise(cells * (2 ** octave), rng) * weight
        norm += weight
        weight *= 0.5
    return total / norm


def courses(rng, heights, blocks_range, joint, bevel):
    """Courses of blocks: for each pixel how far inside its block it lies, from 0 in a joint to 1, the tone of its block and its number"""
    inside = np.zeros((SIZE, SIZE))
    tone = np.zeros((SIZE, SIZE))
    number = np.zeros((SIZE, SIZE), dtype=int)
    x = np.arange(SIZE) / SIZE
    top = 0
    count = 0
    for height in heights:
        rows = max(int(round(height * SIZE)), 2)
        blocks = rng.integers(blocks_range[0], blocks_range[1] + 1)
        cuts = np.sort((np.arange(blocks) + rng.uniform(-0.3, 0.3, blocks)) / blocks + rng.uniform(0.0, 1.0)) % 1.0
        cuts = np.sort(cuts)
        # how far each column lies from the nearest joint between two blocks of the course, around the texture
        distance = np.min(np.abs(((x[:, None] - cuts[None, :] + 0.5) % 1.0) - 0.5), axis=1)
        block = np.searchsorted(cuts, x) % blocks
        y = (np.arange(rows) + 0.5) / rows
        vertical = np.minimum(y, 1.0 - y) * height
        edge = np.minimum(distance[None, :], vertical[:, None])
        rows_at = np.arange(top, top + rows) % SIZE
        inside[rows_at, :] = np.clip((edge - joint) / bevel, 0.0, 1.0)
        tones = rng.random(blocks)
        tone[rows_at, :] = tones[block][None, :]
        number[rows_at, :] = count + block[None, :]
        count += blocks
        top += rows
    return inside, tone, number, count


def normal_of(height, strength):
    """The normal of a height that repeats, with green down, as the engine reads a normal map"""
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * strength
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * strength
    length = np.sqrt(dx * dx + dy * dy + 1.0)
    return np.stack((-dx / length * 0.5 + 0.5, dy / length * 0.5 + 0.5, 1.0 / length * 0.5 + 0.5), axis=2)


def to_srgb(linear):
    linear = np.clip(linear, 0.0, 1.0)
    return np.where(linear <= 0.0031308, linear * 12.92, 1.055 * np.power(linear, 1.0 / 2.4) - 0.055)


def human(rng):
    inside, tone, _, _ = courses(rng, [1.0 / 8.0] * 8, (3, 4), 0.004, 0.012)
    grain = fbm(48, 4, rng)
    patches = fbm(6, 3, rng)
    stone = np.array([0.36, 0.33, 0.28])
    warm = np.array([0.44, 0.37, 0.27])
    mortar = np.array([0.13, 0.12, 0.105])
    base = stone[None, None, :] * (1.0 - tone[:, :, None] * 0.5) + warm[None, None, :] * (tone[:, :, None] * 0.5)
    base = base * (0.78 + 0.34 * tone[:, :, None]) * (0.82 + 0.36 * grain[:, :, None]) * (0.9 + 0.2 * patches[:, :, None])
    face = np.clip(inside * 3.0, 0.0, 1.0)[:, :, None]
    color = mortar[None, None, :] * (0.8 + 0.4 * grain[:, :, None]) * (1.0 - face) + base * face
    # the edges of a block are worn lighter
    color += (np.clip(inside * 6.0, 0.0, 1.0) - np.clip(inside * 1.6, 0.0, 1.0))[:, :, None] * 0.035
    height = inside * (0.85 + 0.15 * tone) + (grain - 0.5) * 0.1
    return to_srgb(color), normal_of(height, 5.0)


def undead(rng):
    heights = rng.uniform(0.7, 1.5, 7)
    heights = heights / heights.sum()
    inside, tone, number, count = courses(rng, heights, (2, 5), 0.009, 0.02)
    grain = fbm(40, 5, rng)
    patches = fbm(5, 3, rng)
    seep = fbm(9, 4, rng)
    # some blocks have fallen out, and leave a dark hole
    gone = rng.random(count) < 0.07
    hole = gone[number]
    stone = np.array([0.085, 0.09, 0.1])
    pale = np.array([0.17, 0.16, 0.18])
    joint = np.array([0.012, 0.014, 0.012])
    base = stone[None, None, :] * (1.0 - tone[:, :, None] * 0.6) + pale[None, None, :] * (tone[:, :, None] * 0.6)
    base = base * (0.7 + 0.5 * grain[:, :, None]) * (0.75 + 0.5 * patches[:, :, None])
    # cracks: thin lines where a noise crosses its middle, in some places only
    cracks = (np.abs(fbm(14, 3, rng) - 0.5) < 0.012) & (patches > 0.45)
    base[cracks] *= 0.25
    face = np.clip(inside * 2.2, 0.0, 1.0)
    face = np.where(hole, face * 0.08, face)[:, :, None]
    color = joint[None, None, :] * (1.0 - face) + base * face
    # green seep that runs down the wall from the joints
    stain = np.clip((seep - 0.52) * 5.0, 0.0, 1.0) * np.clip(1.2 - inside * 0.6, 0.0, 1.0)
    run = np.maximum.reduce([np.roll(stain, shift, axis=0) * (1.0 - shift / 60.0) for shift in range(0, 60, 6)])
    green = np.array([0.045, 0.11, 0.035])
    color = color * (1.0 - run[:, :, None] * 0.7) + green[None, None, :] * run[:, :, None] * 0.7
    height = np.where(hole, 0.0, inside * (0.7 + 0.3 * tone)) + (grain - 0.5) * 0.22 - cracks * 0.3
    return to_srgb(color), normal_of(height, 7.0)


def roof(rng):
    """Grey slates in even courses, which the material tints in the color of the owner"""
    inside, tone, _, _ = courses(rng, [1.0 / 11.0] * 11, (8, 9), 0.003, 0.01)
    grain = fbm(64, 4, rng)
    slate = np.array([0.16, 0.165, 0.18])
    base = slate[None, None, :] * (0.75 + 0.5 * tone[:, :, None]) * (0.85 + 0.3 * grain[:, :, None])
    face = np.clip(inside * 3.0, 0.0, 1.0)[:, :, None]
    color = base * (0.35 + 0.65 * face)
    height = inside * (0.8 + 0.2 * tone) + (grain - 0.5) * 0.08
    return to_srgb(color), normal_of(height, 5.0)


def main():
    os.makedirs(OUTPUT_FOLDER, exist_ok=True)
    for name, make, seed in (('T_CastleWall_Human', human, 11), ('T_CastleWall_Undead', undead, 23), ('T_CastleRoof', roof, 31)):
        color, normal = make(np.random.default_rng(seed))
        write_png(os.path.join(OUTPUT_FOLDER, name + '.png'), color)
        write_png(os.path.join(OUTPUT_FOLDER, name + '_N.png'), normal)
        print('%s written' % name)


main()
