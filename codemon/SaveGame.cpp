#include "SaveGame.h"
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

void write_mon(std::ofstream& f, const Mon& m) {
	f << m.species << '\t' << m.level << '\t' << m.hp << '\t' << m.max_hp << '\t'
	  << m.atk << '\t' << m.def << '\t' << m.spa << '\t' << m.spd << '\t' << m.spe << '\t'
	  << m.t1 << '\t' << m.t2 << '\t' << m.exp << '\t';
	for (size_t i = 0; i < m.moves.size(); ++i) {
		if (i) f << ',';
		f << m.moves[i];
	}
	f << '\t' << (int)m.status << '\t' << m.status_turns << '\t' << m.confusion_turns << '\t'
	  << m.nature << '\t' << m.iv_hp << '\t' << m.iv_atk << '\t' << m.iv_def << '\t'
	  << m.iv_spa << '\t' << m.iv_spd << '\t' << m.iv_spe << '\t';
	for (size_t i = 0; i < m.pp.size(); ++i) {
		if (i) f << ',';
		f << m.pp[i];
	}
	f << '\t' << m.held_item << '\t' << m.personality << '\t' << (m.shiny ? 1 : 0);
	// Fields 27.. were added with the party system. They are appended rather
	// than woven in so a savegame written before it still reads back (see
	// read_mon's size checks) -- the same rule the shiny fields followed.
	f << '\t' << m.uid << '\t' << m.nickname << '\t' << m.ot_name
	  << '\t' << m.ot_id << '\t' << m.ot_secret << '\t' << m.ball
	  << '\t' << m.met_location << '\t' << m.met_level << '\t' << m.friendship
	  << '\t' << m.ev_hp << ',' << m.ev_atk << ',' << m.ev_def << ','
	  << m.ev_spa << ',' << m.ev_spd << ',' << m.ev_spe << '\t';
	// Ribbons are the last field, so "no ribbons" is written as "-" rather
	// than as nothing: a trailing empty column would be swallowed by the
	// tab split on read, and the whole record would look one field short.
	if (m.ribbons.empty()) f << '-';
	for (size_t i = 0; i < m.ribbons.size(); ++i) {
		if (i) f << ',';
		f << m.ribbons[i];
	}
	f << '\n';
}

// Splits on '\t'; returns false if the line doesn't have enough fields.
bool read_mon(const std::string& line, Mon& m) {
	std::vector<std::string> f;
	std::stringstream ss(line);
	std::string tok;
	while (std::getline(ss, tok, '\t')) f.push_back(tok);
	if (f.size() < 13) return false;
	m.species = f[0];
	m.level = std::atoi(f[1].c_str());
	m.hp = std::atoi(f[2].c_str());
	m.max_hp = std::atoi(f[3].c_str());
	m.atk = std::atoi(f[4].c_str());
	m.def = std::atoi(f[5].c_str());
	m.spa = std::atoi(f[6].c_str());
	m.spd = std::atoi(f[7].c_str());
	m.spe = std::atoi(f[8].c_str());
	m.t1 = f[9];
	m.t2 = f[10];
	m.exp = std::atol(f[11].c_str());
	m.moves.clear();
	std::stringstream ms(f[12]);
	while (std::getline(ms, tok, ',')) if (!tok.empty()) m.moves.push_back(tok);
	if (f.size() >= 16) {
		m.status = (Status)std::atoi(f[13].c_str());
		m.status_turns = std::atoi(f[14].c_str());
		m.confusion_turns = std::atoi(f[15].c_str());
	}
	if (f.size() >= 23) {
		m.nature = f[16];
		m.iv_hp = std::atoi(f[17].c_str());
		m.iv_atk = std::atoi(f[18].c_str());
		m.iv_def = std::atoi(f[19].c_str());
		m.iv_spa = std::atoi(f[20].c_str());
		m.iv_spd = std::atoi(f[21].c_str());
		m.iv_spe = std::atoi(f[22].c_str());
	}
	if (f.size() >= 24) {
		m.pp.clear();
		std::stringstream ps(f[23]);
		while (std::getline(ps, tok, ',')) if (!tok.empty()) m.pp.push_back(std::atoi(tok.c_str()));
	}
	if (f.size() >= 25) m.held_item = f[24];
	// Saves written before shiny Pokemon existed have no personality value;
	// leaving those two at their defaults keeps such a mon non-shiny, which
	// is exactly what it was when the file was written.
	if (f.size() >= 27) {
		m.personality = (unsigned)std::strtoul(f[25].c_str(), nullptr, 10);
		m.shiny = f[26] != "0";
	}
	// Party-system fields. A pre-party-system save has none of them; the
	// defaults on Mon are already the right answer there (uid 0 means "not
	// adopted yet", which PartySystem mints an id for on load).
	if (f.size() >= 38) {
		m.uid = (unsigned)std::strtoul(f[27].c_str(), nullptr, 10);
		m.nickname = f[28];
		m.ot_name = f[29];
		m.ot_id = (unsigned)std::strtoul(f[30].c_str(), nullptr, 10);
		m.ot_secret = (unsigned)std::strtoul(f[31].c_str(), nullptr, 10);
		m.ball = f[32];
		m.met_location = f[33];
		m.met_level = std::atoi(f[34].c_str());
		m.friendship = std::atoi(f[35].c_str());
		std::vector<int> evs;
		std::stringstream es(f[36]);
		while (std::getline(es, tok, ',')) evs.push_back(std::atoi(tok.c_str()));
		if (evs.size() >= 6) {
			m.ev_hp = evs[0]; m.ev_atk = evs[1]; m.ev_def = evs[2];
			m.ev_spa = evs[3]; m.ev_spd = evs[4]; m.ev_spe = evs[5];
		}
		m.ribbons.clear();
		if (f[37] != "-") {
			std::stringstream rs(f[37]);
			while (std::getline(rs, tok, ',')) if (!tok.empty()) m.ribbons.push_back(tok);
		}
	}
	return true;
}

} // namespace

bool SaveGame::exists(const std::string& path) {
	std::ifstream f(path);
	return f.good();
}

bool SaveGame::save(const std::string& path, const GameState& gs,
                    const std::vector<Mon>& team, const std::vector<Mon>& box,
                    const std::string& map_path, int player_x, int player_y) {
	std::ofstream f(path, std::ios::trunc);
	if (!f.is_open()) return false;

	f << "SAVE 1\n";
	f << "map\t" << map_path << '\n';
	f << "pos\t" << player_x << '\t' << player_y << '\n';
	f << "money\t" << gs.money << '\n';
	f << "heal\t" << gs.last_heal_map << '\t' << gs.last_heal_x << '\t' << gs.last_heal_y << '\n';
	f << "player\t" << (gs.female ? 1 : 0) << '\t' << gs.player_name << '\t' << gs.rival_name << '\n';
	f << "trainerid\t" << gs.trainer_id << '\t' << gs.secret_id << '\n';
	f << "options\t" << (gs.sound_on ? 1 : 0) << '\t' << (gs.battle_scene_on ? 1 : 0)
	  << '\t' << gs.frame_type << '\n';
	// AUFGABEN: only the player's own two choices -- which quest is pinned to
	// the HUD and whether the HUD box is shown. Quest progress itself is
	// derived from the flags/vars written below (see QuestLog.h), so there is
	// nothing else here to keep in sync.
	f << "quest\t" << (gs.tracked_quest.empty() ? "-" : gs.tracked_quest)
	  << '\t' << (gs.quest_hud_on ? 1 : 0) << '\n';
	f << "bike\t" << (gs.on_bike ? 1 : 0) << '\n';

	f << "vars\t" << gs.all_vars().size() << '\n';
	for (const auto& kv : gs.all_vars()) f << kv.first << '\t' << kv.second << '\n';

	f << "bag\t" << gs.bag_items().size() << '\n';
	for (const auto& kv : gs.bag_items()) f << kv.first << '\t' << kv.second << '\n';

	// Pokédex: every species that's at least been seen, flagged S (seen
	// only) or C (caught) -- one section covers both rather than needing
	// two, since a caught species is also seen.
	f << "dex\t" << (gs.pokedex_seen_set().size() + gs.pokedex_caught_set().size()) << '\n';
	for (const std::string& sp : gs.pokedex_seen_set()) f << sp << "\tS\n";
	for (const std::string& sp : gs.pokedex_caught_set()) f << sp << "\tC\n";

	f << "team\t" << team.size() << '\n';
	for (const Mon& m : team) write_mon(f, m);

	f << "box\t" << box.size() << '\n';
	for (const Mon& m : box) write_mon(f, m);

	return (bool)f;
}

bool SaveGame::save(const std::string& path, const GameState& gs, const PartySystem& party,
                    const std::string& map_path, int player_x, int player_y) {
	if (!save(path, gs, party.party_storage(), party.box_storage(), map_path,
	          player_x, player_y))
		return false;
	// The party system's own bookkeeping goes on the end of the same file:
	// the uid counter (so a mon created after loading can't reuse a saved
	// mon's id) plus which slot leads and which one walks with the player.
	std::ofstream f(path, std::ios::app);
	if (!f.is_open()) return false;
	f << "partystate\t" << party.next_uid() << '\t' << party.active_slot()
	  << '\t' << party.companion_slot() << '\n';
	return (bool)f;
}

bool SaveGame::load(const std::string& path, GameState& gs, PartySystem& party,
                    std::string& map_path, int& player_x, int& player_y) {
	std::vector<Mon> team, box;
	if (!load(path, gs, team, box, map_path, player_x, player_y)) return false;
	party.reset(std::move(team), std::move(box));

	// Re-read just the partystate line; the vector-shaped load above ignores
	// keys it doesn't know, which is what keeps the two formats one file.
	std::ifstream f(path);
	std::string line;
	while (std::getline(f, line)) {
		if (line.rfind("partystate\t", 0) != 0) continue;
		std::stringstream ss(line);
		std::string key, uid_s, active_s, companion_s;
		std::getline(ss, key, '\t');
		std::getline(ss, uid_s, '\t');
		std::getline(ss, active_s, '\t');
		std::getline(ss, companion_s, '\t');
		party.set_next_uid((unsigned)std::strtoul(uid_s.c_str(), nullptr, 10));
		party.set_active_slot(std::atoi(active_s.c_str()));
		party.set_companion_slot(std::atoi(companion_s.c_str()));
		break;
	}
	return true;
}

bool SaveGame::load(const std::string& path, GameState& gs,
                    std::vector<Mon>& team, std::vector<Mon>& box,
                    std::string& map_path, int& player_x, int& player_y) {
	std::ifstream f(path);
	if (!f.is_open()) return false;

	std::string line;
	if (!std::getline(f, line) || line.rfind("SAVE", 0) != 0) return false;

	GameState new_gs;
	std::vector<Mon> new_team, new_box;
	std::string new_map; int new_x = 0, new_y = 0;

	auto split_tab = [](const std::string& s) {
		std::vector<std::string> f;
		std::stringstream ss(s);
		std::string tok;
		while (std::getline(ss, tok, '\t')) f.push_back(tok);
		return f;
	};

	while (std::getline(f, line)) {
		if (line.empty()) continue;
		auto parts = split_tab(line);
		const std::string& key = parts[0];
		if (key == "map" && parts.size() >= 2) {
			new_map = parts[1];
		} else if (key == "pos" && parts.size() >= 3) {
			new_x = std::atoi(parts[1].c_str());
			new_y = std::atoi(parts[2].c_str());
		} else if (key == "money" && parts.size() >= 2) {
			new_gs.money = std::atoi(parts[1].c_str());
		} else if (key == "heal" && parts.size() >= 4) {
			new_gs.last_heal_map = parts[1];
			new_gs.last_heal_x = std::atoi(parts[2].c_str());
			new_gs.last_heal_y = std::atoi(parts[3].c_str());
		} else if (key == "player" && parts.size() >= 4) {
			new_gs.female = parts[1] != "0";
			new_gs.player_name = parts[2];
			new_gs.rival_name = parts[3];
		} else if (key == "trainerid" && parts.size() >= 3) {
			new_gs.trainer_id = (unsigned)std::strtoul(parts[1].c_str(), nullptr, 10);
			new_gs.secret_id = (unsigned)std::strtoul(parts[2].c_str(), nullptr, 10);
		} else if (key == "options" && parts.size() >= 4) {
			new_gs.sound_on = parts[1] != "0";
			new_gs.battle_scene_on = parts[2] != "0";
			new_gs.frame_type = std::atoi(parts[3].c_str());
		} else if (key == "quest" && parts.size() >= 3) {
			new_gs.tracked_quest = (parts[1] == "-") ? std::string() : parts[1];
			new_gs.quest_hud_on = parts[2] != "0";
		} else if (key == "bike" && parts.size() >= 2) {
			new_gs.on_bike = parts[1] != "0";
		} else if (key == "vars" && parts.size() >= 2) {
			int n = std::atoi(parts[1].c_str());
			for (int i = 0; i < n && std::getline(f, line); ++i) {
				auto kv = split_tab(line);
				if (kv.size() >= 2) new_gs.set_var(kv[0], std::atoi(kv[1].c_str()));
			}
		} else if (key == "bag" && parts.size() >= 2) {
			int n = std::atoi(parts[1].c_str());
			for (int i = 0; i < n && std::getline(f, line); ++i) {
				auto kv = split_tab(line);
				if (kv.size() >= 2) new_gs.give_item(kv[0], std::atoi(kv[1].c_str()));
			}
		} else if (key == "dex" && parts.size() >= 2) {
			int n = std::atoi(parts[1].c_str());
			for (int i = 0; i < n && std::getline(f, line); ++i) {
				auto kv = split_tab(line);
				if (kv.size() < 2) continue;
				if (kv[1] == "C") new_gs.mark_caught(kv[0]);
				else new_gs.mark_seen(kv[0]);
			}
		} else if (key == "team" && parts.size() >= 2) {
			int n = std::atoi(parts[1].c_str());
			for (int i = 0; i < n && std::getline(f, line); ++i) {
				Mon m;
				if (read_mon(line, m)) new_team.push_back(m);
			}
		} else if (key == "box" && parts.size() >= 2) {
			int n = std::atoi(parts[1].c_str());
			for (int i = 0; i < n && std::getline(f, line); ++i) {
				Mon m;
				if (read_mon(line, m)) new_box.push_back(m);
			}
		}
	}

	// An empty party is legitimate: a new game has none until Birch hands over
	// the starter on Route 101, and SPEICHERN is offered from the first frame.
	// The map line is the real integrity check -- also requiring a party here
	// silently rejected those saves, dropping the player into a new game.
	if (new_map.empty()) return false;

	gs = new_gs;
	team = new_team;
	box = new_box;
	map_path = new_map;
	player_x = new_x;
	player_y = new_y;
	return true;
}
