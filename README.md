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
- ~~add a camera / scrolling~~ (done: the view follows the player and clamps to map edges)
- per-area tile sheets (all areas currently share the one `region/region_tiles.png`)
- background music, and richer sound effects (an `Audio` system + placeholder SFX exist)
- ~~real tile / character art~~ (done: CC-BY pixel art, see `assets/CREDITS.md`)
- ~~Multiple maps + easy transitions between them~~ (done: see `codemon/region/`)
- ~~add collision~~ (done: blocking terrain is not walkable)
- ~~add bounds checking~~ (done: moves are bounds-checked against the active map)
- add music
- Redo assest importation: Make it a little bit more flexible. Had to use some hardcoding that I am unsatisfied with. Should ideally use macros to leverage preprocessor. This would ideally allow us to navigate to the target assets at compile time.
- Add a testing suite for the low level data structures. I regret rolling my own but at this point adding the testing suite is the only way to nail everything down.
