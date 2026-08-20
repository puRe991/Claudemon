#include "BattleScene.h"

BattleScene::BattleScene() : active(false) {}

bool BattleScene::start(const std::string& species_name,
                        const std::string& asset_dir) {
	if (!this->mon_tex.loadFromFile(asset_dir + "/pokemon/" + species_name + ".png")) {
		return false;
	}
	this->mon_tex.setSmooth(false);
	this->species = species_name;
	this->active = true;
	return true;
}

void BattleScene::end() { this->active = false; }
bool BattleScene::is_active() const { return this->active; }
const std::string& BattleScene::current_species() const { return this->species; }

void BattleScene::draw(sf::RenderTarget& target) {
	if (!this->active) return;

	sf::View saved = target.getView();
	target.setView(target.getDefaultView());
	sf::Vector2f size = target.getView().getSize();

	// Sky and ground bands.
	sf::RectangleShape sky(sf::Vector2f(size.x, size.y));
	sky.setFillColor(sf::Color(168, 216, 224));
	target.draw(sky);
	sf::RectangleShape ground(sf::Vector2f(size.x, size.y * 0.42f));
	ground.setPosition(0, size.y * 0.5f);
	ground.setFillColor(sf::Color(120, 200, 120));
	target.draw(ground);

	// Enemy platform (a flat ellipse) in the upper-right.
	sf::CircleShape platform(90.f);
	platform.setScale(1.7f, 0.5f);
	platform.setOrigin(90.f, 90.f);
	platform.setPosition(size.x * 0.66f, size.y * 0.40f);
	platform.setFillColor(sf::Color(96, 176, 96));
	target.draw(platform);

	// Wild pokemon sprite, scaled up and standing on the platform.
	sf::Sprite mon(this->mon_tex);
	float scale = 3.2f;
	mon.setScale(scale, scale);
	sf::Vector2u ts = this->mon_tex.getSize();
	mon.setPosition(size.x * 0.66f - ts.x * scale * 0.5f,
	                size.y * 0.40f - ts.y * scale * 0.72f);
	target.draw(mon);

	target.setView(saved);
}
