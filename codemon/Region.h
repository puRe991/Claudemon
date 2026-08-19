#pragma once
#include <string>
#include <vector>

#include "Coordinates.h"

/******************************************************************************
Region - the regional landscape structure.

A Region is the data model that ties the individual area maps together into one
connected world. It is loaded from a pipe-delimited manifest (see
region/kanto.region):

    START|<area id>
    AREA|<id>|<Display Name>|<map path>|<start_x>|<start_y>
    WARP|<from id>|<x>|<y>|<to id>|<dest_x>|<dest_y>

Every AREA names a map file and the tile the player spawns on. Every WARP says
"stepping onto tile (x,y) of <from> drops you at (dest_x,dest_y) of <to>".
Warps are authored as reciprocal pairs, so the region forms one traversable
graph from the southern coastal village all the way to the final mountain
complex.

Region deliberately does no rendering and pulls in no SFML, so the whole
world-structure - areas, connections and warp resolution - can be unit tested
headlessly.
******************************************************************************/

// One explorable area: a named map plus where the player appears in it.
struct RegionArea {
    std::string id;
    std::string name;
    std::string map_path;
    Coordinates start;
};

// One directed transition: onto `from_pos` in `from_id` -> `to_pos` in `to_id`.
struct RegionWarp {
    std::string from_id;
    Coordinates from_pos;
    std::string to_id;
    Coordinates to_pos;
};

class Region
{
public:
    // Load the region from a manifest file. Check loaded() for success.
    explicit Region(const std::string& manifest_path);

    bool loaded() const;

    // The area the player starts the game in (empty id if unset/unloaded).
    const RegionArea* start_area() const;

    // Look an area up by id, or nullptr if there is no such area.
    const RegionArea* get_area(const std::string& id) const;

    // If stepping onto `pos` in area `area_id` triggers a warp, return it;
    // otherwise nullptr. This is what the game calls after each move.
    const RegionWarp* warp_at(const std::string& area_id, Coordinates pos) const;

    // Ids of every area directly reachable from `area_id` by one warp.
    std::vector<std::string> neighbors(const std::string& area_id) const;

    std::size_t area_count() const;
    std::size_t warp_count() const;

    const std::vector<RegionArea>& areas() const;
    const std::vector<RegionWarp>& warps() const;

private:
    bool is_loaded;
    std::string start_id;
    std::vector<RegionArea> area_list;
    std::vector<RegionWarp> warp_list;

    void parse_line(const std::string& line);
};
