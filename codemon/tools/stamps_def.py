#!/usr/bin/env python3
"""
Shared registry of multi-tile "stamps" (whole houses, the lab, trees, and
generated fences/bushes) used by both build_tiles.py (which bakes their pixels
into region_tiles.png) and build_pallet_town.py (which places them on the map).

Base terrain occupies tile ids 0..BASE_COUNT-1 (see Tile.h). Each stamp is a
w x h block of cells assigned consecutive ids, row-major, starting right after
the base range. Keeping the order/sizes here means the two scripts never drift.
"""

BASE_COUNT = 25  # ids 0..24 are single-tile terrain (Tile::tile)

# (name, width, height) in tiles. Order defines id assignment.
STAMPS = [
    ("house_green",  5, 4),   # player's house
    ("house_purple", 5, 4),   # rival's house
    ("lab_red",      5, 4),   # Professor Oak's lab (green house, red roof)
    ("tree",         2, 3),   # leafy decorative tree (bush base)
    ("bush",         1, 1),   # small shrub
    ("fence_h",      1, 1),   # horizontal wooden fence rail
]


def stamp_ids():
    """name -> (base_id, w, h)."""
    out = {}
    nid = BASE_COUNT
    for name, w, h in STAMPS:
        out[name] = (nid, w, h)
        nid += w * h
    return out


def total_tiles():
    return BASE_COUNT + sum(w * h for _, w, h in STAMPS)
