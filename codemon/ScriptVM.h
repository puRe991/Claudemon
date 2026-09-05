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
	// Human-readable name of the current map, stamped as the met location on
	// anything `givemon`/`giveegg` hands over here (§2). Set by the game loop
	// on every map change; empty just leaves the field blank.
	void set_met_location(const std::string& loc) { this->met_location = loc; }

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
	// Force-stop whatever script is currently running (debug menu only --
	// real play always lets a script run to its own `end`/`release`).
	void abort() { finish(); }

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

	// `opendoor X, Y` / `closedoor X, Y`: pokeemerald's field_door.c swaps in
	// a small sprite (graphics/door_anims/<set>.png, 4 stacked 16x16 frames:
	// closed/frame1/frame2/open) drawn over the door metatile while a task
	// steps through sDoorOpenAnimFrames/sDoorCloseAnimFrames (4 GBA frames
	// per step, ~0x100 tile offsets between them). This engine has no
	// per-tileset door-graphics table (see pe_import.py; only the raw PNGs
	// were mirrored in, not the map->sheet assignment), so it always uses
	// the most common sheet ("general.png") rather than picking the right
	// one per location -- a deliberate approximation, not a no-op. Timing
	// mirrors the original's 4-GBA-frame (~1/15s) hold per step.
	// `opendoor`/`closedoor` themselves don't block (matching the original,
	// which schedules them as a task and returns immediately); only the
	// following `waitdooranim` actually blocks the script.
	// pokeemerald's emote movement actions (Common_Movement_ExclamationMark
	// and friends): a bubble pops up over an object's head for a moment. The
	// importer could only turn those movement scripts into a plain delay --
	// this keeps the delay and tells the renderer to draw the bubble, so a
	// trainer noticing the player reads as it does in the original.
	// Substitute the dialogue tokens the importer leaves in text: STR_VAR_n
	// buffered by the script, plus PLAYER/RIVAL. Public because plain NPC
	// lines (an object with no script of its own) are shown straight from the
	// map data by main.cpp and need exactly the same treatment -- without it
	// they greeted the player as "PLAYER".
	std::string expand_text(const std::string& in) const;
	// Resolve a script symbol (constant name, VAR_*, literal number) the same
	// way the opcodes do -- the game loop needs it for map on-load triggers,
	// whose values are symbolic constants as often as they are numbers.
	int const_value(const std::string& s) const { return value_of(s); }

	bool emote_active() const { return this->emote_t > 0.f && this->emote_ch; }
	const Character* emote_target() const { return this->emote_ch; }
	// 0 = exclamation, 1 = question, 2 = heart
	int emote_kind() const { return this->emote_icon; }

	bool door_anim_active() const { return this->door_active; }
	void get_door_tile(int& x, int& y) const { x = this->door_x; y = this->door_y; }
	// -1 = draw nothing (closed door metatile shows through as-is), else the
	// 0-based frame row into the door sheet (0 = just-cracked-open .. 2 = open).
	int door_frame() const { return this->door_frame_idx; }
	static constexpr float DOOR_STEP_DURATION = 4.f / 60.f;

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

	// `bufferstring`/`buffernumberstring`/`bufferspeciesname`/... park their
	// result here (see str_vars below); "" for a name that was never set.
	const std::string& str_var(const std::string& name) const {
		static const std::string empty;
		auto it = this->str_vars.find(name);
		return it == this->str_vars.end() ? empty : it->second;
	}

private:
	enum State { IDLE, RUN, WAIT_MSG, WAIT_MOVE, WAIT_BATTLE, WAIT_STARTER, WAIT_YESNO,
	              WAIT_SHOP, WAIT_HEALFX, WAIT_CHOOSE_PARTY, WAIT_MULTICHOICE, WAIT_DOOR };

	Map* map; GameState* state; DialogBox* box; Battle* battle;
	Audio* audio; Character* player; Character* owner;
	BattleData* bdata; std::vector<Mon>* team; std::mt19937* rng = nullptr;
	std::vector<Mon>* pc_box = nullptr;   // givemon overflow when the party is full
	std::string met_location;
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

	bool door_active = false;      // a door sprite is currently shown/animating
	bool door_waiting = false;     // `waitdooranim` is blocked on it
	bool door_opening = true;      // which frame sequence (open vs close)
	int door_x = -1, door_y = -1;
	int door_step = 0;             // 0..3 index into the open/close frame sequence
	int door_frame_idx = -1;       // frame currently shown (-1 = none)
	Character* emote_ch = nullptr;  // object the emote bubble sits above
	float emote_t = 0.f;            // seconds of bubble left
	int emote_icon = 0;             // 0 exclamation, 1 question, 2 heart
	// Objects currently running a one-shot state animation (rock smash, tree
	// cut, nurse bow). Held only so `waitmovement` can wait for them and
	// update() can tick them; cleared as soon as they are all done.
	std::vector<Character*> state_anims;
	float door_timer = 0.f;

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

	// --- Battle Tent (Slateport's Battle Swap event) -------------------
	// pokeemerald keeps all of this in the save file's frontier data; this
	// engine holds it here for the duration of one challenge (the VM object
	// outlives every warp between lobby, corridor and battle room), which is
	// why a challenge cannot be paused and resumed across a save.
	std::vector<Mon> party_backup;                             // SavePlayerParty
	std::vector<std::pair<std::string, int>> tent_rentals;     // the loaned three
	std::vector<std::pair<std::string, int>> tent_opponent;    // this round's three
	std::vector<std::pair<std::string, int>> tent_swap_pool;   // the beaten team, to swap from
	int tent_battle_num = 0;          // FRONTIER_DATA_BATTLE_NUM (wins so far)
	int tent_status = 0;              // FRONTIER_DATA_CHALLENGE_STATUS
	int tent_lvl_mode = 0;            // FRONTIER_DATA_LVL_MODE
	bool tent_paused = false;         // FRONTIER_DATA_PAUSED
	std::string tent_prize;           // ITEM_* still owed, "" = none
	bool tent_swap_pending = false;   // a slateporttent_swapmons multichoice is open
	bool special_battle = false;      // battle started by DoSpecialTrainerBattle
	std::vector<std::pair<std::string, int>> roll_tent_team();
	void tent_give_rentals();

	int value_of(const std::string& s) const;   // resolve a symbol/number
	Character* resolve(const std::string& localid) const;
	void jump(const std::string& label);
	void pump();                                 // run until blocked or done
	void apply_move_action(Character* ch, const std::string& act);
	void finish();
};
