#pragma once
#include "SFML/Graphics.hpp"
#include <algorithm>
#include <cmath>
#include "ScriptVM.h"

// The Pokemon Center nurse's healing animation (pokeemerald's
// FLDEFF_POKECENTER_HEAL, src/field_effect.c CreateGlowingPokeballsEffect +
// CreatePokecenterMonitorSprite): one small Poke Ball per party member
// glows in front of the counter while the healing machine's screen blinks.
// ScriptVM blocks on `dofieldeffect FLDEFF_POKECENTER_HEAL` and times itself
// out (ScriptVM::HEALFX_DURATION); this just needs to look busy for exactly
// that long, drawn in world space so it stays put next to the player like
// any other actor.
struct HealFx {
    bool active_ = false;
    float t = 0.f;
    int n_balls = 1;
    sf::Texture ball_tex, mon0_tex, mon1_tex;
    bool ok = false;

    void load() {
        ok = ball_tex.loadFromFile("assets/graphics/field_effects/pics/pokeball_glow.png");
        ok = mon0_tex.loadFromFile("assets/graphics/field_effects/pics/pokecenter_monitor/0.png") && ok;
        ok = mon1_tex.loadFromFile("assets/graphics/field_effects/pics/pokecenter_monitor/1.png") && ok;
        ball_tex.setSmooth(false); mon0_tex.setSmooth(false); mon1_tex.setSmooth(false);
    }
    void start(int party_size) {
        active_ = true; t = 0.f;
        n_balls = std::max(1, std::min(6, party_size));
    }
    bool active() const { return active_; }
    void tick(float dt) {
        if (!active_) return;
        t += dt;
        if (t >= ScriptVM::HEALFX_DURATION) active_ = false;
    }

    // Centered above the world pixel position (wx,wy) -- call while the
    // target's view is still the world camera (i.e. from inside draw_scene).
    void draw(sf::RenderTarget& target, float wx, float wy) const {
        if (!active_ || !ok) return;
        const float scale = 2.5f;
        bool bright = std::fmod(t, 0.3f) < 0.15f;
        float bw = 8.f * scale;
        float total = n_balls * bw;
        for (int i = 0; i < n_balls; ++i) {
            sf::Sprite s(ball_tex);
            s.setScale(scale, scale);
            s.setColor(bright ? sf::Color::White : sf::Color(255, 255, 255, 110));
            s.setPosition(wx - total / 2.f + i * bw, wy - 30.f);
            target.draw(s);
        }
        sf::Sprite ms(bright ? mon1_tex : mon0_tex);
        ms.setScale(scale, scale);
        ms.setPosition(wx - 30.f, wy - 54.f);
        target.draw(ms);
    }
};
