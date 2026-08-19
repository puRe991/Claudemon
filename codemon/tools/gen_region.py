#!/usr/bin/env python3
"""
Region generator for Cod-e-mon / Monsta engine.

Emits one .map file per area of the region plus a pipe-delimited manifest
(kanto.region) that wires the areas together with warps. Map files use the
engine's format: a "W,H" header line followed by H rows of comma-separated
tile ids (see Tile::tile in Tile.h).

Everything here is data authoring: the geography (which area sits where and
what connects to what) is expressed in AREAS and CONNECTIONS below and matches
the described south->north / coastal / island structure of the region.
"""

import os

# ---- tile ids (must match Tile.h) -----------------------------------------
SHORT_GRASS = 0
LONG_GRASS  = 1
PATH        = 2
WATER       = 3
SAND        = 4
TREE        = 5
ROCK        = 6
FLOWER      = 7
BUILDING    = 8
FLOOR       = 9
WALL        = 10
CAVE_FLOOR  = 11
CAVE_WALL   = 12
LEDGE       = 13
BRIDGE      = 14
STAIRS      = 15
WARP        = 16
SIGN        = 17
ICE         = 18
LAVA        = 19
DEEP_WATER  = 20

W, H = 15, 13                      # standard area size (fits the 15/16 window)
CX, CY = W // 2, H // 2            # centre tile

# Writes into the sibling region/ data directory (codemon/region).
OUT_DIR = os.path.normpath(os.path.join(os.path.dirname(__file__), "..", "region"))
MAPS_DIR = os.path.join(OUT_DIR, "maps")


# ---------------------------------------------------------------------------
# Theme table: base fill, border blocker, scatter tile (+ density), spine tile.
# A theme paints the terrain character of an area; features and warps are laid
# on top afterwards.
# ---------------------------------------------------------------------------
THEMES = {
    "village":     dict(base=SHORT_GRASS, border=TREE,      scatter=FLOWER,     dens=6,  spine=PATH),
    "route_grass": dict(base=SHORT_GRASS, border=TREE,      scatter=LONG_GRASS, dens=4,  spine=PATH),
    "city":        dict(base=PATH,        border=BUILDING,  scatter=SHORT_GRASS,dens=6,  spine=PATH),
    "forest":      dict(base=LONG_GRASS,  border=TREE,      scatter=TREE,       dens=3,  spine=PATH),
    "rocky_city":  dict(base=PATH,        border=ROCK,      scatter=BUILDING,   dens=7,  spine=PATH),
    "rocky_route": dict(base=SHORT_GRASS, border=ROCK,      scatter=ROCK,       dens=5,  spine=PATH),
    "cave":        dict(base=CAVE_FLOOR,  border=CAVE_WALL, scatter=ROCK,       dens=6,  spine=CAVE_FLOOR),
    "water_city":  dict(base=PATH,        border=BUILDING,  scatter=WATER,      dens=6,  spine=PATH),
    "bridge":      dict(base=WATER,       border=WATER,     scatter=WATER,      dens=0,  spine=BRIDGE),
    "port_city":   dict(base=PATH,        border=BUILDING,  scatter=BUILDING,   dens=5,  spine=PATH),
    "ship":        dict(base=FLOOR,       border=WALL,      scatter=WALL,       dens=6,  spine=FLOOR),
    "tunnel":      dict(base=CAVE_FLOOR,  border=CAVE_WALL, scatter=CAVE_WALL,  dens=3,  spine=CAVE_FLOOR),
    "quiet_town":  dict(base=PATH,        border=TREE,      scatter=BUILDING,   dens=5,  spine=PATH),
    "tower":       dict(base=FLOOR,       border=WALL,      scatter=SIGN,       dens=7,  spine=FLOOR),
    "commercial":  dict(base=PATH,        border=BUILDING,  scatter=BUILDING,   dens=8,  spine=PATH),
    "underground": dict(base=FLOOR,       border=WALL,      scatter=WALL,       dens=9,  spine=FLOOR),
    "metropolis":  dict(base=PATH,        border=BUILDING,  scatter=BUILDING,   dens=9,  spine=PATH),
    "skyscraper":  dict(base=FLOOR,       border=WALL,      scatter=WALL,       dens=7,  spine=FLOOR),
    "nature_city": dict(base=PATH,        border=TREE,      scatter=LONG_GRASS, dens=5,  spine=PATH),
    "safari":      dict(base=LONG_GRASS,  border=TREE,      scatter=WATER,      dens=5,  spine=SHORT_GRASS),
    "cycling":     dict(base=PATH,        border=ROCK,      scatter=LEDGE,      dens=4,  spine=PATH),
    "sea_cave":    dict(base=CAVE_FLOOR,  border=CAVE_WALL, scatter=ICE,        dens=5,  spine=CAVE_FLOOR),
    "volcano":     dict(base=SAND,        border=ROCK,      scatter=LAVA,       dens=5,  spine=PATH),
    "mansion":     dict(base=FLOOR,       border=WALL,      scatter=WALL,       dens=8,  spine=FLOOR),
    "power":       dict(base=FLOOR,       border=WALL,      scatter=SIGN,       dens=6,  spine=FLOOR),
    "sea_route":   dict(base=DEEP_WATER,  border=DEEP_WATER,scatter=WATER,      dens=2,  spine=SAND),
    "victory":     dict(base=CAVE_FLOOR,  border=CAVE_WALL, scatter=ROCK,       dens=7,  spine=CAVE_FLOOR),
    "plateau":     dict(base=PATH,        border=ROCK,      scatter=BUILDING,   dens=6,  spine=PATH),
}


# ---------------------------------------------------------------------------
# Areas: id -> (display name, theme, start_x, start_y, coast_south?)
# coast_south lays a beach/sea band along the south edge for coastal places.
# ---------------------------------------------------------------------------
AREAS = {
    "pallet_town":     ("Pallet Town",      "village",     CX, CY, True),
    "route_01":        ("Route 1",          "route_grass", CX, CY, False),
    "viridian_city":   ("Viridian City",    "city",        CX, CY, False),
    "viridian_forest": ("Viridian Forest",  "forest",      CX, CY, False),
    "pewter_city":     ("Pewter City",      "rocky_city",  CX, CY, False),
    "route_03":        ("Route 3",          "rocky_route", CX, CY, False),
    "mt_moon":         ("Mt. Moon",         "cave",        1,  CY, False),
    "cerulean_city":   ("Cerulean City",    "water_city",  CX, CY, False),
    "nugget_bridge":   ("Nugget Bridge",    "bridge",      CX, H-2, True),
    "vermilion_city":  ("Vermilion City",   "port_city",   CX, 2,  True),
    "ss_anne":         ("S.S. Anne",        "ship",        CX, H-2, False),
    "diglett_cave":    ("Diglett's Cave",   "tunnel",      CX, H-2, False),
    "route_11":        ("Route 11",         "route_grass", 1,  CY, False),
    "rock_tunnel":     ("Rock Tunnel",      "cave",        1,  CY, False),
    "lavender_town":   ("Lavender Town",    "quiet_town",  CX, CY, False),
    "pokemon_tower":   ("Pokemon Tower",    "tower",       CX, H-2, False),
    "celadon_city":    ("Celadon City",     "commercial",  CX, CY, False),
    "rocket_hideout":  ("Rocket Hideout",   "underground", CX, H-2, False),
    "saffron_city":    ("Saffron City",     "metropolis",  CX, CY, False),
    "silph_co":        ("Silph Co.",        "skyscraper",  CX, H-2, False),
    "fuchsia_city":    ("Fuchsia City",     "nature_city", CX, CY, False),
    "safari_zone":     ("Safari Zone",      "safari",      CX, H-2, False),
    "cycling_road":    ("Cycling Road",     "cycling",     CX, CY, False),
    "seafoam_islands": ("Seafoam Islands",  "sea_cave",    1,  CY, False),
    "cinnabar_island": ("Cinnabar Island",  "volcano",     CX, CY, True),
    "pokemon_mansion": ("Pokemon Mansion",  "mansion",     CX, H-2, False),
    "power_plant":     ("Power Plant",      "power",       CX, H-2, False),
    "sea_route":       ("Sea Route",        "sea_route",   CX, CY, False),
    "victory_road":    ("Victory Road",     "victory",     W-2, CY, False),
    "indigo_plateau":  ("Indigo Plateau",   "plateau",     CX, H-2, False),
}


# ---------------------------------------------------------------------------
# Connections. Each entry warps between area A at (ax,ay) and area B at (bx,by).
# 'edge' connections are the geographic road/sea links; 'door' connections are
# a town entering one of its buildings/dungeons. All are two-way: stepping onto
# a warp tile drops you just inside the far side.
# ---------------------------------------------------------------------------
N = (CX, 0)
S = (CX, H - 1)
E = (W - 1, CY)
Wd = (0, CY)

CONNECTIONS = [
    # ---- southern rural chain, growing northward -------------------------
    ("pallet_town",    N,       "route_01",        S),
    ("route_01",       N,       "viridian_city",   S),
    ("viridian_city",  N,       "viridian_forest", S),
    ("viridian_forest",N,       "pewter_city",     S),
    # ---- west: Pewter -> Mt. Moon -> Cerulean ----------------------------
    ("pewter_city",    E,       "route_03",        Wd),
    ("route_03",       E,       "mt_moon",         Wd),
    ("mt_moon",        E,       "cerulean_city",   Wd),
    # ---- Cerulean hub: north bridge, east tunnel, south to centre --------
    ("cerulean_city",  N,       "nugget_bridge",   S),
    ("cerulean_city",  E,       "rock_tunnel",     Wd),
    ("rock_tunnel",    S,       "lavender_town",   N),
    ("rock_tunnel",    N,       "power_plant",     S),
    ("cerulean_city",  S,       "saffron_city",    N),
    # ---- central metropolis and its neighbours ---------------------------
    ("saffron_city",   S,       "vermilion_city",  N),
    ("saffron_city",   Wd,      "celadon_city",    E),
    ("saffron_city",   E,       "lavender_town",   Wd),
    ("saffron_city",   (CX, 2), "silph_co",        S),  # door -> skyscraper
    # ---- Lavender's tower ------------------------------------------------
    ("lavender_town",  (CX, 3), "pokemon_tower",   S),
    # ---- Celadon: dept-store district + hideout under the game corner ----
    ("celadon_city",   (4, 4),  "rocket_hideout",  S),
    ("celadon_city",   S,       "cycling_road",    N),
    # ---- Vermilion port: the ship, Diglett's Cave, Route 11 --------------
    ("vermilion_city", (10, 3), "ss_anne",         S),
    ("vermilion_city", E,       "route_11",        Wd),
    ("route_11",       N,       "diglett_cave",    S),
    ("diglett_cave",   N,       "route_01",        (2, 2)),       # far end near Viridian
    # ---- Cycling Road drops south into Fuchsia ---------------------------
    ("cycling_road",   S,       "fuchsia_city",    N),
    ("fuchsia_city",   (CX, 3), "safari_zone",     S),
    # ---- south into open sea and the islands -----------------------------
    ("fuchsia_city",   S,       "sea_route",       N),
    ("sea_route",      S,       "seafoam_islands", N),
    ("sea_route",      Wd,      "cinnabar_island", E),
    ("seafoam_islands",S,       "cinnabar_island", N),
    ("cinnabar_island",(4, 6),  "pokemon_mansion", (CX, H - 2)),
    ("sea_route",      E,       "pallet_town",     S),            # closes the loop
    # ---- the western barrier and the final complex -----------------------
    ("viridian_city",  Wd,      "victory_road",    E),
    ("victory_road",   Wd,      "indigo_plateau",  E),
]


# ---------------------------------------------------------------------------
def blank(theme):
    t = THEMES[theme]
    g = [[t["base"]] * W for _ in range(H)]
    # border
    for x in range(W):
        g[0][x] = t["border"]
        g[H - 1][x] = t["border"]
    for y in range(H):
        g[y][0] = t["border"]
        g[y][W - 1] = t["border"]
    return g


def scatter(g, theme):
    """Deterministic pseudo-random scatter of the theme's feature tile."""
    t = THEMES[theme]
    if t["dens"] <= 0:
        return
    seed = sum(ord(c) for c in theme) + 7
    for y in range(1, H - 1):
        for x in range(1, W - 1):
            seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF
            if (seed >> 8) % t["dens"] == 0:
                # never bury the centre / spine column
                if x == CX or y == CY:
                    continue
                g[y][x] = t["scatter"]


def carve_spine(g, theme):
    """A walkable cross through the centre so every edge is reachable."""
    sp = THEMES[theme]["spine"]
    for x in range(1, W - 1):
        g[CY][x] = sp
    for y in range(1, H - 1):
        g[y][CX] = sp


def coast(g):
    """A beach + sea band along the south edge for coastal areas."""
    for x in range(1, W - 1):
        g[H - 2][x] = SAND
    for x in range(W):
        g[H - 1][x] = WATER


def inward(x, y):
    """The walkable tile just inside an edge warp at (x,y)."""
    nx, ny = x, y
    if x == 0:
        nx = 1
    elif x == W - 1:
        nx = W - 2
    if y == 0:
        ny = 1
    elif y == H - 1:
        ny = H - 2
    return nx, ny


def carve_to_centre(g, x, y, theme):
    """Ensure a warp at (x,y) is reachable: punch a walkable line to centre."""
    sp = THEMES[theme]["spine"]
    ix, iy = inward(x, y)
    cx, cy = x, y
    # step inward first
    g[iy][ix] = sp
    # walk toward centre column then centre row
    while cx != CX:
        cx += 1 if cx < CX else -1
        if 0 < cx < W - 1 and 0 < y < H - 1:
            g[y][cx] = sp
    while cy != CY:
        cy += 1 if cy < CY else -1
        if 0 < CX < W - 1 and 0 < cy < H - 1:
            g[cy][CX] = sp


# ---------------------------------------------------------------------------
def build():
    os.makedirs(MAPS_DIR, exist_ok=True)

    # collect the warps that live on each area's map
    per_area = {aid: [] for aid in AREAS}
    for a, (ax, ay), b, (bx, by) in CONNECTIONS:
        per_area[a].append((ax, ay))
        per_area[b].append((bx, by))

    for aid, (name, theme, sx, sy, coast_south) in AREAS.items():
        g = blank(theme)
        scatter(g, theme)
        carve_spine(g, theme)
        if coast_south:
            coast(g)
        # stamp warps and make sure each is reachable
        # keep the spawn tile walkable, then stamp warps so they always survive
        g[sy][sx] = THEMES[theme]["spine"]
        for (wx, wy) in per_area[aid]:
            carve_to_centre(g, wx, wy, theme)
            g[wy][wx] = WARP

        path = os.path.join(MAPS_DIR, aid + ".map")
        with open(path, "w") as f:
            f.write(f"{W},{H}\n")
            for row in g:
                f.write(",".join(str(v) for v in row) + "\n")

    # manifest
    manifest = os.path.join(OUT_DIR, "kanto.region")
    with open(manifest, "w") as f:
        f.write("# Kanto-style region manifest for Cod-e-mon (Monsta engine).\n")
        f.write("# Pipe-delimited. Loaded by Region (Region.cpp).\n")
        f.write("#\n")
        f.write("#   AREA|<id>|<Display Name>|<map path>|<start_x>|<start_y>\n")
        f.write("#   WARP|<from id>|<x>|<y>|<to id>|<dest_x>|<dest_y>\n")
        f.write("#\n")
        f.write("# START names the area the player spawns in.\n")
        f.write("START|pallet_town\n")
        f.write("#\n# ---- areas ----\n")
        for aid, (name, theme, sx, sy, _c) in AREAS.items():
            f.write(f"AREA|{aid}|{name}|region/maps/{aid}.map|{sx}|{sy}\n")
        f.write("#\n# ---- warps (two-way) ----\n")
        for a, (ax, ay), b, (bx, by) in CONNECTIONS:
            adx, ady = inward(bx, by)   # arriving in B lands just inside B's warp
            bdx, bdy = inward(ax, ay)
            f.write(f"WARP|{a}|{ax}|{ay}|{b}|{adx}|{ady}\n")
            f.write(f"WARP|{b}|{bx}|{by}|{a}|{bdx}|{bdy}\n")

    print(f"areas={len(AREAS)} connections={len(CONNECTIONS)} "
          f"warps={len(CONNECTIONS)*2}")
    print("out:", OUT_DIR)


if __name__ == "__main__":
    build()
