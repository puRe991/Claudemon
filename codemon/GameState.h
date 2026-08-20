#pragma once
#include <string>
#include <unordered_map>

/******************************************************************************
GameState - persistent flags, variables and bag shared across maps.

pokeemerald scripts read and write named flags (FLAG_*) and variables (VAR_*)
and hand items to the player. Those live here so state survives map changes and
conditional dialog behaves (e.g. a shopkeeper who talks differently the second
time). Flags are stored as 0/1 variables in the same table.
*****************************************************************************/
class GameState
{
private:
	std::unordered_map<std::string, int> vars;   // FLAG_* and VAR_*
	std::unordered_map<std::string, int> bag;     // ITEM_* -> count

public:
	int get_var(const std::string& name) const {
		auto it = vars.find(name);
		return it == vars.end() ? 0 : it->second;
	}
	void set_var(const std::string& name, int value) { vars[name] = value; }

	bool flag(const std::string& name) const { return get_var(name) != 0; }
	void set_flag(const std::string& name) { vars[name] = 1; }
	void clear_flag(const std::string& name) { vars[name] = 0; }

	void give_item(const std::string& item, int amount) { bag[item] += amount; }
	int item_count(const std::string& item) const {
		auto it = bag.find(item);
		return it == bag.end() ? 0 : it->second;
	}
	const std::unordered_map<std::string, int>& bag_items() const { return bag; }
};
