#include "Menu.h"
#include <cctype>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

Menu::Menu() : font_ok(false), screen(CLOSED), cursor(0),
               bag_cursor(0), teach_cursor(0), use_cursor(0), fly_cursor(0),
               gs(nullptr), bdata(nullptr), party(nullptr),
               cursor_ok(false) {}

// Fixed Fly destinations (pokeemerald's canFly town/city list, region_map.c's
// GetMapSecType switch): flag -> (map file stem, arrival tile), the same
// tile each town's own heal location uses (src/data/heal_locations.json),
// since flying lands you at the same spot recovering from a whiteout would.
struct FlyDest { const char* flag; const char* map; const char* name; int x, y; };
static const FlyDest FLY_DESTINATIONS[] = {
	{"FLAG_VISITED_LITTLEROOT_TOWN", "LittlerootTown",  "Littleroot Town",  5,  9},
	{"FLAG_VISITED_OLDALE_TOWN",     "OldaleTown",      "Oldale Town",       6, 17},
	{"FLAG_VISITED_DEWFORD_TOWN",    "DewfordTown",     "Dewford Town",      2, 11},
	{"FLAG_VISITED_LAVARIDGE_TOWN",  "LavaridgeTown",   "Lavaridge Town",    9,  7},
	{"FLAG_VISITED_FALLARBOR_TOWN",  "FallarborTown",   "Fallarbor Town",   14,  8},
	{"FLAG_VISITED_VERDANTURF_TOWN", "VerdanturfTown",  "Verdanturf Town",  16,  4},
	{"FLAG_VISITED_PACIFIDLOG_TOWN", "PacifidlogTown",  "Pacifidlog Town",   8, 16},
	{"FLAG_VISITED_PETALBURG_CITY",  "PetalburgCity",   "Petalburg City",  20, 17},
	{"FLAG_VISITED_SLATEPORT_CITY",  "SlateportCity",   "Slateport City",  19, 20},
	{"FLAG_VISITED_MAUVILLE_CITY",   "MauvilleCity",    "Mauville City",   22,  6},
	{"FLAG_VISITED_RUSTBORO_CITY",   "RustboroCity",    "Rustboro City",   16, 39},
	{"FLAG_VISITED_FORTREE_CITY",    "FortreeCity",     "Fortree City",     5,  7},
	{"FLAG_VISITED_LILYCOVE_CITY",   "LilycoveCity",    "Lilycove City",   24, 15},
	{"FLAG_VISITED_MOSSDEEP_CITY",   "MossdeepCity",    "Mossdeep City",   28, 17},
	{"FLAG_VISITED_SOOTOPOLIS_CITY", "SootopolisCity",  "Sootopolis City", 43, 32},
	{"FLAG_VISITED_EVER_GRANDE_CITY","EverGrandeCity",  "Ever Grande City",27, 49},
};
static const int FLY_DESTINATIONS_N = sizeof(FLY_DESTINATIONS) / sizeof(FLY_DESTINATIONS[0]);

void Menu::configure(GameState* g, PartySystem* p, BattleData* bd) {
	this->gs = g; this->bdata = bd;
	if (this->party && this->party_token >= 0) {
		this->party->unsubscribe(this->party_token);
		this->party_token = -1;
	}
	this->party = p;
	if (this->party)
		this->party_token = this->party->subscribe(
			[this](const PartyNotice& n) { this->on_party_event(n); });
	for (int i = 0; i < PartySystem::MAX_SLOTS; ++i) this->slot_dirty[i] = true;
}

// The whole point of the event plumbing: an event marks the affected row (or
// every row, for a change of who is in the party at all) for rebuilding, and
// nothing else is touched. A mon taking 10 damage costs one row (§28).
void Menu::on_party_event(const PartyNotice& n) {
	bool all = n.slot < 0 ||
	           n.event == PartyEvent::PartyChanged ||
	           n.event == PartyEvent::PartyOrderChanged ||
	           n.event == PartyEvent::PokemonAddedToParty ||
	           n.event == PartyEvent::PokemonRemovedFromParty;
	if (all) {
		for (int i = 0; i < PartySystem::MAX_SLOTS; ++i) this->slot_dirty[i] = true;
	} else if (n.slot < PartySystem::MAX_SLOTS) {
		this->slot_dirty[n.slot] = true;
	}
}

std::vector<std::pair<std::string, int>> Menu::bag_sorted() const {
	std::vector<std::pair<std::string, int>> v;
	if (this->gs)
		for (const auto& kv : this->gs->bag_items()) v.push_back(kv);
	std::sort(v.begin(), v.end(),
	          [](const auto& a, const auto& b) { return a.first < b.first; });
	return v;
}

// Is this bag item a TM or HM? (ITEM_TM_FOCUS_PUNCH / ITEM_HM_CUT)
static bool is_machine(const std::string& item) {
	return item.rfind("ITEM_TM", 0) == 0 || item.rfind("ITEM_HM", 0) == 0;
}
static bool is_hm(const std::string& item) { return item.rfind("ITEM_HM", 0) == 0; }

// HP restored by a healing item; 0 means a full heal. -1 = not a healing item.
static int heal_amount(const std::string& item) {
	static const std::map<std::string, int> tbl = {
		{"ITEM_POTION", 20}, {"ITEM_SUPER_POTION", 50}, {"ITEM_HYPER_POTION", 200},
		{"ITEM_MAX_POTION", 0}, {"ITEM_FULL_RESTORE", 0},
		{"ITEM_FRESH_WATER", 50}, {"ITEM_SODA_POP", 60}, {"ITEM_LEMONADE", 80},
		{"ITEM_MOOMOO_MILK", 100}, {"ITEM_BERRY_JUICE", 20},
		{"ITEM_ORAN_BERRY", 10}, {"ITEM_SITRUS_BERRY", 30},
	};
	auto it = tbl.find(item);
	return it == tbl.end() ? -1 : it->second;
}
static bool is_revive_item(const std::string& item) {
	return item == "ITEM_REVIVE" || item == "ITEM_MAX_REVIVE" || item == "ITEM_REVIVAL_HERB";
}
// The single status a dedicated status healer cures; NONE if `item` isn't one.
static Status status_cured(const std::string& item) {
	if (item == "ITEM_ANTIDOTE") return Status::POISON;   // also cures TOXIC, see below
	if (item == "ITEM_PARALYZE_HEAL") return Status::PARALYSIS;
	if (item == "ITEM_AWAKENING") return Status::SLEEP;
	if (item == "ITEM_BURN_HEAL") return Status::BURN;
	if (item == "ITEM_ICE_HEAL") return Status::FREEZE;
	return Status::NONE;
}
// Full Heal cures any status; Full Restore does too, on top of a full heal.
static bool cures_any_status(const std::string& item) {
	return item == "ITEM_FULL_HEAL" || item == "ITEM_FULL_RESTORE";
}
static bool has_curable_status(const std::string& item, const Mon& m) {
	if (cures_any_status(item)) return m.status != Status::NONE;
	Status cures = status_cured(item);
	if (cures == Status::NONE) return false;
	return m.status == cures || (cures == Status::POISON && m.status == Status::TOXIC);
}

bool Menu::load_font(const std::string& path) {
	this->font_ok = this->font.loadFromFile(path);
	this->frame.load();
	this->cursor_ok = this->cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
	this->cursor_tex.setSmooth(false);
	this->region_map_ok = this->region_map_tex.loadFromFile("assets/graphics/pokenav/region_map/map.png");
	this->region_map_tex.setSmooth(false);
	this->marker_ok[0] = this->marker_tex[0].loadFromFile("assets/graphics/pokenav/region_map/brendan_icon.png");
	this->marker_ok[1] = this->marker_tex[1].loadFromFile("assets/graphics/pokenav/region_map/may_icon.png");
	this->marker_tex[0].setSmooth(false);
	this->marker_tex[1].setSmooth(false);
	this->map_sections.clear();
	std::ifstream secf("assets/pokenav/region_map_sections.tsv");
	std::string secline;
	while (std::getline(secf, secline)) {
		std::stringstream ss(secline);
		std::string id, xs, ys, ws, hs, name;
		if (!std::getline(ss, id, '\t')) continue;
		if (!std::getline(ss, xs, '\t')) continue;
		if (!std::getline(ss, ys, '\t')) continue;
		if (!std::getline(ss, ws, '\t')) continue;
		if (!std::getline(ss, hs, '\t')) continue;
		std::getline(ss, name);   // rest of the line (name may contain spaces)
		MapSecEntry e;
		e.x = std::atoi(xs.c_str()); e.y = std::atoi(ys.c_str());
		e.w = std::atoi(ws.c_str()); e.h = std::atoi(hs.c_str());
		e.name = name;
		this->map_sections.push_back(e);
	}
	return this->font_ok;
}

// Real pokeemerald's GetMapSecIdAt / region_map.c uses the same "which
// rectangle contains this grid cell" lookup; a handful of unused/FRLG-only
// entries sit at (0,0) with no real rectangle -- skip those explicitly so a
// cursor near the map's own corner doesn't wrongly report one of them.
std::string Menu::map_section_at(int grid_x, int grid_y) const {
	for (const MapSecEntry& e : this->map_sections) {
		if (e.x == 0 && e.y == 0) continue;
		if (grid_x >= e.x && grid_x < e.x + e.w &&
		    grid_y >= e.y && grid_y < e.y + e.h)
			return e.name;
	}
	return std::string();
}

static std::string pretty(const std::string& id, const std::string& prefix) {
	std::string s = id;
	if (!prefix.empty() && s.rfind(prefix, 0) == 0) s = s.substr(prefix.size());
	std::string out; bool cap = true;
	for (char c : s) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out;
}

// A party/box entry's display name. Shiny individuals get a star after the
// species name -- the recoloured sprite next to it is the real tell, but at
// icon size (and for a species whose normal colours the player doesn't know
// by heart) that is easy to miss, and the real games' send-out sparkle has
// no equivalent outside battle either.
static std::string mon_label(const Mon& m) {
	return pretty(m.display_name(), "") + (m.shiny ? " \u2605" : "");
}

// German status labels for the party/summary screens. BattleData::status_name
// gives the three-letter battle codes (PSN/BRN/...); these are what the German
// games print in their own menus.
static const char* status_short_de(Status s, bool fainted) {
	if (fainted) return "KO";
	switch (s) {
	case Status::SLEEP:     return "SLF";
	case Status::POISON:    return "GIF";
	case Status::TOXIC:     return "GIF";
	case Status::BURN:      return "BRT";
	case Status::PARALYSIS: return "PAR";
	case Status::FREEZE:    return "GFR";
	default:                return "";
	}
}
static const char* status_long_de(Status s, bool fainted) {
	if (fainted) return "Kampfunfähig";
	switch (s) {
	case Status::SLEEP:     return "Schlaf";
	case Status::POISON:    return "Vergiftet";
	case Status::TOXIC:     return "Schwer vergiftet";
	case Status::BURN:      return "Verbrennung";
	case Status::PARALYSIS: return "Paralyse";
	case Status::FREEZE:    return "Vereisung";
	default:                return "Normal";
	}
}

const sf::Texture* Menu::item_icon(const std::string& item) {
	std::string f = item;
	if (f.rfind("ITEM_", 0) == 0) f = f.substr(5);
	for (char& c : f) c = (char)std::tolower((unsigned char)c);
	auto it = this->item_tex.find(f);
	if (it != this->item_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/items/" + f + ".png")) { this->item_tex[f]; return nullptr; }
	tex.setSmooth(false);
	this->item_tex[f] = tex;
	return &this->item_tex[f];
}

const sf::Texture* Menu::mon_icon(const std::string& species, bool shiny) {
	// Shiny artwork is a separate file, so it needs its own cache slot --
	// hence the key carrying the flag rather than the bare species name.
	std::string path = BattleData::sprite_path(species, shiny);
	auto it = this->mon_tex.find(path);
	if (it != this->mon_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile(path)) { this->mon_tex[path]; return nullptr; }
	tex.setSmooth(false);
	this->mon_tex[path] = tex;
	return &this->mon_tex[path];
}

const sf::Texture* Menu::type_icon(const std::string& type) {
	std::string t = type;
	for (char& c : t) c = (char)std::tolower((unsigned char)c);
	if (t == "fighting") t = "fight";
	auto it = this->type_tex.find(t);
	if (it != this->type_tex.end())
		return it->second.getSize().x ? &it->second : nullptr;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/types/" + t + ".png")) { this->type_tex[t]; return nullptr; }
	tex.setSmooth(false);
	this->type_tex[t] = tex;
	return &this->type_tex[t];
}

void Menu::open() { this->screen = MAIN; this->cursor = 0; this->flash.clear(); }
void Menu::close() { this->screen = CLOSED; this->swap_from = -1; }

void Menu::open_party() {
	this->screen = PARTY;
	this->swap_from = -1;
	this->party_cursor = 0;
	this->flash.clear();
}

bool Menu::open_move_learn() {
	if (!this->party || !this->party->has_pending_move_learn()) return false;
	this->learn_cursor = 0;
	this->screen = MOVE_LEARN;
	this->flash.clear();
	return true;
}

// Teach teach_move to the selected party member, consuming the TM (HMs are
// reusable). A mon that already knows four moves gets the same "which one
// should be forgotten?" prompt a level-up move gets, instead of silently
// losing move slot 0 -- the TM is only spent once the player picks one.
void Menu::teach_selected() {
	if (!this->bdata || !this->party) return;
	const Mon* sel = this->party->at(this->teach_cursor);
	if (!sel) return;
	std::string name = pretty(sel->display_name(), "");
	std::string mv_disp = pretty(this->teach_move, "");
	if (!this->bdata->can_learn_tm(sel->species, this->teach_move)) {
		this->flash = name + " kann " + mv_disp + " nicht erlernen.";
		return;
	}
	bool reusable = is_hm(this->teach_item);
	if (sel->moves.size() >= 4) {
		PartyResult r = this->party->queue_move_learn(this->teach_cursor, this->teach_move);
		if (!party_ok(r)) { report(r); return; }
		this->learn_item = reusable ? std::string() : this->teach_item;
		this->learn_cursor = 0;
		this->screen = MOVE_LEARN;
		this->flash.clear();
		return;
	}
	PartyResult r = this->party->learn_move(this->teach_cursor, this->teach_move, -1);
	if (!party_ok(r)) { report(r); return; }
	if (!reusable && this->gs) this->gs->take_item(this->teach_item, 1);
	this->flash = name + " erlernt " + mv_disp + "!";
	this->screen = BAG;
	auto items = bag_sorted();
	if (this->bag_cursor >= (int)items.size())
		this->bag_cursor = std::max(0, (int)items.size() - 1);
}

// Apply use_item to the selected party member: heals HP, cures a status,
// and/or revives a fainted mon, depending on what the item actually is. The
// menu decides *what the item means*; PartySystem decides whether the change
// is legal and performs it.
void Menu::use_selected() {
	if (!this->party) return;
	const Mon* sel = this->party->at(this->use_cursor);
	if (!sel) return;
	const int slot = this->use_cursor;
	std::string name = pretty(sel->display_name(), "");
	const std::string item = this->use_item;

	auto consume_and_close = [&](const std::string& msg) {
		if (this->gs) this->gs->take_item(item, 1);
		this->flash = msg;
		this->screen = BAG;
		auto items = bag_sorted();
		if (this->bag_cursor >= (int)items.size())
			this->bag_cursor = std::max(0, (int)items.size() - 1);
	};

	if (is_revive_item(item)) {
		PartyResult r = this->party->revive(slot, item == "ITEM_MAX_REVIVE");
		if (!party_ok(r)) { report(r); return; }
		consume_and_close(name + " wurde wiederbelebt!");
		return;
	}

	bool wants_cure = has_curable_status(item, *sel);
	int amt = heal_amount(item);          // -1 = this item doesn't restore HP
	bool cured = wants_cure && party_ok(this->party->cure_status(slot));
	bool healed = amt >= 0 && party_ok(this->party->heal_hp(slot, amt));
	if (!cured && !healed) {
		this->flash = sel->fainted() ? (name + " ist kampfunfähig.")
		            : (amt >= 0)     ? (name + " hat bereits volle KP.")
		                             : (name + " hat kein Problem, das behandelt werden müsste.");
		return;
	}
	consume_and_close(name + " wurde behandelt!");
}

// ---------------------------------------------------------- party actions --

const char* Menu::action_label(PartyAction a) {
	switch (a) {
	case PartyAction::SUMMARY:       return "BERICHT";
	case PartyAction::SWAP:          return "POSITION";
	case PartyAction::GIVE_ITEM:     return "ITEM GEBEN";
	case PartyAction::TAKE_ITEM:     return "ITEM NEHMEN";
	case PartyAction::TO_BOX:        return "IN BOX";
	case PartyAction::TO_PARTY:      return "INS TEAM";
	case PartyAction::SET_LEAD:      return "ANFÜHRER";
	case PartyAction::SET_COMPANION: return "BEGLEITER";
	case PartyAction::CANCEL:        return "ZURÜCK";
	}
	return "";
}

// Only actions that can actually succeed right now are offered -- the brief's
// rule that a menu must never show an impossible option (§5/§26). "In BOX" is
// left out entirely when it would strand the party rather than shown and then
// refused, and "Item nehmen" only appears when something is held.
std::vector<PartyAction> Menu::build_actions(int slot, PartyContext ctx) const {
	std::vector<PartyAction> out;
	if (!this->party) return out;
	if (ctx == PartyContext::BOX) {
		const Mon* m = this->party->box_at(slot);
		if (!m) return out;
		out.push_back(PartyAction::SUMMARY);
		if (!this->party->full()) out.push_back(PartyAction::TO_PARTY);
		out.push_back(PartyAction::CANCEL);
		return out;
	}
	const Mon* m = this->party->at(slot);
	if (!m) return out;
	out.push_back(PartyAction::SUMMARY);
	if (this->party->size() > 1) out.push_back(PartyAction::SWAP);
	// "Give" is pointless with nothing holdable in the bag; "take" only makes
	// sense when something is actually held.
	bool has_holdable = false;
	if (this->gs)
		for (const auto& kv : this->gs->bag_items())
			if (kv.second > 0 && PartySystem::is_holdable(kv.first)) { has_holdable = true; break; }
	if (has_holdable) out.push_back(PartyAction::GIVE_ITEM);
	if (!m->held_item.empty() && m->held_item != "NONE")
		out.push_back(PartyAction::TAKE_ITEM);
	if (this->party->active_slot() != slot && !m->fainted())
		out.push_back(PartyAction::SET_LEAD);
	if (this->party->companion_slot() != slot && !m->fainted())
		out.push_back(PartyAction::SET_COMPANION);
	// Depositing is only offered when it would be allowed: never the last
	// member, never the last able one, never into a full PC (§14/§31).
	if (this->party->size() > 1 && !this->party->box_full()) {
		bool would_strand = this->party->requires_able_member() && !m->fainted();
		if (would_strand) {
			for (int i = 0; i < this->party->size(); ++i) {
				const Mon* o = this->party->at(i);
				if (i != slot && o && !o->fainted()) { would_strand = false; break; }
			}
		}
		if (!would_strand) out.push_back(PartyAction::TO_BOX);
	}
	out.push_back(PartyAction::CANCEL);
	return out;
}

void Menu::report(PartyResult r, const std::string& ok_text) {
	this->flash = party_ok(r) ? ok_text : party_result_text(r);
}

void Menu::run_action(PartyAction a) {
	if (!this->party) return;
	const int slot = this->action_slot;
	switch (a) {
	case PartyAction::SUMMARY:
		this->summary_from_box = this->action_context == PartyContext::BOX;
		this->summary_index = slot;
		this->summary_page = SummaryPage::OVERVIEW;
		this->screen = SUMMARY;
		this->flash.clear();
		break;
	case PartyAction::SWAP:
		this->swap_from = slot;
		this->screen = PARTY;
		this->flash = "Mit welchem POKéMON tauschen?";
		break;
	case PartyAction::GIVE_ITEM: {
		this->give_slot = slot;
		this->give_cursor = 0;
		this->give_items.clear();
		if (this->gs)
			for (const auto& kv : this->gs->bag_items())
				if (kv.second > 0 && PartySystem::is_holdable(kv.first))
					this->give_items.push_back(kv.first);
		std::sort(this->give_items.begin(), this->give_items.end());
		if (this->give_items.empty()) { this->flash = "Du hast kein passendes Item."; break; }
		this->screen = GIVE_ITEM;
		this->flash.clear();
		break;
	}
	case PartyAction::TAKE_ITEM: {
		const Mon* m = this->party->at(slot);
		std::string item = m ? m->held_item : std::string();
		PartyResult r = this->party->take_held_item(slot);
		report(r, pretty(item, "ITEM_") + " zurück in den BEUTEL.");
		this->screen = PARTY;
		break;
	}
	case PartyAction::TO_BOX: {
		const Mon* m = this->party->at(slot);
		std::string name = m ? pretty(m->display_name(), "") : std::string();
		PartyResult r = this->party->move_to_box(slot);
		report(r, name + " wurde in der BOX aufbewahrt.");
		if (party_ok(r) && this->party_cursor >= this->party->size())
			this->party_cursor = std::max(0, this->party->size() - 1);
		this->screen = PARTY;
		break;
	}
	case PartyAction::TO_PARTY: {
		const Mon* m = this->party->box_at(slot);
		std::string name = m ? pretty(m->display_name(), "") : std::string();
		PartyResult r = this->party->withdraw_from_box(slot);
		report(r, name + " kam ins Team.");
		if (this->box_cursor >= this->party->box_size())
			this->box_cursor = std::max(0, this->party->box_size() - 1);
		this->screen = PC;
		break;
	}
	case PartyAction::SET_LEAD: {
		const Mon* m = this->party->at(slot);
		PartyResult r = this->party->set_active_slot(slot);
		report(r, m ? pretty(m->display_name(), "") + " führt das Team an." : std::string());
		this->screen = PARTY;
		break;
	}
	case PartyAction::SET_COMPANION: {
		const Mon* m = this->party->at(slot);
		PartyResult r = this->party->set_companion_slot(slot);
		report(r, m ? pretty(m->display_name(), "") + " läuft jetzt mit dir." : std::string());
		this->screen = PARTY;
		break;
	}
	case PartyAction::CANCEL:
		this->screen = this->action_context == PartyContext::BOX ? PC : PARTY;
		this->flash.clear();
		break;
	}
}

// Rebuild only the rows an event marked dirty. Everything the party screen
// draws for a slot comes from here, so the draw path never reads a Mon field
// directly and a row that did not change costs nothing to re-render.
void Menu::refresh_slot_views() {
	for (int i = 0; i < PartySystem::MAX_SLOTS; ++i) {
		unsigned rev = this->party ? this->party->slot_revision(i) : 0;
		if (!this->slot_dirty[i] && this->slot_views[i].rev == rev) continue;
		SlotView v;
		v.rev = rev;
		const Mon* m = this->party ? this->party->at(i) : nullptr;
		if (m) {
			v.present = true;
			v.name = pretty(m->display_name(), "");
			v.level_text = "Lv" + std::to_string(m->level);
			v.hp_text = std::to_string(m->hp) + "/" + std::to_string(m->max_hp);
			v.hp = m->hp; v.max_hp = m->max_hp;
			v.fainted = m->fainted();
			v.status_text = status_short_de(m->status, v.fainted);
			bool holding = !m->held_item.empty() && m->held_item != "NONE";
			v.item_id = holding ? m->held_item : std::string();
			v.item_text = holding ? pretty(m->held_item, "ITEM_") : std::string();
			v.gender = BattleData::gender_symbol(
				BattleData::gender(m->species, m->personality));
			v.shiny = m->shiny;
			v.sprite = m->species;
		}
		this->slot_views[i] = v;
		this->slot_dirty[i] = false;
	}
}

// The AUFGABEN list in reading order: what to do now (main missions first,
// then side missions), then what has already been done. Derived on every use
// rather than cached -- a quest can complete while the screen is open (a
// script running under a msgbox), and a stale row would then point the HUD at
// something already finished.
std::vector<int> Menu::quest_rows() const {
	std::vector<int> rows;
	if (!this->quests) return rows;
	for (int i : this->quests->active(QuestKind::MAIN)) rows.push_back(i);
	for (int i : this->quests->active(QuestKind::SIDE)) rows.push_back(i);
	for (int i : this->quests->completed()) rows.push_back(i);
	return rows;
}

void Menu::input(BtnInput b) {
	if (this->screen == MAIN) {
		if (b == BTN_UP && this->cursor > 0) this->cursor--;
		else if (b == BTN_DOWN && this->cursor < 9) this->cursor++;
		else if (b == BTN_CONFIRM) {
			if (this->cursor == 0) { this->screen = POKEDEX; this->flash.clear(); }
			else if (this->cursor == 1) { this->screen = BAG; this->bag_cursor = 0; this->flash.clear(); }
			else if (this->cursor == 2) this->screen = PARTY;
			else if (this->cursor == 3) this->screen = PC;
			else if (this->cursor == 4) {
				this->screen = POKENAV;
				// Open centered on the player's own location, same as the
				// real games' region map.
				if (this->has_mapsec) {
					this->map_cur_x = this->mapsec_x + this->mapsec_w / 2;
					this->map_cur_y = this->mapsec_y + this->mapsec_h / 2;
				}
			}
			else if (this->cursor == 5) {
				// AUFGABEN (§16): the quest list. Nothing to configure -- the
				// game loop keeps the log refreshed, so it is always current
				// by the time this screen opens.
				this->screen = QUESTS;
				this->quest_cursor = 0;
				this->flash.clear();
			}
			else if (this->cursor == 6) {
				// FLIEGEN: only meaningful once a party member knows FLY --
				// mirrors the badge-free "does the team know the move"
				// simplification Surf/Strength/Waterfall already use.
				bool knows_fly = this->party && this->party->knows_move("FLY");
				if (!knows_fly) { this->flash = "Kein POKéMON kennt FLIEGEN."; }
				else {
					this->fly_available.clear();
					for (int i = 0; i < FLY_DESTINATIONS_N; ++i)
						if (this->gs && this->gs->flag(FLY_DESTINATIONS[i].flag))
							this->fly_available.push_back(i);
					if (this->fly_available.empty())
						this->flash = "Du kennst noch keinen Zielort.";
					else { this->screen = FLY; this->fly_cursor = 0; this->flash.clear(); }
				}
			}
			else if (this->cursor == 7) { this->screen = OPTIONS; this->options_cursor = 0; }
			else if (this->cursor == 8) { this->save_requested = true; this->flash.clear(); }
			else this->screen = CLOSED;
		}
	} else if (this->screen == POKEDEX) {
		int n = this->bdata ? this->bdata->species_count() : 0;
		if (b == BTN_UP && this->dex_cursor > 0) this->dex_cursor--;
		else if (b == BTN_DOWN && this->dex_cursor + 1 < n) this->dex_cursor++;
		else if (b == BTN_LEFT || b == BTN_CONFIRM) this->screen = MAIN;
	} else if (this->screen == POKENAV) {
		// Real PokeNav's region map: moves a cursor tile by tile over the
		// 28x15 grid, naming whatever section it's currently over -- it
		// doesn't fly you anywhere itself (that's the FLY move's own
		// screen, a separate destination list below). [SPACE] exits.
		if (b == BTN_UP && this->map_cur_y > 0) this->map_cur_y--;
		else if (b == BTN_DOWN && this->map_cur_y < 14) this->map_cur_y++;
		else if (b == BTN_LEFT && this->map_cur_x > 0) this->map_cur_x--;
		else if (b == BTN_RIGHT && this->map_cur_x < 27) this->map_cur_x++;
		else if (b == BTN_CONFIRM) this->screen = MAIN;
	} else if (this->screen == FLY) {
		int n = (int)this->fly_available.size();
		if (b == BTN_UP && this->fly_cursor > 0) this->fly_cursor--;
		else if (b == BTN_DOWN && this->fly_cursor + 1 < n) this->fly_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM && this->fly_cursor < n) {
			const FlyDest& d = FLY_DESTINATIONS[this->fly_available[this->fly_cursor]];
			this->fly_map = d.map; this->fly_x = d.x; this->fly_y = d.y;
			this->fly_requested = true;
		}
	} else if (this->screen == BAG) {
		auto items = bag_sorted();
		if (b == BTN_UP && this->bag_cursor > 0) this->bag_cursor--;
		else if (b == BTN_DOWN && this->bag_cursor + 1 < (int)items.size()) this->bag_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM) {
			if (this->bag_cursor < (int)items.size()) {
				const std::string& item = items[this->bag_cursor].first;
				if (is_machine(item) && this->bdata) {
					// pokeemerald's real ITEM_TM_*/ITEM_HM_* item constants
					// are built directly from each TM/HM's move name (see
					// include/item.h's ENUM_TM/ENUM_HM, and how
					// tools/pe_import.py's parse_tm_moves() reads the same
					// FOREACH_TM/FOREACH_HM move list) -- imported scripts
					// use those literal names (`giveitem ITEM_HM_CUT`, not
					// a numbered "ITEM_HM01"), so the move is just the item
					// name with its 8-char "ITEM_TM_"/"ITEM_HM_" prefix cut.
					std::string mv = item.size() > 8 ? item.substr(8) : std::string();
					if (mv.empty() || !this->bdata->move(mv)) this->flash = "Es passiert nichts.";
					else {
						this->teach_item = item; this->teach_move = mv;
						this->teach_cursor = 0; this->flash.clear();
						this->screen = TEACH;
					}
				} else if (heal_amount(item) >= 0 || is_revive_item(item) ||
				           status_cured(item) != Status::NONE || cures_any_status(item)) {
					this->use_item = item; this->use_cursor = 0; this->flash.clear();
					this->screen = USE_ITEM;
				} else {
					this->flash = pretty(item, "ITEM_") + " kann hier nicht benutzt werden.";
				}
			}
		}
	} else if (this->screen == TEACH) {
		int n = this->party ? this->party->size() : 0;
		if (b == BTN_UP && this->teach_cursor > 0) this->teach_cursor--;
		else if (b == BTN_DOWN && this->teach_cursor + 1 < n) this->teach_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL) this->screen = BAG;
		else if (b == BTN_CONFIRM) this->teach_selected();
	} else if (this->screen == USE_ITEM) {
		int n = this->party ? this->party->size() : 0;
		if (b == BTN_UP && this->use_cursor > 0) this->use_cursor--;
		else if (b == BTN_DOWN && this->use_cursor + 1 < n) this->use_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL) { this->screen = BAG; this->flash.clear(); }
		else if (b == BTN_CONFIRM) this->use_selected();
	} else if (this->screen == PARTY) {
		// The cursor walks all six SLOTS, not just the filled ones, so an
		// empty slot is a real place to stand (§16) -- it just has no actions.
		if (b == BTN_UP && this->party_cursor > 0) this->party_cursor--;
		else if (b == BTN_DOWN && this->party_cursor + 1 < PartySystem::MAX_SLOTS)
			this->party_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL) {
			if (this->swap_from >= 0) { this->swap_from = -1; this->flash.clear(); }
			else this->screen = MAIN;
		} else if (b == BTN_CONFIRM) {
			if (!this->party || !this->party->at(this->party_cursor)) {
				this->flash = "Dieser Platz ist leer.";
			} else if (this->swap_from >= 0) {
				// Second half of "Position tauschen": the party ORDER changes,
				// which is not the same thing as switching mid-battle (§13).
				int from = this->swap_from;
				this->swap_from = -1;
				PartyResult r = this->party->swap_slots(from, this->party_cursor);
				report(r, "Die Reihenfolge wurde geändert.");
			} else {
				this->action_slot = this->party_cursor;
				this->action_context = PartyContext::FIELD;
				this->actions = build_actions(this->action_slot, PartyContext::FIELD);
				this->action_cursor = 0;
				this->flash.clear();
				if (!this->actions.empty()) this->screen = PARTY_ACTION;
			}
		} else if (b == BTN_ALT && this->party && this->party->at(this->party_cursor)) {
			// Shoulder-style shortcut straight into the report (§27's X button).
			this->summary_from_box = false;
			this->summary_index = this->party_cursor;
			this->summary_page = SummaryPage::OVERVIEW;
			this->screen = SUMMARY;
		}
	} else if (this->screen == PARTY_ACTION) {
		int n = (int)this->actions.size();
		if (b == BTN_UP && this->action_cursor > 0) this->action_cursor--;
		else if (b == BTN_DOWN && this->action_cursor + 1 < n) this->action_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL)
			this->screen = this->action_context == PartyContext::BOX ? PC : PARTY;
		else if (b == BTN_CONFIRM && this->action_cursor < n)
			run_action(this->actions[this->action_cursor]);
	} else if (this->screen == GIVE_ITEM) {
		int n = (int)this->give_items.size();
		if (b == BTN_UP && this->give_cursor > 0) this->give_cursor--;
		else if (b == BTN_DOWN && this->give_cursor + 1 < n) this->give_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL) { this->screen = PARTY; this->flash.clear(); }
		else if (b == BTN_CONFIRM && this->give_cursor < n) {
			const std::string item = this->give_items[this->give_cursor];
			const Mon* m = this->party ? this->party->at(this->give_slot) : nullptr;
			std::string who = m ? pretty(m->display_name(), "") : std::string();
			PartyResult r = this->party ? this->party->give_held_item(this->give_slot, item)
			                            : PartyResult::NOT_CONFIGURED;
			report(r, who + " trägt jetzt " + pretty(item, "ITEM_") + ".");
			if (party_ok(r)) this->screen = PARTY;
		}
	} else if (this->screen == MOVE_LEARN) {
		const MoveLearnRequest* req = this->party ? this->party->pending_move_learn() : nullptr;
		if (!req) { this->screen = this->learn_item.empty() ? PARTY : BAG; this->learn_item.clear(); }
		else {
			const Mon* m = this->party->find(req->uid);
			int rows = (m ? (int)m->moves.size() : 0) + 1;   // + "Verzichten"
			if (b == BTN_UP && this->learn_cursor > 0) this->learn_cursor--;
			else if (b == BTN_DOWN && this->learn_cursor + 1 < rows) this->learn_cursor++;
			else if (b == BTN_LEFT || b == BTN_CANCEL) this->learn_cursor = rows - 1;
			else if (b == BTN_CONFIRM) {
				bool decline = m == nullptr || this->learn_cursor >= (int)m->moves.size();
				std::string forgot = decline ? std::string() : pretty(m->moves[this->learn_cursor], "");
				std::string learned = pretty(req->move, "");
				std::string who = m ? pretty(m->display_name(), "") : std::string();
				PartyResult r = this->party->resolve_move_learn(decline ? -1 : this->learn_cursor);
				if (party_ok(r)) {
					// A declined TM stays in the bag; a used one is spent here,
					// after the player actually committed to it.
					if (!decline && !this->learn_item.empty() && this->gs)
						this->gs->take_item(this->learn_item, 1);
					this->flash = decline
						? (who + " hat " + learned + " nicht erlernt.")
						: (who + " vergisst " + forgot + " und erlernt " + learned + "!");
				} else {
					report(r);
				}
				this->learn_cursor = 0;
				// More prompts can be queued (two level-ups in one battle);
				// stay on this screen until the queue is empty.
				if (!this->party->has_pending_move_learn()) {
					this->screen = this->learn_item.empty() ? PARTY : BAG;
					this->learn_item.clear();
				}
			}
		}
	} else if (this->screen == SUMMARY) {
		// L/R (or left/right) page through the report; B closes it. The pages
		// are SummaryPage's own order, so any other summary surface shows the
		// same five in the same sequence (§6).
		int page = (int)this->summary_page;
		if (b == BTN_RIGHT) this->summary_page = (SummaryPage)((page + 1) % (int)SummaryPage::COUNT);
		else if (b == BTN_LEFT)
			this->summary_page = (SummaryPage)((page + (int)SummaryPage::COUNT - 1) %
			                                   (int)SummaryPage::COUNT);
		else if (b == BTN_DOWN) this->summary_page = (SummaryPage)((page + 1) % (int)SummaryPage::COUNT);
		else if (b == BTN_UP)
			this->summary_page = (SummaryPage)((page + (int)SummaryPage::COUNT - 1) %
			                                   (int)SummaryPage::COUNT);
		else if (b == BTN_CANCEL || b == BTN_CONFIRM)
			this->screen = this->summary_from_box ? PC : PARTY;
	} else if (this->screen == PC) {
		int n = this->party ? this->party->box_size() : 0;
		if (b == BTN_UP && this->box_cursor > 0) this->box_cursor--;
		else if (b == BTN_DOWN && this->box_cursor + 1 < n) this->box_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL) { this->screen = MAIN; this->flash.clear(); }
		else if (b == BTN_CONFIRM) {
			if (this->box_cursor >= n) { this->flash = "Die BOX ist leer."; }
			else {
				this->action_slot = this->box_cursor;
				this->action_context = PartyContext::BOX;
				this->actions = build_actions(this->action_slot, PartyContext::BOX);
				this->action_cursor = 0;
				this->flash.clear();
				if (!this->actions.empty()) this->screen = PARTY_ACTION;
			}
		}
	} else if (this->screen == QUESTS) {
		std::vector<int> rows = quest_rows();
		if (b == BTN_UP && this->quest_cursor > 0) this->quest_cursor--;
		else if (b == BTN_DOWN && this->quest_cursor + 1 < (int)rows.size()) this->quest_cursor++;
		else if (b == BTN_LEFT || b == BTN_CANCEL) { this->screen = MAIN; this->flash.clear(); }
		else if (b == BTN_CONFIRM && this->gs && this->quests &&
		         this->quest_cursor < (int)rows.size()) {
			// Pin this quest to the HUD (§17). A finished quest can't be
			// tracked -- there would be nothing left to point at -- and
			// confirming the already-tracked one unpins it, back to "whatever
			// the current main mission is".
			const Quest& q = this->quests->quests()[rows[this->quest_cursor]];
			if (q.status != QuestStatus::ACTIVE) {
				this->flash = "Diese Aufgabe ist bereits erledigt.";
			} else if (this->gs->tracked_quest == q.id) {
				this->gs->tracked_quest.clear();
				this->flash = "Verfolgung aufgehoben.";
			} else {
				this->gs->tracked_quest = q.id;
				this->flash = "Wird verfolgt: " + q.title;
			}
		}
	} else if (this->screen == OPTIONS) {
		if (b == BTN_UP && this->options_cursor > 0) this->options_cursor--;
		else if (b == BTN_DOWN && this->options_cursor < 3) this->options_cursor++;
		else if (b == BTN_LEFT) this->screen = MAIN;
		else if (b == BTN_CONFIRM || b == BTN_RIGHT) {
			if (!this->gs) { /* nothing to toggle without a GameState */ }
			else if (this->options_cursor == 0) this->gs->sound_on = !this->gs->sound_on;
			else if (this->options_cursor == 1) this->gs->battle_scene_on = !this->gs->battle_scene_on;
			else if (this->options_cursor == 2) this->gs->quest_hud_on = !this->gs->quest_hud_on;
			else this->gs->frame_type = (this->gs->frame_type + 1) % 20;
		}
	} else if (b == BTN_CONFIRM || b == BTN_LEFT || b == BTN_CANCEL) {
		this->screen = MAIN;   // any screen without its own back handling
	}
}

void Menu::draw(sf::RenderTarget& target) {
	if (this->screen == CLOSED || !this->font_ok) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	// heading / body / muted text tuned for the frame's light interior
	const sf::Color head_col(24, 72, 160), body_col(40, 40, 56), muted_col(100, 100, 112);

	sf::FloatRect panel_rect(size.x * 0.45f - 20, 20, size.x * 0.55f, size.y - 40);
	this->frame.load_type(this->gs ? this->gs->frame_type : 0);   // Options: Rahmenart
	if (this->frame.ready()) {
		this->frame.draw(target, panel_rect.left, panel_rect.top,
		                 panel_rect.width, panel_rect.height, 3.f);
	} else {
		sf::RectangleShape panel(sf::Vector2f(panel_rect.width, panel_rect.height));
		panel.setPosition(panel_rect.left, panel_rect.top);
		panel.setFillColor(sf::Color(20, 28, 48, 240));
		panel.setOutlineColor(sf::Color::White); panel.setOutlineThickness(3.f);
		target.draw(panel);
	}
	sf::RectangleShape panel(sf::Vector2f(panel_rect.width, panel_rect.height));
	panel.setPosition(panel_rect.left, panel_rect.top);
	float x = panel.getPosition().x + 24, y = panel.getPosition().y + 20;

	auto text = [&](const std::string& s, float px, float py, unsigned cs, sf::Color col) {
		sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), this->font, cs);
		t.setPosition(px, py); t.setFillColor(col); target.draw(t);
	};
	auto cursor_at = [&](float px, float py) {
		if (!this->cursor_ok) { text(">", px - 16, py, 20, head_col); return; }
		sf::Sprite s(this->cursor_tex);
		s.setPosition(px - 20, py + 2);
		target.draw(s);
	};
	auto hp_bar = [&](float bx, float by, int hp, int mx) {
		float w = 150, r = mx > 0 ? (float)hp / mx : 0.f;
		sf::RectangleShape bg(sf::Vector2f(w, 12)); bg.setPosition(bx, by);
		bg.setFillColor(sf::Color(190, 190, 190)); target.draw(bg);
		sf::RectangleShape fg(sf::Vector2f(w * r, 12)); fg.setPosition(bx, by);
		fg.setFillColor(r > 0.5f ? sf::Color(80, 200, 80)
		               : r > 0.2f ? sf::Color(230, 200, 60) : sf::Color(220, 70, 70));
		target.draw(fg);
	};
	// Progress to next level, same light-blue bar as the battle HUD's.
	auto exp_bar = [&](float bx, float by, const Mon& m) {
		if (!this->bdata) return;
		float w = 150;
		std::string growth = this->bdata->growth_rate(m.species);
		long floor_now = BattleData::exp_for_level(growth, m.level);
		long floor_next = m.level < 100 ? BattleData::exp_for_level(growth, m.level + 1) : floor_now;
		float r = floor_next > floor_now
		          ? (float)(m.exp - floor_now) / (float)(floor_next - floor_now) : 1.f;
		r = std::max(0.f, std::min(1.f, r));
		sf::RectangleShape bg(sf::Vector2f(w, 5)); bg.setPosition(bx, by);
		bg.setFillColor(sf::Color(190, 190, 190)); target.draw(bg);
		sf::RectangleShape fg(sf::Vector2f(w * r, 5)); fg.setPosition(bx, by);
		fg.setFillColor(sf::Color(88, 168, 240)); target.draw(fg);
	};

	// The action menu floats over whichever list opened it, so the row it acts
	// on stays visible while it is open (§34: as few detached submenus as
	// possible).
	auto draw_action_menu = [&](float anchor_y) {
		if (this->actions.empty()) return;
		float aw = 190.f, ah = 22.f + this->actions.size() * 26.f;
		float ax = panel.getPosition().x + panel.getSize().x - aw - 24;
		float ay = std::min(anchor_y,
		                    panel.getPosition().y + panel.getSize().y - ah - 20);
		sf::RectangleShape bg(sf::Vector2f(aw, ah));
		bg.setPosition(ax, ay);
		bg.setFillColor(sf::Color(250, 250, 252, 245));
		bg.setOutlineColor(head_col); bg.setOutlineThickness(2.f);
		target.draw(bg);
		for (size_t i = 0; i < this->actions.size(); ++i) {
			bool sel = (int)i == this->action_cursor;
			float ry = ay + 10 + i * 26.f;
			if (sel) cursor_at(ax + 18, ry);
			text(action_label(this->actions[i]), ax + 34, ry, 17, sel ? head_col : body_col);
		}
	};

	if (this->screen == MAIN) {
		text("MENÜ", x, y, 24, head_col); y += 44;
		const char* opts[] = {"POKéDEX", "BEUTEL", "POKéMON", "PC-BOX", "POKéNAV",
		                      "AUFGABEN", "FLIEGEN", "OPTIONEN", "SPEICHERN", "SCHLIESSEN"};
		for (int i = 0; i < 10; ++i) {
			bool sel = i == this->cursor;
			if (sel) cursor_at(x, y + i * 34);
			text(opts[i], x, y + i * 34, 22, sel ? head_col : body_col);
		}
		if (!this->flash.empty())
			text(this->flash, x, y + 10 * 34 + 8, 16, sf::Color(30, 140, 60));
	} else if (this->screen == POKEDEX) {
		int total = this->bdata ? this->bdata->species_count() : 0;
		int seen = 0, caught = 0;
		if (this->gs)
			for (int i = 0; i < total; ++i) {
				const std::string sp = this->bdata->species_by_id(i);
				if (this->gs->is_caught(sp)) ++caught;
				else if (this->gs->is_seen(sp)) ++seen;
			}
		text("POKéDEX", x, y, 24, head_col); y += 36;
		text("Gesehen: " + std::to_string(seen + caught) +
		     "   Gefangen: " + std::to_string(caught), x, y, 16, muted_col);
		y += 34;
		const int rows = 10;
		int first = std::max(0, std::min(this->dex_cursor - rows / 2, std::max(0, total - rows)));
		for (int row = 0; row < rows && first + row < total; ++row) {
			int idx = first + row;
			std::string sp = this->bdata->species_by_id(idx);
			bool is_caught = this->gs && this->gs->is_caught(sp);
			bool is_seen = is_caught || (this->gs && this->gs->is_seen(sp));
			float ry = y + row * 34;
			bool sel = idx == this->dex_cursor;
			if (sel) cursor_at(x, ry);
			char num[16]; std::snprintf(num, sizeof(num), "#%03d", idx + 1);
			text(num, x, ry, 18, muted_col);
			if (is_seen) {
				const sf::Texture* ic = mon_icon(sp);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.5f, 0.5f); s.setPosition(x + 56, ry - 8); target.draw(s); }
				text(pretty(sp, ""), x + 96, ry, 20, is_caught ? body_col : muted_col);
				if (is_caught) text("●", x + 300, ry, 18, sf::Color(30, 140, 60));
			} else {
				text("? ? ? ? ?", x + 96, ry, 20, muted_col);
			}
		}
	} else if (this->screen == FLY) {
		text("FLIEGEN", x, y, 24, head_col); y += 44;
		if (this->fly_available.empty()) {
			text("(keine Ziele)", x, y, 20, muted_col);
		} else {
			for (int row = 0; row < (int)this->fly_available.size() && row < 10; ++row) {
				const FlyDest& d = FLY_DESTINATIONS[this->fly_available[row]];
				bool sel = row == this->fly_cursor;
				float ry = y + row * 36;
				if (sel) cursor_at(x, ry);
				text(d.name, x, ry, 20, sel ? head_col : body_col);
			}
		}
	} else if (this->screen == BAG) {
		text("BEUTEL", x, y, 24, head_col); y += 44;
		if (this->gs) {
			text("Geld: " + std::to_string(this->gs->money) + " P", x, y, 18, muted_col);
			y += 30;
		}
		auto items = bag_sorted();
		if (items.empty()) {
			text("(leer)", x, y, 20, muted_col);
		} else {
			for (int row = 0; row < (int)items.size() && row < 10; ++row) {
				const auto& kv = items[row];
				bool sel = row == this->bag_cursor;
				float ry = y + row * 40;
				if (sel) cursor_at(x, ry);
				const sf::Texture* ic = item_icon(kv.first);
				if (ic) { sf::Sprite s(*ic); s.setPosition(x, ry - 4); target.draw(s); }
				bool tm = is_machine(kv.first);
				std::string label;
				if (tm) {
					// ITEM_TM_<move>/ITEM_HM_<move> -> the real games' "TM31"
					// style bag label (see move_to_tm_code()); falls back to
					// the move name itself if the code lookup ever misses.
					std::string mv = kv.first.size() > 8 ? kv.first.substr(8) : std::string();
					std::string code = this->bdata ? this->bdata->move_to_tm_code(mv) : std::string();
					label = code.empty() ? pretty(mv, "") : code;
				} else {
					label = pretty(kv.first, "ITEM_");
				}
				text(label, x + 34, ry, 20,
				     tm ? sf::Color(20, 130, 90) : body_col);
				text("x" + std::to_string(kv.second),
				     panel.getPosition().x + panel.getSize().x - 70, ry, 20, body_col);
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(190, 90, 20));
	} else if (this->screen == TEACH) {
		text("LEHRE " + pretty(this->teach_move, ""), x, y, 22, head_col);
		y += 40;
		text("Wähle ein POKéMON:", x, y, 16, muted_col); y += 30;
		if (this->party) {
			for (int row = 0; row < this->party->size() && row < 6; ++row) {
				const Mon& m = *this->party->at(row);
				bool sel = row == this->teach_cursor;
				bool able = this->bdata && this->bdata->can_learn_tm(m.species, this->teach_move);
				float ry = y + row * 44;
				if (sel) cursor_at(x, ry);
				const sf::Texture* ic = mon_icon(m.species, m.shiny);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(mon_label(m) + "  Lv" + std::to_string(m.level), x + 44, ry, 20,
				     able ? body_col : sf::Color(170, 170, 170));
				text(able ? "OK" : "nicht möglich", panel.getPosition().x + panel.getSize().x - 110, ry, 16,
				     able ? sf::Color(30, 150, 60) : sf::Color(190, 90, 90));
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(190, 90, 20));
	} else if (this->screen == USE_ITEM) {
		bool revive = is_revive_item(this->use_item);
		text(pretty(this->use_item, "ITEM_") + " benutzen", x, y, 22, head_col);
		y += 40;
		text("Wähle ein POKéMON:", x, y, 16, muted_col); y += 30;
		if (this->party) {
			for (int row = 0; row < this->party->size() && row < 6; ++row) {
				const Mon& m = *this->party->at(row);
				bool sel = row == this->use_cursor;
				bool able = revive ? m.fainted()
				          : has_curable_status(this->use_item, m) ||
				            (!m.fainted() && heal_amount(this->use_item) >= 0 && m.hp < m.max_hp);
				float ry = y + row * 44;
				if (sel) cursor_at(x, ry);
				const sf::Texture* ic = mon_icon(m.species, m.shiny);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(mon_label(m) + "  Lv" + std::to_string(m.level), x + 44, ry, 20,
				     able ? body_col : sf::Color(170, 170, 170));
				text(std::to_string(m.hp) + "/" + std::to_string(m.max_hp),
				     panel.getPosition().x + panel.getSize().x - 110, ry, 16,
				     able ? sf::Color(30, 150, 60) : sf::Color(190, 90, 90));
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 60, 16,
			     sf::Color(190, 90, 20));
	} else if (this->screen == PARTY ||
	           (this->screen == PARTY_ACTION && this->action_context == PartyContext::FIELD)) {
		refresh_slot_views();
		text("POKéMON", x, y, 24, head_col);
		if (this->party)
			text(std::to_string(this->party->size()) + "/" +
			     std::to_string(PartySystem::MAX_SLOTS),
			     panel.getPosition().x + panel.getSize().x - 70, y + 6, 16, muted_col);
		y += 38;
		// Six slots, always. A filled one shows the row the brief asks for
		// (icon, name, gender, level, HP bar, HP numbers, status, held item);
		// an empty one is drawn as a visibly different placeholder rather
		// than being skipped (§16).
		const float pitch = 66.f;
		for (int slot = 0; slot < PartySystem::MAX_SLOTS; ++slot) {
			const SlotView& v = slot_view(slot);
			float ry = y + slot * pitch;
			bool sel = slot == this->party_cursor && this->screen == PARTY;
			if (sel) cursor_at(x - 12, ry + 4);
			if (!v.present) {
				sf::RectangleShape empty(sf::Vector2f(panel.getSize().x - 70, pitch - 12));
				empty.setPosition(x - 2, ry);
				empty.setFillColor(sf::Color(0, 0, 0, 18));
				empty.setOutlineColor(sf::Color(150, 150, 160, 120));
				empty.setOutlineThickness(1.f);
				target.draw(empty);
				text("LEER", x + 16, ry + 14, 18, sf::Color(150, 150, 160));
				continue;
			}
			// A filled slot gets the same plate as an empty one so the six
			// rows read as one list; the mon being moved is tinted, and the
			// lead/companion are labelled, so "which one am I acting on?" is
			// never a guess.
			sf::RectangleShape plate(sf::Vector2f(panel.getSize().x - 70, pitch - 12));
			plate.setPosition(x - 2, ry);
			plate.setFillColor(slot == this->swap_from ? sf::Color(240, 200, 80, 70)
			                  : sel                    ? sf::Color(120, 170, 240, 45)
			                                           : sf::Color(0, 0, 0, 10));
			plate.setOutlineColor(sf::Color(150, 150, 160, 90));
			plate.setOutlineThickness(1.f);
			target.draw(plate);
			const sf::Texture* ic = mon_icon(v.sprite, v.shiny);
			if (ic) {
				sf::Sprite sp(*ic); sp.setScale(0.8f, 0.8f); sp.setPosition(x, ry - 2);
				target.draw(sp);
			}
			std::string title = v.name + (v.shiny ? " ★" : "") + v.gender;
			text(title, x + 60, ry, 19, v.fainted ? sf::Color(150, 90, 90) : body_col);
			text(v.level_text, panel.getPosition().x + panel.getSize().x - 90, ry + 2, 17,
			     v.fainted ? sf::Color(150, 90, 90) : body_col);
			hp_bar(x + 60, ry + 26, v.hp, v.max_hp);
			text(v.hp_text, x + 218, ry + 24, 15,
			     v.fainted ? sf::Color(200, 70, 70) : body_col);
			float badge_x = x + 60;
			if (!v.status_text.empty()) {
				text(v.status_text, badge_x, ry + 42, 14,
				     v.fainted ? sf::Color(200, 70, 70) : sf::Color(190, 120, 40));
				badge_x += 40;
			}
			if (!v.item_text.empty()) {
				const sf::Texture* it = item_icon(v.item_id);
				if (it) {
					sf::Sprite is(*it); is.setScale(0.6f, 0.6f);
					is.setPosition(badge_x, ry + 38); target.draw(is);
					badge_x += 22;
				}
				text(v.item_text, badge_x, ry + 42, 14, muted_col);
			}
			if (this->party && this->party->active_slot() == slot)
				text("ANFÜHRER", panel.getPosition().x + panel.getSize().x - 90, ry + 24, 12,
				     sf::Color(60, 120, 200));
			if (this->party && this->party->companion_slot() == slot)
				text("BEGLEITER", panel.getPosition().x + panel.getSize().x - 90, ry + 40, 12,
				     sf::Color(60, 150, 90));
		}
		if (this->screen == PARTY_ACTION)
			draw_action_menu(y + this->action_slot * pitch);
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 58, 15,
			     sf::Color(190, 90, 20));
	} else if (this->screen == GIVE_ITEM) {
		const Mon* m = this->party ? this->party->at(this->give_slot) : nullptr;
		text("ITEM GEBEN", x, y, 22, head_col); y += 34;
		text(m ? pretty(m->display_name(), "") : std::string("---"), x, y, 17, muted_col);
		y += 30;
		for (size_t i = 0; i < this->give_items.size() && i < 12; ++i) {
			bool sel = (int)i == this->give_cursor;
			float ry = y + i * 30.f;
			if (sel) cursor_at(x, ry);
			const sf::Texture* it = item_icon(this->give_items[i]);
			if (it) { sf::Sprite is(*it); is.setPosition(x + 26, ry - 4); target.draw(is); }
			text(pretty(this->give_items[i], "ITEM_"), x + 56, ry, 17, sel ? head_col : body_col);
			text("x" + std::to_string(this->gs ? this->gs->item_count(this->give_items[i]) : 0),
			     panel.getPosition().x + panel.getSize().x - 80, ry, 15, muted_col);
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 58, 15,
			     sf::Color(190, 90, 20));
	} else if (this->screen == MOVE_LEARN) {
		// The real games' "1, 2 und ... zack! Welche Attacke soll vergessen
		// werden?" prompt. The request lives in PartySystem; this only shows
		// it and reports the answer back (§9).
		const MoveLearnRequest* req = this->party ? this->party->pending_move_learn() : nullptr;
		const Mon* m = req && this->party ? this->party->find(req->uid) : nullptr;
		text("NEUE ATTACKE", x, y, 22, head_col); y += 34;
		if (!req || !m) {
			text("(nichts offen)", x, y, 17, muted_col);
		} else {
			text(pretty(m->display_name(), "") + " möchte " + pretty(req->move, "") +
			     " erlernen.", x, y, 16, body_col);
			y += 24;
			const MoveInfo* nmi = this->bdata ? this->bdata->move(req->move) : nullptr;
			if (nmi) {
				text(pretty(nmi->type, "") + "   " +
				     (nmi->power > 0 ? "Stärke " + std::to_string(nmi->power) : std::string("Status")) +
				     "   AP " + std::to_string(nmi->pp), x, y, 14, muted_col);
			}
			y += 28;
			text("Welche Attacke soll vergessen werden?", x, y, 15, muted_col);
			y += 26;
			for (size_t i = 0; i < m->moves.size(); ++i) {
				bool sel = (int)i == this->learn_cursor;
				float ry = y + i * 30.f;
				if (sel) cursor_at(x, ry);
				const MoveInfo* mi = this->bdata ? this->bdata->move(m->moves[i]) : nullptr;
				text(pretty(m->moves[i], ""), x + 26, ry, 17, sel ? head_col : body_col);
				if (mi)
					text("AP " + std::to_string(i < m->pp.size() ? m->pp[i] : mi->pp) +
					     "/" + std::to_string(mi->pp),
					     panel.getPosition().x + panel.getSize().x - 110, ry, 15, muted_col);
			}
			float ry = y + m->moves.size() * 30.f;
			bool sel = this->learn_cursor >= (int)m->moves.size();
			if (sel) cursor_at(x, ry);
			text("VERZICHTEN", x + 26, ry, 17, sel ? head_col : body_col);
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 58, 15,
			     sf::Color(190, 90, 20));
	} else if (this->screen == SUMMARY) {
		const Mon* m = this->summary_from_box
			? (this->party ? this->party->box_at(this->summary_index) : nullptr)
			: (this->party ? this->party->at(this->summary_index) : nullptr);
		if (!m) { text("POKéMON", x, y, 24, head_col); }
		else {
			// --- header, shared by every page ---------------------------------
			const sf::Texture* ic = mon_icon(m->species, m->shiny);
			if (ic) { sf::Sprite sp(*ic); sp.setScale(1.1f, 1.1f); sp.setPosition(x, y); target.draw(sp); }
			char g = BattleData::gender(m->species, m->personality);
			text(pretty(m->display_name(), "") + (m->shiny ? " ★" : "") +
			     BattleData::gender_symbol(g), x + 84, y + 4, 21, head_col);
			text("Lv" + std::to_string(m->level), x + 84, y + 30, 18, body_col);
			if (m->nickname.empty())
				text("", x, y, 12, muted_col);
			else
				text(pretty(m->species, ""), x + 150, y + 32, 14, muted_col);
			text(std::string(summary_page_title(this->summary_page)) + "   " +
			     std::to_string((int)this->summary_page + 1) + "/" +
			     std::to_string((int)SummaryPage::COUNT),
			     panel.getPosition().x + panel.getSize().x - 190, y + 4, 15, muted_col);
			text("< L    R >", panel.getPosition().x + panel.getSize().x - 190, y + 26, 13,
			     muted_col);
			y += 84;

			if (this->summary_page == SummaryPage::OVERVIEW) {
				const sf::Texture* t1 = type_icon(m->t1);
				float tx = x;
				if (t1) { sf::Sprite sp(*t1); sp.setPosition(tx, y); target.draw(sp); tx += 60; }
				if (m->t2 != m->t1) {
					const sf::Texture* t2 = type_icon(m->t2);
					if (t2) { sf::Sprite sp(*t2); sp.setPosition(tx, y); target.draw(sp); }
				}
				y += 34;
				hp_bar(x, y, m->hp, m->max_hp);
				text(std::to_string(m->hp) + "/" + std::to_string(m->max_hp), x + 160, y - 4, 18,
				     m->fainted() ? sf::Color(200, 70, 70) : body_col);
				y += 24;
				exp_bar(x, y, *m);
				y += 24;
				text("Zustand: " + std::string(status_long_de(m->status, m->fainted())), x, y, 16,
				     m->fainted() || m->status != Status::NONE ? sf::Color(190, 90, 20) : muted_col);
				y += 24;
				std::string ab = this->bdata ? this->bdata->ability(m->species) : "";
				text("Fähigkeit: " + (ab.empty() || ab == "NONE" ? std::string("---") : pretty(ab, "")),
				     x, y, 16, muted_col);
				y += 22;
				text("Wesen: " + pretty(m->nature, ""), x, y, 16, muted_col);
				y += 22;
				text("Hält: " + (m->held_item.empty() || m->held_item == "NONE"
				     ? std::string("---") : pretty(m->held_item, "ITEM_")), x, y, 16, muted_col);
				y += 22;
				text("Freundschaft: " + std::to_string(m->friendship), x, y, 16, muted_col);
			} else if (this->summary_page == SummaryPage::MOVES) {
				// Up to four moves with everything the brief lists per move
				// (§8): type, category, power, accuracy, current/max AP.
				for (size_t i = 0; i < m->moves.size() && i < 4; ++i) {
					const MoveInfo* mi = this->bdata ? this->bdata->move(m->moves[i]) : nullptr;
					float ry = y + i * 58.f;
					text(pretty(m->moves[i], ""), x, ry, 18, body_col);
					if (!mi) continue;
					const sf::Texture* ti = type_icon(mi->type);
					if (ti) { sf::Sprite sp(*ti); sp.setScale(0.8f, 0.8f); sp.setPosition(x + 190, ry); target.draw(sp); }
					std::string cat = mi->power <= 0 ? "Status"
					                : BattleData::is_physical(mi->type) ? "Physisch" : "Spezial";
					text(cat + "   Stärke " + (mi->power > 0 ? std::to_string(mi->power) : std::string("---")) +
					     "   Gen. " + (mi->accuracy > 0 ? std::to_string(mi->accuracy) : std::string("---")),
					     x, ry + 22, 14, muted_col);
					text("AP " + std::to_string(i < m->pp.size() ? m->pp[i] : mi->pp) +
					     "/" + std::to_string(mi->pp),
					     panel.getPosition().x + panel.getSize().x - 110, ry + 2, 15, body_col);
				}
				if (m->moves.empty()) text("(keine Attacken)", x, y, 17, muted_col);
			} else if (this->summary_page == SummaryPage::STATS) {
				// Nature-boosted stat in a warm color, lowered in a cool one
				// (the real games' own summary convention), neutral otherwise.
				auto stat_col = [&](char stat) -> sf::Color {
					static const std::map<std::string, std::pair<char,char>> nat = {
						{"LONELY",{'A','D'}}, {"BRAVE",{'A','E'}}, {"ADAMANT",{'A','S'}}, {"NAUGHTY",{'A','F'}},
						{"BOLD",{'D','A'}}, {"RELAXED",{'D','E'}}, {"IMPISH",{'D','S'}}, {"LAX",{'D','F'}},
						{"TIMID",{'E','A'}}, {"HASTY",{'E','D'}}, {"JOLLY",{'E','S'}}, {"NAIVE",{'E','F'}},
						{"MODEST",{'S','A'}}, {"MILD",{'S','D'}}, {"QUIET",{'S','E'}}, {"RASH",{'S','F'}},
						{"CALM",{'F','A'}}, {"GENTLE",{'F','D'}}, {"SASSY",{'F','E'}}, {"CAREFUL",{'F','S'}},
					};
					auto it = nat.find(m->nature);
					if (it == nat.end()) return body_col;
					if (it->second.first == stat) return sf::Color(200, 60, 50);    // boosted
					if (it->second.second == stat) return sf::Color(60, 100, 200);  // lowered
					return body_col;
				};
				struct StatRow { const char* label; int val; int iv; int ev; char key; };
				StatRow rows[] = {
					{"KP",                m->max_hp, m->iv_hp,  m->ev_hp,  'H'},
					{"ANGRIFF",           m->atk,    m->iv_atk, m->ev_atk, 'A'},
					{"VERTEIDIGUNG",      m->def,    m->iv_def, m->ev_def, 'D'},
					{"SP. ANGRIFF",       m->spa,    m->iv_spa, m->ev_spa, 'S'},
					{"SP. VERTEIDIGUNG",  m->spd,    m->iv_spd, m->ev_spd, 'F'},
					{"INITIATIVE",        m->spe,    m->iv_spe, m->ev_spe, 'E'},
				};
				for (const StatRow& r : rows) {
					text(r.label, x, y, 16, muted_col);
					text(std::to_string(r.val), x + 190, y, 16, stat_col(r.key));
					text("DV " + std::to_string(r.iv) + "   FP " + std::to_string(r.ev),
					     x + 250, y + 2, 13, muted_col);
					y += 24;
				}
				y += 8;
				std::string growth = this->bdata ? this->bdata->growth_rate(m->species) : "MEDIUM_FAST";
				long floor_next = m->level < 100
					? BattleData::exp_for_level(growth, m->level + 1) : m->exp;
				text("EP: " + std::to_string(m->exp), x, y, 15, muted_col); y += 20;
				text("Bis Level " + std::to_string(std::min(100, m->level + 1)) + ": " +
				     std::to_string(std::max(0L, floor_next - m->exp)), x, y, 15, muted_col);
				y += 22;
				exp_bar(x, y, *m);
			} else if (this->summary_page == SummaryPage::DETAILS) {
				auto row = [&](const std::string& label, const std::string& value) {
					text(label, x, y, 15, muted_col);
					text(value, x + 170, y, 15, body_col);
					y += 24;
				};
				row("OT", m->ot_name.empty() ? std::string("---") : m->ot_name);
				char idbuf[16];
				std::snprintf(idbuf, sizeof(idbuf), "%05u", m->ot_id % 100000u);
				row("Trainer-ID", m->ot_id ? std::string(idbuf) : std::string("---"));
				row("Ball", m->ball == "NONE" || m->ball.empty()
				            ? std::string("---") : pretty(m->ball, "ITEM_"));
				row("Fangort", m->met_location.empty() ? std::string("---") : m->met_location);
				row("Fanglevel", m->met_level > 0 ? std::to_string(m->met_level) : std::string("---"));
				row("Geschlecht", g == 'N' ? std::string("---")
				                  : std::string(BattleData::gender_symbol(g)));
				row("Schillernd", m->shiny ? std::string("Ja") : std::string("Nein"));
				row("Freundschaft", std::to_string(m->friendship));
				row("ID", std::to_string(m->uid));
			} else if (this->summary_page == SummaryPage::RIBBONS) {
				if (m->ribbons.empty()) {
					text("Noch keine Bänder.", x, y, 17, muted_col);
					y += 30;
					text("Bänder erinnern an besondere", x, y, 14, muted_col); y += 20;
					text("Erfolge dieses POKéMON.", x, y, 14, muted_col);
				} else {
					for (size_t i = 0; i < m->ribbons.size(); ++i)
						text("• " + pretty(m->ribbons[i], "RIBBON_"),
						     x + (i % 2) * 180.f, y + (i / 2) * 26.f, 16, body_col);
				}
			}
		}
	} else if (this->screen == QUESTS) {
		text("AUFGABEN", x, y, 24, head_col); y += 40;
		std::vector<int> rows = quest_rows();
		if (rows.empty()) {
			text("Zurzeit gibt es nichts zu tun.", x, y, 18, muted_col);
		} else {
			if (this->quest_cursor >= (int)rows.size())
				this->quest_cursor = (int)rows.size() - 1;
			const std::vector<Quest>& all = this->quests->quests();
			int tracked = this->gs ? this->quests->tracked(*this->gs) : -1;
			// Section headings are emitted as the kind/status changes while
			// walking the rows, so an empty section prints no heading at all.
			int prev_section = -1;
			float bottom = panel.getPosition().y + panel.getSize().y - 60;
			for (int row = 0; row < (int)rows.size() && y < bottom; ++row) {
				const Quest& q = all[rows[row]];
				int section = q.status == QuestStatus::DONE ? 2
				              : (q.kind == QuestKind::MAIN ? 0 : 1);
				if (section != prev_section) {
					prev_section = section;
					y += 6;
					text(section == 0 ? "HAUPTMISSION"
					     : section == 1 ? "NEBENMISSIONEN" : "ERLEDIGT",
					     x, y, 16, muted_col);
					y += 26;
				}
				bool sel = row == this->quest_cursor;
				if (sel) cursor_at(x, y);
				// Filled bullet = the quest the HUD is pointing at, hollow =
				// merely active, check = done (the mockup in the design brief).
				bool is_tracked = rows[row] == tracked && q.status == QuestStatus::ACTIVE;
				const char* bullet = q.status == QuestStatus::DONE ? "✔"
				                     : (is_tracked ? "●" : "○");
				sf::Color row_col = q.status == QuestStatus::DONE ? muted_col
				                    : (sel ? head_col : body_col);
				text(bullet, x, y, 18, q.status == QuestStatus::DONE
				                          ? sf::Color(120, 170, 130) : row_col);
				text(q.title, x + 26, y, 20, row_col);
				y += 30;
				// Only the selected quest unfolds: its description, the step
				// the player is on, and the progress bar.
				if (!sel) continue;
				if (!q.description.empty()) {
					// Wrapped by word at the panel width -- a description long
					// enough to run off the frame used to just get clipped.
					std::string rest = q.description;
					for (int line = 0; line < 3 && !rest.empty(); ++line) {
						std::string head = rest;
						if (head.size() > 46) {
							size_t cut = head.rfind(' ', 46);
							if (cut == std::string::npos || cut < 8) cut = 46;
							head = rest.substr(0, cut);
							rest = rest.substr(cut + (rest[cut] == ' ' ? 1 : 0));
						} else {
							rest.clear();
						}
						text(head, x + 26, y, 15, muted_col);
						y += 20;
					}
					y += 4;
				}
				if (q.current_step < (int)q.steps.size()) {
					text("→ " + q.steps[q.current_step].text, x + 26, y, 17,
					     sf::Color(30, 110, 60));
					y += 26;
				}
				// "████████████░░░░ 75 %" as a real bar: 16 cells, same
				// proportion, drawn instead of typed so it lines up at any
				// font size.
				const float bw = 200.f, bh = 12.f;
				sf::RectangleShape bg(sf::Vector2f(bw, bh));
				bg.setPosition(x + 26, y + 4);
				bg.setFillColor(sf::Color(200, 200, 208));
				target.draw(bg);
				sf::RectangleShape fg(sf::Vector2f(bw * q.percent / 100.f, bh));
				fg.setPosition(x + 26, y + 4);
				fg.setFillColor(q.percent >= 100 ? sf::Color(90, 180, 110)
				                                 : sf::Color(60, 130, 220));
				target.draw(fg);
				text(std::to_string(q.percent) + " %", x + 26 + bw + 10, y - 2, 16, muted_col);
				y += 30;
			}
		}
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 58, 15,
			     sf::Color(30, 140, 60));
	} else if (this->screen == OPTIONS) {
		text("OPTIONEN", x, y, 24, head_col); y += 44;
		bool sound_on = !this->gs || this->gs->sound_on;
		bool scene_on = !this->gs || this->gs->battle_scene_on;
		int ft = this->gs ? this->gs->frame_type : 0;
		struct Row { const char* label; std::string value; };
		bool hud_on = !this->gs || this->gs->quest_hud_on;
		Row rows[] = {
			{"TON", sound_on ? "AN" : "AUS"},
			{"KAMPFSZENE", scene_on ? "AN" : "AUS"},
			{"ZIEL-ANZEIGE", hud_on ? "AN" : "AUS"},
			{"RAHMENART", std::to_string(ft + 1) + "/20"},
		};
		for (int i = 0; i < 4; ++i) {
			bool sel = i == this->options_cursor;
			if (sel) cursor_at(x, y + i * 40);
			text(rows[i].label, x, y + i * 40, 20, sel ? head_col : body_col);
			text(rows[i].value, x + 220, y + i * 40, 20, sel ? head_col : body_col);
		}
	} else if (this->screen == PC ||
	           (this->screen == PARTY_ACTION && this->action_context == PartyContext::BOX)) {
		int stored = this->party ? this->party->box_size() : 0;
		text("PC-BOX", x, y, 24, head_col); y += 40;
		text("Aufbewahrt: " + std::to_string(stored) + "/" +
		     std::to_string(PartySystem::BOX_CAPACITY), x, y, 18, muted_col);
		y += 30;
		if (stored > 0) {
			// Scroll the window with the cursor so a box past the tenth row is
			// still reachable.
			const int VISIBLE = 10;
			int first = std::max(0, std::min(this->box_cursor - VISIBLE / 2,
			                                 stored - VISIBLE));
			if (first < 0) first = 0;
			for (int row = 0; row < VISIBLE && first + row < stored; ++row) {
				const Mon& m = *this->party->box_at(first + row);
				float ry = y + row * 40;
				bool sel = first + row == this->box_cursor;
				if (sel) cursor_at(x - 12, ry);
				const sf::Texture* ic = mon_icon(m.species, m.shiny);
				if (ic) { sf::Sprite s(*ic); s.setScale(0.55f, 0.55f); s.setPosition(x, ry - 6); target.draw(s); }
				text(mon_label(m) + "  Lv" + std::to_string(m.level), x + 44, ry, 20,
				     sel ? head_col : body_col);
				if (m.fainted())
					text("KO", panel.getPosition().x + panel.getSize().x - 70, ry + 2, 14,
					     sf::Color(200, 70, 70));
			}
		} else {
			text("(keine POKéMON aufbewahrt)", x, y, 20, muted_col);
		}
		if (this->screen == PARTY_ACTION) draw_action_menu(y + 20.f);
		if (!this->flash.empty())
			text(this->flash, x, panel.getPosition().y + panel.getSize().y - 58, 15,
			     sf::Color(190, 90, 20));
	} else if (this->screen == POKENAV) {
		// Real PokeNav's "Hoenn Map Full View" is its own dark navy GBA
		// screen, not a page inside the light Bag/Party-style frame -- cover
		// the generic frame just drawn above with that look instead.
		const sf::Color nav_bg(16, 24, 64), nav_border(120, 180, 232);
		const sf::Color nav_head(232, 240, 255), nav_muted(160, 190, 224);
		sf::RectangleShape nav_panel(sf::Vector2f(panel_rect.width, panel_rect.height));
		nav_panel.setPosition(panel_rect.left, panel_rect.top);
		nav_panel.setFillColor(nav_bg);
		nav_panel.setOutlineColor(nav_border);
		nav_panel.setOutlineThickness(3.f);
		target.draw(nav_panel);

		text("KARTE HOENN GANZ", x, y, 20, nav_head); y += 26;
		if (!this->location.empty()) { text(this->location, x, y, 13, nav_muted); y += 20; }
		else y += 6;
		if (this->region_map_ok) {
			// Fit the map to the panel width instead of a fixed scale, so it
			// reads closer to how the original fills nearly the whole GBA
			// screen rather than sitting small inside a big white sub-panel.
			const float scale = (panel_rect.width - 48.f) / 128.f;
			const float map_w = 128.f * scale, map_h = 120.f * scale;
			sf::Sprite map_spr(this->region_map_tex);
			map_spr.setPosition(x, y);
			map_spr.setScale(scale, scale);
			target.draw(map_spr);
			float px_per_x = map_w / 28.f, px_per_y = map_h / 15.f;
			if (this->has_mapsec) {
				int gender = (this->gs && this->gs->female) ? 1 : 0;
				if (this->marker_ok[gender]) {
					float cx = (this->mapsec_x + this->mapsec_w / 2.0f) * px_per_x;
					float cy = (this->mapsec_y + this->mapsec_h / 2.0f) * px_per_y;
					sf::Sprite mk(this->marker_tex[gender]);
					mk.setScale(scale, scale);
					mk.setPosition(x + cx - 8.f * scale, y + cy - 8.f * scale);
					target.draw(mk);
				}
			}
			// The moving cursor (see Menu::input()): a bright outline over
			// whichever grid cell it's currently on, same idea as the real
			// games' blinking selection box.
			sf::RectangleShape cur_box(sf::Vector2f(px_per_x, px_per_y));
			cur_box.setPosition(x + this->map_cur_x * px_per_x, y + this->map_cur_y * px_per_y);
			cur_box.setFillColor(sf::Color(255, 240, 60, 90));
			cur_box.setOutlineColor(sf::Color(255, 240, 60, 230));
			cur_box.setOutlineThickness(-2.f);   // inward, so it stays inside the cell at this scale
			target.draw(cur_box);
			y += map_h + 10;
			// Location name box, same idea as the real screen's bottom bar
			// naming the section the cursor/marker is over.
			sf::RectangleShape loc_box(sf::Vector2f(panel_rect.width - 2.f * (x - panel_rect.left), 26));
			loc_box.setPosition(x, y);
			loc_box.setFillColor(sf::Color(8, 14, 44));
			loc_box.setOutlineColor(nav_border); loc_box.setOutlineThickness(1.f);
			target.draw(loc_box);
			std::string secname = map_section_at(this->map_cur_x, this->map_cur_y);
			text(secname.empty() ? "OFFENES MEER" : secname, x + 8, y + 3, 18, nav_head);
			y += 36;
		}
		{
			static const char* names[8] = {"STEIN", "FAUST", "DYNAMO", "HITZE",
			                               "BALANCE", "FEDER", "GEIST", "REGEN"};
			int got = 0;
			// Spread over the panel's own width (not a fixed pixel step) so
			// all 8 names fit regardless of window size, instead of running
			// off the right edge.
			float slot = (panel_rect.width - 2.f * (x - panel_rect.left)) / 8.f;
			float bx = x;
			for (int i = 0; i < 8; ++i) {
				bool have = this->gs && this->gs->flag(
					"FLAG_BADGE0" + std::to_string(i + 1) + "_GET");
				if (have) ++got;
				text(names[i], bx, y, 9,
				     have ? sf::Color(255, 210, 90) : nav_muted);
				bx += slot;
			}
			y += 20;
			text("Orden: " + std::to_string(got) + "/8", x, y, 14, nav_muted);
		}
	}
	if (this->screen == BAG)
		text("[SPACE] benutzen   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == TEACH)
		text("[SPACE] lehren   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == USE_ITEM)
		text("[SPACE] benutzen   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == OPTIONS)
		text("[SPACE]/[D] ändern   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	else if (this->screen == QUESTS)
		text("[SPACE] verfolgen   [A] zurück", x, panel.getPosition().y + panel.getSize().y - 34,
		     16, muted_col);
	else if (this->screen == POKENAV)
		text("Pfeiltasten: Karte erkunden   [SPACE] zurück",
		     x, panel.getPosition().y + panel.getSize().y - 34, 16, sf::Color(160, 190, 224));
	else if (this->screen == PARTY)
		text(this->swap_from >= 0 ? "[SPACE] tauschen   [B] abbrechen"
		                          : "[SPACE] auswählen   [X] Bericht   [B] zurück",
		     x, panel.getPosition().y + panel.getSize().y - 34, 15, muted_col);
	else if (this->screen == SUMMARY)
		text("[A]/[D] Seite wechseln   [B] zurück",
		     x, panel.getPosition().y + panel.getSize().y - 34, 15, muted_col);
	else if (this->screen == PC)
		text("[SPACE] auswählen   [B] zurück",
		     x, panel.getPosition().y + panel.getSize().y - 34, 15, muted_col);
	else if (this->screen == GIVE_ITEM || this->screen == PARTY_ACTION ||
	         this->screen == MOVE_LEARN)
		text("[SPACE] bestätigen   [B] zurück",
		     x, panel.getPosition().y + panel.getSize().y - 34, 15, muted_col);
	else if (this->screen != MAIN)
		text("[SPACE] zurück", x, panel.getPosition().y + panel.getSize().y - 34, 16, muted_col);
	target.setView(saved);
}
