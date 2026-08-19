#!/usr/bin/env python3
"""Generate the original overworld tileset (codemon/maps/map_set/overworld.png).

All artwork here is generated procedurally and is original to this project
(no third-party/ripped assets). 8 tiles, 32x32 each, laid out horizontally so a
tile's integer id is its column in the sheet.

  0 grass  1 tall grass  2 tree(solid)  3 water(solid)
  4 sand   5 path/dirt    6 flowers      7 fence(solid)
"""
from PIL import Image, ImageDraw
import random, os

T, N = 32, 8
img = Image.new("RGBA", (T*N, T), (0, 0, 0, 0))
d = ImageDraw.Draw(img)
GRASS, GRASS_D = (104, 192, 104), (84, 168, 88)

def rnd(seed): return random.Random(seed)
def base(ox, c): d.rectangle([ox, 0, ox+T-1, T-1], fill=c)
def speck(ox, c, n, seed):
    r = rnd(seed)
    for _ in range(n):
        d.point((ox + r.randint(0, T-1), r.randint(0, T-1)), fill=c)

base(0*T, GRASS); speck(0*T, GRASS_D, 40, 1)

base(1*T, GRASS); speck(1*T, GRASS_D, 30, 2)
r = rnd(3)
for _ in range(22):
    x = 1*T + r.randint(1, T-2); y = r.randint(6, T-2)
    d.line([(x, y), (x, y-5)], fill=(56, 132, 60))

base(2*T, GRASS); speck(2*T, GRASS_D, 20, 4)
cx = 2*T + 16
d.rectangle([cx-3, 20, cx+2, 29], fill=(120, 78, 44))
d.ellipse([2*T+4, 2, 2*T+27, 24], fill=(46, 124, 60))
d.ellipse([2*T+8, 5, 2*T+20, 18], fill=(72, 160, 84))
speck(2*T, (36, 104, 52), 26, 5)

base(3*T, (64, 128, 216))
for y in range(4, T, 8):
    d.line([(3*T+2, y), (3*T+T-3, y)], fill=(120, 176, 240))
    d.line([(3*T+6, y+2), (3*T+14, y+2)], fill=(150, 196, 248))

base(4*T, (226, 210, 150)); speck(4*T, (206, 186, 124), 40, 6)

base(5*T, (198, 166, 120)); speck(5*T, (176, 142, 102), 50, 7)
d.rectangle([5*T, 0, 5*T+T-1, T-1], outline=(170, 138, 98))

base(6*T, GRASS); speck(6*T, GRASS_D, 24, 8)
r = rnd(9); cols = [(220, 64, 64), (240, 216, 72), (248, 248, 248), (200, 96, 220)]
for _ in range(10):
    x = 6*T + r.randint(3, T-4); y = r.randint(3, T-4)
    c = r.choice(cols); d.ellipse([x-1, y-1, x+1, y+1], fill=c)

base(7*T, GRASS); speck(7*T, GRASS_D, 18, 10)
d.rectangle([7*T, 13, 7*T+T-1, 18], fill=(150, 104, 60))
for px in (7*T+5, 7*T+15, 7*T+25):
    d.rectangle([px, 6, px+3, 27], fill=(120, 80, 44))

out = os.path.join(os.path.dirname(__file__), "..", "codemon", "maps", "map_set", "overworld.png")
img.save(out)
print("wrote", os.path.normpath(out), img.size)
