"""Models the catapult of a siege, as the developer asked on 2026-11-13 for one modelled and textured in place of basic shapes: a frame of
beams on four wheels with iron tyres and axles, an A-frame with a padded stop beam, a twisted bundle of rope across the frame, and its throwing
arm with a bucket and a stone apart, so the field swings it when it fires. It throws towards +X; at rest the arm lies back, its bucket behind
the frame. Every face is UV mapped in centimetres along the grain of its beam (U along the length), for the planks of M_HexBattleProp, and
each part has one of three materials, Wood, Iron and Rope, which the field gives textures. Run with Blender:

  blender --background --factory-startup --python blender_build_catapult.py [-- preview]

It writes Saved/BattleCatapult/SM_BattleCatapult.glb (origin on the ground under the middle of the frame), SM_BattleCatapultArm.glb (origin on
its pivot) and catapult.json, where the pivot lies in the frame, in centimetres; `preview` renders both to preview.png.
"""
import json
import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Vector

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'Saved', 'BattleCatapult')
# where the arm turns, in metres from the middle of the frame on the ground, and how far it lies back at rest, in degrees above level
PIVOT = Vector((-0.25, 0.0, 0.78))
REST_LIFT = 14.0
ARM_LENGTH = 2.3


def material(name, color):
    made = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    made.diffuse_color = color + (1.0,)
    return made


class Builder:
    """Boxes and cylinders into one mesh, each face UV mapped in centimetres: U along the longest side of a box or the axis of a cylinder"""

    def __init__(self, name):
        self.bm = bmesh.new()
        self.uv = self.bm.loops.layers.uv.new('UVMap')
        self.name = name
        self.slots = []

    def slot(self, name):
        if name not in self.slots:
            self.slots.append(name)
        return self.slots.index(name)

    def box(self, start, end, width, height, slot, up=Vector((0.0, 0.0, 1.0)), jitter=0.0):
        """A beam from start to end, width across and height up"""
        start, end = Vector(start), Vector(end)
        axis = end - start
        length = axis.length
        along = axis.normalized()
        side = along.cross(up)
        if side.length < 1e-4:
            side = along.cross(Vector((1.0, 0.0, 0.0)))
        side.normalize()
        top = side.cross(along).normalized()
        made = bmesh.ops.create_cube(self.bm, size=1.0)
        verts = set(made['verts'])
        basis = Matrix((along, side, top)).transposed()
        for vert in verts:
            local = Vector((vert.co.x * length, vert.co.y * width, vert.co.z * height))
            vert.co = start + along * (length * 0.5) + basis @ local
        index = self.slot(slot)
        for face in {f for v in verts for f in v.link_faces}:
            face.material_index = index
            face.smooth = False
            normal = face.normal
            for loop in face.loops:
                point = loop.vert.co - start
                u = point.dot(along) * 100.0
                v = (point.dot(side) if abs(normal.dot(top)) > 0.7 else point.dot(top)) * 100.0
                if abs(normal.dot(along)) > 0.7:
                    u = point.dot(side) * 100.0
                    v = point.dot(top) * 100.0
                loop[self.uv].uv = (u, v)

    def cylinder(self, start, end, radius, slot, segments=12, end_radius=None, smooth=True, around=False):
        """A cylinder from start to end; around lays U around it and V along it (a tyre, a rope), else U along it"""
        start, end = Vector(start), Vector(end)
        axis = end - start
        made = bmesh.ops.create_cone(self.bm, cap_ends=True, segments=segments, radius1=radius, radius2=radius if end_radius is None else end_radius,
            depth=axis.length)
        turn = axis.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()
        middle = (start + end) * 0.5
        for vert in made['verts']:
            vert.co = turn @ vert.co + middle
        index = self.slot(slot)
        along = axis.normalized()
        for face in {f for v in made['verts'] for f in v.link_faces}:
            face.material_index = index
            face.smooth = smooth and abs(face.normal.dot(along)) < 0.7
            for loop in face.loops:
                point = loop.vert.co - start
                along_cm = point.dot(along) * 100.0
                flat = point - along * point.dot(along)
                angle = math.atan2(flat.dot(turn.col[1].to_3d()), flat.dot(turn.col[0].to_3d()))
                around_cm = angle * radius * 100.0
                loop[self.uv].uv = (around_cm, along_cm) if around else (along_cm, around_cm)

    def ring(self, middle, axis, radius, thickness, width, slot, segments=24):
        """A tyre: a band around an axis"""
        axis = Vector(axis).normalized()
        a = axis.orthogonal().normalized()
        b = axis.cross(a)
        index = self.slot(slot)
        outer = []
        inner = []
        for step in range(segments):
            angle = 2.0 * math.pi * step / segments
            out = a * math.cos(angle) + b * math.sin(angle)
            for r, row in ((radius, outer), (radius - thickness, inner)):
                row.append((self.bm.verts.new(Vector(middle) + out * r - axis * width * 0.5), self.bm.verts.new(Vector(middle) + out * r + axis * width * 0.5)))
        for step in range(segments):
            nxt = (step + 1) % segments
            for quad in ((outer[step][0], outer[nxt][0], outer[nxt][1], outer[step][1]), (inner[step][1], inner[nxt][1], inner[nxt][0], inner[step][0]),
                         (outer[step][1], outer[nxt][1], inner[nxt][1], inner[step][1]), (inner[step][0], inner[nxt][0], outer[nxt][0], outer[step][0])):
                face = self.bm.faces.new(quad)
                face.material_index = index
                face.smooth = True
                for k, loop in enumerate(face.loops):
                    loop[self.uv].uv = ((step + (1 if k in (1, 2) else 0)) * 2.0 * math.pi * radius * 100.0 / segments, 0.0 if k in (0, 1) else width * 100.0)

    def finish(self, materials):
        mesh = bpy.data.meshes.new(self.name)
        bmesh.ops.recalc_face_normals(self.bm, faces=self.bm.faces)
        self.bm.to_mesh(mesh)
        self.bm.free()
        for name in self.slots:
            mesh.materials.append(materials[name])
        obj = bpy.data.objects.new(self.name, mesh)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def wheel(builder, middle, axis_y):
    """A wheel of planks: a disc of wood, spokes, an iron tyre and a hub"""
    radius = 0.42
    width = 0.12
    builder.cylinder((middle[0], middle[1] - width * 0.45 * axis_y, middle[2]), (middle[0], middle[1] + width * 0.45 * axis_y, middle[2]), radius * 0.93, 'Wood', 20)
    builder.ring(middle, (0.0, 1.0, 0.0), radius, 0.05, width, 'Iron', 24)
    builder.cylinder((middle[0], middle[1] - width * 0.8 * axis_y, middle[2]), (middle[0], middle[1] + width * 0.8 * axis_y, middle[2]), 0.09, 'Iron', 10)
    for spoke in range(6):
        angle = math.pi * spoke / 6.0
        offset = Vector((math.cos(angle), 0.0, math.sin(angle))) * radius * 0.85
        out = middle[1] + width * 0.52 * axis_y
        builder.box((middle[0] - offset.x, out, middle[2] - offset.z), (middle[0] + offset.x, out, middle[2] + offset.z), 0.05, 0.03, 'Wood',
            up=Vector((0.0, 1.0, 0.0)))


def build_frame(materials):
    b = Builder('SM_BattleCatapult')
    axle_z = 0.42
    rail_z = 0.62
    for y in (-0.55, 0.55):
        # the side rails, the uprights of the A-frame and their braces
        b.box((-1.45, y, rail_z), (1.45, y, rail_z), 0.2, 0.22, 'Wood')
        b.box((0.55, y, rail_z + 0.1), (0.25, y, 1.85), 0.16, 0.16, 'Wood')
        b.box((-0.35, y, rail_z + 0.1), (0.3, y, 1.8), 0.13, 0.13, 'Wood')
        b.box((1.2, y, rail_z + 0.1), (0.38, y, 1.45), 0.12, 0.12, 'Wood')
        # iron bands where the uprights meet the rail
        b.box((0.42, y - 0.11 * (1 if y > 0 else -1) * 0.0, rail_z + 0.18), (0.62, y, rail_z + 0.18), 0.22, 0.05, 'Iron')
    for x in (-1.3, -0.25, 0.65, 1.3):
        b.box((x, -0.7, rail_z), (x, 0.7, rail_z), 0.17, 0.17, 'Wood')
    # the stop beam the arm strikes, padded with rope
    b.box((0.3, -0.72, 1.82), (0.3, 0.72, 1.82), 0.2, 0.2, 'Wood')
    b.cylinder((0.3, -0.32, 1.82), (0.3, 0.32, 1.82), 0.16, 'Rope', 14, around=True)
    # the twisted bundle of rope the arm stands in, and its winches
    b.cylinder((PIVOT.x, -0.48, PIVOT.z), (PIVOT.x, 0.48, PIVOT.z), 0.13, 'Rope', 16, around=True)
    for y in (-0.66, 0.66):
        b.cylinder((PIVOT.x, y - 0.06, PIVOT.z), (PIVOT.x, y + 0.06, PIVOT.z), 0.2, 'Iron', 16)
        for spoke in range(4):
            angle = math.pi * spoke / 4.0
            d = Vector((math.cos(angle), 0.0, math.sin(angle))) * 0.3
            b.box((PIVOT.x - d.x, y + (0.1 if y > 0 else -0.1), PIVOT.z - d.z), (PIVOT.x + d.x, y + (0.1 if y > 0 else -0.1), PIVOT.z + d.z), 0.05, 0.05,
                'Wood', up=Vector((0.0, 1.0, 0.0)))
    # the axles and the wheels
    for x in (-1.0, 1.0):
        b.cylinder((x, -0.85, axle_z), (x, 0.85, axle_z), 0.055, 'Iron', 10)
        b.box((x, -0.55, axle_z + 0.08), (x, -0.55, rail_z - 0.05), 0.14, 0.14, 'Wood', up=Vector((1.0, 0.0, 0.0)))
        b.box((x, 0.55, axle_z + 0.08), (x, 0.55, rail_z - 0.05), 0.14, 0.14, 'Wood', up=Vector((1.0, 0.0, 0.0)))
        for y in (-0.76, 0.76):
            wheel(b, (x, y, axle_z), 1.0 if y > 0 else -1.0)
    return b.finish(materials)


def build_arm(materials):
    """The arm lying along -X from its pivot at the origin, lifted REST_LIFT degrees, with its bucket and stone at its end"""
    b = Builder('SM_BattleCatapultArm')
    lift = math.radians(REST_LIFT)
    out = Vector((-math.cos(lift), 0.0, math.sin(lift)))
    tip = out * ARM_LENGTH
    b.box(out * -0.25, tip, 0.15, 0.17, 'Wood', up=Vector((0.0, 0.0, 1.0)))
    for share in (0.12, 0.55, 0.92):
        b.box(out * (ARM_LENGTH * share - 0.04), out * (ARM_LENGTH * share + 0.04), 0.19, 0.21, 'Iron')
    # the bucket: a shallow box of planks with a rim, open on its top
    up = Vector((0.0, 0.0, 1.0))
    centre = tip + up * 0.1
    b.box(centre - out * 0.25 - up * 0.08, centre + out * 0.25 - up * 0.08, 0.5, 0.05, 'Wood')
    for side in (-1.0, 1.0):
        b.box(centre - out * 0.25 + Vector((0.0, side * 0.24, 0.0)), centre + out * 0.25 + Vector((0.0, side * 0.24, 0.0)), 0.04, 0.2, 'Wood')
        b.box(centre + out * side * 0.25 - Vector((0.0, 0.24, 0.0)), centre + out * side * 0.25 + Vector((0.0, 0.24, 0.0)), 0.04, 0.2, 'Wood')
    b.cylinder(centre - up * 0.1, centre + up * 0.02, 0.2, 'Iron', 12)
    arm = b.finish(materials)
    # the stone, a rough ball
    bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=2, radius=0.2, location=centre + up * 0.16)
    stone = bpy.context.object
    for vert in stone.data.vertices:
        vert.co *= 0.85 + 0.3 * (math.sin(vert.co.x * 31.0) * math.cos(vert.co.y * 23.0 + vert.co.z * 17.0) * 0.5 + 0.5)
    stone.data.materials.append(materials['Iron'])
    bpy.ops.object.select_all(action='DESELECT')
    stone.select_set(True)
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.join()
    return arm


def export(obj, name):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUT, name + '.glb'), export_format='GLB', use_selection=True, export_materials='EXPORT',
        export_texcoords=True, export_normals=True, export_yup=True)


def triangles(obj):
    return sum(len(p.vertices) - 2 for p in obj.data.polygons)


def main():
    args = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    bpy.ops.wm.read_factory_settings(use_empty=True)
    os.makedirs(OUT, exist_ok=True)
    materials = {'Wood': material('Wood', (0.4, 0.27, 0.15)), 'Iron': material('Iron', (0.22, 0.22, 0.23)), 'Rope': material('Rope', (0.55, 0.45, 0.3))}
    frame = build_frame(materials)
    arm = build_arm(materials)
    export(frame, 'SM_BattleCatapult')
    export(arm, 'SM_BattleCatapultArm')
    info = {'pivot': [PIVOT.x * 100.0, PIVOT.y * 100.0, PIVOT.z * 100.0], 'frame_triangles': triangles(frame), 'arm_triangles': triangles(arm),
        'slots': ['Wood', 'Iron', 'Rope']}
    with open(os.path.join(OUT, 'catapult.json'), 'w', encoding='utf-8') as file:
        json.dump(info, file, indent=1)
    print('CATAPULT frame %d, arm %d triangles' % (info['frame_triangles'], info['arm_triangles']))
    if 'preview' in args:
        arm.location = PIVOT
        scene = bpy.context.scene
        scene.render.engine = 'BLENDER_EEVEE'
        scene.render.resolution_x = 1200
        scene.render.resolution_y = 800
        world = bpy.data.worlds.new('World')
        world.color = (0.5, 0.55, 0.65)
        scene.world = world
        bpy.ops.object.light_add(type='SUN', rotation=(math.radians(45), 0.0, math.radians(35)))
        bpy.context.object.data.energy = 4.0
        bpy.ops.object.camera_add(location=(4.5, -5.5, 3.6), rotation=(math.radians(65), 0.0, math.radians(40)))
        scene.camera = bpy.context.object
        scene.render.filepath = os.path.join(OUT, 'preview.png')
        bpy.ops.render.render(write_still=True)


main()
