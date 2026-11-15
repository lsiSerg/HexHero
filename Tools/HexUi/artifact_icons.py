"""The pictures of the artifacts, as the developer asked on 2026-11-15 ("додай артефакти"): each a glyph of its kind of slot (a helm, an amulet, a
breastplate, a blade, a shield, a cloak, a boot, a ring, a charm) in colors of its own, with a gem or a mark that tells it apart, on a plate in
the color of its rank (Treasure, Minor, Major, Relic). generate_ui_icons.py draws them (ICONS of this file, one frame each, T_Art<Id>) and
import_ui_icons.py imports them to /Game/HexPlanet/UI/Artifacts, where FHexArtifact::Icon names them by their Ids. Placeholders an artist
replaces"""

import numpy as np

from generate_ui_icons import capsule, circle, cut, ellipse, polygon, star, sword, union
from spell_icons import boot, book, clover, drop, flame, inner_flame, rock, shield, swirl, wing

RANKS = {'Treasure': (0.38, 0.34, 0.3), 'Minor': (0.2, 0.35, 0.62), 'Major': (0.46, 0.22, 0.6), 'Relic': (0.8, 0.58, 0.12)}

IRON = (0.62, 0.64, 0.68)
STEEL = (0.82, 0.85, 0.9)
GOLD = (1.0, 0.8, 0.3)
BRONZE = (0.75, 0.5, 0.25)
LEATHER = (0.5, 0.33, 0.18)
WOOD = (0.55, 0.38, 0.2)
RED = (0.9, 0.18, 0.15)
BLUE = (0.3, 0.55, 1.0)
GREEN = (0.35, 0.85, 0.35)
PURPLE = (0.7, 0.4, 0.95)
WHITE = (0.95, 0.94, 0.9)
CLOTH = (0.55, 0.15, 0.15)
STONE = (0.55, 0.52, 0.48)


def plate(c, rank):
    """A square plate with cut corners in the color of the rank, lighter in its middle"""
    color = RANKS[rank]
    outer = polygon(c, [(10, 4), (54, 4), (60, 10), (60, 54), (54, 60), (10, 60), (4, 54), (4, 10)])
    c.outline(outer, 2.2)
    c.fill(outer, tuple(v * 0.5 for v in color), shade=0.0)
    c.fill(polygon(c, [(12, 9), (52, 9), (55, 12), (55, 52), (52, 55), (12, 55), (9, 52), (9, 12)]), tuple(min(v * 0.8 + 0.06, 1.0) for v in color), shade=0.4)


# the glyphs of the kinds of slots

def helm(c):
    dome = cut(union(circle(c, 32, 32, 18), polygon(c, [(14, 32), (50, 32), (48, 50), (16, 50)])), polygon(c, [(22, 36), (42, 36), (40, 44), (24, 44)]))
    return union(dome, capsule(c, 32, 12, 32, 30, 2.0))


def hood(c):
    return union(polygon(c, [(32, 8), (52, 30), (50, 54), (14, 54), (12, 30)]))


def crown(c):
    return polygon(c, [(12, 46), (12, 22), (22, 34), (32, 16), (42, 34), (52, 22), (52, 46)])


def amulet(c):
    chain = cut(ellipse(c, 32, 24, 17, 15), ellipse(c, 32, 24, 13.5, 11.5))
    return union(cut(chain, polygon(c, [(20, 30), (44, 30), (44, 40), (20, 40)])), circle(c, 32, 44, 9))


def torc(c):
    arc = cut(ellipse(c, 32, 30, 20, 17), ellipse(c, 32, 30, 15, 12))
    return union(cut(arc, polygon(c, [(24, 40), (40, 40), (40, 52), (24, 52)])), circle(c, 23, 44, 4.5), circle(c, 41, 44, 4.5))


def breastplate(c):
    return polygon(c, [(16, 12), (26, 16), (38, 16), (48, 12), (54, 24), (48, 30), (46, 54), (18, 54), (16, 30), (10, 24)])


def robe(c):
    return polygon(c, [(24, 10), (40, 10), (44, 22), (52, 56), (12, 56), (20, 22)])


def blade(c):
    return union(*sword(c, 16, 50, 50, 14, 4.2, 10.0))


def axe(c):
    haft = capsule(c, 20, 54, 42, 12, 2.6)
    head = polygon(c, [(36, 10), (54, 14), (56, 30), (44, 26)])
    return union(haft, head)


def staff(c):
    return union(capsule(c, 18, 54, 42, 14, 2.6), circle(c, 44, 12, 6.5))


def spear(c):
    return union(capsule(c, 14, 54, 44, 18, 2.2), polygon(c, [(52, 8), (38, 16), (46, 24)]))


def buckler(c):
    return circle(c, 32, 32, 20)


def cloak(c):
    return polygon(c, [(22, 10), (42, 10), (46, 16), (54, 56), (40, 52), (32, 56), (24, 52), (10, 56), (18, 16)])


def ring(c, cx=32, cy=34):
    band = cut(circle(c, cx, cy, 15), circle(c, cx, cy, 10))
    return union(band, polygon(c, [(cx - 7, cy - 14), (cx, cy - 22), (cx + 7, cy - 14), (cx, cy - 10)]))


def flask(c):
    return union(circle(c, 32, 40, 14), polygon(c, [(28, 12), (36, 12), (36, 28), (28, 28)]))


def horn(c):
    return polygon(c, [(10, 22), (18, 18), (40, 28), (56, 40), (54, 52), (44, 46), (24, 34), (12, 30)])


def orb(c):
    return circle(c, 32, 30, 16)


def lantern(c):
    return union(polygon(c, [(20, 18), (44, 18), (40, 52), (24, 52)]), capsule(c, 26, 12, 38, 12, 2.5))


def draw(c, rank, parts):
    """The plate of a rank and the parts of a glyph over it: (distance, color) each, under one outline"""
    plate(c, rank)
    c.outline(union(*[d for d, _ in parts]), 2.4)
    for distance, color in parts:
        c.fill(distance, color)


def gem(c, cx, cy, r, color):
    return (polygon(c, [(cx, cy - r), (cx + r, cy), (cx, cy + r), (cx - r, cy)]), color)


def art(rank, make):
    return lambda c: draw(c, rank, make(c))


ARTIFACTS = {
    'SentinelHelm': art('Treasure', lambda c: [(helm(c), IRON)]),
    'WatcherHood': art('Minor', lambda c: [(hood(c), (0.25, 0.32, 0.3)), (ellipse(c, 32, 32, 7, 4), (0.6, 0.95, 0.8))]),
    'CrownOfDawn': art('Relic', lambda c: [(crown(c), GOLD), gem(c, 32, 38, 5, RED), gem(c, 20, 40, 3, BLUE), gem(c, 44, 40, 3, BLUE)]),
    'PendantOfCourage': art('Treasure', lambda c: [(amulet(c), BRONZE), (circle(c, 32, 44, 5), RED)]),
    'EmberAmulet': art('Minor', lambda c: [(amulet(c), GOLD), (flame(c, 32, 44, 0.32), (1.0, 0.45, 0.1))]),
    'TideTorc': art('Minor', lambda c: [(torc(c), STEEL), (drop(c, 32, 30, 0.4), BLUE)]),
    'RivetedHauberk': art('Minor', lambda c: [(breastplate(c), IRON), (capsule(c, 32, 22, 32, 48, 1.2), (0.4, 0.4, 0.45))]),
    'RobeOfStars': art('Minor', lambda c: [(robe(c), (0.2, 0.22, 0.55)), (star(c, 32, 36, 7, 3, 5), GOLD), (star(c, 24, 46, 4, 2, 5), WHITE)]),
    'DragonscaleCuirass': art('Major', lambda c: [(breastplate(c), (0.35, 0.6, 0.3)), (polygon(c, [(24, 26), (32, 22), (40, 26), (32, 40)]), (0.8, 0.85, 0.3))]),
    'SoldierBlade': art('Treasure', lambda c: [(blade(c), STEEL)]),
    'NorthernAxe': art('Minor', lambda c: [(axe(c), STEEL)]),
    'StaffOfAsh': art('Minor', lambda c: [(staff(c), WOOD), (circle(c, 44, 12, 4), (0.7, 0.85, 1.0))]),
    'ThunderSpear': art('Major', lambda c: [(spear(c), STEEL), (polygon(c, [(20, 22), (14, 34), (20, 34), (16, 44), (26, 30), (20, 30), (24, 22)]), (1.0, 0.95, 0.5))]),
    'SunforgedBlade': art('Relic', lambda c: [(blade(c), (1.0, 0.92, 0.6)), (circle(c, 22, 42, 5), (1.0, 0.6, 0.15))]),
    'OakBuckler': art('Treasure', lambda c: [(buckler(c), WOOD), (circle(c, 32, 32, 6), IRON)]),
    'TowerShield': art('Minor', lambda c: [(shield(c, 1.05), IRON), (capsule(c, 32, 16, 32, 46, 2.0), (0.4, 0.4, 0.45))]),
    'AegisOfDawn': art('Major', lambda c: [(shield(c, 1.05), GOLD), (star(c, 32, 30, 10, 4, 8, 0.0), WHITE)]),
    'TravellerCloak': art('Minor', lambda c: [(cloak(c), (0.45, 0.35, 0.2)), (circle(c, 32, 14, 3.5), BRONZE)]),
    'CloakOfStone': art('Minor', lambda c: [(cloak(c), STONE), (rock(c, 32, 36, 0.45), (0.4, 0.38, 0.35))]),
    'WindMantle': art('Major', lambda c: [(cloak(c), (0.55, 0.75, 0.9)), (swirl(c, 32, 36, 11), WHITE)]),
    'PilgrimSandals': art('Treasure', lambda c: [(boot(c), LEATHER)]),
    'RoadBoots': art('Minor', lambda c: [(boot(c), (0.4, 0.26, 0.14)), (capsule(c, 20, 40, 34, 40, 1.4), BRONZE)]),
    'LeagueBoots': art('Major', lambda c: [(boot(c), (0.35, 0.18, 0.12)), (wing(c), WHITE)]),
    'RingOfInsight': art('Treasure', lambda c: [(ring(c), STEEL), gem(c, 32, 15, 5, (0.8, 0.95, 1.0))]),
    'RingOfFortune': art('Treasure', lambda c: [(ring(c), GOLD), gem(c, 32, 15, 5, GREEN)]),
    'RingOfVigour': art('Minor', lambda c: [(ring(c), BRONZE), gem(c, 32, 15, 5, RED)]),
    'SignetOfKings': art('Major', lambda c: [(ring(c), GOLD), gem(c, 32, 15, 6, PURPLE)]),
    'CloverCharm': art('Treasure', lambda c: [(clover(c), GREEN)]),
    'EverfullFlask': art('Minor', lambda c: [(flask(c), (0.75, 0.85, 0.95)), (circle(c, 32, 42, 9), BLUE)]),
    'HornOfTheHost': art('Major', lambda c: [(horn(c), (0.9, 0.85, 0.7)), (capsule(c, 26, 26, 30, 36, 2.0), GOLD)]),
    'OrbOfDeepEarth': art('Major', lambda c: [(orb(c), (0.45, 0.32, 0.18)), (rock(c, 32, 32, 0.4), (0.7, 0.6, 0.4)), (capsule(c, 18, 50, 46, 50, 3.0), GOLD)]),
    'HeartOfEmbers': art('Major', lambda c: [(flame(c, 32, 34, 0.9), (1.0, 0.35, 0.1)), (inner_flame(c, 32, 34, 0.9), (1.0, 0.85, 0.3))]),
    'BookOfMists': art('Major', lambda c: [(book(c), (0.3, 0.45, 0.6)), (drop(c, 32, 30, 0.45), (0.7, 0.9, 1.0))]),
    'SeekerLantern': art('Minor', lambda c: [(lantern(c), BRONZE), (circle(c, 32, 36, 7), (1.0, 0.85, 0.4))]),
}

# name, drawing, pixels, kind
ICONS = [('Art' + name, make, 64, 'artifact') for name, make in ARTIFACTS.items()]
