# Art & asset credits

The game's terrain tiles and the player character are **derived** from a
third-party pixel-art pack. Credit is required by that pack's licence.

## MyPixelWorld Special Packs #01 — by scarloxy

- Source: https://scarloxy.itch.io/mpwsp01
- Author: **scarloxy**
- Licence: **Creative Commons Attribution 4.0 International (CC-BY 4.0)**
  — https://creativecommons.org/licenses/by/4.0/
  — commercial use permitted **with attribution**.

Files in this repo taken from / derived from that pack:

| File | Origin |
|------|--------|
| `assets/art/mpwsp01_tileset.png` | the pack's `tileset/tileset.png` (unmodified) |
| `assets/art/mpwsp01_trainer.png` | the pack's `character overworld/ow1.png` (unmodified) |
| `region/region_tiles.png` | **derived**: terrain tiles + whole-building / tree stamps (houses, Oak's lab, trees) cut from the tileset, laid out one cell per tile id (see `tools/build_tiles.py` and `tools/stamps_def.py`) |
| `assets/Red_player.png` | **derived**: the `ow1` trainer re-laid into the engine's 3-frame × 4-direction sheet (see `tools/build_tiles.py`) |

The lab roof is the green house stamp recoloured red; the bush, wooden fence
and signpost tiles are **original** art drawn in `tools/build_tiles.py` (no
third-party source).

Regenerate the derived files with `python3 codemon/tools/build_tiles.py`.

## Not included

The following packs were considered but are **not** bundled here:

- **n3cloud — Pocket Creature Tamer Adventure Kit** (https://n3cloud.itch.io/pocket-creature-tamer-adventure-kit-16-x16-rpg-asset-pack).
  Only the free demo is available and its licence is **non-commercial, no
  redistribution**, so it cannot be committed to this repo.
- **bellblitzking — Pokémon Sound Collection** (https://bellblitzking.itch.io/pokemon-sound-collection).
  This is ~723 MB of ripped first-party Pokémon game audio (copyright
  Nintendo / Game Freak / The Pokémon Company). It is neither redistributable
  nor practical to commit. The engine ships an audio system (see
  `codemon/audio/`) with a small original placeholder sound; drop your own
  licensed audio into `assets/sfx/` to use it.
