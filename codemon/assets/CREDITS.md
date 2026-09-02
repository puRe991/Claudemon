# Asset credits & licensing

The tilesets, overworld character/NPC sprites, Pokémon battle sprites and
Pokémon cries under `assets/tilesets/`, `assets/overworld/`, `assets/pokemon/`
(including the shiny set under `assets/pokemon/shiny/`, the same artwork read
through each species' second palette) and `assets/sfx/cries/` are derived from
the **pokeemerald** decompilation project (https://github.com/pret/pokeemerald).

* pokeemerald's own tooling and code are released under a permissive licence,
  but the **graphics, sprites and audio are the intellectual property of
  Nintendo / Game Freak / Creatures Inc.** They are reproduced here only for
  non-commercial, educational and fan-project use.
* Do **not** ship these assets in a commercial product. For a public or
  commercial release, replace them with original or properly-licensed art and
  sound (the import pipeline in `tools/` makes swapping the source trivial).

The generated blips `assets/sfx/step.wav`, `bump.wav` and `select.wav` are
synthesized from scratch by `tools/pe_import.py` and carry no third-party
rights.

NPC dialog lines under the maps' `dialogs` sections are extracted from
pokeemerald's scripts and are likewise Nintendo / Game Freak text.

The UI font `assets/fonts/DejaVuSans.ttf` is DejaVu Sans, released under the
permissive DejaVu Fonts License (a Bitstream Vera / free license); it is
redistributable and usable commercially.

## How these files were produced

Everything here is generated from a `pokeemerald-master` checkout by
`tools/pe_import.py` — see `tools/README.md`. Nothing in this folder is
hand-edited; re-run the importer to regenerate or extend it.

---

# Region art & asset credits

The hand-authored `region/` map set and its terrain tiles / player character
under `assets/art/` are **derived** from a
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
