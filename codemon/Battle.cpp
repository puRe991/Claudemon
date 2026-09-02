#include "Battle.h"
#include "NameTables.h"
#include <cmath>
#include <cctype>
#include <algorithm>
#include <unordered_map>
#include <climits>

// Fainting cures every status condition (every generation's rule), so a
// later Revive doesn't bring a mon back still nominally poisoned/asleep/...
static void deal_damage(Mon& m, int dmg) {
	m.hp = std::max(0, m.hp - dmg);
	if (m.hp == 0) { m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0; }
}

Battle::Battle()
	: data(nullptr), rng(nullptr), gs(nullptr), team(nullptr), box(nullptr),
	  action_cursor(0), font_ok(false), player(nullptr), active_idx(0),
	  is_trainer(false), party_idx(0), switch_cursor(0), forced_switch(false),
	  log_pos(0), phase(INACTIVE),
	  after_msg(INACTIVE), cursor(0), over(false), victory(false),
	  last_outcome(OUTCOME_NONE),
	  has_trainer_pic(false), intro_shown(false),
	  shake_t(0.f), shake_side(0), prev_ehp(0), prev_php(0) {}

void Battle::set_capture(GameState* g, std::vector<Mon>* t, std::vector<Mon>* b) {
	this->gs = g; this->team = t; this->box = b;
}

void Battle::tick(float dt) {
	if (this->phase == INACTIVE) return;
	// Options screen's "Kampfszene" (Battle Scene) setting: off skips the
	// hit-shake/flash animation entirely -- HP still updates instantly,
	// just without the animated flourish, same idea as the real games'
	// "BATTLE SCENE: OFF" (though real Emerald also skips move animations
	// this engine doesn't have in the first place).
	bool scene_on = !this->gs || this->gs->battle_scene_on;
	// start a shake on whichever side just lost HP
	if (scene_on && this->enemy.hp < this->prev_ehp) { this->shake_t = 0.3f; this->shake_side = 1; }
	else if (scene_on && this->player && this->player->hp < this->prev_php) { this->shake_t = 0.3f; this->shake_side = 2; }
	this->prev_ehp = this->enemy.hp;
	this->prev_php = this->player ? this->player->hp : 0;
	if (this->shake_t > 0.f) this->shake_t -= dt;
}

// Move type name -> type-icon file stem (pokeemerald names).
const sf::Texture* Battle::type_icon(const std::string& type) {
	std::string t = type;
	for (char& c : t) c = (char)std::tolower((unsigned char)c);
	if (t == "fighting") t = "fight";
	auto it = this->type_tex.find(t);
	if (it != this->type_tex.end()) return &it->second;
	sf::Texture tex;
	if (!tex.loadFromFile("assets/types/" + t + ".png")) {
		this->type_tex[t];                 // cache the miss (empty)
		return nullptr;
	}
	tex.setSmooth(false);
	this->type_tex[t] = tex;
	return &this->type_tex[t];
}

void Battle::configure(BattleData* d, std::mt19937* r) {
	this->data = d; this->rng = r;
	this->font_ok = this->font.loadFromFile("assets/fonts/DejaVuSans.ttf");
	this->frame.load();
}

static std::string lower(const std::string& s) {
	std::string o = s;
	for (char& c : o) c = (char)std::tolower((unsigned char)c);
	return o;
}

// pokeemerald's own opponent-kind -> battle theme selection (src/pokemon.c's
// GetTrainerBattleBGM_Internal), trimmed to what's actually reachable here:
// gym leaders (goto_if... walkthrough content), the Champion, story rivals
// (BRENDAN/MAY), the Elite Four, and everything else as a generic trainer.
static std::string trainer_battle_music(const std::string& trainer_id) {
	static const char* GYM_LEADERS[] = {"ROXANNE", "BRAWLY", "WATTSON", "FLANNERY",
	                                    "NORMAN", "WINONA", "TATE_AND_LIZA", "JUAN"};
	static const char* ELITE_FOUR[] = {"SIDNEY", "PHOEBE", "GLACIA", "DRAKE"};
	if (trainer_id.find("WALLACE") != std::string::npos) return "MUS_VS_CHAMPION";
	for (const char* n : ELITE_FOUR)
		if (trainer_id.find(n) != std::string::npos) return "MUS_VS_ELITE_FOUR";
	for (const char* n : GYM_LEADERS)
		if (trainer_id.find(n) != std::string::npos) return "MUS_VS_GYM_LEADER";
	if (trainer_id.find("BRENDAN") != std::string::npos ||
	    trainer_id.find("MAY") != std::string::npos)
		return "MUS_VS_RIVAL";
	return "MUS_VS_TRAINER";
}

std::string Battle::nice(const std::string& id) {
	std::string out; bool cap = true;
	for (char c : id) {
		if (c == '_') { out += ' '; cap = true; }
		else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
		else out += (char)std::tolower((unsigned char)c);
	}
	return out;
}

// A texture that failed to load keeps whatever it held before -- for a
// reused Battle object that is the previous fight's Pokemon, and for a fresh
// one it is undefined content, which is what a missing sprite looked like on
// screen. Blank it instead, so a species with no artwork draws nothing.
static void load_mon_texture(sf::Texture& tex, const std::string& path,
                             const std::string& fallback_path) {
	if (tex.loadFromFile(path)) { tex.setSmooth(false); return; }
	if (!fallback_path.empty() && tex.loadFromFile(fallback_path)) {
		tex.setSmooth(false); return;
	}
	sf::Image blank;
	blank.create(64, 64, sf::Color::Transparent);
	tex.loadFromImage(blank);
	tex.setSmooth(false);
}

// The HUD name plate, e.g. "PIKACHU  Lv12". A shiny individual gets a star
// after its name: the real games mark shininess with a sparkle animation on
// send-out, which this engine has no animation system for, and without any
// marker the only clue would be knowing the species' normal colours by heart.
static std::string hud_name(const Mon& m, const std::string& pretty_name) {
	return pretty_name + (m.shiny ? " \u2605" : "") + "  Lv" + std::to_string(m.level);
}

void Battle::load_sprites() {
	// A shiny individual is drawn from the mirrored shiny sprite set. The
	// fallback chain keeps its old shape -- the player falls back from back
	// sprite to front sprite, both from that mon's own set -- with the enemy
	// additionally falling back from a missing shiny front to the normal
	// one, so an unimported shiny shows the species in its usual colours
	// rather than a blank space.
	load_mon_texture(this->enemy_tex,
	                 BattleData::sprite_path(this->enemy.species, this->enemy.shiny),
	                 this->enemy.shiny ? BattleData::sprite_path(this->enemy.species, false) : "");
	load_mon_texture(this->player_tex,
	                 BattleData::sprite_path(this->player->species, this->player->shiny, true),
	                 BattleData::sprite_path(this->player->species, this->player->shiny));
}

void Battle::queue(const std::string& line) { this->log.push_back(line); }

void Battle::show_messages(Phase next) {
	this->after_msg = next;
	this->log_pos = 0;
	this->phase = MSG;
}

bool Battle::start_wild(const std::string& species, int level, Mon* pm) {
	if (!this->data || !this->data->has_species(species)) return false;
	set_lead(pm);
	this->forced_switch = false;
	this->is_trainer = false;
	this->enemy_title.clear();
	this->party.clear(); this->party_idx = 0;
	this->enemy_items.clear();   // wild Pokemon never carry items
	this->enemy = this->data->make_mon(species, level, this->rng,
	                                   this->gs ? this->gs->trainer_id : 0,
	                                   this->gs ? this->gs->secret_id : 0);
	if (this->gs) this->gs->mark_seen(this->enemy.species);
	this->player_stages = StatStages(); this->enemy_stages = StatStages();
	this->weather = WEATHER_NONE; this->weather_turns = 0;
	this->over = this->victory = false;
	this->last_outcome = OUTCOME_NONE;
	this->turn_count = 0;
	this->cursor = 0;
	load_sprites();
	this->prev_ehp = this->enemy.hp; this->prev_php = this->player->hp; this->shake_t = 0.f;
	this->log.clear();
	queue("Wildes " + nice(species) + " taucht auf!");
	on_switch_in(this->enemy);
	queue("Los, " + nice(this->player->species) + "!");
	on_switch_in(*this->player);
	show_messages(ACTION);
	if (this->audio) {
		this->audio->play_bgm("MUS_VS_WILD");
		this->audio->play_cry(lower(this->enemy.species));
		this->audio->play_cry(lower(this->player->species));
	}
	return true;
}

bool Battle::start_trainer(const std::string& trainer_id, const std::string& name,
                           Mon* pm) {
	if (!this->data) return false;
	auto pty = this->data->trainer_party(trainer_id);
	if (pty.empty()) pty.push_back({"POOCHYENA", 12});   // fallback opponent
	set_lead(pm);
	this->forced_switch = false;
	this->is_trainer = true;
	this->enemy_title = name.empty() ? "TRAINER" : name;
	this->party = pty; this->party_idx = 0;
	this->enemy_items = this->data->trainer_items(trainer_id);
	this->enemy = this->data->make_mon(pty[0].first, pty[0].second, this->rng);
	// pokeemerald builds an NPC trainer's party with OT_ID_RANDOM_NO_SHINY
	// (CreateNPCTrainerParty): it keeps re-rolling the OT id until the mon
	// comes out non-shiny, so a trainer's Pokemon never sparkles.
	this->enemy.shiny = false;
	if (this->gs) this->gs->mark_seen(this->enemy.species);
	this->player_stages = StatStages(); this->enemy_stages = StatStages();
	this->weather = WEATHER_NONE; this->weather_turns = 0;
	this->over = this->victory = false;
	this->last_outcome = OUTCOME_NONE;
	this->turn_count = 0;
	this->cursor = 0;
	// trainer front sprite for the intro
	std::string pic = this->data->trainer_pic(trainer_id);
	this->has_trainer_pic = !pic.empty() &&
		this->trainer_tex.loadFromFile("assets/trainers/" + pic + ".png");
	this->trainer_tex.setSmooth(false);
	this->intro_shown = false;
	load_sprites();
	this->prev_ehp = this->enemy.hp; this->prev_php = this->player->hp; this->shake_t = 0.f;
	this->log.clear();
	queue(this->enemy_title + " möchte kämpfen!");
	queue(this->enemy_title + " schickt " + nice(this->enemy.species) + "!");
	on_switch_in(this->enemy);
	queue("Los, " + nice(this->player->species) + "!");
	on_switch_in(*this->player);
	show_messages(ACTION);
	if (this->audio) {
		this->audio->play_bgm(trainer_battle_music(trainer_id));
		this->audio->play_cry(lower(this->enemy.species));
		this->audio->play_cry(lower(this->player->species));
	}
	return true;
}

bool Battle::try_use_enemy_item() {
	if (!this->is_trainer || this->enemy_items.empty() || this->enemy.fainted())
		return false;
	// Gen-3 potion family + Full Restore/Full Heal -- the common trainer AI
	// item set. Max Potion and Full Restore heal to full; Full Restore and
	// Full Heal also cure a status. (Revive isn't handled here -- it only
	// matters on a faint, a different codepath from "attack or heal".)
	static const std::unordered_map<std::string, int> heal_amount = {
		{"ITEM_POTION", 20}, {"ITEM_SUPER_POTION", 50},
		{"ITEM_HYPER_POTION", 200}, {"ITEM_MAX_POTION", 9999},
		{"ITEM_FULL_RESTORE", 9999},
	};
	int missing = this->enemy.max_hp - this->enemy.hp;
	bool hurting = this->enemy.max_hp > 0 && this->enemy.hp * 2 <= this->enemy.max_hp;   // <=50%
	bool statused = this->enemy.status != Status::NONE;
	if (!hurting && !statused) return false;

	// Pick the trainer's own first item (array order, same as real AI) that
	// actually helps right now: a potion when hurting (prefer the smallest
	// one that still covers the missing HP, so a Hyper Potion isn't wasted
	// on a scratch when a Potion would do), or Full Heal/Full Restore for a
	// pure status problem.
	int best_idx = -1; int best_over = INT_MAX;
	for (size_t i = 0; i < this->enemy_items.size(); ++i) {
		const std::string& it = this->enemy_items[i];
		auto h = heal_amount.find(it);
		if (h != heal_amount.end()) {
			if (!hurting) continue;
			int over = h->second - missing;
			if (over >= 0 && over < best_over) { best_over = over; best_idx = (int)i; }
		} else if (it == "ITEM_FULL_HEAL" && statused && !hurting) {
			best_idx = (int)i; break;   // nothing to weigh, take it
		}
	}
	if (best_idx < 0) return false;

	std::string item = this->enemy_items[(size_t)best_idx];
	this->enemy_items.erase(this->enemy_items.begin() + best_idx);
	this->log.clear();
	queue(this->enemy_title + " setzte " + nice(item.substr(5)) + " ein!");
	auto h = heal_amount.find(item);
	if (h != heal_amount.end()) {
		this->enemy.hp = std::min(this->enemy.max_hp, this->enemy.hp + h->second);
		queue(nice(this->enemy.species) + "s KP wurden aufgefüllt!");
	}
	if (item == "ITEM_FULL_RESTORE" || item == "ITEM_FULL_HEAL") {
		if (statused) {
			this->enemy.status = Status::NONE;
			queue(nice(this->enemy.species) + " wurde geheilt!");
		}
	}
	return true;
}

std::string Battle::ai_move() const {
	if (out_of_pp(this->enemy)) return "STRUGGLE";
	std::string best = this->enemy.moves.empty() ? "TACKLE" : this->enemy.moves[0];
	float best_expected = -1.f;
	for (size_t i = 0; i < this->enemy.moves.size(); ++i) {
		if (i < this->enemy.pp.size() && this->enemy.pp[i] <= 0) continue;   // out of PP
		const std::string& m = this->enemy.moves[i];
		std::mt19937 tmp(12345);
		const MoveInfo* mi = this->data->move(m);
		bool physical = mi && BattleData::is_physical(mi->type);
		int d = this->data->damage(this->enemy, *this->player, m, tmp,
		                           stage_mult(physical ? this->enemy_stages.atk : this->enemy_stages.spa),
		                           stage_mult(physical ? this->player_stages.def : this->player_stages.spd));
		// Weight by accuracy so a big-power-but-unreliable move (e.g. a 120
		// power / 70% hit move) doesn't automatically beat a slightly weaker
		// move that actually lands -- matches AI_SCRIPT_CHECK_VIABILITY's
		// real intent (real pokeemerald AI penalizes low accuracy similarly)
		// without needing that script's full point system.
		float acc = (mi && mi->accuracy > 0) ? mi->accuracy / 100.f : 1.f;
		float expected = d * acc;
		if (expected > best_expected) { best_expected = expected; best = m; }
	}
	return best;
}

bool Battle::roll_accuracy(int accuracy, int acc_stage, int eva_stage) const {
	if (accuracy <= 0) return true;   // 0 = never misses (pokeemerald convention)
	float chance = accuracy * acc_stage_mult(acc_stage - eva_stage);
	return (int)((*this->rng)() % 100) < (int)chance;
}

// Gen-3 stat-stage multiplier tables (indexed by stage+6, so -6..+6 -> 0..12).
float Battle::stage_mult(int stage) {
	static const int num[13] = {2, 2, 2, 2, 2, 2, 2, 3, 4, 5, 6, 7, 8};
	static const int den[13] = {8, 7, 6, 5, 4, 3, 2, 2, 2, 2, 2, 2, 2};
	int i = std::clamp(stage, -6, 6) + 6;
	return (float)num[i] / den[i];
}
float Battle::acc_stage_mult(int stage) {
	static const int num[13] = {3, 3, 3, 3, 3, 3, 3, 4, 5, 6, 7, 8, 9};
	static const int den[13] = {9, 8, 7, 6, 5, 4, 3, 3, 3, 3, 3, 3, 3};
	int i = std::clamp(stage, -6, 6) + 6;
	return (float)num[i] / den[i];
}

bool Battle::status_blocks_turn(Mon& m) {
	std::string name = nice(m.species);
	if (m.status == Status::SLEEP) {
		if (--m.status_turns <= 0) {
			m.status = Status::NONE;
			queue(name + " ist aufgewacht!");
		} else {
			queue(name + " schläft tief und fest.");
			return true;
		}
	} else if (m.status == Status::FREEZE) {
		if (roll_accuracy(20)) {
			m.status = Status::NONE;
			queue(name + " ist aufgetaut!");
		} else {
			queue(name + " ist gefroren!");
			return true;
		}
	}
	if (m.confusion_turns > 0) {
		if (--m.confusion_turns <= 0) {
			queue(name + " ist nicht mehr verwirrt!");
		} else if (roll_accuracy(33)) {
			queue(name + " ist verwirrt!");
			int dmg = std::max(1, (((2 * m.level) / 5 + 2) * 40 * m.atk /
			                       std::max(1, m.def)) / 50 + 2);
			deal_damage(m, dmg);
			check_pinch_berry(m);
			queue(name + " verletzt sich selbst vor Verwirrung!");
			return true;
		}
	}
	if (m.status == Status::PARALYSIS && roll_accuracy(25)) {
		queue(name + " ist paralysiert! Es kann sich nicht bewegen!");
		return true;
	}
	return false;
}

void Battle::try_inflict_status(Mon& target, const std::string& effect) {
	std::string ab = this->data->ability(target.species);
	if (BattleData::effect_confuses(effect)) {
		if (ab == "OWN_TEMPO") return;
		if (target.confusion_turns <= 0) {
			target.confusion_turns = 2 + (int)((*this->rng)() % 3);   // 2-4 turns
			queue(nice(target.species) + " ist jetzt verwirrt!");
			check_status_berry(target);
		}
		return;
	}
	Status st = BattleData::effect_status(effect);
	if (st == Status::NONE || target.status != Status::NONE) return;   // one major status at a time
	// Gen-3 type-based status immunities.
	if ((st == Status::POISON || st == Status::TOXIC) &&
	    (target.t1 == "POISON" || target.t2 == "POISON" ||
	     target.t1 == "STEEL" || target.t2 == "STEEL")) return;
	if (st == Status::BURN && (target.t1 == "FIRE" || target.t2 == "FIRE")) return;
	if (st == Status::FREEZE && (target.t1 == "ICE" || target.t2 == "ICE")) return;
	// Ability-based status immunities.
	if ((st == Status::SLEEP) && (ab == "INSOMNIA" || ab == "VITAL_SPIRIT")) return;
	if ((st == Status::POISON || st == Status::TOXIC) && ab == "IMMUNITY") return;
	if (st == Status::PARALYSIS && ab == "LIMBER") return;
	if (st == Status::BURN && ab == "WATER_VEIL") return;
	if (st == Status::FREEZE && ab == "MAGMA_ARMOR") return;
	target.status = st;
	target.status_turns = (st == Status::SLEEP) ? 1 + (int)((*this->rng)() % 3) : 0;
	switch (st) {
		case Status::SLEEP:     queue(nice(target.species) + " ist eingeschlafen!"); break;
		case Status::POISON:    queue(nice(target.species) + " wurde vergiftet!"); break;
		case Status::TOXIC:     queue(nice(target.species) + " wurde schwer vergiftet!"); break;
		case Status::BURN:      queue(nice(target.species) + " erlitt Verbrennungen!"); break;
		case Status::PARALYSIS: queue(nice(target.species) + " ist paralysiert!"); break;
		case Status::FREEZE:    queue(nice(target.species) + " wurde eingefroren!"); break;
		default: break;
	}
	check_status_berry(target);
}

void Battle::check_status_berry(Mon& m) {
	const std::string& item = m.held_item;
	bool matches = (item == "LUM_BERRY" && (m.status != Status::NONE || m.confusion_turns > 0)) ||
	               (item == "CHERI_BERRY" && m.status == Status::PARALYSIS) ||
	               (item == "CHESTO_BERRY" && m.status == Status::SLEEP) ||
	               (item == "PECHA_BERRY" && (m.status == Status::POISON || m.status == Status::TOXIC)) ||
	               (item == "RAWST_BERRY" && m.status == Status::BURN) ||
	               (item == "ASPEAR_BERRY" && m.status == Status::FREEZE) ||
	               (item == "PERSIM_BERRY" && m.confusion_turns > 0);
	if (!matches) return;
	m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
	queue(nice(m.species) + " wird durch " + nice(item) + " geheilt!");
	m.held_item = "NONE";
}

void Battle::check_pinch_berry(Mon& m) {
	if (m.fainted() || m.hp * 2 > m.max_hp) return;   // only below/at 50% HP
	int heal = 0;
	if (m.held_item == "ORAN_BERRY") heal = 10;
	else if (m.held_item == "SITRUS_BERRY") heal = std::max(1, m.max_hp / 8);
	else return;
	m.hp = std::min(m.max_hp, m.hp + heal);
	queue(nice(m.species) + " isst " + nice(m.held_item) + " und erholt sich!");
	m.held_item = "NONE";
}

float Battle::held_item_type_mult(const Mon& atk, const std::string& move_type) {
	// Real Gen-3 type-boosting held items (+10% damage for their one type).
	static const std::unordered_map<std::string, std::string> tbl = {
		{"MYSTIC_WATER", "WATER"}, {"CHARCOAL", "FIRE"}, {"MAGNET", "ELECTRIC"},
		{"MIRACLE_SEED", "GRASS"}, {"NEVER_MELT_ICE", "ICE"}, {"BLACK_BELT", "FIGHTING"},
		{"POISON_BARB", "POISON"}, {"SOFT_SAND", "GROUND"}, {"SHARP_BEAK", "FLYING"},
		{"TWISTED_SPOON", "PSYCHIC"}, {"SILVER_POWDER", "BUG"}, {"HARD_STONE", "ROCK"},
		{"SPELL_TAG", "GHOST"}, {"DRAGON_FANG", "DRAGON"}, {"BLACKGLASSES", "DARK"},
		{"METAL_COAT", "STEEL"},
	};
	auto it = tbl.find(atk.held_item);
	return (it != tbl.end() && it->second == move_type) ? 1.1f : 1.f;
}

bool Battle::apply_stat_change(Mon& atk, Mon& def, const std::string& effect) {
	struct Entry { char stat; int delta; bool self; };
	static const std::unordered_map<std::string, Entry> tbl = {
		{"ATTACK_UP",               {'A', +1, true}},
		{"ATTACK_UP_2",             {'A', +2, true}},
		{"ATTACK_UP_HIT",           {'A', +1, true}},
		{"ATTACK_DOWN",             {'A', -1, false}},
		{"ATTACK_DOWN_2",           {'A', -2, false}},
		{"ATTACK_DOWN_HIT",         {'A', -1, false}},
		{"DEFENSE_UP",              {'D', +1, true}},
		{"DEFENSE_UP_2",            {'D', +2, true}},
		{"DEFENSE_UP_HIT",          {'D', +1, true}},
		{"DEFENSE_DOWN",            {'D', -1, false}},
		{"DEFENSE_DOWN_2",          {'D', -2, false}},
		{"DEFENSE_DOWN_HIT",        {'D', -1, false}},
		{"DEFENSE_CURL",            {'D', +1, true}},
		{"SPECIAL_ATTACK_UP",       {'S', +1, true}},
		{"SPECIAL_ATTACK_UP_2",     {'S', +2, true}},
		{"SPECIAL_ATTACK_DOWN_HIT", {'S', -1, false}},
		{"SPECIAL_DEFENSE_UP_2",    {'F', +2, true}},
		{"SPECIAL_DEFENSE_DOWN_2",  {'F', -2, false}},
		{"SPECIAL_DEFENSE_DOWN_HIT",{'F', -1, false}},
		{"SPEED_UP_2",              {'E', +2, true}},
		{"SPEED_DOWN",              {'E', -1, false}},
		{"SPEED_DOWN_2",            {'E', -2, false}},
		{"SPEED_DOWN_HIT",          {'E', -1, false}},
		{"ACCURACY_DOWN",           {'C', -1, false}},
		{"ACCURACY_DOWN_HIT",       {'C', -1, false}},
		{"EVASION_UP",              {'V', +1, true}},
		{"EVASION_DOWN",            {'V', -1, false}},
		{"MINIMIZE",                {'V', +1, true}},
	};
	auto apply_one = [&](Mon& target, char stat, int delta) {
		StatStages& st = stages_for(target);
		int* field; std::string name;
		switch (stat) {
			case 'A': field = &st.atk; name = "ANGRIFF"; break;
			case 'D': field = &st.def; name = "VERTEIDIGUNG"; break;
			case 'S': field = &st.spa; name = "SP. ANGRIFF"; break;
			case 'F': field = &st.spd; name = "SP. VERTEIDIGUNG"; break;
			case 'E': field = &st.spe; name = "INITIATIVE"; break;
			case 'C': field = &st.acc; name = "GENAUIGKEIT"; break;
			default:  field = &st.eva; name = "FLUCHT"; break;
		}
		int before = *field;
		*field = std::clamp(*field + delta, -6, 6);
		if (*field == before) {
			queue(nice(target.species) + "s " + name +
			      (delta > 0 ? " kann nicht weiter steigen!" : " kann nicht weiter sinken!"));
		} else {
			queue(nice(target.species) + "s " + name +
			      (delta > 0 ? (delta >= 2 ? " stieg stark an!" : " stieg an!")
			                 : (delta <= -2 ? " sank stark!" : " sank!")));
		}
	};

	auto it = tbl.find(effect);
	if (it != tbl.end()) { apply_one(it->second.self ? atk : def, it->second.stat, it->second.delta); return true; }
	if (effect == "BULK_UP")      { apply_one(atk, 'A', +1); apply_one(atk, 'D', +1); return true; }
	if (effect == "CALM_MIND")    { apply_one(atk, 'S', +1); apply_one(atk, 'F', +1); return true; }
	if (effect == "DRAGON_DANCE") { apply_one(atk, 'A', +1); apply_one(atk, 'E', +1); return true; }
	if (effect == "COSMIC_POWER") { apply_one(atk, 'D', +1); apply_one(atk, 'F', +1); return true; }
	if (effect == "ALL_STATS_UP_HIT") {
		apply_one(atk, 'A', +1); apply_one(atk, 'D', +1); apply_one(atk, 'S', +1);
		apply_one(atk, 'F', +1); apply_one(atk, 'E', +1);
		return true;
	}
	if (effect == "TICKLE")   { apply_one(def, 'A', -1); apply_one(def, 'D', -1); return true; }
	if (effect == "SWAGGER")  { apply_one(def, 'A', +2); try_inflict_status(def, "CONFUSE"); return true; }
	if (effect == "FLATTER") { apply_one(def, 'S', +1); try_inflict_status(def, "CONFUSE"); return true; }
	if (effect == "HAZE") {
		this->player_stages = StatStages(); this->enemy_stages = StatStages();
		queue("Alle Statusveränderungen wurden aufgehoben!");
		return true;
	}
	if (effect == "FOCUS_ENERGY") {
		StatStages& st = stages_for(atk);
		st.crit = std::min(st.crit + 2, 4);
		queue(nice(atk.species) + " ist jetzt kampfbereit!");
		return true;
	}
	return false;
}

bool Battle::apply_weather_effect(const std::string& effect) {
	if (effect == "RAIN_DANCE") {
		this->weather = WEATHER_RAIN; this->weather_turns = 5;
		queue("Es fängt an zu regnen!");
	} else if (effect == "SUNNY_DAY") {
		this->weather = WEATHER_SUN; this->weather_turns = 5;
		queue("Das Sonnenlicht wird grell!");
	} else if (effect == "SANDSTORM") {
		this->weather = WEATHER_SANDSTORM; this->weather_turns = 5;
		queue("Ein Sandsturm zieht auf!");
	} else if (effect == "HAIL") {
		this->weather = WEATHER_HAIL; this->weather_turns = 5;
		queue("Es beginnt zu hageln!");
	} else {
		return false;
	}
	return true;
}

float Battle::weather_damage_mult(const std::string& move_type) const {
	if (this->weather == WEATHER_RAIN) {
		if (move_type == "WATER") return 1.5f;
		if (move_type == "FIRE") return 0.5f;
	} else if (this->weather == WEATHER_SUN) {
		if (move_type == "FIRE") return 1.5f;
		if (move_type == "WATER") return 0.5f;
	}
	return 1.f;
}

void Battle::on_switch_in(Mon& incoming) {
	std::string ab = this->data->ability(incoming.species);
	Mon& other = (&incoming == this->player) ? this->enemy : *this->player;
	if (ab == "INTIMIDATE") {
		StatStages& ost = stages_for(other);
		int before = ost.atk;
		ost.atk = std::clamp(ost.atk - 1, -6, 6);
		queue(nice(incoming.species) + " schüchtert " + nice(other.species) + " ein!");
		queue(nice(other.species) + "s ANGRIFF " +
		      (ost.atk == before ? "kann nicht weiter sinken!" : "sank!"));
	} else if (ab == "DRIZZLE") {
		this->weather = WEATHER_RAIN; this->weather_turns = 127;
		queue(nice(incoming.species) + " lässt es regnen!");
	} else if (ab == "DROUGHT") {
		this->weather = WEATHER_SUN; this->weather_turns = 127;
		queue(nice(incoming.species) + " lässt die Sonne scheinen!");
	} else if (ab == "SAND_STREAM") {
		this->weather = WEATHER_SANDSTORM; this->weather_turns = 127;
		queue(nice(incoming.species) + " wirbelt einen Sandsturm auf!");
	}
}

// Gen-3 crit-stage odds: 0 -> 1/16, 1 -> 1/8, 2 -> 1/4, 3 -> 1/3, 4+ -> 1/2.
bool Battle::roll_critical(const std::string& move_effect, int crit_stage) const {
	static const int denom[5] = {16, 8, 4, 3, 2};
	int stage = crit_stage + (move_effect == "HIGH_CRITICAL" ? 1 : 0);
	int d = denom[std::clamp(stage, 0, 4)];
	return (int)((*this->rng)() % d) == 0;
}

int Battle::move_priority(const std::string& mv) const {
	const MoveInfo* mi = this->data->move(mv);
	if (!mi) return 0;
	if (mi->effect == "QUICK_ATTACK") return 1;    // also Mach Punch/Extreme Speed
	if (mi->effect == "VITAL_THROW") return -1;
	return 0;
}

void Battle::apply_end_of_turn_effects() {
	Mon* sides[2] = {this->player, &this->enemy};

	// Sandstorm/Hail chip damage, then weather's own countdown/expiry.
	if (this->weather == WEATHER_SANDSTORM || this->weather == WEATHER_HAIL) {
		for (Mon* m : sides) {
			if (!m || m->fainted()) continue;
			bool immune = this->weather == WEATHER_SANDSTORM
				? (m->t1 == "ROCK" || m->t2 == "ROCK" || m->t1 == "GROUND" || m->t2 == "GROUND" ||
				   m->t1 == "STEEL" || m->t2 == "STEEL")
				: (m->t1 == "ICE" || m->t2 == "ICE");
			if (immune) continue;
			deal_damage(*m, std::max(1, m->max_hp / 16));
			check_pinch_berry(*m);
			queue(nice(m->species) + (this->weather == WEATHER_SANDSTORM
			      ? " wird vom Sandsturm getroffen!" : " wird vom Hagel getroffen!"));
			if (m->fainted()) queue(nice(m->species) + " wurde besiegt!");
		}
	}
	if (this->weather != WEATHER_NONE && --this->weather_turns <= 0) {
		static const char* clear_msg[] = {"", "Der Regen hört auf.",
		                                  "Das grelle Sonnenlicht lässt nach.",
		                                  "Der Sandsturm legt sich.", "Der Hagelsturm hört auf."};
		queue(clear_msg[this->weather]);
		this->weather = WEATHER_NONE;
	}

	for (Mon* m : sides) {
		if (!m || m->fainted() || m->status == Status::NONE) continue;
		int dmg = 0;
		const char* msg = nullptr;
		if (m->status == Status::POISON) {
			dmg = std::max(1, m->max_hp / 8);
			msg = " leidet unter der Vergiftung!";
		} else if (m->status == Status::TOXIC) {
			m->status_turns++;
			dmg = std::max(1, (m->max_hp * m->status_turns) / 16);
			msg = " leidet stark unter der Vergiftung!";
		} else if (m->status == Status::BURN) {
			dmg = std::max(1, m->max_hp / 8);
			msg = " leidet unter der Verbrennung!";
		}
		if (!msg) continue;
		deal_damage(*m, dmg);
		check_pinch_berry(*m);
		queue(nice(m->species) + msg);
		if (m->fainted()) queue(nice(m->species) + " wurde besiegt!");
	}

	// Leftovers: heal 1/16 max HP at the very end of the turn.
	for (Mon* m : sides) {
		if (!m || m->fainted() || m->held_item != "LEFTOVERS") continue;
		if (m->hp >= m->max_hp) continue;
		m->hp = std::min(m->max_hp, m->hp + std::max(1, m->max_hp / 16));
		queue(nice(m->species) + " erholt sich durch sein Leftovers!");
	}
}

void Battle::consume_pp(Mon& m, const std::string& mv) {
	for (size_t i = 0; i < m.moves.size(); ++i) {
		if (m.moves[i] != mv) continue;
		if (i < m.pp.size() && m.pp[i] > 0) m.pp[i]--;
		return;
	}
}

bool Battle::out_of_pp(const Mon& m) {
	if (m.moves.empty()) return true;
	for (size_t i = 0; i < m.moves.size(); ++i)
		if (i >= m.pp.size() || m.pp[i] > 0) return false;   // missing pp entry reads as "has PP"
	return true;
}

void Battle::do_move(Mon& atk, Mon& def, const std::string& mv,
                     const std::string& atk_name) {
	if (status_blocks_turn(atk)) return;
	consume_pp(atk, mv);
	queue(atk_name + " setzt " + nice(mv) + " ein!");
	const MoveInfo* mi = this->data->move(mv);
	if (!mi) return;
	StatStages& atk_st = stages_for(atk);
	StatStages& def_st = stages_for(def);
	if (!roll_accuracy(mi->accuracy, atk_st.acc, def_st.eva)) {
		queue("Der Angriff geht daneben!"); return;
	}
	// Struggle bypasses the type chart entirely in real pokeemerald (always
	// neutral, no immunities, no STAB) -- it's the only way to act at 0 PP,
	// so it can't be blocked by a Ghost/Steel/etc immunity or Wonder Guard.
	bool is_struggle = (mv == "STRUGGLE");
	float eff = is_struggle ? 1.f : BattleData::type_eff(mi->type, def.t1, def.t2);
	std::string def_ability = this->data->ability(def.species);
	if (!is_struggle && mi->type == "GROUND" && def_ability == "LEVITATE") eff = 0.f;
	if (eff == 0.f) { queue("Hat keine Wirkung auf " + nice(def.species) + " ..."); return; }
	if (mi->power <= 0) {                      // pure status move: no damage model
		if (!apply_weather_effect(mi->effect) &&
		    !apply_stat_change(atk, def, mi->effect))
			try_inflict_status(def, mi->effect);
		return;
	}
	// Wonder Guard: a damaging move that isn't super effective does nothing
	// (status moves aren't affected -- Shedinja can still be poisoned).
	if (!is_struggle && def_ability == "WONDER_GUARD" && eff <= 1.f) {
		queue("Hat keine Wirkung auf " + nice(def.species) + " ..."); return;
	}
	bool physical = BattleData::is_physical(mi->type);
	bool crit = roll_critical(mi->effect, atk_st.crit);
	float atk_mult = stage_mult(physical ? atk_st.atk : atk_st.spa);
	float def_mult = stage_mult(physical ? def_st.def : def_st.spd);
	// A crit ignores the attacker's own stat drop and the defender's own
	// stat boost (real games' rule) -- clamp each multiplier back to at
	// least/most neutral rather than dropping it outright. Weather is its
	// own multiplier, layered on after, so a crit never cancels it out.
	if (crit) { atk_mult = std::max(atk_mult, 1.f); def_mult = std::min(def_mult, 1.f); }
	atk_mult *= weather_damage_mult(mi->type);
	if (!is_struggle) atk_mult *= held_item_type_mult(atk, mi->type);
	int dmg = this->data->damage(atk, def, mv, *this->rng, atk_mult, def_mult, crit);
	// Recoil is a fraction of the HP *actually taken off*, not of the raw
	// damage roll -- pokeemerald clamps gHpDealt to the target's remaining
	// HP before computing `gBattleMoveDamage = gHpDealt / 4`. Using the raw
	// roll let an overkill hit on a nearly-dead target recoil for a quarter
	// of a number far bigger than that target's whole HP bar, which could
	// faint the attacker outright.
	int dealt = std::min(dmg, std::max(0, def.hp));
	deal_damage(def, dmg);
	check_pinch_berry(def);
	if (crit) queue("Ein Volltreffer!");
	if (eff > 1.f) queue("Das ist sehr effektiv!");
	else if (eff < 1.f) queue("Das ist nicht sehr effektiv ...");
	if (mi->effect == "RECOIL" && dealt > 0) {
		deal_damage(atk, std::max(1, dealt / 4));
		check_pinch_berry(atk);
		queue(nice(atk.species) + " wird vom Rückstoß getroffen!");
		if (atk.fainted()) queue(nice(atk.species) + " wurde besiegt!");
	}
	if (def.fainted()) { queue(nice(def.species) + " wurde besiegt!"); return; }
	if (mi->secondary_chance > 0 && roll_accuracy(mi->secondary_chance))
		if (!apply_stat_change(atk, def, mi->effect)) try_inflict_status(def, mi->effect);
}

void Battle::send_next_enemy() {
	this->party_idx++;
	this->enemy = this->data->make_mon(this->party[this->party_idx].first,
	                                   this->party[this->party_idx].second, this->rng);
	this->enemy.shiny = false;                 // see start_trainer(): NPC parties never are
	if (this->gs) this->gs->mark_seen(this->enemy.species);
	this->enemy_stages = StatStages();
	load_sprites();
	queue(this->enemy_title + " schickt " + nice(this->enemy.species) + "!");
	on_switch_in(this->enemy);
	if (this->audio) this->audio->play_cry(lower(this->enemy.species));
}

void Battle::handle_enemy_faint() {
	// award experience to the player's pokemon
	long gain = (long)this->data->exp_yield(this->enemy.species) *
	            this->enemy.level / 7;
	std::vector<std::string> xm;
	this->data->grant_exp(*this->player, gain, xm);
	for (const std::string& m : xm) queue(m);
	if (this->is_trainer && this->party_idx + 1 < this->party.size()) {
		send_next_enemy();
		show_messages(ACTION);
		return;
	}
	queue(this->is_trainer ? ("Du hast " + this->enemy_title + " besiegt!")
	                       : "Das wilde POKéMON wurde besiegt!");
	this->over = true; this->victory = true;
	this->last_outcome = OUTCOME_WON;
	show_messages(INACTIVE);
	if (this->audio)
		this->audio->play_bgm(this->is_trainer ? "MUS_VICTORY_TRAINER" : "MUS_VICTORY_WILD", false);
}

void Battle::resolve_turn(const std::string& player_move) {
	this->turn_count++;
	if (try_use_enemy_item()) {
		// Item use takes the trainer's whole turn -- the player still acts.
		do_move(*this->player, this->enemy, player_move, nice(this->player->species));
		apply_end_of_turn_effects();
		if (this->player->fainted()) { handle_player_faint(); return; }
		if (this->enemy.fainted()) { handle_enemy_faint(); return; }
		show_messages(ACTION);
		return;
	}
	std::string enemy_move = ai_move();
	// Paralysis quarters effective Speed for turn-order purposes (Gen-3),
	// on top of that side's own Speed stat stage.
	auto eff_speed = [this](const Mon& m) {
		float spe = m.spe * stage_mult((&m == this->player) ? this->player_stages.spe
		                                                     : this->enemy_stages.spe);
		return m.status == Status::PARALYSIS ? std::max(1.f, spe / 4.f) : spe;
	};
	// Priority moves (Quick Attack, ...) act before Speed is ever consulted;
	// only a tie there falls back to Speed.
	int pp = move_priority(player_move), pe = move_priority(enemy_move);
	bool player_first = pp != pe ? pp > pe
	                             : eff_speed(*this->player) >= eff_speed(this->enemy);

	Mon* first = player_first ? this->player : &this->enemy;
	Mon* second = player_first ? &this->enemy : this->player;
	std::string fm = player_first ? player_move : enemy_move;
	std::string sm = player_first ? enemy_move : player_move;
	std::string first_name  = player_first ? nice(this->player->species) : nice(this->enemy.species);
	std::string second_name = player_first ? nice(this->enemy.species) : nice(this->player->species);

	do_move(*first, *second, fm, first_name);
	if (!second->fainted() && !first->fainted())
		do_move(*second, *first, sm, second_name);

	apply_end_of_turn_effects();

	// outcome
	if (this->player->fainted()) {
		handle_player_faint();
		return;
	}
	if (this->enemy.fainted()) {
		handle_enemy_faint();
		return;
	}
	show_messages(ACTION);
}

void Battle::input(BtnInput b) {
	if (this->phase == MSG) {
		if (b == BTN_CONFIRM) {
			this->log_pos++;
			if (this->log_pos >= this->log.size()) {
				this->log.clear();
				this->phase = this->after_msg;   // MENU or INACTIVE
				if (this->phase == ACTION) this->intro_shown = true;
			}
		}
		return;
	}
	if (this->phase == ACTION) {
		// FIGHT / POKEMON / BALL / RUN
		if (b == BTN_UP && this->action_cursor > 0) this->action_cursor--;
		else if (b == BTN_DOWN && this->action_cursor < 3) this->action_cursor++;
		else if (b == BTN_CONFIRM) {
			if (this->action_cursor == 0) {
				// Every move out of PP: pokeemerald skips the move menu
				// entirely and just uses Struggle.
				if (out_of_pp(*this->player)) resolve_turn("STRUGGLE");
				else { this->cursor = 0; this->phase = MOVE; }
			}
			else if (this->action_cursor == 1) open_switch();
			else if (this->action_cursor == 2) open_ball_menu();
			else flee();
		}
		return;
	}
	if (this->phase == MOVE) {
		int n = (int)this->player->moves.size();
		if (n <= 0) return;
		if (b == BTN_CANCEL) { this->phase = ACTION; return; }
		if (b == BTN_LEFT  && (this->cursor % 2) == 1) this->cursor--;
		else if (b == BTN_RIGHT && (this->cursor % 2) == 0 && this->cursor + 1 < n) this->cursor++;
		else if (b == BTN_UP   && this->cursor >= 2) this->cursor -= 2;
		else if (b == BTN_DOWN && this->cursor + 2 < n) this->cursor += 2;
		else if (b == BTN_CONFIRM) {
			bool has_pp = this->cursor >= (int)this->player->pp.size() ||
			             this->player->pp[this->cursor] > 0;
			if (!has_pp) {
				this->log.clear();
				queue("Kein PP mehr für " + nice(this->player->moves[this->cursor]) + "!");
				show_messages(MOVE);
			} else {
				resolve_turn(this->player->moves[this->cursor]);
			}
		}
		return;
	}
	if (this->phase == SWITCH) {
		int n = this->team ? (int)this->team->size() : 0;
		if (b == BTN_UP && this->switch_cursor > 0) this->switch_cursor--;
		else if (b == BTN_DOWN && this->switch_cursor + 1 < n) this->switch_cursor++;
		else if ((b == BTN_LEFT || b == BTN_CANCEL) && !this->forced_switch) this->phase = ACTION;
		else if (b == BTN_CONFIRM) do_switch(this->switch_cursor);
	}
	if (this->phase == BALL) {
		int n = (int)this->owned_balls.size();
		if (b == BTN_UP && this->ball_cursor > 0) this->ball_cursor--;
		else if (b == BTN_DOWN && this->ball_cursor + 1 < n) this->ball_cursor++;
		else if (b == BTN_CANCEL) this->phase = ACTION;
		else if (b == BTN_CONFIRM && this->ball_cursor < n)
			throw_ball(this->owned_balls[this->ball_cursor]);
	}
}

bool Battle::has_healthy_reserve() const {
	if (!this->team) return false;
	for (const Mon& m : *this->team) if (!m.fainted()) return true;
	return false;
}

void Battle::set_lead(Mon* fallback) {
	this->active_idx = lead_index();
	this->player = (this->team && this->active_idx < this->team->size())
		? &(*this->team)[this->active_idx] : fallback;
}

size_t Battle::lead_index() const {
	if (!this->team) return 0;
	for (size_t i = 0; i < this->team->size(); ++i)
		if (!(*this->team)[i].fainted()) return i;
	return 0;   // whole party down: the caller's whiteout handling takes over
}

Mon Battle::caught_mon() const {
	// Reuse the encounter's own already-rolled values rather than generating
	// new ones -- they were "always" this individual's, same as a wild
	// Pokemon's stats not changing at the moment you catch it. make_mon()
	// without an RNG is only used to rebuild the moveset/base stats, so
	// everything it would otherwise have rolled is copied over here; missing
	// the held item out of that list handed every caught Pokemon an empty
	// item slot, even one that had just been eating its own Oran Berry.
	Mon caught = this->data->make_mon(this->enemy.species, this->enemy.level);
	caught.iv_hp = this->enemy.iv_hp; caught.iv_atk = this->enemy.iv_atk;
	caught.iv_def = this->enemy.iv_def; caught.iv_spa = this->enemy.iv_spa;
	caught.iv_spd = this->enemy.iv_spd; caught.iv_spe = this->enemy.iv_spe;
	caught.nature = this->enemy.nature;
	caught.personality = this->enemy.personality;
	caught.shiny = this->enemy.shiny;
	caught.held_item = this->enemy.held_item;
	this->data->recompute_stats(caught, false);   // fills HP up to the new max
	// ... and then the battle-worn state on top: a Pokemon caught at 3 HP and
	// asleep joins the party at 3 HP and asleep, it does not walk in fully
	// healed. Confusion is deliberately left behind -- it is volatile in
	// Gen 3 and wears off with the battle, unlike the major status
	// conditions, which the mon carries around until it is cured.
	caught.hp = std::max(1, std::min(this->enemy.hp, caught.max_hp));
	caught.status = this->enemy.status;
	caught.status_turns = this->enemy.status_turns;
	return caught;
}

void Battle::handle_player_faint() {
	queue(nice(this->player->species) + " wurde besiegt!");
	if (has_healthy_reserve()) {
		this->forced_switch = true;
		this->switch_cursor = 0;
		if (this->team)
			for (size_t i = 0; i < this->team->size(); ++i)
				if (!(*this->team)[i].fainted()) { this->switch_cursor = (int)i; break; }
		show_messages(SWITCH);
	} else {
		queue("Du hast den Kampf verloren ...");
		this->over = true; this->victory = false;
		this->last_outcome = OUTCOME_LOST;
		show_messages(INACTIVE);
	}
}

void Battle::open_switch() {
	this->switch_cursor = 0;
	if (this->team)
		for (size_t i = 0; i < this->team->size(); ++i)
			if (i != this->active_idx) { this->switch_cursor = (int)i; break; }
	this->forced_switch = false;
	this->phase = SWITCH;
}

void Battle::do_switch(int idx) {
	if (!this->team || idx < 0 || idx >= (int)this->team->size()) return;
	if ((size_t)idx == this->active_idx) return;   // already out
	Mon& chosen = (*this->team)[idx];
	if (chosen.fainted()) {
		this->log.clear();
		queue(nice(chosen.species) + " kann nicht kämpfen!");
		show_messages(SWITCH);
		return;
	}
	bool was_forced = this->forced_switch;
	this->active_idx = (size_t)idx;
	this->player = &chosen;
	this->player_stages = StatStages();
	load_sprites();
	this->prev_php = this->player->hp; this->shake_t = 0.f;
	this->log.clear();
	queue("Los, " + nice(this->player->species) + "!");
	on_switch_in(*this->player);
	if (this->audio) this->audio->play_cry(lower(this->player->species));
	if (was_forced) {
		// the old mon's faint already used up this turn.
		show_messages(ACTION);
		return;
	}
	// a voluntary switch spends the turn -- the opponent gets a free hit.
	enemy_turn_after();
}

void Battle::enemy_turn_after() {
	this->turn_count++;
	if (try_use_enemy_item()) { show_messages(ACTION); return; }
	std::string em = ai_move();
	do_move(this->enemy, *this->player, em, nice(this->enemy.species));
	apply_end_of_turn_effects();
	if (this->player->fainted()) { handle_player_faint(); return; }
	if (this->enemy.fainted()) { handle_enemy_faint(); return; }
	show_messages(ACTION);
}

const std::vector<std::string>& Battle::ball_types() {
	static const std::vector<std::string> balls = {
		"ITEM_POKE_BALL", "ITEM_GREAT_BALL", "ITEM_ULTRA_BALL", "ITEM_MASTER_BALL",
		"ITEM_SAFARI_BALL", "ITEM_NET_BALL", "ITEM_DIVE_BALL", "ITEM_NEST_BALL",
		"ITEM_REPEAT_BALL", "ITEM_TIMER_BALL", "ITEM_LUXURY_BALL", "ITEM_PREMIER_BALL",
		"ITEM_DUSK_BALL", "ITEM_HEAL_BALL", "ITEM_QUICK_BALL", "ITEM_CHERISH_BALL",
		"ITEM_FAST_BALL", "ITEM_LEVEL_BALL", "ITEM_LURE_BALL", "ITEM_HEAVY_BALL",
		"ITEM_LOVE_BALL", "ITEM_FRIEND_BALL", "ITEM_MOON_BALL", "ITEM_SPORT_BALL",
		"ITEM_PARK_BALL", "ITEM_DREAM_BALL", "ITEM_BEAST_BALL",
	};
	return balls;
}

void Battle::open_ball_menu() {
	this->owned_balls.clear();
	if (this->gs)
		for (const std::string& b : ball_types())
			if (this->gs->item_count(b) > 0) this->owned_balls.push_back(b);
	if (this->owned_balls.empty()) {
		this->log.clear(); queue("Du hast keine POKéBÄLLE mehr!");
		show_messages(ACTION); return;
	}
	this->ball_cursor = 0;
	this->phase = BALL;
}

float Battle::ball_multiplier(const std::string& ball_item) const {
	if (ball_item == "ITEM_GREAT_BALL") return 1.5f;
	if (ball_item == "ITEM_ULTRA_BALL") return 2.f;
	if (ball_item == "ITEM_SAFARI_BALL") return 1.5f;
	if (ball_item == "ITEM_NET_BALL") {
		auto t = this->data->species_types(this->enemy.species);
		return (t.first == "WATER" || t.second == "WATER" ||
		        t.first == "BUG" || t.second == "BUG") ? 3.f : 1.f;
	}
	if (ball_item == "ITEM_NEST_BALL")
		return this->enemy.level < 41 ? std::max(1.f, (41.f - this->enemy.level) / 10.f) : 1.f;
	if (ball_item == "ITEM_REPEAT_BALL")
		return (this->gs && this->gs->is_caught(this->enemy.species)) ? 3.5f : 1.f;
	if (ball_item == "ITEM_TIMER_BALL")
		return std::min(4.f, 1.f + 3.f * std::min(this->turn_count, 10) / 10.f);
	if (ball_item == "ITEM_QUICK_BALL")
		return this->turn_count <= 0 ? 5.f : 1.f;
	if (ball_item == "ITEM_FAST_BALL")
		return this->enemy.spe >= 100 ? 4.f : 1.f;
	if (ball_item == "ITEM_LEVEL_BALL") {
		if (!this->player) return 1.f;
		if (this->player->level >= this->enemy.level * 4) return 8.f;
		if (this->player->level >= this->enemy.level * 2) return 4.f;
		if (this->player->level > this->enemy.level) return 2.f;
		return 1.f;
	}
	if (ball_item == "ITEM_BEAST_BALL") return 0.1f;   // no Ultra Beasts to boost against
	// Dive/Lure (fishing/surfing context), Dusk (cave/night), Heavy (weight),
	// Love/Friend/Moon (gender/friendship/evolution data this engine doesn't
	// track), and the never-sold Luxury/Premier/Cherish/Sport/Park/Dream all
	// fall back to a neutral Poke-Ball-equivalent roll.
	return 1.f;
}

void Battle::throw_ball(const std::string& ball_item) {
	if (this->is_trainer) {
		this->log.clear(); queue("Du kannst nicht das POKéMON eines TRAINERS fangen!");
		show_messages(ACTION); return;
	}
	if (!this->gs || this->gs->item_count(ball_item) <= 0) {
		this->log.clear(); queue("Du hast keine POKéBÄLLE mehr!");
		show_messages(ACTION); return;
	}
	this->gs->give_item(ball_item, -1);
	this->log.clear();
	std::string ball_name = REAL_ITEM_NAMES.count(ball_item) ? REAL_ITEM_NAMES.at(ball_item) : "POKéBALL";
	queue("Du hast einen " + ball_name + " geworfen!");
	if (ball_item == "ITEM_MASTER_BALL") {
		queue("Erwischt! " + nice(this->enemy.species) + " wurde gefangen!");
		if (this->gs) this->gs->mark_caught(this->enemy.species);
		Mon caught = caught_mon();
		if (this->team && this->team->size() < 6) this->team->push_back(caught);
		else if (this->box) { this->box->push_back(caught);
			queue(nice(this->enemy.species) + " wurde zur PC-BOX geschickt."); }
		this->over = true; this->victory = true;
		this->last_outcome = OUTCOME_CAUGHT;
		show_messages(INACTIVE);
		return;
	}
	// Real Gen-3 catch formula: a = (3*maxHP - 2*hp) * catchRate * ballBonus /
	// (3*maxHP), scaled by a status bonus (2x asleep/frozen, 1.5x any other
	// status). The classic 4-shake check's aggregate success probability
	// collapses to exactly a/255 (each shake succeeds at (a/255)^(1/4); four
	// independent successes multiply back to a/255), so a single roll
	// against that is mathematically equivalent without needing to simulate
	// the shake UI.
	int species_catch_rate = this->data->catch_rate(this->enemy.species);
	float ball_bonus = ball_multiplier(ball_item);
	long a = this->enemy.max_hp > 0
		? (long)(((3L * this->enemy.max_hp - 2L * this->enemy.hp) * species_catch_rate) * ball_bonus / (3L * this->enemy.max_hp))
		: 0;
	float status_bonus = (this->enemy.status == Status::SLEEP || this->enemy.status == Status::FREEZE) ? 2.f
	                    : this->enemy.status != Status::NONE ? 1.5f : 1.f;
	float p = std::min(1.f, (float)a * status_bonus / 255.f);
	if ((*this->rng)() % 65536 < (unsigned)(p * 65536.f)) {
		queue("Erwischt! " + nice(this->enemy.species) + " wurde gefangen!");
		if (this->gs) this->gs->mark_caught(this->enemy.species);
		Mon caught = caught_mon();
		if (this->team && this->team->size() < 6) this->team->push_back(caught);
		else if (this->box) { this->box->push_back(caught);
			queue(nice(this->enemy.species) + " wurde zur PC-BOX geschickt."); }
		this->over = true; this->victory = true;
		this->last_outcome = OUTCOME_CAUGHT;
		show_messages(INACTIVE);
	} else {
		queue(nice(this->enemy.species) + " hat sich befreit!");
		enemy_turn_after();
	}
}

void Battle::flee() {
	if (this->is_trainer) {
		this->log.clear(); queue("Nein! Du kannst nicht vor einem TRAINER-Kampf fliehen!");
		show_messages(ACTION); return;
	}
	this->log.clear(); queue("Erfolgreich entkommen!");
	this->over = true; this->victory = false;
	this->last_outcome = OUTCOME_RAN;
	show_messages(INACTIVE);
}

// ------------------------------------------------------------------ drawing --
static void draw_hp_bar(sf::RenderTarget& t, float x, float y, float w, float h,
                        int hp, int max_hp) {
	sf::RectangleShape bg(sf::Vector2f(w, h));
	bg.setPosition(x, y); bg.setFillColor(sf::Color(60, 60, 60));
	bg.setOutlineColor(sf::Color::White); bg.setOutlineThickness(2.f);
	t.draw(bg);
	float r = max_hp > 0 ? (float)hp / max_hp : 0.f;
	sf::RectangleShape fg(sf::Vector2f(std::max(0.f, (w - 4) * r), h - 4));
	fg.setPosition(x + 2, y + 2);
	fg.setFillColor(r > 0.5f ? sf::Color(80, 200, 80)
	               : r > 0.2f ? sf::Color(230, 200, 60) : sf::Color(220, 70, 70));
	t.draw(fg);
}

// The player's own EXP bar (pokeemerald shows this only for the active
// player mon, never the opponent's): a thin light-blue bar under the HP bar,
// filled by progress from this level's own EXP floor to the next one's.
static void draw_exp_bar(sf::RenderTarget& t, const BattleData& bdata,
                         float x, float y, float w, float h, const Mon& mon) {
	sf::RectangleShape bg(sf::Vector2f(w, h));
	bg.setPosition(x, y); bg.setFillColor(sf::Color(60, 60, 60));
	bg.setOutlineColor(sf::Color::White); bg.setOutlineThickness(1.f);
	t.draw(bg);
	std::string growth = bdata.growth_rate(mon.species);
	long floor_now = BattleData::exp_for_level(growth, mon.level);
	long floor_next = mon.level < 100 ? BattleData::exp_for_level(growth, mon.level + 1) : floor_now;
	float r = floor_next > floor_now
	          ? (float)(mon.exp - floor_now) / (float)(floor_next - floor_now) : 1.f;
	r = std::max(0.f, std::min(1.f, r));
	sf::RectangleShape fg(sf::Vector2f(std::max(0.f, (w - 2) * r), h - 2));
	fg.setPosition(x + 1, y + 1);
	fg.setFillColor(sf::Color(88, 168, 240));
	t.draw(fg);
}

static void draw_status_badge(sf::RenderTarget& t, const sf::Font& font,
                              float x, float y, Status st) {
	const char* label = BattleData::status_name(st);
	if (!*label) return;
	sf::Color col = (st == Status::BURN) ? sf::Color(220, 100, 40)
	              : (st == Status::PARALYSIS) ? sf::Color(210, 180, 30)
	              : (st == Status::FREEZE) ? sf::Color(110, 195, 230)
	              : (st == Status::SLEEP) ? sf::Color(140, 140, 160)
	              : sf::Color(160, 70, 190);   // POISON / TOXIC
	sf::RectangleShape bg(sf::Vector2f(38, 20));
	bg.setPosition(x, y); bg.setFillColor(col);
	t.draw(bg);
	sf::Text txt(label, font, 14);
	txt.setPosition(x + 4, y + 2); txt.setFillColor(sf::Color::White);
	t.draw(txt);
}

void Battle::draw(sf::RenderTarget& target) {
	if (this->phase == INACTIVE) return;
	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	sf::RectangleShape sky(sf::Vector2f(size.x, size.y));
	sky.setFillColor(sf::Color(232, 232, 200)); target.draw(sky);
	sf::RectangleShape ground(sf::Vector2f(size.x, size.y * 0.35f));
	ground.setPosition(0, size.y * 0.45f);
	ground.setFillColor(sf::Color(200, 216, 160)); target.draw(ground);

	// hit-animation helpers: defender flashes + shakes, attacker lunges forward.
	float anim = (this->shake_t > 0.f) ? (0.3f - this->shake_t) / 0.3f : -1.f; // 0..1
	float lunge = anim >= 0.f ? std::sin(anim * 3.14159f) * 22.f : 0.f;
	bool def_enemy = this->shake_side == 1;   // enemy is the one being hit
	sf::Uint8 eflash = (def_enemy && this->shake_t > 0.f && ((int)(this->shake_t * 30) % 2)) ? 90 : 255;
	sf::Uint8 pflash = (!def_enemy && this->shake_t > 0.f && ((int)(this->shake_t * 30) % 2)) ? 90 : 255;
	float ex = (def_enemy && this->shake_t > 0.f) ? std::sin(this->shake_t * 60.f) * 8.f : 0.f;
	float ey = (!def_enemy) ? -lunge * 0.6f : 0.f;   // enemy lunges down-left when attacking
	float pxo = (!def_enemy && this->shake_t > 0.f) ? std::sin(this->shake_t * 60.f) * 8.f : 0.f;
	float pyo = (def_enemy) ? -lunge * 0.6f : 0.f;   // player lunges up-right when attacking

	// enemy side: trainer sprite during the intro, otherwise the pokemon + HP
	bool show_trainer = this->is_trainer && this->has_trainer_pic && !this->intro_shown;
	if (show_trainer) {
		sf::Sprite ts(this->trainer_tex);
		ts.setScale(2.8f, 2.8f);
		ts.setPosition(size.x * 0.60f, size.y * 0.05f);
		target.draw(ts);
	} else {
		sf::Sprite es(this->enemy_tex);
		es.setScale(2.6f, 2.6f);
		es.setColor(sf::Color(255, 255, 255, eflash));
		es.setPosition(size.x * 0.62f + ex - (def_enemy ? 0.f : lunge),
		               size.y * 0.06f + ey);
		target.draw(es);
		if (this->font_ok) {
			std::string en = hud_name(this->enemy, nice(this->enemy.species));
			sf::Text n(sf::String::fromUtf8(en.begin(), en.end()), this->font, 20);
			n.setPosition(24, 24); n.setFillColor(sf::Color(20, 20, 20)); target.draw(n);
			draw_status_badge(target, this->font, 210, 22, this->enemy.status);
		}
		draw_hp_bar(target, 24, 52, 240, 16, this->enemy.hp, this->enemy.max_hp);
	}

	// player (back) bottom-left + info bottom-right
	sf::Sprite ps(this->player_tex);
	ps.setScale(2.8f, 2.8f);
	ps.setColor(sf::Color(255, 255, 255, pflash));
	ps.setPosition(size.x * 0.10f + pxo + (def_enemy ? lunge : 0.f),
	               size.y * 0.40f + pyo);
	target.draw(ps);
	if (this->font_ok) {
		std::string pn = hud_name(*this->player, nice(this->player->species));
		sf::Text n(sf::String::fromUtf8(pn.begin(), pn.end()), this->font, 20);
		n.setPosition(size.x - 264, size.y * 0.44f);
		n.setFillColor(sf::Color(20, 20, 20)); target.draw(n);
		draw_status_badge(target, this->font, size.x - 60, size.y * 0.44f - 2, this->player->status);
		sf::Text hp(std::to_string(this->player->hp) + " / " + std::to_string(this->player->max_hp),
		            this->font, 18);
		hp.setPosition(size.x - 264, size.y * 0.44f + 48);
		hp.setFillColor(sf::Color(20, 20, 20)); target.draw(hp);
	}
	draw_hp_bar(target, size.x - 264, size.y * 0.44f + 28, 240, 16,
	            this->player->hp, this->player->max_hp);
	if (this->data)
		draw_exp_bar(target, *this->data, size.x - 264, size.y * 0.44f + 68, 240, 6,
		             *this->player);

	// bottom panel (pokeemerald's own textbox frame)
	const float bh = 150.f, bm = 14.f;
	if (this->frame.ready()) {
		this->frame.draw(target, bm, size.y - bh - bm, size.x - 2 * bm, bh, 3.f);
	} else {
		sf::RectangleShape box(sf::Vector2f(size.x - 2 * bm, bh));
		box.setPosition(bm, size.y - bh - bm);
		box.setFillColor(sf::Color(20, 28, 48, 240));
		box.setOutlineColor(sf::Color::White); box.setOutlineThickness(3.f);
		target.draw(box);
	}
	if (!this->font_ok) { target.setView(saved); return; }

	const sf::Color body_col = this->frame.ready() ? sf::Color(40, 40, 56) : sf::Color::White;
	const sf::Color head_col = this->frame.ready() ? sf::Color(24, 72, 160) : sf::Color(150, 210, 255);
	const sf::Color dis_col = this->frame.ready() ? sf::Color(170, 170, 170) : sf::Color(120, 120, 120);
	const sf::Color muted_col = this->frame.ready() ? sf::Color(100, 100, 112) : sf::Color(200, 200, 200);
	auto cursor_at = [&](float px, float py) {
		sf::Text a(">", this->font, 22); a.setPosition(px - 18, py);
		a.setFillColor(head_col); target.draw(a);
	};

	float tx = bm + 18, ty = size.y - bh - bm + 16;
	if (this->phase == MSG) {
		std::string line = this->log_pos < this->log.size() ? this->log[this->log_pos] : "";
		sf::Text t(sf::String::fromUtf8(line.begin(), line.end()), this->font, 22);
		t.setPosition(tx, ty + 30); t.setFillColor(body_col); target.draw(t);
	} else if (this->phase == ACTION) {
		sf::Text q("Was soll " + nice(this->player->species) + " tun?", this->font, 20);
		q.setPosition(tx, ty); q.setFillColor(body_col); target.draw(q);
		static const std::string acts[] = {"KAMPF", "POKéMON", "BALL", "FLUCHT"};
		for (int i = 0; i < 4; ++i) {
			bool sel = i == this->action_cursor;
			bool dis = (i == 2 || i == 3) && this->is_trainer;   // no catching/running trainers
			if (sel) cursor_at(size.x * 0.46f, ty + i * 34);
			sf::Text a(sf::String::fromUtf8(acts[i].begin(), acts[i].end()), this->font, 22);
			a.setPosition(size.x * 0.46f, ty + i * 34);
			a.setFillColor(dis ? dis_col : sel ? head_col : body_col);
			target.draw(a);
		}
		if (this->gs) {
			int total_balls = 0;
			for (const std::string& b : ball_types()) total_balls += this->gs->item_count(b);
			std::string bt = "BÄLLE x" + std::to_string(total_balls);
			sf::Text bc(sf::String::fromUtf8(bt.begin(), bt.end()), this->font, 16);
			bc.setPosition(size.x * 0.72f, ty + 34);
			bc.setFillColor(muted_col); target.draw(bc);
		}
	} else if (this->phase == BALL) {
		std::string qs = "Welchen BALL werfen?";
		sf::Text q(sf::String::fromUtf8(qs.begin(), qs.end()), this->font, 20);
		q.setPosition(tx, ty); q.setFillColor(body_col); target.draw(q);
		for (size_t i = 0; i < this->owned_balls.size(); ++i) {
			bool sel = (int)i == this->ball_cursor;
			float ry = ty + 30 + i * 22;
			if (sel) cursor_at(tx + 14, ry);
			const std::string& id = this->owned_balls[i];
			std::string label = (REAL_ITEM_NAMES.count(id) ? REAL_ITEM_NAMES.at(id) : id)
			                     + " x" + std::to_string(this->gs ? this->gs->item_count(id) : 0);
			sf::Text t(sf::String::fromUtf8(label.begin(), label.end()), this->font, 16);
			t.setPosition(tx + 28, ry); t.setFillColor(sel ? head_col : body_col);
			target.draw(t);
		}
	} else if (this->phase == MOVE) {
		std::string qs = "Wähle eine Attacke:";
		sf::Text q(sf::String::fromUtf8(qs.begin(), qs.end()), this->font, 20);
		q.setPosition(tx, ty); q.setFillColor(body_col); target.draw(q);
		for (size_t i = 0; i < this->player->moves.size(); ++i) {
			float mx = size.x * 0.42f + (i % 2) * (size.x * 0.27f);
			float my = ty + 34 + (i / 2) * 40;
			bool sel = (int)i == this->cursor;
			bool no_pp = i < this->player->pp.size() && this->player->pp[i] <= 0;
			if (sel) cursor_at(mx, my);
			sf::Text m(nice(this->player->moves[i]), this->font, 20);
			m.setPosition(mx, my);
			m.setFillColor(no_pp ? dis_col : sel ? head_col : body_col);
			target.draw(m);
			if (i < this->player->pp.size()) {
				const MoveInfo* pmi = this->data->move(this->player->moves[i]);
				int max_pp = pmi ? pmi->pp : this->player->pp[i];
				sf::Text pp(std::to_string(this->player->pp[i]) + "/" + std::to_string(max_pp),
				            this->font, 14);
				pp.setPosition(mx, my + 20);
				pp.setFillColor(no_pp ? sf::Color(190, 90, 90) : muted_col);
				target.draw(pp);
			}
			// type badge next to the move
			const MoveInfo* mi = this->data->move(this->player->moves[i]);
			if (mi) {
				const sf::Texture* ti = type_icon(mi->type);
				if (ti) {
					sf::Sprite badge(*ti);
					badge.setScale(1.2f, 1.2f);
					badge.setPosition(mx + size.x * 0.19f, my + 2);
					target.draw(badge);
				}
			}
		}
	} else if (this->phase == SWITCH) {
		std::string qs = this->forced_switch ? "Wer soll als nächstes kämpfen?"
		                                     : "POKéMON wählen:";
		sf::Text q(sf::String::fromUtf8(qs.begin(), qs.end()), this->font, 18);
		q.setPosition(tx, ty - 4); q.setFillColor(body_col); target.draw(q);
		if (this->team) {
			int n = std::min((int)this->team->size(), 6);
			for (int i = 0; i < n; ++i) {
				const Mon& m = (*this->team)[i];
				bool sel = i == this->switch_cursor;
				bool cur = (size_t)i == this->active_idx;
				float ry = ty + 22 + i * 18;
				if (sel) cursor_at(tx + 14, ry);
				sf::Color col = m.fainted() ? dis_col : cur ? muted_col : body_col;
				std::string label = nice(m.species) + " Lv" + std::to_string(m.level) +
				                     (cur ? " (im Kampf)" : m.fainted() ? " (K.O.)" : "");
				sf::Text t(sf::String::fromUtf8(label.begin(), label.end()), this->font, 14);
				t.setPosition(tx + 28, ry); t.setFillColor(col); target.draw(t);
				draw_hp_bar(target, size.x * 0.62f, ry + 1, 130, 10, m.hp, m.max_hp);
			}
		}
	}
	target.setView(saved);
}
