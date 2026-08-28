#pragma once
#include <string>
#include "SFML/Graphics.hpp"

/******************************************************************************
Tileset - a sheet of fixed-size metatiles imported from pokeemerald.

The importer (tools/pe_import.py) renders each pokeemerald metatile into a
fully coloured 16x16 image and packs them left-to-right, top-to-bottom into a
single sheet (assets/tilesets/<name>.png), `cols` metatiles per row. A map
stores metatile ids; this class turns an id into the source rectangle to sample
from the sheet, so no per-map palette handling is needed at runtime.
*****************************************************************************/
class Tileset
{
private:
	sf::Texture sheet;
	int metatile_px;   // edge length of one metatile in pixels (16)
	int cols;          // metatiles per row in the sheet
	int count;         // total metatiles available
	bool loaded;

public:
	Tileset();

	// Load a coloured metatile sheet. `metatile_size` is the edge length in
	// pixels; `cols`/`count` are derived from the texture dimensions.
	bool load(const std::string& sheet_path, int metatile_size = 16);

	bool is_loaded() const;
	int size() const;                 // metatile edge length in px
	int metatile_count() const;
	const sf::Texture& texture() const;

	// Source rectangle inside the sheet for a given metatile id.
	sf::IntRect source_rect(int id) const;
};
