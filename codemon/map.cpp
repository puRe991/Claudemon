#include "map.h"
#include "Window.h"

//Create a map object from paths to a map file and a tilesheet for said map.
Map::Map(std::string map_path, std::string sheet_path)
	: tile_w(32), tile_h(32)
{
	this->map_path = map_path;
	this->sheet_path = sheet_path;

	//Load the tilesheet for this map.
	if (!this->map_sheet.loadFromFile(this->sheet_path)) {
		//Missing sheet: tiles will render as a blank/!default texture.
	}

	//Load the tile grid (robust CSV loader, tolerant of bad/short files).
	this->m_data.load_from_file(this->map_path);
}

//Render each tile of the map into the active window's buffer.
void Map::render_map(Window* active_window)
{
	sf::Sprite to_draw(this->map_sheet);

	for (unsigned int y = 0; y < this->m_data.get_height(); ++y) {           //each row
		for (unsigned int x = 0; x < this->m_data.get_width(); ++x) {        //each column
			int id = this->m_data.get_tile(x, y);
			if (id < 0) {
				id = 0;
			}

			//A tile's id is its column in the horizontally-laid-out tilesheet.
			const int x_sheet_off = id * static_cast<int>(this->tile_w);
			const int y_sheet_off = 0;

			to_draw.setTextureRect(sf::IntRect(
				x_sheet_off, y_sheet_off,
				static_cast<int>(this->tile_w), static_cast<int>(this->tile_h)));

			to_draw.setPosition(
				static_cast<float>(x * this->tile_w),
				static_cast<float>(y * this->tile_h));

			active_window->draw(&to_draw);
		}
	}
}

bool Map::in_bounds(Coordinates proposed_coord)
{
	return this->m_data.in_bounds(proposed_coord.get_x(), proposed_coord.get_y());
}

unsigned int Map::get_width()  { return this->m_data.get_width(); }
unsigned int Map::get_height() { return this->m_data.get_height(); }
unsigned int Map::get_tile_width()  { return this->tile_w; }
unsigned int Map::get_tile_height() { return this->tile_h; }
unsigned int Map::get_pixel_width()  { return this->m_data.get_width()  * this->tile_w; }
unsigned int Map::get_pixel_height() { return this->m_data.get_height() * this->tile_h; }

Coordinates Map::get_start_pos()
{
	return Coordinates(this->m_data.get_start_x(), this->m_data.get_start_y());
}
