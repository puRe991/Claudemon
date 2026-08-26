#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <random>
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

LOCALID resolution: LOCALID_PLAYER is the player; a name found in the current
map's localid_map (object events porymap actually named, see NpcSpawn::
local_id) is that specific object, for cutscenes that address more than one
NPC by id (e.g. Wally's Petalburg Gym battle); anything else (VAR_LAST_TALKED,
an unnamed object, ...) falls back to the script's owner -- the NPC the player
interacted with, enough for the common "NPC approaches the player" case.
*****************************************************************************/
class ScriptVM
{
public:
	ScriptVM();

	// `actors` is every Character on the current map (player + NPCs), for
	// opcodes that need to find/move objects by world position rather than
	// LOCALID (the Mossdeep Gym rotating-tile puzzle).
	void configure(Map* map, GameState* state, DialogBox* box, Battle* battle,
	               Audio* audio, Character* player,
	               std::vector<Character*>* actors = nullptr,
	               const std::unordered_map<std::string, Character*>* localid_map = nullptr);

	// Battle hooks for trainerbattle (set once at startup). `team` is the
	// player's whole party (not just the lead mon) so `special
	// HealPlayerParty` can actually heal all of it, not only team[0]. `rng`
	// is the same shared, seeded generator main() hands to Battle -- needed
	// for make_mon()'s IV/nature roll (givemon, in-game trades, starters).
	// `pc` is the PC box, where `givemon` puts a gift mon when the party is
	// already full (optional: without it a full party just refuses the gift).
	void set_battle_data(BattleData* bd, std::vector<Mon>* team, std::mt19937* rng,
	                     std::vector<Mon>* pc = nullptr) {
		this->bdata = bd; this->team = team; this->rng = rng; this->pc_box = pc;
	}

	// Begin running `label`; owner is the interacted NPC (for LOCALID + face).
	void start(const std::string& label, Character* owner);

	// True if `label` is a trainer's battle script whose trainer has not been
	// beaten yet. Drives the on-sight challenge in main.cpp: a trainer only
	// spots the player while they still owe them a battle -- afterwards
	// walking back through their line of sight must do nothing at all.
	bool script_has_pending_trainer(const std::string& label) const;

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

	// `special ChoosePartyMon` (in-game trades: offer up a party mon): same
	// story as ChooseStarter -- the VM can't drive a real party-list picker
	// itself, so the game loop shows one and calls resolve_choose_party_mon()
	// with the chosen index, or -1 if the player backed out.
	bool wants_choose_party_mon() const { return this->st == WAIT_CHOOSE_PARTY; }
	void resolve_choose_party_mon(int idx);

	// `multichoice`/`multichoicedefault`: a fixed option list (see
	// MULTICHOICE_LISTS in ScriptVM.cpp) the player picks one of. Same story
	// as ChoosePartyMon -- the VM can't drive a real cursor-driven menu
	// itself, so the game loop shows one (MultiChoicePrompt in main.cpp) and
	// calls resolve_multichoice() with the picked index.
	bool wants_multichoice() const { return this->st == WAIT_MULTICHOICE; }
	const std::vector<std::string>& multichoice_options() const { return this->pending_multichoice_options; }
	int multichoice_default() const { return this->pending_multichoice_default; }
	void resolve_multichoice(int idx);

private:
	enum State { IDLE, RUN, WAIT_MSG, WAIT_MOVE, WAIT_BATTLE, WAIT_STARTER, WAIT_YESNO,
	              WAIT_SHOP, WAIT_HEALFX, WAIT_CHOOSE_PARTY, WAIT_MULTICHOICE };

	Map* map; GameState* state; DialogBox* box; Battle* battle;
	Audio* audio; Character* player; Character* owner;
	BattleData* bdata; std::vector<Mon>* team; std::mt19937* rng = nullptr;
	std::vector<Mon>* pc_box = nullptr;   // givemon overflow when the party is full
	std::vector<Character*>* actors = nullptr;
	const std::unordered_map<std::string, Character*>* localid_map = nullptr;

	// Mossdeep Gym's (and Trick House Puzzle #7's) rotating-tile puzzle:
	// `moverotatingtileobjects` records which characters actually shifted
	// (queuing their movement), so the following `turnrotatingtileobjects`
	// knows which ones to re-face. `rot_trick_house` picks which of the two
	// real puzzles' base tile id to check, set by `initrotatingtilepuzzle`.
	std::vector<Character*> rot_objects;
	bool rot_trick_house = false;

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
	// The trainer currently being fought, so a win can mark them defeated (see
	// trainer_flag below). Cleared once that happens.
	std::string pending_trainer_id;

	// pokeemerald tracks "this trainer has been beaten" in a dedicated flag
	// array (TRAINER_FLAGS_START + trainerId). This engine has no fixed
	// trainer-id numbering, so it derives a flag name from the TRAINER_*
	// constant instead -- it lands in GameState's ordinary var map and so is
	// persisted by the savegame like every other flag.
	static std::string trainer_flag(const std::string& trainer_id) {
		return "TRAINER_DEFEATED_" + trainer_id;
	}
	bool trainer_defeated(const std::string& trainer_id) const;

	bool pending_warp = false;
	std::string warp_dest; int warp_x = -1, warp_y = -1;
	bool pending_yesno = false;   // set by pump() until the msgbox closes
	std::string pending_shop_label;
	std::vector<std::string> pending_multichoice_options;
	int pending_multichoice_default = 0;

	// `bufferpartymonnick STR_VAR_n <idx>` / `buffermovename STR_VAR_n <move>`
	// (field moves like Cut/Rock Smash): stashes the nickname/move name so a
	// following msgbox's "STR_VAR_1 used STR_VAR_2!"-style text can
	// substitute the real values in (see pump()'s msgbox handling).
	std::unordered_map<std::string, std::string> str_vars;

	// `setwildbattle SPECIES, LEVEL`: pokeemerald stashes the species/level
	// for a following `dowildbattle` (or the legendary-specific specials
	// that start one the same way) rather than passing them inline.
	std::string pending_wild_species;
	int pending_wild_level = 5;
	bool start_pending_wild_battle();   // true if a battle actually started

	int value_of(const std::string& s) const;   // resolve a symbol/number
	Character* resolve(const std::string& localid) const;
	void jump(const std::string& label);
	void pump();                                 // run until blocked or done
	void apply_move_action(Character* ch, const std::string& act);
	void finish();
};
