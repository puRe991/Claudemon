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

	// Battle hooks for trainerbattle (set once at startup).
	void set_battle_data(BattleData* bd, Mon* party) { this->bdata = bd; this->party = party; }

	// Begin running `label`; owner is the interacted NPC (for LOCALID + face).
	void start(const std::string& label, Character* owner);

	bool running() const;           // a script is active (input/NPCs frozen)
	bool waiting_message() const;    // blocked on the dialog box
	void on_key();                   // player pressed the advance key
	void update(float dt);           // advance timers / movement

private:
	enum State { IDLE, RUN, WAIT_MSG, WAIT_MOVE, WAIT_BATTLE };

	Map* map; GameState* state; DialogBox* box; Battle* battle;
	Audio* audio; Character* player; Character* owner;
	BattleData* bdata; Mon* party;

	State st;
	std::string cur;                 // current script label
	size_t ip;                       // instruction pointer
	std::vector<std::pair<std::string, size_t>> call_stack;
	int switch_value;

	struct MoveQ { Character* ch; std::deque<std::string> actions; };
	std::vector<MoveQ> queues;
	float move_timer;

	int value_of(const std::string& s) const;   // resolve a symbol/number
	Character* resolve(const std::string& localid) const;
	void jump(const std::string& label);
	void pump();                                 // run until blocked or done
	void apply_move_action(Character* ch, const std::string& act);
	void finish();
};
