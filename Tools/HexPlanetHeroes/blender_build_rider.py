"""Builds the animated hero of the hex planet, a knight on a horse, from two CC0 models of Quaternius (rider_model_sources.json).

Run with Blender, after fetch_rider_models.ps1 downloaded the models to Saved/ModelSources/hero:
    blender --background --factory-startup --python blender_build_rider.py -- [preview]
Writes SK_PlanetRider.glb to Saved/HexPlanetHeroes, which import_planet_heroes.py imports as a skeletal mesh with its animations; with the
argument preview it also renders frames of its animations to Saved/HexPlanetHeroes/preview, for judging them without the engine.

The developer asked on 2026-11-11 to find meshes of a horse and a man, simplify them and animate them, and then for a knight in place of
the first man, who looked like a scarecrow, bound to the horse and moving with it ("людина має бути в дуеті з цим - синхронна до його
анімацій"). The horse keeps its skeleton and the animations it came with (ACTIONS), and gets one made here, Look, in which only its head
turns (LOOK_BONES, LOOK_ANGLE), for when a hero is chosen: the developer wanted no more than that. The knight keeps his skeleton too: its
bones, named Rider_<bone>, join the skeleton of the horse under the bone of its back that carries him (SEAT_BONE), his meshes are reduced
(KNIGHT_RATIO), and his own animations are dropped. In every frame of every animation of the horse he is posed here: seated (SEAT_POSE,
each limb along a direction in the world), his back kept upright against COUNTER of how far the back of the horse tips (SPINE), leaning
with how fast it rises and falls (LEAN), his head level (HEAD_COUNTER), and alive, as he sat like a mannequin: he breathes (BREATH) and
looks about (HEAD_LOOK, HEAD_NOD) in the animations long enough for it. His right hand holds the pole of his banner, which is bound to
that hand, and his left the reins, strips from the mouth of the horse to that hand, each end bound to its own bone, and that hand follows
the neck of the horse (REINS), as the developer asked ("рицар має тримати флаг-шток у руці, інша рука не видно поводій"). A saddle cloth
rides on the seat bone.

The mesh has no textures: the colors of the materials of the models go into the colors of its vertices, whose alpha is how much of the color
of the player a vertex takes (M_HexPlanetHero): the cloth of the knight (OWNER_MATERIALS), the saddle cloth and the banner.
"""

import math
import os
import sys

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
SOURCES = os.path.join(ROOT, 'Saved', 'ModelSources', 'hero')
OUTPUT = os.path.join(ROOT, 'Saved', 'HexPlanetHeroes')

# the animations of the horse that are kept, by their names in its file
ACTIONS = ['Idle', 'Idle_2', 'Walk', 'Gallop', 'Gallop_Jump', 'Jump_toIdle', 'Idle_HitReact_Left']
# the one made here from Idle: the horse turns its head to a side and back, by this many degrees over these bones
LOOK = 'Look'
LOOK_BONES = ['Neck1', 'Neck2', 'Neck3', 'Head']
LOOK_ANGLE = 44.0
# the bone of the horse that carries the knight and the cloth, the one his rein hand follows, and the one its reins start at
SEAT_BONE = 'Torso'
REIN_BONE = 'Neck1'
MOUTH_BONE = 'Head'
PREFIX = 'Rider_'
# the knight is this many times his model, and sits this far along the back from the seat bone and this deep in it
RIDER_SCALE = 2.7
SEAT_FORWARD = 0.4
SEAT_SINK = 0.25
# how much of the triangles of each of his meshes is kept
KNIGHT_RATIO = {'King_Body': 0.4, 'King_Head': 0.3, 'King_Legs': 0.45, 'King_Feet': 0.5}
# his seat: for each bone of a limb, in order from the body out, which of his sides it is posed on and where it points in the world, as
# (out to its own side, forward, up); he faces -Y as the horse does. His left hand holds the reins before him and his right the pole
SEAT_POSE = [
    ('UpperLeg', 'LR', (0.42, 0.72, -0.55)), ('LowerLeg', 'LR', (0.12, -0.2, -1.0)),
    ('UpperArm', 'L', (0.25, 0.45, -0.85)), ('LowerArm', 'L', (-0.35, 0.9, -0.05)),
    ('UpperArm', 'R', (0.45, 0.15, -0.9)), ('LowerArm', 'R', (0.25, 0.85, 0.3)),
]
# the bones of his feet hang on nothing in his model, which moves them by targets, so each is hung on its shin here
FEET = {'Foot': 'LowerLeg'}
# his hands, by the bones of his wrists: the right holds the pole and the left the reins
POLE_HAND = 'Wrist.R'
REIN_HAND = 'Wrist.L'
# the bones of his hands, whose triangles are cut away: gauntlets made with the banner stand in for them
HAND_BONES = ('Wrist', 'Index', 'Middle', 'Ring', 'Pinky', 'Thumb')
# his back: the bones that keep him upright, each taking its share of COUNTER of the tip of the back of the horse
SPINE = ['Abdomen', 'Torso', 'Chest']
COUNTER = 0.65
# his head takes this much more of it, so it stays level
HEAD_BONE = 'Neck'
HEAD_COUNTER = 0.2
# he leans by this many degrees for each unit a frame the seat rises (forward as it rises), at most LEAN_MAX
LEAN = 60.0
LEAN_MAX = 14.0
# his rein arm follows this much of the turn of the neck of the horse
REINS = 0.35
REIN_ARM = 'UpperArm.L'
# in an animation of IDLE_FRAMES or more he breathes, his chest tipping by BREATH degrees once every BREATH_FRAMES or so, and looks about,
# his head turning by HEAD_LOOK degrees to each side once and nodding by HEAD_NOD twice
IDLE_FRAMES = 28
BREATH = 2.2
BREATH_FRAMES = 40.0
BREATH_BONE = 'Chest'
HEAD_LOOK = 14.0
HEAD_NOD = 3.0
# the materials that take the color of the player
OWNER_MATERIALS = {'Blue'}
OWNER_COLOR = (0.9, 0.9, 0.9)
WOOD = (0.2, 0.12, 0.06)
LEATHER = (0.05, 0.03, 0.02)
# a glove is this wide around the line from a wrist to where its knuckles were
GLOVE = 0.15
GLOVE_SIDES = 7
GLOVE_RINGS = 4
GLOVE_COLOR = (0.13, 0.08, 0.045)
# the cloth of the banner is in this many strips, and the V of its UV is this, by which M_HexPlanetHero knows it and waves it
FLAG_STRIPS = 6
FLAG_V = 7.0


def import_model(name):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=os.path.join(SOURCES, name + '.glb'))
    added = [obj for obj in bpy.data.objects if obj not in before]
    for obj in list(added):
        if obj.type == 'MESH' and obj.name.startswith('Icosphere'):
            added.remove(obj)
            bpy.data.objects.remove(obj)
    return added


def paint(obj, owner_materials=(), same_as=None):
    """Puts the colors of the materials of a mesh into the colors of its corners, and the alpha 1 on those of the owner; a material of
    same_as takes the color of the one it names"""
    mesh = obj.data
    colors = mesh.color_attributes.new('Col', 'FLOAT_COLOR', 'CORNER')
    by_name = {material.name.split('.')[0]: material for material in mesh.materials if material}
    for polygon in mesh.polygons:
        material = mesh.materials[polygon.material_index] if mesh.materials else None
        named = material.name.split('.')[0] if material else ''
        if same_as and named in same_as and same_as[named] in by_name:
            material = by_name[same_as[named]]
        color = tuple(material.diffuse_color[:3]) if material else (0.5, 0.5, 0.5)
        owned = material is not None and material.name.split('.')[0] in owner_materials
        value = (OWNER_COLOR + (1.0,)) if owned else (color + (0.0,))
        for loop in polygon.loop_indices:
            colors.data[loop].color = value
    mesh.materials.clear()


def use_action(armature, action):
    armature.animation_data.action = action
    if action and hasattr(armature.animation_data, 'action_slot') and action.slots:
        armature.animation_data.action_slot = action.slots[0]


def to_skeleton(armature, direction):
    """A direction of the world in the space of the skeleton, which the importer turns"""
    return (armature.matrix_world.to_3x3().inverted() @ Vector(direction)).normalized()


def turn_bone(armature, name, rotation):
    """Turns a bone, and what hangs on it, around its head by a rotation in the space of the skeleton"""
    bone = armature.pose.bones[name]
    pivot = Matrix.Translation(bone.matrix.translation)
    bone.matrix = pivot @ rotation.to_matrix().to_4x4() @ pivot.inverted() @ bone.matrix
    bpy.context.view_layer.update()


def turn(armature, name, rotation):
    turn_bone(armature, PREFIX + name, rotation)


def aim(armature, name, direction):
    """Points a bone of the knight, and what hangs on it, along a direction of the world"""
    bone = armature.pose.bones[PREFIX + name]
    # along it is towards the bone that hangs on it, as the tails of imported bones point anywhere
    ends = [child for child in bone.children if child.name.startswith(PREFIX)]
    if not ends:
        return
    along = ends[0].matrix.translation - bone.matrix.translation
    turn(armature, name, along.normalized().rotation_difference(to_skeleton(armature, direction)))


def sit(armature, left):
    """Seats the knight: each limb along its direction"""
    for name, sides, (out, forward, up) in SEAT_POSE:
        for suffix, sign in (('.L', 1.0), ('.R', -1.0)):
            if suffix[1] in sides:
                aim(armature, name + suffix, (out * sign * left, -forward, up))


def get_delta(armature, name):
    """How a bone of the horse is turned from where it rests, in the space of the skeleton, and where its head is"""
    bone = armature.pose.bones[name]
    delta = bone.matrix @ armature.data.bones[name].matrix_local.inverted()
    return delta.to_quaternion(), bone.matrix.translation.copy()


def part_of(rotation, share):
    return Quaternion().slerp(rotation, share)


def key_rotation(pose_bone, frame, last=None):
    """Keys the turn of a bone in a frame. A turn has two quaternions, one the negative of the other, and from one frame to the next the
    one nearer the last is kept (last, by the name of the bone), or what plays the animation turns the bone the long way round between two
    frames: the banner shook in the hand of the knight until 2026-11-11"""
    if last is not None and pose_bone.rotation_mode == 'QUATERNION':
        turn_now = pose_bone.rotation_quaternion.copy()
        if pose_bone.name in last and last[pose_bone.name].dot(turn_now) < 0.0:
            turn_now.negate()
            pose_bone.rotation_quaternion = turn_now
        last[pose_bone.name] = turn_now
    pose_bone.keyframe_insert('rotation_quaternion' if pose_bone.rotation_mode == 'QUATERNION' else 'rotation_euler', frame=frame)


def make_look(armature):
    """The animation in which the horse only turns its head to a side and back, over its idle one"""
    look = bpy.data.actions['Idle'].copy()
    look.name = LOOK
    use_action(armature, look)
    scene = bpy.context.scene
    up = to_skeleton(armature, (0.0, 0.0, 1.0))
    bones = [armature.pose.bones[name] for name in LOOK_BONES]
    start, end = (int(round(value)) for value in look.frame_range)
    # what the idle animation gives these bones in each frame, read before any of it is changed
    rests = {}
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        rests[frame] = [bone.matrix_basis.copy() for bone in bones]
    last = {}
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        for bone, rest in zip(bones, rests[frame]):
            bone.matrix_basis = rest
        bpy.context.view_layer.update()
        share = (frame - start) / max(end - start, 1)
        # to one side, back and a little to the other, starting and ending where the idle one is
        angle = math.radians(LOOK_ANGLE) * math.sin(2.0 * math.pi * share) * (1.0 - 0.45 * share) / len(bones)
        for bone in bones:
            turn_bone(armature, bone.name, Quaternion(up, angle))
        for bone in bones:
            key_rotation(bone, frame, last)
    scene.frame_set(start + (end - start) // 4)
    tip = armature.matrix_world @ armature.pose.bones[LOOK_BONES[-1]].tail
    print('RIDER look: the head is %.2f to the side a quarter in, modes %s' % (tip.x, [bone.rotation_mode for bone in bones]))
    scene.frame_set(start)
    return look


def animate(armature, rider_bones, left):
    """Poses the knight in every frame of every animation of the horse: seated, upright, leaning with the horse, breathing and looking about"""
    scene = bpy.context.scene
    side = to_skeleton(armature, (1.0, 0.0, 0.0))
    up = to_skeleton(armature, (0.0, 0.0, 1.0))
    for pose_bone in rider_bones:
        pose_bone.rotation_mode = 'QUATERNION'
    for action in bpy.data.actions:
        use_action(armature, action)
        start, end = (int(round(value)) for value in action.frame_range)
        heights = {}
        for frame in range(start - 1, end + 2):
            scene.frame_set(max(start, min(end, frame)))
            heights[frame] = get_delta(armature, SEAT_BONE)[1].z
        alive = end - start >= IDLE_FRAMES
        last = {}
        breaths = max(round((end - start) / BREATH_FRAMES), 1)
        for frame in range(start, end + 1):
            scene.frame_set(frame)
            for pose_bone in rider_bones:
                pose_bone.matrix_basis = Matrix.Identity(4)
            bpy.context.view_layer.update()
            share = (frame - start) / max(end - start, 1)
            tip, _ = get_delta(armature, SEAT_BONE)
            neck, _ = get_delta(armature, REIN_BONE)
            # upright against the tip of the back, and leaning with how fast the seat rises
            rise = (heights[frame + 1] - heights[frame - 1]) * 0.5
            lean = Quaternion(side, math.radians(max(-LEAN_MAX, min(LEAN_MAX, -LEAN * rise))))
            upright = part_of(tip.inverted(), COUNTER)
            for name in SPINE:
                turn(armature, name, part_of(upright, 1.0 / len(SPINE)) @ part_of(lean, 1.0 / len(SPINE)))
            head = part_of(tip.inverted(), HEAD_COUNTER) @ lean.inverted()
            if alive:
                breath = Quaternion(side, math.radians(BREATH * math.sin(2.0 * math.pi * breaths * share)))
                turn(armature, BREATH_BONE, breath)
                head = Quaternion(up, math.radians(HEAD_LOOK * math.sin(2.0 * math.pi * share))) @ Quaternion(side,
                    math.radians(HEAD_NOD * math.sin(4.0 * math.pi * share))) @ breath.inverted() @ head
            turn(armature, HEAD_BONE, head)
            sit(armature, left)
            # his rein hand goes with the neck of the horse, less what the back of the horse already gave it
            turn(armature, REIN_ARM, part_of(tip.inverted() @ neck, REINS))
            for pose_bone in rider_bones:
                key_rotation(pose_bone, frame, last)
    scene.frame_set(0)


# the rider of the battles holds a lance in place of the banner, which only a hero carries, as the developer asked on 2026-11-13 (the argument
# lance writes SK_UnitRider.glb): a shaft couched forward along the horse, a little up, with a steel point
LANCE_BACK = 1.4
LANCE_FRONT = 4.6
LANCE_RISE = 0.5
STEEL = (0.62, 0.64, 0.68)
# round, as the developer found the lance of blocks too low poly on 2026-11-14: LANCE_SIDES around, a shaft that thickens at the hand and thins
# to the point (LANCE_BUTT, LANCE_GRIP, LANCE_END radii), a cone of steel before the hand that opens towards it (LANCE_GUARD wide,
# LANCE_GUARD_LENGTH long) and a leaf of steel at the point (LANCE_HEAD long, LANCE_HEAD_WIDTH wide)
LANCE_SIDES = 12
LANCE_BUTT = 0.05
LANCE_GRIP = 0.07
LANCE_END = 0.035
LANCE_GUARD = 0.26
LANCE_GUARD_LENGTH = 0.5
LANCE_HEAD = 0.7
LANCE_HEAD_WIDTH = 0.07
LEATHER_GRIP = (0.16, 0.1, 0.06)
LANCE_DROPPED = {'Gold'}
CLOTH_GAP = 0.12
LANCE_SEAT_SINK = 0.05
HORSE_PLAIN = {'Main_Light': 'Main'}


def make_extras(armature, horse, seat, half_width, back, left, lance=False):
    """The saddle cloth on the seat bone, the banner in the right hand of the knight and the reins from the mouth of the horse to his left:
    made where they are while he sits, and each vertex taken back to where its bone rests, so the bone carries it from there"""
    world = armature.matrix_world

    def to_rest(name):
        return world @ armature.data.bones[name].matrix_local @ armature.pose.bones[name].matrix.inverted() @ world.inverted()

    def hand(name):
        # where his knuckles were
        return world @ armature.pose.bones['%sMiddle1%s' % (PREFIX, name[-2:])].matrix.translation

    def wrist(name):
        return world @ armature.pose.bones[PREFIX + name].matrix.translation

    mesh = bpy.data.meshes.new('Extras')
    build = bmesh.new()
    color = build.loops.layers.float_color.new('Col')
    uv = build.loops.layers.uv.new('UVMap')
    bones = []

    def face(points, value, marks=None):
        """A face of points, each (where it is, the bone that carries it); marks are how far along the cloth of the banner each lies, which
        the material waves it by: the U of its UV, with FLAG_V as its V"""
        verts = []
        for point, bone in points:
            verts.append(build.verts.new(to_rest(bone) @ Vector(point)))
            bones.append(bone)
        made = build.faces.new(verts)
        for index, loop in enumerate(made.loops):
            loop[color] = value
            # the exporter writes 1 - V
            loop[uv].uv = (marks[index], 1.0 - FLAG_V) if marks else (0.0, 0.0)
        return made

    def lathe(start, end, profile, bone):
        """A body turned around the line from start to end: profile is (how far along it, radius, color) at each ring, smooth, its ends closed
        in a point where the radius is 0"""
        axis = (end - start).normalized()
        side = axis.cross(Vector((1.0, 0.0, 0.0)))
        side = (side if side.length > 1.0e-4 else axis.orthogonal()).normalized()
        other = axis.cross(side)
        rings = []
        for along, radius, value in profile:
            centre = start + axis * along
            rings.append(([centre + (side * math.cos(2.0 * math.pi * step / LANCE_SIDES) + other * math.sin(2.0 * math.pi * step / LANCE_SIDES)) * radius
                for step in range(LANCE_SIDES)], radius, value))
        for (lower, low_radius, value), (upper, high_radius, _) in zip(rings, rings[1:]):
            for step in range(LANCE_SIDES):
                following = (step + 1) % LANCE_SIDES
                corners = [lower[step], lower[following], upper[following], upper[step]]
                if low_radius <= 0.0:
                    corners = [lower[step], upper[following], upper[step]]
                elif high_radius <= 0.0:
                    corners = [lower[step], lower[following], upper[step]]
                made = face([(corner, bone) for corner in corners], value)
                made.smooth = True

    def glove(start, end, value, bone):
        """A closed hand: a rounded lump around the line from a wrist to where its knuckles were"""
        along = (end - start).normalized()
        side = along.orthogonal().normalized()
        other = along.cross(side)
        centre = start + (end - start) * 0.7
        rings = []
        for ring in range(GLOVE_RINGS + 1):
            polar = math.pi * ring / GLOVE_RINGS
            rings.append([centre + along * (math.cos(polar) * GLOVE * 1.3) + (side * math.cos(2.0 * math.pi * step / GLOVE_SIDES) +
                other * math.sin(2.0 * math.pi * step / GLOVE_SIDES)) * (math.sin(polar) * GLOVE) for step in range(GLOVE_SIDES)])
        for lower, upper in zip(rings, rings[1:]):
            for step in range(GLOVE_SIDES):
                following = (step + 1) % GLOVE_SIDES
                corners = [lower[step], lower[following], upper[following], upper[step]]
                # the ends of the lump close in a point
                corners = [corner for index, corner in enumerate(corners) if all((corner - before).length > 1.0e-5 for before in corners[:index])]
                if len(corners) >= 3:
                    face([(corner, bone) for corner in corners], value)

    def box(low, high, value, bone):
        corners = [Vector((x, y, z)) for x in (low[0], high[0]) for y in (low[1], high[1]) for z in (low[2], high[2])]
        for indices in ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1), (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)):
            face([(corners[index], bone) for index in indices], value)

    # the cloth: a strip over the back, hanging down both flanks, CLOTH_GAP off them, as the back of the horse came through it in its
    # animations in patches the developer saw close on the field on 2026-11-14 (0.05 and 0.04 before)
    steps = 8
    centre = back - half_width * 0.9
    rows = []
    for step in range(steps + 1):
        angle = math.radians(-78.0 + 156.0 * step / steps)
        rows.append((math.sin(angle) * (half_width + CLOTH_GAP), centre + math.cos(angle) * (back - centre + CLOTH_GAP)))
    for (x0, z0), (x1, z1) in zip(rows, rows[1:]):
        face([((x0, seat - 0.75, z0), SEAT_BONE), ((x1, seat - 0.75, z1), SEAT_BONE), ((x1, seat + 0.75, z1), SEAT_BONE), ((x0, seat + 0.75, z0), SEAT_BONE)],
            OWNER_COLOR + (1.0,))
    # his gloves: a closed hand of leather on each wrist, bound to the wrist (blocks of steel until the developer asked them away)
    for name in (POLE_HAND, REIN_HAND):
        glove(wrist(name), hand(name), GLOVE_COLOR + (0.0,), PREFIX + name)
    grip = hand(POLE_HAND)
    holder = PREFIX + POLE_HAND
    if lance:
        # the lance, one round body that rises a little towards its point, the horse facing -Y, the hand LANCE_BACK from its butt
        start = Vector((grip.x, grip.y + LANCE_BACK, grip.z - LANCE_RISE * LANCE_BACK / (LANCE_FRONT + LANCE_BACK)))
        end = Vector((grip.x, grip.y - LANCE_FRONT, grip.z + LANCE_RISE * LANCE_FRONT / (LANCE_FRONT + LANCE_BACK)))
        length = (end - start).length
        wood = WOOD + (0.0,)
        steel = STEEL + (0.0,)
        leather = LEATHER_GRIP + (0.0,)
        hold = LANCE_BACK
        lathe(start, end, [
            (0.0, 0.0, steel), (0.03, LANCE_BUTT * 0.8, steel), (0.12, LANCE_BUTT, wood),
            (hold - 0.25, LANCE_GRIP * 0.85, leather), (hold - 0.15, LANCE_GRIP, leather), (hold + 0.15, LANCE_GRIP, wood),
            # the guard: a cone of steel that opens towards the hand
            (hold + 0.2, LANCE_GRIP, steel), (hold + 0.22, LANCE_GUARD, steel), (hold + 0.26, LANCE_GUARD * 0.96, steel),
            (hold + 0.2 + LANCE_GUARD_LENGTH, LANCE_GRIP * 0.95, wood),
            (length - 0.08, LANCE_END, steel), (length, LANCE_END * 1.1, steel),
            # the head: a leaf of steel
            (length + LANCE_HEAD * 0.25, LANCE_HEAD_WIDTH, steel), (length + LANCE_HEAD, 0.0, steel)], holder)
    # the banner: its pole stands in his right hand, its foot by his stirrup, and its cloth flies back
    if not lance:
        box((grip.x - 0.05, grip.y - 0.05, grip.z - 2.1), (grip.x + 0.05, grip.y + 0.05, grip.z + 4.6), WOOD + (0.0,), holder)
    # in strips along it, so the material can wave it
    for strip in range(0 if lance else FLAG_STRIPS):
        near, far = strip / FLAG_STRIPS, (strip + 1) / FLAG_STRIPS
        face([((grip.x, grip.y + 0.05 + 1.7 * near, grip.z + 4.5 - 0.15 * near), holder), ((grip.x, grip.y + 0.05 + 1.7 * near, grip.z + 3.4 + 0.15 * near), holder),
            ((grip.x, grip.y + 0.05 + 1.7 * far, grip.z + 3.4 + 0.15 * far), holder), ((grip.x, grip.y + 0.05 + 1.7 * far, grip.z + 4.5 - 0.15 * far), holder)],
            OWNER_COLOR + (1.0,), (near, near, far, far))
    # the reins: a strip of leather from each side of the mouth of the horse to his left hand
    points = [horse.matrix_world @ vertex.co for vertex in horse.data.vertices]
    front = min(point.y for point in points)
    muzzle = [point for point in points if point.y < front + 0.35]
    mouth = Vector((0.0, front + 0.3, sum(point.z for point in muzzle) / len(muzzle)))
    reins = hand(REIN_HAND)
    holder = PREFIX + REIN_HAND
    for sign in (-1.0, 1.0):
        at_mouth = Vector((sign * 0.2, mouth.y, mouth.z))
        at_hand = Vector((reins.x + sign * 0.06, reins.y, reins.z))
        face([(at_mouth + Vector((0.0, 0.0, 0.05)), MOUTH_BONE), (at_mouth - Vector((0.0, 0.0, 0.05)), MOUTH_BONE), (at_hand - Vector((0.0, 0.0, 0.05)), holder),
            (at_hand + Vector((0.0, 0.0, 0.05)), holder)], LEATHER + (0.0,))
    build.to_mesh(mesh)
    build.free()
    extras = bpy.data.objects.new('Extras', mesh)
    bpy.context.scene.collection.objects.link(extras)
    for bone in sorted(set(bones)):
        group = extras.vertex_groups.new(name=bone)
        group.add([index for index, name in enumerate(bones) if name == bone], 1.0, 'REPLACE')
    return extras


def render_preview(armature):
    folder = os.path.join(OUTPUT, 'preview')
    os.makedirs(folder, exist_ok=True)
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_WORKBENCH'
    scene.display.shading.light = 'STUDIO'
    scene.display.shading.color_type = 'VERTEX'
    scene.display.shading.show_backface_culling = False
    scene.render.resolution_x = 360
    scene.render.resolution_y = 360
    camera = bpy.data.objects.new('Camera', bpy.data.cameras.new('Camera'))
    camera.data.type = 'ORTHO'
    camera.data.ortho_scale = 10.5
    scene.collection.objects.link(camera)
    scene.camera = camera
    views = {'side': (Vector((14.0, 0.0, 3.6)), (math.radians(90.0), 0.0, math.radians(90.0))),
        'hands': (Vector((-9.0, -9.0, 10.3)), (math.radians(62.0), 0.0, math.radians(-45.0))),
        'front': (Vector((0.0, -14.0, 3.6)), (math.radians(90.0), 0.0, 0.0)),
        'three': (Vector((-9.0, -9.0, 8.6)), (math.radians(62.0), 0.0, math.radians(-45.0)))}

    def shoot(name, view):
        camera.location, camera.rotation_euler = views[view]
        scene.render.filepath = os.path.join(folder, name + '.png')
        bpy.ops.render.render(write_still=True)

    for name in ACTIONS + [LOOK]:
        action = bpy.data.actions.get(name)
        if not action:
            continue
        use_action(armature, action)
        start, end = action.frame_range
        for index in range(4):
            scene.frame_set(int(round(start + (end - start) * index / 4.0)))
            shoot('%s_%d' % (name, index), 'front' if name == LOOK else 'side')
    use_action(armature, bpy.data.actions.get('Idle'))
    scene.frame_set(0)
    shoot('pose_front', 'front')
    shoot('pose_three', 'three')
    camera.data.ortho_scale = 3.2
    shoot('pose_hands', 'hands')


def main():
    arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    lance = 'lance' in arguments
    bpy.ops.wm.read_factory_settings(use_empty=True)
    horse_objects = import_model('horse')
    armature = next(obj for obj in horse_objects if obj.type == 'ARMATURE')
    horse = next(obj for obj in horse_objects if obj.type == 'MESH')
    # the animations of the horse that are kept, under their plain names
    for action in list(bpy.data.actions):
        plain = action.name.split('|')[-1]
        if '|' not in action.name and bpy.data.actions.get('AnimalArmature|' + plain):
            bpy.data.actions.remove(action)
    for action in list(bpy.data.actions):
        action.name = action.name.split('|')[-1]
        if action.name not in ACTIONS:
            bpy.data.actions.remove(action)
    make_look(armature)
    horse_actions = set(bpy.data.actions)
    # the dapples of the coat, small faces lighter than the rest, read as pixels on the horse of the battles, close to the camera, as the
    # developer saw on 2026-11-14, so that horse is of one tone (HORSE_PLAIN)
    paint(horse, same_as=HORSE_PLAIN if lance else None)
    # and smooth: its small flat faces lit each its own way drew a checker on its back on the field (2026-11-14); the vertices the glTF split
    # for the flat faces are joined first, so the normals blend across them
    if lance:
        build = bmesh.new()
        build.from_mesh(horse.data)
        bmesh.ops.remove_doubles(build, verts=build.verts, dist=1.0e-4)
        build.to_mesh(horse.data)
        build.free()
        for polygon in horse.data.polygons:
            polygon.use_smooth = True

    # where the knight sits: over the seat bone, on the back of the horse there; the horse faces -Y
    use_action(armature, None)
    for bone in armature.pose.bones:
        bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    bone = armature.data.bones[SEAT_BONE]
    seat_point = armature.matrix_world @ ((bone.head_local + bone.tail_local) * 0.5)
    head_point = armature.matrix_world @ armature.data.bones['Head'].head_local
    if head_point.y > seat_point.y:
        raise RuntimeError('the horse is expected to face -Y')
    seat = seat_point.y - SEAT_FORWARD
    near = [horse.matrix_world @ vertex.co for vertex in horse.data.vertices]
    near = [point for point in near if abs(point.y - seat) < 0.35]
    back = max(point.z for point in near)
    half_width = max(abs(point.x) for point in near)

    # the knight: reduced, without his own animations, his bones under names of their own
    knight_objects = import_model('knight')
    for action in list(bpy.data.actions):
        if action not in horse_actions:
            bpy.data.actions.remove(action)
    knight_armature = next(obj for obj in knight_objects if obj.type == 'ARMATURE')
    knight_armature.animation_data_clear()
    for pose_bone in knight_armature.pose.bones:
        pose_bone.matrix_basis = Matrix.Identity(4)
    parts = [obj for obj in knight_objects if obj.type == 'MESH']
    for part in parts:
        bpy.context.view_layer.objects.active = part
        # without his hands, which were full of holes and turned in his wrists: gauntlets made with the banner take their place
        names = [group.name for group in part.vertex_groups]
        build = bmesh.new()
        build.from_mesh(part.data)
        layer = build.verts.layers.deform.verify()
        def is_hand(vertex):
            weights = [(weight, names[group]) for group, weight in vertex[layer].items() if group < len(names)]
            return bool(weights) and max(weights)[1].startswith(HAND_BONES)
        bmesh.ops.delete(build, geom=[face for face in build.faces if all(is_hand(vertex) for vertex in face.verts)], context='FACES')
        # the rider of the battles is no king: the crown of the model, which the reduction of the hero tore away, goes (LANCE_DROPPED)
        if lance:
            dropped = {index for index, material in enumerate(part.data.materials) if material and material.name.split('.')[0] in LANCE_DROPPED}
            bmesh.ops.delete(build, geom=[face for face in build.faces if face.material_index in dropped], context='FACES')
        build.to_mesh(part.data)
        build.free()
        # reduced for the hero, which is seen from afar on the planet; the rider of the battles keeps every triangle, as reduced the faces of
        # his materials ran into each other in ragged patches, which the developer saw close on the field on 2026-11-14
        if not lance:
            modifier = part.modifiers.new('Decimate', 'DECIMATE')
            modifier.ratio = KNIGHT_RATIO.get(part.name.split('.')[0], 0.4)
            bpy.ops.object.modifier_move_to_index(modifier='Decimate', index=0)
            bpy.ops.object.modifier_apply(modifier='Decimate')
        paint(part, OWNER_MATERIALS)
    for bone in knight_armature.data.bones:
        bone.name = PREFIX + bone.name
    rider_names = [bone.name for bone in knight_armature.data.bones]
    roots = [bone.name for bone in knight_armature.data.bones if bone.parent is None]
    # his hips onto the back of the horse, facing where it does
    bpy.context.view_layer.update()
    hips = (knight_armature.matrix_world @ knight_armature.data.bones[PREFIX + 'Hips'].head_local).z
    # the rider of the battles sits higher, LANCE_SEAT_SINK, as the skirt of the armor, whole there, went through the back of the horse
    # and drew a ragged patchwork the developer saw on the field on 2026-11-14
    sink = LANCE_SEAT_SINK if lance else SEAT_SINK
    place = Matrix.Translation((0.0, seat, back - sink)) @ Matrix.Scale(RIDER_SCALE, 4) @ Matrix.Translation((0.0, 0.0, -hips))
    knight_armature.matrix_world = place @ knight_armature.matrix_world
    bpy.context.view_layer.update()
    worlds = {part.name: part.matrix_world.copy() for part in parts}

    # one skeleton: the bones of the knight under the seat bone of the horse
    bpy.ops.object.select_all(action='DESELECT')
    knight_armature.select_set(True)
    armature.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.object.join()
    bpy.ops.object.mode_set(mode='EDIT')
    for name in roots:
        edit_bone = armature.data.edit_bones[name]
        edit_bone.use_connect = False
        edit_bone.parent = armature.data.edit_bones[SEAT_BONE]
    for name, parent in FEET.items():
        for suffix in ('.L', '.R'):
            edit_bone = armature.data.edit_bones[PREFIX + name + suffix]
            edit_bone.use_connect = False
            edit_bone.parent = armature.data.edit_bones[PREFIX + parent + suffix]
    bpy.ops.object.mode_set(mode='OBJECT')
    for part in parts:
        part.parent = armature
        part.matrix_world = worlds[part.name]
        for modifier in part.modifiers:
            if modifier.type == 'ARMATURE':
                modifier.object = armature
    bpy.context.view_layer.update()
    rider_bones = [armature.pose.bones[name] for name in rider_names]
    # which way his left is
    left = 1.0 if (armature.matrix_world @ armature.data.bones[PREFIX + 'UpperArm.L'].head_local).x > 0.0 else -1.0

    # the cloth, the banner and the reins, made around him as he sits on the horse at rest
    sit(armature, left)
    name = 'SK_UnitRider' if lance else 'SK_PlanetRider'
    extras = make_extras(armature, horse, seat, half_width, back, left, lance)
    for pose_bone in rider_bones:
        pose_bone.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()

    # one mesh, with the UV of the banner alone: the models have no textures
    for part in parts + [horse]:
        while part.data.uv_layers:
            part.data.uv_layers.remove(part.data.uv_layers[0])
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts + [extras]:
        part.select_set(True)
    horse.select_set(True)
    bpy.context.view_layer.objects.active = horse
    bpy.ops.object.join()
    horse.name = name
    horse.data.name = name
    material = bpy.data.materials.new('Rider')
    horse.data.materials.clear()
    horse.data.materials.append(material)
    for polygon in horse.data.polygons:
        polygon.material_index = 0
    colors = horse.data.color_attributes['Col']
    horse.data.color_attributes.active_color = colors
    horse.data.color_attributes.render_color_index = list(horse.data.color_attributes).index(colors)
    horse.data.calc_loop_triangles()

    animate(armature, rider_bones, left)
    print('RIDER %d triangles, %d vertices, %d bones, actions %s' % (len(horse.data.loop_triangles), len(horse.data.vertices), len(armature.data.bones),
        sorted(action.name for action in bpy.data.actions)))

    if 'preview' in arguments:
        render_preview(armature)
    use_action(armature, None)
    os.makedirs(OUTPUT, exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    horse.select_set(True)
    armature.select_set(True)
    bpy.ops.export_scene.gltf(filepath=os.path.join(OUTPUT, name + '.glb'), export_format='GLB', use_selection=True, export_animations=True,
        export_animation_mode='ACTIONS', export_vertex_color='ACTIVE', export_all_vertex_colors=False, export_materials='EXPORT', export_yup=True,
        export_skins=True, export_def_bones=False, export_force_sampling=True)
    print('RIDER DONE')


main()
