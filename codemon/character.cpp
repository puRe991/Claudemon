#include "SFML/Graphics.hpp"
#include "character.h"
#include "direction.h"
#include "Coordinates.h"
#include "map.h"
#include <cassert>

/* Default Constructor */
Character::Character() {
	this->pos = Coordinates(0, 0);
	this->walk_anim_index = 0;
	sf::Texture sprite_sheet;
	sf::Sprite current_sprite;
	this->facing = DIR::NONE;

	this->sprite_width = 32;
	this->sprite_height = 32;
}

// Coordinate Constructor
Character::Character(int x, int y) {
	//Create character at a position
	this->pos = Coordinates(x, y);
	this->walk_anim_index = 0;
	sf::Texture sprite_sheet;
	sf::Sprite current_sprite;
	this->facing = DIR::NONE;

	this->sprite_width = 32;
	this->sprite_height = 32;

}

/* Getters Section */
int Character::get_x()
{
	return this->pos.get_x();
}

int Character::get_y()
{
	return this->pos.get_y();
}

/* Setters section */

void Character::set_x(int new_x)
{
	this->pos.set_x(new_x);
}

void Character::set_y(int new_y)
{
	this->pos.set_y(new_y);
}

/*Move this character by an ammount on the X axis.
-Set facing direction for the character
-Move the character
-No error, bounds or collision checks
*/
void Character::move_x(int new_x)
{
	//Determine which direction the proposed move (new_x) is in.
	if (new_x >= 0) {
		this->set_facing(DIR::E); //Face East
	}
	else {
		this->set_facing(DIR::W); //Face West
	}
	//Add the diff to this character's position
	this->set_x(this->get_x() + new_x);
}

/*/Move this character byand ammount on the Y axis.
-Set facing direction for the character
-Move the character
-No error, bounds or collision checks
*/
void Character::move_y(int new_y)
{
	if (new_y >= 0) {
		this->set_facing(DIR::S);
	}
	else {
		this->set_facing(DIR::N);
	}
	//Add the diff to this character's position
	this->set_y(this->get_y() + new_y);
}

//Return this character's position
Coordinates Character::get_pos()
{
	return this->pos;
}

/* Return a pointer to the current sprite. Its basically a getter */
sf::Sprite* Character::get_current_sprite()
{
	//Update sprite
	return &this->current_sprite;
}

/* Return a pointer to the entire sprite sheet. Its basically a getter */
sf::Texture* Character::get_sprite_sheet() {
	return &this->sprite_sheet;
}

/* Load entire sprite sheet */
bool Character::load_sprite_sheet()
{
	// Load, into memory, the sprite sheet from a static path (staticness is temp). 
	// Load into mem from storage.
	if (!this->sprite_sheet.loadFromFile("assets/Red_player.png")) {
		return false;
	}

	// Set the Character's sprite sheet into the obj.
	this->current_sprite.setTexture(*this->get_sprite_sheet());

	// Set what part of the sprite sheet is displayed.
	this->current_sprite.setTextureRect(
		sf::IntRect(
			0, 0, 
			this->sprite_width, this->sprite_height
		)
	);
	return true;
}

//Given a direction, return this character's new position after 
//a hypothetical move. Does not actually move character.
Coordinates Character::move_cord(DIR move_dir) {
	//Probably unneccessary, but good to have while initially setting up 
	//program
	assert(move_dir != DIR::NONE);

	//Result holder - A vector tuple that describes how the chararcter would move
	Coordinates delta(0,0);		

	//Fill result holder with apprioriate vector based on given, desired move direction.
	switch (move_dir) {
	case DIR::N:
		delta = delta + Coordinates(1, 0);
		break;
	case DIR::S:
		delta = delta + Coordinates(-1, 0);
		break;
	case DIR::E:
		delta = delta + Coordinates(0, 1);
		break;
	case DIR::W:
		delta = delta + Coordinates(0, -1);
		break;
	}

	//return Tile position that the character would occupy IF they moved
	return this->get_pos() + delta;

}

//Move character in a direction. 
//This func actually changes characters position state.
void Character::move(DIR new_dir)
{
	//Make sure its a valid move direction
	assert(new_dir != DIR::NONE);
	this->facing = new_dir;
	switch (new_dir) {
	case DIR::N:
		move_y(0 - this->sprite_height);
		break;
	case DIR::S:
		move_y(this->sprite_height);
		break;
	case DIR::E:
		move_x(this->sprite_width);
		break;
	case DIR::W:
		move_x(0 - this->sprite_width);
		break;
	}
}

// Update sprite animation for rendering. 
// Uses the, hopefully, apprioriately set facing and walk animation values to select which sprite from 
// the sprite sheet should be shown
void Character::update_sprite_pos() {

	//Set display target rectangle. 
	//Relies on, currently:
	// 1. the walking animation index being the value which describes what part of the walk cycle is.
	// 2. The relationship between the characters facing enum value 
	//     being the index for what direction a sprite faces in the sprite sheet 
	//Essentially as the character walks in a direction, the selected rectangles 
	//shifts in the X-axis along the 2d spritesheet. Similarily, facing changes the y-axis.
	sf::IntRect walk_mask = sf::IntRect(
		walk_anim_index * this->sprite_width, 
		this->facing * this->sprite_width, 
		this->sprite_width, this->sprite_height
	);

	//Update position of cuurent sprite based on the characters position in tile map.
	//TODO: decouple updating the spritesheet mask and updating a characters position in map.
	this->current_sprite.setPosition((float) this->get_x(), (float) this->get_y());

	// Set the previously calculated selection rectangle. 
	this->current_sprite.setTextureRect(walk_mask);

	//Advance the walk cycle.
	this->walk_anim_index = (this->walk_anim_index + 1) % 3;
}

// Reposition the drawn sprite to the current pixel position only. No walk-cycle
// advance - used on every in-between frame while the character glides.
void Character::refresh_position() {
	this->current_sprite.setPosition((float)this->get_x(), (float)this->get_y());
}

// Show the standing frame (walk index 0) for the current facing direction.
void Character::set_standing() {
	this->walk_anim_index = 0;
	sf::IntRect stand_mask = sf::IntRect(
		0, this->facing * this->sprite_width,
		this->sprite_width, this->sprite_height
	);
	this->current_sprite.setTextureRect(stand_mask);
	this->current_sprite.setPosition((float)this->get_x(), (float)this->get_y());
}

/*Getter - updates facing direction to the given direction.*/
void Character::set_facing(DIR dir)
{
	this->facing = dir;
}
