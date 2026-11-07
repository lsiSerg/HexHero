"""Trial of the mountains in Blender: adds erosion to the generated shapes and renders a group of hexes. Writes no project assets.

Run with: blender --background --factory-startup --python blender_mountain_trial.py
Writes Saved/HexMountains/Blender/Trial.png and Trial.blend.
"""

import math
import os
import random
import sys

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import generate_hex_mountains as g

# the shapes are relaxed on a finer lattice than the game meshes, and the detail goes on a finer one still
g.SUBDIVISIONS = 48
g.SMOOTH_PASSES = 10
UPSCALE = 4
N = g.SUBDIVISIONS * UPSCALE
STEP = g.HEX_RADIUS / N
# how far the arrays reach, as the base of a mesh can go past its hex
# with room for the margin of the bake, and a multiple of the coarser lattices, so their points fall on points of this one
LATTICE_REACH = g.get_lattice_reach() + (2 if g.get_lattice_reach() % 2 == 0 else 3)
REACH = LATTICE_REACH * UPSCALE
# how many fine steps the surface for the bake goes past the mesh
EDGE_MARGIN = 8
DROPLETS = 40000
GROUND_HEIGHT = g.FOOT_HEIGHT - 1.0
APRON_RISE = 1.0
# how far noise moves the line where the slopes start
FOOT_WANDER = 7.0

HEXES = [(0, 0), (1, 0), (2, 0), (3, 0), (1, 1), (0, 2), (6, 0)]
OUTPUT = os.path.join(g.OUTPUT_FOLDER, 'Blender')
STEPS = [(1, 0), (0, 1), (-1, 1), (-1, 0), (0, -1), (1, -1)]


def get_positions():
    a, b = np.meshgrid(np.arange(-REACH, REACH + 1), np.arange(-REACH, REACH + 1), indexing='ij')
    distance = np.maximum(np.maximum(np.abs(a), np.abs(b)), np.abs(a + b))
    return a, b, (a + b * 0.5) * STEP, b * math.sqrt(3.0) * 0.5 * STEP, distance


def upscale(mountain):
    """Spreads the heights over the finer lattice. A side stays the same seen from both hexes, as it is interpolated along itself."""
    low = g.SUBDIVISIONS
    offset = LATTICE_REACH
    size = offset * 2 + 3
    table = np.zeros((size, size))
    edges = np.zeros((size, size))
    valid = np.zeros((size, size), dtype=bool)
    for index, (a, b) in enumerate(mountain.points):
        table[a + offset, b + offset] = mountain.heights[index]
        edges[a + offset, b + offset] = mountain.edge_distance[index]
        valid[a + offset, b + offset] = True

    a, b, _, _, _ = get_positions()
    fine_a, fine_b = (a + REACH) / UPSCALE, (b + REACH) / UPSCALE
    i, j = np.floor(fine_a).astype(int), np.floor(fine_b).astype(int)
    u, v = fine_a - i, fine_b - j
    lower = u + v < 1.0
    spread = lambda values: np.where(lower,
        values[i, j] * (1.0 - u - v) + values[i + 1, j] * u + values[i, j + 1] * v,
        values[i + 1, j + 1] * (u + v - 1.0) + values[i + 1, j] * (1.0 - v) + values[i, j + 1] * (1.0 - u))
    # a corner of a triangle is what counts, not a place off the mesh that weighs nothing
    exact = (u == 0.0) & (v == 0.0)
    inside = np.where(exact, valid[i, j], np.where(lower, valid[i, j] & valid[i + 1, j] & valid[i, j + 1], valid[i + 1, j + 1] & valid[i + 1, j] & valid[i, j + 1]))
    # distance counts in steps down from N at an edge of the mesh, whose shape does not matter to what reads it
    distance = np.where(inside, N - np.clip(spread(edges) / STEP, 0.0, float(N)), N + 1.0)
    return np.where(inside, spread(table), 0.0), distance


def value_noise(x, y, rng_table):
    size = rng_table.shape[0]
    fx, fy = np.floor(x).astype(int), np.floor(y).astype(int)
    u, v = x - fx, y - fy
    u, v = u * u * (3.0 - 2.0 * u), v * v * (3.0 - 2.0 * v)
    t = lambda p, q: rng_table[p % size, q % size]
    return (t(fx, fy) * (1 - u) + t(fx + 1, fy) * u) * (1 - v) + (t(fx, fy + 1) * (1 - u) + t(fx + 1, fy + 1) * u) * v


def ridged(x, y, table, octaves):
    result, amplitude, total = 0.0, 1.0, 0.0
    for _ in range(octaves):
        crest = 1.0 - np.abs(value_noise(x, y, table))
        result = result + (crest * crest * 2.0 - 1.0) * amplitude
        total += amplitude
        amplitude *= 0.5
        x, y = x * 2.03 + 3.1, y * 2.03 + 1.7
    return result / total


def erode(heights, inside, keep, seed):
    """Lets droplets run downhill, which cuts gullies and leaves fans of sediment. keep is 0 where the surface must not change."""
    random.seed(seed)
    size = heights.shape[0]
    valid = np.argwhere(inside)
    values = heights.tolist()
    weights = keep.tolist()

    for _ in range(DROPLETS):
        a, b = valid[random.randrange(len(valid))]
        a, b = int(a), int(b)
        sediment = 0.0
        for _ in range(70):
            here = values[a][b]
            best, best_a, best_b = here, -1, -1
            for step_a, step_b in STEPS:
                other_a, other_b = a + step_a, b + step_b
                if 0 <= other_a < size and 0 <= other_b < size and weights[other_a][other_b] > 0.0 and values[other_a][other_b] < best:
                    best, best_a, best_b = values[other_a][other_b], other_a, other_b
            if best_a < 0:
                values[a][b] = here + sediment * 0.5 * weights[a][b]
                break
            drop = here - best
            capacity = min(drop, 2.0) * 0.5
            if sediment < capacity:
                taken = (capacity - sediment) * 0.08 * weights[a][b]
                values[a][b] = here - taken
                sediment += taken
            else:
                put = (sediment - capacity) * 0.15
                values[a][b] = here + min(put, drop * 0.5) * weights[a][b]
                sediment -= put
            a, b = best_a, best_b
    return np.array(values)


def relax_interior(heights, interior, passes, strength=0.6):
    for _ in range(passes):
        around = sum(np.roll(np.roll(heights, s[0], 0), s[1], 1) for s in STEPS) / 6.0
        heights = np.where(interior, heights + (around - heights) * strength, heights)
    return heights


def build_surface(mask, variant=0):
    mountain = g.Mountain(mask, variant)
    heights, distance = upscale(mountain)
    a, b, x, y, _ = get_positions()
    # only what has all its neighbors on the mesh changes, as off the mesh the arrays hold nothing, which would pull an uneven edge up into a comb
    inside = distance <= N
    interior = (distance < N) & inside
    for step in STEPS:
        interior &= np.roll(np.roll(inside, step[0], 0), step[1], 1)
    heights = relax_interior(heights, interior, 6)

    # detail fades out towards the sides, so neighbors still meet exactly
    keep = np.clip((N - distance) / (N * 0.14), 0.0, 1.0)
    keep = keep * keep * (3.0 - 2.0 * keep) * interior

    rng = np.random.default_rng(100 + mask + 1000 * variant)
    table = rng.uniform(-1.0, 1.0, (256, 256))

    # the line where the slopes start wanders, and what is flat stays clean, so the edge of the mesh keeps its height and color
    heights = heights + (value_noise(x / 22.0, y / 22.0, table) * 0.5 + 0.5) * FOOT_WANDER * keep
    keep = keep * np.clip((heights - g.FOOT_HEIGHT - FOOT_WANDER) / 10.0, 0.0, 1.0)
    warp = value_noise(x / 40.0, y / 40.0, table) * 0.8
    heights = heights + ridged(x / 38.0 + warp, y / 38.0 - warp, table, 5) * 10.0 * keep
    heights = erode(heights, keep > 0.5, keep, 100 + mask + 1000 * variant)
    heights = relax_interior(heights, keep > 0.0, 1, 0.35)
    heights = heights + ridged(x / 7.0, y / 7.0, table, 3) * 0.7 * keep

    # above the terrain under it, except on the very edge where the two meet
    lowest = g.FOOT_HEIGHT + np.clip((N - distance) * STEP / 10.0, 0.0, 1.0) * APRON_RISE
    heights = np.where(distance < N, np.maximum(heights, lowest), heights)
    print('MESH', g.get_mesh_name(mask, variant), 'height %.0f to %.0f' % (heights[distance <= N].min(), heights[distance <= N].max()), flush=True)

    # a little past every edge, so the bake finds a surface all the way to the edge of the light mesh
    for _ in range(EDGE_MARGIN):
        inside = distance <= N
        total = sum(np.roll(np.roll(heights * inside, s[0], 0), s[1], 1) for s in STEPS)
        count = sum(np.roll(np.roll(inside.astype(float), s[0], 0), s[1], 1) for s in STEPS)
        grow = ~inside & (count > 0.0)
        heights = np.where(grow, total / np.maximum(count, 1.0), heights)
        distance = np.where(grow, float(N), distance)
    return heights, distance, mountain


def get_colors(heights, x):
    """What the material needs to color the rock. Red is the shade: dark in steep cracks and strata, light on gentle ground. Green wanders the snow line."""
    right = np.roll(heights, -1, 0) - np.roll(heights, 1, 0)
    up_right = np.roll(heights, -1, 1) - np.roll(heights, 1, 1)
    slope_x = right / (2.0 * STEP)
    slope_y = (up_right / (2.0 * STEP) - 0.5 * slope_x) / (math.sqrt(3.0) * 0.5)
    up = 1.0 / np.sqrt(1.0 + slope_x ** 2 + slope_y ** 2)

    smooth = lambda low, high, value: (lambda t: t * t * (3.0 - 2.0 * t))(np.clip((value - low) / (high - low), 0.0, 1.0))
    strata = 0.5 + 0.5 * np.sin(heights * 0.55 + np.sin(heights * 0.13) * 3.0)
    around = sum(np.roll(np.roll(heights, s[0], 0), s[1], 1) for s in STEPS) / 6.0
    cavity = np.clip((heights - around) * 2.5 + 0.5, 0.0, 1.0)
    shade = np.clip(0.2 + 0.25 * strata * (1.0 - smooth(0.6, 0.9, up)) + 0.3 * up + 0.3 * cavity, 0.0, 1.0)
    wander = np.clip(0.5 + 0.5 * np.sin(x * 0.07 + heights * 0.09) * np.cos(heights * 0.21), 0.0, 1.0)
    return np.stack([shade, wander, np.zeros_like(heights)], -1)


def make_object(name, heights, distance, turns, world, material):
    """Y is negated, as the engine is left-handed and Blender is not, so the picture is not mirrored."""
    a, b, x, y, _ = get_positions()
    colors = get_colors(heights, x)
    index = np.full(heights.shape, -1)
    inside = distance <= N
    index[inside] = np.arange(inside.sum())

    angle = math.radians(60.0 * turns)
    placed_x = x * math.cos(angle) - y * math.sin(angle) + world[0]
    placed_y = x * math.sin(angle) + y * math.cos(angle) + world[1]
    vertices = np.stack([placed_x[inside], -placed_y[inside], heights[inside]], -1)

    first = index[:-1, :-1]
    right, top, both = index[1:, :-1], index[:-1, 1:], index[1:, 1:]
    faces = []
    keep = (first >= 0) & (right >= 0) & (top >= 0)
    faces.append(np.stack([first[keep], top[keep], right[keep]], -1))
    keep = (right >= 0) & (top >= 0) & (both >= 0)
    faces.append(np.stack([right[keep], top[keep], both[keep]], -1))
    faces = np.concatenate(faces)

    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices.tolist(), [], faces.tolist())
    mesh.update()
    if mesh.polygons[0].normal.z < 0.0:
        mesh.flip_normals()
    mesh.shade_smooth()
    attribute = mesh.color_attributes.new('Rock', 'FLOAT_COLOR', 'POINT')
    attribute.data.foreach_set('color', np.concatenate([colors[inside], np.ones((inside.sum(), 1))], 1).ravel())
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return vertices, distance[inside]


def make_rock_material():
    material = bpy.data.materials.new('Rock')
    material.use_nodes = True
    nodes, links = material.node_tree.nodes, material.node_tree.links
    shader = nodes['Principled BSDF']
    shader.inputs['Roughness'].default_value = 0.85
    color = nodes.new('ShaderNodeVertexColor')
    color.layer_name = 'Rock'
    noise = nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 0.35
    noise.inputs['Detail'].default_value = 10.0
    bump = nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = 0.5
    bump.inputs['Distance'].default_value = 1.2
    links.new(color.outputs['Color'], shader.inputs['Base Color'])
    links.new(noise.outputs['Fac'], bump.inputs['Height'])
    links.new(bump.outputs['Normal'], shader.inputs['Normal'])
    return material


def make_ground(center_x, center_y):
    bpy.ops.mesh.primitive_plane_add(size=6000.0, location=(center_x, -center_y, GROUND_HEIGHT))
    material = bpy.data.materials.new('Ground')
    material.use_nodes = True
    shader = material.node_tree.nodes['Principled BSDF']
    shader.inputs['Base Color'].default_value = (0.13, 0.15, 0.045, 1.0)
    shader.inputs['Roughness'].default_value = 0.95
    bpy.context.object.data.materials.append(material)


def setup_scene(center_x, center_y, span):
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.resolution_x, scene.render.resolution_y = 1500, 860
    scene.render.filepath = os.path.join(OUTPUT, 'Trial.png')

    camera = bpy.data.cameras.new('Camera')
    camera.lens = 45.0
    camera.clip_end = 20000.0
    camera_object = bpy.data.objects.new('Camera', camera)
    bpy.context.collection.objects.link(camera_object)
    camera_object.location = (center_x - span * 0.72, -center_y, span * 0.72)
    # north of the map stays at the top of the picture
    direction = np.array([center_x, -center_y, 30.0]) - np.array(camera_object.location)
    camera_object.rotation_mode = 'XYZ'
    from mathutils import Vector
    camera_object.rotation_euler = Vector(direction.tolist()).to_track_quat('-Z', 'Y').to_euler()
    scene.camera = camera_object

    sun = bpy.data.lights.new('Sun', 'SUN')
    sun.energy = 4.0
    sun.angle = math.radians(4.0)
    sun_object = bpy.data.objects.new('Sun', sun)
    bpy.context.collection.objects.link(sun_object)
    sun_object.rotation_euler = (math.radians(58.0), 0.0, math.radians(35.0))

    world = bpy.data.worlds.new('World')
    world.use_nodes = True
    world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.55, 0.68, 0.9, 1.0)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.6
    scene.world = world


def check_seams(placed):
    """Compares the heights of all vertices that placed hexes share on their sides."""
    seen = {}
    largest, shared = 0.0, 0
    for vertices, distance in placed:
        for x, y, z in vertices[distance == N]:
            key = (round(float(x), 1), round(float(y), 1))
            if key in seen:
                shared += 1
                largest = max(largest, abs(seen[key] - float(z)))
            else:
                seen[key] = float(z)
    print('SEAMS shared %d vertices, largest height difference %.5f' % (shared, largest), flush=True)


def main():
    os.makedirs(OUTPUT, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    hexes = set(HEXES)
    material = make_rock_material()
    surfaces = {}
    placed = []
    worlds = []

    for coord in HEXES:
        mesh_mask, turns = g.get_canonical(g.get_map_mask(hexes, coord))
        if mesh_mask not in surfaces:
            surfaces[mesh_mask] = build_surface(mesh_mask)
        world = g.hex_to_world(*coord)
        worlds.append(world)
        placed.append(make_object('Hex_%d_%d' % coord, surfaces[mesh_mask][0], surfaces[mesh_mask][1], turns, world, material))

    check_seams(placed)

    center_x = sum(world[0] for world in worlds) / len(worlds)
    center_y = sum(world[1] for world in worlds) / len(worlds)
    span = max(world[1] for world in worlds) - min(world[1] for world in worlds) + 300.0
    make_ground(center_x, center_y)
    setup_scene(center_x, center_y, span)

    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(OUTPUT, 'Trial.blend'))
    bpy.ops.render.render(write_still=True)
    print('RENDERED', flush=True)


if __name__ == '__main__':
    main()
