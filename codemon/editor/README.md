# codemon_editor — external map editor

A standalone tool for painting the region's `.map` files. It is **external to
the game**: it shares the terrain sheet (`region/region_tiles.png`) and the
`.map` file format, but runs as its own program and does not depend on the game
loop.

## Build & run

Built by the top-level CMake alongside the game:

```sh
cmake -S . -B build
cmake --build build
cd build            # so region/ (staged next to the binary) is on the path
./codemon_editor                         # opens region/maps/pallet_town.map
./codemon_editor region/maps/viridian_city.map
./codemon_editor region/maps/new_area.map 20 16   # create a 20x16 map if absent
```

## Controls

| Input | Action |
|-------|--------|
| Left-click a palette tile | select it as the paint tile |
| Left-click / drag on the map | paint the selected tile |
| Right-click / drag on the map | erase to grass (tile 0) |
| `[` / `]` | cycle the selected tile |
| `G` | toggle the grid |
| `S` | save to the open `.map` file |

The window title is the live status line: open file, selected tile id, hovered
cell, and a `*saved*` flash after `S`. Palette tiles are indexed by
`Tile::tile` id (see `../Tile.h`), so what you paint is exactly what the game
renders.

## Notes

- New maps are created filled with grass; edit and press `S` to write them.
- Warps between areas live in the region manifest (`region/kanto.region`), not
  in the tile grid — add a `WARP|` line there for a new area (and place a door
  tile, id 16, where the transition should be).
- `codemon_editor --selftest` runs a headless load/save round-trip (wired into
  CTest); `codemon_editor --shot out.png [map] [sheet]` renders one frame to a
  PNG.
