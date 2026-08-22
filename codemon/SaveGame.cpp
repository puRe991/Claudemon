#include "SaveGame.h"
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
	f << '\t' << (int)m.status << '\t' << m.status_turns << '\t' << m.confusion_turns << '\n';
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

	f << "vars\t" << gs.all_vars().size() << '\n';
	for (const auto& kv : gs.all_vars()) f << kv.first << '\t' << kv.second << '\n';

	f << "bag\t" << gs.bag_items().size() << '\n';
	for (const auto& kv : gs.bag_items()) f << kv.first << '\t' << kv.second << '\n';

	f << "team\t" << team.size() << '\n';
	for (const Mon& m : team) write_mon(f, m);

	f << "box\t" << box.size() << '\n';
	for (const Mon& m : box) write_mon(f, m);

	return (bool)f;
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

	if (new_map.empty() || new_team.empty()) return false;

	gs = new_gs;
	team = new_team;
	box = new_box;
	map_path = new_map;
	player_x = new_x;
	player_y = new_y;
	return true;
}
