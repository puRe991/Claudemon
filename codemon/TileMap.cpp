#include "TileMap.h"

#include <fstream>
#include <sstream>

TileMap::TileMap()
	: width(0), height(0), start_x(0), start_y(0)
{
}

TileMap::TileMap(unsigned int width, unsigned int height, int fill)
	: width(0), height(0), start_x(0), start_y(0)
{
	this->resize(width, height, fill);
}

unsigned int TileMap::get_width() const { return this->width; }
unsigned int TileMap::get_height() const { return this->height; }

void TileMap::resize(unsigned int width, unsigned int height, int fill)
{
	std::vector<int> resized(static_cast<size_t>(width) * height, fill);

	// Preserve any overlapping tiles from the previous grid.
	const unsigned int copy_w = (width < this->width) ? width : this->width;
	const unsigned int copy_h = (height < this->height) ? height : this->height;
	for (unsigned int y = 0; y < copy_h; ++y) {
		for (unsigned int x = 0; x < copy_w; ++x) {
			resized[static_cast<size_t>(y) * width + x] = this->tiles[static_cast<size_t>(y) * this->width + x];
		}
	}

	this->width = width;
	this->height = height;
	this->tiles.swap(resized);

	// Keep the spawn inside the (possibly shrunk) map.
	if (this->width == 0 || this->height == 0) {
		this->start_x = 0;
		this->start_y = 0;
	} else {
		if (this->start_x >= this->width)  this->start_x = this->width - 1;
		if (this->start_y >= this->height) this->start_y = this->height - 1;
	}
}

bool TileMap::in_bounds(unsigned int x, unsigned int y) const
{
	return (x < this->width) && (y < this->height);
}

int TileMap::get_tile(unsigned int x, unsigned int y) const
{
	if (!this->in_bounds(x, y)) {
		return -1;
	}
	return this->tiles[static_cast<size_t>(y) * this->width + x];
}

void TileMap::set_tile(unsigned int x, unsigned int y, int tile_id)
{
	if (!this->in_bounds(x, y)) {
		return;
	}
	this->tiles[static_cast<size_t>(y) * this->width + x] = tile_id;
}

unsigned int TileMap::get_start_x() const { return this->start_x; }
unsigned int TileMap::get_start_y() const { return this->start_y; }

void TileMap::set_start(unsigned int x, unsigned int y)
{
	this->start_x = x;
	this->start_y = y;
}

// Parse a line of comma-separated integers into `out`. Returns how many were read.
static size_t parse_int_row(const std::string& line, std::vector<int>& out)
{
	out.clear();
	std::stringstream ss(line);
	std::string cell;
	while (std::getline(ss, cell, ',')) {
		// Skip empty cells produced by trailing commas / whitespace-only lines.
		bool has_digit = false;
		for (char c : cell) {
			if (c != ' ' && c != '\t' && c != '\r') { has_digit = true; break; }
		}
		if (!has_digit) continue;
		out.push_back(std::stoi(cell));
	}
	return out.size();
}

bool TileMap::load_from_file(const std::string& path)
{
	std::ifstream in(path);
	if (!in.is_open()) {
		return false;
	}

	std::string line;
	// Header: width,height[,startX,startY]
	if (!std::getline(in, line)) {
		return false;
	}
	std::vector<int> header;
	if (parse_int_row(line, header) < 2 || header[0] < 0 || header[1] < 0) {
		return false;
	}

	const unsigned int w = static_cast<unsigned int>(header[0]);
	const unsigned int h = static_cast<unsigned int>(header[1]);

	std::vector<int> loaded;
	loaded.reserve(static_cast<size_t>(w) * h);

	std::vector<int> row;
	unsigned int rows_read = 0;
	while (rows_read < h && std::getline(in, line)) {
		if (parse_int_row(line, row) == 0) {
			continue; // tolerate blank lines between rows
		}
		for (unsigned int x = 0; x < w; ++x) {
			loaded.push_back(x < row.size() ? row[x] : 0);
		}
		++rows_read;
	}

	// Missing rows are filled with 0 so a truncated file still yields a valid grid.
	loaded.resize(static_cast<size_t>(w) * h, 0);

	this->width = w;
	this->height = h;
	this->tiles.swap(loaded);

	this->start_x = (header.size() >= 4 && header[2] >= 0) ? static_cast<unsigned int>(header[2]) : 0;
	this->start_y = (header.size() >= 4 && header[3] >= 0) ? static_cast<unsigned int>(header[3]) : 0;
	if (this->width  == 0 || this->start_x >= this->width)  this->start_x = 0;
	if (this->height == 0 || this->start_y >= this->height) this->start_y = 0;

	return true;
}

bool TileMap::save_to_file(const std::string& path) const
{
	std::ofstream out(path, std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}

	// Header carries the spawn so the editor can round-trip it.
	out << this->width << ',' << this->height << ','
	    << this->start_x << ',' << this->start_y << '\n';

	for (unsigned int y = 0; y < this->height; ++y) {
		for (unsigned int x = 0; x < this->width; ++x) {
			if (x != 0) out << ',';
			out << this->tiles[static_cast<size_t>(y) * this->width + x];
		}
		out << '\n';
	}

	return out.good();
}
