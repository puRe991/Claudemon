#include "MenuModel.h"

MenuModel::MenuModel()
	: m_selected(0)
{
}

void MenuModel::add_item(const std::string& label, MenuAction action, bool enabled)
{
	this->m_items.push_back(Item{ label, action, enabled });
	this->ensure_valid();
}

unsigned int MenuModel::selected() const
{
	return this->m_selected;
}

const std::vector<MenuModel::Item>& MenuModel::items() const
{
	return this->m_items;
}

void MenuModel::move_up()   { this->step(-1); }
void MenuModel::move_down() { this->step(+1); }

void MenuModel::step(int dir)
{
	const int n = static_cast<int>(this->m_items.size());
	if (n == 0) {
		return;
	}

	// Walk in the requested direction until we land on an enabled item.
	for (int i = 1; i <= n; ++i) {
		int idx = (static_cast<int>(this->m_selected) + dir * i) % n;
		if (idx < 0) {
			idx += n;
		}
		if (this->m_items[idx].enabled) {
			this->m_selected = static_cast<unsigned int>(idx);
			return;
		}
	}
	// No enabled item anywhere: leave the selection unchanged.
}

MenuAction MenuModel::confirm() const
{
	if (this->m_items.empty()) {
		return MenuAction::None;
	}
	const Item& item = this->m_items[this->m_selected];
	return item.enabled ? item.action : MenuAction::None;
}

void MenuModel::ensure_valid()
{
	if (this->m_items.empty()) {
		this->m_selected = 0;
		return;
	}
	if (this->m_selected < this->m_items.size() && this->m_items[this->m_selected].enabled) {
		return;
	}
	// Current selection is disabled/out of range: snap to the first enabled item.
	for (unsigned int i = 0; i < this->m_items.size(); ++i) {
		if (this->m_items[i].enabled) {
			this->m_selected = i;
			return;
		}
	}
	this->m_selected = 0;
}
