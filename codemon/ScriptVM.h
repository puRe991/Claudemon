#pragma once
#include <string>
#include <vector>
#include <deque>
#include "map.h"
#include "character.h"
#include "DialogBox.h"
#include "Battle.h"
#include "BattleData.h"
#include "GameState.h"
#include "Audio.h"

/******************************************************************************
ScriptVM - a cooperative interpreter for the imported pokeemerald event scripts.

Scripts block on things that take time (a message box, a walking animation), so
the VM runs to the next blocking point and yields; the game loop resumes it when
the player advances a message or a queued movement finishes.

Supported opcodes cover the common overworld set: lock/faceplayer/release,
msgbox, applymovement/waitmovement, setflag/clearflag/setvar/copyvar, the
goto/call/return family with goto_if_set/eq/ne conditionals, switch/case,
giveitem, setmetatile, and a trainerbattle stub. Unknown opcodes (special, and
GBA-only engine calls) are skipped so a script still runs to completion.

LOCALID resolution is simplified: LOCALID_PLAYER is the player and any other
object id is the NPC the player interacted with (the script owner) - enough for
the "NPC approaches the player" cutscenes.
*****************************************************************************/
class ScriptVM
{
public:
	ScriptVM();

	void configure(Map* map, GameState* state, DialogBox* box,
	               Battle* battle, Audio* audio, Character* player);

	// Battle hooks for trainerbattle (set once at startup). `team` is the
	// player's whole party (not just the lead mon) so `special
	// HealPlayerParty` can actually heal all of it, not only team[0].
	void set_battle_data(BattleData* bd, std::vector<Mon>* team) {
		this->bdata = bd; this->team = team;
	}

	// Begin running `label`; owner is the interacted NPC (for LOCALID + face).
	void start(const std::string& label, Character* owner);

	bool running() const;           // a script is active (input/NPCs frozen)
	bool waiting_message() const;    // blocked on the dialog box
	void on_key();                   // player pressed the advance key
	void update(float dt);           // advance timers / movement

	// `special ChooseStarter` (Route 101 Birch's bag): the VM can't drive a
	// multi-frame UI itself, so it blocks here and the game loop shows the
	// real starter-choice screen, then calls resolve_starter() to continue.
	bool wants_starter() const { return this->st == WAIT_STARTER; }
	void resolve_starter(const std::string& species);

	// `warp`/`warpdoor`/... (e.g. Birch's bag sending the player to his lab):
	// the VM can't swap the active Session itself, so it records the
	// destination and the game loop performs the actual map change.
	bool has_pending_warp() const { return this->pending_warp; }
	void get_pending_warp(std::string& dest, int& x, int& y) const {
		dest = this->warp_dest; x = this->warp_x; y = this->warp_y;
	}
	void clear_pending_warp() { this->pending_warp = false; }

	// `msgbox ..., MSGBOX_YESNO` (heal at the Pokemon Center, buy/sell
	// confirmations, ...): same story as ChooseStarter -- the VM can't
	// drive a cursor-driven choice itself, so the game loop shows a real
	// Ja/Nein prompt and calls resolve_yesno() with the pick.
	bool wants_yesno() const { return this->st == WAIT_YESNO; }
	void resolve_yesno(bool yes);

	// `pokemart <label>`: same story again -- the VM can't drive a
	// scrolling buy screen itself, so the game loop shows a real shop
	// using shop_items() (the map's item list for this label) and calls
	// close_shop() once the player leaves.
	bool wants_shop() const { return this->st == WAIT_SHOP; }
	const std::vector<std::string>* shop_items() const;
	void close_shop();

	// `dofieldeffect FLDEFF_POKECENTER_HEAL`: the nurse's glowing-Pokeball
	// animation. The VM times it out on its own (see update()); this just
	// lets the game loop know to draw the animation (HealFx in main.cpp)
	// while it runs, sized to the party (one ball per mon).
	bool wants_heal_fx() const { return this->st == WAIT_HEALFX; }
	// Shared with HealFx in main.cpp so the animation and the VM's own
	// block-then-resume timer run for the same length of time.
	static constexpr float HEALFX_DURATION = 1.5f;

private:
	enum State { IDLE, RUN, WAIT_MSG, WAIT_MOVE, WAIT_BATTLE, WAIT_STARTER, WAIT_YESNO,
	              WAIT_SHOP, WAIT_HEALFX };

	Map* map; GameState* state; DialogBox* box; Battle* battle;
	Audio* audio; Character* player; Character* owner;
	BattleData* bdata; std::vector<Mon>* team;

	State st;
	std::string cur;                 // current script label
	size_t ip;                       // instruction pointer
	std::vector<std::pair<std::string, size_t>> call_stack;
	int switch_value;

	struct MoveQ { Character* ch; std::deque<std::string> actions; };
	std::vector<MoveQ> queues;
	float move_timer;
	float healfx_timer = 0.f;
	std::string pending_win_script;  // trainerbattle's post-victory script label, if any

	bool pending_warp = false;
	std::string warp_dest; int warp_x = -1, warp_y = -1;
	bool pending_yesno = false;   // set by pump() until the msgbox closes
	std::string pending_shop_label;

	int value_of(const std::string& s) const;   // resolve a symbol/number
	Character* resolve(const std::string& localid) const;
	void jump(const std::string& label);
	void pump();                                 // run until blocked or done
	void apply_move_action(Character* ch, const std::string& act);
	void finish();
};
