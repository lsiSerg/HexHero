"""Draws the cursors of the game and the icons of the stats of the creatures as PNG files to Saved/HexUi, which import_ui_icons.py imports to
/Game/HexPlanet/UI. Run it with the Python of Blender, for its numpy:

    "<Blender>/5.2/python/bin/python.exe" Tools/HexUi/generate_ui_icons.py

They are placeholders an artist can replace in DA_HexUi, each a picture of its own. Each is animated, as the developer asked on 2026-11-12: a
strip of FRAMES frames side by side, which the game plays at the rate of ICONS (the horse of moving gallops, flags wave, swords clash, the
rest turn, beat or shine). A cursor is drawn at twice the size it shows at, so its first mip is that size; its hot spot is given in ICONS,
where the click lands: the tip of the arrow of the default one, the middle of the rest. The cursor of a strike is a sword that points right,
which the game turns to point from the hex it strikes from to the one it strikes. Words after the file name draw only those pictures."""

import json
import os
import struct
import sys
import zlib

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OUT = os.path.join(ROOT, 'Saved', 'HexUi')
SS = 4
FRAMES = 8

INK = (0.08, 0.06, 0.04)


def write_png(path, rgba):
    height, width, _ = rgba.shape
    data = (np.clip(rgba, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    raw = b''.join(b'\x00' + data[row].tobytes() for row in range(height))

    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
    png += chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')
    with open(path, 'wb') as file:
        file.write(png)


class Canvas:
    """Shapes as signed distances in pixels of a canvas of 64 units a side, drawn at Size pixels with SS samples a pixel each way"""

    def __init__(self, size, t=0.0):
        self.size = size
        self.t = t
        n = size * SS
        coords = (np.arange(n) + 0.5) / SS * (64.0 / size)
        self.bx, self.by = np.meshgrid(coords, coords)
        self.x, self.y = self.bx, self.by
        self.rgba = np.zeros((n, n, 4))
        self.scale = size / 64.0

    def pose(self, angle=0.0, scale=1.0, dx=0.0, dy=0.0, cx=32.0, cy=32.0):
        """What is drawn next shows turned by angle about (cx, cy), scaled and moved"""
        ca, sa = np.cos(-angle), np.sin(-angle)
        px, py = (self.bx - cx - dx) / scale, (self.by - cy - dy) / scale
        self.x, self.y = cx + px * ca - py * sa, cy + px * sa + py * ca

    def glint(self, width=7.0, strength=0.45):
        """A band of light that sweeps over what is drawn from the top left to the bottom right in the first half of the cycle"""
        along = (self.bx + self.by) * 0.5
        position = -12.0 + (self.t * 2.0) * 88.0
        band = np.exp(-((along - position) / width) ** 2) * strength * (self.rgba[..., 3] > 0.0)
        for channel in range(3):
            self.rgba[..., channel] = np.clip(self.rgba[..., channel] + band * (1.0 - self.rgba[..., channel]), 0.0, 1.0)

    def fill(self, distance, color, shade=0.22, alpha=1.0):
        mask = distance < 0.0
        # lighter at the top left, as if lit from there
        light = 1.0 + shade * (0.5 - (self.x + self.y) / 128.0)
        for channel in range(3):
            self.rgba[..., channel] = np.where(mask, np.clip(color[channel] * light, 0.0, 1.0) * alpha + self.rgba[..., channel] * (1.0 - alpha), self.rgba[..., channel])
        self.rgba[..., 3] = np.where(mask, alpha + self.rgba[..., 3] * (1.0 - alpha), self.rgba[..., 3])

    def outline(self, distance, width=3.2, color=INK):
        self.fill(distance - width, color, shade=0.0)

    def image(self):
        n = self.size
        blocks = self.rgba.reshape(n, SS, n, SS, 4)
        alpha = blocks[..., 3].mean(axis=(1, 3))
        color = (blocks[..., :3] * blocks[..., 3:4]).sum(axis=(1, 3)) / np.maximum(blocks[..., 3].sum(axis=(1, 3)), 1e-6)[..., None]
        return np.concatenate([color, alpha[..., None]], axis=2)


def circle(c, cx, cy, r):
    return np.hypot(c.x - cx, c.y - cy) - r


def ellipse(c, cx, cy, rx, ry, angle=0.0):
    ca, sa = np.cos(angle), np.sin(angle)
    dx, dy = c.x - cx, c.y - cy
    u, v = dx * ca + dy * sa, -dx * sa + dy * ca
    k = np.hypot(u / rx, v / ry)
    return (k - 1.0) * min(rx, ry)


def capsule(c, ax, ay, bx, by, r):
    px, py = c.x - ax, c.y - ay
    dx, dy = bx - ax, by - ay
    t = np.clip((px * dx + py * dy) / max(dx * dx + dy * dy, 1e-9), 0.0, 1.0)
    return np.hypot(px - dx * t, py - dy * t) - r


def polygon(c, points):
    """The exact signed distance to a polygon: the nearest of its sides, negative inside by the even-odd rule"""
    distance = np.full(c.x.shape, 1e9)
    inside = np.zeros(c.x.shape, dtype=bool)
    count = len(points)
    for index in range(count):
        ax, ay = points[index]
        bx, by = points[(index + 1) % count]
        distance = np.minimum(distance, capsule(c, ax, ay, bx, by, 0.0))
        crosses = ((ay > c.y) != (by > c.y)) & (c.x < (bx - ax) * (c.y - ay) / (by - ay + 1e-12) + ax)
        inside ^= crosses
    return np.where(inside, -distance, distance)


def union(*parts):
    result = parts[0]
    for part in parts[1:]:
        result = np.minimum(result, part)
    return result


def cut(a, b):
    return np.maximum(a, -b)


def turned(points, angle, cx=32.0, cy=32.0):
    ca, sa = np.cos(angle), np.sin(angle)
    return [(cx + (x - cx) * ca - (y - cy) * sa, cy + (x - cx) * sa + (y - cy) * ca) for x, y in points]


# the shapes, each drawing on a canvas: every outline first, then the fills over them, so touching parts share one outline

def arrow(c):
    return polygon(c, [(5, 4), (5, 50), (16, 40), (24, 58), (32, 54), (24, 37), (40, 37)])


def draw_default(c):
    shape = arrow(c)
    c.outline(shape)
    c.fill(shape, (0.97, 0.92, 0.78))
    c.glint()


def draw_choose(c):
    shape = arrow(c)
    ring = np.abs(circle(c, 44, 46, 10 + 1.5 * np.sin(2.0 * np.pi * c.t))) - 3.0
    c.outline(union(shape, ring))
    c.fill(shape, (0.97, 0.92, 0.78))
    c.fill(ring, (1.0, 0.78, 0.25))


def horse(c):
    """A horse seen from the side at the gallop, its legs by the phase of the cycle: the body, the neck and head, the mane, the tail and four
    legs of two parts each, as the developer asked on 2026-11-12 for a horse whose legs move in place of the footprints, which seemed to
    point a way"""
    phase = 2.0 * np.pi * c.t
    bob = np.sin(phase * 2.0) * 1.2
    body = ellipse(c, 30, 32 + bob, 15.5, 8.0, -0.05)
    neck = polygon(c, [(39, 30 + bob), (47, 14 + bob), (53, 17 + bob), (46, 33 + bob)])
    head = ellipse(c, 53, 17 + bob, 7.5, 4.2, 0.55)
    ear = polygon(c, [(48, 12 + bob), (50, 6 + bob), (52, 12 + bob)])
    tail = capsule(c, 15, 28 + bob, 6, 34 + bob + np.sin(phase) * 3.0, 2.6)
    legs = []
    for hip_x, base, offset in [(40, 0.25, 0.0), (37, 0.25, 0.9), (21, -0.2, 3.1), (18, -0.2, 4.0)]:
        swing = base + 0.6 * np.sin(phase + offset)
        bend = 0.35 + 0.45 * (0.5 + 0.5 * np.sin(phase + offset + 1.3))
        kx, ky = hip_x + np.sin(swing) * 9.5, 36 + bob + np.cos(swing) * 9.5
        fx, fy = kx + np.sin(swing - bend * np.sign(base + 0.01)) * 9.5, ky + np.cos(swing - bend * np.sign(base + 0.01)) * 9.5
        legs.append((capsule(c, hip_x, 35 + bob, kx, ky, 3.3), capsule(c, kx, ky, fx, fy, 2.5), circle(c, fx, fy, 2.8)))
    return body, union(neck, head, ear), tail, legs


def draw_move(c, color=(0.62, 0.4, 0.22)):
    body, head, tail, legs = horse(c)
    every = union(body, head, tail, *[part for leg in legs for part in leg])
    c.outline(every)
    # the legs further away darker, under the body
    for index, (upper, lower, hoof) in enumerate(legs):
        tone = 0.75 if index % 2 == 1 else 1.0
        c.fill(union(upper, lower), tuple(channel * tone for channel in color))
        c.fill(hoof, (0.12, 0.08, 0.05), shade=0.0)
    c.fill(tail, (0.18, 0.1, 0.06))
    c.fill(body, color)
    c.fill(head, color)
    # the mane along the neck
    c.fill(capsule(c, 41, 27 + np.sin(4.0 * np.pi * c.t) * 1.2, 48, 13 + np.sin(4.0 * np.pi * c.t) * 1.2, 2.0), (0.18, 0.1, 0.06))


def draw_move_later(c):
    draw_move(c, (0.95, 0.6, 0.25))


def draw_blocked(c):
    c.pose(scale=1.0 + 0.06 * np.sin(2.0 * np.pi * c.t))
    ring = np.abs(circle(c, 32, 32, 21)) - 4.5
    slash = capsule(c, 18, 46, 46, 18, 4.5)
    shape = union(ring, np.maximum(slash, circle(c, 32, 32, 21)))
    c.outline(shape)
    c.fill(shape, (0.92, 0.18, 0.12))


def sword(c, ax, ay, bx, by, blade=4.2, guard=10.0):
    """A sword from its pommel A to its tip B: the blade, a guard across it a fifth of the way up and the grip"""
    length = np.hypot(bx - ax, by - ay)
    ux, uy = (bx - ax) / length, (by - ay) / length
    gx, gy = ax + ux * length * 0.24, ay + uy * length * 0.24
    tip = polygon(c, [(gx + uy * blade, gy - ux * blade), (bx - ux * blade * 1.6 + uy * blade, by - uy * blade * 1.6 - ux * blade),
                      (bx, by), (bx - ux * blade * 1.6 - uy * blade, by - uy * blade * 1.6 + ux * blade), (gx - uy * blade, gy + ux * blade)])
    cross = capsule(c, gx + uy * guard, gy - ux * guard, gx - uy * guard, gy + ux * guard, 2.6)
    grip = capsule(c, ax + ux * 2.5, ay + uy * 2.5, gx, gy, 2.4)
    pommel = circle(c, ax + ux * 2.0, ay + uy * 2.0, 3.6)
    return tip, union(cross, grip, pommel)


def draw_attack(c):
    # the blades clash: they swing apart and together
    swing = 0.12 * np.sin(2.0 * np.pi * c.t)
    c.pose(angle=swing)
    blade_a, hilt_a = sword(c, 12, 54, 54, 10)
    c.pose(angle=-swing)
    blade_b, hilt_b = sword(c, 52, 54, 10, 10)
    c.pose()
    c.outline(union(blade_a, hilt_a, blade_b, hilt_b))
    c.fill(blade_a, (0.85, 0.88, 0.92))
    c.fill(hilt_a, (0.75, 0.2, 0.12))
    c.fill(blade_b, (0.85, 0.88, 0.92))
    c.fill(hilt_b, (0.75, 0.2, 0.12))


def draw_castle(c):
    wall = polygon(c, [(12, 56), (12, 22), (18, 22), (18, 16), (25, 16), (25, 22), (29, 22), (29, 16), (35, 16), (35, 22), (39, 22), (39, 16),
                       (46, 16), (46, 22), (52, 22), (52, 56)])
    gate = union(capsule(c, 32, 44, 32, 58, 6.0))
    shape = cut(wall, gate)
    wave = np.sin(2.0 * np.pi * c.t)
    flag = polygon(c, [(31, 16), (31, 4), (38, 5 + wave), (45, 7 - wave), (39, 9 + wave), (33, 11), (33, 16)])
    c.outline(union(shape, flag))
    c.fill(shape, (0.93, 0.78, 0.45))
    c.fill(flag, (0.95, 0.25, 0.18))


def draw_visit(c):
    gem = polygon(c, [(20, 22), (32, 10), (44, 22), (32, 56)])
    c.outline(gem)
    c.fill(gem, (0.35, 0.9, 1.0))
    c.fill(polygon(c, [(20, 22), (44, 22), (32, 56)]), (0.2, 0.65, 0.9))
    for index, (x, y) in enumerate([(50, 12), (12, 40), (52, 44)]):
        # each twinkles at its own time
        size = 2.0 + 4.0 * (0.5 + 0.5 * np.sin(2.0 * np.pi * (c.t + index / 3.0)))
        spark = union(capsule(c, x - size, y, x + size, y, 1.1), capsule(c, x, y - size, x, y + size, 1.1))
        c.fill(spark - 1.2, INK, shade=0.0)
        c.fill(spark, (1.0, 0.95, 0.6))


def draw_portal(c):
    ring = np.abs(circle(c, 32, 32, 22)) - 4.0
    arms = []
    for index in range(3):
        # the swirl turns a third of the way round in a cycle, so it loops
        angle = index * 2.0 * np.pi / 3.0 + c.t * 2.0 * np.pi / 3.0
        points = []
        for step in range(9):
            t = step / 8.0
            r = 4.0 + 15.0 * t
            a = angle + t * 2.4
            points.append((32 + r * np.cos(a), 32 + r * np.sin(a)))
        arm = union(*[capsule(c, points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], 2.4 * (1.0 - i / 10.0)) for i in range(8)])
        arms.append(arm)
    swirl = union(*arms)
    c.outline(union(ring, swirl))
    c.fill(ring, (0.55, 0.45, 1.0))
    c.fill(swirl, (0.45, 1.0, 0.95))


def curved_arrow(c, start, end, radius, flip):
    points = []
    for step in range(13):
        a = start + (end - start) * step / 12.0
        points.append((32 + radius * np.cos(a), 32 + radius * np.sin(a)))
    body = union(*[capsule(c, points[i][0], points[i][1], points[i + 1][0], points[i + 1][1], 3.2) for i in range(12)])
    tx, ty = -np.sin(end) * flip, np.cos(end) * flip
    ex, ey = points[-1]
    head = polygon(c, [(ex + tx * 9, ey + ty * 9), (ex - np.cos(end) * 8, ey - np.sin(end) * 8), (ex + np.cos(end) * 8, ey + np.sin(end) * 8)])
    return union(body, head)


def draw_swap(c):
    c.pose(angle=c.t * np.pi)
    a = curved_arrow(c, np.pi * 1.05, np.pi * 1.85, 19, 1.0)
    b = curved_arrow(c, np.pi * 0.05, np.pi * 0.85, 19, 1.0)
    c.outline(union(a, b))
    c.fill(a, (0.45, 0.75, 1.0))
    c.fill(b, (0.45, 0.75, 1.0))


def draw_fly(c):
    # long feathers fanning down from the shoulder at the top right, each drawn over the one before with its own outline, under the arm
    feathers = []
    flap = 0.22 * np.sin(2.0 * np.pi * c.t)
    c.pose(angle=-flap * 0.6, cx=50, cy=12)
    for index in range(5):
        angle = 1.65 + index * (0.3 + flap * 0.25)
        length = 46.0 - index * 3.0
        dx, dy = np.cos(angle), np.sin(angle)
        feathers.append(ellipse(c, 50 + dx * length * 0.5, 12 + dy * length * 0.5, length * 0.5, 5.2, angle))
    arm = union(capsule(c, 50, 12, 14, 22, 5.5), circle(c, 50, 12, 6.5))
    c.outline(union(arm, *feathers))
    for feather in reversed(feathers):
        c.outline(feather, 1.6)
        c.fill(feather, (0.98, 0.95, 0.85))
    c.outline(arm, 1.6)
    c.fill(arm, (0.95, 0.85, 0.6))


def bow_arrow(c, broken=False):
    fletch = union(polygon(c, [(10, 54), (6, 46), (14, 50)]), polygon(c, [(10, 54), (18, 58), (14, 50)]))
    if not broken:
        return capsule(c, 10, 54, 50, 14, 2.2), polygon(c, [(57, 7), (45, 12), (52, 19)]), fletch
    # broken in the middle, its front half bent down at an angle at the break, with splinters there, as the developer asked on 2026-11-14
    # (the two halves lay in one line before)
    bend = np.radians(-5.0)
    dx, dy = np.cos(bend), np.sin(bend)
    start = (34.0, 33.5)
    end = (start[0] + dx * 21.0, start[1] + dy * 21.0)
    px, py = -dy, dx
    rear = capsule(c, 10, 54, 29.5, 34.5, 2.2)
    front = capsule(c, start[0], start[1], end[0], end[1], 2.2)
    tip = (end[0] + dx * 9.0, end[1] + dy * 9.0)
    head = polygon(c, [tip, (end[0] - dx + px * 4.8, end[1] - dy + py * 4.8), (end[0] - dx - px * 4.8, end[1] - dy - py * 4.8)])
    # jagged ends of the wood at the break, on the rear half and on the front one
    splinters = union(polygon(c, [(27.5, 34.0), (33.0, 29.5), (31.0, 36.5)]), polygon(c, [(33.0, 31.0), (37.5, 31.5), (34.0, 37.0)]))
    return union(rear, front, splinters), head, fletch


def draw_shoot(c, broken=False, color=(0.85, 0.6, 0.3)):
    # the arrow flies on at the target and back
    target = np.abs(circle(c, 20, 20, 11)) - 2.2
    push = 3.0 * np.sin(2.0 * np.pi * c.t)
    c.pose(dx=-push, dy=push)
    shaft, head, fletch = bow_arrow(c, broken)
    c.pose()
    c.outline(union(shaft, head, fletch, target))
    c.fill(target, (0.95, 0.25, 0.18))
    c.fill(shaft, color)
    c.fill(head, (0.85, 0.88, 0.92))
    c.fill(fletch, (0.95, 0.95, 0.9))


def draw_shoot_far(c):
    draw_shoot(c, True, (1.0, 0.55, 0.2))


def draw_strike(c):
    # a sword that points right, its tip at the right edge, thrusting
    c.pose(dx=3.0 * np.sin(2.0 * np.pi * c.t), scale=0.94)
    blade, hilt = sword(c, 6, 32, 58, 32, blade=4.6, guard=11.0)
    c.outline(union(blade, hilt))
    c.fill(blade, (0.88, 0.9, 0.95))
    c.fill(hilt, (0.95, 0.72, 0.2))


def star(c, cx, cy, outer, inner, points=5, angle=-np.pi / 2):
    corners = []
    for index in range(points * 2):
        r = outer if index % 2 == 0 else inner
        a = angle + index * np.pi / points
        corners.append((cx + r * np.cos(a), cy + r * np.sin(a)))
    return polygon(c, corners)


def draw_cast(c):
    twinkle = np.sin(2.0 * np.pi * c.t)
    big = star(c, 30, 34, 21 + 2.0 * twinkle, 8, 4, 0.15 * twinkle)
    small = star(c, 50, 14, 9 - 2.0 * twinkle, 3.2, 4, -0.3 * twinkle)
    c.outline(union(big, small))
    c.fill(big, (0.75, 0.5, 1.0))
    c.fill(small, (0.55, 0.9, 1.0))


# the icons of the stats, drawn at 40 pixels

def draw_icon_attack(c):
    blade, hilt = sword(c, 14, 52, 52, 12, blade=5.0, guard=11.0)
    c.outline(union(blade, hilt), 3.0)
    c.fill(blade, (0.85, 0.88, 0.92))
    c.fill(hilt, (0.75, 0.3, 0.15))
    c.glint()


def draw_icon_defense(c):
    shield = polygon(c, [(12, 12), (32, 6), (52, 12), (50, 34), (32, 58), (14, 34)])
    c.outline(shield, 3.0)
    c.fill(shield, (0.95, 0.78, 0.3))
    c.fill(polygon(c, [(17, 16), (32, 11), (47, 16), (45, 33), (32, 51), (19, 33)]), (0.25, 0.45, 0.85))
    c.glint()


def draw_icon_damage(c):
    c.pose(angle=c.t * np.pi / 4.0, scale=1.0 + 0.05 * np.sin(2.0 * np.pi * c.t))
    burst = star(c, 32, 32, 27, 11, 8, 0.2)
    c.outline(burst, 3.0)
    c.fill(burst, (1.0, 0.45, 0.15))
    c.fill(star(c, 32, 32, 13, 6, 8, 0.2), (1.0, 0.85, 0.3))


def draw_icon_health(c):
    # a beat: two quick swells in the first half of the cycle
    beat = max(np.sin(4.0 * np.pi * c.t), 0.0) if c.t < 0.5 else 0.0
    c.pose(scale=1.0 + 0.1 * beat)
    heart = union(circle(c, 22, 24, 12), circle(c, 42, 24, 12), polygon(c, [(11, 28), (53, 28), (32, 55)]))
    c.outline(heart, 3.0)
    c.fill(heart, (0.92, 0.2, 0.22))


def draw_icon_speed(c):
    c.pose(dx=3.0 * np.sin(2.0 * np.pi * c.t))
    chevrons = union(*[polygon(c, [(x, 12), (x + 14, 32), (x, 52), (x + 9, 52), (x + 23, 32), (x + 9, 12)]) for x in (8, 26)])
    c.outline(chevrons, 3.0)
    c.fill(chevrons, (0.45, 0.9, 0.4))


def draw_icon_shots(c):
    shaft = capsule(c, 12, 52, 48, 16, 2.6)
    head = polygon(c, [(57, 7), (43, 13), (51, 21)])
    fletch = union(polygon(c, [(12, 52), (6, 42), (16, 47)]), polygon(c, [(12, 52), (22, 58), (17, 47)]))
    c.outline(union(shaft, head, fletch), 3.0)
    c.fill(shaft, (0.75, 0.52, 0.3))
    c.fill(head, (0.85, 0.88, 0.92))
    c.fill(fletch, (0.95, 0.95, 0.9))
    c.glint()


def draw_icon_morale(c):
    pole = capsule(c, 16, 8, 16, 58, 2.6)
    wave = 2.0 * np.sin(2.0 * np.pi * c.t)
    cloth = polygon(c, [(18, 9), (36, 11 + wave), (54, 15 - wave), (44, 24), (54, 33 - wave), (36, 31 + wave), (18, 33)])
    c.outline(union(pole, cloth), 3.0)
    c.fill(pole, (0.6, 0.45, 0.3))
    c.fill(cloth, (1.0, 0.82, 0.25))


def draw_icon_luck(c):
    c.pose(angle=0.12 * np.sin(2.0 * np.pi * c.t))
    leaves = union(circle(c, 23, 23, 10), circle(c, 41, 23, 10), circle(c, 23, 41, 10), circle(c, 41, 41, 10))
    stem = capsule(c, 36, 40, 50, 58, 2.6)
    c.outline(union(leaves, stem), 3.0)
    c.fill(stem, (0.3, 0.6, 0.2))
    c.fill(leaves, (0.35, 0.82, 0.3))


def draw_icon_mana(c):
    c.pose(dy=1.5 * np.sin(2.0 * np.pi * c.t))
    drop = union(circle(c, 32, 40, 16), polygon(c, [(17, 34), (32, 6), (47, 34)]))
    c.outline(drop, 3.0)
    c.fill(drop, (0.3, 0.55, 1.0))
    c.fill(ellipse(c, 26, 38, 4, 7, 0.4), (0.75, 0.88, 1.0), shade=0.0)


def draw_icon_kills(c):
    skull = union(circle(c, 32, 28, 19), polygon(c, [(20, 36), (44, 36), (42, 54), (22, 54)]))
    holes = union(circle(c, 25, 29, 5.5), circle(c, 39, 29, 5.5), polygon(c, [(32, 36), (28, 43), (36, 43)]))
    shape = cut(skull, holes)
    c.outline(skull, 3.0)
    c.fill(shape, (0.95, 0.92, 0.85))
    c.glint()


def draw_icon_count(c):
    figures = []
    for index, (cx, cy) in enumerate([(22, 22), (42, 22)]):
        cy += 1.5 * np.sin(2.0 * np.pi * c.t + index * np.pi)
        figures.append(circle(c, cx, cy, 8))
        figures.append(ellipse(c, cx, cy + 22, 12, 12))
    shape = cut(union(*figures), polygon(c, [(0, 56), (64, 56), (64, 64), (0, 64)]))
    c.outline(shape, 3.0)
    c.fill(shape, (0.8, 0.82, 0.88))


def draw_icon_flies(c):
    draw_fly(c)


def straight_arrow(c, ax, ay, bx, by):
    # a shaft from a to b with its head at b
    dx, dy = bx - ax, by - ay
    length = max(np.hypot(dx, dy), 1e-3)
    ux, uy = dx / length, dy / length
    head = polygon(c, [(bx + ux * 3, by + uy * 3), (bx - ux * 8 - uy * 7, by - uy * 8 + ux * 7), (bx - ux * 8 + uy * 7, by - uy * 8 - ux * 7)])
    return union(capsule(c, ax, ay, bx - ux * 5, by - uy * 5, 3.0), head)


def draw_exchange(c):
    # two banners that hand stacks to each other, as heroes of one player exchange their armies, the arrows swinging to and fro
    swing = 3.0 * np.sin(2.0 * np.pi * c.t)
    left = polygon(c, [(8, 12), (22, 12), (22, 30), (15, 25), (8, 30)])
    right = polygon(c, [(42, 34), (56, 34), (56, 52), (49, 47), (42, 52)])
    top = straight_arrow(c, 24 + swing, 18, 48 + swing, 18)
    bottom = straight_arrow(c, 40 - swing, 46, 16 - swing, 46)
    c.outline(union(left, right, top, bottom))
    c.fill(left, (0.95, 0.3, 0.2))
    c.fill(right, (0.3, 0.55, 1.0))
    c.fill(top, (1.0, 0.85, 0.4))
    c.fill(bottom, (1.0, 0.85, 0.4))


def draw_icon_level_up(c):
    # an arrow up that rises and falls a little, on a gold disc, for the button that upgrades a stack
    lift = 2.0 * np.sin(2.0 * np.pi * c.t)
    disc = circle(c, 32, 32, 26)
    shaft = capsule(c, 32, 46 - lift, 32, 26 - lift, 5.0)
    head = polygon(c, [(32, 10 - lift), (48, 28 - lift), (16, 28 - lift)])
    c.outline(disc)
    c.fill(disc, (0.55, 0.38, 0.08))
    c.fill(union(shaft, head), (1.0, 0.9, 0.45))

# name, drawing, pixels, hot spot (0 to 1 of the picture), whether it is a cursor, frames a second
ICONS = [
    ('Default', draw_default, 64, (5 / 64, 4 / 64), True),
    ('Choose', draw_choose, 64, (5 / 64, 4 / 64), True),
    ('Move', draw_move, 64, (0.5, 0.5), True),
    ('MoveLater', draw_move_later, 64, (0.5, 0.5), True),
    ('Blocked', draw_blocked, 64, (0.5, 0.5), True),
    ('Attack', draw_attack, 64, (0.5, 0.5), True),
    ('Castle', draw_castle, 64, (0.5, 0.5), True),
    ('Visit', draw_visit, 64, (0.5, 0.5), True),
    ('Portal', draw_portal, 64, (0.5, 0.5), True),
    ('Swap', draw_swap, 64, (0.5, 0.5), True),
    ('Exchange', draw_exchange, 64, (0.5, 0.5), True),
    ('Fly', draw_fly, 64, (0.5, 0.5), True),
    ('Shoot', draw_shoot, 64, (0.5, 0.5), True),
    ('ShootFar', draw_shoot_far, 64, (0.5, 0.5), True),
    ('Strike', draw_strike, 64, (0.5, 0.5), True),
    ('Cast', draw_cast, 64, (0.5, 0.5), True),
    ('IconAttack', draw_icon_attack, 40, (0.5, 0.5), False),
    ('IconDefense', draw_icon_defense, 40, (0.5, 0.5), False),
    ('IconDamage', draw_icon_damage, 40, (0.5, 0.5), False),
    ('IconHealth', draw_icon_health, 40, (0.5, 0.5), False),
    ('IconSpeed', draw_icon_speed, 40, (0.5, 0.5), False),
    ('IconShots', draw_icon_shots, 40, (0.5, 0.5), False),
    ('IconMorale', draw_icon_morale, 40, (0.5, 0.5), False),
    ('IconLuck', draw_icon_luck, 40, (0.5, 0.5), False),
    ('IconMana', draw_icon_mana, 40, (0.5, 0.5), False),
    ('IconKills', draw_icon_kills, 40, (0.5, 0.5), False),
    ('IconCount', draw_icon_count, 40, (0.5, 0.5), False),
    ('IconFlies', draw_icon_flies, 40, (0.5, 0.5), False),
    ('IconLevelUp', draw_icon_level_up, 40, (0.5, 0.5), False),
]

# how fast each plays, frames a second; the rest at DEFAULT_RATE
RATES = {'Move': 12.0, 'MoveLater': 10.0, 'Default': 4.0, 'Choose': 6.0, 'IconHealth': 6.0}
DEFAULT_RATE = 8.0


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    manifest = []
    # a partial run keeps the lines of the pictures it does not draw
    listed = os.path.join(OUT, 'ui_icons.json')
    if only and os.path.exists(listed):
        with open(listed) as file:
            manifest = [entry for entry in json.load(file) if entry['name'] not in only]
    for name, draw, size, hot, is_cursor in ICONS:
        if only and name not in only:
            continue
        frames = []
        for frame in range(FRAMES):
            canvas = Canvas(size, frame / FRAMES)
            draw(canvas)
            frames.append(canvas.image())
        file = ('T_Cursor_' if is_cursor else 'T_') + name + '.png'
        write_png(os.path.join(OUT, file), np.concatenate(frames, axis=1))
        manifest.append({'name': name, 'file': file, 'size': size, 'hotspot': hot, 'cursor': is_cursor, 'frames': FRAMES,
                         'rate': RATES.get(name, DEFAULT_RATE)})
        print('wrote', file)
    with open(os.path.join(OUT, 'ui_icons.json'), 'w') as file:
        json.dump(manifest, file, indent=1)


if __name__ == '__main__':
    main()
