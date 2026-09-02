# Codemon asset tools

`pe_import.py` is the asset pipeline that brings **pokeemerald** content into
the Codemon SFML engine. It replaces the parts of the pokeemerald GBA build
toolchain that we actually need, reimplemented in portable Python so they emit
desktop-ready assets instead of Game Boy Advance binaries.

## Why the original scripts were rewritten

pokeemerald's `tools/` are C programs that build a GBA ROM:

| pokeemerald tool | what it does for the GBA | Codemon equivalent |
|------------------|--------------------------|--------------------|
| `gbagfx`         | PNG ⇄ 4bpp/8bpp + palette blobs | `pe_import.py tilesets` / `overworld` — decode 4bpp + apply the JASC palettes, output plain RGBA PNG |
| (GBA tile HW)    | metatile → screen via BG layers + per-tile palette | metatile renderer inside `pe_import.py` (`render_tileset`) |
| `wav2agb`        | WAV → GBA direct-sound blob | `pe_import.py audio` — copy WAV as-is (SFML loads WAV) |
| `mid2agb`        | MIDI → GBA sequence data | `pe_import.py audio` — MIDI → OGG via a desktop synth |
| `gbafix`, `ramscrgen`, `scaninc`, `preproc`, `bin2c`, `jsonproc`, `mapjson`, `rsfont` | ROM layout / codegen | **not applicable** — these only make sense for a GBA ROM build and have no role in an SFML desktop game |

So the pieces that carry *assets* (graphics, audio) are reimplemented; the
pieces that only assemble a GBA ROM are intentionally not ported.

## Usage

```sh
pip install Pillow
# point --src at a pokeemerald-master checkout
python3 tools/pe_import.py all --src /path/to/pokeemerald-master
```

Sub-commands: `tilesets`, `overworld`, `audio`, `all`, or `map`.

### Importing a real pokeemerald map

```sh
python3 tools/pe_import.py map --src /path/to/pokeemerald-master \
        --layout LittlerootTown_Layout
```

This reads the layout's `map.bin` (the metatile grid + collision bits) and its
primary+secondary tileset pair, renders a **combined** coloured metatile sheet
(`assets/tilesets/<primary>__<secondary>.png`, primary metatiles 0-511 then the
secondary ones from 512) and writes a ready-to-play map (e.g.
`maps/littleroot_town.map`) with a collision layer derived from the map data.
Any `*_Layout` in `data/layouts/layouts.json` works; the two tilesets are
resolved automatically. The game loads `maps/littleroot_town.map` by default
(override with the `CODEMON_MAP` env var).

### Outputs

* `assets/tilesets/<name>.png` (+ `.json`) — coloured 16×16 metatile sheets.
  The engine (`Tileset`) samples these directly; no runtime palette work.
* `assets/overworld/<key>.png` (+ `index.json`) — every character / NPC /
  pokemon walking sheet, flattened to RGBA with a transparent backdrop. Each is
  a single row of 16×32 frames (see `character.h`).
* `assets/pokemon/<SPECIES>.png` and `assets/pokemon/back/<SPECIES>.png` —
  64×64 battle artwork, coloured with the species' `normal.pal`, plus the same
  two views under `assets/pokemon/shiny/` coloured with its `shiny.pal`. On the
  GBA shininess is nothing but that second palette on identical pixels, so the
  engine picks a folder rather than recolouring anything at runtime.
* `assets/sfx/cries/<species>.wav` — Pokémon cries (SFML-loadable).
* `assets/sfx/{step,bump,select}.wav` — synthesized UI/movement blips.
* `assets/sfx/music/` — OGG music if a synth was available, otherwise a note
  explaining how to render the MIDI (SFML cannot play MIDI directly).

## Notes / limits

* **Secondary tilesets** (`data/tilesets/secondary/*`) borrow palettes and tiles
  from a paired primary, so they are not rendered stand-alone yet. The three
  self-contained primaries (`general`, `building`, `secret_base`) are.
* **Music**: install `fluidsynth` (with an `.sf2` soundfont) or `timidity`,
  then re-run `audio` to get OGG tracks. Without a synth the songs stay as
  un-playable MIDI and are skipped.
