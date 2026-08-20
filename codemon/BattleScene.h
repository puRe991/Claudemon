#pragma once
#include <string>
#include "SFML/Graphics.hpp"

/******************************************************************************
BattleScene - the wild-encounter entry screen.

When the player steps into tall grass and an encounter fires, this draws a
simple battle backdrop with the wild pokemon's coloured front sprite. The
"A wild X appeared!" line is shown through the shared DialogBox; dismissing it
ends the encounter and returns to the overworld.
*****************************************************************************/
class BattleScene
{
private:
	sf::Texture mon_tex;
	bool active;
	std::string species;

public:
	BattleScene();

	// Load assets/pokemon/<SPECIES>.png and enter the scene. False if missing.
	bool start(const std::string& species_name,
	           const std::string& asset_dir = "assets");
	void end();
	bool is_active() const;
	const std::string& current_species() const;

	// Draw the backdrop + wild sprite in screen space.
	void draw(sf::RenderTarget& target);
};
