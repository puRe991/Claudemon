#pragma once
#include <string>
#include "GameState.h"
#include "direction.h"

/******************************************************************************
Bike - the MACH BIKE / ACRO BIKE as a real field system.

Rydel's shop in Mauville already hands the bike over through its own imported
script (`giveitem ITEM_MACH_BIKE`/`ITEM_ACRO_BIKE`, swappable later), so this
class owns only what happens afterwards: whether the player may get on right
now, how fast they then move, which overworld sheet they are drawn with, and
what they are told when it doesn't work.

Everything here is pure logic over the bag and two booleans (indoors,
surfing), so the rules are testable without a window -- the game loop in
main.cpp does the sprite swap and the message box.

Fidelity notes, all deliberate:
* Real Emerald refuses the bike indoors and while surfing, and dismounts you
  automatically when you enter a building. Both are implemented.
* The MACH BIKE's defining trait is acceleration: it starts at the ACRO
  BIKE's speed and gets faster the longer you hold one direction. That is
  modelled (see step_interval); its metatile-specific tricks (climbing muddy
  slopes) and the ACRO BIKE's bunny hop are not -- this engine has no
  metatile behaviours for either, so both bikes ride the same terrain.
* pokeemerald plays MUS_CYCLING while riding; that track was never converted
  into assets/sfx/music (see tools/pe_import.py), so the map's own music
  keeps playing instead of a silent BGM swap.
*****************************************************************************/

enum class BikeKind { NONE, MACH, ACRO };

// What a mount/dismount attempt did, so the caller can show the right line.
enum class BikeResult {
	MOUNTED,
	DISMOUNTED,
	NO_BIKE,      // nothing in the bag yet (Rydel hasn't been visited)
	INDOORS,      // "Hier kannst du nicht Rad fahren!"
	SURFING,      // already on a Pokemon's back
};

class Bike
{
public:
	// The bike currently in the bag. The shop only ever leaves one of the two
	// there (it swaps rather than adds), but if both are somehow present the
	// MACH BIKE wins -- it's the one the player would have registered.
	static BikeKind in_bag(const GameState& gs);
	static const char* item_id(BikeKind kind);
	// German display name, for the mount/dismount messages.
	static const char* display_name(BikeKind kind);

	// The overworld sheet to draw the player with: the bike sheets while
	// riding, the ordinary walking sheet otherwise. Both genders have their
	// own set (people_{brendan,may}_{mach,acro}_bike.png), each the same
	// 9-frame layout as the walking sheet, just 32px wide per frame.
	static std::string sheet_for(BikeKind kind, bool female);

	bool riding() const { return this->kind != BikeKind::NONE; }
	BikeKind riding_kind() const { return this->kind; }

	// Get on if possible, get off if already riding. `indoors`/`surfing`
	// describe the player's situation right now; see BikeResult for the
	// refusals.
	BikeResult toggle(const GameState& gs, bool indoors, bool surfing);
	// Forced dismount (walked into a building, started surfing, whited out).
	// Returns true if the player actually was on a bike.
	bool dismount();

	// Restore a saved ride (GameState::on_bike) without re-running the
	// situation checks -- the save already knows the player was riding, and
	// the map they resume on is the one they were riding on.
	void resume(const GameState& gs);

	// One overworld step was taken in `dir` (the game loop calls this only
	// when a step really happened). Feeds the MACH BIKE's acceleration.
	void on_step(DIR dir);
	// Movement stopped (no direction held, a warp, a battle, a script).
	void on_stop() { this->streak = 0; }

	// How long to wait between steps while riding, given the engine's normal
	// walking interval. The ACRO BIKE is a flat 2x walking speed -- the same
	// as the Running Shoes; the MACH BIKE starts there and accelerates to 3x
	// after MACH_RAMP_STEPS steps in one direction, then holds that.
	float step_interval(float walk_interval) const;

	static constexpr int MACH_RAMP_STEPS = 4;

private:
	BikeKind kind = BikeKind::NONE;
	int streak = 0;              // consecutive steps in `streak_dir`
	DIR streak_dir = DIR::NONE;
};
