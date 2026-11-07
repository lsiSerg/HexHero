"""Builds the game's mountain meshes in Blender: light meshes whose detail is baked into one mask atlas and one normal atlas.

Run with: blender --background --factory-startup --python blender_build_mountains.py [-- <mesh masks to build>]
Writes GLB files and atlases to Saved/HexMountains/Game. Then run import_hex_mountains.py and assign_hex_mountains.py.
Fails if neighbors would not match in height or normal.
"""

import math
import os
import sys
import time

import bpy
import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import blender_mountain_trial as trial
import generate_hex_mountains as g

LOW_SUBDIVISIONS = 24
FACTOR = trial.N // LOW_SUBDIVISIONS
ATLAS_CELLS = 6
CELL_SIZE = 682
# a power of two, which mipmaps need, with a few unused pixels at the end
ATLAS_SIZE = 4096
BAKE_SAMPLES = 4
# a hex with its base fills this part of its atlas cell, which leaves room for the bake margin
CELL_FILL = 0.92
# the farthest that a base reaches from the center of a hex
MESH_RADIUS = g.get_mesh_radius() + 3.0
BAKE_MARGIN = 32
CAGE_EXTRUSION = 45.0
MAX_RAY_DISTANCE = 100.0
OUTPUT = os.path.join(g.OUTPUT_FOLDER, 'Game')
# extra versions with the same sides of each mesh, most of all those that make straight ranges and zigzags
EXTRA_VARIANTS = {0: 1, 1: 1, 3: 1, 5: 2, 7: 1, 9: 2, 11: 1, 13: 1, 15: 1, 21: 1, 23: 1, 27: 1, 31: 1, 63: 1}


class LowMesh:
    """What the seam check of the generator reads from a mesh."""

    def __init__(self, heights):
        self.points = []
        self.heights = []
        self.normals = []
        self.colors = []
        for point, (height, normal) in heights.items():
            self.points.append(point)
            self.heights.append(height)
            self.normals.append(normal)
            self.colors.append((0.0, 0.0, 0.0))

    def get_position(self, index):
        x, y = g.lattice_to_position(*self.points[index])
        return x, y, self.heights[index]


def make_low_object(mask, heights, mountain):
    """Samples the detailed surface. Y is negated for Blender, and the import turns it back."""
    low_step = g.HEX_RADIUS / LOW_SUBDIVISIONS
    # as far as the detailed surface reaches, which it covers on every vertex
    reach = trial.REACH // FACTOR
    contains = lambda a, b: mountain.contains(a * 2, b * 2)
    indices = {}
    points = []
    vertices = []
    for a in range(-reach, reach + 1):
        for b in range(-reach, reach + 1):
            if contains(a, b):
                indices[(a, b)] = len(points)
                points.append((a, b))
                x = (a + b * 0.5) * low_step
                y = b * math.sqrt(3.0) * 0.5 * low_step
                height = float(heights[a * FACTOR + trial.REACH, b * FACTOR + trial.REACH])
                vertices.append((x, -y, height))

    # just outside an edge that does not follow the lattice, vertices move onto it, which leaves it straight
    for a in range(-reach, reach + 1):
        for b in range(-reach, reach + 1):
            if (a, b) in indices:
                continue
            x = (a + b * 0.5) * low_step
            y = b * math.sqrt(3.0) * 0.5 * low_step
            for (start_x, start_y), (end_x, end_y) in mountain.outer_edges:
                along_x, along_y = end_x - start_x, end_y - start_y
                alpha = ((x - start_x) * along_x + (y - start_y) * along_y) / (along_x * along_x + along_y * along_y)
                on_x, on_y = start_x + along_x * alpha, start_y + along_y * alpha
                if 0.0 < alpha < 1.0 and math.hypot(x - on_x, y - on_y) < low_step * 0.7:
                    indices[(a, b)] = len(points)
                    points.append((a, b))
                    vertices.append((on_x, -on_y, g.FOOT_HEIGHT))
                    break

    # what stands at the edge after that lies at the foot
    for index, (a, b) in enumerate(points):
        if (a * 2, b * 2) not in mountain.fixed and any((a + step[0], b + step[1]) not in indices for step in g.CORNER_STEPS):
            vertices[index] = (vertices[index][0], vertices[index][1], g.FOOT_HEIGHT)

    faces = []
    # the corner a square starts from can be off the hex while its far triangle is on it
    for a in range(-reach - 1, reach + 1):
        for b in range(-reach - 1, reach + 1):
            right, top, both = (a + 1, b), (a, b + 1), (a + 1, b + 1)
            if (a, b) in indices and right in indices and top in indices:
                faces.append((indices[(a, b)], indices[right], indices[top]))
            if right in indices and top in indices and both in indices:
                faces.append((indices[right], indices[both], indices[top]))

    name = g.get_mesh_name(mask, mountain.variant)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    if mesh.polygons[0].normal.z < 0.0:
        mesh.flip_normals()
        mesh.update()
    mesh.shade_smooth()

    # the normals of the sides towards mountains come from the shared profile, so both hexes shade them the same
    normals = []
    shared = {}
    for index, (a, b) in enumerate(points):
        side_normal = mountain.normals_on_sides.get((a * 2, b * 2))
        if not side_normal and vertices[index][2] == g.FOOT_HEIGHT:
            side_normal = (0.0, 0.0, 1.0)
        if side_normal:
            normal = (side_normal[0], -side_normal[1], side_normal[2])
        else:
            normal = tuple(mesh.vertices[index].normal)
        normals.append(normal)
        if contains(a, b):
            shared[(a, b)] = (vertices[index][2], (normal[0], -normal[1], normal[2]))
    mesh.normals_split_custom_set_from_vertices(normals)

    uv_layer = mesh.uv_layers.new(name='UV')
    for loop in mesh.loops:
        x, y, _ = vertices[loop.vertex_index]
        uv_layer.data[loop.index].uv = (0.5 + x / (2.0 * MESH_RADIUS) * CELL_FILL, 0.5 + y / (2.0 * MESH_RADIUS) * CELL_FILL)

    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj, shared


def bake(low, high, image, bake_type, **options):
    material = low.data.materials[0]
    node = material.node_tree.nodes['BakeTarget']
    node.image = image
    material.node_tree.nodes.active = node
    bpy.ops.object.select_all(action='DESELECT')
    high.select_set(True)
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    bpy.ops.object.bake(type=bake_type, use_selected_to_active=True, cage_extrusion=CAGE_EXTRUSION, max_ray_distance=MAX_RAY_DISTANCE,
        margin=BAKE_MARGIN, use_clear=True, **options)
    pixels = np.empty(CELL_SIZE * CELL_SIZE * 4, dtype=np.float32)
    image.pixels.foreach_get(pixels)
    return pixels.reshape(CELL_SIZE, CELL_SIZE, 4)


def build_mesh(mask, cell, rock_material, color_atlas, normal_atlas, variant=0):
    started = time.time()
    shared = None

    heights, distance, mountain = trial.build_surface(mask, variant)
    before = set(bpy.data.objects)
    trial.make_object('High', heights, distance, 0, (0.0, 0.0), rock_material)
    high = next(obj for obj in bpy.data.objects if obj not in before)
    low, shared = make_low_object(mask, heights, mountain)

    name = low.name
    material = bpy.data.materials.new('Bake')
    material.use_nodes = True
    target = material.node_tree.nodes.new('ShaderNodeTexImage')
    target.name = 'BakeTarget'
    low.data.materials.append(material)

    color_image = bpy.data.images.new('Color', CELL_SIZE, CELL_SIZE, alpha=False)
    color_image.colorspace_settings.name = 'Non-Color'
    normal_image = bpy.data.images.new('Normal', CELL_SIZE, CELL_SIZE, alpha=False)
    normal_image.colorspace_settings.name = 'Non-Color'
    color = bake(low, high, color_image, 'DIFFUSE', pass_filter={'COLOR'})
    normal = bake(low, high, normal_image, 'NORMAL', normal_space='TANGENT', normal_g='NEG_Y')

    column, row = cell % ATLAS_CELLS, cell // ATLAS_CELLS
    top, left = row * CELL_SIZE, column * CELL_SIZE
    color_atlas[top:top + CELL_SIZE, left:left + CELL_SIZE] = color
    normal_atlas[top:top + CELL_SIZE, left:left + CELL_SIZE] = normal

    # the bake used the whole image, and the game looks up this mesh's cell of the atlas
    uv_data = low.data.uv_layers['UV'].data
    for item in uv_data:
        item.uv = ((item.uv[0] + column) * CELL_SIZE / ATLAS_SIZE, (item.uv[1] + row) * CELL_SIZE / ATLAS_SIZE)

    low.data.materials.clear()

    # glTF positions are in meters and the import turns them into centimeters
    for vertex in low.data.vertices:
        vertex.co *= 0.01

    bpy.ops.object.select_all(action='DESELECT')
    low.select_set(True)
    bpy.context.view_layer.objects.active = low
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUTPUT, name + '.glb'), export_format='GLB', use_selection=True,
        export_materials='NONE', export_normals=True, export_yup=True)

    bpy.data.objects.remove(high)
    bpy.data.objects.remove(low)
    print('BUILT', name, 'cell', cell, 'in %.0f s' % (time.time() - started), flush=True)
    return shared


def save_atlas(pixels, name, color_space):
    image = bpy.data.images.new(name, ATLAS_SIZE, ATLAS_SIZE, alpha=False)
    image.colorspace_settings.name = color_space
    image.pixels.foreach_set(pixels.ravel())
    image.filepath_raw = os.path.join(OUTPUT, name + '.png')
    image.file_format = 'PNG'
    image.save()


def check_same_sides(base, shared, mask, variant):
    """A variant must meet every neighbor just as the first version of its mesh does."""
    fixed = g.Mountain(mask).fixed
    for point, (height, normal) in base.items():
        if (point[0] * 2, point[1] * 2) in fixed:
            other_height, other_normal = shared[point]
            if abs(height - other_height) > 0.01 or max(abs(normal[axis] - other_normal[axis]) for axis in range(3)) > 0.01:
                sys.exit('Variant %d of mesh %d has other sides at %s' % (variant, mask, point))


def check_all_seams(shared_by_mask):
    """Runs the check of the generator on the light meshes. Skipped unless the whole set was built."""
    if len(shared_by_mask) != 14:
        print('SEAMS not checked, only %d meshes built' % len(shared_by_mask), flush=True)
        return

    meshes = {mask: LowMesh(shared) for mask, shared in shared_by_mask.items()}
    g.SUBDIVISIONS = LOW_SUBDIVISIONS
    largest_all = [0.0, 0.0]
    for ring in range(64):
        around = {(0, 0)} | {g.DIRECTION_OFFSETS[direction] for direction in range(6) if ring >> direction & 1}
        _, largest = g.check_seams(meshes, around)
        largest_all = [max(largest_all[axis], largest[axis]) for axis in range(2)]
    print('SEAMS all 64 neighbor sets: height %.5f, normal %.5f' % tuple(largest_all), flush=True)
    if max(largest_all) > 0.01:
        sys.exit('The meshes do not join')


def main():
    os.makedirs(OUTPUT, exist_ok=True)
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    all_masks = sorted({g.get_canonical(mask)[0] for mask in range(64)})
    wanted = [int(argument) for argument in arguments] or all_masks

    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = BAKE_SAMPLES
    scene.cycles.use_denoising = False

    rock_material = trial.make_rock_material()
    color_atlas = np.zeros((ATLAS_SIZE, ATLAS_SIZE, 4), dtype=np.float32)
    color_atlas[..., 3] = 1.0
    normal_atlas = np.full((ATLAS_SIZE, ATLAS_SIZE, 4), 0.5, dtype=np.float32)
    normal_atlas[..., 2:] = 1.0

    shared_by_mask = {}
    for mask in wanted:
        shared_by_mask[mask] = build_mesh(mask, all_masks.index(mask), rock_material, color_atlas, normal_atlas)

    cell = len(all_masks)
    for mask, count in EXTRA_VARIANTS.items():
        for variant in range(1, count + 1):
            check_same_sides(shared_by_mask[mask], build_mesh(mask, cell, rock_material, color_atlas, normal_atlas, variant), mask, variant)
            cell += 1

    check_all_seams(shared_by_mask)
    save_atlas(color_atlas, 'T_Mountain_Masks', 'Non-Color')
    save_atlas(normal_atlas, 'T_Mountain_Normal', 'Non-Color')
    print('DONE', flush=True)


if __name__ == '__main__':
    main()
