#pragma once
#include "character.h"
#include "direction.h"
#include "Coordinates.h"


/******************************************************************************
Character class - does whatever a character does. 
Seriously though its pretty boiler plate. 
Everything operaters through setters and getters.

1/1/2023

*****************************************************************************/
class Character
{
private:
	//Helper function that gets a pointer to the spirte sheet.
	sf::Texture* get_sprite_sheet();

	//Position in the current map. None if not on a map.
	Coordinates pos;
	//Sprite sheet.
	sf::Texture sprite_sheet;
	//current walk animation sprite.
	sf::Sprite current_sprite;

	//Which part of the walk cycle this character is at.
	unsigned char walk_anim_index;
	
	//What direction of is this character facing.
	DIR facing;

	//Sprite dimensions
	unsigned int sprite_width;
	unsigned int sprite_height;
	
public:
	/*Constructors*/
	Character();
	Character(int x, int y);

	/*coordinate element getters*/
	int get_x();
	int get_y();

	/*coordinate element setters*/
	void set_x(int new_x);
	void set_y(int new_y);

	/*Coordinate element modifier functions*/
	//Change x or y values by the given ammount.
	void move_x(int new_x);
	void move_y(int new_y);

	/*move character in given direction*/
	void move(DIR move_dir);

	//Returns this character's coordinates after a 
	//succesful move in a direction
	Coordinates move_cord(DIR move_dir);

	//Get this characters current position
	Coordinates get_pos();

	/*Sprite accounting functions*/
	sf::Sprite* get_current_sprite();
	bool load_sprite_sheet();
	void update_sprite_pos();
	//Move the drawn sprite to the character's current pixel position without
	//advancing the walk cycle. Used every frame while gliding between tiles.
	void refresh_position();
	//Show the standing (non-walking) frame for the current facing.
	void set_standing();
	void set_facing(DIR dir);
};

