#!/usr/bin/env python3
"""
Hand-authored Pallet Town: a proper town built from multi-tile stamps (whole
houses, Professor Oak's lab, trees) laid over a terrain base layer, instead of
single repeated cells. Writes region/maps/pallet_town.map.

Warp tiles the manifest expects stay walkable:
  (7,0)  north  -> Route 1     (path opening in the tree line)
  (7,12) south  -> Sea Route   (dock in the beach)
Player spawns at (7,6) on the main path.

Run:  python3 codemon/tools/build_pallet_town.py   (after build_tiles.py)
"""
import os
from stamps_def import stamp_ids

IDS = stamp_ids()

# base terrain characters (single-tile ids); 'b'/'F' are 1x1 stamps
BASE = {
    'T': 5,             # tree (border)
    '.': 0,             # short_grass
    ',': 1,             # long_grass
    '#': 2,             # path
    '_': 4,             # sand (beach / dock)
    '~': 3,             # water
    '*': 7,             # flower
    'S': 17,            # sign
    'b': IDS['bush'][0],
    'F': IDS['fence_h'][0],
}

# 15 wide x 13 tall base layer (terrain only; buildings stamped on top).
BASE_ROWS = [
    "TTTTTTT#TTTTTTT",  # 0  north exit at x=7
    "T.....b#b.....T",  # 1  bushes flanking the entrance
    "T......#......T",  # 2
    "T......#......T",  # 3
    "T.....S#......T",  # 4  town sign beside the path
    "T.#########...T",  # 5  path linking both doorsteps (x2..x10)
    "T..#########..T",  # 6  connector to the side lanes (x3..x11)
    "T..#b.....b#..T",  # 7  side lanes x3/x11; bushes beside the lab
    "T..#*.....*#..T",  # 8  flowerbeds beside the lab
    "T.F#.......#F.T",  # 9  garden fence edges
    "T.*#.......#*.T",  # 10 garden flowers
    "T_____________T",  # 11 beach
    "~~~~~~~_~~~~~~~",  # 12 sea, dock at x7 -> Sea Route
]

# stamps: (name, top-left x, y)
PLACE = [
    ("house_green", 1, 1),    # player's house  (door -> (2,4))
    ("house_purple", 9, 1),   # rival's house   (door -> (10,4))
    ("lab_red", 5, 7),        # Oak's lab        (door -> (6,10))
    ("tree", 1, 6),           # decorative trees
    ("tree", 12, 6),
]

OUT = os.path.normpath(os.path.join(
    os.path.dirname(__file__), "..", "region", "maps", "pallet_town.map"))
W, H = 15, 13


def main():
    assert len(BASE_ROWS) == H
    grid = []
    for y, row in enumerate(BASE_ROWS):
        assert len(row) == W, f"row {y} width {len(row)}: {row!r}"
        grid.append([BASE[c] for c in row])

    def place(name, x, y):
        base_id, w, h = IDS[name]
        for rr in range(h):
            for cc in range(w):
                grid[y + rr][x + cc] = base_id + rr * w + cc

    for name, x, y in PLACE:
        place(name, x, y)

    # area-warp tiles must stay walkable
    for (wx, wy) in [(7, 0), (7, 12)]:
        assert grid[wy][wx] in (2, 4, 16), f"warp ({wx},{wy}) blocked = {grid[wy][wx]}"

    with open(OUT, "w") as f:
        f.write(f"{W},{H}\n")
        for row in grid:
            f.write(",".join(str(v) for v in row) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
