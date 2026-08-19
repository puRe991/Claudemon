#pragma once
#include <string>
#include <vector>

/******************************************************************************
TileMap - headless, SFML-free tile map data model.

This is the shared "document" that both the game and the map editor operate on.
It deliberately has no dependency on SFML or any graphics context, so it can be
constructed, edited, saved and unit-tested without a display.

On-disk format (CSV text), backwards compatible with the original maps:

    line 1 : width,height[,startX,startY]
    next H : `height` rows, each with `width` comma-separated integer tile ids

The optional startX,startY on the header line record the player spawn tile; if
absent they default to 0,0.
******************************************************************************/
class TileMap
{
public:
	TileMap();
	TileMap(unsigned int width, unsigned int height, int fill = 0);

	/* Persistence */
	bool load_from_file(const std::string& path);
	bool save_to_file(const std::string& path) const;

	/* Dimensions */
	unsigned int get_width() const;
	unsigned int get_height() const;
	void resize(unsigned int width, unsigned int height, int fill = 0);

	/* Tile access. Out-of-range reads return -1; out-of-range writes are ignored. */
	int get_tile(unsigned int x, unsigned int y) const;
	void set_tile(unsigned int x, unsigned int y, int tile_id);
	bool in_bounds(unsigned int x, unsigned int y) const;

	/* Player spawn */
	unsigned int get_start_x() const;
	unsigned int get_start_y() const;
	void set_start(unsigned int x, unsigned int y);

	/* Raw row-major tile buffer (size == width * height) */
	const std::vector<int>& raw() const { return tiles; }

private:
	unsigned int width;
	unsigned int height;
	unsigned int start_x;
	unsigned int start_y;
	std::vector<int> tiles; // row-major: index = y * width + x
};
