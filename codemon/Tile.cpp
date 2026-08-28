#include "Tile.h"
#include <string>
#include "SFML/Graphics.hpp"

/* Constructors */

//Naked int constructor
Tile::Tile(unsigned int x, unsigned int y, 	int data) {
	this->pos = new Coordinates(x, y);
	this->data = data;
}

//Coordinate pointer constructor
Tile::Tile(Coordinates *tile_pos, int data) {
	this->pos = tile_pos;
	this->data = data;
}

//Empty Constructor
Tile::Tile() {
	this->pos = new Coordinates(0, 0);
	this->data = 0;
}

/* Coordinate getters*/
unsigned int Tile::get_x()
{
	return this->get_pos()->get_x();
}


unsigned int Tile::get_y()
{
	return this->get_pos()->get_y();
}

Coordinates* Tile::get_pos() {
	return this->pos;
}

//Coordinate setter
void Tile::set_pos(unsigned int x, unsigned int y) {
	this->get_pos()->set_x(x);
	this->get_pos()->set_y(y);
}

/* Metatile id getter */
int Tile::get_data()
{
	return this->data;
}



