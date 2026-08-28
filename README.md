# Cod-é-mon — a Pokémon clone

A 2D tile-based game engine, written portably in C++, with a Pokémon Emerald–style
game (**Cod-e-mon**) built on top of it. Both cross-compile to Unix (macOS, Debian-based
Linux) and Windows 8+, using CMake as the build system and SFML (Simple and Fast
Multimedia Library) for graphics, audio and windowing.

Note: Don't sue me, Nintendo business daddy.

## What's actually in it

This isn't a tech demo with placeholder art — the world, the Pokémon, and the
mechanics are all imported from a real **pokeemerald** (Pokémon Emerald
decompilation) checkout via `codemon/tools/pe_import.py`, then driven by an
independent C++ engine:

* **489 maps** rendered from the original tilesets, with real collision,
  warps/transitions (fades between maps), signs, and NPCs placed exactly where
  they are in-game.
* **A cooperative script VM** that runs pokeemerald's own event scripts —
  NPC dialog (multi-page, `\x1f`-separated), movement scripts, coordinate
  triggers, flags/vars — so NPCs and story events behave like the original.
* **A full turn-based battle system**: 385 species with real base stats,
  types and growth curves; 354 moves with power/type/accuracy; a 17-type
  effectiveness chart; STAB; the physical/special split by move type; wild
  encounters and 854 trainer battles with real parties.
* **Progression**: EXP gain and 6 species-specific growth curves, level-up
  stat recalculation, learning the correct move at the correct level
  (411 learnsets), and 172 evolution paths — all pulled from source data, not
  guessed.
* **TM/HM teaching**: the bag lets you teach a held TM or HM to any party
  member, gated by that species' real TM/HM learnset (372 entries); TMs are
  consumed on use, HMs are reusable, exactly like Gen III.
* **Wild encounters** that match pokeemerald's `wild_encounters.json`
  per map — species, level ranges and Gen-3 slot weighting (20/20/10/10/…) —
  including cave/indoor floors that have encounters but no visible grass.
* **Story-accurate start**: new games begin in the player's bedroom
  (Brendan's House 2F) at the canonical heal-location tile, not a guessed
  spawn point, with an empty team and an empty bag (just the canonical 3000
  money) — no Pokémon or items until the real story hands them over, same as
  pokeemerald.
* **UI**: a start menu with Bag, Party, PC Box and PokéNav screens, capture
  and storage, item/type/species icons, an HP bar, a map-name banner on
  transitions, and a few overworld minigames (slots/roulette/blender/jump)
  using an in-game coin currency.
* **Audio**: Pokémon cries (wild encounters, trainer sends-out, switching),
  per-map background music, and battle/victory themes, all converted from
  the original MIDI to OGG and playing live, plus generated step/bump/select
  SFX.

Everything above is verified against the source data (not fabricated): the
import pipeline is data-driven, and every feature has been checked with
headless rendering (`CODEMON_SCREENSHOT=...`) against what pokeemerald
actually does for that map/species/trainer.

## Screenshots

<table>
<tr>
<td><img src="docs/screenshots/spawn_bedroom.png" width="380" alt="Story-accurate spawn in the player's bedroom"><br><sub>Story-accurate spawn: Brendan's House 2F</sub></td>
<td><img src="docs/screenshots/overworld_littleroot.png" width="380" alt="Littleroot Town overworld"><br><sub>Littleroot Town, imported 1:1 from pokeemerald</sub></td>
</tr>
<tr>
<td><img src="docs/screenshots/battle_wurmple.png" width="380" alt="Wild battle against a Wurmple on Route 102"><br><sub>Wild encounter on Route 102, matching the source encounter table</sub></td>
<td><img src="docs/screenshots/tm_teach.png" width="380" alt="Teaching Giga Drain to Treecko from the bag"><br><sub>Teaching a TM from the bag, gated by the real learnset</sub></td>
</tr>
</table>

## Backstory

#### This part is 95% ok to skip — it's mostly rambling about why I started this

In 2010, when I first began to learn to code in a systematic way, I had
starry-eyed dreams of becoming a video games programmer. I think this is
likely a natural pipeline into computer science for many — game dev seems
like a lot of crunch for rarely enough pay, to me now. <br><br>
Any-hoozles. <br><br>
As I had at that time already learned Basic, and was learning C in my
highschool programming class, I decided to try my hand at a pure-C Pokémon
clone. I submitted it as a summative project. It never ran to my
satisfaction, and apparently it never ran at all on Mr. Krealman's PC. I got
a good grade anyway, somehow. (Thanks Mr. K.) <br>
It was a good piece of code for a highschooler — it had transitions, double
buffering, a working (much to my astonishment) camera system, battles, et
cetera. <br><br>
My issue was that it was never portable or reliable — simply put, 'twas
buggy as all get-out, and the amount of *ahem* assistance I received on the
project made me feel like it was never truly my work. (Thanks Geoffrey S.)
<br>
Now, having finally finished my formal education and amassed a decade or so
of practical experience, let's try our hand at reimplementing this solution.

#### TL;DR: this is an exercise in catharsis, as well as a way for me to gauge my growth as a dev

## Exec. summary

### A from-scratch C++/SFML reimplementation of Pokémon Emerald, with real data imported from pokeemerald

## Project goals

#### In no particular order, subject to change at any time as the mood strikes me

> Multiple maps
> > * Easy transitions between maps — "easy" meaning no noticeable loading
> >   times. ✅ done — warp fade, map-name banner
> > * Transition animations: wipes, fades. If you're thinking of early-2000s
> >   video-editing-software-style transitions, you're in the right ballpark.
> >   ✅ fade done; wipes not yet
>
> Wild Codemon areas
> > * A battle system ✅ done — turn-based, trainers + wild, capture
> > * RPG progression system inspired by, but hopefully not hopelessly
> >   derivative of, Pokémon. ✅ done — EXP, levels, evolution, TM/level-up
> >   movesets
>
> A map editor would be nice
> > * Procedural map generator — stochastic, probably noise-based. Not started;
> >   maps are currently imported 1:1 from pokeemerald.
>
> Music
> > * Jazz, maybe? MIDI→OGG conversion exists; the soundtrack itself is
> >   whatever pokeemerald ships.
>
> General goals
> > * No algorithms with runtimes beyond n·log(n)
> > * Unit-testing harness suite ✅ `codemon_tests`, wired into CTest
> > * Would be nice if the harness suite also timed each major algorithm
> >   automagically. Not yet.

## Sprites

Pokémon sprite artwork is pulled in from [PokeAPI/sprites](https://github.com/PokeAPI/sprites)
as a git submodule under `sprites/`. Clone the project with submodules to get it:

```sh
git clone --recurse-submodules <repo-url>
# or, in an existing checkout:
git submodule update --init --depth 1 sprites
```

## World & battle data from pokeemerald

The overworld, battle system and Pokémon data are all built from
**pokeemerald** source files, imported into engine-ready form by
`codemon/tools/pe_import.py` (see `codemon/tools/README.md`). That script is
the SFML-friendly replacement for the pokeemerald GBA build tools:

* **Tilesets** → fully coloured 16×16 metatile sheets in
  `codemon/assets/tilesets/` (palettes + metatile layers resolved at import
  time, so the engine just samples the sheet — no runtime palette work).
* **Maps** → `codemon/maps/*.map`: metatiles, collision, warps, NPCs (with
  dialog and movement scripts), signs, coordinate triggers, and wild
  encounter tables, one file per pokeemerald map.
* **Characters / NPCs** → every overworld walking sheet in
  `codemon/assets/overworld/` (16×32, 9-frame layout); the player and NPCs
  share the `Character` class, with east reusing west frames mirrored.
* **Battle data** → `codemon/assets/battle/*.tsv`: species (stats, types,
  growth curve, EXP yield), moves, level-up learnsets, TM/HM learnsets and
  the TM→move table, evolutions, and trainer parties.
* **Audio** → Pokémon cries plus generated step/bump/select blips in
  `codemon/assets/sfx/`, played through the `Audio` class. MIDI music is
  converted to OGG via fluidsynth/timidity + ffmpeg when installed (SFML
  can't play MIDI directly); each map's own `MUS_*` id is carried through
  into its `.map` file and resolved to `assets/sfx/music/<id>.ogg`.

Re-run or extend the import with:

```sh
pip install Pillow
python3 codemon/tools/pe_import.py all --src /path/to/pokeemerald-master
```

Licensing note: the imported graphics/audio/text remain Nintendo/Game Freak
property and are for non-commercial fan use only — see
`codemon/assets/CREDITS.md`.

## The Region

The game world is a data-driven **region** under [`codemon/region/`](codemon/region/):
a manifest (`kanto.region`) plus one map file per area, wiring ~30 areas
(coastal village → cities → forests → caves → industry → islands → a monumental
mountain complex) into one connected, two-way-traversable world.

The `Region` class (`codemon/Region.h`) loads the manifest headlessly (no SFML),
and `main.cpp` walks the player from area to area through **warp** tiles. See
[`codemon/region/README.md`](codemon/region/README.md) for the geography, the
area list, the connection graph and the file formats. Regenerate the data with
`python3 codemon/tools/gen_region.py`.

## Art & audio

Terrain tiles (`region/region_tiles.png`) and the player trainer
(`assets/Red_player.png`) are derived from **scarloxy's "MyPixelWorld Special
Packs #01"** (CC-BY 4.0); the source art lives in `assets/art/` and the derived
sheets are rebuilt with `python3 codemon/tools/build_tiles.py`. The game plays
short **original** placeholder sound effects from `assets/sfx/` (a bump when a
move is blocked, a blip on area transitions), regenerated with
`python3 codemon/tools/make_sfx.py`; drop your own licensed audio in with the
same names to replace them. See [`codemon/assets/CREDITS.md`](codemon/assets/CREDITS.md)
for attribution and for why the other requested packs are not bundled.

## Map editor

A standalone external editor, `codemon_editor`, paints the region's `.map`
files against the real terrain sheet (WYSIWYG with the game). Build it with the
same CMake, then from the build directory run `./codemon_editor
region/maps/<area>.map` — left-click the palette to pick a tile, drag to paint,
`S` to save. See [`codemon/editor/README.md`](codemon/editor/README.md).

## Building & Running

The project builds with CMake and needs a system install of **SFML 2.5+**.

```sh
# Dependencies (Debian/Ubuntu)
sudo apt-get install libsfml-dev cmake g++

# Configure & build
cmake -S . -B build
cmake --build build

# Run the tests (headless, no display needed)
ctest --test-dir build --output-on-failure

# Start the game (needs a display; maps/ and assets/ are copied next to the binary)
./build/codemon
```

**Controls**: WASD to move, Space/Enter to confirm/interact, `M` opens the
menu, `G` opens minigames (where available), hold `Shift` to run (once the
Running Shoes are received).

### Windows

Double-click **`run-windows.bat`** (or run it from a terminal). It hands off to
`scripts\windows-setup.ps1`, which **checks the build dependencies, installs any
that are missing (via winget), then builds and starts the game** in one step:

```bat
run-windows.bat            :: Release build, then run
run-windows.bat Debug      :: Debug build, then run
```

What it does:

- Checks for **CMake** and a **C++ toolchain** (MSVC). If either is missing it
  installs it with `winget` (Kitware.CMake / Visual Studio Build Tools with the
  "Desktop development with C++" workload), asking for administrator rights via
  UAC when needed.
- Configures and builds with CMake. By default it links the MSVC SFML bundled
  under `codemon\SFML` statically, so no separate SFML install or DLLs are
  needed. If that fails to link (toolset mismatch), it automatically retries by
  building SFML from source (`-DCODEMON_FETCH_SFML=ON`). You can also point at
  your own SFML with `set SFML_DIR=...` before running.
- Keeps the console window open on exit so any messages are readable.

The launcher needs winget (ships with Windows 10/11 as "App Installer"). The
older Visual Studio solution (`codemon.sln`) is still present but uses
machine-specific absolute paths; the launcher/CMake path is preferred.

#### 32-bit Windows (no build needed)

Modern C++ toolchains no longer run on 32-bit Windows (Visual Studio 2022 is
64-bit only), so the game cannot be built locally there. Instead, download the
prebuilt 32-bit binary produced by CI:

1. Open the [Windows 32-bit build workflow](https://github.com/puRe991/Claudemon/actions/workflows/windows-build.yml).
2. Open the newest successful run and download the **`codemon-windows-x86`** artifact.
3. Unzip it and double-click `codemon.exe` (keep `maps\` and `assets\` next to it).

That build is statically linked, so it needs no SFML DLLs and no Visual C++
redistributable.

The `codemon_tests` target exercises the display-independent core logic
(`Coordinates`, `Tile`, `TileMap`, `Region`, `MenuModel`, the letterbox math)
and is wired into CTest.

### Map editors

Two standalone editors ship alongside the game, for two different map sets:

- **`codemon_editor`** — an SFML tool that paints the hand-authored
  `codemon/region/` map set (WYSIWYG with the game's own terrain sheet). See
  [`codemon/editor/README.md`](codemon/editor/README.md); left-click the
  palette to pick a tile, drag to paint, `S` to save.
- **`codemon_editor_imgui`** — a Dear ImGui tool that paints the
  pokeemerald-imported `codemon/maps/` CSV maps (it fetches Dear ImGui +
  ImGui-SFML at configure time, so the first configure needs network access;
  disable it with `-DCODEMON_BUILD_EDITOR=OFF`). Left click paints, right
  click erases to grass, tick "Place player start" to move the spawn, resize
  the map, Save/Reload in the same CSV format the game loads. Maps use the
  tileset generated by `tools/gen_overworld_tileset.py`.

```sh
./build/codemon_editor region/maps/<area>.map
./build/codemon_editor_imgui
```

### Headless / CI mode

The game binary supports rendering off-screen to PNGs for testing and demos —
useful under `xvfb-run` in CI, with no window needed:

```sh
CODEMON_SCREENSHOT=out.png CODEMON_FRAMES=20 \
CODEMON_WALK=N,N,E,T,M,S,G \
xvfb-run -a ./build/codemon
```

Key env vars: `CODEMON_MAP` (start map), `CODEMON_WALK` (a comma-separated
token script: `N/S/E/W` move, `T` talk/advance dialog, `M` toggle menu, `G`
toggle minigames), `CODEMON_FRAMES`, `CODEMON_FORCE_ENCOUNTER` /
`CODEMON_NO_WILD`, `CODEMON_GRANT_EXP` (grant the starter EXP, to see
level-ups/evolution in one shot), `CODEMON_NO_SAVE` (ignore an existing
`savegame.dat` and start fresh), and `CODEMON_TEST_SCRIPT` (run a script by
its label immediately at startup — handy for exercising a specific event
script, e.g. a legendary encounter, without navigating to it in-world).

## Testing

`codemon_tests` exercises the display-independent core logic (`Coordinates`,
`Tile`, the `Region` manifest/warp graph) and is wired into CTest.

## Project status

A snapshot of what's real right now versus what's still missing, based on a
full audit of the engine (`codemon/*.cpp/.h`), the ScriptVM opcode dispatch
cross-checked against every opcode actually used across all 489 imported
maps, and the importer. This is not aspirational — everything marked done
has been verified either by an automated test or by headless screenshot
(`CODEMON_SCREENSHOT=...`) against what pokeemerald actually does.

### ✅ What works

* Map rendering, collision, warps/transitions, signs, coordinate triggers,
  NPC placement & movement types (static/wander/pace), load triggers
* Seamless map connections (pokeemerald's edge-to-edge world stitching --
  Route101's north edge continuing into Oldale Town, etc.), on top of
  door/warp transitions; smooth per-tile sliding movement (camera included)
  driven by steady per-frame input polling instead of OS key-repeat timing
* Cooperative script VM (`ScriptVM`) running pokeemerald's real event
  scripts: dialog (multi-page), flags/vars, `goto`/`call` + `eq`/`ne`/`set`/
  `unset` conditionals, `switch`/`case`, movement scripts, `giveitem`/
  `finditem`, `setmetatile`
* Turn-based battles: 385 species (real stats/types/growth curve), 354
  moves (power/type/accuracy/effect data parsed), 17-type effectiveness
  chart, STAB, physical/special split, an accuracy roll on every move, wild
  encounters (real per-map tables + Gen-3 slot weighting), 854 trainer
  battles with real parties, trainer rematches
* Status conditions — sleep, poison, toxic, burn, paralysis, freeze, and
  confusion — driven by each move's real pokeemerald effect data (Toxic,
  Thunder Wave, Sleep Powder, Will-O-Wisp, Confuse Ray, and the % secondary
  chance on hits like Body Slam/Ice Beam/Flamethrower/Poison Sting), with
  real turn-blocking, end-of-turn damage, paralysis' quartered Speed, burn's
  halved physical damage, and Gen-3 type immunities
* Switching Pokémon mid-battle (a POKéMON option in the action menu), and
  surviving a faint by switching to a healthy party member instead of an
  automatic loss — the battle only ends once the whole team is down
* Real win/loss gating on scripted trainer battles (gym leaders, the Elite
  Four, the Champion, every ordinary trainer): losing no longer continues
  the "you won" story script that follows the battle command — instead the
  team is healed and the player is whited out back to the last Pokémon
  Center visited, same as losing any battle in the real games (including
  random wild encounters)
* Capture mechanic (approximate probability, Poké Ball only — see below)
* Scripted legendary/static encounters (Regirock/Regice/Registeel,
  Rayquaza, Kyogre/Groudon, Kecleon, the New Mauville Voltorb swarm) via
  `setwildbattle`/`dowildbattle`, with a real WON/LOST/RAN/CAUGHT battle
  outcome scripts can branch on (`specialvar VAR_RESULT GetBattleOutcome`)
* Mossdeep Gym's rotating-tile floor puzzle (and Trick House Puzzle #7's
  identical mechanic): pressing a switch actually shifts and re-faces every
  character standing on that color's arrow tiles, ported from pokeemerald's
  real metatile-driven algorithm
* Cut as a real field move: a party member that knows it (taught via HM01)
  can chop down any of the game's cuttable trees (`checkpartymove`,
  `bufferpartymonnick`/`buffermovename` message interpolation,
  `removeobject` permanently clearing the tree, gated on the first Badge,
  same checks/messages as pokeemerald) — Rock Smash uses the identical
  shared opcodes/script shape so it works the same way
* Running Shoes: once received (`FLAG_SYS_B_DASH`), holding Shift moves at
  the real games' exact 2x walk speed — the GBA's hold-B-to-run adapted to
  a PC control (there's no B button), same gated flag as the original
* All 4 in-game trades (Rustboro/Fortree/Pacifidlog/Battle Frontier Lounge6
  NPCs): a real party-list picker (`ChoosePartyMon`), species matching
  (`GetTradeSpecies`/`GetInGameTradeSpeciesInfo`, own stable species-id
  table in `BattleData`), and the swap itself (`CreateInGameTradePokemon`,
  matched level like the original — no IVs/personality/held mail here,
  same as nothing else in this engine has those either)
* EXP gain, all 6 species growth curves, level-up stat recalculation,
  level-up movesets (411 learnsets), **level-up evolution** (172 paths) —
  including a real EXP bar (progress to next level, same growth-curve math
  as the gain itself), shown under the active mon's HP bar in battle and
  next to each party member in the POKéMON menu; EXP gain/level-up/move-
  learned/evolution text already existed, just no visual bar before
* TM/HM teaching from the bag, gated by real per-species learnsets (372
  entries); TMs consumed on use, HMs reusable (this was silently broken for
  *every* TM/HM the whole time before — see the fix note below)
* Surf: badge-gated water-tile classification (pokeemerald's own surfable
  metatile behaviors), a held-Shift-style Ja/Nein prompt the first time you
  approach open water, automatic dismount back on dry land, and its own
  water wild-encounter table (5-slot Gen-3 rates) — without this, roughly
  the entire back half of Hoenn (Mossdeep, Sootopolis, Ever Grande/the
  Pokémon League itself) was unreachable
* Strength: activating it is the same generic `checkpartymove`/Ja-Nein/
  `setflag FLAG_SYS_USE_STRENGTH` script shape Cut and Surf already use (so
  it worked for free), plus a new native push mechanic in `player_step()`
  mirroring pokeemerald's `TryPushBoulder` — walking into a
  `misc_pushable_boulder` NPC with Strength active shoves it one tile
  further in the same direction (checked for bounds/collision/water) instead
  of just blocking, gating every Trick House/Seafloor Cavern/Victory Road
  boulder puzzle
* Waterfall: same water-tile infrastructure as Surf, plus its own
  MB_WATERFALL metatile classification — climbing one (moving north into it
  while already surfing) prompts the same Ja/Nein pattern; any other
  direction/approach (floating down or across its base) stays ordinary open
  water, matching pokeemerald's `IsPlayerSurfingNorth` check. Gates Route
  119, Meteor Falls, Route 114, Victory Road B2F, the Ever Grande/Battle
  Frontier/Safari Zone Southeast waterfalls
* Fly: a new FLIEGEN entry in the start menu lists every town/city actually
  visited (pokeemerald's `MAP_SCRIPT_ON_TRANSITION` unconditionally setting
  that map's own `FLAG_VISITED_*`, now tracked on every map entry) and warps
  straight to it, landing at the same tile as that town's own heal location.
  Gated on the team knowing FLY (badge check omitted, same simplification
  already used for Surf/Strength/Waterfall)
* Dive: pressing A while surfing over deep water (pokeemerald's own
  `MetatileBehavior_IsDiveable` metatiles) drops to the underwater map
  hanging off that map's `dive` connection, at the same tile; pressing A
  underwater surfaces again. Maps without such a connection use their
  `setdivewarp` target instead, which brings its own arrival tile. Gated on
  the 7th badge *and* a party member knowing the move — deliberately
  stricter than the badge-free convention the other HMs use here, since
  `FLAG_BADGE07_GET` is exactly what the real check reads.
  **This is what makes Sootopolis City reachable at all**: it has no land
  route, no warp and no map connection from outside, so surfacing out of
  `Underwater_SootopolisCity` is the only entrance. Without Dive the 8th
  gym, the Cave of Origin and the whole endgame were cut off, and the 14
  imported `Underwater_*` maps were unreachable dead weight
* Trainers challenge the player on sight, along their own facing direction
  up to their real per-trainer range, with walls and other NPCs breaking
  the line of sight (pokeemerald's `GetTrainerApproachDistance`). 530
  trainers across the 489 maps carry a range; previously every one of them
  had to be walked up to and talked to. A trainer only watches while the
  player still owes them a battle
* Scripted multi-NPC cutscenes: `applymovement`/`addobject`/`removeobject`/
  `hideobject`/`showobject` targeting a specific object event by its
  `LOCALID_*` name (not just "the NPC just talked to"), e.g. Petalburg Gym's
  Wally tutorial battle
* `multichoice`/`multichoicedefault` prompts: a real cursor-driven option
  list (fishing quality, contest info, the Game Corner shop, the Trick
  House's 15-question Mechadoll trivia puzzle, the Devon Corp fossil choice,
  ...) — 55 option lists hand-resolved from pokeemerald's own C source
  (`src/data/script_menu.h`/`strings.c`) for every already-imported
  non-Battle-Frontier use
* Using healing/revive/status-curing bag items (Potion family, soft drinks,
  berries, Revive/Max Revive, Antidote/Paralyze Heal/Awakening/Burn Heal/
  Ice Heal/Full Heal/Full Restore) on a chosen party member, with real
  Gen-3 heal amounts and status-cure matching
* Shops (`pokemart`) and Ja/Nein prompts via the VM block-and-resume pattern
* Story-accurate start (Brendan's House 2F, canonical heal-location tile,
  empty team/bag until the real story hands them over)
* A real title screen at launch (real interactive play only — headless
  screenshot tests and `CODEMON_MAP` demos still load straight into a map,
  same as before): FORTSETZEN/NEUES SPIEL when a save exists, just NEUES
  SPIEL otherwise. No licensed title logo art was importable (the intro
  cutscene's own animated assets are all there, just no wordmark), so it's
  text-only, styled like the rest of the UI. Previously the game skipped
  straight into gameplay on launch with no menu at all
* Player identity, chosen once at the start of a brand new game (skipped
  when continuing a save): a GenderSelect screen (Brendan/May portraits,
  real games' own character-select convention) picks the overworld sprite
  sheet and `checkplayergender`'s answer, then a grid-driven NameEntry
  screen (A-Z + DEL/OK, WASD-navigated like every other menu here rather
  than free keyboard typing) names yourself and your rival. Both names
  replace the literal "PLAYER"/"RIVAL" tokens dialogue text carries --
  pe_import.py turns pokeemerald's `{PLAYER}`/`{RIVAL}` escape codes into
  those literal words on import (137 map files use one), and previously
  they were never substituted back out, so dialogue really did say
  "PLAYER inserted and turned the KEY." verbatim
* An OPTIONS screen in the start menu (a curated subset of the real
  games' SaveBlock2 options -- Text Speed and Battle Style aren't
  implemented, see below): Ton (Sound) mutes SFX/cries and drops the
  current/future music track's volume to 0 without stopping the stream
  (so unmuting picks back up mid-track); Kampfszene (Battle Scene) skips
  the hit-shake/flash animation on a damaging hit, HP still updates
  instantly; Rahmenart (Frame Type) cycles through all 20 real alternate
  window-border designs (`assets/graphics/text_window/1..20.png`),
  applied live to the start menu and every dialogue box. All three
  persist in the savegame
* UI: start menu (Pokédex/Bag/Party/PC Box/PokéNav), map-name banner, HP
  bars, item/type/species icons, Pokémon Center heal (whole team) + its
  glowing-Pokéball/monitor animation
* Pokédex: real seen/caught tracking (a species is "seen" from any battle
  encounter, wild or trainer, "caught" from a successful catch, a starter
  pick, or an in-game trade), persisted with the save; a scrollable list
  screen showing name/icon for a seen species and "? ? ? ? ?" for an
  unseen one, plus a live Gesehen/Gefangen count -- species order is
  species.tsv's own (alphabetical) order, not the real games' curated
  Hoenn Dex numbering (not imported)
* Party summary screen (select a party member to see it): type icon(s),
  HP/EXP bars, nature and ability, all five non-HP stats with the real
  games' own red/blue boosted/lowered-by-nature coloring, and its moves
* Overworld minigames: Slot Machine, Roulette, Berry Blender, Pokémon Jump
  (real gameplay, not stubs), with an in-game coin currency
* Save/load: a SPEICHERN entry in the main menu writes the whole run (map,
  position, flags/vars, bag, money, party, PC box) to `savegame.dat`;
  starting the game resumes from it automatically
* Audio: step/bump/select SFX, per-map background music (switches on every
  map transition/warp/fly), Pokémon cries (wild encounters, trainer
  send-outs, mid-battle switches, `playmoncry` field scripts), battle/
  victory themes (wild, trainer, gym leader/champion/rival/Elite Four
  variants), and `playbgm` field-script music cues all play natively
* Gen-3 in-battle stat stages (-6..+6, Attack/Defense/Sp.Atk/Sp.Def/Speed/
  accuracy/evasion): every stat-changing move (Growl, Leer, Swords Dance,
  Bulk Up, Calm Mind, Haze, ~50 in total) actually raises/lowers the right
  stat now, with the real "won't go any higher!" cap message; stages reset
  whenever a side's active mon changes, same as the real games
* Critical hits (Gen-3 stage odds: 1/16 base, 1/8 for a HIGH_CRITICAL move
  like Slash/Cross Chop, +2 stages from Focus Energy, capping at 1/2),
  ignoring the attacker's own stat drop and the defender's own boost the
  same way the real games do; verified over 1000+ simulated hits landing
  at ~12.5% for a high-crit move (expected 1/8)
* Move priority brackets (Quick Attack/Mach Punch/Extreme Speed act before
  Speed is even checked, Vital Throw always goes last), verified against a
  much faster opponent
* Battle weather (Rain Dance/Sunny Day/Sandstorm/Hail, 5 turns, the latest
  overwrites any previous one): Rain/Sun give Water/Fire moves their real
  1.5x/0.5x swing, Sandstorm/Hail chip 1/16 max HP a turn from anything not
  Rock/Ground/Steel or Ice respectively, clearing with its own message on
  expiry -- never carries over between battles
* Every species' real ability (pokeemerald's `.abilities`, always slot 1 --
  this engine doesn't model the personality-value bit that picks between
  two in the real games) and a hand-picked subset of the ones that matter
  in ordinary play: Intimidate (drops the opposing side's Attack a stage
  on switch-in), Drizzle/Drought/Sand Stream (set their weather on
  switch-in), Levitate (immune to Ground moves), Wonder Guard (only a
  super-effective hit does anything), and the status-immunity abilities
  (Insomnia/Vital Spirit, Immunity, Limber, Water Veil, Magma Armor, Own
  Tempo) -- the other ~65 Gen-3 abilities are recognized in the data but
  have no effect yet
* Real IVs (0..31 per stat) and natures (one of the real 25, boosting one
  stat 10% and lowering another, five neutral): every `make_mon()` call
  that's handed the shared RNG rolls both once and keeps them for that
  individual's whole life (saved with the rest of the mon), using the
  real Gen-3 stat formula -- so two Level 50 Machop are no longer
  identical. A caught wild Pokémon keeps the IVs/nature it already had
  during the encounter rather than rolling new ones. EVs are still always
  0 (no battling-based EV gain tracked)
* Real per-move PP (pokeemerald's `.pp`), shown in the move menu and
  decremented on every use (even a miss; not when the turn itself is
  blocked by sleep/paralysis/confusion self-hit). Picking a move at 0 PP
  in the menu is blocked with a message instead of resolving the turn; once
  every move is at 0 PP the game skips the move menu entirely and forces
  Struggle, same as the real games -- including Struggle's real hardcoded
  behavior of bypassing the type chart and STAB entirely (it's typed
  NORMAL in the data but the actual C engine special-cases it). Recoil
  (Take Down, Submission, Struggle) deals `max(1, dmg/4)` back to the
  attacker, applied even if that same hit faints the target. PP is
  restored to max on every full heal (Pokémon Center, whiteout recovery)
  and persisted with the rest of the mon in the savegame
* Wild held items (pokeemerald's `.itemCommon`/`.itemRare`, e.g. Pikachu's
  Oran Berry / Light Ball): rolled once in `make_mon()` using the real
  odds (a species whose common and rare item are the same always holds it;
  otherwise 45% nothing / 50% common / 5% rare, verified over 5000 rolls),
  kept for that individual's whole life like IVs/nature, and shown on the
  party summary screen. A hand-picked subset of items that matter in
  ordinary play actually do something: status-curing berries (Cheri/
  Chesto/Pecha/Rawst/Aspear/Persim cure their one status or confusion, Lum
  cures anything), Oran/Sitrus (heal at <=50% HP), Leftovers (heal 1/16
  max HP every end of turn), the real Gen-3 type-boosting items (+10%
  damage for their one type, e.g. Black Belt for Fighting moves), and
  Everstone (blocks level-up evolution) -- the rest (evolution stones,
  Shards, Nuggets, Quick Claw, King's Rock, ...) are recognized in the
  data but have no effect yet (no turn-order-roll or flinch mechanic to
  hook them into)
* **PokeNav region map**: the POKéNAV screen draws the real Hoenn town map
  image (`assets/graphics/pokenav/region_map/map.png`, the genuine unused
  pokeemerald asset), the player's gendered marker icon
  (`brendan_icon.png`/`may_icon.png`), and a real moving cursor -- arrow
  keys walk it tile by tile over the region map's 28x15 grid, naming
  whichever section it's currently over (the importer now also dumps the
  *full* 213-entry `region_map_sections.json` table to
  `assets/pokenav/region_map_sections.tsv`, not just the current map's own
  section, precisely so the cursor can name any cell, not only where the
  player is standing). Matches the real games' PokeNav map screen: it's a
  browsable view, not a destination picker -- it doesn't fly you anywhere
  itself (see below). Per-map `mapsec` grid rectangles (28x15 logical grid,
  `src/data/region_map/region_map_sections.json`) come from the same
  importer pass; indoor maps correctly inherit their outdoor town's
  section. Grid-to-pixel uses the map image's actual 128x120 size (128/28
  horizontal, 120/15 vertical), calibrated and verified against known
  real-world town positions (Littleroot, Ever Grande, Sootopolis,
  Fallarbor, Pacifidlog, Mossdeep all land in geographically correct spots)
* **More script opcodes**: `bufferstring`/`buffernumberstring` (dynamic
  values -- Birch's Pokédex-seen/caught rating and similar -- now actually
  render instead of leaving the number/word out); `bufferleadmonspeciesname`;
  `getplayerxy`; `hideplayer`/`showplayer`; `setobjectmovementtype`
  (an NPC's behaviour can now change at runtime, not just at map load);
  `setdynamicwarp` (`WARP_ID_DYNAMIC` tiles resolve to whatever a script set
  last -- this is what gets the player out of the intro moving truck and
  drives the Lilycove department store elevator; the truck's own exit tiles
  used to just silently do nothing, a genuine dead end)
* **Importer**: `#ifdef UBFIX`/`#ifdef BUGFIX`/`#ifndef BUGFIX` (+ `#else`/
  `#endif`) conditionals in source scripts are now actually resolved (both
  symbols undefined, matching this repo's default build config) instead of
  leaking the raw `#ifdef`/`#else`/`#endif` lines into the exported script
  and including both branches' instructions back to back
* **Trainer AI items**: a trainer's own real `.items` field (Full Restore/
  Hyper Potion/.../Full Heal, ~141 trainers -- mostly Gym Leaders, rivals,
  the Elite Four) is imported and actually used: at <=50% HP (or any status
  with nothing better to do) the AI heals/cures instead of attacking, once
  per item, same as the real games. `ai_move()` also now weighs a move's
  accuracy against its damage (a 120-power/70%-accuracy move no longer
  automatically beats a slightly weaker move that reliably lands)
* `codemon_tests` / CTest for the display-independent core data structures

### ⚠️ Partial / simplified

* **Battle system** is a simplified 1v1 damage calculator: no EVs tracked
  yet (no battling-based EV gain), and only a hand-picked subset of held
  items actually does anything (see above); see
  `codemon/tools/pe_import.py`'s usage note for how to point the
  importer at a pokeemerald checkout (no doubles, no EXP Share either;
  status conditions, accuracy, stat stages, critical hits, move priority,
  weather, IVs/natures, PP/Struggle/recoil, held items, a real per-species
  catch formula, and a starter set of abilities are implemented, see above)
* **Catch mechanic**: `ITEM_POKE_BALL` is the only ball that functionally
  exists (no ball-type bonus), but the odds themselves are the real Gen-3
  formula now -- each species' actual catch rate, current/max HP, and a
  sleep/freeze/other-status bonus, verified against known values (a
  full-HP Magikarp catches ~1/3 of the time, a full-HP Registeel ~1/255)
* **Evolution**: level-up only (an Everstone blocks it, same as the real
  games); stone/trade/friendship evolutions are imported into data but
  never triggered in-game
* **Audio**: `playse` (one-off pokeemerald SE_* sound effects beyond
  step/bump/select) is still a no-op — only the small hand-made SFX set was
  imported, not every original SE_* WAV; `MUS_ROUTE118`'s real behavior
  (dynamic pick between MUS_ROUTE110/MUS_ROUTE119 by player x position) is
  simplified to a static MUS_ROUTE119 default; Battle Frontier's own
  MUS_B_* tracks convert but that mode isn't implemented (see below)
* **`specialvar` opcode**: `GetBattleOutcome` and `PlayerHasBerries` return
  real answers; everything else (`ShouldTryRematchBattle`, Pokérus, trading,
  breeding, contests, the fan club, Trainer Hill, ...) honestly returns
  0/false since those systems don't exist here, rather than faking one
  (this also means `gotobeatenscript` -- gated on `ShouldTryGetTrainerScript`
  -- never actually branches; not a missed opcode, a consequence of that
  same honesty)
* **`setobjectsubpriority`/`resetobjectsubpriority`**: real opcodes,
  intentional no-ops -- they're a draw-order tiebreak for two objects
  sharing a tile, and this engine already sorts every actor by tile-y every
  frame, which already gives the right order for every real use of them
* **PC item storage / multiple PC boxes**: `checkpcitem` and
  `bufferboxname` honestly read as "nothing there" -- there is only one
  unlimited party-mon box, no separate item storage
* **Trainer AI switching**: a trainer's bench mons are only ever species+level
  (regenerated fresh via `make_mon` the moment they're sent out), not
  persistent `Mon`s with tracked HP/status -- there's nothing to switch
  *back to*. A trainer's party is always sent out in a fixed order, same as
  before; only the *active* mon's own turn (attack, or an item, see above)
  is AI-decided. Revive is a real item but isn't handled by the item-use AI
  either, for the same reason (it only matters exactly at the moment of a
  faint, a separate codepath this engine doesn't have)

### ❌ Not implemented yet

* Flash (the dark-cave maps render fully lit instead, so they're passable —
  easier than the original rather than blocked)
* Bike, day-night cycle, overworld weather, fishing, berry growing
* Multiple PC boxes (currently one unlimited list), item storage in the PC
* Breeding/eggs, contests, secret bases, Battle Frontier (Trading — the 4
  fixed NPC in-game trades — now works; there's no real link-cable trading
  since there's no second player)
* Door-open animations (purely cosmetic, no gameplay effect either way)
* A map editor / procedural map generator

### 🎯 Next-fix priority (walkthrough-ordered, per a Bulbapedia walkthrough audit)

Story content itself (dialogue, scripts, maps) is imported for the whole
game and translated through Rustboro Gym; these are the *systemic* gaps
found to actually block or break story progression, in the order they'd be
hit:

1. ~~Cut as a field move~~ — done: `checkpartymove`/`bufferpartymonnick`/
   `buffermovename`/`removeobject` implemented in `ScriptVM`, badge-gated
   cuttable trees now work exactly like pokeemerald (Rock Smash rides along
   for free, same shared opcodes/script shape). Surf/Strength/Waterfall/Fly/
   Dive still aren't implemented but aren't needed this early.
2. ~~Running Shoes' run mechanic~~ — done: gated on `FLAG_SYS_B_DASH`,
   holding Shift (the PC-appropriate stand-in for the GBA's B button) now
   moves at the real games' 2x walk speed.
3. ~~Trading~~ — done: all 4 fixed in-game trades (Rustboro's
   Seedot↔Ralts, Fortree's Plusle↔Volbeat, Pacifidlog's Horsea↔Bagon,
   Battle Frontier Lounge6's Meowth↔Skitty) work via a real party picker
   and species matching. No real link-cable trading (no second player) --
   trade evolutions (Kadabra, Machoke, Graveler, Haunter) still can't
   happen, same as the rest of Evolution being level-up only.
4. ~~`goto_if_ge/gt/lt/le`/`multichoice` audit~~ — done. Findings:
   - `goto_if_ge/gt/lt/le` **and** `call_if_ge/gt/lt/le` (33 files) — implemented,
     same pattern as `goto_if_eq`/`call_if_eq`. Mostly Battle Frontier/Contest
     Hall score thresholds, but also real story content.
   - `checkitem`/`removeitem` result vars (51/23 files) — implemented, wired
     to the existing bag (`GameState::item_count`/`take_item`). Verified on
     Devon Corp's fossil hand-off quest (Rustboro), which also needed
     `bufferitemname` (found along the way, same missing-opcode class) —
     now correctly shows "PLAYER handed the ROOT FOSSIL to the DEVON
     RESEARCHER." and removes it from the bag.
   - `multichoice` (115 files, ~90% Battle Frontier) — at the time of this
     audit, not implemented. Its option text isn't in any script file
     pokeemerald ships per-map (it's a fixed C array, `gMultichoiceLists`),
     so each non-Battle-Frontier use needs its own hand-transcribed option
     list, not a single generic fix. **Since implemented** — see below.
   - `addobject`/`hideobject`/`showobject` (41/0/0 files) — at the time of
     this audit, not implemented, and bigger than expected: this engine had
     no LOCALID→object registry at all (`ScriptVM::resolve()` only knew
     "the player" or "the NPC just talked to"), so scripts that spawn an
     NPC mid-scene by id -- e.g. Petalburg Gym's Wally tutorial battle
     cutscene (`addobject LOCALID_PETALBURG_GYM_WALLY`) -- did nothing when
     they tried. **Since implemented** — see below.
   - `opendoor`/`closedoor`/`waitdooranim` — confirmed purely cosmetic
     (metatile swap + SFX; door tiles are already walkable independent of
     the animation), left as no-ops.
5. ~~Playthrough-blocker audit~~ — done, prompted by "can we actually finish
   the game right now?". Two real blockers found and fixed:
   - **TM/HM teaching was completely broken for every TM and HM in the
     game**, Cut included — `Menu.cpp` assumed bag items were named like
     `ITEM_HM03`, but pokeemerald's real item constants are descriptive
     (`ITEM_HM_CUT`, `ITEM_TM_BULK_UP`, ...), so the derived move was
     always garbage and teaching always silently failed ("Es passiert
     nichts."). Fixed by reading the move straight off the item name
     (pokeemerald builds those constants from the move name itself, so no
     numeric code is even needed) — verified headlessly teaching both an
     HM and a TM.
   - **Surf didn't exist at all**, and worse, water wasn't even collision-
     blocked, so the "back half" of Hoenn (everything past Lilycove --
     Mossdeep, Sootopolis, and Ever Grande/the Pokémon League itself, all
     only reachable over open water) was already walkable on foot with no
     Surf check, animation, or encounters at all. Fixed: real surfable-
     water metatile classification (mirroring how tall grass is already
     classified), a gate that blocks entry without Surf and prompts for it
     with the real games' own question text, automatic dismount back on
     land, and a water wild-encounter table.
6. ~~Strength as a field move~~ — done. Activating it needed no new VM code
   at all (`EventScript_StrengthBoulder` is the same
   `checkpartymove`/Ja-Nein/`setflag` shape Cut and Surf already use), but
   pushing the boulder is a walk-into-it collision, not an interaction, so it
   needed a new native mechanic in `player_step()` mirroring pokeemerald's
   `TryPushBoulder`: walking into a `misc_pushable_boulder` NPC while
   `FLAG_SYS_USE_STRENGTH` is set moves it one tile further (bounds/
   collision/water-checked) instead of just blocking, and the player only
   advances into its old tile on the *next* step, exactly like the original.
   Along the way, found and fixed a much bigger, unrelated bug this surfaced:
   any msgbox referencing a text label defined in `data/event_scripts.s`
   (pokeemerald's single biggest shared-script file — HM/badge flavour text,
   the PokéRus explanation, the department store elevator's floor prompt,
   ...) was rendering the raw label name (e.g. literally "Text_CantStrength")
   instead of real text, because the importer's global text pool only
   scanned `data/text/*.inc` and `data/scripts/*.inc`, never that file. Fixed
   in `pe_import.py` and back-filled non-destructively into the 74 already-
   imported `.map` files it affected (Pokémon Centers, Battle Frontier
   rooms, several routes, the Trick House/boulder rooms, ...) — 486 lines
   resolved to their real text. One text (`gStringVar4`, a runtime-formatted
   Battle Frontier buffer, not a static string) is legitimately out of scope
   for a static text pool and left as-is. Also fixed a second, narrower bug
   found while re-deriving the Strength text: the importer's `{STR_VAR_n}`
   handling dropped the token entirely instead of keeping it as the bare
   `STR_VAR_n` placeholder `ScriptVM`'s msgbox substitution actually looks
   for, silently eating the Pokémon's name out of "X used STRENGTH!"-style
   lines.
7. ~~Waterfall as a field move~~ — done: its own MB_WATERFALL metatile
   classification (new `waterfall` map-file section, mirroring `water`),
   gated on moving north into one while already surfing (pokeemerald's
   `IsPlayerSurfingNorth` check) — any other direction is just ordinary open
   water. Reused Surf's Ja/Nein-prompt plumbing directly (`needs_waterfall`/
   `pending_waterfall` alongside the existing `needs_surf`/`pending_surf`).
   Backfilled non-destructively into the 7 maps that actually have a
   waterfall (Route 119, Meteor Falls, Route 114, Victory Road B2F, Ever
   Grande City, Battle Frontier Outside East, Safari Zone Southeast).
8. ~~Fly as a field move~~ — done, but the biggest of the four so far: real
   Fly needs a region-map UI this engine has no assets/infrastructure for at
   all, so it's simplified to a FLIEGEN entry in the start menu listing
   every visited town/city as plain text. "Visited" itself didn't exist as
   data anywhere -- pokeemerald sets each town's `FLAG_VISITED_*` from its
   `MAP_SCRIPT_ON_TRANSITION` (not the `MAP_SCRIPT_ON_FRAME_TABLE` this
   engine already imports), sometimes indirectly through an unconditionally-
   called helper script (Slateport) or a coord_event near the entrance (Ever
   Grande, already covered by the existing trigger system with zero new
   code). Rather than tracing each town's call graph, the importer derives
   the expected flag name from the map's own folder name and just confirms
   it's really set somewhere in that map's scripts.inc (`derive_visited_flag`)
   -- true for exactly the 16 canFly towns/cities, matching pokeemerald's own
   `region_map.c` list. New `visit` map-file section (backfilled into those
   16 maps) makes `load_session` set the flag unconditionally on every entry;
   arrival tiles reuse each town's own heal-location coordinates (same spot
   recovering from a whiteout would use). Gated on the team knowing FLY, not
   a badge, same simplification as Surf/Strength/Waterfall.
9. ~~A per-map LOCALID→object registry~~ — done. `NpcSpawn` now carries the
   `LOCALID_*` name porymap gave an object event, if any (most don't have
   one) -- read straight off `map.json`'s own `local_id` field, so no
   fragile index/position matching was needed. `Session::localid_map` (name
   → `Character*`) is built in `load_session` and handed to `ScriptVM`,
   whose `resolve()` now checks it before falling back to "the NPC just
   talked to". A LOCALID-tagged NPC's `Character` is created even while its
   own `FLAG_HIDE_*` is set (unlike a plain hidden NPC, which is skipped
   entirely) so `addobject` has something to spawn; it just isn't added to
   the live actor list until then. `addobject`/`hideobject`/`showobject`
   are new opcodes that add/remove a resolved object from that list, nothing
   more — real pokeemerald's `removeobject` has no flag side effect, unlike
   this engine's existing one for Cut/Rock Smash's own trees (which
   permanently marks `FLAG_HIDE_*`/`FLAG_TEMP_*` and the object
   uninteractable, since that call is *never* LOCALID-resolved in practice —
   only ever the tree just interacted with). Verified end to end on
   Petalburg Gym's Wally tutorial cutscene (`addobject` spawning him,
   `applymovement` walking him into place by LOCALID, no crash across the
   whole scripted sequence) with no regression on Cut (a tree still cut and
   stays gone).
10. ~~`multichoice` support~~ — done. `MultiChoicePrompt` is a real
    cursor-driven option list (same block-and-resume VM pattern as the party
    picker/Ja-Nein prompt — a new `WAIT_MULTICHOICE` state, `wants_/
    resolve_multichoice()`); the picked index lands in `VAR_RESULT`, same
    contract as pokeemerald's own opcode. `multichoicedefault`'s extra
    default-selected-index argument is honored too. The option text itself
    isn't in any imported script file (it's `sMultichoiceLists[]`, a fixed C
    array indexed by a `MULTI_*` id, in `src/data/script_menu.h`), so it's a
    55-entry static table hand-resolved from that file plus the two places
    its actual text lives (`src/strings.c`'s `gText_*` string literals,
    `data/text/trick_house_mechadolls.inc`'s `.string` blocks) — covering
    every `MULTI_*` id an already-imported non-Battle-Frontier map actually
    references (fishing quality, contest info, the Game Corner shop, the
    Trick House's 15-question Mechadoll trivia puzzle, the Devon Corp fossil
    choice, ...). Battle Frontier's own ~100 uses are its own facility
    flavour text, left unresolved (falls through as a no-op, same as before
    this opcode existed) rather than transcribed sight unseen.

## To-do checklist

Everything checked off below has been verified working (test or headless
screenshot), not just written and assumed correct.

**Foundations**
- [x] Collision (per-tile layer, checked on move)
- [x] Bounds checking (`Map::in_bounds` / `Map::passable`)
- [x] Asset importer fully data-driven (`tools/pe_import.py`), no
      compile-time hardcoding
- [x] NPC behaviour, map transitions, warp fades
- [x] Seamless map connections (edge-to-edge, not just door/warp tiles)
- [x] Smooth sliding movement + camera, steady per-frame input polling
- [x] Unit-testing harness wired into CTest

**Battle & progression**
- [x] Turn-based battle system (trainers + wild, capture)
- [x] RPG progression (EXP, levels, level-up evolution, level-up movesets)
- [x] TM/HM teaching gated by real learnsets
- [x] Status conditions (paralysis/burn/poison/toxic/sleep/freeze/confusion)
      + a real per-move accuracy roll
- [x] Critical hits (Gen-3 stage odds, Focus Energy, real ignore-stages rule)
- [x] Move priority (Quick Attack/Mach Punch/Extreme Speed, Vital Throw)
- [x] Stat-stage changes from status moves (-6..+6, all seven stats,
      compound effects like Bulk Up/Calm Mind/Haze included)
- [x] Weather (Rain/Sun damage swing, Sandstorm/Hail chip damage, 5-turn
      duration)
- [x] Abilities (real per-species data, ~12 implemented: Intimidate,
      Drizzle/Drought/Sand Stream, Levitate, Wonder Guard, the status
      immunities -- the other ~65 are recognized but inert)
- [x] IVs (0..31/stat) and natures (real Gen-3 stat formula), rolled once
      per individual and persisted with the mon
- [x] PP (real per-move values), decremented on use, forced Struggle at 0 PP
      (bypasses the type chart/STAB entirely, real engine special case),
      recoil (Take Down/Submission/Struggle deal 1/4 back to the attacker)
- [x] Held items (real per-species odds), a curated subset with real effects:
      status/pinch berries, Leftovers, type-boost items, Everstone
- [ ] EVs (no battling-based EV gain tracked)
- [x] Switching Pokémon mid-battle / surviving a faint with a healthy party
- [x] Whiteout on a lost battle (heal + warp to last heal location) instead
      of scripted trainer battles continuing their win dialogue on a loss
- [ ] Double battles, EXP Share
- [x] In-game trades (all 4 fixed NPC trades, real party picker)
- [ ] Stone/trade/friendship evolution triggers
- [x] Catch-rate-aware capture formula (real Gen-3 formula, each species'
      actual catch rate + HP + status bonus)
- [ ] Ball-type awareness (only `ITEM_POKE_BALL` functionally exists)

**Core loop**
- [x] Save/load system
- [x] Usable healing/revive/status-curing bag items outside TM/HM teaching
- [x] `specialvar` opcode — `GetBattleOutcome`/`PlayerHasBerries` real,
      the rest honestly default to 0/false (see above)
- [x] Scripted legendary/static encounters (`setwildbattle`/`dowildbattle`)
- [x] Gift/fossil Pokémon (`givemon`/`giveegg`: Johto starters, Beldum,
      Castform, the revived fossils; overflows to the PC when the party is full)
- [x] Mossdeep Gym rotating-tile puzzle (also covers Trick House Puzzle #7)
- [x] Comparison `goto_if_ge/gt/lt/le`/`call_if_ge/gt/lt/le`,
      `checkitem`/`removeitem`/`bufferitemname`
- [x] LOCALID→object registry + `addobject`/`hideobject`/`showobject`
- [x] `multichoice`/`multichoicedefault` (real choice-menu UI, 55 resolved
      option lists)
- [x] `bufferstring`/`buffernumberstring`/`bufferleadmonspeciesname`,
      `getplayerxy`, `hideplayer`/`showplayer`, `setobjectmovementtype`,
      `setdynamicwarp` (fixes the intro moving-truck exit, a real dead end)
- [x] Importer resolves `#ifdef UBFIX`/`#ifdef BUGFIX`/`#ifndef BUGFIX`
      conditionals in source scripts instead of exporting both branches
- [x] Trainer AI uses its own real item pool (Full Restore/Hyper Potion/...,
      ~141 trainers) and weighs accuracy, not just raw damage, picking a move
- [ ] Trainer AI switching mid-battle (see scope note above -- bench mons
      have no persistent state to switch back to)
- [ ] Door animations (cosmetic only, see audit above)

**Audio**
- [x] Step/bump/select SFX
- [x] Per-map background music (58 tracks, plays on load/warp/fly/whiteout)
- [x] Pokémon cries (wild encounters, trainer send-outs, mid-battle switches)
- [x] Battle/victory themes (wild, trainer, gym leader/champion/rival/Elite
      Four, wild/trainer victory jingles)
- [x] `ScriptVM`'s `playbgm`/`playmoncry` opcodes (field-script music/cry cues)
- [ ] `playse` (one-off SE_* sound effects beyond step/bump/select)

**Overworld features**
- [x] Overworld minigames (slots/roulette/blender/jump) with coin currency
- [x] Cut/Rock Smash field moves (badge-gated, real removeobject/text)
- [x] Surf (real water-tile gate, Ja/Nein prompt, dismount, water encounters)
- [x] Strength (native boulder-push collision mechanic, real activation text)
- [x] Waterfall (climbing gated on surfing north into it, real activation text)
- [x] Fly (FLIEGEN menu entry, visited-town tracking, real arrival tiles)
- [x] Dive (deep-water metatiles, dive/emerge connections and `setdivewarp`)
      — unblocks Sootopolis City, which had no other way in
- [x] Trainers challenge the player on sight (real per-trainer sight ranges)
- [ ] Flash (dark caves render lit, so they're passable anyway)
- [x] Running Shoes (hold Shift, gated on FLAG_SYS_B_DASH, real 2x speed)
- [ ] Bike
- [ ] Day-night cycle, overworld weather
- [ ] Fishing, berry growing

**UI**
- [x] Start menu: Pokédex, Bag, Party, PC Box, PokéNav
- [x] PokéNav region map (real Hoenn town map image + gendered player
      marker + a real moving cursor that names any section it's over,
      positioned from imported `region_map_sections.json` data)
- [x] Pokémon Center full-team heal + animation
- [x] Pokédex screen (real seen/caught tracking, persisted)
- [x] Party member summary screen (stats, nature/ability, moves)
- [x] Player naming / gender selection (GenderSelect + grid NameEntry, once
      at the start of a new game); fixes dialogue's "PLAYER"/"RIVAL" tokens
      never being substituted
- [x] Options/settings screen (Ton/Kampfszene/Rahmenart -- Text Speed and
      Battle Style aren't implemented, no typewriter-text or switch-prompt
      mechanic to hook them into)
- [ ] Multiple PC boxes, item storage in the PC

**Contests & Battle Frontier** (brought into scope; previously deferred)
- [ ] Contests (Cool/Beauty/Cute/Smart/Tough, judging, Contest Hall NPCs)
- [ ] Secret bases
- [ ] Battle Frontier (Tower/Dome/Palace/Arena/Factory/Pike/Pyramid)

**Out of scope for now (not silently faked, just not attempted)**
- [ ] Breeding/eggs (link-cable trading N/A -- no second player)
- [x] Map editor (`codemon_editor`, see `codemon/editor/README.md`) —
      paints the hand-authored `codemon/region/` map set, not the imported
      pokeemerald maps
- [ ] Procedural map generator
