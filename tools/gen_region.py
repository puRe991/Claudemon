#!/usr/bin/env python3
"""Generate the original starter region (codemon/maps/region_00.txt).

Original layout (a fenced town plot, paths, a pond, a tall-grass route),
authored here - not a reproduction of any existing game's map.
Tile ids match gen_overworld_tileset.py.
"""
import os
W, H = 40, 30
g = [[0]*W for _ in range(H)]

def rect(x0, y0, x1, y1, v):
    for y in range(max(0, y0), min(H, y1+1)):
        for x in range(max(0, x0), min(W, x1+1)):
            g[y][x] = v

for x in range(W): g[0][x] = 2; g[H-1][x] = 2
for y in range(H): g[y][0] = 2; g[y][W-1] = 2
rect(1, 1, 3, 2, 2); rect(W-4, 1, W-2, 2, 2); rect(1, H-3, 3, H-2, 2); rect(W-4, H-3, W-2, H-2, 2)

for y in range(H):
    for x in range(W):
        dx = (x-30)/5.0; dy = (y-8)/3.5
        if dx*dx + dy*dy < 1.0: g[y][x] = 3
        elif dx*dx + dy*dy < 1.6 and g[y][x] == 0: g[y][x] = 4

rect(5, 4, 13, 11, 0)
for x in range(5, 14): g[4][x] = 7; g[11][x] = 7
for y in range(4, 12): g[y][5] = 7; g[y][13] = 7
g[11][9] = 5
for (fx, fy) in [(7,6),(9,6),(11,6),(7,9),(11,9),(9,8)]: g[fy][fx] = 6

for y in range(11, H-1): g[y][9] = 5
for x in range(1, W-1): g[18][x] = 5
for x in range(9, 27):
    if g[8][x] == 0: g[8][x] = 5

rect(12, 21, 24, 27, 1)
for y in range(21, 28):
    if g[y][9] != 2: g[y][9] = 5

for (tx, ty) in [(20,5),(24,14),(15,15),(30,20),(34,24),(6,22),(26,26)]:
    if g[ty][tx] == 0: g[ty][tx] = 2
for (fx, fy) in [(3,14),(4,15),(16,10),(17,11)]:
    if g[fy][fx] == 0: g[fy][fx] = 6

start = (9, 13); g[start[1]][start[0]] = 5
out = os.path.join(os.path.dirname(__file__), "..", "codemon", "maps", "region_00.txt")
with open(out, "w") as f:
    f.write(f"{W},{H},{start[0]},{start[1]}\n")
    for row in g:
        f.write(",".join(str(v) for v in row) + "\n")
print("wrote", os.path.normpath(out), W, "x", H, "start", start)
