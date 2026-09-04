#pragma once
#include <functional>
#include <string>
#include <vector>
#include "BattleData.h"
#include "GameState.h"

/******************************************************************************
PartySystem - the player's six party slots and the PC boxes, as one gameplay
system rather than a list the party screen happens to own.

Before this existed the party was a bare `std::vector<Mon>` passed around by
pointer, and whichever screen was on top mutated it directly: the party screen
wrote HP, the bag screen wrote held items, `givemon` pushed onto it, and
nothing else in the engine could tell that any of it had happened. That is
exactly the shape the design brief calls out as wrong -- the UI owning game
data -- so all of it moved here:

    game data  ->  PartySystem  ->  event  ->  UI

Every mutation goes through a named operation that validates first and reports
a PartyResult, and every mutation that lands raises PartyNotice events the UI
(and anything else) subscribes to. Screens read; they never write.

Two things it deliberately does NOT hide:

  * party_storage()/box_storage() hand out the raw vectors, because Battle and
    ScriptVM predate this class and address the party as `std::vector<Mon>*`.
    Those paths write straight into a Mon (a hit lands, a script heals), so
    after driving them the owner calls sync(), which diffs against the last
    snapshot and raises the events those writes should have raised.
  * Mon objects are values, not handles. Moving one between the party and a box
    moves the object; Mon::uid is what keeps its identity stable across the
    move, so a mon is never two independent pokemon (design brief §22).
*****************************************************************************/

// Why an operation was refused. Every mutating call returns one of these
// instead of asserting or silently doing nothing -- the failure cases in the
// brief (§31) are all normal player actions, not programming errors.
enum class PartyResult {
	OK,
	PARTY_FULL,           // six already
	PARTY_EMPTY,          // nothing to act on
	BOX_FULL,
	INVALID_SLOT,         // slot out of range, or an empty slot
	INVALID_ID,           // no mon with that uid / unknown species
	ALREADY_IN_PARTY,
	ALREADY_IN_BOX,
	LAST_POKEMON,         // would leave the party with nothing in it
	LAST_ABLE_POKEMON,    // would leave the party with only fainted members
	SAME_SLOT,            // "swap" onto itself
	NOTHING_TO_DO,        // already at full HP, no status to cure, ...
	IS_FAINTED,           // a fainted mon can't be healed by a plain potion
	NOT_FAINTED,          // ... and a healthy one can't be revived
	ITEM_MISSING,         // not in the bag
	ITEM_NOT_HOLDABLE,    // a TM/HM, a key item, a Poke Ball, ...
	NO_HELD_ITEM,         // "take item" with nothing held
	NO_MOVE,              // unknown move / no such move slot
	ALREADY_KNOWS_MOVE,
	NO_PENDING_REQUEST,   // resolve_move_learn() with no prompt open
	NOT_CONFIGURED,       // no BattleData/GameState wired up
};

// A short German line for the player, e.g. for the party screen's status row.
const char* party_result_text(PartyResult r);
inline bool party_ok(PartyResult r) { return r == PartyResult::OK; }

// What happened. Subscribers get one of these per change; a single operation
// can raise several (moving a mon to a box raises PokemonRemovedFromParty and
// PartyChanged, and PartyOrderChanged if the slots below it shifted up).
enum class PartyEvent {
	PartyChanged,               // membership or ordering changed at all
	PartyOrderChanged,          // the same members, in a different order
	PokemonAddedToParty,
	PokemonRemovedFromParty,
	PokemonUpdated,             // HP/status/stats/moves of one member changed
	PokemonHealed,
	PokemonFainted,
	PokemonLevelUp,
	PokemonLearnedMove,
	PokemonEvolutionStarted,
	PokemonEvolutionCompleted,
	HeldItemChanged,
	ActivePokemonChanged,       // lead / companion slot changed
	BoxChanged,                 // PC storage membership changed
};
const char* party_event_name(PartyEvent e);   // for logs and tests

struct PartyNotice {
	PartyEvent event;
	int slot = -1;            // party slot the event is about, -1 if none
	unsigned uid = 0;         // Mon::uid involved, 0 if none
	std::string detail;       // move / item / species, depending on the event
};

// A level-up move that did not fit: the real games' "welche Attacke soll
// vergessen werden?" prompt. Queued by grant_exp() and answered with
// resolve_move_learn().
struct MoveLearnRequest {
	unsigned uid = 0;
	int slot = -1;
	std::string move;
	std::string species;      // for the prompt's wording
};

class PartySystem
{
public:
	static const int MAX_SLOTS = 6;
	// PC storage: pokeemerald's 14 boxes of 30. Stored flat -- nothing in this
	// engine paginates the PC yet -- but capacity is enforced so "box full"
	// (§31) is a real, testable state rather than unbounded growth.
	static const int BOX_COUNT = 14;
	static const int BOX_SIZE = 30;
	static const int BOX_CAPACITY = BOX_COUNT * BOX_SIZE;

	PartySystem();

	// BattleData is needed for stat/EXP/move maths, GameState for the bag
	// (held items) and the Pokedex (evolution). Both may be null; operations
	// that need one return NOT_CONFIGURED rather than crashing.
	void configure(BattleData* bd, GameState* gs);

	// Whether the party is allowed to end up with no able (non-fainted) mon.
	// The real games refuse; the brief (§14) makes it a rule the game may or
	// may not have switched on.
	void set_require_able_member(bool on) { this->require_able = on; }
	bool requires_able_member() const { return this->require_able; }

	// --- reading -----------------------------------------------------------
	int size() const { return (int)this->party.size(); }
	bool empty() const { return this->party.empty(); }
	bool full() const { return (int)this->party.size() >= MAX_SLOTS; }
	int box_size() const { return (int)this->boxed.size(); }
	bool box_full() const { return (int)this->boxed.size() >= BOX_CAPACITY; }
	// The mon in a slot, or nullptr when the slot is empty/out of range. An
	// empty slot is the absence of a Mon, never a placeholder object (§16).
	const Mon* at(int slot) const;
	const Mon* box_at(int index) const;
	int slot_of(unsigned uid) const;        // -1 if not in the party
	int box_index_of(unsigned uid) const;   // -1 if not in a box
	const Mon* find(unsigned uid) const;    // party first, then boxes

	// First slot holding a mon that can still fight, or -1. Battles lead with
	// this rather than blindly with slot 0.
	int first_able_slot() const;
	bool has_able_pokemon() const { return first_able_slot() >= 0; }
	bool knows_move(const std::string& move) const;   // any member, for HM checks

	// Which party slot is the "current" one outside battle (the lead), and
	// which mon walks with the player. Kept apart on purpose (§25): a
	// companion is not automatically the mon a battle starts with.
	int active_slot() const { return this->active; }
	PartyResult set_active_slot(int slot);
	int companion_slot() const { return this->companion; }   // -1 = nobody
	PartyResult set_companion_slot(int slot);                // -1 clears it

	// --- mutating ----------------------------------------------------------
	// Into the party if there is room, otherwise into the PC (the real games'
	// overflow rule). out_slot, when given, receives the party slot or -1 for
	// "went to the box".
	PartyResult add(const Mon& mon, int* out_slot = nullptr);
	PartyResult add_to_box(const Mon& mon);
	// Exchange two party slots -- the brief's "Pokémon wechseln" (§4/§13);
	// nothing to do with switching mid-battle.
	PartyResult swap_slots(int a, int b);
	// Party -> PC. Refuses to leave the party empty (or without an able
	// member, when that rule is on); survivors close the gap (§15).
	PartyResult move_to_box(int slot);
	// PC -> party, by index into the flat box list.
	PartyResult withdraw_from_box(int index, int* out_slot = nullptr);
	// Drop a party member entirely (release). Same last-mon protection.
	PartyResult release(int slot);

	// Held items. Both sides of the bag transfer happen here so the item can
	// never be duplicated: giving takes it from the bag, swapping an existing
	// held item puts the old one back (§10-§12).
	PartyResult give_held_item(int slot, const std::string& item);
	PartyResult take_held_item(int slot);
	// Whether a bag item is something a pokemon may hold at all.
	static bool is_holdable(const std::string& item);

	// Stamp the origin data (§2) onto a mon that joins the player somewhere
	// other than a thrown ball -- a starter, a gift, a script's `givemon`.
	// The caught path does the same thing in Battle::caught_mon(), where the
	// ball and the encounter's level are what is known.
	static void stamp_origin(Mon& m, const GameState& gs, const std::string& ball,
	                         const std::string& location, int friendship = 120);

	// Healing. heal_hp(slot, 0) is a full restore; cure_status clears the
	// major status; revive brings a fainted mon back with half (or full) HP.
	PartyResult heal_hp(int slot, int amount);
	PartyResult cure_status(int slot);
	PartyResult heal_full(int slot);        // HP + status + PP, i.e. a Center
	PartyResult heal_all();                 // every member, i.e. a Center
	PartyResult revive(int slot, bool full_hp);
	// Damage from outside battle (a script, poison while walking). Raises
	// PokemonFainted when it drops the mon to 0.
	PartyResult apply_damage(int slot, int amount);

	// Experience. Level-ups, learned moves and evolutions all raise their own
	// events; a level-up move that does not fit is queued as a prompt rather
	// than overwriting a move (§9).
	PartyResult grant_exp(int slot, long amount, std::vector<std::string>& msgs);
	// Teach a move now. replace_index < 0 means "append" and fails if the mon
	// already knows four.
	PartyResult learn_move(int slot, const std::string& move, int replace_index);
	// Effort values, clamped to the real 255-per-stat / 510-total caps.
	PartyResult add_ev(int slot, char stat, int amount);
	PartyResult add_ribbon(int slot, const std::string& ribbon);
	PartyResult set_nickname(int slot, const std::string& nickname);

	// Queue a "which move should be forgotten?" prompt by hand -- for the
	// TM/HM path, which reaches a full moveset the same way a level-up does
	// and should ask the same question rather than clobbering move slot 0.
	PartyResult queue_move_learn(int slot, const std::string& move);

	// The queued "which move should be forgotten?" prompt, if any.
	bool has_pending_move_learn() const { return !this->pending_learns.empty(); }
	const MoveLearnRequest* pending_move_learn() const;
	// replace_index in 0..3 replaces that move; < 0 declines the new move.
	PartyResult resolve_move_learn(int replace_index);

	// --- legacy interop ----------------------------------------------------
	// The raw backing stores, for Battle/ScriptVM, which take
	// `std::vector<Mon>*`. Anything that writes through these must call
	// sync() afterwards so the events still fire; new code should use the
	// named operations above instead.
	std::vector<Mon>& party_storage() { return this->party; }
	const std::vector<Mon>& party_storage() const { return this->party; }
	std::vector<Mon>& box_storage() { return this->boxed; }
	const std::vector<Mon>& box_storage() const { return this->boxed; }

	// Re-scan both stores and raise events for whatever changed underneath.
	// Cheap (at most six members plus a membership count), meant to be called
	// once a frame by the game loop.
	void sync();
	// Same, for one slot a caller knows it just wrote to.
	void touch(int slot);
	// Adopt any mon that has no uid yet (a fresh save, a mon pushed straight
	// onto the raw storage) without raising add/remove events.
	void adopt_all();

	// --- events ------------------------------------------------------------
	using Listener = std::function<void(const PartyNotice&)>;
	int subscribe(Listener l);       // returns a token for unsubscribe()
	void unsubscribe(int token);
	// Bumped by every change; a screen can compare it to decide whether it
	// needs to rebuild anything at all.
	unsigned revision() const { return this->rev; }
	// Per-slot revision, so a screen redraws only the row that changed (§28).
	unsigned slot_revision(int slot) const;

	// --- save/load ---------------------------------------------------------
	// Replace both stores wholesale (SaveGame). Raises a single PartyChanged
	// rather than a storm of per-mon events, and re-seeds the uid counter
	// above the highest id in the loaded data so new mons stay unique.
	void reset(std::vector<Mon> new_party, std::vector<Mon> new_box);
	void clear();
	unsigned next_uid() const { return this->uid_counter; }
	void set_next_uid(unsigned n) { if (n > this->uid_counter) this->uid_counter = n; }
	// Give `mon` a uid if it has none. Public because SaveGame and the
	// capture path build Mons before handing them over.
	void adopt(Mon& mon);

private:
	std::vector<Mon> party;
	std::vector<Mon> boxed;
	BattleData* bdata = nullptr;
	GameState* gs = nullptr;
	bool require_able = true;
	int active = 0;
	int companion = -1;
	unsigned uid_counter = 1;
	unsigned rev = 1;

	std::vector<MoveLearnRequest> pending_learns;

	struct Listening { int token; Listener fn; };
	std::vector<Listening> listeners;
	int next_token = 1;

	// What each slot looked like the last time events were up to date, so
	// sync() can tell what changed after a raw write.
	struct Snapshot {
		unsigned uid = 0;
		std::string species, held_item;
		int level = 0, hp = 0, max_hp = 0, moves = 0;
		Status status = Status::NONE;
		unsigned rev = 1;
	};
	std::vector<Snapshot> shots;
	int box_shot = 0;

	void emit(PartyEvent e, int slot, unsigned uid, const std::string& detail = std::string());
	void bump(int slot);
	Snapshot snap_of(const Mon& m, unsigned slot_rev) const;
	void resnap();                       // rebuild all snapshots, no events
	// Keep the per-slot snapshot array the same length as the party, for the
	// case where something wrote through party_storage() without sync()ing.
	void ensure_shots();
	// After a removal: drop empty gaps and keep active/companion pointing at
	// the same mon (or the nearest valid slot).
	void reindex_after_removal(int removed_slot);
	bool would_strand_party(int slot) const;   // last-mon / last-able check
};
