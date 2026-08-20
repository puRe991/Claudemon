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
    groups = ["people", "pokemon", "misc"]
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
    ap.add_argument("cmd", choices=["all", "tilesets", "overworld", "audio", "map"])
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


if __name__ == "__main__":
    main()
