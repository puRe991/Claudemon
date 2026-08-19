# The Region — regional landscape structure

This directory is the **region**: the connected world the game is set in. It is
pure data — a manifest plus one map file per area — loaded by the `Region`
class (`codemon/Region.h` / `Region.cpp`) and traversed in `main.cpp`. No code
change is needed to reshape the world; edit the data (or the generator) and
rebuild.

The region is a compact but varied landscape with a clear geographic arc: it
starts quiet and rural on the southern coast and grows — step by step — into
dense cities, large nature reserves, industry, mountains and sprawling coasts,
finishing at a monumental complex high in the western mountains.

```
                       Victory Road ── Indigo Plateau      (west: the final barrier)
                            │
   Viridian Forest ── Viridian City ── Route 1 ── Pallet Town  (south: rural start, coast)
        │                                              │
   Pewter City ── Route 3 ── Mt. Moon ── Cerulean City │
                                            │   │   │   │
                                   Nugget Bridge │  Rock Tunnel ── Lavender Town ── Pokemon Tower
                                                 │      │
                                                 │   Power Plant
                                                 │
                                          Saffron City ── Silph Co.        (centre: the metropolis)
                                          │    │    │
                          Celadon City ───┘    │    └─── Lavender Town
                          │       │            │
                 Rocket Hideout   │       Vermilion City ── S.S. Anne
                                  │            │
                          Cycling Road         └── Route 11 ── Diglett's Cave ── Route 1
                                  │
                          Fuchsia City ── Safari Zone
                                  │
                              Sea Route ──────────────── Pallet Town   (loop closes by sea)
                              │      │
                 Seafoam Islands ── Cinnabar Island ── Pokemon Mansion  (south: islands, volcano)
```

## Geographic arc

`Pallet Town → open country → forest → mountain town → caves → water city →
port → metropolis → industry → nature reserves → islands → volcano island →
mountain road → monumental end complex`, forming one closed loop.

- **South:** a small, quiet coastal settlement (Pallet Town), open meadows,
  farmland and calm residential country.
- **Centre:** the larger cities — shopping, industry, research, transport hubs
  and the region's biggest metropolis.
- **North & west:** forests, mountains, cave systems, remote paths and the
  region's final target complex.
- **South & south-west:** islands, sea routes, harbours, coasts and volcanic
  landscape.

## Areas (30)

| id | name | character |
|----|------|-----------|
| `pallet_town` | Pallet Town | southern coastal village, research lab, sea to the south |
| `route_01` | Route 1 | open grass road north |
| `viridian_city` | Viridian City | first big town / crossroads |
| `viridian_forest` | Viridian Forest | dense, enclosed woodland |
| `pewter_city` | Pewter City | stony town, museum, mountains |
| `route_03` | Route 3 | grass → hills → rock, up to the cave |
| `mt_moon` | Mt. Moon | multi-level cave system |
| `cerulean_city` | Cerulean City | water city, canals, bridges |
| `nugget_bridge` | Nugget Bridge | long bridge into the remote north |
| `vermilion_city` | Vermilion City | harbour city, maritime |
| `ss_anne` | S.S. Anne | large passenger ship interior |
| `diglett_cave` | Diglett's Cave | narrow tunnel shortcut |
| `route_11` | Route 11 | open eastern route |
| `rock_tunnel` | Rock Tunnel | large dark cave, natural barrier |
| `lavender_town` | Lavender Town | small, unusually still town |
| `pokemon_tower` | Pokemon Tower | tall memorial tower |
| `celadon_city` | Celadon City | commercial centre, department store |
| `rocket_hideout` | Rocket Hideout | hidden underground complex |
| `saffron_city` | Saffron City | central metropolis, many main roads |
| `silph_co` | Silph Co. | corporate skyscraper |
| `fuchsia_city` | Fuchsia City | town on the edge of nature |
| `safari_zone` | Safari Zone | large multi-zone nature reserve |
| `cycling_road` | Cycling Road | long downhill cycle route |
| `seafoam_islands` | Seafoam Islands | flooded sea caves, ice |
| `cinnabar_island` | Cinnabar Island | volcanic island settlement |
| `pokemon_mansion` | Pokemon Mansion | ruined, abandoned estate |
| `power_plant` | Power Plant | remote industrial plant on the water |
| `sea_route` | Sea Route | open ocean linking the islands |
| `victory_road` | Victory Road | sprawling mountain cave, last barrier |
| `indigo_plateau` | Indigo Plateau | monumental end complex on a high plateau |

## Connections (33, all two-way)

```
pallet_town   <-> route_01 <-> viridian_city <-> viridian_forest <-> pewter_city
pewter_city   <-> route_03 <-> mt_moon <-> cerulean_city
cerulean_city <-> nugget_bridge
cerulean_city <-> rock_tunnel <-> lavender_town <-> pokemon_tower
rock_tunnel   <-> power_plant
cerulean_city <-> saffron_city <-> vermilion_city
saffron_city  <-> celadon_city <-> rocket_hideout
saffron_city  <-> lavender_town
saffron_city  <-> silph_co
celadon_city  <-> cycling_road <-> fuchsia_city <-> safari_zone
vermilion_city<-> ss_anne
vermilion_city<-> route_11 <-> diglett_cave <-> route_01
fuchsia_city  <-> sea_route <-> seafoam_islands <-> cinnabar_island
sea_route     <-> cinnabar_island <-> pokemon_mansion
sea_route     <-> pallet_town                 (the loop closes by sea)
viridian_city <-> victory_road <-> indigo_plateau
```

Every area is reachable from `pallet_town`, and every warp has a reciprocal
return warp (both invariants are checked by `codemon_tests`).

## Manifest format (`kanto.region`)

Pipe-delimited, one record per line; `#` starts a comment:

```
START|<area id>                                     # where the player spawns
AREA|<id>|<Display Name>|<map path>|<start_x>|<start_y>
WARP|<from id>|<x>|<y>|<to id>|<dest_x>|<dest_y>     # step onto (x,y) -> (dest_x,dest_y)
```

## Map file format (`maps/*.map`)

The engine's tile format: a `width,height` header followed by `height` rows of
comma-separated tile ids. Tile ids are the `Tile::tile` enum in
`codemon/Tile.h` (0 = short grass, 1 = tall grass, 2 = path, 3 = water, … 16 =
warp, …). A warp tile (16) marks a transition; where it leads is defined by the
matching `WARP` line in the manifest.

## Regenerating

The maps and manifest are generated deterministically so the whole structure
stays consistent. To reshape the region, edit the tables in the generator and
re-run it:

```sh
python3 codemon/tools/gen_region.py
```

This rewrites `kanto.region` and every file under `maps/`.
