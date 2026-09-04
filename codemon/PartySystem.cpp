#include "PartySystem.h"
#include <algorithm>

// ---------------------------------------------------------------- messages --

const char* party_result_text(PartyResult r) {
	switch (r) {
	case PartyResult::OK:                 return "";
	case PartyResult::PARTY_FULL:         return "Dein Team ist voll.";
	case PartyResult::PARTY_EMPTY:        return "Du hast keine POKéMON.";
	case PartyResult::BOX_FULL:           return "Die BOX ist voll.";
	case PartyResult::INVALID_SLOT:       return "Dieser Platz ist leer.";
	case PartyResult::INVALID_ID:         return "Dieses POKéMON gibt es nicht.";
	case PartyResult::ALREADY_IN_PARTY:   return "Es ist schon in deinem Team.";
	case PartyResult::ALREADY_IN_BOX:     return "Es liegt schon in der BOX.";
	case PartyResult::LAST_POKEMON:       return "Das ist dein letztes POKéMON!";
	case PartyResult::LAST_ABLE_POKEMON:  return "Du brauchst ein kampffähiges POKéMON!";
	case PartyResult::SAME_SLOT:          return "Das ist derselbe Platz.";
	case PartyResult::NOTHING_TO_DO:      return "Es passiert nichts.";
	case PartyResult::IS_FAINTED:         return "Es ist kampfunfähig.";
	case PartyResult::NOT_FAINTED:        return "Es ist nicht kampfunfähig.";
	case PartyResult::ITEM_MISSING:       return "Du hast dieses Item nicht.";
	case PartyResult::ITEM_NOT_HOLDABLE:  return "Dieses Item kann nicht getragen werden.";
	case PartyResult::NO_HELD_ITEM:       return "Es trägt kein Item.";
	case PartyResult::NO_MOVE:            return "Diese Attacke gibt es nicht.";
	case PartyResult::ALREADY_KNOWS_MOVE: return "Es kennt diese Attacke bereits.";
	case PartyResult::NO_PENDING_REQUEST: return "Es wartet keine Attacke.";
	case PartyResult::NOT_CONFIGURED:     return "Es passiert nichts.";
	}
	return "";
}

const char* party_event_name(PartyEvent e) {
	switch (e) {
	case PartyEvent::PartyChanged:               return "PartyChanged";
	case PartyEvent::PartyOrderChanged:          return "PartyOrderChanged";
	case PartyEvent::PokemonAddedToParty:        return "PokemonAddedToParty";
	case PartyEvent::PokemonRemovedFromParty:    return "PokemonRemovedFromParty";
	case PartyEvent::PokemonUpdated:             return "PokemonUpdated";
	case PartyEvent::PokemonHealed:              return "PokemonHealed";
	case PartyEvent::PokemonFainted:             return "PokemonFainted";
	case PartyEvent::PokemonLevelUp:             return "PokemonLevelUp";
	case PartyEvent::PokemonLearnedMove:         return "PokemonLearnedMove";
	case PartyEvent::PokemonEvolutionStarted:    return "PokemonEvolutionStarted";
	case PartyEvent::PokemonEvolutionCompleted:  return "PokemonEvolutionCompleted";
	case PartyEvent::HeldItemChanged:            return "HeldItemChanged";
	case PartyEvent::ActivePokemonChanged:       return "ActivePokemonChanged";
	case PartyEvent::BoxChanged:                 return "BoxChanged";
	}
	return "?";
}

// ------------------------------------------------------------------- setup --

PartySystem::PartySystem() {}

void PartySystem::configure(BattleData* bd, GameState* g) {
	this->bdata = bd;
	this->gs = g;
}

void PartySystem::adopt(Mon& mon) {
	if (mon.uid == 0) mon.uid = this->uid_counter++;
	else if (mon.uid >= this->uid_counter) this->uid_counter = mon.uid + 1;
}

void PartySystem::adopt_all() {
	for (Mon& m : this->party) adopt(m);
	for (Mon& m : this->boxed) adopt(m);
	resnap();
}

// ------------------------------------------------------------------ events --

void PartySystem::emit(PartyEvent e, int slot, unsigned uid, const std::string& detail) {
	this->rev++;
	PartyNotice n; n.event = e; n.slot = slot; n.uid = uid; n.detail = detail;
	// Copy first: a listener is allowed to subscribe/unsubscribe from inside
	// its own callback (the party screen drops its subscription when it
	// closes), which would otherwise invalidate the iterator mid-notify.
	std::vector<Listening> snapshot = this->listeners;
	for (const Listening& l : snapshot) if (l.fn) l.fn(n);
}

int PartySystem::subscribe(Listener l) {
	Listening entry; entry.token = this->next_token++; entry.fn = std::move(l);
	this->listeners.push_back(entry);
	return entry.token;
}

void PartySystem::unsubscribe(int token) {
	this->listeners.erase(
		std::remove_if(this->listeners.begin(), this->listeners.end(),
		               [token](const Listening& l) { return l.token == token; }),
		this->listeners.end());
}

unsigned PartySystem::slot_revision(int slot) const {
	if (slot < 0 || slot >= (int)this->shots.size()) return 0;
	return this->shots[slot].rev;
}

void PartySystem::bump(int slot) {
	if (slot >= 0 && slot < (int)this->shots.size()) this->shots[slot].rev++;
}

PartySystem::Snapshot PartySystem::snap_of(const Mon& m, unsigned slot_rev) const {
	Snapshot s;
	s.uid = m.uid; s.species = m.species; s.held_item = m.held_item;
	s.level = m.level; s.hp = m.hp; s.max_hp = m.max_hp;
	s.moves = (int)m.moves.size(); s.status = m.status; s.rev = slot_rev;
	return s;
}

void PartySystem::resnap() {
	std::vector<Snapshot> next;
	next.reserve(this->party.size());
	for (size_t i = 0; i < this->party.size(); ++i) {
		unsigned prev = i < this->shots.size() ? this->shots[i].rev : 1u;
		next.push_back(snap_of(this->party[i], prev + 1));
	}
	this->shots.swap(next);
	this->box_shot = (int)this->boxed.size();
}

void PartySystem::ensure_shots() {
	while (this->shots.size() < this->party.size())
		this->shots.push_back(snap_of(this->party[this->shots.size()], 1));
	if (this->shots.size() > this->party.size()) this->shots.resize(this->party.size());
}

void PartySystem::sync() {
	// Membership changed underneath us (a script pushed onto the raw store).
	if (this->party.size() != this->shots.size()) {
		for (Mon& m : this->party) adopt(m);
		bool grew = this->party.size() > this->shots.size();
		resnap();
		emit(grew ? PartyEvent::PokemonAddedToParty : PartyEvent::PokemonRemovedFromParty,
		     -1, 0);
		emit(PartyEvent::PartyChanged, -1, 0);
		if (this->active >= (int)this->party.size())
			this->active = this->party.empty() ? 0 : (int)this->party.size() - 1;
		if (this->companion >= (int)this->party.size()) this->companion = -1;
	} else {
		for (int i = 0; i < (int)this->party.size(); ++i) {
			Mon& m = this->party[i];
			adopt(m);
			const Snapshot& s = this->shots[i];
			bool identity = s.uid != m.uid;
			bool evolved = s.species != m.species;
			bool levelled = s.level > 0 && m.level > s.level;
			bool fainted_now = s.hp > 0 && m.hp <= 0;
			bool healed = m.hp > s.hp || (s.status != Status::NONE && m.status == Status::NONE);
			bool item = s.held_item != m.held_item;
			bool changed = identity || evolved || levelled || item ||
			               s.hp != m.hp || s.max_hp != m.max_hp ||
			               s.status != m.status || s.moves != (int)m.moves.size();
			if (!changed) continue;
			this->shots[i] = snap_of(m, s.rev + 1);
			if (evolved) {
				emit(PartyEvent::PokemonEvolutionCompleted, i, m.uid, m.species);
				if (this->gs) this->gs->mark_caught(m.species);
			}
			if (levelled) emit(PartyEvent::PokemonLevelUp, i, m.uid, std::to_string(m.level));
			if (item) emit(PartyEvent::HeldItemChanged, i, m.uid, m.held_item);
			emit(PartyEvent::PokemonUpdated, i, m.uid);
			if (fainted_now) emit(PartyEvent::PokemonFainted, i, m.uid);
			else if (healed) emit(PartyEvent::PokemonHealed, i, m.uid);
		}
	}
	if ((int)this->boxed.size() != this->box_shot) {
		for (Mon& m : this->boxed) adopt(m);
		this->box_shot = (int)this->boxed.size();
		emit(PartyEvent::BoxChanged, -1, 0);
	}
}

void PartySystem::touch(int slot) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return;
	Mon& m = this->party[slot];
	adopt(m);
	const Snapshot before = this->shots[slot];
	this->shots[slot] = snap_of(m, before.rev + 1);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	if (before.hp > 0 && m.hp <= 0) emit(PartyEvent::PokemonFainted, slot, m.uid);
}

// ------------------------------------------------------------------ access --

const Mon* PartySystem::at(int slot) const {
	if (slot < 0 || slot >= (int)this->party.size()) return nullptr;
	return &this->party[slot];
}

const Mon* PartySystem::box_at(int index) const {
	if (index < 0 || index >= (int)this->boxed.size()) return nullptr;
	return &this->boxed[index];
}

int PartySystem::slot_of(unsigned uid) const {
	if (uid == 0) return -1;
	for (int i = 0; i < (int)this->party.size(); ++i)
		if (this->party[i].uid == uid) return i;
	return -1;
}

int PartySystem::box_index_of(unsigned uid) const {
	if (uid == 0) return -1;
	for (int i = 0; i < (int)this->boxed.size(); ++i)
		if (this->boxed[i].uid == uid) return i;
	return -1;
}

const Mon* PartySystem::find(unsigned uid) const {
	int s = slot_of(uid);
	if (s >= 0) return &this->party[s];
	int b = box_index_of(uid);
	if (b >= 0) return &this->boxed[b];
	return nullptr;
}

int PartySystem::first_able_slot() const {
	for (int i = 0; i < (int)this->party.size(); ++i)
		if (!this->party[i].fainted()) return i;
	return -1;
}

bool PartySystem::knows_move(const std::string& move) const {
	for (const Mon& m : this->party)
		if (std::find(m.moves.begin(), m.moves.end(), move) != m.moves.end()) return true;
	return false;
}

PartyResult PartySystem::set_active_slot(int slot) {
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (this->active == slot) return PartyResult::OK;
	this->active = slot;
	emit(PartyEvent::ActivePokemonChanged, slot, this->party[slot].uid, "active");
	return PartyResult::OK;
}

PartyResult PartySystem::set_companion_slot(int slot) {
	if (slot < -1 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (this->companion == slot) return PartyResult::OK;
	this->companion = slot;
	emit(PartyEvent::ActivePokemonChanged, slot,
	     slot >= 0 ? this->party[slot].uid : 0u, "companion");
	return PartyResult::OK;
}

// ------------------------------------------------------------- membership --

bool PartySystem::would_strand_party(int slot) const {
	if (slot < 0 || slot >= (int)this->party.size()) return false;
	if (this->party.size() <= 1) return true;                 // last one, always
	if (!this->require_able) return false;
	if (this->party[slot].fainted()) return false;            // takes no ability away
	for (int i = 0; i < (int)this->party.size(); ++i)
		if (i != slot && !this->party[i].fainted()) return false;
	return true;                                              // it was the only able one
}

PartyResult PartySystem::add(const Mon& mon, int* out_slot) {
	ensure_shots();
	if (out_slot) *out_slot = -1;
	if (mon.uid != 0 && slot_of(mon.uid) >= 0) return PartyResult::ALREADY_IN_PARTY;
	if (full()) {
		PartyResult r = add_to_box(mon);
		return r;
	}
	this->party.push_back(mon);
	Mon& m = this->party.back();
	adopt(m);
	int slot = (int)this->party.size() - 1;
	this->shots.push_back(snap_of(m, 1));
	if (out_slot) *out_slot = slot;
	emit(PartyEvent::PokemonAddedToParty, slot, m.uid, m.species);
	emit(PartyEvent::PartyChanged, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::add_to_box(const Mon& mon) {
	if (box_full()) return PartyResult::BOX_FULL;
	if (mon.uid != 0 && box_index_of(mon.uid) >= 0) return PartyResult::ALREADY_IN_BOX;
	this->boxed.push_back(mon);
	adopt(this->boxed.back());
	this->box_shot = (int)this->boxed.size();
	emit(PartyEvent::BoxChanged, -1, this->boxed.back().uid, this->boxed.back().species);
	return PartyResult::OK;
}

PartyResult PartySystem::swap_slots(int a, int b) {
	ensure_shots();
	if (a < 0 || b < 0 || a >= (int)this->party.size() || b >= (int)this->party.size())
		return PartyResult::INVALID_SLOT;
	if (a == b) return PartyResult::SAME_SLOT;
	std::swap(this->party[a], this->party[b]);
	// The rows swapped, so both need redrawing -- but the party's membership
	// did not change, which is why this is an order event, not a change of
	// who is in the team.
	std::swap(this->shots[a], this->shots[b]);
	bump(a); bump(b);
	// The lead and the companion follow the mon they pointed at, not the slot
	// number: swapping slots 1 and 2 must not silently change which mon leads.
	auto follow = [&](int& idx) { if (idx == a) idx = b; else if (idx == b) idx = a; };
	follow(this->active); follow(this->companion);
	emit(PartyEvent::PartyOrderChanged, a, this->party[a].uid);
	emit(PartyEvent::PartyChanged, b, this->party[b].uid);
	return PartyResult::OK;
}

void PartySystem::reindex_after_removal(int removed_slot) {
	// The vector already closed the gap; keep active/companion on the same
	// logical position and never let them dangle (§15).
	if (this->active > removed_slot) this->active--;
	if (this->active >= (int)this->party.size())
		this->active = this->party.empty() ? 0 : (int)this->party.size() - 1;
	if (this->companion == removed_slot) this->companion = -1;
	else if (this->companion > removed_slot) this->companion--;
	if (this->companion >= (int)this->party.size()) this->companion = -1;
}

PartyResult PartySystem::move_to_box(int slot) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (box_full()) return PartyResult::BOX_FULL;
	if (would_strand_party(slot))
		return this->party.size() <= 1 ? PartyResult::LAST_POKEMON
		                               : PartyResult::LAST_ABLE_POKEMON;
	Mon moving = this->party[slot];
	unsigned uid = moving.uid;
	std::string species = moving.species;
	this->party.erase(this->party.begin() + slot);
	this->shots.erase(this->shots.begin() + slot);
	reindex_after_removal(slot);
	this->boxed.push_back(moving);        // the same object, not a copy of a copy
	this->box_shot = (int)this->boxed.size();
	// Everything below the hole moved up a slot, so its rows are stale too.
	for (int i = slot; i < (int)this->shots.size(); ++i) bump(i);
	emit(PartyEvent::PokemonRemovedFromParty, slot, uid, species);
	emit(PartyEvent::BoxChanged, -1, uid, species);
	emit(PartyEvent::PartyChanged, -1, uid);
	return PartyResult::OK;
}

PartyResult PartySystem::withdraw_from_box(int index, int* out_slot) {
	ensure_shots();
	if (out_slot) *out_slot = -1;
	if (index < 0 || index >= (int)this->boxed.size()) return PartyResult::INVALID_SLOT;
	if (full()) return PartyResult::PARTY_FULL;
	Mon moving = this->boxed[index];
	this->boxed.erase(this->boxed.begin() + index);
	this->box_shot = (int)this->boxed.size();
	this->party.push_back(moving);
	adopt(this->party.back());
	int slot = (int)this->party.size() - 1;
	this->shots.push_back(snap_of(this->party.back(), 1));
	if (out_slot) *out_slot = slot;
	emit(PartyEvent::PokemonAddedToParty, slot, this->party.back().uid,
	     this->party.back().species);
	emit(PartyEvent::BoxChanged, -1, this->party.back().uid);
	emit(PartyEvent::PartyChanged, slot, this->party.back().uid);
	return PartyResult::OK;
}

PartyResult PartySystem::release(int slot) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (would_strand_party(slot))
		return this->party.size() <= 1 ? PartyResult::LAST_POKEMON
		                               : PartyResult::LAST_ABLE_POKEMON;
	unsigned uid = this->party[slot].uid;
	std::string species = this->party[slot].species;
	this->party.erase(this->party.begin() + slot);
	this->shots.erase(this->shots.begin() + slot);
	reindex_after_removal(slot);
	for (int i = slot; i < (int)this->shots.size(); ++i) bump(i);
	emit(PartyEvent::PokemonRemovedFromParty, slot, uid, species);
	emit(PartyEvent::PartyChanged, -1, uid);
	return PartyResult::OK;
}

// ------------------------------------------------------------- held items --

bool PartySystem::is_holdable(const std::string& item) {
	if (item.empty() || item == "NONE") return false;
	if (item.rfind("ITEM_TM", 0) == 0 || item.rfind("ITEM_HM", 0) == 0) return false;
	// Balls are thrown, never held; the same goes for the story key items and
	// the bike/rods, which the real games keep in a separate pocket entirely.
	if (item.size() > 5 && item.rfind("_BALL") == item.size() - 5) return false;
	static const char* KEY_ITEMS[] = {
		"ITEM_BICYCLE", "ITEM_MACH_BIKE", "ITEM_ACRO_BIKE", "ITEM_OLD_ROD",
		"ITEM_GOOD_ROD", "ITEM_SUPER_ROD", "ITEM_POKEDEX", "ITEM_POKENAV",
		"ITEM_ITEMFINDER", "ITEM_DEVON_SCOPE", "ITEM_GO_GOGGLES",
		"ITEM_SS_TICKET", "ITEM_EON_TICKET", "ITEM_LETTER", "ITEM_DEVON_GOODS",
		"ITEM_BASEMENT_KEY", "ITEM_STORAGE_KEY", "ITEM_ROOM_1_KEY",
		"ITEM_ROOM_2_KEY", "ITEM_ROOM_4_KEY", "ITEM_ROOM_6_KEY",
		"ITEM_SCANNER", "ITEM_METEORITE", "ITEM_MAGMA_EMBLEM",
		"ITEM_OLD_SEA_MAP", "ITEM_CONTEST_PASS", "ITEM_WAILMER_PAIL",
		"ITEM_COIN_CASE", "ITEM_POWDER_JAR",
	};
	for (const char* k : KEY_ITEMS) if (item == k) return false;
	return true;
}

void PartySystem::stamp_origin(Mon& m, const GameState& gs, const std::string& ball,
                               const std::string& location, int friendship) {
	m.ot_name = gs.player_name;
	m.ot_id = gs.trainer_id;
	m.ot_secret = gs.secret_id;
	m.ball = ball.empty() ? std::string("NONE") : ball;
	m.met_location = location;
	m.met_level = m.level;
	m.friendship = friendship;
}

PartyResult PartySystem::give_held_item(int slot, const std::string& item) {
	ensure_shots();
	if (!this->gs) return PartyResult::NOT_CONFIGURED;
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (!is_holdable(item)) return PartyResult::ITEM_NOT_HOLDABLE;
	if (this->gs->item_count(item) <= 0) return PartyResult::ITEM_MISSING;
	Mon& m = this->party[slot];
	std::string previous = m.held_item;
	this->gs->take_item(item, 1);
	// Swapping an item: the old one goes back to the bag in the same step, so
	// the transfer can never lose or duplicate an item (§11).
	if (!previous.empty() && previous != "NONE") this->gs->give_item(previous, 1);
	m.held_item = item;
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::HeldItemChanged, slot, m.uid, item);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::take_held_item(int slot) {
	ensure_shots();
	if (!this->gs) return PartyResult::NOT_CONFIGURED;
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	if (m.held_item.empty() || m.held_item == "NONE") return PartyResult::NO_HELD_ITEM;
	std::string item = m.held_item;
	m.held_item = "NONE";
	this->gs->give_item(item, 1);
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::HeldItemChanged, slot, m.uid, "NONE");
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

// ---------------------------------------------------------------- healing --

PartyResult PartySystem::heal_hp(int slot, int amount) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	if (m.fainted()) return PartyResult::IS_FAINTED;      // needs a Revive first
	if (m.hp >= m.max_hp) return PartyResult::NOTHING_TO_DO;
	m.hp = amount <= 0 ? m.max_hp : std::min(m.max_hp, m.hp + amount);
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonHealed, slot, m.uid);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::cure_status(int slot) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	if (m.status == Status::NONE && m.confusion_turns == 0) return PartyResult::NOTHING_TO_DO;
	m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonHealed, slot, m.uid);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::heal_full(int slot) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	bool anything = m.hp < m.max_hp || m.status != Status::NONE || m.confusion_turns != 0;
	m.hp = m.max_hp;
	m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
	if (this->bdata) {
		for (size_t i = 0; i < m.moves.size(); ++i) {
			const MoveInfo* mi = this->bdata->move(m.moves[i]);
			int max_pp = mi ? mi->pp : 20;
			if (i < m.pp.size() && m.pp[i] < max_pp) anything = true;
		}
		this->bdata->restore_pp(m);
	}
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonHealed, slot, m.uid);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return anything ? PartyResult::OK : PartyResult::NOTHING_TO_DO;
}

PartyResult PartySystem::heal_all() {
	if (this->party.empty()) return PartyResult::PARTY_EMPTY;
	for (int i = 0; i < (int)this->party.size(); ++i) heal_full(i);
	return PartyResult::OK;
}

PartyResult PartySystem::revive(int slot, bool full_hp) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	if (!m.fainted()) return PartyResult::NOT_FAINTED;
	m.hp = full_hp ? m.max_hp : std::max(1, m.max_hp / 2);
	m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonHealed, slot, m.uid);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::apply_damage(int slot, int amount) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (amount <= 0) return PartyResult::NOTHING_TO_DO;
	Mon& m = this->party[slot];
	if (m.fainted()) return PartyResult::IS_FAINTED;
	m.hp = std::max(0, m.hp - amount);
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	// A fainted mon stays in the party -- it is never auto-removed (§17).
	if (m.fainted()) emit(PartyEvent::PokemonFainted, slot, m.uid);
	return PartyResult::OK;
}

// ------------------------------------------------------------ progression --

PartyResult PartySystem::grant_exp(int slot, long amount, std::vector<std::string>& msgs) {
	ensure_shots();
	if (!this->bdata) return PartyResult::NOT_CONFIGURED;
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	if (m.fainted()) return PartyResult::IS_FAINTED;
	if (amount <= 0) return PartyResult::NOTHING_TO_DO;

	std::string before_species = m.species;
	BattleData::LevelUpReport rep;
	this->bdata->grant_exp(m, amount, msgs, &rep);

	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	if (rep.levels_gained > 0)
		emit(PartyEvent::PokemonLevelUp, slot, m.uid, std::to_string(m.level));
	for (const std::string& mv : rep.learned)
		emit(PartyEvent::PokemonLearnedMove, slot, m.uid, mv);
	if (!rep.evolved_to.empty()) {
		emit(PartyEvent::PokemonEvolutionStarted, slot, m.uid, before_species);
		if (this->gs) { this->gs->mark_seen(m.species); this->gs->mark_caught(m.species); }
		emit(PartyEvent::PokemonEvolutionCompleted, slot, m.uid, rep.evolved_to);
	}
	for (const std::string& mv : rep.pending_moves) {
		MoveLearnRequest req;
		req.uid = m.uid; req.slot = slot; req.move = mv; req.species = m.species;
		this->pending_learns.push_back(req);
	}
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::learn_move(int slot, const std::string& move, int replace_index) {
	ensure_shots();
	if (!this->bdata) return PartyResult::NOT_CONFIGURED;
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	const MoveInfo* mi = this->bdata->move(move);
	if (!mi) return PartyResult::NO_MOVE;
	Mon& m = this->party[slot];
	if (std::find(m.moves.begin(), m.moves.end(), move) != m.moves.end())
		return PartyResult::ALREADY_KNOWS_MOVE;
	if (replace_index >= 0) {
		if (replace_index >= (int)m.moves.size()) return PartyResult::NO_MOVE;
		m.moves[replace_index] = move;
		if ((int)m.pp.size() > replace_index) m.pp[replace_index] = mi->pp;
		else m.pp.push_back(mi->pp);
	} else {
		if (m.moves.size() >= 4) return PartyResult::NO_MOVE;
		m.moves.push_back(move);
		m.pp.push_back(mi->pp);
	}
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonLearnedMove, slot, m.uid, move);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::queue_move_learn(int slot, const std::string& move) {
	if (!this->bdata) return PartyResult::NOT_CONFIGURED;
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (!this->bdata->move(move)) return PartyResult::NO_MOVE;
	Mon& m = this->party[slot];
	if (std::find(m.moves.begin(), m.moves.end(), move) != m.moves.end())
		return PartyResult::ALREADY_KNOWS_MOVE;
	MoveLearnRequest req;
	req.uid = m.uid; req.slot = slot; req.move = move; req.species = m.species;
	this->pending_learns.push_back(req);
	return PartyResult::OK;
}

const MoveLearnRequest* PartySystem::pending_move_learn() const {
	return this->pending_learns.empty() ? nullptr : &this->pending_learns.front();
}

PartyResult PartySystem::resolve_move_learn(int replace_index) {
	if (this->pending_learns.empty()) return PartyResult::NO_PENDING_REQUEST;
	MoveLearnRequest req = this->pending_learns.front();
	this->pending_learns.erase(this->pending_learns.begin());
	if (replace_index < 0) return PartyResult::OK;      // declined, nothing to do
	// The mon may have moved slots (or into a box) between the prompt being
	// queued and the player answering it -- go by uid, not by the old slot.
	int slot = slot_of(req.uid);
	if (slot < 0) return PartyResult::INVALID_ID;
	return learn_move(slot, req.move, replace_index);
}

PartyResult PartySystem::add_ev(int slot, char stat, int amount) {
	ensure_shots();
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (amount == 0) return PartyResult::NOTHING_TO_DO;
	Mon& m = this->party[slot];
	int* target = nullptr;
	switch (stat) {
	case 'H': target = &m.ev_hp;  break;
	case 'A': target = &m.ev_atk; break;
	case 'D': target = &m.ev_def; break;
	case 'S': target = &m.ev_spa; break;
	case 'F': target = &m.ev_spd; break;
	case 'E': target = &m.ev_spe; break;
	default: return PartyResult::NOTHING_TO_DO;
	}
	// Real Gen-3 caps: 255 in one stat, 510 across all six.
	int room_stat = 255 - *target;
	int room_total = 510 - m.ev_total();
	int grant = std::min(amount, std::min(room_stat, room_total));
	if (grant <= 0) return PartyResult::NOTHING_TO_DO;
	*target += grant;
	if (this->bdata) this->bdata->recompute_stats(m, true);
	bump(slot);
	this->shots[slot] = snap_of(m, this->shots[slot].rev);
	emit(PartyEvent::PokemonUpdated, slot, m.uid);
	return PartyResult::OK;
}

PartyResult PartySystem::add_ribbon(int slot, const std::string& ribbon) {
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	if (ribbon.empty()) return PartyResult::NOTHING_TO_DO;
	Mon& m = this->party[slot];
	if (std::find(m.ribbons.begin(), m.ribbons.end(), ribbon) != m.ribbons.end())
		return PartyResult::NOTHING_TO_DO;
	m.ribbons.push_back(ribbon);
	bump(slot);
	emit(PartyEvent::PokemonUpdated, slot, m.uid, ribbon);
	return PartyResult::OK;
}

PartyResult PartySystem::set_nickname(int slot, const std::string& nickname) {
	if (slot < 0 || slot >= (int)this->party.size()) return PartyResult::INVALID_SLOT;
	Mon& m = this->party[slot];
	if (m.nickname == nickname) return PartyResult::NOTHING_TO_DO;
	m.nickname = nickname;
	bump(slot);
	emit(PartyEvent::PokemonUpdated, slot, m.uid, nickname);
	return PartyResult::OK;
}

// --------------------------------------------------------------- save/load --

void PartySystem::reset(std::vector<Mon> new_party, std::vector<Mon> new_box) {
	if ((int)new_party.size() > MAX_SLOTS) new_party.resize(MAX_SLOTS);
	if ((int)new_box.size() > BOX_CAPACITY) new_box.resize(BOX_CAPACITY);
	this->party = std::move(new_party);
	this->boxed = std::move(new_box);
	this->pending_learns.clear();
	this->active = 0;
	this->companion = -1;
	this->shots.clear();
	for (Mon& m : this->party) adopt(m);
	for (Mon& m : this->boxed) adopt(m);
	resnap();
	emit(PartyEvent::PartyChanged, -1, 0);
	emit(PartyEvent::BoxChanged, -1, 0);
}

void PartySystem::clear() {
	reset(std::vector<Mon>(), std::vector<Mon>());
	this->uid_counter = 1;
}
