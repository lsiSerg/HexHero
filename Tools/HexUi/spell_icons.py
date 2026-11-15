"""The pictures of the spells, of the skills of heroes and of the attributes of a hero, as the developer asked on 2026-11-15 for a spell book of
pictures and skills that show: each a glyph of a few shapes on a plate in the color of its school or kind. generate_ui_icons.py draws them as
it draws the cursors (ICONS of this file, one frame each), and import_ui_icons.py imports the spells to /Game/HexPlanet/UI/Spells and the
skills to /Game/HexPlanet/UI/Skills, where FHexSpell::Icon and FHexSkillInfo::Icon name them by their Ids. Placeholders an artist replaces"""

import numpy as np

from generate_ui_icons import capsule, circle, cut, ellipse, polygon, star, sword, turned, union

SCHOOLS = {'Air': (0.42, 0.62, 0.85), 'Earth': (0.55, 0.42, 0.25), 'Fire': (0.75, 0.25, 0.1), 'Water': (0.18, 0.38, 0.75), 'Skill': (0.35, 0.3, 0.42),
           'Stat': (0.0, 0.0, 0.0)}

WHITE = (0.96, 0.95, 0.9)
GOLD = (1.0, 0.8, 0.3)
RED = (0.95, 0.2, 0.15)
DARK = (0.25, 0.12, 0.3)
CYAN = (0.6, 0.95, 1.0)
GREEN = (0.4, 0.9, 0.35)
STEEL = (0.75, 0.78, 0.82)
ORANGE = (1.0, 0.55, 0.15)
PURPLE = (0.7, 0.4, 0.95)
BROWN = (0.55, 0.38, 0.2)
GREY = (0.62, 0.6, 0.56)


def plate(c, school):
    """A round plate of the color of the school, lighter in its middle"""
    if school == 'Stat':
        return
    color = SCHOOLS[school]
    disc = circle(c, 32, 32, 29)
    c.outline(disc, 2.4)
    c.fill(disc, tuple(v * 0.55 for v in color), shade=0.0)
    c.fill(circle(c, 30, 30, 22), tuple(min(v * 0.85 + 0.05, 1.0) for v in color), shade=0.35)


# glyphs: each returns a distance field and a color, drawn together with one outline

def bolt(c, dx=0.0, dy=0.0, scale=1.0):
    p = [(36, 6), (18, 34), (30, 34), (24, 58), (46, 26), (34, 26), (42, 6)]
    return polygon(c, [(32 + (x - 32) * scale + dx, 32 + (y - 32) * scale + dy) for x, y in p])


def arrow(c, angle=-0.8):
    shaft = capsule(c, 14, 50, 46, 18, 2.4)
    head = polygon(c, [(55, 9), (39, 15), (49, 25)])
    fletch = union(polygon(c, [(14, 50), (7, 42), (17, 46)]), polygon(c, [(14, 50), (22, 57), (18, 47)]))
    return union(shaft, head, fletch)


def flame(c, cx=32, cy=36, s=1.0):
    outer = union(circle(c, cx, cy + 8 * s, 13 * s), polygon(c, [(cx - 13 * s, cy + 7 * s), (cx - 4 * s, cy - 20 * s), (cx + 2 * s, cy - 8 * s),
                                                                 (cx + 8 * s, cy - 26 * s), (cx + 13 * s, cy + 7 * s)]))
    return outer


def inner_flame(c, cx=32, cy=36, s=1.0):
    return union(circle(c, cx, cy + 10 * s, 6 * s), polygon(c, [(cx - 6 * s, cy + 10 * s), (cx + 1 * s, cy - 6 * s), (cx + 6 * s, cy + 10 * s)]))


def drop(c, cx=32, cy=36, s=1.0):
    return union(circle(c, cx, cy + 4 * s, 13 * s), polygon(c, [(cx - 12 * s, cy), (cx, cy - 22 * s), (cx + 12 * s, cy)]))


def snowflake(c, cx=32, cy=32, r=20.0):
    arms = []
    for k in range(3):
        a = k * np.pi / 3
        dx, dy = np.cos(a) * r, np.sin(a) * r
        arms.append(capsule(c, cx - dx, cy - dy, cx + dx, cy + dy, 2.2))
        for sign in (-1, 1):
            for end in (-1, 1):
                bx, by = cx + end * dx * 0.6, cy + end * dy * 0.6
                b = a + sign * 0.7 + (np.pi if end < 0 else 0)
                arms.append(capsule(c, bx, by, bx + np.cos(b) * r * 0.32, by + np.sin(b) * r * 0.32, 1.6))
    return union(*arms)


def shield(c, s=1.0, cx=32, cy=32):
    p = [(12, 12), (32, 6), (52, 12), (50, 34), (32, 58), (14, 34)]
    return polygon(c, [(cx + (x - 32) * s, cy + (y - 32) * s) for x, y in p])


def heart(c, cx=32, cy=32, s=1.0):
    return union(circle(c, cx - 10 * s, cy - 6 * s, 11 * s), circle(c, cx + 10 * s, cy - 6 * s, 11 * s),
                 polygon(c, [(cx - 20 * s, cy - 2 * s), (cx + 20 * s, cy - 2 * s), (cx, cy + 22 * s)]))


def skull(c, cx=32, cy=30, s=1.0):
    head = union(circle(c, cx, cy, 17 * s), polygon(c, [(cx - 10 * s, cy + 8 * s), (cx + 10 * s, cy + 8 * s), (cx + 8 * s, cy + 22 * s), (cx - 8 * s, cy + 22 * s)]))
    holes = union(circle(c, cx - 7 * s, cy + 1 * s, 5 * s), circle(c, cx + 7 * s, cy + 1 * s, 5 * s), polygon(c, [(cx, cy + 8 * s), (cx - 3 * s, cy + 13 * s), (cx + 3 * s, cy + 13 * s)]))
    return cut(head, holes)


def eye(c):
    lid = ellipse(c, 32, 32, 22, 12)
    return cut(lid, cut(circle(c, 32, 32, 8.5), circle(c, 32, 32, 4)))


def ring(c, cx=32, cy=32, r=20.0, w=3.0):
    return np.abs(circle(c, cx, cy, r)) - w


def slash(c):
    return capsule(c, 14, 50, 50, 14, 3.4)


def clover(c):
    leaves = union(circle(c, 24, 24, 9), circle(c, 40, 24, 9), circle(c, 24, 40, 9), circle(c, 40, 40, 9))
    return union(leaves, capsule(c, 36, 38, 50, 56, 2.4))


def hourglass(c):
    glass = union(polygon(c, [(18, 12), (46, 12), (32, 32)]), polygon(c, [(32, 32), (18, 52), (46, 52)]))
    bars = union(capsule(c, 14, 10, 50, 10, 2.6), capsule(c, 14, 54, 50, 54, 2.6))
    return union(glass, bars)


def swirl(c, cx=32, cy=32, r=22.0):
    parts = []
    points = [(cx + np.cos(t) * r * (1.0 - t / 9.0), cy + np.sin(t) * r * (1.0 - t / 9.0)) for t in np.linspace(0.0, 7.5, 16)]
    for (ax, ay), (bx, by) in zip(points[:-1], points[1:]):
        parts.append(capsule(c, ax, ay, bx, by, 2.6))
    return union(*parts)


def rock(c, cx=32, cy=34, s=1.0):
    p = [(10, 46), (14, 24), (26, 12), (42, 14), (54, 30), (50, 48), (30, 54)]
    return polygon(c, [(cx + (x - 32) * s, cy + (y - 32) * s) for x, y in p])


def crack(c):
    p = [(10, 30), (22, 36), (28, 26), (38, 38), (44, 28), (54, 34)]
    return union(*[capsule(c, ax, ay, bx, by, 2.4) for (ax, ay), (bx, by) in zip(p[:-1], p[1:])])


def plus(c, cx=32, cy=32, r=16.0, w=4.5):
    return union(capsule(c, cx - r, cy, cx + r, cy, w), capsule(c, cx, cy - r, cx, cy + r, w))


def qmark(c):
    points = [(22, 22), (24, 15), (32, 11), (40, 15), (42, 23), (38, 30), (32, 34), (32, 42)]
    stroke = union(*[capsule(c, ax, ay, bx, by, 3.4) for (ax, ay), (bx, by) in zip(points[:-1], points[1:])])
    return union(stroke, circle(c, 32, 51, 4))


def chevrons(c):
    return union(*[polygon(c, [(x, 12), (x + 14, 32), (x, 52), (x + 9, 52), (x + 23, 32), (x + 9, 12)]) for x in (10, 28)])


def target(c):
    return union(ring(c, 32, 32, 20, 2.5), ring(c, 32, 32, 11, 2.5), circle(c, 32, 32, 4))


def sun(c, cx=32, cy=32, r=11.0):
    return union(star(c, cx, cy, r * 2.0, r * 1.05, 12, 0.0), circle(c, cx, cy, r))


def meteor(c, cx, cy, r):
    return union(circle(c, cx, cy, r), capsule(c, cx - r * 2.2, cy - r * 2.2, cx, cy, r * 0.55))


def claws(c):
    return union(*[capsule(c, x, 12, x - 12, 52, 2.6) for x in (28, 38, 48)])


def inward(c):
    parts = [circle(c, 32, 32, 6)]
    for k in range(4):
        a = k * np.pi / 2 + np.pi / 4
        ox, oy = 32 + np.cos(a) * 24, 32 + np.sin(a) * 24
        ix, iy = 32 + np.cos(a) * 12, 32 + np.sin(a) * 12
        parts.append(capsule(c, ox, oy, ix, iy, 2.6))
        parts.append(polygon(c, turned([(ix - 4, iy), (ix + 4, iy), (ix, iy)], 0.0)))
    return union(*parts)


def banner(c):
    pole = capsule(c, 18, 8, 18, 58, 2.6)
    cloth = polygon(c, [(20, 10), (52, 14), (44, 24), (52, 34), (20, 32)])
    return union(pole, cloth)


def boot(c):
    return union(polygon(c, [(20, 10), (34, 10), (34, 38), (52, 44), (52, 54), (18, 54)]), capsule(c, 18, 10, 36, 10, 2.0))


def wing(c):
    return polygon(c, [(30, 30), (10, 18), (16, 26), (8, 28), (16, 34), (10, 38), (30, 36)])


def compass(c):
    return union(ring(c, 32, 32, 22, 2.2), star(c, 32, 32, 18, 4.5, 4, -np.pi / 2))


def book(c):
    cover = polygon(c, [(12, 14), (32, 18), (52, 14), (52, 50), (32, 54), (12, 50)])
    return cover


def wand(c):
    return union(capsule(c, 14, 52, 40, 26, 2.8), star(c, 44, 20, 12, 5, 5, -np.pi / 2))


def horseshoe(c):
    arc = cut(ring(c, 32, 28, 17, 5.0), polygon(c, [(16, 40), (48, 40), (48, 64), (16, 64)]))
    return union(arc, capsule(c, 15, 30, 17, 50, 4.6), capsule(c, 49, 30, 47, 50, 4.6))


def boat(c):
    """A hull with a sail, for Summon boat"""
    hull = polygon(c, [(8, 40), (56, 40), (48, 52), (16, 52)])
    sail = polygon(c, [(31, 8), (31, 36), (12, 36)])
    return hull, union(sail, capsule(c, 33, 8, 33, 40, 1.6))


def hills(c, cy=46):
    return polygon(c, [(8, cy + 10), (8, cy), (20, cy - 10), (30, cy - 2), (40, cy - 12), (56, cy), (56, cy + 10)])


def doorway(c):
    """An arch of stone, for Dimension door"""
    outer = union(polygon(c, [(14, 56), (14, 28), (50, 28), (50, 56)]), circle(c, 32, 28, 18))
    inner = union(polygon(c, [(21, 56), (21, 28), (43, 28), (43, 56)]), circle(c, 32, 28, 11))
    return cut(outer, inner), inner


def hexagon(c, cx, cy, r):
    return polygon(c, [(cx + r * np.cos(np.pi / 6 + k * np.pi / 3), cy + r * np.sin(np.pi / 6 + k * np.pi / 3)) for k in range(6)])


def tower(c):
    """A tower with battlements, for Town portal"""
    body = polygon(c, [(22, 56), (24, 22), (40, 22), (42, 56)])
    merlons = union(*[polygon(c, [(x, 22), (x, 14), (x + 5, 14), (x + 5, 22)]) for x in (21, 29.5, 38)])
    return union(body, merlons), polygon(c, [(29, 56), (29, 44), (35, 44), (35, 56)])


def laurel(c):
    return star(c, 32, 30, 22, 9, 5, -np.pi / 2)


def draw(c, school, parts):
    """The plate of a school and the parts of a glyph over it: (distance, color) each, under one outline"""
    plate(c, school)
    c.outline(union(*[d for d, _ in parts]), 2.6)
    for distance, color in parts:
        c.fill(distance, color)


def spell(school, make):
    return lambda c: draw(c, school, make(c))


SPELLS = {
    'Haste': spell('Air', lambda c: [(chevrons(c), GOLD)]),
    'MagicArrow': spell('Air', lambda c: [(arrow(c), CYAN)]),
    'LightningBolt': spell('Air', lambda c: [(bolt(c), (1.0, 0.95, 0.55))]),
    'DisruptingRay': spell('Air', lambda c: [(shield(c, 0.8), STEEL), (crack(c), (0.4, 1.0, 0.75))]),
    'Fortune': spell('Air', lambda c: [(clover(c), GREEN)]),
    'Precision': spell('Air', lambda c: [(target(c), WHITE), (arrow(c), CYAN)]),
    'AirShield': spell('Air', lambda c: [(shield(c, 0.85), (0.75, 0.9, 1.0)), (swirl(c, 32, 32, 12), WHITE)]),
    'DestroyUndead': spell('Air', lambda c: [(sun(c, 32, 30, 12), GOLD), (skull(c, 32, 32, 0.55), WHITE)]),
    'ChainLightning': spell('Air', lambda c: [(bolt(c, -8, 0, 0.8), (1.0, 0.95, 0.55)), (bolt(c, 10, 4, 0.7), (0.8, 0.9, 1.0))]),
    'Counterstrike': spell('Air', lambda c: [(union(*sword(c, 12, 52, 52, 12, 3.6, 8.0)), STEEL), (union(*sword(c, 52, 52, 12, 12, 3.6, 8.0)), GOLD)]),
    'Slow': spell('Earth', lambda c: [(hourglass(c), (0.9, 0.78, 0.5))]),
    'StoneSkin': spell('Earth', lambda c: [(rock(c), GREY)]),
    'Shield': spell('Earth', lambda c: [(shield(c), STEEL)]),
    'DeathRipple': spell('Earth', lambda c: [(ring(c, 32, 32, 24, 2.4), PURPLE), (skull(c, 32, 30, 0.7), WHITE)]),
    'AnimateDead': spell('Earth', lambda c: [(skull(c, 32, 34, 0.75), (0.75, 0.95, 0.7)), (polygon(c, [(32, 4), (22, 16), (42, 16)]), GREEN)]),
    'AntiMagic': spell('Earth', lambda c: [(ring(c, 32, 32, 20, 3.4), WHITE), (slash(c), RED)]),
    'Earthquake': spell('Earth', lambda c: [(rock(c, 32, 40, 0.75), BROWN), (crack(c), (0.1, 0.07, 0.05))]),
    'MeteorShower': spell('Earth', lambda c: [(meteor(c, 44, 22, 6), ORANGE), (meteor(c, 28, 38, 8), (1.0, 0.7, 0.3)), (meteor(c, 48, 46, 5), ORANGE)]),
    'Resurrection': spell('Earth', lambda c: [(sun(c, 32, 32, 9), (1.0, 0.92, 0.6)), (plus(c, 32, 32, 14, 3.5), WHITE)]),
    'Implosion': spell('Earth', lambda c: [(inward(c), PURPLE)]),
    'Bloodlust': spell('Fire', lambda c: [(claws(c), RED)]),
    'Curse': spell('Fire', lambda c: [(skull(c), (0.45, 0.25, 0.5))]),
    'ProtectionFromFire': spell('Fire', lambda c: [(shield(c), STEEL), (flame(c, 32, 34, 0.5), ORANGE)]),
    'Blind': spell('Fire', lambda c: [(eye(c), WHITE), (slash(c), RED)]),
    'Fireball': spell('Fire', lambda c: [(circle(c, 34, 34, 15), ORANGE), (inner_flame(c, 34, 24, 1.1), (1.0, 0.9, 0.4))]),
    'Misfortune': spell('Fire', lambda c: [(clover(c), (0.3, 0.4, 0.25)), (slash(c), RED)]),
    'Inferno': spell('Fire', lambda c: [(flame(c, 32, 34, 1.1), (1.0, 0.4, 0.1)), (inner_flame(c, 32, 34, 1.1), (1.0, 0.85, 0.3))]),
    'Frenzy': spell('Fire', lambda c: [(claws(c), (0.6, 0.05, 0.05)), (star(c, 18, 18, 9, 4, 5, 0.0), GOLD)]),
    'FireShield': spell('Fire', lambda c: [(ring(c, 32, 32, 22, 3.0), ORANGE), (shield(c, 0.65), STEEL)]),
    'Armageddon': spell('Fire', lambda c: [(flame(c, 32, 40, 0.9), (1.0, 0.35, 0.1)), (meteor(c, 46, 16, 5), ORANGE), (meteor(c, 20, 14, 4), ORANGE)]),
    'Bless': spell('Water', lambda c: [(ring(c, 32, 14, 12, 2.0), GOLD), (star(c, 32, 38, 18, 8, 5, -np.pi / 2), (1.0, 0.95, 0.7))]),
    'Cure': spell('Water', lambda c: [(heart(c), (0.95, 0.3, 0.35)), (plus(c, 32, 30, 8, 2.6), WHITE)]),
    'Dispel': spell('Water', lambda c: [(swirl(c), CYAN), (slash(c), WHITE)]),
    'ProtectionFromWater': spell('Water', lambda c: [(shield(c), STEEL), (drop(c, 32, 32, 0.55), (0.35, 0.65, 1.0))]),
    'IceBolt': spell('Water', lambda c: [(polygon(c, [(52, 10), (40, 14), (14, 46), (18, 50), (50, 22)]), CYAN), (snowflake(c, 20, 44, 8), WHITE)]),
    'Weakness': spell('Water', lambda c: [(union(*sword(c, 14, 50, 50, 14, 4.0, 9.0)), STEEL), (slash(c), RED)]),
    'Forgetfulness': spell('Water', lambda c: [(qmark(c), (0.75, 0.8, 1.0))]),
    'FrostRing': spell('Water', lambda c: [(ring(c, 32, 32, 22, 3.0), CYAN), (snowflake(c, 32, 32, 12), WHITE)]),
    'Mirth': spell('Water', lambda c: [(sun(c), GOLD)]),
    'Prayer': spell('Water', lambda c: [(star(c, 32, 32, 22, 9, 8, 0.0), (1.0, 0.92, 0.6)), (plus(c, 32, 32, 10, 3.0), WHITE)]),
    # the spells of the planet, as the developer asked on 2026-11-15
    'SummonBoat': spell('Water', lambda c: [(boat(c)[0], BROWN), (boat(c)[1], WHITE)]),
    'ViewLand': spell('Air', lambda c: [(hills(c), GREEN), (eye(c), WHITE)]),
    'ViewDepths': spell('Earth', lambda c: [(cut(rock(c, 32, 40, 0.85), circle(c, 32, 50, 10)), GREY), (eye(c), CYAN)]),
    'DimensionDoor': spell('Air', lambda c: [(doorway(c)[0], GREY), (doorway(c)[1], PURPLE), (swirl(c, 32, 38, 9), WHITE)]),
    # two hexes and a leap between them in an arc, so it reads apart from the doorway of Dimension door (2026-11-15)
    'Teleport': spell('Water', lambda c: [(cut(hexagon(c, 16, 44, 12), hexagon(c, 16, 44, 7)), CYAN), (cut(hexagon(c, 48, 44, 12), hexagon(c, 48, 44, 7)), (0.75, 0.6, 1.0)),
        (union(*[circle(c, 16 + 32 * t, 38 - 26 * np.sin(np.pi * t), 2.2) for t in (0.2, 0.35, 0.5, 0.65)]), WHITE), (star(c, 48, 22, 8, 3.5, 4, 0.0), (1.0, 0.95, 0.6))]),
    'TownPortal': spell('Earth', lambda c: [(ring(c, 32, 34, 25, 2.6), GOLD), (tower(c)[0], (0.8, 0.75, 0.65)), (tower(c)[1], (0.2, 0.12, 0.08))]),
}

SKILLS = {
    'Sorcery': lambda c: draw(c, 'Skill', [(star(c, 32, 32, 22, 9, 6, -np.pi / 2), PURPLE), (star(c, 48, 14, 7, 3, 4, 0.0), GOLD)]),
    'Diplomacy': lambda c: draw(c, 'Skill', [(banner(c), WHITE)]),
    'Logistics': lambda c: draw(c, 'Skill', [(boot(c), BROWN), (wing(c), WHITE)]),
    'AirMagic': lambda c: draw(c, 'Air', [(swirl(c), WHITE)]),
    'EarthMagic': lambda c: draw(c, 'Earth', [(rock(c), GREY)]),
    'FireMagic': lambda c: draw(c, 'Fire', [(flame(c), ORANGE), (inner_flame(c), (1.0, 0.85, 0.3))]),
    'WaterMagic': lambda c: draw(c, 'Water', [(drop(c), (0.45, 0.75, 1.0))]),
    'Mysticism': lambda c: draw(c, 'Skill', [(polygon(c, [(32, 8), (56, 52), (8, 52)]), PURPLE), (eye(c), WHITE)]),
    'Pathfinding': lambda c: draw(c, 'Skill', [(compass(c), GOLD)]),
}

STATS = {
    'IconSpellPower': lambda c: draw(c, 'Stat', [(wand(c), (0.75, 0.55, 1.0))]),
    'IconKnowledge': lambda c: draw(c, 'Stat', [(book(c), (0.35, 0.5, 0.9)), (capsule(c, 32, 18, 32, 52, 1.2), WHITE)]),
    'IconExperience': lambda c: draw(c, 'Stat', [(laurel(c), GOLD)]),
    'IconMovement': lambda c: draw(c, 'Stat', [(horseshoe(c), STEEL)]),
    'IconBuild': lambda c: draw(c, 'Stat', [(capsule(c, 18, 52, 39, 22, 3.2), BROWN), (capsule(c, 30, 12, 50, 27, 6.5), STEEL)]),
}

# name, drawing, pixels, whether it is a spell, a skill or an icon of a stat
ICONS = [('Spell' + name, make, 64, 'spell') for name, make in SPELLS.items()] + [('Skill' + name, make, 64, 'skill') for name, make in SKILLS.items()] + [
    (name, make, 40, 'stat') for name, make in STATS.items()]
