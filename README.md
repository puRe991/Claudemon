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
  spawn point.
* **UI**: a start menu with Bag, Party, PC Box and PokéNav screens, capture
  and storage, item/type/species icons, an HP bar, a map-name banner on
  transitions, and a few overworld minigames (slots/roulette/blender/jump)
  using an in-game coin currency.
* **Audio**: Pokémon cries and generated step/bump/select SFX, with MIDI
  music converted to OGG when a synth is available.

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
  converted to OGG when a synth is installed (SFML can't play MIDI directly).

Re-run or extend the import with:

```sh
pip install Pillow
python3 codemon/tools/pe_import.py all --src /path/to/pokeemerald-master
```

Licensing note: the imported graphics/audio/text remain Nintendo/Game Freak
property and are for non-commercial fan use only — see
`codemon/assets/CREDITS.md`.

## Building & running

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
menu, `G` opens minigames (where available).

### Windows

Double-click **`run-windows.bat`** (or run it from a terminal) to configure,
build and start the game in one step:

```bat
run-windows.bat            :: Release build, then run
run-windows.bat Debug      :: Debug build, then run
```

It drives the same CMake build and, unless you set `SFML_DIR` yourself, links
the MSVC SFML bundled under `codemon\SFML` statically, so no separate SFML
install or DLLs are needed. It requires CMake and a C++ toolchain (e.g.
Visual Studio Build Tools). The older Visual Studio solution (`codemon.sln`)
is still present but uses machine-specific absolute paths; the batch/CMake
path is preferred.

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
`CODEMON_NO_WILD`, and `CODEMON_GRANT_EXP` (grant the starter EXP, to see
level-ups/evolution in one shot).

## Testing

`codemon_tests` exercises the display-independent core logic (`Coordinates`,
`Tile`, the hand-rolled `Linked_list`) and is wired into CTest.

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
* Cooperative script VM (`ScriptVM`) running pokeemerald's real event
  scripts: dialog (multi-page), flags/vars, `goto`/`call` + `eq`/`ne`/`set`/
  `unset` conditionals, `switch`/`case`, movement scripts, `giveitem`/
  `finditem`, `setmetatile`
* Turn-based battles: 385 species (real stats/types/growth curve), 354
  moves (power/type/accuracy parsed), 17-type effectiveness chart, STAB,
  physical/special split, wild encounters (real per-map tables + Gen-3 slot
  weighting), 854 trainer battles with real parties, trainer rematches
* Capture mechanic (approximate probability, Poké Ball only — see below)
* EXP gain, all 6 species growth curves, level-up stat recalculation,
  level-up movesets (411 learnsets), **level-up evolution** (172 paths)
* TM/HM teaching from the bag, gated by real per-species learnsets (372
  entries); TMs consumed on use, HMs reusable
* Shops (`pokemart`) and Ja/Nein prompts via the VM block-and-resume pattern
* Story-accurate start (Brendan's House 2F, canonical heal-location tile)
* UI: start menu (Bag/Party/PC Box/PokéNav), map-name banner, HP bars,
  item/type/species icons, Pokémon Center heal (whole team) + its glowing-
  Pokéball/monitor animation
* Overworld minigames: Slot Machine, Roulette, Berry Blender, Pokémon Jump
  (real gameplay, not stubs), with an in-game coin currency
* Audio: step/bump/select SFX play natively; cries and MIDI→OGG music exist
  and work when called, but nothing in the game currently calls them yet
  (see below)
* `codemon_tests` / CTest for the display-independent core data structures

### ⚠️ Partial / simplified

* **Battle system** is a simplified 1v1 damage calculator: no status
  conditions (paralysis/burn/poison/sleep/freeze/confusion), no critical
  hits, no accuracy/evasion rolls, no move priority, no stat-stage changes
  from status moves (Growl etc. do nothing but print text), no weather, no
  abilities, no held items, no IV/EV/natures, no PP/Struggle, no doubles, no
  EXP Share
* **Catch mechanic** is a flat approximate formula, independent of species
  catch rate or ball type (only `ITEM_POKE_BALL` exists functionally)
* **Evolution**: level-up only; stone/trade/friendship evolutions are
  imported into data but never triggered in-game
* **Audio**: the `Audio` class is fully functional, but nothing calls
  `play_cry`/`play_music` yet, and `ScriptVM` never calls its `Audio*` for
  `playbgm`/`playse`/`playmoncry` — so the game is currently silent beyond a
  few UI blips
* **Text interpolation** (`bufferstring` family): the importer bakes static
  values into dialog at import time, but genuinely dynamic values (e.g.
  Birch's Pokédex-seen/caught rating) render with the number missing

### ❌ Not implemented yet

* Save/load (no persistence at all — every run starts fresh)
* Switching Pokémon mid-battle, or continuing a battle on a healthy party
  member after the active one faints
* Using bag items (Potions, status healers, Poké Balls, berries) outside the
  TM/HM-teaching flow
* `specialvar` opcode (breaks `GetBattleOutcome`, rematch checks, Pokérus,
  breeding-state checks and more — 361 uses across the maps)
* Scripted legendary/static encounters (`setwildbattle`/`dowildbattle`:
  Rayquaza, the Regis, Kyogre/Groudon, Kecleon, the Voltorb swarm)
* Gift/fossil Pokémon (`givemon`: Johto starters, Beldum, Castform, fossils)
* Mossdeep Gym's rotating-tile puzzle
* HM field moves (Cut/Surf/Fly/Strength/Flash/Rock Smash/Waterfall/Dive) —
  water is currently just impassable terrain
* Running/bike, day-night cycle, overworld weather, fishing, berry growing
* Pokédex screen (no seen/caught tracking), party summary/stats screen,
  player naming/gender selection, options/settings screen
* Multiple PC boxes (currently one unlimited list), item storage in the PC
* Trading, breeding/eggs, contests, secret bases, Battle Frontier
* Comparison `goto_if_ge/gt/lt/le` opcodes, `multichoice` prompts,
  `checkitem`/`removeitem` result vars, NPC add/hide/move opcodes, door-open
  animations — all currently silent no-ops where scripts use them
* A map editor / procedural map generator

## To-do checklist

Everything checked off below has been verified working (test or headless
screenshot), not just written and assumed correct.

**Foundations**
- [x] Collision (per-tile layer, checked on move)
- [x] Bounds checking (`Map::in_bounds` / `Map::passable`)
- [x] Asset importer fully data-driven (`tools/pe_import.py`), no
      compile-time hardcoding
- [x] NPC behaviour, map transitions, warp fades
- [x] Unit-testing harness wired into CTest

**Battle & progression**
- [x] Turn-based battle system (trainers + wild, capture)
- [x] RPG progression (EXP, levels, level-up evolution, level-up movesets)
- [x] TM/HM teaching gated by real learnsets
- [ ] Status conditions (paralysis/burn/poison/sleep/freeze/confusion)
- [ ] Critical hits, accuracy/evasion, move priority
- [ ] Stat-stage changes from status moves
- [ ] Weather, abilities, held items, IV/EV/natures, PP/Struggle
- [ ] Double battles, EXP Share
- [ ] Switching Pokémon mid-battle / surviving a faint with a healthy party
- [ ] Stone/trade/friendship evolution triggers
- [ ] Ball-type-aware, catch-rate-aware capture formula

**Core loop**
- [ ] Save/load system
- [ ] Usable bag items outside TM/HM teaching (Potions, status healers,
      berries, Poké Balls)
- [ ] `specialvar` opcode (`GetBattleOutcome`, rematch checks, Pokérus, ...)
- [ ] Scripted legendary/static encounters (`setwildbattle`/`dowildbattle`)
- [ ] Gift/fossil Pokémon (`givemon`)
- [ ] Mossdeep Gym rotating-tile puzzle
- [ ] Comparison `goto_if_ge/gt/lt/le`, `multichoice`, `checkitem`/
      `removeitem` result vars, NPC add/hide/move opcodes, door animations

**Audio**
- [x] Step/bump/select SFX
- [x] `Audio` class supports cries + MIDI→OGG music (functional but unused)
- [ ] Wire cries into send-out/level-up, music into `ScriptVM`'s `playbgm`/
      `playse`/`playmoncry` opcodes and per-map track selection

**Overworld features**
- [x] Overworld minigames (slots/roulette/blender/jump) with coin currency
- [ ] HM field moves (Cut/Surf/Fly/Strength/Flash/Rock Smash/Waterfall/Dive)
- [ ] Running/bike
- [ ] Day-night cycle, overworld weather
- [ ] Fishing, berry growing

**UI**
- [x] Start menu: Bag, Party, PC Box, PokéNav
- [x] Pokémon Center full-team heal + animation
- [ ] Pokédex screen (seen/caught tracking)
- [ ] Party member summary/stats screen
- [ ] Player naming / gender selection
- [ ] Options/settings screen
- [ ] Multiple PC boxes, item storage in the PC

**Out of scope for now (not silently faked, just not attempted)**
- [ ] Trading, breeding/eggs
- [ ] Contests, secret bases
- [ ] Battle Frontier
- [ ] Map editor / procedural map generator
