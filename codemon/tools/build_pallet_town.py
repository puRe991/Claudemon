#!/usr/bin/env python3
"""
Hand-authored Pallet Town map (a proper town: two houses, Professor Oak's lab,
a sign, grass, a beach to the south, a path leading north to Route 1) instead
of the procedural fill. Writes region/maps/pallet_town.map.

The town keeps the warp tiles the manifest expects:
  (7,0)  north  -> Route 1     (the path opening in the tree line)
  (7,12) south  -> Sea Route   (the little dock in the beach)
and the player spawns at (7,6) on the main path.

Legend below maps ASCII -> Tile::tile ids (see Tile.h). Run:
  python3 codemon/tools/build_pallet_town.py
"""
import os

LEGEND = {
    'T': 5,   # tree
    '.': 0,   # short_grass
    ',': 1,   # long_grass
    '#': 2,   # path
    '_': 4,   # sand (beach / dock)
    '~': 3,   # water
    '*': 7,   # flower
    'S': 17,  # sign
    'g': 21,  # roof_green   (house A)
    'p': 22,  # roof_purple  (house B)
    'r': 23,  # roof_red     (Oak's lab)
    'W': 8,   # building wall
    'N': 24,  # wall_window
    'D': 16,  # door / warp tile
}

# 15 wide x 13 tall. Each row MUST be exactly 15 characters.
ROWS = [
    "T......#......T",  # 0  tree line, north exit path at x=7 -> Route 1
    "T......#......T",  # 1
    "T.gggg.#.pppp.T",  # 2  house A (green) x2-5 | house B (purple) x9-12
    "T.gggg.#.pppp.T",  # 3
    "T.WDNW.#.WDNW.T",  # 4  walls + doors (A door x3, B door x10)
    "T..########...T",  # 5  path linking both doorsteps to the main path
    "T.S....#...*..T",  # 6  town sign; player spawns at (7,6)
    "T,.#########..T",  # 7  path spreads out to skirt the lab
    "T,.#.rrrrr.#..T",  # 8  Oak's lab roof (red) x5-9, side paths x3/x11
    "T,.#.rrrrr.#..T",  # 9
    "T..#.WWDWW.#..T",  # 10 lab wall + front door at x7
    "T_____________T",  # 11 beach
    "~~~~~~~_~~~~~~~",  # 12 sea, dock at x7 -> Sea Route
]

OUT = os.path.normpath(os.path.join(
    os.path.dirname(__file__), "..", "region", "maps", "pallet_town.map"))


def main():
    assert len(ROWS) == 13, "expected 13 rows"
    grid = []
    for y, row in enumerate(ROWS):
        assert len(row) == 15, f"row {y} is {len(row)} chars, need 15: {row!r}"
        grid.append([LEGEND[c] for c in row])

    # sanity: the two area-warp tiles must be walkable (not tree/water/etc.)
    for (wx, wy) in [(7, 0), (7, 12)]:
        assert grid[wy][wx] in (2, 4, 16), f"warp ({wx},{wy}) not walkable"

    with open(OUT, "w") as f:
        f.write("15,13\n")
        for row in grid:
            f.write(",".join(str(v) for v in row) + "\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
