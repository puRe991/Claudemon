#include "map.h"
#include "Tile.h"
#include "Window.h"
#include <string>
#include <sstream>
#include <iostream>
#include <cassert>

/* Generic Tile size specification. Works for bare ints or coordinates */
void Map::set_tile_size(unsigned int tile_width, unsigned int tile_height) {
    this->tile_dimensions = Coordinates(tile_width, tile_height);
}

void Map::set_tile_size(Coordinates tile_dimensionality) {
    this->tile_dimensions = tile_dimensionality;
}

int Map::get_tile_width() {
    return this->tile_dimensions.get_x();
}

int Map::get_tile_height() {
    return this->tile_dimensions.get_y();
}

//Create a map object from a given paths to a map file and a tile sheet for said map.
Map::Map(std::string map_path, std::string sheet_path) {
    /* Tilesize hardcoding */
    unsigned int tile_width = 32;
    unsigned int tile_height = 32;

    /*Initialize all state variables*/
    this->set_dimensions(Coordinates(0, 0));
    this->set_start_pos(Coordinates(0, 0));
    this->set_tile_size(Coordinates(tile_width, tile_height));

    //relative paths to binary map file and sprite sheet image
    this->map_path = map_path;
    this->sheet_path = sheet_path;
    std::fstream mapfile;               //create a spot for map fstream

    /* Load - the sprite sheet for this map. */
    if (!this->map_sheet.loadFromFile(this->sheet_path))
    {
        // TODO: error...
    }

    /* Load - the integer map textfile for this map */
    std::string delim = ",";                        //Deliminator for map file
    mapfile.open(this->map_path, std::ios::in);

    unsigned int x = 0;
    unsigned int y = 0;

    //if the map_storage file is successfully opened
    if (mapfile.is_open()) {
        std::string new_row;
        std::string tile;

        //Read in height and width data and then create the tile map storage
        std::getline(mapfile, new_row);
        this->set_width((unsigned int)(new_row[0] - '0'));
        this->set_height((unsigned int)(new_row[2] - '0'));

        //Create the tile storage matrix
        this->tile_map = new Tile[this->get_width() * this->get_height()];

        //Read in each Tile::enum and create a tile obj for it.
        //Store said tile in the tile_map
        while (std::getline(mapfile, new_row)) {
            std::stringstream tile_row_stream(new_row);
            //Read in each character for each row
            while (std::getline(tile_row_stream, tile, ',')) {
                Tile* new_tile = new Tile(x, y, (Tile::tile)std::stoi(tile));
                this->add_tile(new_tile);
                x++;
            }
            x = 0;
            y++;
        }
        //Close the input file after reading.
        mapfile.close();
    }
        return;
}

//Return a texture rendered from get_map();
//This only works because each tilesheet is constructed such that its value of the enum is its position in the 
//tile sheet row. i.e. the index of a specific tiles graphics is given by the enum. Pretty dangerous coupling 
//will change
void Map::render_map(Window* active_window)
{
    //Get the spritesheet read to sample from
    sf::Sprite to_draw(this->map_sheet);
    //Read through tile_map and render each tile
    for (unsigned int y = 0; y < this->get_height(); y++) {             //Each row
        for (unsigned int x = 0; x < this->get_height(); x++) {         //Each column in each row
            //Get the tile representation
            Tile::tile curr_tile = this->tile_map[y * this->get_height() + x].get_data();

            /* Set the appropriate sprite_sheet area to draw */
            //Calculatex offset of the current tile from the spirite sheet
            //the tile type id (i.e. the number its saved as) is the index to the spirite sheet
            int x_sheet_off = ((int)curr_tile) * this->get_tile_width(); 
            int y_sheet_off = 0; //set as 0 currently. Will need to be extended evetually

            //Clip the tile from the sprite sheet
            sf::IntRect mask_rect = sf::IntRect(
                x_sheet_off, y_sheet_off, 
                this->get_tile_width(), this->get_tile_height()
            );
            to_draw.setTextureRect(mask_rect);

            /* Calculate & set the destination coords of the tile for drawing */
            float dest_x = (float)x * this->get_tile_width();
            float dest_y = (float)y * this->get_tile_height();
            to_draw.setPosition(dest_x, dest_y);

            //Draw the current tile to active_windows buffer
            active_window->draw(&to_draw);
        }
    }
}

//Returns boolean of wether a coordinate is within map boundaries of not.
//I can skip the lower bounds check that is usually present in a 
//boundary check as coordinates are defined as unsigned
bool Map::in_bounds(Coordinates proposed_coord) {
    return (proposed_coord.get_x() < this->get_width()) && (proposed_coord.get_y() < this->get_height());
}

/* Getters and Setters section */

void Map::set_dimensions(Coordinates dim) {
    this->dimensions = dim;
}

Coordinates* Map::get_dimensions() {
    return &this->dimensions;
}

void Map::set_width(unsigned int width) {
    this->get_dimensions()->set_x(width);
}

unsigned int Map::get_height() {
    return this->get_dimensions()->get_y();
}

void Map::set_height(unsigned int height) {
    this->get_dimensions()->set_y(height);
}

unsigned int Map::get_width() {
    return this->get_dimensions()->get_x();
}

void Map::set_start_pos(Coordinates start) {
    this->start_pos = start;
}

Tile* Map::get_map()
{
    return this->tile_map;
}

void Map::add_tile(Tile* new_tile)
{
    this->tile_map[new_tile->get_x() + new_tile->get_y() * this->get_width()] = *new_tile;
}