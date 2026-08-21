#!/usr/bin/env python3
"""
pe_import.py - Codemon asset import pipeline.

This is the SFML-friendly replacement for the pokeemerald GBA build tools
(gbagfx / wav2agb / mid2agb). Those tools turn PNG+palette / WAV / MIDI into
GBA-specific binary blobs that only mean something to a Game Boy Advance ROM.
Codemon is a desktop C++/SFML game, so instead of GBA blobs we produce plain,
engine-ready assets:

  * tilesets  -> fully coloured 16x16 metatile sheets (RGBA PNG) + JSON meta
                 (replaces gbagfx's 4bpp+palette handling and the GBA metatile
                  renderer, done here in pure Python)
  * overworld -> every character / NPC / pokemon walking sheet, flattened from
                 4bpp-indexed to RGBA with a transparent backdrop
  * audio     -> pokemon cries copied as SFML-loadable WAV, plus generated
                 step / bump / select blips. MIDI songs are converted to OGG
                 when a synth (fluidsynth/timidity) is available, otherwise the
                 .mid files are copied and a note is written explaining that a
                 synth is required (SFML cannot play MIDI directly).

Usage:
    python3 pe_import.py all       --src /path/to/pokeemerald-master
    python3 pe_import.py tilesets  --src ...
    python3 pe_import.py overworld --src ...
    python3 pe_import.py audio     --src ...

Only dependency: Pillow (pip install Pillow).
"""
import argparse, json, os, shutil, struct, subprocess, sys, wave, math, glob

from PIL import Image

# Repository-relative output roots (this file lives in codemon/tools/).
TOOLS_DIR   = os.path.dirname(os.path.abspath(__file__))
ASSETS_DIR  = os.path.normpath(os.path.join(TOOLS_DIR, "..", "assets"))
OW_DIR      = os.path.join(ASSETS_DIR, "overworld")
TS_DIR      = os.path.join(ASSETS_DIR, "tilesets")
SFX_DIR     = os.path.join(ASSETS_DIR, "sfx")

# Primary tilesets are self-contained (own palettes 0-5) so they colourise
# correctly on their own. Secondary tilesets borrow the primary's palettes and
# need a paired primary, so we render the useful primaries here.
PRIMARY_TILESETS = ["general", "building", "secret_base"]

METATILE_SIZE = 16          # px, a metatile is 2x2 hardware tiles
TILE          = 8           # px, one hardware tile
SHEET_COLS    = 16          # metatiles per row in the output sheet


# --------------------------------------------------------------------------- #
# palette handling  (JASC-PAL, the format gbagfx consumes)
# --------------------------------------------------------------------------- #
def load_jasc_pal(path):
    """Return a list of (r,g,b) tuples from a JASC-PAL file."""
    with open(path) as f:
        lines = [l.strip() for l in f if l.strip()]
    # lines[0]="JASC-PAL", [1]="0100", [2]=count, then r g b triples
    n = int(lines[2])
    out = []
    for i in range(n):
        r, g, b = (int(v) for v in lines[3 + i].split()[:3])
        out.append((r, g, b))
    while len(out) < 16:
        out.append((0, 0, 0))
    return out


def load_tileset_palettes(ts_path):
    """palettes/NN.pal -> {index: [(r,g,b)*16]}"""
    pals = {}
    pdir = os.path.join(ts_path, "palettes")
    for p in sorted(glob.glob(os.path.join(pdir, "*.pal"))):
        idx = int(os.path.splitext(os.path.basename(p))[0])
        pals[idx] = load_jasc_pal(p)
    return pals


# --------------------------------------------------------------------------- #
# tilesets -> coloured metatile sheets
# --------------------------------------------------------------------------- #
def read_tile_indices(tiles_png):
    """Return (cols, rows, index_grid) for a 4bpp-indexed tiles.png."""
    im = tiles_png.convert("P") if tiles_png.mode != "P" else tiles_png
    px = im.load()
    w, h = im.size
    return w // TILE, h // TILE, px, w, h


def blit_tile(dst, dst_x, dst_y, tiles_px, tiles_w, tile_id, palette, xflip, yflip,
              transparent_zero, tiles_h=None):
    """Draw one 8x8 hardware tile into dst (RGBA) at (dst_x,dst_y)."""
    tiles_per_row = tiles_w // TILE
    tx = (tile_id % tiles_per_row) * TILE
    ty = (tile_id // tiles_per_row) * TILE
    for y in range(TILE):
        sy = ty + (TILE - 1 - y if yflip else y)
        for x in range(TILE):
            sx = tx + (TILE - 1 - x if xflip else x)
            if sx >= tiles_w or (tiles_h is not None and sy >= tiles_h):
                continue   # tile id past the end of this sheet: leave clear
            idx = tiles_px[sx, sy]
            if transparent_zero and idx == 0:
                continue
            r, g, b = palette[idx & 0x0F]
            a = 0 if idx == 0 and transparent_zero else 255
            dst.putpixel((dst_x + x, dst_y + y), (r, g, b, 255 if a else 0) if a else (0, 0, 0, 0))


def render_tileset(ts_path, name, out_dir):
    tiles_png = Image.open(os.path.join(ts_path, "tiles.png"))
    _, _, tiles_px, tiles_w, tiles_h = read_tile_indices(tiles_png)
    palettes = load_tileset_palettes(ts_path)

    with open(os.path.join(ts_path, "metatiles.bin"), "rb") as f:
        mt = f.read()
    count = len(mt) // 16          # 8 tiles * 2 bytes
    rows = (count + SHEET_COLS - 1) // SHEET_COLS
    sheet = Image.new("RGBA", (SHEET_COLS * METATILE_SIZE, rows * METATILE_SIZE), (0, 0, 0, 0))

    # 2x2 sub-tile offsets within a metatile
    quad = [(0, 0), (TILE, 0), (0, TILE), (TILE, TILE)]

    for m in range(count):
        base = m * 16
        entries = struct.unpack_from("<8H", mt, base)
        mx = (m % SHEET_COLS) * METATILE_SIZE
        my = (m // SHEET_COLS) * METATILE_SIZE
        # entries 0-3 = bottom layer (opaque), 4-7 = top layer (index0 clear)
        for layer, is_top in ((0, False), (4, True)):
            for q in range(4):
                e = entries[layer + q]
                tile_id = e & 0x03FF
                xflip   = bool(e & 0x0400)
                yflip   = bool(e & 0x0800)
                pal_id  = (e >> 12) & 0x0F
                palette = palettes.get(pal_id, palettes.get(0, [(0, 0, 0)] * 16))
                blit_tile(sheet, mx + quad[q][0], my + quad[q][1],
                          tiles_px, tiles_w, tile_id, palette, xflip, yflip,
                          transparent_zero=is_top)

    os.makedirs(out_dir, exist_ok=True)
    out_png = os.path.join(out_dir, f"{name}.png")
    sheet.save(out_png)
    meta = {"name": name, "metatile_size": METATILE_SIZE, "cols": SHEET_COLS,
            "count": count, "sheet": f"{name}.png"}
    with open(os.path.join(out_dir, f"{name}.json"), "w") as f:
        json.dump(meta, f, indent=2)
    print(f"  tileset {name:12s} {count:4d} metatiles -> {os.path.relpath(out_png, ASSETS_DIR)}")
    return meta


def cmd_tilesets(src):
    out = TS_DIR
    metas = []
    for name in PRIMARY_TILESETS:
        ts_path = os.path.join(src, "data", "tilesets", "primary", name)
        if os.path.isdir(ts_path):
            metas.append(render_tileset(ts_path, name, out))
    with open(os.path.join(out, "index.json"), "w") as f:
        json.dump({"tilesets": metas}, f, indent=2)
    print(f"tilesets: wrote {len(metas)} sheet(s) to {os.path.relpath(out)}")


# --------------------------------------------------------------------------- #
# real pokeemerald maps: combined primary+secondary tileset + map.bin layout
# --------------------------------------------------------------------------- #
NUM_TILES_PRIMARY     = 512   # tile ids < this come from the primary tiles.png
NUM_METATILES_PRIMARY = 512   # metatile ids >= this are secondary
NUM_PALS_PRIMARY      = 6     # palettes 0-5 primary, 6-15 secondary
MB_TALL_GRASS         = 2     # metatile behavior for encounter grass
MB_COUNTER            = 128   # shop/PC-counter behavior (see combined_counter_ids)


def _tileset_behaviors(ts_path):
    """metatile_attributes.bin -> list of behavior bytes (attr & 0xFF)."""
    p = os.path.join(ts_path, "metatile_attributes.bin")
    if not os.path.isfile(p):
        return []
    blob = open(p, "rb").read()
    return [v & 0xFF for v in struct.unpack("<%dH" % (len(blob) // 2), blob)]


def combined_grass_ids(prim_path, sec_path):
    """Set of combined-sheet metatile ids that are tall (encounter) grass."""
    grass = set()
    for i, b in enumerate(_tileset_behaviors(prim_path)):
        if b == MB_TALL_GRASS:
            grass.add(i)
    if sec_path:
        for i, b in enumerate(_tileset_behaviors(sec_path)):
            if b == MB_TALL_GRASS:
                grass.add(NUM_METATILES_PRIMARY + i)
    return grass


def combined_counter_ids(prim_path, sec_path):
    """Set of combined-sheet metatile ids that are MB_COUNTER: the raised
    shop/Pokemon-Center desk tile, impassable but "see-through" for talking
    to the clerk/nurse standing behind it -- pokeemerald's
    GetInteractedObjectEventScript() special-cases these (see
    field_control_avatar.c) to look one tile past a counter for an NPC
    instead of requiring the player stand right next to them, which the
    counter's own collision makes impossible."""
    counters = set()
    for i, b in enumerate(_tileset_behaviors(prim_path)):
        if b == MB_COUNTER:
            counters.add(i)
    if sec_path:
        for i, b in enumerate(_tileset_behaviors(sec_path)):
            if b == MB_COUNTER:
                counters.add(NUM_METATILES_PRIMARY + i)
    return counters


def load_encounters(src):
    """MAP_ id -> {"land"/"water"/"fishing"/"rock": [(species, min, max), ...]}.

    Slots are kept in pokeemerald order (their position encodes the encounter
    rate), so the engine can weight them faithfully.
    """
    p = os.path.join(src, "src", "data", "wild_encounters.json")
    out = {}
    if not os.path.isfile(p):
        return out
    data = json.load(open(p))
    fields = [("land_mons", "land"), ("water_mons", "water"),
              ("fishing_mons", "fishing"), ("rock_smash_mons", "rock")]
    for grp in data.get("wild_encounter_groups", []):
        if not grp.get("for_maps", True):
            continue
        for enc in grp.get("encounters", []):
            mp = enc.get("map")
            if not mp:
                continue
            d = {}
            for key, name in fields:
                block = enc.get(key)
                if block and block.get("mons"):
                    slots = [(m["species"].replace("SPECIES_", ""),
                              int(m["min_level"]), int(m["max_level"]))
                             for m in block["mons"]]
                    d[name] = slots
            if d:
                out[mp] = d
    return out


# --------------------------------------------------------------------------- #
# battle data: species base stats, moves, learnsets, trainer parties
# --------------------------------------------------------------------------- #
def _strip_species(s):
    return s.replace("SPECIES_", "")


def parse_species_info(src):
    """[SPECIES_X] -> (hp,atk,def,spa,spd,spe,type1,type2,growthRate,expYield)."""
    import re
    txt = open(os.path.join(src, "src/data/pokemon/species_info.h"),
               encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"\[SPECIES_(\w+)\]\s*=\s*\{(.*?)\n    \}", txt, re.S):
        name, body = m.group(1), m.group(2)
        def g(field, d="0"):
            mm = re.search(r"\.%s\s*=\s*([0-9]+)" % field, body)
            return int(mm.group(1)) if mm else int(d)
        tm = re.search(r"\.types\s*=\s*\{\s*TYPE_(\w+)\s*,\s*TYPE_(\w+)", body)
        t1, t2 = (tm.group(1), tm.group(2)) if tm else ("NORMAL", "NORMAL")
        gr = re.search(r"\.growthRate\s*=\s*GROWTH_(\w+)", body)
        growth = gr.group(1) if gr else "MEDIUM_FAST"
        if name in ("NONE",):
            continue
        out[name] = (g("baseHP"), g("baseAttack"), g("baseDefense"),
                     g("baseSpAttack"), g("baseSpDefense"), g("baseSpeed"),
                     t1, t2, growth, g("expYield", "50"))
    return out


def parse_evolutions(src):
    """SPECIES_X -> [(method, param, target)] (method w/o EVO_ prefix)."""
    import re
    p = os.path.join(src, "src/data/pokemon/evolution.h")
    if not os.path.isfile(p):
        return {}
    txt = open(p, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"\[SPECIES_(\w+)\]\s*=\s*\{(.*?)\}\s*,?\s*\n", txt, re.S):
        name = m.group(1)
        evos = []
        for e in re.finditer(r"\{\s*EVO_(\w+)\s*,\s*(\w+)\s*,\s*SPECIES_(\w+)", m.group(2)):
            evos.append((e.group(1), e.group(2), e.group(3)))
        if evos:
            out[name] = evos
    return out


def parse_tm_learnsets(src):
    """SPECIES_X -> [MOVE_NAME, ...] learnable by TM/HM."""
    import re
    p = os.path.join(src, "src/data/pokemon/tmhm_learnsets.h")
    if not os.path.isfile(p):
        return {}
    txt = open(p, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"\[SPECIES_(\w+)\]\s*=\s*\{\s*\.learnset\s*=\s*\{(.*?)\}\s*\}", txt, re.S):
        moves = re.findall(r"\.(\w+)\s*=\s*TRUE", m.group(2))
        if moves:
            out[m.group(1)] = moves
    return out


def parse_tm_moves(src):
    """TM/HM number order -> move name; returns {'TM01': 'FOCUS_PUNCH', ...}."""
    import re
    p = os.path.join(src, "include/constants/tms_hms.h")
    if not os.path.isfile(p):
        return {}
    txt = open(p, encoding="utf-8", errors="replace").read()
    tms = re.findall(r"FOREACH_TM\(F\)\s*\\(.*?)#define FOREACH_HM", txt, re.S)
    hms = re.findall(r"FOREACH_HM\(F\)\s*\\(.*?)#define", txt, re.S)
    out = {}
    if tms:
        for i, mv in enumerate(re.findall(r"F\((\w+)\)", tms[0]), start=1):
            out["TM%02d" % i] = mv
    if hms:
        for i, mv in enumerate(re.findall(r"F\((\w+)\)", hms[0]), start=1):
            out["HM%02d" % i] = mv
    return out


def parse_moves(src):
    """[MOVE_X] -> (power, type, accuracy)."""
    import re
    txt = open(os.path.join(src, "src/data/battle_moves.h"),
               encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"\[MOVE_(\w+)\]\s*=\s*\{(.*?)\n    \}", txt, re.S):
        name, body = m.group(1), m.group(2)
        pw = re.search(r"\.power\s*=\s*([0-9]+)", body)
        ty = re.search(r"\.type\s*=\s*TYPE_(\w+)", body)
        ac = re.search(r"\.accuracy\s*=\s*([0-9]+)", body)
        if name == "NONE":
            continue
        out[name] = (int(pw.group(1)) if pw else 0,
                     ty.group(1) if ty else "NORMAL",
                     int(ac.group(1)) if ac else 100)
    return out


def parse_learnsets(src):
    """SPECIES_X -> ordered [(level, MOVE_NAME)] from the level-up learnsets."""
    import re
    base = os.path.join(src, "src/data/pokemon")
    learn = open(os.path.join(base, "level_up_learnsets.h"),
                 encoding="utf-8", errors="replace").read()
    ptrs = open(os.path.join(base, "level_up_learnset_pointers.h"),
                encoding="utf-8", errors="replace").read()
    # learnset array name -> moves
    arrays = {}
    for m in re.finditer(r"static const u16 (\w+)\[\]\s*=\s*\{(.*?)\};", learn, re.S):
        moves = [(int(a), b) for a, b in
                 re.findall(r"LEVEL_UP_MOVE\(\s*(\d+),\s*MOVE_(\w+)\)", m.group(2))]
        arrays[m.group(1)] = moves
    out = {}
    for m in re.finditer(r"\[SPECIES_(\w+)\]\s*=\s*(\w+)", ptrs):
        if m.group(1) != "NONE" and m.group(2) in arrays:
            out[m.group(1)] = arrays[m.group(2)]
    return out


def parse_trainers(src):
    """TRAINER_X -> [(SPECIES, level), ...] via trainers.h + trainer_parties.h."""
    import re
    parties = {}
    ptxt = open(os.path.join(src, "src/data/trainer_parties.h"),
                encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"(\w+)\[\]\s*=\s*\{(.*?)\n\};", ptxt, re.S):
        mons = []
        for mon in re.finditer(r"\.lvl\s*=\s*(\d+),\s*\.species\s*=\s*SPECIES_(\w+)",
                               m.group(2)):
            mons.append((mon.group(2), int(mon.group(1))))
        if mons:
            parties[m.group(1)] = mons
    trainers = {}
    ttxt = open(os.path.join(src, "src/data/trainers.h"),
                encoding="utf-8", errors="replace").read()
    for m in re.finditer(r"\[TRAINER_(\w+)\]\s*=\s*\{(.*?)\n    \}", ttxt, re.S):
        pm = re.search(r"\.party\s*=.*?(\w*[Pp]arty\w+|sParty_\w+)", m.group(2))
        if not pm:
            pm = re.search(r"TRAINER_PARTY\((\w+)\)", m.group(2))
        if pm and pm.group(1) in parties:
            trainers["TRAINER_" + m.group(1)] = parties[pm.group(1)]
    return trainers


def cmd_battle(src, starters=("TREECKO", "TORCHIC", "MUDKIP", "PIKACHU")):
    out = os.path.join(ASSETS_DIR, "battle")
    os.makedirs(out, exist_ok=True)
    species = parse_species_info(src)
    moves = parse_moves(src)
    learn = parse_learnsets(src)
    trainers = parse_trainers(src)

    with open(os.path.join(out, "species.tsv"), "w") as f:
        for name, s in sorted(species.items()):
            f.write("\t".join([name] + [str(v) for v in s]) + "\n")
    # evolutions, TM learnsets and the TM-number -> move table
    evos = parse_evolutions(src)
    with open(os.path.join(out, "evolutions.tsv"), "w") as f:
        for name, lst in sorted(evos.items()):
            f.write(name + "\t" + ",".join(f"{me}:{pa}:{tg}" for me, pa, tg in lst) + "\n")
    tml = parse_tm_learnsets(src)
    with open(os.path.join(out, "tm_learnsets.tsv"), "w") as f:
        for name, ms in sorted(tml.items()):
            f.write(name + "\t" + ",".join(ms) + "\n")
    tmm = parse_tm_moves(src)
    with open(os.path.join(out, "tm_moves.tsv"), "w") as f:
        for tm, mv in sorted(tmm.items()):
            f.write(f"{tm}\t{mv}\n")
    with open(os.path.join(out, "moves.tsv"), "w") as f:
        for name, (p, t, a) in sorted(moves.items()):
            f.write(f"{name}\t{p}\t{t}\t{a}\n")
    with open(os.path.join(out, "learnsets.tsv"), "w") as f:
        for name, ms in sorted(learn.items()):
            f.write(name + "\t" + ",".join(f"{lv}:{mv}" for lv, mv in ms) + "\n")
    with open(os.path.join(out, "trainers.tsv"), "w") as f:
        for name, party in sorted(trainers.items()):
            f.write(name + "\t" + ",".join(f"{sp}:{lv}" for sp, lv in party) + "\n")

    # sprites: front for every species, back for the starters (player side)
    nf = sum(1 for sp in species if import_pokemon_front(sp, src))
    nb = sum(1 for sp in starters if import_pokemon_back(sp, src))
    print(f"battle: {len(species)} species, {len(moves)} moves, "
          f"{len(learn)} learnsets, {len(trainers)} trainers; "
          f"{nf} front + {nb} back sprites -> {os.path.relpath(out)}")


def _flatten_to(src_png, dst_png):
    """P-mode (or any) PNG -> RGBA, palette index 0 transparent."""
    im = Image.open(src_png)
    if im.mode == "P":
        idx = im.load(); rgba = im.convert("RGBA"); px = rgba.load()
        w, h = im.size
        for y in range(h):
            for x in range(w):
                if idx[x, y] == 0:
                    px[x, y] = (0, 0, 0, 0)
    else:
        rgba = im.convert("RGBA")
    os.makedirs(os.path.dirname(dst_png), exist_ok=True)
    rgba.save(dst_png)


def parse_trainer_pics(src):
    """TRAINER_X -> front-pic file stem (e.g. 'hiker')."""
    import re
    txt = open(os.path.join(src, "src/data/trainers.h"),
               encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"\[TRAINER_(\w+)\]\s*=\s*\{(.*?)\n    \}", txt, re.S):
        pm = re.search(r"\.trainerPic\s*=\s*TRAINER_PIC_(\w+)", m.group(2))
        if pm:
            out["TRAINER_" + m.group(1)] = pm.group(1).lower()
    return out


def cmd_extras(src):
    """Import the remaining pokeemerald graphics: all back sprites, trainer
    battle sprites, type + item icons, the extra overworld object pics, and a
    flattened mirror of the other graphics folders."""
    sp = parse_species_info(src)
    nb = sum(1 for s in sp if import_pokemon_back(s, src))
    print(f"extras: {nb} back sprites")

    # trainer battle sprites (front_pics embed their palette)
    tdir = os.path.join(src, "graphics/trainers/front_pics")
    outt = os.path.join(ASSETS_DIR, "trainers")
    nt = 0
    for f in sorted(glob.glob(os.path.join(tdir, "*.png"))):
        _flatten_to(f, os.path.join(outt, os.path.basename(f))); nt += 1
    pics = parse_trainer_pics(src)
    with open(os.path.join(ASSETS_DIR, "battle", "trainer_pics.tsv"), "w") as fh:
        for t, pic in sorted(pics.items()):
            fh.write(f"{t}\t{pic}\n")
    print(f"extras: {nt} trainer sprites, {len(pics)} trainer->pic entries")

    # type icons
    ntp = 0
    for f in sorted(glob.glob(os.path.join(src, "graphics/types/*.png"))):
        name = os.path.basename(f)
        if name.startswith("contest"):
            continue
        _flatten_to(f, os.path.join(ASSETS_DIR, "types", name)); ntp += 1
    print(f"extras: {ntp} type icons")

    # item icons
    ni = 0
    for f in sorted(glob.glob(os.path.join(src, "graphics/items/icons/*.png"))):
        _flatten_to(f, os.path.join(ASSETS_DIR, "items", os.path.basename(f))); ni += 1
    print(f"extras: {ni} item icons")

    # extra overworld object pics
    no = 0
    for grp in ("berry_trees", "cushions", "dolls"):
        gdir = os.path.join(src, "graphics/object_events/pics", grp)
        for dp, _, fs in os.walk(gdir):
            for fn in fs:
                if fn.endswith(".png"):
                    rel = os.path.relpath(os.path.join(dp, fn), gdir)
                    key = grp + "_" + rel[:-4].replace(os.sep, "_")
                    _flatten_to(os.path.join(dp, fn),
                                os.path.join(OW_DIR, key + ".png")); no += 1
    print(f"extras: {no} extra overworld pics")

    # flattened mirror of the remaining graphics categories
    cats = ["field_effects", "door_anims", "map_popup", "text_window", "fonts",
            "battle_interface", "battle_anims", "pokenav", "pokemon_storage",
            "decorations", "berries", "slot_machine", "roulette", "contest",
            "pokedex", "naming_screen", "intro", "rayquaza_scene", "mail",
            "trade", "bag", "pokemon_jump", "berry_blender", "interface",
            "battle_transitions", "credits", "party_menu", "summary_screen"]
    nm = 0
    gout = os.path.join(ASSETS_DIR, "graphics")
    for cat in cats:
        base = os.path.join(src, "graphics", cat)
        for dp, _, fs in os.walk(base):
            for fn in fs:
                if not fn.endswith(".png"):
                    continue
                rel = os.path.relpath(os.path.join(dp, fn),
                                      os.path.join(src, "graphics"))
                try:
                    _flatten_to(os.path.join(dp, fn), os.path.join(gout, rel)); nm += 1
                except Exception:
                    pass
    print(f"extras: mirrored {nm} more graphics -> {os.path.relpath(gout)}")


def import_pokemon_back(species, src):
    """Colour a species' back sprite (first 64x64 frame) into assets/pokemon/back."""
    name = species.lower()
    base = os.path.join(src, "graphics", "pokemon", name)
    png = os.path.join(base, "back.png")
    pal = os.path.join(base, "normal.pal")
    if not (os.path.isfile(png) and os.path.isfile(pal)):
        return False
    im = Image.open(png)
    im = im.convert("P") if im.mode != "P" else im
    palette = load_jasc_pal(pal)
    frame = im.crop((0, 0, 64, 64)); idx = frame.load()
    out = Image.new("RGBA", (64, 64), (0, 0, 0, 0)); op = out.load()
    for y in range(64):
        for x in range(64):
            i = idx[x, y]
            if i == 0:
                continue
            r, g, b = palette[i & 0x0F]
            op[x, y] = (r, g, b, 255)
    d = os.path.join(ASSETS_DIR, "pokemon", "back")
    os.makedirs(d, exist_ok=True)
    out.save(os.path.join(d, species + ".png"))
    return True


def import_pokemon_front(species, src):
    """Colour a species' front sprite (first 64x64 frame) into assets/pokemon."""
    name = species.lower()
    base = os.path.join(src, "graphics", "pokemon", name)
    png = os.path.join(base, "front.png")
    pal = os.path.join(base, "normal.pal")
    if not (os.path.isfile(png) and os.path.isfile(pal)):
        return False
    im = Image.open(png)
    if im.mode != "P":
        im = im.convert("P")
    palette = load_jasc_pal(pal)
    frame = im.crop((0, 0, 64, 64))
    idx = frame.load()
    out = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
    op = out.load()
    for y in range(64):
        for x in range(64):
            i = idx[x, y]
            if i == 0:
                continue                      # transparent backdrop
            r, g, b = palette[i & 0x0F]
            op[x, y] = (r, g, b, 255)
    dst_dir = os.path.join(ASSETS_DIR, "pokemon")
    os.makedirs(dst_dir, exist_ok=True)
    out.save(os.path.join(dst_dir, species + ".png"))
    return True


def _tileset_raw(path):
    im = Image.open(os.path.join(path, "tiles.png"))
    im = im.convert("P") if im.mode != "P" else im
    return {"px": im.load(), "w": im.size[0], "h": im.size[1],
            "pals": load_tileset_palettes(path),
            "mt": open(os.path.join(path, "metatiles.bin"), "rb").read()}


def _tileset_name(gname):
    # "gTileset_General" -> "general"
    import re
    s = gname.replace("gTileset_", "")
    return re.sub(r"(?<!^)(?=[A-Z])", "_", s).lower()


def render_pair_sheet(primary_path, secondary_path, out_dir, name):
    """Render a combined metatile sheet: primary metatiles 0..N then secondary."""
    prim = _tileset_raw(primary_path)
    sec = _tileset_raw(secondary_path) if secondary_path else None

    # merged palette table: 0-5 primary, 6-15 from secondary when present
    pals = {i: prim["pals"].get(i, [(0, 0, 0)] * 16) for i in range(16)}
    if sec:
        for i in range(NUM_PALS_PRIMARY, 16):
            if i in sec["pals"]:
                pals[i] = sec["pals"][i]

    prim_count = len(prim["mt"]) // 16
    sec_count = len(sec["mt"]) // 16 if sec else 0
    total = (NUM_METATILES_PRIMARY + sec_count) if sec else prim_count

    rows = (total + SHEET_COLS - 1) // SHEET_COLS
    sheet = Image.new("RGBA", (SHEET_COLS * METATILE_SIZE, rows * METATILE_SIZE), (0, 0, 0, 0))
    quad = [(0, 0), (TILE, 0), (0, TILE), (TILE, TILE)]

    def metatile_entries(mid):
        if mid < prim_count:
            return prim["mt"], mid
        if sec and mid >= NUM_METATILES_PRIMARY and (mid - NUM_METATILES_PRIMARY) < sec_count:
            return sec["mt"], mid - NUM_METATILES_PRIMARY
        return None, None

    for mid in range(total):
        src_mt, local = metatile_entries(mid)
        if src_mt is None:
            continue
        entries = struct.unpack_from("<8H", src_mt, local * 16)
        mx = (mid % SHEET_COLS) * METATILE_SIZE
        my = (mid // SHEET_COLS) * METATILE_SIZE
        for layer, is_top in ((0, False), (4, True)):
            for q in range(4):
                e = entries[layer + q]
                tile_id = e & 0x03FF
                xflip = bool(e & 0x0400)
                yflip = bool(e & 0x0800)
                pal_id = (e >> 12) & 0x0F
                # tile pixels come from primary or secondary sheet by id range
                if tile_id < NUM_TILES_PRIMARY or not sec:
                    tpx, tw, th, local_tile = prim["px"], prim["w"], prim["h"], tile_id
                else:
                    tpx, tw, th, local_tile = sec["px"], sec["w"], sec["h"], tile_id - NUM_TILES_PRIMARY
                blit_tile(sheet, mx + quad[q][0], my + quad[q][1],
                          tpx, tw, local_tile, pals[pal_id], xflip, yflip,
                          transparent_zero=is_top, tiles_h=th)

    os.makedirs(out_dir, exist_ok=True)
    out_png = os.path.join(out_dir, name + ".png")
    sheet.save(out_png)
    return total


def cmd_map(src, layout_name):
    layouts = json.load(open(os.path.join(src, "data", "layouts", "layouts.json")))
    layout = next((l for l in layouts["layouts"] if l["name"] == layout_name), None)
    if layout is None:
        sys.exit("map: unknown layout " + layout_name)

    prim = _tileset_name(layout["primary_tileset"])
    sec = _tileset_name(layout["secondary_tileset"]) if layout.get("secondary_tileset") else None
    prim_path = os.path.join(src, "data", "tilesets", "primary", prim)
    sec_path = os.path.join(src, "data", "tilesets", "secondary", sec) if sec else None

    sheet_name = prim + ("__" + sec if sec else "")
    total = render_pair_sheet(prim_path, sec_path, TS_DIR, sheet_name)
    print(f"  combined tileset {sheet_name}: {total} metatiles")

    w, h = layout["width"], layout["height"]
    blob = open(os.path.join(src, layout["blockdata_filepath"]), "rb").read()
    cells = struct.unpack("<%dH" % (w * h), blob[:w * h * 2])
    ids = [c & 0x03FF for c in cells]
    # collision bits (10-11): non-zero => blocked
    solid = [1 if ((c >> 10) & 0x03) else 0 for c in cells]

    # spawn: first passable tile scanning from the map centre outward
    def passable_at(x, y):
        return 0 <= x < w and 0 <= y < h and solid[y * w + x] == 0
    sx, sy = w // 2, h // 2
    if not passable_at(sx, sy):
        for r in range(1, max(w, h)):
            found = False
            for yy in range(max(0, sy - r), min(h, sy + r + 1)):
                for xx in range(max(0, sx - r), min(w, sx + r + 1)):
                    if passable_at(xx, yy):
                        sx, sy, found = xx, yy, True
                        break
                if found: break
            if found: break

    maps_dir = os.path.join(TOOLS_DIR, "..", "maps")
    os.makedirs(maps_dir, exist_ok=True)
    # friendly file name: LittlerootTown_Layout -> littleroot_town
    short = _tileset_name(layout_name.replace("_Layout", ""))
    out_map = os.path.join(maps_dir, short + ".map")
    lines = [f"tileset {sheet_name}", f"{w} {h}", f"{sx} {sy}"]
    for y in range(h):
        lines.append(",".join(str(ids[y * w + x]) for x in range(w)))
    lines.append("collision")
    for y in range(h):
        lines.append(",".join(str(solid[y * w + x]) for x in range(w)))
    open(out_map, "w").write("\n".join(lines) + "\n")
    print(f"map: {layout_name} ({w}x{h}) -> {os.path.relpath(out_map)}  spawn ({sx},{sy})")


# --------------------------------------------------------------------------- #
# world: import EVERY map with its NPCs (object events) + movement
# --------------------------------------------------------------------------- #

# graphics_id -> overworld sheet key aliases where the pokeemerald name does not
# match our imported file name. Value is the sheet key (without .png).
GFX_ALIASES = {
    "rival_brendan_normal": "people_brendan_walking",
    "rival_may_normal":     "people_may_walking",
    "aqua_member_m":        "people_team_aqua_aqua_member_m",
    "aqua_member_f":        "people_team_aqua_aqua_member_f",
    "magma_member_m":       "people_team_magma_magma_member_m",
    "magma_member_f":       "people_team_magma_magma_member_f",
    "archie":               "people_team_aqua_archie",
    "maxie":                "people_team_magma_maxie",
}
FALLBACK_SHEET = "people_man_1"     # generic NPC when nothing else matches


def build_gfx_resolver(ow_index):
    """graphics_id -> sheet key, using suffix match, aliases, then a fallback."""
    local = {}
    for k in ow_index:
        parts = k.split("_", 1)
        if len(parts) == 2:
            local.setdefault(parts[1], k)

    def resolve(gfx):
        name = gfx.replace("OBJ_EVENT_GFX_", "").lower()
        if name in local:
            return local[name]
        if name in GFX_ALIASES and GFX_ALIASES[name] in ow_index:
            return GFX_ALIASES[name]
        # berry trees etc. sometimes need the leaf name only
        leaf = name.split("_")[-1]
        if leaf in local:
            return local[leaf]
        return FALLBACK_SHEET if FALLBACK_SHEET in ow_index else None
    return resolve


# Pages within one dialog are separated by \p in pokeemerald; we keep them as
# separate pages joined by U+001F so the engine can show one box per page.
PAGE_SEP = "\x1f"


def _clean_dialog(raw):
    """Turn joined .string content into pages (joined by PAGE_SEP)."""
    import re
    pages = []
    for pg in raw.split("\\p"):
        s = pg
        for code in ("\\n", "\\l"):
            s = s.replace(code, " ")
        s = s.replace('\\"', '"')
        s = s.replace("{PLAYER}", "PLAYER").replace("{RIVAL}", "RIVAL")
        s = re.sub(r"\{[^}]*\}", "", s)     # drop remaining control codes
        s = s.replace("$", " ")
        s = re.sub(r"\s+", " ", s).strip()
        if s:
            pages.append(s[:180])
    return PAGE_SEP.join(pages)


def _parse_text_lines(lines):
    """Core of parse_map_texts/parse_text_file: pull every `label::` /
    `.string "..."` block out of a list of source lines."""
    import re
    texts = {}
    i = 0
    while i < len(lines):
        m = re.match(r"^(\w+)::?\s*$", lines[i])
        if m and (i + 1 < len(lines)) and ".string" in lines[i + 1]:
            label = m.group(1)
            parts = []
            j = i + 1
            while j < len(lines) and ".string" in lines[j]:
                sm = re.search(r'\.string\s+"(.*)"', lines[j])
                if sm:
                    parts.append(sm.group(1))
                j += 1
            texts[label] = _clean_dialog(" ".join(parts))
            i = j
        else:
            i += 1
    return texts


def parse_map_texts(map_dir):
    """Return {text_label: cleaned text} from a map's scripts.inc."""
    inc = os.path.join(map_dir, "scripts.inc")
    if not os.path.isfile(inc):
        return {}
    lines = open(inc, encoding="utf-8", errors="replace").read().splitlines()
    return _parse_text_lines(lines)


def parse_text_file(path):
    """Return {text_label: cleaned text} from a plain data/text/*.inc file
    (no scripts, just .string blocks -- e.g. mart_clerk.inc's "How may I
    serve you?", pkmn_center_nurse.inc's "Would you like to rest your
    Pokemon?"). These live outside data/scripts/ and data/maps/, so the
    project-wide text pool misses them unless scanned separately."""
    if not os.path.isfile(path):
        return {}
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    return _parse_text_lines(lines)


def parse_map_load_triggers(map_dir):
    """pokeemerald maps can auto-run a script on load via a var check
    (MAP_SCRIPT_ON_FRAME_TABLE -> `map_script_2 VAR, val, LABEL`), used all
    over for one-time "first time entering this map" setup -- e.g. Route 101
    advances VAR_ROUTE101_STATE from 0 to 1 this way, which is also the exact
    condition the Birch-rescue coord trigger waits on. Without running this,
    that trigger (and ~100 similar ones elsewhere) can never fire.

    Returns [(var, val, label), ...] for this map's ON_FRAME_TABLE script."""
    inc = os.path.join(map_dir, "scripts.inc")
    if not os.path.isfile(inc):
        return []
    lines = open(inc, encoding="utf-8", errors="replace").read().splitlines()
    # find the label named by "map_script MAP_SCRIPT_ON_FRAME_TABLE, <label>"
    frame_label = None
    for ln in lines:
        m = _re.search(r"map_script\s+MAP_SCRIPT_ON_FRAME_TABLE,\s*(\w+)", ln)
        if m:
            frame_label = m.group(1)
            break
    if not frame_label:
        return []
    out = []
    in_block = False
    for ln in lines:
        if _re.match(r"^" + _re.escape(frame_label) + r":\s*$", ln):
            in_block = True
            continue
        if in_block:
            if _re.match(r"^\w+::?\s*$", ln):
                break
            m = _re.search(r"map_script_2\s+(\w+),\s*(\w+),\s*(\w+)", ln)
            if m:
                out.append((m.group(1), m.group(2), m.group(3)))
    return out


def parse_map_dialogs(map_dir):
    """Return {script_label: dialog_text} for a map's scripts.inc."""
    import re
    inc = os.path.join(map_dir, "scripts.inc")
    if not os.path.isfile(inc):
        return {}
    lines = open(inc, encoding="utf-8", errors="replace").read().splitlines()
    texts = parse_map_texts(map_dir)

    # script label -> first msgbox text label
    scripts = {}
    cur = None
    for ln in lines:
        sm = re.match(r"^(\w+)::", ln)
        if sm:
            cur = sm.group(1)
            continue
        if cur and cur not in scripts:
            mm = re.search(r"\bmsgbox\s+(\w+)", ln)
            if mm:
                scripts[cur] = mm.group(1)
    # resolve script -> text
    return {sl: texts[tl] for sl, tl in scripts.items() if tl in texts}


# --------------------------------------------------------------------------- #
# full event-script extraction (for the in-engine ScriptVM)
# --------------------------------------------------------------------------- #
import re as _re

# MAP_ id -> folder name, so script-driven `warp`/`warpdoor`/... instructions
# can point at the right .map file. Populated once by cmd_world (it already
# builds this table for the `warps` door-tile section) before any script
# parsing happens, so it's ready by the time _parse_script_lines needs it.
_WARP_DEST = {}
_WARP_OPS = {"warp", "warpdoor", "warphole", "warpsilent", "warpspinenter",
             "warpteleport", "warpmossdeepgym", "warpwhitefade"}


def _translate_warp_args(op, args):
    """A script `warp` instruction names its destination by MAP_ constant
    (e.g. MAP_LITTLEROOT_TOWN_PROFESSOR_BIRCHS_LAB); the engine's map files
    are named by folder instead, so rewrite arg[0] to match (or "-" if the
    destination map wasn't importable)."""
    if op not in _WARP_OPS or not args:
        return args
    dest = _WARP_DEST.get(args[0], "-")
    return [dest] + args[1:]


# Movement macro -> compact engine action. Unknowns become "delay" (a no-op
# pause) so a template still runs to completion.
_MOVE_MAP = {
    "walk_up": "up", "walk_down": "down", "walk_left": "left", "walk_right": "right",
    "walk_fast_up": "up", "walk_fast_down": "down",
    "walk_fast_left": "left", "walk_fast_right": "right",
    "walk_slow_up": "up", "walk_slow_down": "down",
    "walk_slow_left": "left", "walk_slow_right": "right",
    "jump_up": "up", "jump_down": "down", "jump_left": "left", "jump_right": "right",
    "jump_2_up": "up", "jump_2_down": "down", "jump_2_left": "left", "jump_2_right": "right",
    "face_up": "face_up", "face_down": "face_down",
    "face_left": "face_left", "face_right": "face_right",
    "step_end": "end",
}


def _move_action(tok):
    tok = tok.strip()
    if tok in _MOVE_MAP:
        return _MOVE_MAP[tok]
    if tok.startswith("delay") or tok.startswith("walk_in_place"):
        return "delay"
    if tok.startswith("face"):
        return "face_down"
    return "delay"


def parse_map_scripts(map_dir, texts):
    """Parse a map's scripts.inc into executable scripts + movement templates.
    See _parse_script_lines for the return shape."""
    inc = os.path.join(map_dir, "scripts.inc")
    if not os.path.isfile(inc):
        return {}, {}
    lines = open(inc, encoding="utf-8", errors="replace").read().splitlines()
    return _parse_script_lines(lines, texts)


def parse_shared_script_file(path, texts):
    """Parse one data/scripts/*.inc shared script file the same way as a
    map's scripts.inc. Many object events (e.g. every player house's Mom)
    point at a script defined once in a shared file like players_house.inc
    rather than in that map's own scripts.inc, so these need to be in the
    pool too or the reference silently resolves to nothing."""
    if not os.path.isfile(path):
        return {}, {}
    lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
    return _parse_script_lines(lines, texts)


def _parse_script_lines(lines, texts):
    """Parse scripts.inc-formatted lines into executable scripts + movement
    templates.

    Returns (scripts, movements):
      scripts[label]   = list of instructions; each instruction is a list of
                         string tokens (opcode first). msgbox text is resolved
                         inline as ["msgbox", <text>].
      movements[label] = list of compact actions (up/down/left/right/face_*/...)
    """
    scripts, movements = {}, {}
    i, n = 0, len(lines)
    while i < n:
        ln = lines[i]
        m_scr = _re.match(r"^(\w+)::", ln)
        m_dat = _re.match(r"^(\w+):\s*$", ln)
        if m_scr:
            label = m_scr.group(1)
            body = []
            i += 1
            while i < n and not _re.match(r"^\w+:", lines[i]):
                s = lines[i].strip()
                i += 1
                if not s or s.startswith("."):
                    continue
                # "op arg1, arg2" -> [op, arg1, arg2]
                parts = s.split(None, 1)
                op = parts[0]
                args = [a.strip() for a in parts[1].split(",")] if len(parts) > 1 else []
                if op in ("msgbox", "message") and args and args[0] in texts:
                    # MSGBOX_YESNO gets its own opcode (rather than a 3rd
                    # instruction arg) because the map-file writer serializes
                    # msgbox as "msgbox\t<text>" -- a 3rd element would be
                    # silently dropped, and the text itself is free-form
                    # enough that we can't safely tack a marker onto it.
                    is_yesno = op == "msgbox" and len(args) > 1 and args[1] == "MSGBOX_YESNO"
                    body.append(["msgboxyesno" if is_yesno else "msgbox", texts[args[0]]])
                elif op == "message":
                    # pokeemerald's low-level "message TEXT; waitmessage;
                    # waitbuttonpress" is the same on-screen effect as the
                    # msgbox macro (which already expands to exactly that);
                    # the ScriptVM only knows the "msgbox" opcode, so map
                    # this one across too instead of silently doing nothing.
                    body.append(["msgbox", args[0] if args else ""])
                elif op in _WARP_OPS:
                    body.append([op] + _translate_warp_args(op, args))
                else:
                    body.append([op] + args)
            scripts[label] = body
        elif m_dat:
            label = m_dat.group(1)
            # movement template? (body of bare movement tokens, no .string)
            j = i + 1
            if j < n and ".string" not in lines[j] and lines[j].strip():
                acts = []
                while j < n and not _re.match(r"^\w+:", lines[j]):
                    t = lines[j].strip(); j += 1
                    if not t or t.startswith("."):
                        continue
                    a = _move_action(t)
                    acts.append(a)
                    if a == "end":
                        break
                if acts:
                    movements[label] = acts
            i += 1
        else:
            i += 1

    # pokeemerald scripts commonly chain adjacent "::" labels by falling
    # through (no goto/return needed -- the next label is just the next
    # instruction in the assembled ROM). Our engine looks up a script's body
    # by label and runs only that list, so a label whose body doesn't end in
    # an unconditional terminator must get an explicit goto to the next
    # label in file order, or it silently finish()es a call/subscript early
    # (e.g. a helper that falls through a chain of "goto_if_not_defeated"
    # checks into the next one, common in gym "count defeated trainers"
    # helpers) -- which can abort a caller's script before it reaches
    # whatever comes after the call (a badge's setflag, for instance).
    TERMINATORS = {"end", "return", "goto"}
    order = list(scripts.keys())
    for idx, label in enumerate(order):
        body = scripts[label]
        if body and body[-1][0] not in TERMINATORS and idx + 1 < len(order):
            body.append(["goto", order[idx + 1]])

    return scripts, movements


def parse_item_ball_scripts(src):
    """The visible item-ball pickup scripts live in one shared file, not per
    map (`data/scripts/item_ball_scripts.inc`), each just `finditem ITEM_X`.
    Returns label -> item name."""
    out = {}
    p = os.path.join(src, "data", "scripts", "item_ball_scripts.inc")
    if not os.path.isfile(p):
        return out
    label = None
    for ln in open(p, encoding="utf-8", errors="replace"):
        m = _re.match(r"^(\w+)::", ln)
        if m:
            label = m.group(1)
            continue
        m = _re.search(r"\bfinditem\s+(ITEM_\w+)", ln)
        if m and label:
            out[label] = m.group(1)
            label = None
    return out


def parse_new_game_flags(src):
    """The flags pokeemerald sets at the very start of a new save, mostly
    FLAG_HIDE_* for every NPC/item that shouldn't appear until its own scene
    unlocks it (data/scripts/new_game.inc, EventScript_ResetAllMapFlags).
    Without these, every "hidden until met" NPC -- the rival standing in
    your bedroom, Birch already in his lab, etc. -- is visible from turn one,
    which can even block the story (the rival's static placement sits right
    on the stairs down)."""
    out = []
    p = os.path.join(src, "data", "scripts", "new_game.inc")
    if not os.path.isfile(p):
        return out
    in_block = False
    for ln in open(p, encoding="utf-8", errors="replace"):
        if ln.startswith("EventScript_ResetAllMapFlags::"):
            in_block = True
            continue
        if in_block:
            if _re.match(r"^\w+::", ln):
                break
            m = _re.search(r"\bsetflag\s+(FLAG_\w+)", ln)
            if m:
                out.append(m.group(1))
    return out


def reachable_scripts(entry_labels, scripts):
    """BFS the scripts reachable from entry labels via label-valued args."""
    keep = {}
    stack = [l for l in entry_labels if l in scripts]
    while stack:
        lab = stack.pop()
        if lab in keep:
            continue
        keep[lab] = scripts[lab]
        for instr in scripts[lab]:
            for a in instr[1:]:
                if a in scripts and a not in keep:
                    stack.append(a)
    return keep


def movement_token(mt):
    mt = (mt or "").upper()
    if "WANDER" in mt:                      return "wander", "S"
    if "UP_AND_DOWN" in mt:                 return "pace_v", "S"
    if "LEFT_AND_RIGHT" in mt:              return "pace_h", "W"
    if mt.endswith("FACE_UP"):              return "static", "N"
    if mt.endswith("FACE_LEFT"):            return "static", "W"
    if mt.endswith("FACE_RIGHT"):           return "static", "E"
    return "static", "S"


def cmd_world(src, limit=None):
    layouts = json.load(open(os.path.join(src, "data", "layouts", "layouts.json")))
    by_id = {l["id"]: l for l in layouts["layouts"]}
    ow_index = json.load(open(os.path.join(OW_DIR, "index.json")))
    resolve = build_gfx_resolver(ow_index)

    maps_dir = os.path.normpath(os.path.join(TOOLS_DIR, "..", "maps"))
    os.makedirs(maps_dir, exist_ok=True)
    pair_cache = {}    # (prim,sec) -> sheet name
    grass_cache = {}   # (prim,sec) -> set of grass metatile ids
    counter_cache = {}   # (prim,sec) -> set of shop/PC-counter metatile ids
    catalog = []
    encounters = load_encounters(src)
    used_species = set()

    mroot = os.path.join(src, "data", "maps")
    names = sorted(d for d in os.listdir(mroot)
                   if os.path.isdir(os.path.join(mroot, d)))

    # MAP_ id -> folder name, so warps can point at the right .map file.
    id_to_folder = {}
    for d in names:
        p = os.path.join(mroot, d, "map.json")
        if os.path.isfile(p):
            try:
                id_to_folder[json.load(open(p)).get("id")] = d
            except Exception:
                pass
    _WARP_DEST.clear()
    _WARP_DEST.update(id_to_folder)

    # Story-accurate spawns: a map's heal location is where the player wakes /
    # respawns (MAP_ id -> (x, y)). Used in preference to a geometric guess.
    heal_by_mapid = {}
    for hp in (os.path.join(src, "src", "data", "heal_locations.json"),
               os.path.join(src, "data", "heal_locations.json")):
        if os.path.isfile(hp):
            try:
                for hl in json.load(open(hp)).get("heal_locations", []):
                    if "map" in hl and "x" in hl and "y" in hl:
                        heal_by_mapid[hl["map"]] = (int(hl["x"]), int(hl["y"]))
            except Exception:
                pass
            break

    # Visible item-ball pickup scripts (`data/scripts/item_ball_scripts.inc`):
    # label -> item, so item balls give the real item instead of sitting inert.
    item_ball_scripts = parse_item_ball_scripts(src)

    # A project-wide text pool: shared scripts (below) reference text labels
    # that are usually defined in just one of the maps that uses them (e.g.
    # Mom's "Isn't it nice in here?" line lives in Brendan's House 1F's own
    # scripts.inc, but the shared PlayersHouse_1F_EventScript_Mom that shows
    # it is reused by May's House too), so a single map's local text pool
    # isn't enough to resolve them.
    global_texts = {}
    for mname in names:
        global_texts.update(parse_map_texts(os.path.join(mroot, mname)))
    # data/text/*.inc: shared strings with no home map at all (mart clerk
    # greetings, the Pokemon Center nurse's heal prompt, ...) -- referenced
    # by the shared scripts below via plain `message`/`msgbox`, so without
    # these the label itself would render instead of the actual text.
    tdir = os.path.join(src, "data", "text")
    if os.path.isdir(tdir):
        for fn in sorted(os.listdir(tdir)):
            if fn.endswith(".inc"):
                global_texts.update(parse_text_file(os.path.join(tdir, fn)))

    # Shared scripts (data/scripts/*.inc): common NPCs like the player's mom,
    # the PC, Nurse Joy, move tutors etc. are defined once here and pointed
    # to by object events across many maps -- without these, that object
    # event's script label resolves to nothing and interacting does nothing.
    shared_scripts, shared_moves = {}, {}
    sdir = os.path.join(src, "data", "scripts")
    if os.path.isdir(sdir):
        for fn in sorted(os.listdir(sdir)):
            # item_ball_scripts.inc is handled separately above (with the
            # pickup-flag gating item balls need); skip it here so its plain
            # `finditem` bodies don't shadow the flag-gated versions.
            if not fn.endswith(".inc") or fn == "item_ball_scripts.inc":
                continue
            s, m = parse_shared_script_file(os.path.join(sdir, fn), global_texts)
            shared_scripts.update(s)
            shared_moves.update(m)

    done = 0
    item_total = 0
    for mname in names:
        mj_path = os.path.join(mroot, mname, "map.json")
        if not os.path.isfile(mj_path):
            continue
        mj = json.load(open(mj_path))
        layout = by_id.get(mj.get("layout"))
        if layout is None or layout["width"] * layout["height"] <= 1:
            continue

        prim = _tileset_name(layout["primary_tileset"])
        sec = _tileset_name(layout["secondary_tileset"]) if layout.get("secondary_tileset") else None
        key = (prim, sec)
        if key not in pair_cache:
            sheet_name = prim + ("__" + sec if sec else "")
            prim_path = os.path.join(src, "data", "tilesets", "primary", prim)
            sec_path = os.path.join(src, "data", "tilesets", "secondary", sec) if sec else None
            try:
                render_pair_sheet(prim_path, sec_path, TS_DIR, sheet_name)
            except Exception as ex:
                print("  tileset fail", sheet_name, ex); continue
            pair_cache[key] = sheet_name
            grass_cache[key] = combined_grass_ids(prim_path, sec_path)
            counter_cache[key] = combined_counter_ids(prim_path, sec_path)
        sheet_name = pair_cache[key]
        grass_ids = grass_cache.get(key, set())
        counter_ids = counter_cache.get(key, set())

        w, h = layout["width"], layout["height"]
        try:
            blob = open(os.path.join(src, layout["blockdata_filepath"]), "rb").read()
            cells = struct.unpack("<%dH" % (w * h), blob[:w * h * 2])
        except Exception as ex:
            print("  blockdata fail", mname, ex); continue
        ids = [c & 0x03FF for c in cells]
        solid = [1 if ((c >> 10) & 0x03) else 0 for c in cells]

        # NPCs from object events, with dialog + full script from scripts.inc
        mapdir = os.path.join(mroot, mname)
        dialogs = parse_map_dialogs(mapdir)
        # Local text wins on a clash, but a map-local script can just as
        # easily reference a shared data/text/*.inc string (e.g. every mart
        # clerk's "How may I serve you?") as one of its own .string blocks,
        # so start from the global pool and layer this map's own on top.
        texts = dict(global_texts)
        texts.update(parse_map_texts(mapdir))
        all_scripts, all_moves = parse_map_scripts(mapdir, texts)
        # Local (per-map) definitions win on a name clash; shared ones fill
        # in anything the map doesn't define itself.
        for lab, body in shared_scripts.items():
            all_scripts.setdefault(lab, body)
        for lab, acts in shared_moves.items():
            all_moves.setdefault(lab, acts)

        # Item balls: synthesize a pickup script for any ball whose script
        # isn't already defined locally (almost all of them -- the real
        # scripts live in one shared file, data/scripts/item_ball_scripts.inc,
        # keyed by label). Gated by the object's own flag so it only pays out
        # once, exactly like the real finditem/STD_FIND_ITEM behaviour.
        for oe in mj.get("object_events", []):
            if oe.get("graphics_id") != "OBJ_EVENT_GFX_ITEM_BALL":
                continue
            label = oe.get("script")
            if not label or label in all_scripts:
                continue
            item = item_ball_scripts.get(label)
            if not item:
                continue
            flag = oe.get("flag")
            if flag:
                done_label = label + "_Taken"
                all_scripts[label] = [
                    ["goto_if_set", flag, done_label],
                    ["finditem", item],
                    ["setflag", flag],
                    ["end"],
                ]
                all_scripts[done_label] = [["end"]]
            else:
                all_scripts[label] = [["finditem", item], ["end"]]
            item_total += 1
        npcs = []
        for oe in mj.get("object_events", []):
            try:
                x = int(oe["x"]); y = int(oe["y"])
            except Exception:
                continue
            if not (0 <= x < w and 0 <= y < h):
                continue
            gid = oe.get("graphics_id", "")
            scr = oe.get("script", "") or ""
            # OBJ_EVENT_GFX_VAR_* is a runtime-substituted sprite id, used for
            # a grab-bag of things we can't know the real look of at import
            # time: secret-base decoration placeholders (no script), and
            # dynamic feature NPCs like the Battle Tower apprentice, Union
            # Room player slots, or the record-mix visitor (all with a real
            # script, but no way to guess their sprite) -- skip all of those,
            # a wrong sprite is worse than none. The one case worth keeping
            # is the story RIVAL: their outdoor sprite is *always* just the
            # player's opposite-gender counterpart (VAR_OBJ_GFX_ID_0), never
            # actually random, and every rival object event's script name
            # says so (Route103_EventScript_Rival, RustboroCity_..._Rival,
            # etc. -- the chain that eventually unlocks the Pokedex).
            if "OBJ_EVENT_GFX_VAR_" in gid:
                is_rival = "rival" in scr.lower() or "RIVAL" in (oe.get("local_id") or "")
                if not is_rival:
                    continue
                gid = "OBJ_EVENT_GFX_RIVAL_MAY_NORMAL"
            sheet = resolve(gid)
            if not sheet:
                continue
            move, face = movement_token(oe.get("movement_type"))
            dlg = dialogs.get(scr, "")
            raw_flag = oe.get("flag") or ""
            hide_flag = raw_flag if raw_flag and raw_flag != "0x0" else "-"
            npcs.append((sheet, x, y, face, move, dlg, scr, hide_flag))

        # Signs (readable bg_events) share the same script->text resolution.
        signs = []
        for bg in mj.get("bg_events", []):
            if bg.get("type") != "sign" or not bg.get("script"):
                continue
            text = dialogs.get(bg["script"], "")
            if not text:
                continue
            try:
                bx = int(bg["x"]); by = int(bg["y"])
            except Exception:
                continue
            if 0 <= bx < w and 0 <= by < h:
                signs.append((bx, by, text))

        # spawn: use the story heal location for this map when it has one
        # (that's where the player wakes / respawns); else a passable centre tile.
        def passable_at(x, y):
            return 0 <= x < w and 0 <= y < h and solid[y * w + x] == 0
        heal = heal_by_mapid.get(mj.get("id"))
        if heal and passable_at(*heal):
            sx, sy = heal
        else:
            sx, sy = w // 2, h // 2
        if not passable_at(sx, sy):
            for r in range(1, max(w, h)):
                hit = None
                for yy in range(max(0, sy - r), min(h, sy + r + 1)):
                    for xx in range(max(0, sx - r), min(w, sx + r + 1)):
                        if passable_at(xx, yy):
                            hit = (xx, yy); break
                    if hit: break
                if hit:
                    sx, sy = hit; break

        # Warps: keep every warp in source order (index == dest_warp_id used by
        # other maps). dest resolves to a folder name; unknown dests kept as "-".
        warps = []
        for wv in mj.get("warp_events", []):
            try:
                wx = int(wv["x"]); wy = int(wv["y"])
            except Exception:
                continue
            dest = id_to_folder.get(wv.get("dest_map"), "-")
            try:
                dwarp = int(wv.get("dest_warp_id", 0))
            except Exception:
                dwarp = 0
            warps.append((wx, wy, dest, dwarp))

        out = os.path.join(maps_dir, mname + ".map")
        lines = [f"tileset {sheet_name}", f"{w} {h}", f"{sx} {sy}"]
        for y in range(h):
            lines.append(",".join(str(ids[y * w + x]) for x in range(w)))
        lines.append("collision")
        for y in range(h):
            lines.append(",".join(str(solid[y * w + x]) for x in range(w)))
        if warps:
            lines.append("warps")
            for (x, y, dest, dw) in warps:
                lines.append(f"{x} {y} {dest} {dw}")
        if npcs:
            lines.append("npcs")
            for n in npcs:
                lines.append(f"{n[0]} {n[1]} {n[2]} {n[3]} {n[4]} {n[7]}")
            # dialog lines are tab-separated (text has spaces): "<index>\t<text>"
            dlg_lines = [(i, n[5]) for i, n in enumerate(npcs) if n[5]]
            if dlg_lines:
                lines.append("dialogs")
                for (i, text) in dlg_lines:
                    lines.append(f"{i}\t{text}")

        # --- full event scripts (for the ScriptVM) -------------------------
        coord_triggers = []
        for ce in mj.get("coord_events", []):
            if ce.get("type") != "trigger" or not ce.get("script"):
                continue
            try:
                cx = int(ce["x"]); cy = int(ce["y"])
            except Exception:
                continue
            coord_triggers.append((cx, cy, ce.get("var", "0"),
                                   ce.get("var_value", "0"), ce["script"]))

        # Hidden items: no sprite, just an (item, flag) pair on a bg_event
        # tile. Simplified here to pick up on stepping onto the tile (the
        # real game gates the reveal on the Itemfinder, which this engine
        # doesn't model) via an always-true coord trigger ("0" == "0").
        for bg in mj.get("bg_events", []):
            if bg.get("type") != "hidden_item" or not bg.get("item"):
                continue
            try:
                hx = int(bg["x"]); hy = int(bg["y"])
            except Exception:
                continue
            label = f"{mname}_HiddenItem_{hx}_{hy}"
            flag = bg.get("flag")
            if flag:
                done_label = label + "_Taken"
                all_scripts[label] = [
                    ["goto_if_set", flag, done_label],
                    ["finditem", bg["item"]],
                    ["setflag", flag],
                    ["end"],
                ]
                all_scripts[done_label] = [["end"]]
            else:
                all_scripts[label] = [["finditem", bg["item"]], ["end"]]
            coord_triggers.append((hx, hy, "0", "0", label))
            item_total += 1

        # One-time "on entering this map" auto-triggers (var-gated), e.g.
        # Route 101 advancing VAR_ROUTE101_STATE 0->1, which is also the
        # exact condition the Birch-rescue coord trigger waits on.
        load_triggers = [t for t in parse_map_load_triggers(mapdir) if t[2] in all_scripts]

        entry = [n[6] for n in npcs if n[6] in all_scripts]
        entry += [t[4] for t in coord_triggers if t[4] in all_scripts]
        entry += [t[2] for t in load_triggers]
        keep = reachable_scripts(entry, all_scripts)
        used_moves = set()
        for body in keep.values():
            for instr in body:
                for a in instr[1:]:
                    if a in all_moves:
                        used_moves.add(a)

        obj_scr = [(i, n[6]) for i, n in enumerate(npcs) if n[6] in keep]
        if obj_scr:
            lines.append("objscripts")
            for (i, lab) in obj_scr:
                lines.append(f"{i} {lab}")
        trig = [t for t in coord_triggers if t[4] in keep]
        if trig:
            lines.append("triggers")
            for (cx, cy, var, val, lab) in trig:
                lines.append(f"{cx} {cy} {var} {val} {lab}")
        onload = [t for t in load_triggers if t[2] in keep]
        if onload:
            lines.append("onload")
            for (var, val, lab) in onload:
                lines.append(f"{var} {val} {lab}")
        if used_moves:
            lines.append("movements")
            for lab in sorted(used_moves):
                lines.append(f"{lab}\t{','.join(all_moves[lab])}")
        if keep:
            lines.append("scriptdefs")
            for lab, body in keep.items():
                lines.append("= " + lab)
                for instr in body:
                    if instr[0] in ("msgbox", "msgboxyesno") and len(instr) >= 2:
                        lines.append(instr[0] + "\t" + instr[1])
                    else:
                        lines.append(" ".join(instr))
        if signs:
            lines.append("signs")
            for (bx, by, text) in signs:
                lines.append(f"{bx} {by}\t{text}")
        # Tall-grass metatile ids that actually occur on this map.
        used_grass = sorted(set(ids) & grass_ids)
        if used_grass:
            lines.append("grass")
            lines.append(",".join(str(g) for g in used_grass))
        # Shop/PC-counter metatile ids on this map (see combined_counter_ids):
        # impassable, but interact() should look one tile past them.
        used_counters = sorted(set(ids) & counter_ids)
        if used_counters:
            lines.append("counters")
            lines.append(",".join(str(g) for g in used_counters))
        # Encounters are emitted for every map that has them; maps with land
        # encounters but no tall grass (caves/indoor) trigger on the floor.
        enc = encounters.get(mj.get("id"))
        if enc:
            lines.append("encounters")
            for etype in ("land", "water", "fishing", "rock"):
                slots = enc.get(etype)
                if slots:
                    lines.append(etype + " " +
                                 ",".join(f"{s}:{mn}:{mx}" for s, mn, mx in slots))
                    used_species.update(s for s, _, _ in slots)
        open(out, "w").write("\n".join(lines) + "\n")
        catalog.append({"map": mname, "w": w, "h": h, "tileset": sheet_name,
                        "npcs": len(npcs), "warps": len(warps),
                        "dialogs": sum(1 for n in npcs if n[5]),
                        "signs": len(signs), "scripts": len(keep),
                        "triggers": len(trig), "spawn": [sx, sy]})
        done += 1
        if limit and done >= limit:
            break

    with open(os.path.join(maps_dir, "index.json"), "w") as f:
        json.dump({"count": len(catalog), "maps": catalog}, f, indent=1)
    total_npcs = sum(c["npcs"] for c in catalog)
    total_warps = sum(c.get("warps", 0) for c in catalog)
    total_signs = sum(c.get("signs", 0) for c in catalog)
    total_dlg = sum(c.get("dialogs", 0) for c in catalog)

    total_scripts = sum(c.get("scripts", 0) for c in catalog)
    total_trig = sum(c.get("triggers", 0) for c in catalog)

    # New-game default state: which NPCs/items start hidden until their own
    # story beat unlocks them. The engine applies these flags once at a
    # fresh game's very first load.
    new_game_flags = parse_new_game_flags(src)
    with open(os.path.join(ASSETS_DIR, "new_game_flags.txt"), "w") as f:
        f.write("\n".join(new_game_flags) + "\n")

    # Colour the front sprites of every wild species that can be encountered.
    imported = sum(1 for sp in sorted(used_species) if import_pokemon_front(sp, src))
    print(f"world: {len(catalog)} maps, {len(pair_cache)} tileset sheets, "
          f"{total_npcs} NPCs, {total_warps} warps, {total_dlg} dialogs, "
          f"{total_signs} signs, {imported} wild sprites, "
          f"{total_scripts} scripts, {total_trig} triggers, "
          f"{item_total} item pickups, {len(new_game_flags)} new-game hide-flags "
          f"-> {os.path.relpath(maps_dir)}")


# --------------------------------------------------------------------------- #
# overworld -> flattened RGBA character / NPC sheets
# --------------------------------------------------------------------------- #
def flatten_indexed(src_png, dst_png):
    """P-mode 4bpp sheet -> RGBA, palette index 0 becomes transparent."""
    im = Image.open(src_png)
    if im.mode == "P":
        rgba = im.convert("RGBA")
        # index 0 = transparent backdrop in pokeemerald OW sheets
        idx = im.load()
        px = rgba.load()
        w, h = im.size
        for y in range(h):
            for x in range(w):
                if idx[x, y] == 0:
                    px[x, y] = (0, 0, 0, 0)
    else:
        rgba = im.convert("RGBA")
    rgba.save(dst_png)
    return im.size


def cmd_overworld(src):
    root = os.path.join(src, "graphics", "object_events", "pics")
    os.makedirs(OW_DIR, exist_ok=True)
    index = {}
    groups = ["people", "pokemon", "misc", "berry_trees"]
    for group in groups:
        gdir = os.path.join(root, group)
        if not os.path.isdir(gdir):
            continue
        for dirpath, _, files in os.walk(gdir):
            for fn in files:
                if not fn.endswith(".png"):
                    continue
                rel = os.path.relpath(os.path.join(dirpath, fn), gdir)
                key = group + "_" + rel[:-4].replace(os.sep, "_")
                dst = os.path.join(OW_DIR, key + ".png")
                try:
                    size = flatten_indexed(os.path.join(dirpath, fn), dst)
                    index[key] = {"file": key + ".png", "w": size[0], "h": size[1]}
                except Exception as ex:
                    print("  skip", rel, ex)
    with open(os.path.join(OW_DIR, "index.json"), "w") as f:
        json.dump(index, f, indent=2, sort_keys=True)
    walkers = [k for k in index if k.endswith("_walking")]
    print(f"overworld: {len(index)} sheets ({len(walkers)} walking) -> {os.path.relpath(OW_DIR)}")


# --------------------------------------------------------------------------- #
# audio -> cries + generated blips + optional MIDI->OGG
# --------------------------------------------------------------------------- #
def gen_blip(path, freq=880.0, ms=70, kind="sine", rate=22050, vol=0.35):
    n = int(rate * ms / 1000.0)
    frames = bytearray()
    for i in range(n):
        t = i / rate
        env = min(1.0, (n - i) / (rate * 0.02))     # short decay tail
        if kind == "square":
            s = 1.0 if math.sin(2 * math.pi * freq * t) >= 0 else -1.0
        elif kind == "noise":
            s = (hash((i, int(freq))) % 1000) / 500.0 - 1.0
        else:
            s = math.sin(2 * math.pi * freq * t)
        val = int(32767 * vol * env * s)
        frames += struct.pack("<h", max(-32768, min(32767, val)))
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
        w.writeframes(bytes(frames))


def have_synth():
    for t in ("fluidsynth", "timidity"):
        if shutil.which(t):
            return t
    return None


def cmd_audio(src, cry_limit=100000, songs=("mus_littleroot", "mus_route101", "mus_gsc_route38")):
    cries_src = os.path.join(src, "sound", "direct_sound_samples", "cries")
    cries_dst = os.path.join(SFX_DIR, "cries")
    os.makedirs(cries_dst, exist_ok=True)
    copied = 0
    for wav in sorted(glob.glob(os.path.join(cries_src, "*.wav")))[:cry_limit]:
        shutil.copy2(wav, os.path.join(cries_dst, os.path.basename(wav)))
        copied += 1
    print(f"audio: copied {copied} cries -> {os.path.relpath(cries_dst)}")

    # generated UI / movement blips (real, usable SFX for the engine right now)
    os.makedirs(SFX_DIR, exist_ok=True)
    gen_blip(os.path.join(SFX_DIR, "step.wav"), freq=660, ms=45, kind="square", vol=0.18)
    gen_blip(os.path.join(SFX_DIR, "bump.wav"), freq=160, ms=110, kind="square", vol=0.30)
    gen_blip(os.path.join(SFX_DIR, "select.wav"), freq=990, ms=60, kind="sine", vol=0.25)
    print("audio: generated step/bump/select blips")

    # MIDI -> OGG when a synth exists; otherwise document the requirement
    synth = have_synth()
    music_dst = os.path.join(SFX_DIR, "music")
    os.makedirs(music_dst, exist_ok=True)
    midi_dir = os.path.join(src, "sound", "songs", "midi")
    note = os.path.join(music_dst, "README.txt")
    if synth:
        for s in songs:
            mid = os.path.join(midi_dir, s + ".mid")
            if not os.path.exists(mid):
                continue
            ogg = os.path.join(music_dst, s + ".ogg")
            if synth == "fluidsynth":
                sf2 = next(iter(glob.glob("/usr/share/**/*.sf2", recursive=True)), None)
                if not sf2:
                    break
                subprocess.run(["fluidsynth", "-ni", sf2, mid, "-F", ogg.replace(".ogg", ".wav"),
                                "-r", "44100"], check=False)
            else:
                subprocess.run(["timidity", mid, "-Ow", "-o", ogg.replace(".ogg", ".wav")], check=False)
        print(f"audio: converted music via {synth}")
    else:
        with open(note, "w") as f:
            f.write("pokeemerald music is stored as MIDI (.mid). SFML cannot play MIDI.\n"
                    "Install fluidsynth (with a .sf2 soundfont) or timidity and re-run:\n"
                    "  python3 tools/pe_import.py audio --src /path/to/pokeemerald-master\n"
                    "to render the songs to OGG here.\n")
        print("audio: no synth found -> MIDI not converted (see sfx/music/README.txt)")


# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser(description="Codemon pokeemerald asset importer")
    ap.add_argument("cmd", choices=["all", "tilesets", "overworld", "audio",
                                    "map", "world", "battle", "extras"])
    ap.add_argument("--src", default=os.environ.get("POKEEMERALD",
                    ""), help="path to pokeemerald-master checkout")
    ap.add_argument("--layout", default="LittlerootTown_Layout",
                    help="layout name for the 'map' command (e.g. Route101_Layout)")
    args = ap.parse_args()
    if not args.src or not os.path.isdir(args.src):
        sys.exit("error: --src must point to a pokeemerald-master checkout "
                 "(or set $POKEEMERALD)")
    if args.cmd in ("all", "tilesets"):
        cmd_tilesets(args.src)
    if args.cmd in ("all", "overworld"):
        cmd_overworld(args.src)
    if args.cmd in ("all", "audio"):
        cmd_audio(args.src)
    if args.cmd == "map":
        cmd_map(args.src, args.layout)
    if args.cmd in ("all", "battle"):
        cmd_battle(args.src)
    if args.cmd in ("all", "extras"):
        cmd_extras(args.src)
    if args.cmd == "world":
        cmd_world(args.src)


if __name__ == "__main__":
    main()
