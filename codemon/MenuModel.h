#pragma once
#include <string>
#include <vector>

/******************************************************************************
MenuModel - display-independent main-menu navigation.

Holds the list of menu items and which one is selected, with wrap-around
navigation that skips disabled items (e.g. "Continue" when there is no save).
No SFML dependency, so it can be unit tested on its own.
******************************************************************************/

enum class MenuAction {
	None,
	NewGame,
	Continue,
	Options,
	Quit
};

class MenuModel
{
public:
	struct Item {
		std::string label;
		MenuAction  action;
		bool        enabled;
	};

	MenuModel();

	//Append an item. Disabled items are shown but skipped during navigation.
	void add_item(const std::string& label, MenuAction action, bool enabled = true);

	/* Navigation - wraps around and skips disabled items */
	void move_up();
	void move_down();

	//Action of the currently selected item (None if it is disabled/empty).
	MenuAction confirm() const;

	unsigned int selected() const;
	const std::vector<Item>& items() const;

private:
	std::vector<Item> m_items;
	unsigned int      m_selected;

	void step(int dir);       // move by dir (+1/-1), wrapping, skipping disabled
	void ensure_valid();      // keep the selection on an enabled item if possible
};
