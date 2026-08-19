#!/usr/bin/env python3
"""
Build the game's derived art from the licensed source packs:

  assets/art/mpwsp01_tileset.png  ->  region/region_tiles.png   (terrain sheet)
  assets/art/mpwsp01_trainer.png  ->  assets/Red_player.png     (player sheet)

Both sources are from scarloxy's "MyPixelWorld Special Packs #01" (CC-BY 4.0);
see assets/CREDITS.md. The engine samples region_tiles.png as one row of 32px
tiles indexed by Tile::tile id (Map::render_map), and the player sheet as a
3-frame x 4-direction grid in S,W,E,N row order (Character::update_sprite_pos).

Run:  python3 codemon/tools/build_tiles.py
Requires Pillow.
"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(__file__)
ART = os.path.normpath(os.path.join(HERE, "..", "assets", "art"))
TILESET = os.path.join(ART, "mpwsp01_tileset.png")
TRAINER = os.path.join(ART, "mpwsp01_trainer.png")
OUT_SHEET = os.path.normpath(os.path.join(HERE, "..", "region", "region_tiles.png"))
OUT_PLAYER = os.path.normpath(os.path.join(HERE, "..", "assets", "Red_player.png"))

SRC_TS = 16   # source tileset cell size
OUT_TS = 32   # engine tile size


def build_tileset():
    im = Image.open(TILESET).convert("RGBA")

    def cell(c, r):
        return im.crop((c * SRC_TS, r * SRC_TS, c * SRC_TS + SRC_TS, r * SRC_TS + SRC_TS))

    grass = cell(0, 0)

    def real(c, r, bg=None):
        base = bg.copy() if bg is not None else Image.new("RGBA", (SRC_TS, SRC_TS), (0, 0, 0, 0))
        base.alpha_composite(cell(c, r))
        return base

    def flat(rgb):
        t = Image.new("RGBA", (SRC_TS, SRC_TS), rgb + (255,))
        d = ImageDraw.Draw(t)
        dk = tuple(int(v * 0.8) for v in rgb)
        lt = tuple(min(255, int(v * 1.12)) for v in rgb)
        d.rectangle([0, 0, SRC_TS - 1, SRC_TS - 1], outline=dk + (255,))
        d.line([1, 1, SRC_TS - 2, 1], fill=lt + (255,))
        return t

    # Tile::tile id -> 16x16 source tile. Real cells picked from the tileset for
    # the common terrains; matching solids for the few fills the pack lacks.
    M = {
        0:  real(0, 0),            # short_grass
        1:  real(40, 17),          # long_grass (dense foliage)
        2:  real(7, 17),           # path (dirt/sand)
        3:  real(26, 17),          # water
        4:  real(6, 17),           # sand
        5:  real(1, 20, bg=grass), # tree (canopy over grass)
        6:  flat((128, 116, 98)),  # rock
        7:  real(9, 20),           # flower (flowerbed on grass)
        8:  real(32, 12),          # building (house wall)
        9:  real(48, 8),           # floor (interior)
        10: real(48, 11),          # wall (interior)
        11: flat((150, 140, 120)), # cave_floor
        12: flat((72, 62, 54)),    # cave_wall
        13: flat((150, 110, 70)),  # ledge
        14: flat((170, 120, 60)),  # bridge
        15: flat((185, 180, 175)), # stairs
        16: real(31, 13),          # warp (door)
        17: flat((120, 80, 46)),   # sign
        18: real(2, 0),            # ice (snow)
        19: real(43, 18),          # lava (red)
        20: flat((28, 66, 140)),   # deep_water
    }
    n = len(M)
    sheet = Image.new("RGBA", (n * OUT_TS, OUT_TS), (0, 0, 0, 0))
    for i in range(n):
        sheet.paste(M[i].resize((OUT_TS, OUT_TS), Image.NEAREST), (i * OUT_TS, 0))
    sheet.save(OUT_SHEET)
    print("wrote", OUT_SHEET, sheet.size)


def build_player():
    ow = Image.open(TRAINER).convert("RGBA")

    def frame(c, r):
        return ow.crop((c * OUT_TS, r * OUT_TS, c * OUT_TS + OUT_TS, r * OUT_TS + OUT_TS))

    # Source rows are Down, Left, Right, Up == the engine's facing order S,W,E,N.
    # Source columns are [stand, step, stand, step]; take stand + both steps for
    # a 3-frame walk cycle (engine cycles frame index 0..2).
    src_cols = [0, 1, 3]
    out = Image.new("RGBA", (3 * OUT_TS, 4 * OUT_TS), (0, 0, 0, 0))
    for row in range(4):
        for dst_c, src_c in enumerate(src_cols):
            out.paste(frame(src_c, row), (dst_c * OUT_TS, row * OUT_TS))
    out.save(OUT_PLAYER)
    print("wrote", OUT_PLAYER, out.size)


if __name__ == "__main__":
    build_tileset()
    build_player()
