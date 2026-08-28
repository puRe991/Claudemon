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

    def gen_sign():
        t = grass.copy()
        d = ImageDraw.Draw(t)
        d.rectangle([7, 8, 8, 14], fill=(110, 74, 40))                       # post
        d.rectangle([3, 3, 12, 9], fill=(178, 140, 92), outline=(110, 74, 40))  # board
        d.line([5, 5, 10, 5], fill=(120, 84, 50))
        d.line([5, 7, 9, 7], fill=(120, 84, 50))
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
        17: gen_sign(),            # sign (drawn signpost)
        18: real(2, 0),            # ice (snow)
        19: real(43, 18),          # lava (red)
        20: flat((28, 66, 140)),   # deep_water
        21: real(31, 11),          # roof_green (house roof)
        22: real(31, 17),          # roof_purple (house roof)
        23: flat((196, 74, 60)),   # roof_red (lab / civic roof)
        24: real(33, 12),          # wall_window (house front with window)
    }
    # ---- multi-tile stamps (whole buildings, trees, fences) ----------------
    # Each stamp yields a list of 16x16 cells, row-major, composited over grass
    # so any transparent corners (gable roofs, tree canopies) show grass, not
    # black. Ids are assigned by stamps_def in the same order.
    def cells(c, r, w, h):
        out = []
        for rr in range(h):
            for cc in range(w):
                out.append(real(c + cc, r + rr, bg=grass))
        return out

    def recolor_roof_red(cell_list, w, h, roof_rows):
        # Turn greenish roof pixels red on the top `roof_rows` rows, keeping
        # the shading, so Oak's lab reads as a distinct red-roofed building.
        for idx, im2 in enumerate(cell_list):
            row = idx // w
            if row >= roof_rows:
                continue
            px = im2.load()
            for y in range(SRC_TS):
                for x in range(SRC_TS):
                    r, g, b, a = px[x, y]
                    if a > 0 and g > r and g > b and g > 60:
                        px[x, y] = (min(255, g), int(r * 0.5), int(b * 0.45), a)
        return cell_list

    def gen_bush():
        t = Image.new("RGBA", (SRC_TS, SRC_TS), (0, 0, 0, 0))
        d = ImageDraw.Draw(t)
        base = t.copy(); base.alpha_composite(grass); t = base
        d = ImageDraw.Draw(t)
        d.ellipse([2, 5, 13, 14], fill=(46, 120, 58), outline=(28, 84, 40))
        d.ellipse([4, 4, 9, 9], fill=(70, 150, 78))          # highlight
        t.putpixel((6, 6), (150, 200, 130, 255))
        return [t]

    def gen_fence_h():
        t = Image.new("RGBA", (SRC_TS, SRC_TS), (0, 0, 0, 0))
        base = t.copy(); base.alpha_composite(grass); t = base
        d = ImageDraw.Draw(t)
        wood, dk, lt = (150, 104, 58), (110, 74, 40), (186, 140, 92)
        d.rectangle([0, 4, SRC_TS - 1, 6], fill=wood)         # top rail
        d.rectangle([0, 10, SRC_TS - 1, 12], fill=wood)       # bottom rail
        d.line([0, 4, SRC_TS - 1, 4], fill=lt)
        for px in (2, 8, 14):                                 # posts
            d.rectangle([px, 2, px + 1, 15], fill=dk)
        return [t]

    from stamps_def import STAMPS, BASE_COUNT, total_tiles
    producers = {
        "house_green":  lambda: cells(30, 10, 5, 4),
        "house_purple": lambda: cells(30, 16, 5, 4),
        "lab_red":      lambda: recolor_roof_red(cells(30, 10, 5, 4), 5, 4, 2),
        "tree":         lambda: cells(0, 19, 2, 3),
        "bush":         gen_bush,
        "fence_h":      gen_fence_h,
    }

    n = total_tiles()
    sheet = Image.new("RGBA", (n * OUT_TS, OUT_TS), (0, 0, 0, 0))
    for i in range(BASE_COUNT):
        sheet.paste(M[i].resize((OUT_TS, OUT_TS), Image.NEAREST), (i * OUT_TS, 0))
    nid = BASE_COUNT
    for name, w, h in STAMPS:
        for cell_img in producers[name]():
            sheet.paste(cell_img.resize((OUT_TS, OUT_TS), Image.NEAREST), (nid * OUT_TS, 0))
            nid += 1
    assert nid == n, (nid, n)
    sheet.save(OUT_SHEET)
    print("wrote", OUT_SHEET, sheet.size, "(%d tiles)" % n)


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
