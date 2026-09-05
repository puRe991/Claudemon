#pragma once
#include <string>
#include <vector>
#include "GameState.h"

/******************************************************************************
QuestLog - the AUFGABEN (quest/task) system: what the player is supposed to be
doing right now, drawn as a menu screen, as a HUD line over the overworld, and
as a marker in the world itself.

Design brief §16-18. The important property is that a quest owns *no* state of
its own: every quest, step and completion condition is a predicate over the
flags and variables the imported pokeemerald scripts already set (FLAG_BADGE01_
GET, VAR_RUSTBORO_CITY_STATE, ...). Nothing has to be written into a script,
nothing extra has to be saved, and a quest cannot ever disagree with the world
-- deleting quests.txt just removes the UI, it can never break a playthrough.
The only piece of player intent that isn't derivable, "which quest do I want
tracked on the HUD", lives in GameState (tracked_quest) and rides along in the
savegame like any other setting.

Quest data lives in assets/quests.txt:

    quest QUEST_FIRST_BADGE main
    title Die naechste Arena
    desc Reise nach Rustboro City und fordere den Arenaleiter heraus.
    start FLAG_ADVENTURE_STARTED
    step Erreiche Rustboro City | FLAG_VISITED_RUSTBORO_CITY | RustboroCity
    step Besiege Rocko | FLAG_BADGE01_GET | RustboroCity_Gym

A step's three fields are its text, the condition that completes it, and an
optional target ("<map>" or "<map> <x> <y>") the world marker points at.

Condition grammar (evaluated against GameState, see eval_condition):
  FLAG_X            set (non-zero)
  !FLAG_X           not set
  VAR_X >= 3        any of >=, <=, ==, !=, >, < against a number
  ITEM_X            at least one in the bag (ITEM_X >= 2 works too)
  caught:SPECIES    that species is registered as caught
  seen:SPECIES      ... seen or caught
  money, badges     read as numbers (badges = how many FLAG_BADGE0n_GET are set)
  a & b             both        (binds tighter than |)
  a | b             either
*****************************************************************************/

enum class QuestKind { MAIN, SIDE };
enum class QuestStatus { HIDDEN, ACTIVE, DONE };

struct QuestStep {
	std::string text;        // "Besiege Rocko in der Arena"
	std::string done_cond;   // completes the step
	std::string target_map;  // "" = no world marker for this step
	int target_x = -1, target_y = -1;   // -1 = "somewhere on that map"
	bool done = false;       // recomputed by refresh()
};

struct Quest {
	std::string id;          // QUEST_*
	std::string title;
	std::string description;
	QuestKind kind = QuestKind::SIDE;
	std::string start_cond;  // "" = available from the start
	std::string done_cond;   // "" = done once every step is done
	std::vector<QuestStep> steps;

	// --- recomputed by refresh(), never stored ---------------------------
	QuestStatus status = QuestStatus::HIDDEN;
	int current_step = 0;    // first unfinished step (== steps.size() when done)
	int percent = 0;         // 0..100
};

class QuestLog
{
public:
	// Missing/unreadable file is not an error: the game simply has no quests.
	bool load(const std::string& path = "assets/quests.txt");

	// Recompute every quest's status/step/percent from the world state. Cheap
	// (a few dozen string predicates), so the game loop just calls it whenever
	// the world may have moved on.
	void refresh(const GameState& gs);

	const std::vector<Quest>& quests() const { return this->all; }
	bool empty() const { return this->all.empty(); }

	// Indices into quests(), in file order, of everything currently visible.
	std::vector<int> active(QuestKind kind) const;
	std::vector<int> completed() const;

	// The quest shown on the HUD: the player's pick (GameState::tracked_quest)
	// while it is still active, otherwise the first active main quest, other-
	// wise the first active side quest. -1 when there is nothing to do.
	int tracked(const GameState& gs) const;
	const Quest* tracked_quest(const GameState& gs) const;
	// The step of that quest the player is on, or nullptr.
	const QuestStep* tracked_step(const GameState& gs) const;

	int find(const std::string& id) const;

	// Exposed for tests and for the menu's own condition-free use.
	static bool eval_condition(const std::string& expr, const GameState& gs);

private:
	std::vector<Quest> all;
};
