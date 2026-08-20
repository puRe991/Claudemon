# Cod-é-mon - Pokemon clone 

#### Cliff's notes - Abstract :

Two Dimensional (2D) tile based game engine. Written, portably, in C++, _Cod-e-mon_ and the associated _Monsta_ game-engine can be cross-compiled, to unix based OS's (MacOs and Debian based Linux distros) and Windows 8+. 
The Monsta Engine and which _Cod-e-mon_ game built on it, leverages CMake for its build system. 
The SFML (Super Fast Multi-Media) is used as the graphical interface library. 

Note: Don't sue me nintendo buissness daddy.

## Backstory 

#### This part is 95% ok to skip, its mostly my rambling and what I read to remember why I started this

In 2010 when I first began to learn to code in a systematic way, I had starry eyed dreams of becoming a video games programmer. <br>
I think this is likely a natural pipeline into computer science for many. Game Dev seems like a lot of crunch for rarely enough pay to me now. <br> <br>
Any-hoozles. <br> <br>
As I had at that time already learned Basic, and was learning C in my highscool programming class I decided to try my hand at a pure C Pokemon clone. I submitted it as a summative project. It never ran to my satisfaction, and apparently it never ran at all on Mr. Krealman's PC. I got a good grade anyways somehow. (Thanks Mr.K) <br>
It was a good piece of code for a highschooler, it had: <br>
transitions, double buffering, a working (much to my astonishment) camera system, battles, et cetera. <br> <br>
My issues was that it was never portable or reliable. Simply put t'was buggy as-all-get-out and the ammount of *ahem* assistance I recieved on the project made me feel like it was never truly my work. (Thanks Geoffrey S.)<br>
Now as I finally finish my formal education and have amassed a decade or so of practical experience, let try our hand at reimplementing this solution. <br>

#### TLDR: This is an exercise in catharsis aswell as a way for me to gauge my growth as a dev

## Exec. Summary: <br>
### Attempt to reimplement pokemon in C++ as an exercise

## Project Goals
#### In no particular order and subject to change at anytime as the mood strikes me

> Multiple Maps
>
> > * Easy transitions between maps. Easy in this context means no noticable loading times.
> > * Transition animations, i.e. wipes, fades. <br> If you're thinking of early 2000s video editing software style transitions, you are in the right ball park.
>
> Wild Codemon areas 
> > * A battle system   
> > * RPG progression system inspried, but hopefully not hopelessly derivative, of Pokemon.
>
> A Map Editor would be nice
> > * Procedural map generator. Stoichastic - probably random noise based
>
>  Music
>
> > * Jazz maybe?
>
> General Goals
> > * No algorithms with runtimes beyond n Log(n)
> > * Unit-testing harness suite
> > * Would be nice if the harness suite also timed each major alorigthm automagically.


## Sprites

Pokémon sprite artwork is pulled in from [PokeAPI/sprites](https://github.com/PokeAPI/sprites)
as a git submodule under `sprites/`. Clone the project with submodules to get it:

```sh
git clone --recurse-submodules <repo-url>
# or, in an existing checkout:
git submodule update --init --depth 1 sprites
```

## World assets from pokeemerald

The overworld is built from **pokeemerald** assets, imported into engine-ready
form by `codemon/tools/pe_import.py` (see `codemon/tools/README.md`). That
script is the SFML-friendly replacement for the pokeemerald GBA build tools:

* **Tilesets** → fully coloured 16×16 metatile sheets in
  `codemon/assets/tilesets/` (palettes + metatile layers are resolved at import
  time, so the engine just samples the sheet — no runtime palette work). Loaded
  by the new `Tileset` class and rendered by `Map`.
* **Characters / NPCs** → every overworld walking sheet in
  `codemon/assets/overworld/` (16×32, 9-frame layout). The player and NPCs use
  the same `Character` class; east reuses the west frames mirrored.
* **Audio** → Pokémon cries plus generated step/bump/select blips in
  `codemon/assets/sfx/`, played through the new `Audio` class. MIDI music is
  converted to OGG when a synth is installed (SFML can't play MIDI directly).

Re-run or extend the import with:

```sh
pip install Pillow
python3 codemon/tools/pe_import.py all --src /path/to/pokeemerald-master
```

Licensing note: the imported graphics/audio remain Nintendo/Game Freak property
and are for non-commercial fan use only — see `codemon/assets/CREDITS.md`.

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

### Windows

Double-click **`run-windows.bat`** (or run it from a terminal) to configure,
build and start the game in one step:

```bat
run-windows.bat            :: Release build, then run
run-windows.bat Debug      :: Debug build, then run
```

It drives the same CMake build and, unless you set `SFML_DIR` yourself, links
the MSVC SFML bundled under `codemon\SFML` statically, so no separate SFML
install or DLLs are needed. It requires CMake and a C++ toolchain (e.g. Visual
Studio Build Tools). The older Visual Studio solution (`codemon.sln`) is still
present but uses machine-specific absolute paths; the batch/CMake path is
preferred.

The `codemon_tests` target exercises the display-independent core logic
(`Coordinates`, `Tile`, the hand-rolled `Linked_list`) and is wired into CTest.

### To-do bucket:<br>
- ~~add collision~~ — done: per-tile collision layer in the map format, checked on move
- ~~add bounds checking~~ — done: `Map::in_bounds` / `Map::passable`
- ~~add music~~ — audio system in place (SFX + cries); MIDI→OGG music is a synth away
- Redo asset importation: now handled by `tools/pe_import.py` (data-driven, no
  compile-time hardcoding). Next: render secondary tilesets against their primary.
- Add a testing suite for the low level data structures. I regret rolling my own but at this point adding the testing suite is the only way to nail everything down.
- NPC behaviour (movement/interaction), map-to-map transitions, a battle system.
