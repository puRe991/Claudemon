# Asset credits & licensing

The tilesets, overworld character/NPC sprites and Pokémon cries under
`assets/tilesets/`, `assets/overworld/` and `assets/sfx/cries/` are derived
from the **pokeemerald** decompilation project (https://github.com/pret/pokeemerald).

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
