#include <SFML/Graphics.hpp>

#include "character.h"
#include "map.h"
#include "Window.h"
#include "direction.h"
#include "Audio.h"
#include "DialogBox.h"
#include "Battle.h"
#include "BattleData.h"
#include "GameState.h"
#include "ScriptVM.h"
#include "Menu.h"
#include "Minigame.h"
#include "UiFrame.h"
#include "SaveGame.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

static const int SCALE = 3;
static const float NPC_TICK = 0.45f;
// Seconds between overworld steps while a direction is held (interactive
// play only); Character::MOVE_DURATION is kept a little under this so one
// slide always finishes before the next step is allowed to start.
static const float MOVE_INTERVAL = 0.15f;
// Running Shoes (FLAG_SYS_B_DASH): the real games' hold-B-to-run, adapted to
// PC as held Shift -- exactly half MOVE_INTERVAL, the same 2x speed as the
// original (paired with Character::RUN_MOVE_DURATION for the slide itself).
static const float RUN_MOVE_INTERVAL = MOVE_INTERVAL / 2.f;
// Fixed camera viewport in tiles (so the window size is independent of the map).
static const int VIEW_TW = 16, VIEW_TH = 12;
// Headless/screenshot mode wants exact, deterministic tile-snapped rendering
// (see Character::set_animated) instead of the smooth interactive slide.
static const bool g_headless = std::getenv("CODEMON_SCREENSHOT") != nullptr;
static const char* PLAYER_SHEET = "assets/overworld/people_brendan_walking.png";

// "maps/LittlerootTown.map" -> "Littleroot Town", "Route102" -> "Route 102".
static std::string pretty_map(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string s = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = s.rfind(".map");
    if (dot != std::string::npos) s = s.substr(0, dot);
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (c == '_') { out += ' '; continue; }
        bool up = std::isupper((unsigned char)c), dg = std::isdigit((unsigned char)c);
        if (i > 0 && (up || (dg && !std::isdigit((unsigned char)s[i - 1]))) &&
            out.size() && out.back() != ' ')
            out += ' ';
        out += c;
    }
    return out;
}

// Draw a map-name banner at the top; alpha fades with `t` (seconds remaining).
static void draw_banner(sf::RenderTarget& target, const sf::Font& font,
                        const std::string& name, float t) {
    if (t <= 0.f || name.empty()) return;
    sf::View saved = target.getView();
    target.setView(target.getDefaultView());

    sf::Uint8 a = (sf::Uint8)(std::min(1.f, t) * 235);
    sf::RectangleShape box(sf::Vector2f(280, 46));
    box.setPosition(18, 14);
    box.setFillColor(sf::Color(20, 28, 48, a));
    box.setOutlineColor(sf::Color(245, 245, 245, a)); box.setOutlineThickness(2.f);
    target.draw(box);
    sf::Text txt(sf::String::fromUtf8(name.begin(), name.end()), font, 22);
    txt.setPosition(32, 24); txt.setFillColor(sf::Color(255, 255, 255, a));
    target.draw(txt);
    target.setView(saved);
}

struct Agent {
    Character* ch;
    DIR pace_dir;
    int home_x, home_y;
    std::string sheet;    // for deriving a speaker name
    std::string dialog;
    // Index into Map::npcs() this agent was built from. Not the same as the
    // agent's own index: flag-hidden objects and sprites that fail to load are
    // skipped, which shifts everything after them -- looking a script up by the
    // agent index handed later NPCs somebody else's script.
    int npc_index = -1;
    // Trainer sight range in tiles (0 = not a trainer), copied from NpcSpawn.
    int sight = 0;
};

// "people_old_man" / "people_brendan_walking" -> "Old Man" / "Brendan"
static std::string speaker_name(const std::string& sheet) {
    std::string s = sheet;
    size_t us = s.find('_');
    if (us != std::string::npos) s = s.substr(us + 1);          // drop group
    const std::string suffix = "_walking";
    if (s.size() > suffix.size() && s.compare(s.size() - suffix.size(),
        suffix.size(), suffix) == 0)
        s = s.substr(0, s.size() - suffix.size());
    std::string out;
    bool cap = true;
    for (char c : s) {
        if (c == '_') { out += ' '; cap = true; }
        else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
        else out += c;
    }
    return out;
}

static DIR opposite(DIR d) {
    switch (d) { case DIR::N: return DIR::S; case DIR::S: return DIR::N;
                 case DIR::E: return DIR::W; case DIR::W: return DIR::E;
                 default: return DIR::S; }
}

// Everything tied to the currently-loaded map.
struct Session {
    std::string path;
    Map* map = nullptr;
    Character* player = nullptr;
    std::vector<Agent> agents;
    std::vector<Character*> npc_chars;
    std::vector<Character*> actors;   // npcs + player, for draw/collision
    // pokeemerald LOCALID_* -> its Character, for scripted cutscenes that
    // address a specific object event by name (`applymovement`/`addobject`/
    // `removeobject`/`hideobject`/`showobject`) rather than "the NPC just
    // talked to" -- see ScriptVM's resolve(). Only NPCs porymap actually
    // named get an entry (NpcSpawn::local_id), most don't.
    std::unordered_map<std::string, Character*> localid_map;
};

static bool actor_at(const std::vector<Character*>& actors, Character* self,
                     int tx, int ty) {
    for (Character* a : actors) {
        if (a == self) continue;
        if (a->get_tile_x() == tx && a->get_tile_y() == ty) return true;
    }
    return false;
}

static void free_session(Session* s) {
    if (!s) return;
    for (Character* n : s->npc_chars) delete n;
    delete s->player;
    delete s->map;
    delete s;
}

// Load a map and build the player + NPC agents. arrival<0 means use the map's
// own start position.
static Session* load_session(const std::string& path, int arr_x, int arr_y,
                             GameState* gs) {
    Session* s = new Session();
    s->path = path;
    s->map = new Map(path);
    // pokeemerald's MAP_SCRIPT_ON_TRANSITION unconditionally sets this map's
    // own FLAG_VISITED_* the instant you step onto it (used by the FLIEGEN
    // destination list, see Menu.cpp) -- runs every entry, not just once, but
    // GameState::set_flag is idempotent so that's harmless.
    if (gs && !s->map->visit_flag().empty()) gs->set_flag(s->map->visit_flag());
    int px = (arr_x >= 0) ? arr_x : (int)s->map->get_start_pos().get_x();
    int py = (arr_y >= 0) ? arr_y : (int)s->map->get_start_pos().get_y();

    s->player = new Character(px, py);
    s->player->load_sprite_sheet(PLAYER_SHEET);
    s->player->face(DIR::S);
    s->player->set_animated(!g_headless);

    int npc_idx = -1;
    for (const NpcSpawn& sp : s->map->npcs()) {
        ++npc_idx;
        // pokeemerald FLAG_HIDE_*: this object doesn't exist yet/anymore
        // (not met, already given away, story hasn't reached it, ...) --
        // skip it entirely, UNLESS a script might still `addobject` it later
        // by LOCALID (a Wally-style cutscene NPC also starts flag-hidden,
        // but its Character still needs to exist for that to work).
        bool hidden = !sp.hide_flag.empty() && gs && gs->flag(sp.hide_flag);
        if (hidden && sp.local_id.empty()) continue;
        Character* ch = new Character(sp.x, sp.y);
        if (!ch->load_sprite_sheet("assets/overworld/" + sp.sheet + ".png")) {
            delete ch; continue;
        }
        ch->set_animated(!g_headless);
        ch->set_hide_flag(sp.hide_flag);
        ch->face(sp.facing);
        ch->set_move_kind((int)sp.movement);
        Agent ag; ag.ch = ch;
        ag.home_x = sp.x; ag.home_y = sp.y;
        ag.pace_dir = (sp.movement == MOVE_PACE_H) ? DIR::E : DIR::N;
        ag.sheet = sp.sheet; ag.dialog = sp.dialog;
        ag.npc_index = npc_idx; ag.sight = sp.sight;
        s->agents.push_back(ag);
        s->npc_chars.push_back(ch);
        if (!sp.local_id.empty()) s->localid_map[sp.local_id] = ch;
        if (!hidden) s->actors.push_back(ch);
    }
    s->actors.push_back(s->player);
    return s;
}

static bool try_step(Agent& ag, DIR dir, Map& map, std::vector<Character*>& actors) {
    int tx, ty;
    ag.ch->set_facing(dir);
    ag.ch->target_tile(dir, tx, ty);
    if (map.passable(tx, ty) && !actor_at(actors, ag.ch, tx, ty)) {
        ag.ch->step(dir); return true;
    }
    ag.ch->face(dir); return false;
}

static void tick_npcs(Session* s, std::mt19937& rng) {
    static const DIR dirs[4] = {DIR::N, DIR::S, DIR::E, DIR::W};
    for (Agent& ag : s->agents) {
        if (ag.ch->is_removed()) continue;   // cut down / smashed this session
        // Read from the Character, not ag.kind -- `setobjectmovementtype`
        // (ScriptVM) can change it live and only has the Character*.
        switch ((MoveKind)ag.ch->get_move_kind()) {
        case MOVE_WANDER: {
            DIR d = dirs[rng() % 4];
            int tx, ty; ag.ch->target_tile(d, tx, ty);
            if (std::abs(tx - ag.home_x) <= 2 && std::abs(ty - ag.home_y) <= 2)
                try_step(ag, d, *s->map, s->actors);
            else ag.ch->face(d);
            break;
        }
        case MOVE_PACE_V:
            if (!try_step(ag, ag.pace_dir, *s->map, s->actors))
                { ag.pace_dir = (ag.pace_dir == DIR::N) ? DIR::S : DIR::N;
                  try_step(ag, ag.pace_dir, *s->map, s->actors); }
            break;
        case MOVE_PACE_H:
            if (!try_step(ag, ag.pace_dir, *s->map, s->actors))
                { ag.pace_dir = (ag.pace_dir == DIR::E) ? DIR::W : DIR::E;
                  try_step(ag, ag.pace_dir, *s->map, s->actors); }
            break;
        default: break;
        }
    }
}

static bool team_knows_move(const std::vector<Mon>& team, const std::string& move) {
    for (const Mon& m : team)
        if (std::find(m.moves.begin(), m.moves.end(), move) != m.moves.end()) return true;
    return false;
}

// Dive/emerge (pokeemerald's TrySetDiveWarp, src/field_control_avatar.c):
// pressing A while surfing over deep water that has an underwater map below it
// takes the player down; pressing A on an underwater map surfaces again. Both
// keep the player's exact x/y -- the two maps are the same size and lie on top
// of each other -- so no arrival tile is needed.
//
// Sootopolis City has no land route, no warp and no map connection from
// outside: diving in Route 126 and surfacing inside it is the only way in, so
// without this the 8th gym, the Cave of Origin and the story's whole endgame
// were unreachable.
//
// Returned string is the destination map name, or "" if diving isn't possible
// here. Gated on the 7th badge like the real game, plus a party member that
// actually knows the move (this engine's badge-free HM convention for the
// others is deliberately tightened here, since FLAG_BADGE07_GET is exactly
// what the real check uses).
static std::string dive_target(Session* s, const std::vector<Mon>& team,
                               const GameState& gs, int& out_x, int& out_y) {
    out_x = out_y = -1;   // -1/-1 = keep the player's current tile
    if (!gs.flag("FLAG_BADGE07_GET") || !team_knows_move(team, "DIVE"))
        return std::string();
    if (!s->map->emerge_dest().empty()) return s->map->emerge_dest();   // surfacing
    // A fixed setdivewarp overrides both, and brings its own arrival tile. It
    // works in both directions: Sootopolis City sets one pointing down, and
    // Underwater_SootopolisCity sets one pointing back up.
    if (!s->map->divewarp_dest().empty()) {
        const bool on_water = s->player->is_surfing();
        const bool diveable_here =
            s->map->is_diveable(s->player->get_tile_x(), s->player->get_tile_y());
        // Underwater maps have no diveable tiles of their own -- being there at
        // all is enough to surface; on the surface the player has to be over
        // deep water.
        if (!on_water || diveable_here || s->map->dive_dest().empty()) {
            out_x = s->map->divewarp_x();
            out_y = s->map->divewarp_y();
            return s->map->divewarp_dest();
        }
    }
    if (s->player->is_surfing() &&
        s->map->is_diveable(s->player->get_tile_x(), s->player->get_tile_y()))
        return s->map->dive_dest();
    return std::string();
}

// Move the player one tile if possible; then, if the destination tile is a
// warp, load the target map and place the player at the arrival warp. Returns
// the (possibly new) session.
// `surf_confirmed`: the caller already ran the Surf Ja/Nein prompt (see the
// `pending_surf` handling below) and the player said yes -- treat the
// target water tile as enterable and start surfing on it. `needs_surf`
// (optional out): set
// true instead of bumping when the *only* reason this step was refused is
// unconfirmed water, so the caller knows to show that prompt (with the
// player already faced towards it either way). `waterfall_confirmed`/
// `needs_waterfall` are the same pattern for climbing a waterfall (moving
// north into an MB_WATERFALL tile while already surfing).
static Session* player_step(Session* s, DIR dir, Audio* audio, GameState* gs,
                            bool surf_confirmed = false, bool* needs_surf = nullptr,
                            bool waterfall_confirmed = false, bool* needs_waterfall = nullptr) {
    if (needs_surf) *needs_surf = false;
    if (needs_waterfall) *needs_waterfall = false;
    bool can_surf = s->player->is_surfing() || surf_confirmed;
    int tx, ty;
    s->player->target_tile(dir, tx, ty);

    // Off this map's edge: pokeemerald maps aren't warps at their borders,
    // they're stitched together into one seamless world via `connections`
    // (Route101's north edge continues straight into OldaleTown, etc.). If
    // this map's edge in `dir` has one, cross into it instead of just
    // stopping at the boundary.
    if (!s->map->in_bounds(tx, ty)) {
        const Connection* cn = s->map->connection_for(dir);
        if (cn) {
            Session* ns = load_session("maps/" + cn->dest + ".map", -1, -1, gs);
            if (ns->map->ready()) {
                int nx = tx, ny = ty;
                switch (dir) {
                case DIR::N: nx = tx - cn->offset; ny = (int)ns->map->get_height() - 1; break;
                case DIR::S: nx = tx - cn->offset; ny = 0; break;
                case DIR::W: nx = (int)ns->map->get_width() - 1; ny = ty - cn->offset; break;
                case DIR::E: nx = 0; ny = ty - cn->offset; break;
                default: break;
                }
                bool in_b = ns->map->in_bounds(nx, ny);
                bool water = in_b && ns->map->is_water(nx, ny);
                bool clear = in_b && ns->map->passable(nx, ny) &&
                            !actor_at(ns->actors, ns->player, nx, ny);
                if (clear && (!water || can_surf)) {
                    ns->player->set_tile(nx, ny);
                    ns->player->set_facing(dir);
                    // Carry Surf across the map boundary if the new tile is
                    // still water (Route 126/127/128's ocean chain, ...);
                    // dismount right here if it's dry land on the other side.
                    ns->player->set_surfing(can_surf && water);
                    free_session(s);
                    return ns;
                }
                if (clear && water && needs_surf) *needs_surf = true;
            }
            free_session(ns);
        }
        s->player->face(dir);
        if (audio && !(needs_surf && *needs_surf)) audio->play_bump();
        return s;
    }

    // One-way ledges (pokeemerald's MB_JUMP_*): crossable only in their own
    // direction, where stepping onto one hops two tiles in a single motion;
    // from any other side (including trying to climb back up it) it's solid,
    // regardless of the raw collision layer.
    DIR ledge = s->map->ledge_dir(tx, ty);
    if (ledge != DIR::NONE) {
        s->player->face(dir);
        if (ledge != dir) { if (audio) audio->play_bump(); return s; }
        int lx, ly;
        Character tmp(tx, ty);
        tmp.target_tile(dir, lx, ly);
        bool land_clear = s->map->in_bounds(lx, ly) && s->map->passable(lx, ly) &&
                          !s->map->is_water(lx, ly) && !actor_at(s->actors, s->player, lx, ly);
        if (!land_clear) { if (audio) audio->play_bump(); return s; }
        s->player->jump(dir);
        return s;
    }

    // Warp/door tiles are impassable metatiles but can be walked onto: the warp
    // overrides collision (that is how doors work in pokeemerald). Surfable
    // water is collision-passable already (see Map::is_water), but entering
    // it still needs Surf, confirmed via the same Ja/Nein prompt as above.
    const Warp* target_warp = s->map->warp_at(tx, ty);
    bool water = s->map->is_water(tx, ty);
    bool blocked_by_actor = actor_at(s->actors, s->player, tx, ty);
    // Strength: walking into a boulder pushes it one tile further in the same
    // direction instead of just bumping (pokeemerald's TryPushBoulder in
    // field_player_avatar.c). The player doesn't move on this same step --
    // COLLISION_PUSHED_BOULDER blocks that attempt while the boulder itself
    // slides forward; a normal step onto the now-empty tile follows next.
    if (blocked_by_actor && gs->flag("FLAG_SYS_USE_STRENGTH")) {
        for (Agent& ag : s->agents) {
            if (ag.ch->is_removed()) continue;
            if (ag.ch->get_tile_x() != tx || ag.ch->get_tile_y() != ty) continue;
            if (ag.sheet != "misc_pushable_boulder") break;
            int bx, by; ag.ch->target_tile(dir, bx, by);
            if (s->map->in_bounds(bx, by) && s->map->passable(bx, by) &&
                !s->map->is_water(bx, by) && !actor_at(s->actors, ag.ch, bx, by)) {
                ag.ch->step(dir);
            } else if (audio) audio->play_bump();
            break;
        }
        s->player->face(dir);
        return s;
    }
    // Waterfall: like water, collision-passable already but gated -- and
    // only when climbing (moving north into it) while already surfing,
    // matching pokeemerald's IsPlayerSurfingNorth() check. Any other
    // direction (floating down/across its base) is ordinary open water.
    bool waterfall = dir == DIR::N && s->player->is_surfing() && s->map->is_waterfall(tx, ty);
    bool can_waterfall = waterfall_confirmed;
    bool clear = (s->map->passable(tx, ty) || target_warp) && !blocked_by_actor;
    if (!clear || (water && !can_surf) || (waterfall && !can_waterfall)) {
        s->player->face(dir);
        if (clear && water && needs_surf) *needs_surf = true;
        else if (clear && waterfall && needs_waterfall) *needs_waterfall = true;
        else if (audio) audio->play_bump();
        return s;
    }
    s->player->step(dir);
    if (water) s->player->set_surfing(true);
    // Reaching dry land automatically ends a Surf, same as the real games.
    if (s->player->is_surfing() &&
        !s->map->is_water(s->player->get_tile_x(), s->player->get_tile_y()))
        s->player->set_surfing(false);

    const Warp* wp = s->map->warp_at(s->player->get_tile_x(), s->player->get_tile_y());
    if (wp && wp->dest != "-") {
        Session* ns = load_session("maps/" + wp->dest + ".map", -1, -1, gs);
        if (ns->map->ready()) {
            const Warp* dw = ns->map->warp_by_index(wp->dest_warp);
            if (dw) ns->player->set_tile(dw->x, dw->y);
            free_session(s);
            return ns;
        }
        free_session(ns);   // destination not available; stay put
    } else if (wp && wp->dest == "-" && !gs->dynamic_warp_map.empty()) {
        // WARP_ID_DYNAMIC (real pokeemerald): this warp tile's destination
        // isn't fixed in the map data -- it's whatever the last `setdynamicwarp`
        // recorded (the intro moving truck's exit, the department store
        // elevator's floor select). Landing spot is the exact x/y that
        // recorded, not another warp's arrival tile.
        Session* ns = load_session("maps/" + gs->dynamic_warp_map + ".map", -1, -1, gs);
        if (ns->map->ready()) {
            ns->player->set_tile(gs->dynamic_warp_x, gs->dynamic_warp_y);
            free_session(s);
            return ns;
        }
        free_session(ns);
    }
    return s;
}

// Talk to whatever the player is facing. If a dialog is already open, this
// advances/closes it. Returns true if something happened.
// Find and talk to whichever NPC occupies (tx,ty); returns true if one did.
static bool talk_to_npc_at(Session* s, DialogBox& box, Audio* audio, ScriptVM& vm,
                           int tx, int ty) {
    for (size_t i = 0; i < s->agents.size(); ++i) {
        Agent& ag = s->agents[i];
        if (ag.ch->is_removed()) continue;   // cut down / smashed this session
        if (ag.ch->get_tile_x() == tx && ag.ch->get_tile_y() == ty) {
            ag.ch->face(opposite(s->player->get_facing()));   // turn to the player
            if (audio) audio->play_select();
            std::string label = s->map->npc_script(ag.npc_index);
            if (!label.empty() && s->map->has_script(label)) {
                vm.start(label, ag.ch);                        // run its event script
            } else {
                box.open(speaker_name(ag.sheet),
                         ag.dialog.empty() ? "..." : ag.dialog);
            }
            return true;
        }
    }
    return false;
}

static bool interact(Session* s, DialogBox& box, Audio* audio, ScriptVM& vm) {
    if (box.is_active()) { box.advance(); return true; }   // next page / dismiss
    int tx, ty;
    DIR facing = s->player->get_facing();
    s->player->target_tile(facing, tx, ty);
    // 1) an NPC on the faced tile
    if (talk_to_npc_at(s, box, audio, vm, tx, ty)) return true;
    // 1b) a shop/PC counter: the clerk/nurse stands one tile past it, out
    // of normal reach since the counter itself blocks movement (pokeemerald
    // looks through counters the same way -- MetatileBehavior_IsCounter in
    // field_control_avatar.c).
    if (s->map->is_counter(tx, ty)) {
        int cx, cy;
        Character tmp(tx, ty);
        tmp.target_tile(facing, cx, cy);
        if (talk_to_npc_at(s, box, audio, vm, cx, cy)) return true;
    }
    // 2) a readable sign on the faced tile
    const Sign* sg = s->map->sign_at(tx, ty);
    if (sg) {
        box.open("", sg->text);
        if (audio) audio->play_select();
        return true;
    }
    return false;
}

// pokeemerald's MAP_SCRIPT_ON_FRAME_TABLE: on entering a map, run any
// var-gated one-time setup script (e.g. Route 101 advancing
// VAR_ROUTE101_STATE 0->1, which the Birch-rescue coord trigger waits on).
// These are simple setflag/setvar scripts with no blocking ops, so running
// them inline to completion is safe.
static void run_load_triggers(Map* map, GameState& gs, ScriptVM& vm) {
    for (const LoadTrigger& t : map->on_load_triggers()) {
        if (gs.get_var(t.var) == std::atoi(t.val.c_str()))
            vm.start(t.label, nullptr);
    }
}

// After a real step, run a coord_event trigger if the player is on one and its
// variable condition matches.
// A trainer challenges the player the moment they step into its line of sight
// (pokeemerald's GetTrainerApproachDistance in trainer_see.c): straight along
// the direction the trainer is facing, 1..sight tiles away, on the same row or
// column, with nothing solid in between. Real TRAINER_TYPE_SEE_ALL_DIRECTIONS
// trainers also look sideways; this models the ordinary one-direction case that
// covers nearly every trainer in the game.
static bool check_trainer_sight(Session* s, ScriptVM& vm) {
    const int px = s->player->get_tile_x(), py = s->player->get_tile_y();
    for (Agent& ag : s->agents) {
        if (ag.sight <= 0 || ag.ch->is_removed()) continue;
        // Only actually spawned NPCs can see (a flag-hidden trainer isn't there).
        if (std::find(s->actors.begin(), s->actors.end(), ag.ch) == s->actors.end()) continue;

        const bool spotted = trainer_can_see(
            *s->map, ag.ch->get_tile_x(), ag.ch->get_tile_y(),
            ag.ch->get_facing(), ag.sight, px, py,
            [&](int cx, int cy) { return actor_at(s->actors, s->player, cx, cy); });
        if (!spotted) continue;

        const std::string label = s->map->npc_script(ag.npc_index);
        if (!vm.script_has_pending_trainer(label)) continue;   // already beaten
        // No re-facing needed: the trainer is already looking down the ray it
        // just spotted the player on.
        vm.start(label, ag.ch);
        return true;
    }
    return false;
}

static void check_trigger(Session* s, ScriptVM& vm, GameState& gs) {
    if (vm.running()) return;
    const ScriptTrigger* t = s->map->trigger_at(s->player->get_tile_x(),
                                                s->player->get_tile_y());
    if (t && s->map->has_script(t->label)) {
        int want = (!t->val.empty() && (std::isdigit((unsigned char)t->val[0])))
                       ? std::atoi(t->val.c_str()) : gs.get_var(t->val);
        if (gs.get_var(t->var) == want) { vm.start(t->label, nullptr); return; }
    }
    // Coord events win over a trainer standing in the same spot, matching
    // pokeemerald's own order in field_control_avatar.c.
    check_trainer_sight(s, vm);
}

// After a real step, maybe start a wild encounter if standing in tall grass.
static void try_encounter(Session* s, Battle& battle, Mon& party,
                          std::mt19937& rng, bool force) {
    if (std::getenv("CODEMON_NO_WILD")) return;    // for scripted trainer demos
    int px = s->player->get_tile_x(), py = s->player->get_tile_y();
    if (!s->map->encounter_here(px, py)) return;
    if (!force && (rng() % 100) >= 22) return;                 // ~22% per step
    std::string sp; int level;
    if (s->map->roll_encounter(rng, px, py, sp, level))         // real species + level
        battle.start_wild(sp, level, &party);
}

// A camera that follows the player, clamped to the map bounds.
static sf::View camera_for(Session* s) {
    int tp = s->map->get_tile_size();
    float vw = (float)(VIEW_TW * tp), vh = (float)(VIEW_TH * tp);
    float worldw = (float)(s->map->get_width() * tp);
    float worldh = (float)(s->map->get_height() * tp);
    float cx = s->player->interp_x(tp) + tp * 0.5f;
    float cy = s->player->interp_y(tp) + tp * 0.5f;
    // clamp so we never show outside the map (unless the map is smaller than view)
    if (worldw >= vw) cx = std::max(vw / 2, std::min(cx, worldw - vw / 2));
    else              cx = worldw / 2;
    if (worldh >= vh) cy = std::max(vh / 2, std::min(cy, worldh - vh / 2));
    else              cy = worldh / 2;
    sf::View v(sf::FloatRect(0, 0, vw, vh));
    v.setCenter(cx, cy);
    return v;
}

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

static void draw_scene(sf::RenderTarget& target, Session* s, const HealFx* healfx = nullptr,
                        const ScriptVM* vm = nullptr) {
    target.setView(camera_for(s));
    s->map->render_to(target);
    std::sort(s->actors.begin(), s->actors.end(),
              [](Character* a, Character* b) {
                  if (a->get_tile_y() != b->get_tile_y())
                      return a->get_tile_y() < b->get_tile_y();
                  // Same row: `setobjectsubpriority` breaks the tie (Mr.
                  // Briney's boat scenes); unset (-1) sorts as if it were the
                  // lowest value, matching pre-opcode behaviour for every
                  // object nobody has scripted a subpriority for.
                  return a->get_subpriority() < b->get_subpriority();
              });
    for (Character* a : s->actors) {
        a->update_sprite(s->map->get_tile_size());
        target.draw(*a->get_current_sprite());
    }
    // `opendoor`/`closedoor`/`waitdooranim` (see ScriptVM.h's door_anim_active()
    // comment): overlay the matching frame of the imported door sheet on top
    // of the door tile. Loaded once and reused for every door in every map --
    // this engine has no per-location door-graphics table, so it's always
    // the "general" sheet, the most common one in the original game.
    if (vm && vm->door_anim_active() && vm->door_frame() >= 0) {
        static sf::Texture door_tex;
        static bool door_tex_ok = door_tex.loadFromFile("assets/graphics/door_anims/general.png");
        if (door_tex_ok) {
            int tp = s->map->get_tile_size();
            int dx, dy; vm->get_door_tile(dx, dy);
            sf::Sprite spr(door_tex);
            spr.setTextureRect(sf::IntRect(0, vm->door_frame() * 16, 16, 16));
            spr.setPosition((float)(dx * tp), (float)(dy * tp));
            if (tp != 16) spr.setScale((float)tp / 16.f, (float)tp / 16.f);
            target.draw(spr);
        }
    }
    // `fadescreen`/`fadescreenswapbuffers`: a full-screen black/white overlay
    // whose alpha ScriptVM animates over FADE_DURATION; see fade_active()'s
    // comment for what this does and doesn't replicate from the original.
    if (vm && vm->fade_active()) {
        sf::RectangleShape overlay(target.getView().getSize());
        overlay.setPosition(target.getView().getCenter() - target.getView().getSize() / 2.f);
        sf::Uint8 a = (sf::Uint8)std::clamp(vm->fade_alpha(), 0.f, 255.f);
        sf::Color c = vm->fade_white() ? sf::Color::White : sf::Color::Black;
        overlay.setFillColor(sf::Color(c.r, c.g, c.b, a));
        target.draw(overlay);
    }
    // `dofieldeffectsparkle` (see ScriptVM.h's Sparkle comment): a small
    // procedural star burst, since this effect has no imported sprite sheet.
    if (vm) {
        int tp = s->map->get_tile_size();
        for (const ScriptVM::Sparkle& sp : vm->sparkles()) {
            float p = sp.t / ScriptVM::SPARKLE_DURATION;
            float cx = sp.x * tp + tp * 0.5f, cy = sp.y * tp + tp * 0.5f;
            sf::Uint8 a = (sf::Uint8)(255.f * (1.f - p));
            for (int i = 0; i < 4; ++i) {
                float ang = (float)i * 1.5708f + p * 3.14159f;
                float r = tp * 0.5f * p;
                sf::CircleShape dot(2.f);
                dot.setFillColor(sf::Color(255, 255, 200, a));
                dot.setPosition(cx + std::cos(ang) * r - 2.f, cy + std::sin(ang) * r - 2.f);
                target.draw(dot);
            }
        }
    }
    if (healfx && healfx->active() && s->player) {
        int tp = s->map->get_tile_size();
        float wx = s->player->get_tile_x() * tp + tp * 0.5f;
        float wy = s->player->get_tile_y() * tp + tp * 0.5f;
        healfx->draw(target, wx, wy);
    }
}

// Parse "N,N,W,T" into action tokens (N/S/E/W move, T talk) for demo walks.
static std::vector<char> parse_walk(const char* env) {
    std::vector<char> out;
    if (!env) return out;
    std::stringstream ss(env); std::string t;
    while (std::getline(ss, t, ',')) {
        if (t.size() == 1 && std::string("NSEWTMG").find(t[0]) != std::string::npos)
            out.push_back(t[0]);
    }
    return out;
}

static DIR char_to_dir(char c) {
    switch (c) { case 'N': return DIR::N; case 'S': return DIR::S;
                 case 'E': return DIR::E; case 'W': return DIR::W;
                 default: return DIR::NONE; }
}

/******************************************************************************
StarterSelect - the one-time "choose your partner" screen (Treecko / Torchic /
Mudkip), matching Prof. Birch's bag on Route 101. Opt-in via
CODEMON_CHOOSE_STARTER so existing demos/tests keep their fixed Treecko start;
real play always opens it. Follows the Menu/Minigame active()/input()/draw()
shape so it slots into both the headless token loop and the interactive one.
*****************************************************************************/
struct StarterSelect {
    static constexpr const char* SPECIES[3] = {"TREECKO", "TORCHIC", "MUDKIP"};
    bool open_ = false, done_ = false;
    int cursor = 1;
    sf::Font font; bool font_ok = false;
    sf::Texture tex[3]; bool tex_ok[3] = {false, false, false};
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        for (int i = 0; i < 3; ++i)
            tex_ok[i] = tex[i].loadFromFile(std::string("assets/pokemon/") + SPECIES[i] + ".png");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void open() { open_ = true; done_ = false; cursor = 1; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }               // caller has consumed the choice
    const char* chosen() const { return SPECIES[cursor]; }

    void input(BtnInput b) {
        if (b == BTN_LEFT && cursor > 0) cursor--;
        else if (b == BTN_RIGHT && cursor < 2) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        sf::RectangleShape bg(size);
        bg.setFillColor(sf::Color(20, 40, 32));
        target.draw(bg);

        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56);
        sf::FloatRect title_r(size.x * 0.5f - 220, 22, 440, 44);
        frame.draw(target, title_r.left, title_r.top, title_r.width, title_r.height, 2.5f);
        std::string title_s = "Wähle dein Partner-Pokémon!";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 20);
        title.setPosition(title_r.left + 20, title_r.top + 12);
        title.setFillColor(body_col);
        target.draw(title);

        // Front sprites are 64x64 native; at the 3x scale used elsewhere for
        // this screen that's 192x192, so the card has to be sized for that
        // (not just for the label under it) or the sprite spills out of it.
        const float card_w = 210.f, card_h = 250.f, sprite_px = 64.f * 3.f;
        for (int i = 0; i < 3; ++i) {
            float cx = size.x * (0.2f + 0.3f * i);
            float card_top = 96.f;
            sf::FloatRect card(cx - card_w / 2.f, card_top, card_w, card_h);
            bool sel = i == cursor;
            frame.draw(target, card.left, card.top, card.width, card.height, 2.f);
            float sx = cx - sprite_px / 2.f, sy = card_top + 14.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(card.left - 22.f, sy + sprite_px * 0.35f);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 22);
                    mark.setPosition(card.left - 18.f, sy + sprite_px * 0.3f);
                    mark.setFillColor(head_col);
                    target.draw(mark);
                }
            }
            if (tex_ok[i]) {
                sf::Sprite s(tex[i]);
                s.setScale(3.f, 3.f);
                s.setPosition(sx, sy);
                target.draw(s);
            }
            sf::Text label(SPECIES[i], font, 16);
            sf::FloatRect lb = label.getLocalBounds();
            label.setPosition(cx - lb.width / 2.f, sy + sprite_px + 6.f);
            label.setFillColor(sel ? sf::Color(24, 72, 160) : body_col);
            target.draw(label);
        }

        sf::FloatRect hint_r(size.x * 0.5f - 170, size.y - 56, 340, 40);
        frame.draw(target, hint_r.left, hint_r.top, hint_r.width, hint_r.height, 2.f);
        sf::Text hint("[A/D] waehlen   [SPACE] bestaetigen", font, 14);
        hint.setPosition(hint_r.left + 16, hint_r.top + 12);
        hint.setFillColor(body_col);
        target.draw(hint);
        target.setView(saved);
    }
};

// `msgbox ..., MSGBOX_YESNO` (heal at the Pokemon Center, buy/sell
// confirmations, ...): the VM can't drive a cursor-driven choice itself
// (same reason as StarterSelect above), so this shows a real Ja/Nein
// prompt and the pick is fed back via ScriptVM::resolve_yesno().
struct YesNoPrompt {
    bool open_ = false, done_ = false;
    int cursor = 0;   // 0 = Ja, 1 = Nein
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;
    // Set only by callers that aren't already showing their own question
    // text via the message box first (VM-driven msgboxyesno flows do that
    // via `box`, so leave this empty and get the plain corner prompt as
    // before; the Surf gate below has no such preceding message, so it
    // passes its own question straight into the prompt).
    std::string prompt;

    bool load() {
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    }
    void open(const std::string& p = "") { open_ = true; done_ = false; cursor = 0; prompt = p; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    bool yes() const { return cursor == 0; }

    void input(BtnInput b) {
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor < 1) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        float w = 132.f, h = 78.f;
        float x = size.x - w - 14.f, y = size.y - h - 100.f;
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
        if (!prompt.empty()) {
            // Crude greedy word-wrap (no per-glyph measurement, just a
            // char-count budget tuned for this font/size/box width) -- the
            // only prompt text this widget ever shows is the short, fixed
            // Surf question, so this doesn't need to be a general wrapper.
            std::vector<std::string> plines;
            std::string cur;
            std::stringstream ss(prompt);
            std::string word;
            while (ss >> word) {
                if (!cur.empty() && cur.size() + 1 + word.size() > 38) {
                    plines.push_back(cur); cur.clear();
                }
                if (!cur.empty()) cur += ' ';
                cur += word;
            }
            if (!cur.empty()) plines.push_back(cur);
            float pw = 340.f, ph = 34.f + plines.size() * 22.f;
            float px = size.x - pw - 14.f, py = y - ph - 8.f;
            frame.draw(target, px, py, pw, ph, 2.5f);
            for (size_t li = 0; li < plines.size(); ++li) {
                sf::Text t(sf::String::fromUtf8(plines[li].begin(), plines[li].end()), font, 15);
                t.setPosition(px + 14, py + 14 + li * 22.f);
                t.setFillColor(body_col);
                target.draw(t);
            }
        }
        frame.draw(target, x, y, w, h, 2.5f);
        const char* opts[2] = {"Ja", "Nein"};
        for (int i = 0; i < 2; ++i) {
            bool sel = i == cursor;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 12, y + 12 + i * 32);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 20);
                    mark.setPosition(x + 14, y + 10 + i * 32);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            sf::Text t(opts[i], font, 20);
            t.setPosition(x + 40, y + 10 + i * 32);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};

// "ITEM_POKE_BALL" -> "Poke Ball"
static std::string item_display_name(const std::string& item) {
    std::string s = item;
    if (s.rfind("ITEM_", 0) == 0) s = s.substr(5);
    std::string out; bool cap = true;
    for (char c : s) {
        if (c == '_') { out += ' '; cap = true; }
        else if (cap) { out += (char)std::toupper((unsigned char)c); cap = false; }
        else out += (char)std::tolower((unsigned char)c);
    }
    return out;
}

// `special ChoosePartyMon` (in-game trades: Rustboro/Fortree/Pacifidlog/
// Battle Frontier Lounge NPCs offering to swap a mon): same story as
// StarterSelect/YesNoPrompt above -- the VM can't drive a real party list
// itself, so this shows one (plus a trailing "Abbrechen" row) and calls
// ScriptVM::resolve_choose_party_mon() with the picked index, or -1.
struct PartyPicker {
    bool open_ = false, done_ = false;
    int cursor = 0;
    std::vector<Mon>* team = nullptr;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void configure(std::vector<Mon>* t) { team = t; }
    void open() { open_ = true; done_ = false; cursor = 0; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    int chosen() const {
        int n = team ? (int)team->size() : 0;
        return cursor < n ? cursor : -1;   // last row ("Abbrechen") -> -1
    }

    void input(BtnInput b) {
        int rows = (team ? (int)team->size() : 0) + 1;
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor < rows - 1) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        int n_mons = team ? (int)team->size() : 0;
        float w = 260.f, h = 50.f + (n_mons + 1) * 32.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56), sel_col(24, 72, 160);
        std::string title_s = "Waehle ein POKéMON";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 18);
        title.setPosition(x + 14, y + 10);
        title.setFillColor(head_col);
        target.draw(title);
        auto row = [&](int i, const std::string& label) {
            bool sel = i == cursor;
            float ry = y + 46.f + i * 32.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 10, ry - 2);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 18);
                    mark.setPosition(x + 12, ry - 2);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            sf::Text t(label, font, 16);
            t.setPosition(x + 36, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        };
        for (int i = 0; i < n_mons; ++i)
            row(i, item_display_name((*team)[i].species) + "  Lv" + std::to_string((*team)[i].level));
        row(n_mons, "Abbrechen");
        target.setView(saved);
    }
};

// `multichoice`/`multichoicedefault`: a fixed option list (ScriptVM's
// MULTICHOICE_LISTS) the player picks one of, same block-and-resume shape as
// PartyPicker above -- the VM can't drive a real cursor-driven menu itself.
struct MultiChoicePrompt {
    bool open_ = false, done_ = false;
    int cursor = 0;
    std::vector<std::string> options;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void open(const std::vector<std::string>& opts, int default_idx) {
        options = opts; open_ = true; done_ = false;
        cursor = (default_idx >= 0 && default_idx < (int)opts.size()) ? default_idx : 0;
    }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    int chosen() const { return cursor; }

    void input(BtnInput b) {
        int n = (int)options.size();
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor + 1 < n) cursor++;
        else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        int n = (int)options.size();
        float w = 260.f, h = 30.f + n * 32.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
        for (int i = 0; i < n; ++i) {
            bool sel = i == cursor;
            float ry = y + 16.f + i * 32.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 10, ry - 2);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 18);
                    mark.setPosition(x + 12, ry - 2);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            sf::Text t(sf::String::fromUtf8(options[i].begin(), options[i].end()), font, 16);
            t.setPosition(x + 36, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};

// DebugMenu - a developer cheat menu (toggle with H), for testing without
// playing through the whole story: heal, money, badges, HMs, a starter,
// common items, and skipping whatever script/dialog is currently running.
// Same list-driven shape as MultiChoicePrompt, but with a fixed action list
// dispatched by index in the main loop below instead of a caller-supplied
// options vector.
struct DebugMenu {
    enum Action {
        HEAL_TEAM, ADD_MONEY, ALL_BADGES, GIVE_STARTER, GIVE_ANY_POKEMON, TEACH_HMS,
        GIVE_ITEMS, GIVE_XP, SKIP_SCRIPT, CLOSE, COUNT
    };
    bool open_ = false;
    int cursor = 0;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    bool active() const { return open_; }
    void open() { open_ = true; cursor = 0; }
    void close() { open_ = false; }

    static const char* label(int i) {
        switch (i) {
        case HEAL_TEAM:    return "Team komplett heilen";
        case ADD_MONEY:    return "+50.000 Geld";
        case ALL_BADGES:   return "Alle 8 Orden geben";
        case GIVE_STARTER: return "Starter-Pokemon waehlen";
        case GIVE_ANY_POKEMON: return "Beliebiges Pokemon ins Team";
        case TEACH_HMS:    return "Alle Hm-Attacken lehren (Leadmon)";
        case GIVE_ITEMS:   return "99x Poke Ball / Trank / Rare Candy";
        case GIVE_XP:      return "+1000 EP fuer das ganze Team";
        case SKIP_SCRIPT:  return "Laufendes Skript/Dialog abbrechen";
        case CLOSE:        return "Schliessen";
        default: return "";
        }
    }

    // Returns the chosen action, or -1 if just navigating/still open.
    int input(BtnInput b) {
        if (b == BTN_UP && cursor > 0) cursor--;
        else if (b == BTN_DOWN && cursor + 1 < COUNT) cursor++;
        else if (b == BTN_CONFIRM) { open_ = false; return cursor; }
        return -1;
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        float w = 340.f, h = 30.f + COUNT * 30.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        std::string title_s = "DEBUG-MENUE";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 16);
        title.setPosition(x + 12, y + 6); title.setFillColor(sf::Color(150, 40, 40));
        target.draw(title);
        const sf::Color body_col(40, 40, 56), sel_col(24, 72, 160);
        for (int i = 0; i < COUNT; ++i) {
            bool sel = i == cursor;
            float ry = y + 34.f + i * 30.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 10, ry - 2);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 16);
                    mark.setPosition(x + 12, ry - 2);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            std::string s = label(i);
            sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, 15);
            t.setPosition(x + 36, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};

// SpeciesPicker - the DebugMenu's "beliebiges Pokemon" submenu: a scrolling
// list of every species BattleData knows about (species_by_id/species_count,
// load()-order, not the real Hoenn Dex numbering), so the player can pick any
// one of them and have it added to the team at a fixed level. Same
// block-and-resume shape as PartyPicker, but scrollable since the species
// list is far longer than a party.
struct SpeciesPicker {
    static constexpr int VISIBLE = 12;
    bool open_ = false, done_ = false;
    int cursor = 0, top = 0;
    std::vector<std::string> names;
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }
    void configure(const BattleData* bd) {
        names.clear();
        int n = bd->species_count();
        for (int i = 0; i < n; ++i) names.push_back(bd->species_by_id(i));
    }
    void open() { open_ = true; done_ = false; cursor = 0; top = 0; }
    void cancel() { open_ = false; done_ = false; }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }
    const std::string& chosen() const { return names[cursor]; }

    void input(BtnInput b) {
        int n = (int)names.size();
        if (n == 0) return;
        if (b == BTN_UP && cursor > 0) {
            cursor--;
            if (cursor < top) top = cursor;
        } else if (b == BTN_DOWN && cursor + 1 < n) {
            cursor++;
            if (cursor >= top + VISIBLE) top = cursor - VISIBLE + 1;
        } else if (b == BTN_CONFIRM) { done_ = true; open_ = false; }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        int n = (int)names.size();
        int shown = std::min(n - top, VISIBLE);
        float w = 300.f, h = 50.f + shown * 26.f;
        float x = size.x * 0.5f - w / 2.f, y = size.y * 0.5f - h / 2.f;
        frame.draw(target, x, y, w, h, 2.5f);
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56), sel_col(24, 72, 160);
        std::string title_s = "Waehle ein POKéMON (" + std::to_string(cursor + 1) + "/" +
                               std::to_string(n) + ")";
        sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 16);
        title.setPosition(x + 12, y + 8);
        title.setFillColor(head_col);
        target.draw(title);
        for (int row = 0; row < shown; ++row) {
            int i = top + row;
            bool sel = i == cursor;
            float ry = y + 40.f + row * 26.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(x + 8, ry - 2);
                    target.draw(cs);
                } else {
                    sf::Text mark(">", font, 15);
                    mark.setPosition(x + 10, ry - 2);
                    mark.setFillColor(sel_col);
                    target.draw(mark);
                }
            }
            sf::Text t(names[i], font, 14);
            t.setPosition(x + 32, ry);
            t.setFillColor(sel ? sel_col : body_col);
            target.draw(t);
        }
        target.setView(saved);
    }
};

// `pokemart <label>` (the mart buy screen): the VM can't drive a scrolling
// item list + quantity stepper itself (same reason as StarterSelect and
// YesNoPrompt above), so this shows a real shop UI built from the map's
// item list (ScriptVM::shop_items()) and GameState's money/bag, then calls
// ScriptVM::close_shop() once the player leaves.
struct Shop {
    enum Stage { LIST, QTY, MSG };
    Stage stage = LIST;
    bool open_ = false, done_ = false;
    std::vector<std::string> items;
    int cursor = 0, qty = 1;
    GameState* gs = nullptr;
    const std::unordered_map<std::string, int>* prices = nullptr;
    sf::Font font; bool font_ok = false;
    std::string flash;   // transient feedback ("Gekauft!" / "Nicht genug Geld!")
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;
    std::unordered_map<std::string, sf::Texture> icon_cache;

    bool load() {
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        return font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    }
    void configure(GameState* g, const std::unordered_map<std::string, int>* p) {
        this->gs = g; this->prices = p;
    }
    // "ITEM_POTION" -> assets/items/potion.png, cached like Menu::item_icon.
    const sf::Texture* icon_of(const std::string& item) {
        std::string f = item;
        if (f.rfind("ITEM_", 0) == 0) f = f.substr(5);
        for (char& c : f) c = (char)std::tolower((unsigned char)c);
        auto it = icon_cache.find(f);
        if (it != icon_cache.end()) return it->second.getSize().x ? &it->second : nullptr;
        sf::Texture tex;
        if (!tex.loadFromFile("assets/items/" + f + ".png")) { icon_cache[f]; return nullptr; }
        tex.setSmooth(false);
        icon_cache[f] = tex;
        return &icon_cache[f];
    }
    bool active() const { return open_; }
    bool done() const { return done_; }
    void ack() { done_ = false; }

    void open(const std::vector<std::string>& list) {
        items = list; cursor = 0; qty = 1; stage = LIST;
        open_ = true; done_ = false; flash.clear();
    }

    int price_of(const std::string& item) const {
        if (!prices) return 0;
        auto it = prices->find(item);
        return it == prices->end() ? 0 : it->second;
    }

    void input(BtnInput b) {
        if (stage == MSG) {
            if (b == BTN_CONFIRM) stage = LIST;
            return;
        }
        int n = (int)items.size();   // row `n` is the trailing "Verlassen"
        if (stage == LIST) {
            if (b == BTN_UP && cursor > 0) cursor--;
            else if (b == BTN_DOWN && cursor < n) cursor++;
            else if (b == BTN_CONFIRM) {
                if (cursor == n) { open_ = false; done_ = true; return; }
                int price = price_of(items[cursor]);
                if (price <= 0 || !gs || gs->money < price) {
                    flash = "Nicht genug Geld!"; stage = MSG; return;
                }
                qty = 1; stage = QTY;
            }
        } else if (stage == QTY) {
            int price = price_of(items[cursor]);
            int max_afford = price > 0 && gs ? gs->money / price : 1;
            int cap = std::min(99, std::max(1, max_afford));
            if (b == BTN_UP && qty < cap) qty++;
            else if (b == BTN_DOWN && qty > 1) qty--;
            else if (b == BTN_LEFT) stage = LIST;
            else if (b == BTN_CONFIRM && gs) {
                gs->money -= price * qty;
                gs->give_item(items[cursor], qty);
                flash = "Gekauft: " + item_display_name(items[cursor]) +
                        " x" + std::to_string(qty) + "!";
                stage = MSG;
            }
        }
    }

    void draw(sf::RenderTarget& target) {
        if (!open_ || !font_ok) return;
        sf::View saved = target.getView();
        target.setView(target.getDefaultView());
        sf::Vector2f size = target.getView().getSize();
        sf::RectangleShape dim(size);
        dim.setFillColor(sf::Color(0, 0, 0, 140));
        target.draw(dim);

        const sf::Color head_col(24, 72, 160), body_col(40, 40, 56), muted_col(100, 100, 112);
        auto text = [&](const std::string& s, float px, float py, unsigned cs, sf::Color col) {
            sf::Text t(sf::String::fromUtf8(s.begin(), s.end()), font, cs);
            t.setPosition(px, py); t.setFillColor(col); target.draw(t);
        };

        sf::FloatRect panel(size.x * 0.5f - 200, 20, 400, size.y - 40);
        frame.draw(target, panel.left, panel.top, panel.width, panel.height, 3.f);

        sf::FloatRect money_r(panel.left, panel.top - 6, 180, 40);
        frame.draw(target, money_r.left, money_r.top, money_r.width, money_r.height, 2.f);
        text("Geld: " + std::to_string(gs ? gs->money : 0) + " P",
             money_r.left + 14, money_r.top + 8, 16, body_col);

        if (stage == MSG) {
            text(flash, panel.left + 30, panel.top + panel.height * 0.4f, 18, body_col);
            text("[SPACE] weiter", panel.left + 30, panel.top + panel.height * 0.4f + 34, 14, muted_col);
            target.setView(saved);
            return;
        }

        // rows[0..items.size()-1] are the items, the trailing row is "Verlassen"
        int row_count = (int)items.size() + 1;
        const int visible = 7;
        int scroll = std::max(0, std::min(cursor - visible / 2, row_count - visible));
        scroll = std::max(0, scroll);
        float x = panel.left + 24, y0 = panel.top + 56;
        for (int r = 0; r < visible && scroll + r < row_count; ++r) {
            int i = scroll + r;
            bool sel = i == cursor;
            float ry = y0 + r * 42;
            if (sel) {
                if (cursor_ok) { sf::Sprite cs(cursor_tex); cs.setPosition(x - 22, ry - 2); target.draw(cs); }
                else text(">", x - 18, ry, 18, head_col);
            }
            if (i < (int)items.size()) {
                const sf::Texture* ic = icon_of(items[i]);
                if (ic) { sf::Sprite s(*ic); s.setPosition(x, ry - 4); target.draw(s); }
                text(item_display_name(items[i]), x + 36, ry, 17, sel ? head_col : body_col);
                std::string price_s = std::to_string(price_of(items[i])) + " P";
                text(price_s, panel.left + panel.width - 26 - price_s.size() * 9.f, ry, 16,
                     sel ? head_col : muted_col);
            } else {
                text("Verlassen", x + 36, ry, 17, sel ? head_col : body_col);
            }
        }

        if (stage == QTY) {
            sf::FloatRect qr(panel.left + 20, panel.top + panel.height - 84, panel.width - 40, 64);
            frame.draw(target, qr.left, qr.top, qr.width, qr.height, 2.f);
            text("Anzahl: " + std::to_string(qty), qr.left + 16, qr.top + 8, 17, body_col);
            text(std::to_string(price_of(items[cursor]) * qty) + " P",
                 qr.left + 16, qr.top + 32, 16, muted_col);
        }
        std::string hint_s = stage == QTY
            ? "[W/S] Anzahl  [A] zurueck  [SPACE] kaufen"
            : "[W/S] waehlen  [SPACE] auswaehlen";
        text(hint_s, panel.left, panel.top + panel.height + 8, 13, sf::Color(230, 230, 230));
        target.setView(saved);
    }
};

// The game's own start screen, shown once at launch before any map loads
// (real interactive play only -- headless tests and CODEMON_MAP demos skip
// straight past it, same as every other test-only branch in main()). No
// licensed logo art exists to import (assets/graphics/intro/ has the full
// animated intro cutscene, but no actual title wordmark), so this is text
// over a plain background, styled like every other screen in this engine
// (UiFrame panel, arrow cursor, same W/S/Space input as the rest of the UI).
struct TitleScreen {
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }

    // Shared by run() below and the headless screenshot harness (which has
    // no real window/event loop to drive run() with, but still wants to
    // render a frame of this to verify the layout).
    void draw(sf::RenderTarget& w, const std::vector<std::string>& opts, int cursor) {
        sf::Vector2f size = w.getView().getSize();
        w.clear(sf::Color(24, 60, 40));
        if (font_ok) {
            std::string title_s = "CODEMON";
            sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 64);
            title.setStyle(sf::Text::Bold);
            sf::FloatRect tb = title.getLocalBounds();
            title.setPosition(size.x / 2.f - tb.width / 2.f - tb.left, size.y * 0.20f);
            title.setFillColor(sf::Color(255, 232, 160));
            title.setOutlineColor(sf::Color(60, 30, 10));
            title.setOutlineThickness(3.f);
            w.draw(title);

            std::string sub_s = "Ein Pokémon-Fanprojekt";
            sf::Text sub(sf::String::fromUtf8(sub_s.begin(), sub_s.end()), font, 18);
            sf::FloatRect sb = sub.getLocalBounds();
            sub.setPosition(size.x / 2.f - sb.width / 2.f - sb.left, size.y * 0.20f + 78.f);
            sub.setFillColor(sf::Color(230, 230, 230));
            w.draw(sub);
        }
        float bw = 220.f, bh = 24.f + opts.size() * 38.f;
        float bx = size.x / 2.f - bw / 2.f, by = size.y * 0.6f;
        if (frame.ready()) frame.draw(w, bx, by, bw, bh, 2.5f);
        if (font_ok) {
            for (int i = 0; i < (int)opts.size(); ++i) {
                bool sel = i == cursor;
                float ry = by + 16.f + i * 38.f;
                if (sel) {
                    if (cursor_ok) {
                        sf::Sprite cs(cursor_tex);
                        cs.setPosition(bx + 16, ry - 2);
                        w.draw(cs);
                    } else {
                        sf::Text mark(">", font, 18);
                        mark.setPosition(bx + 18, ry - 2);
                        mark.setFillColor(sf::Color(24, 72, 160));
                        w.draw(mark);
                    }
                }
                sf::Text t(sf::String::fromUtf8(opts[i].begin(), opts[i].end()), font, 18);
                t.setPosition(bx + 44, ry);
                t.setFillColor(sel ? sf::Color(24, 72, 160) : sf::Color(40, 40, 56));
                w.draw(t);
            }
        }
    }

    // Blocks until the player picks an option. Returns true for "FORTSETZEN"
    // (only offered when has_save), false for "NEUES SPIEL". Closing the
    // window here exits the whole program, same as closing it mid-game would.
    bool run(sf::RenderWindow& w, bool has_save) {
        std::vector<std::string> opts;
        if (has_save) opts.push_back("FORTSETZEN");
        opts.push_back("NEUES SPIEL");
        int cursor = 0;
        while (w.isOpen()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::W && cursor > 0) cursor--;
                else if (event.key.code == sf::Keyboard::S && cursor + 1 < (int)opts.size()) cursor++;
                else if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return)
                    return has_save ? (cursor == 0) : false;
            }
            draw(w, opts, cursor);
            w.display();
        }
        std::exit(0);
    }
};

/******************************************************************************
GenderSelect - the "who are you" screen shown once at the start of a brand
new game: Brendan (boy) or May (girl), same portraits real Emerald uses on
its own character-select screen. Determines the overworld sprite sheet and
`checkplayergender`'s answer. Self-contained blocking run(), same shape as
TitleScreen -- this runs before the main session/window even exist.
*****************************************************************************/
struct GenderSelect {
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;
    sf::Texture portrait[2]; bool portrait_ok[2] = {false, false};   // 0=Brendan, 1=May

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
        portrait_ok[0] = portrait[0].loadFromFile("assets/trainers/brendan.png");
        portrait_ok[1] = portrait[1].loadFromFile("assets/trainers/may.png");
    }

    void draw(sf::RenderTarget& w, int cursor) {
        sf::Vector2f size = w.getView().getSize();
        w.clear(sf::Color(24, 60, 40));
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56);
        if (font_ok) {
            std::string title_s = "Bist du ein Junge oder ein Mädchen?";
            sf::Text title(sf::String::fromUtf8(title_s.begin(), title_s.end()), font, 22);
            sf::FloatRect tb = title.getLocalBounds();
            title.setPosition(size.x / 2.f - tb.width / 2.f - tb.left, size.y * 0.18f);
            title.setFillColor(head_col);
            w.draw(title);
        }
        static const char* NAMES[2] = {"JUNGE", "MÄDCHEN"};
        const float card_w = 160.f, card_h = 200.f, sprite_px = 64.f * 2.f;
        for (int i = 0; i < 2; ++i) {
            float cx = size.x * (0.32f + 0.36f * i);
            float card_top = size.y * 0.32f;
            sf::FloatRect card(cx - card_w / 2.f, card_top, card_w, card_h);
            bool sel = i == cursor;
            if (frame.ready()) frame.draw(w, card.left, card.top, card.width, card.height, 2.f);
            float sx = cx - sprite_px / 2.f, sy = card_top + 14.f;
            if (sel) {
                if (cursor_ok) {
                    sf::Sprite cs(cursor_tex);
                    cs.setPosition(card.left - 22.f, sy + sprite_px * 0.35f);
                    w.draw(cs);
                } else if (font_ok) {
                    sf::Text mark(">", font, 22);
                    mark.setPosition(card.left - 18.f, sy + sprite_px * 0.3f);
                    mark.setFillColor(head_col);
                    w.draw(mark);
                }
            }
            if (portrait_ok[i]) {
                sf::Sprite s(portrait[i]);
                s.setScale(2.f, 2.f);
                s.setPosition(sx, sy);
                w.draw(s);
            }
            if (font_ok) {
                std::string name_s = NAMES[i];
                sf::Text label(sf::String::fromUtf8(name_s.begin(), name_s.end()), font, 16);
                sf::FloatRect lb = label.getLocalBounds();
                label.setPosition(cx - lb.width / 2.f, sy + sprite_px + 10.f);
                label.setFillColor(sel ? sf::Color(24, 72, 160) : body_col);
                w.draw(label);
            }
        }
        if (font_ok) {
            std::string hint_s = "[A/D] wählen   [SPACE] bestätigen";
            sf::Text hint(sf::String::fromUtf8(hint_s.begin(), hint_s.end()), font, 14);
            sf::FloatRect hb = hint.getLocalBounds();
            hint.setPosition(size.x / 2.f - hb.width / 2.f, size.y - 40);
            hint.setFillColor(sf::Color(230, 230, 230));
            w.draw(hint);
        }
    }

    // Blocks until the player picks Brendan (false) or May (true).
    bool run(sf::RenderWindow& w) {
        int cursor = 0;
        while (w.isOpen()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::A && cursor > 0) cursor--;
                else if (event.key.code == sf::Keyboard::D && cursor < 1) cursor++;
                else if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return)
                    return cursor == 1;
            }
            draw(w, cursor);
            w.display();
        }
        std::exit(0);
    }
};

/******************************************************************************
EarlyAccessNotice - a one-shot disclaimer shown right after character
creation on a brand new game, using the same DialogBox the overworld uses for
NPC text so it looks consistent with the rest of the UI. Self-contained
blocking run(), same shape as TitleScreen/GenderSelect/NameEntry.
*****************************************************************************/
struct EarlyAccessNotice {
    void run(sf::RenderWindow& w) {
        DialogBox box;
        box.load_font();
        box.open("", "Dies ist eine Early-Access-Version.\x1f"
                      "Es können noch verschiedene Bugs auftreten, "
                      "und nicht alles ist bereits perfekt dem Original nachgestellt.");
        while (w.isOpen() && box.is_active()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return)
                    box.advance();
            }
            w.clear(sf::Color(24, 60, 40));
            box.draw(w);
            w.display();
        }
    }
};

/******************************************************************************
NameEntry - the on-screen letter-grid naming keyboard real Gen-3 games use
(A-Z plus DEL/OK), reused both for the player's own name and their rival's.
Grid-driven like every other menu in this engine (WASD + confirm), not free
keyboard typing, to stay consistent with the rest of the UI. Self-contained
blocking run(), same shape as TitleScreen/GenderSelect.
*****************************************************************************/
struct NameEntry {
    static constexpr int COLS = 7, ROWS = 4, MAX_LEN = 7;
    // 26 letters + DEL + OK, row-major, filling exactly COLS*ROWS cells.
    static constexpr const char* CELLS[COLS * ROWS] = {
        "A","B","C","D","E","F","G",
        "H","I","J","K","L","M","N",
        "O","P","Q","R","S","T","U",
        "V","W","X","Y","Z","DEL","OK",
    };
    sf::Font font; bool font_ok = false;
    UiFrame frame;
    sf::Texture cursor_tex; bool cursor_ok = false;

    void load() {
        font_ok = font.loadFromFile("assets/fonts/DejaVuSans.ttf");
        frame.load();
        cursor_ok = cursor_tex.loadFromFile("assets/graphics/interface/arrow_cursor.png");
        cursor_tex.setSmooth(false);
    }

    void draw(sf::RenderTarget& w, const std::string& title, const std::string& name, int cur_r, int cur_c) {
        sf::Vector2f size = w.getView().getSize();
        w.clear(sf::Color(24, 60, 40));
        const sf::Color head_col(255, 232, 160), body_col(40, 40, 56);
        if (!font_ok) return;
        sf::Text title_t(sf::String::fromUtf8(title.begin(), title.end()), font, 20);
        sf::FloatRect tb = title_t.getLocalBounds();
        title_t.setPosition(size.x / 2.f - tb.width / 2.f - tb.left, size.y * 0.08f);
        title_t.setFillColor(head_col);
        w.draw(title_t);

        // Typed-so-far preview, with a blinking-style underline cursor cell.
        float name_bar_w = 260.f, name_bar_h = 40.f;
        float name_bar_x = size.x / 2.f - name_bar_w / 2.f, name_bar_y = size.y * 0.17f;
        if (frame.ready()) frame.draw(w, name_bar_x, name_bar_y, name_bar_w, name_bar_h, 2.f);
        std::string shown = name.empty() ? "_" : name;
        sf::Text name_t(sf::String::fromUtf8(shown.begin(), shown.end()), font, 22);
        name_t.setPosition(name_bar_x + 16, name_bar_y + 6);
        name_t.setFillColor(body_col);
        w.draw(name_t);

        float grid_w = 420.f, grid_h = 220.f;
        float grid_x = size.x / 2.f - grid_w / 2.f, grid_y = size.y * 0.36f;
        // A light panel behind the grid, same as every other bordered card in
        // this UI -- without it, body_col's dark navy letters are unreadable
        // straight against the dark green screen background.
        if (frame.ready()) frame.draw(w, grid_x, grid_y, grid_w, grid_h, 2.f);
        float cell_w = grid_w / COLS, cell_h = grid_h / ROWS;
        for (int r = 0; r < ROWS; ++r) {
            for (int c = 0; c < COLS; ++c) {
                const char* label = CELLS[r * COLS + c];
                float cx = grid_x + c * cell_w, cy = grid_y + r * cell_h;
                bool sel = r == cur_r && c == cur_c;
                if (sel) {
                    sf::RectangleShape hl(sf::Vector2f(cell_w - 4.f, cell_h - 4.f));
                    hl.setPosition(cx + 2.f, cy + 2.f);
                    hl.setFillColor(sf::Color(24, 72, 160, 90));
                    w.draw(hl);
                }
                sf::Text t(label, font, 16);
                sf::FloatRect lb = t.getLocalBounds();
                t.setPosition(cx + cell_w / 2.f - lb.width / 2.f - lb.left,
                               cy + cell_h / 2.f - lb.height / 2.f - lb.top);
                t.setFillColor(sel ? sf::Color(24, 72, 160) : body_col);
                w.draw(t);
            }
        }
        std::string hint_s = "[WASD] wählen  [SPACE] bestätigen  [BACKSPACE] löschen";
        sf::Text hint(sf::String::fromUtf8(hint_s.begin(), hint_s.end()), font, 13);
        sf::FloatRect hb = hint.getLocalBounds();
        hint.setPosition(size.x / 2.f - hb.width / 2.f, size.y - 34);
        hint.setFillColor(sf::Color(230, 230, 230));
        w.draw(hint);
    }

    // Blocks until "OK" is confirmed with a non-empty name. `def` pre-fills
    // the name (real games start blank, but a sane default keeps this
    // skippable-feeling and gives something sensible if the window closes).
    std::string run(sf::RenderWindow& w, const std::string& title, const std::string& def) {
        std::string name = def.substr(0, MAX_LEN);
        int cur_r = 0, cur_c = 0;
        while (w.isOpen()) {
            sf::Event event;
            while (w.pollEvent(event)) {
                if (event.type == sf::Event::Closed) { w.close(); std::exit(0); }
                if (event.type != sf::Event::KeyPressed) continue;
                if (event.key.code == sf::Keyboard::W && cur_r > 0) cur_r--;
                else if (event.key.code == sf::Keyboard::S && cur_r < ROWS - 1) cur_r++;
                else if (event.key.code == sf::Keyboard::A && cur_c > 0) cur_c--;
                else if (event.key.code == sf::Keyboard::D && cur_c < COLS - 1) cur_c++;
                else if (event.key.code == sf::Keyboard::BackSpace) {
                    if (!name.empty()) name.pop_back();
                } else if (event.key.code == sf::Keyboard::Space || event.key.code == sf::Keyboard::Return) {
                    std::string cell = CELLS[cur_r * COLS + cur_c];
                    if (cell == "DEL") { if (!name.empty()) name.pop_back(); }
                    else if (cell == "OK") { if (!name.empty()) return name; }
                    else if ((int)name.size() < MAX_LEN) name += cell;
                }
            }
            draw(w, title, name, cur_r, cur_c);
            w.display();
        }
        std::exit(0);
    }
};

int main() {
    const char* map_env = std::getenv("CODEMON_MAP");
    std::mt19937 rng(1234);

    static const char* SAVE_PATH = "savegame.dat";
    GameState gs;
    BattleData bdata;
    bdata.load("assets/battle");
    std::vector<Mon> team;                          // the player's party
    team.reserve(6);                                // keep &team[0] stable
    std::vector<Mon> pc_box;                         // PC storage

    // A saved run resumes exactly where it left off (map, position, flags,
    // bag, money, party, PC box). CODEMON_MAP/CODEMON_NO_SAVE force a fresh
    // start for demos/tests even when a savegame.dat is lying around.
    std::string start_map; int start_x = -1, start_y = -1;
    bool resumed = false;
    if (!map_env && !std::getenv("CODEMON_NO_SAVE")) {
        resumed = SaveGame::load(SAVE_PATH, gs, team, pc_box, start_map, start_x, start_y);
        // A save written before PP tracking existed has no `pp` field at all
        // for its party/box mons -- give them full PP rather than leaving
        // the vector empty (which would read as "0 PP, can't move").
        for (Mon& m : team) if (m.pp.size() != m.moves.size()) bdata.restore_pp(m);
        for (Mon& m : pc_box) if (m.pp.size() != m.moves.size()) bdata.restore_pp(m);
    }
    // Story start: the player rides in on the moving truck (InsideOfTruck),
    // which is where pokeemerald's own new-game intro begins -- its own
    // imported script (checkplayergender + SetIntroFlagsMale/Female) hides
    // the unused house's occupants, sets the respawn point and points the
    // truck's WARP_ID_DYNAMIC exit at the right house, all keyed off
    // gs.female (see below). Walking to the exit door then drops the player
    // right outside their house in Littleroot Town, same as real Emerald.
    // Factored into a lambda so "NEUES SPIEL" on the title screen below can
    // also run it even when a save was already loaded (discarding it, same
    // as any other save file that's simply never opened again).
    auto start_new_game = [&]() {
        gs = GameState(); team.clear(); pc_box.clear();
        start_map = map_env ? map_env : "maps/InsideOfTruck.map";
        start_x = start_y = -1;
        // New-game default world state: every NPC/item hidden until its own
        // story beat unlocks it (data/scripts/new_game.inc in pokeemerald),
        // e.g. the rival isn't standing in your bedroom, Birch isn't already
        // in his lab, Mom's "moving in" dialogue is used instead of the daily one.
        std::ifstream ngf("assets/new_game_flags.txt");
        std::string ln;
        while (std::getline(ngf, ln))
            if (!ln.empty()) gs.set_flag(ln);
        // In real pokeemerald VAR_LITTLEROOT_TOWN_STATE only reaches 1 by
        // visiting the rival's house and finding their poke ball on day 1 --
        // the one and only way LittlerootTown's Route 101 warning-kid trigger
        // ever lets the player through to the Birch-rescue scene. Skipping
        // straight to "wake up on day 2" like above must carry that forward
        // too, or the kid pushes the player back forever with no way to
        // proceed.
        gs.set_var("VAR_LITTLEROOT_TOWN_STATE", 1);
        // Story start: no starter yet -- team stays empty until the player
        // actually picks one from Birch's bag on Route 101, same as pokeemerald.
        // Whiteout recovery point before the player has healed anywhere for
        // real: default to Brendan's House 2F until the truck's own script
        // (which runs setrespawn once gender is known) picks the right one
        // below -- there's no battle before then, so this default is never
        // actually read as a whiteout target.
        gs.last_heal_map = "LittlerootTown_BrendansHouse_2F";
        gs.last_heal_x = 4; gs.last_heal_y = 2;
    };
    if (!resumed) start_new_game();

    // --- title screen --------------------------------------------------
    // Real interactive play only: headless screenshot tests and CODEMON_MAP
    // demos need deterministic, immediate map loading, same reasoning as
    // every other CODEMON_* test hook bypassing normal flow.
    if (!g_headless && !map_env) {
        TitleScreen title;
        title.load();
        sf::RenderWindow titlewin(sf::VideoMode(VIEW_TW * 16 * SCALE, VIEW_TH * 16 * SCALE),
                                  "Codemon!");
        bool wants_continue = title.run(titlewin, resumed);
        titlewin.close();
        if (!wants_continue && resumed) { start_new_game(); resumed = false; }

        // A brand new game (not a loaded save) still needs a player before
        // it can begin: who you are (GenderSelect) and your/your rival's
        // name (NameEntry), matching real Emerald's own character-select +
        // naming-screen intro. Skipped entirely when continuing a save.
        if (!resumed) {
            sf::RenderWindow setupwin(sf::VideoMode(VIEW_TW * 16 * SCALE, VIEW_TH * 16 * SCALE),
                                      "Codemon!");
            GenderSelect gender_ui; gender_ui.load();
            gs.female = gender_ui.run(setupwin);
            PLAYER_SHEET = gs.female ? "assets/overworld/people_may_walking.png"
                                     : "assets/overworld/people_brendan_walking.png";
            NameEntry name_ui; name_ui.load();
            gs.player_name = name_ui.run(setupwin, "Wie heisst du?", gs.female ? "MAY" : "BRENDAN");
            gs.rival_name = name_ui.run(setupwin, "Wie heisst dein Rivale?", gs.female ? "BRENDAN" : "MAY");
            EarlyAccessNotice().run(setupwin);
            setupwin.close();
            // Now that gender is known, point the pre-battle whiteout target
            // at the matching house (InsideOfTruck's own script sets the
            // *dynamic warp* target the same way, but never touches
            // last_heal_* -- see setrespawn's comment in ScriptVM.cpp).
            if (gs.female) {
                gs.last_heal_map = "LittlerootTown_MaysHouse_2F";
                gs.last_heal_x = 4; gs.last_heal_y = 2;
            }
        }
    }

    // CODEMON_START_X/Y (with CODEMON_MAP): land on a specific tile instead
    // of the map's own default start position -- for headlessly reaching an
    // object/NPC that isn't right next to the map's normal entrance.
    if (map_env) {
        if (const char* ex = std::getenv("CODEMON_START_X")) start_x = atoi(ex);
        if (const char* ey = std::getenv("CODEMON_START_Y")) start_y = atoi(ey);
    }
    Session* sess = load_session(start_map, start_x, start_y, &gs);

    const unsigned win_w = VIEW_TW * sess->map->get_tile_size() * SCALE;
    const unsigned win_h = VIEW_TH * sess->map->get_tile_size() * SCALE;

    DialogBox box;
    box.load_font();
    box.configure(&gs);
    Battle battle;
    battle.configure(&bdata, &rng);
    battle.set_capture(&gs, &team, &pc_box);
    ScriptVM vm;
    vm.set_battle_data(&bdata, &team, &rng, &pc_box);
    vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player, &sess->actors, &sess->localid_map);
    run_load_triggers(sess->map, gs, vm);
    check_trigger(sess, vm, gs);
    if (const char* ts = std::getenv("CODEMON_TEST_SCRIPT")) vm.start(ts, sess->player);
    Menu menu;
    menu.load_font();
    menu.configure(&gs, &team, &pc_box, &bdata);
    Minigame games;
    games.load_font();
    games.configure(&gs, &rng);
    StarterSelect starter;
    starter.load();
    if (std::getenv("CODEMON_CHOOSE_STARTER")) starter.open();
    YesNoPrompt yesno;
    yesno.load();
    PartyPicker picker;
    picker.load();
    picker.configure(&team);
    MultiChoicePrompt multichoice;
    multichoice.load();
    std::unordered_map<std::string, int> item_prices;
    {
        std::ifstream pf("assets/items/prices.tsv");
        std::string ln;
        while (std::getline(pf, ln)) {
            size_t tab = ln.find('\t');
            if (tab == std::string::npos) continue;
            item_prices[ln.substr(0, tab)] = std::atoi(ln.c_str() + tab + 1);
        }
    }
    Shop shop;
    shop.load();
    shop.configure(&gs, &item_prices);
    HealFx healfx;
    healfx.load();
    DebugMenu debugmenu;
    debugmenu.load();
    SpeciesPicker speciespicker;
    speciespicker.load();
    speciespicker.configure(&bdata);
    // Story-accurate new game: an empty bag (just the starting 3000 money,
    // GameState's own default) and 0 Game Corner coins, same as pokeemerald --
    // items, TMs and coins all come from actually playing the story.
    // demo hook: grant EXP to the starter to show level-ups / evolution
    if (const char* xe = std::getenv("CODEMON_GRANT_EXP"); xe && !team.empty()) {
        std::vector<std::string> xm;
        bdata.grant_exp(team[0], atol(xe), xm);
        std::string joined;
        for (size_t i = 0; i < xm.size(); ++i) { if (i) joined += '\x1f'; joined += xm[i]; }
        if (!joined.empty()) box.open("", joined);
    }
    bool force_enc = std::getenv("CODEMON_FORCE_ENCOUNTER") != nullptr;

    // map-name banner + warp fade-in state
    std::string banner = pretty_map(start_map);
    float banner_t = 2.2f, fade = 1.0f;
    sf::Font ban_font; ban_font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    auto on_map_change = [&](const std::string& path, Audio* aud) {
        banner = pretty_map(path); banner_t = 2.2f; fade = 1.0f;
        menu.set_location(banner);
        menu.set_mapsec(sess->map->has_mapsec(), sess->map->mapsec_x(),
                         sess->map->mapsec_y(), sess->map->mapsec_w(), sess->map->mapsec_h());
        if (aud) aud->play_bgm(sess->map->music());
    };
    // A script-driven `warp` (e.g. Route 101 Birch's bag sending the player
    // to his lab after picking a starter) swaps the session the same way
    // stepping onto a warp tile does.
    auto do_pending_warp = [&](Audio* aud) {
        if (!vm.has_pending_warp()) return;
        std::string dest; int wx, wy;
        vm.get_pending_warp(dest, wx, wy);
        vm.clear_pending_warp();
        if (dest == "-") return;
        Session* ns = load_session("maps/" + dest + ".map", wx, wy, &gs);
        if (!ns->map->ready()) { free_session(ns); return; }
        free_session(sess);
        sess = ns;
        vm.configure(sess->map, &gs, &box, &battle, aud, sess->player, &sess->actors, &sess->localid_map);
        run_load_triggers(sess->map, gs, vm);
        check_trigger(sess, vm, gs);
        on_map_change(sess->path, aud);
    };
    // FLIEGEN: the menu can't touch the session either (same reasoning as
    // do_pending_warp above), so it just names a destination and the game
    // loop performs the actual load_session + player placement.
    auto do_pending_fly = [&](Audio* aud) {
        if (!menu.wants_fly()) return;
        std::string dest; int fx, fy;
        menu.fly_destination(dest, fx, fy);
        menu.ack_fly();
        Session* ns = load_session("maps/" + dest + ".map", fx, fy, &gs);
        if (!ns->map->ready()) { free_session(ns); return; }
        free_session(sess);
        sess = ns;
        sess->player->face(DIR::S);
        vm.configure(sess->map, &gs, &box, &battle, aud, sess->player, &sess->actors, &sess->localid_map);
        run_load_triggers(sess->map, gs, vm);
        check_trigger(sess, vm, gs);
        on_map_change(sess->path, aud);
    };
    menu.set_location(banner);
    menu.set_mapsec(sess->map->has_mapsec(), sess->map->mapsec_x(),
                     sess->map->mapsec_y(), sess->map->mapsec_w(), sess->map->mapsec_h());

    // Map a walk token to a battle button (for scripted battle demos).
    auto token_btn = [](char t) -> BtnInput {
        switch (t) { case 'N': return BTN_UP; case 'S': return BTN_DOWN;
                     case 'W': return BTN_LEFT; case 'E': return BTN_RIGHT;
                     default: return BTN_CONFIRM; }
    };

    // Whiteout recovery for battles the VM never knew about (random wild
    // encounters started directly by try_encounter()); ScriptVM handles its
    // own scripted battles (trainerbattle/dowildbattle) itself, the same way.
    bool battle_was_active = false;
    auto handle_whiteout = [&](Audio* aud) {
        bool battle_just_ended = battle_was_active && !battle.active() && !vm.running();
        if (battle_just_ended && !battle.won()) {
            for (Mon& m : team) {
                m.hp = m.max_hp;
                m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
                bdata.restore_pp(m);
            }
            if (!gs.last_heal_map.empty()) {
                Session* ns = load_session("maps/" + gs.last_heal_map + ".map",
                                           gs.last_heal_x, gs.last_heal_y, &gs);
                if (ns->map->ready()) {
                    free_session(sess);
                    sess = ns;
                    vm.configure(sess->map, &gs, &box, &battle, aud, sess->player, &sess->actors, &sess->localid_map);
                    run_load_triggers(sess->map, gs, vm);
                    check_trigger(sess, vm, gs);
                    on_map_change(sess->path, aud);
                } else {
                    free_session(ns);
                }
            }
        } else if (battle_just_ended && aud) {
            // Won/fled/caught: same map, just resume its own music over the
            // battle theme (a loss already resumed it via on_map_change
            // above, whiteout warping to the last heal spot).
            aud->play_bgm(sess->map->music());
        }
        battle_was_active = battle.active();
    };

    // --- headless screenshot / animation mode ------------------------------
    if (const char* shot = std::getenv("CODEMON_SCREENSHOT")) {
        int frames = 1;
        if (const char* fe = std::getenv("CODEMON_FRAMES")) frames = std::max(1, atoi(fe));
        std::vector<char> walk = parse_walk(std::getenv("CODEMON_WALK"));
        sf::RenderTexture rt;
        bool pending_surf = false;   // Ja/Nein prompt is up for a Surf attempt
        bool pending_waterfall = false;   // ... for a Waterfall climb attempt
        if (rt.create(win_w, win_h)) {
            for (int i = 0; i < frames; ++i) {
                if (i > 0) {
                    handle_whiteout(nullptr);
                    char tok = ((size_t)(i - 1) < walk.size()) ? walk[i - 1] : 0;
                    if (starter.active()) {
                        if (tok) starter.input(token_btn(tok));
                    } else if (yesno.active()) {
                        if (tok) yesno.input(token_btn(tok));
                    } else if (picker.active()) {
                        if (tok) picker.input(token_btn(tok));
                    } else if (multichoice.active()) {
                        if (tok) multichoice.input(token_btn(tok));
                    } else if (shop.active()) {
                        if (tok) shop.input(token_btn(tok));
                    } else if (battle.active()) {
                        if (tok) battle.input(token_btn(tok));
                    } else if (games.active()) {
                        if (tok) games.input(token_btn(tok));
                    } else if (tok == 'G') {
                        games.open();
                    } else if (menu.active()) {
                        if (tok == 'M') menu.close();
                        else if (tok) menu.input(token_btn(tok));
                    } else if (tok == 'M') {
                        menu.open();
                    } else if (vm.running()) {
                        if (tok == 'T') vm.on_key();
                        vm.update(0.13f);
                    } else if (tok == 'T') {
                        interact(sess, box, nullptr, vm);
                    } else if (!box.is_active()) {
                        if (tok) {
                            int pbx = sess->player->get_tile_x();
                            int pby = sess->player->get_tile_y();
                            Session* before = sess;
                            bool needs_surf = false, needs_waterfall = false;
                            sess = player_step(sess, char_to_dir(tok), nullptr, &gs, false, &needs_surf,
                                               false, &needs_waterfall);
                            if (needs_surf && team_knows_move(team, "SURF")) {
                                pending_surf = true;
                                yesno.open("Das Wasser schimmert tiefblau... Möchtest du SURFER einsetzen?");
                            } else if (needs_waterfall && team_knows_move(team, "WATERFALL")) {
                                pending_waterfall = true;
                                yesno.open("Ein gewaltiger Wasserfall stürzt herab... Möchtest du WASSERFALL einsetzen?");
                            }
                            if (sess != before) {
                                vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player, &sess->actors, &sess->localid_map);
    run_load_triggers(sess->map, gs, vm);
    check_trigger(sess, vm, gs);
                                on_map_change(sess->path, nullptr);
                            } else if (sess->player->get_tile_x() != pbx ||
                                       sess->player->get_tile_y() != pby) {
                                // No wild encounters before the player has a
                                // Pokemon to send out (matches the story: you
                                // can't be jumped in the grass while trailing
                                // Birch with an empty team).
                                if (!team.empty())
                                    try_encounter(sess, battle, team[0], rng, force_enc);
                                check_trigger(sess, vm, gs);
                            }
                        }
                        if (walk.empty()) tick_npcs(sess, rng);
                    }
                    // Route 101's Birch's-bag script blocks on `special
                    // ChooseStarter`; show the real chooser and feed the
                    // pick back so the script (and its Pokedex/heal/warp
                    // follow-up) continues.
                    if (starter.done()) {
                        if (vm.wants_starter()) vm.resolve_starter(starter.chosen());
                        else if (team.empty()) team.push_back(bdata.make_mon(starter.chosen(), 5, &rng));
                        else team[0] = bdata.make_mon(starter.chosen(), 5, &rng);
                        gs.mark_caught(starter.chosen());
                        starter.ack();
                    } else if (!starter.active() && vm.wants_starter()) {
                        starter.open();
                    }
                    if (pending_surf && yesno.done()) {
                        pending_surf = false;
                        if (yesno.yes()) {
                            int pbx = sess->player->get_tile_x();
                            int pby = sess->player->get_tile_y();
                            Session* before = sess;
                            sess = player_step(sess, sess->player->get_facing(), nullptr, &gs, true);
                            if (sess != before) {
                                vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player, &sess->actors, &sess->localid_map);
                                run_load_triggers(sess->map, gs, vm);
                                check_trigger(sess, vm, gs);
                                on_map_change(sess->path, nullptr);
                            } else if (sess->player->get_tile_x() != pbx ||
                                       sess->player->get_tile_y() != pby) {
                                if (!team.empty())
                                    try_encounter(sess, battle, team[0], rng, force_enc);
                                check_trigger(sess, vm, gs);
                            }
                        }
                        yesno.ack();
                    } else if (pending_waterfall && yesno.done()) {
                        pending_waterfall = false;
                        if (yesno.yes()) {
                            int pbx = sess->player->get_tile_x();
                            int pby = sess->player->get_tile_y();
                            Session* before = sess;
                            sess = player_step(sess, sess->player->get_facing(), nullptr, &gs, false, nullptr, true);
                            if (sess != before) {
                                vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player, &sess->actors, &sess->localid_map);
                                run_load_triggers(sess->map, gs, vm);
                                check_trigger(sess, vm, gs);
                                on_map_change(sess->path, nullptr);
                            } else if (sess->player->get_tile_x() != pbx ||
                                       sess->player->get_tile_y() != pby) {
                                if (!team.empty())
                                    try_encounter(sess, battle, team[0], rng, force_enc);
                                check_trigger(sess, vm, gs);
                            }
                        }
                        yesno.ack();
                    } else if (yesno.done()) {
                        if (vm.wants_yesno()) vm.resolve_yesno(yesno.yes());
                        yesno.ack();
                    } else if (!yesno.active() && vm.wants_yesno()) {
                        yesno.open();
                    }
                    if (picker.done()) {
                        if (vm.wants_choose_party_mon()) vm.resolve_choose_party_mon(picker.chosen());
                        picker.ack();
                    } else if (!picker.active() && vm.wants_choose_party_mon()) {
                        picker.open();
                    }
                    if (multichoice.done()) {
                        if (vm.wants_multichoice()) vm.resolve_multichoice(multichoice.chosen());
                        multichoice.ack();
                    } else if (!multichoice.active() && vm.wants_multichoice()) {
                        multichoice.open(vm.multichoice_options(), vm.multichoice_default());
                    }
                    if (shop.done()) {
                        vm.close_shop();
                        shop.ack();
                    } else if (!shop.active() && vm.wants_shop()) {
                        const std::vector<std::string>* sitems = vm.shop_items();
                        if (sitems && !sitems->empty()) shop.open(*sitems);
                        else vm.close_shop();
                    }
                    if (!healfx.active() && vm.wants_heal_fx()) healfx.start((int)team.size());
                    healfx.tick(0.13f);
                    if (menu.wants_save()) {
                        bool ok = SaveGame::save(SAVE_PATH, gs, team, pc_box, sess->path,
                                                  sess->player->get_tile_x(), sess->player->get_tile_y());
                        menu.set_flash(ok ? "Spiel gespeichert!" : "Speichern fehlgeschlagen.");
                        menu.ack_save();
                    }
                    do_pending_warp(nullptr);
                    do_pending_fly(nullptr);
                    battle.tick(0.13f);
                    games.tick(0.13f);
                    if (banner_t > 0.f) banner_t -= 0.13f;
                    if (fade > 0.f) fade -= 0.13f * 1.6f;
                }
                rt.clear(sf::Color(40, 72, 56));
                if (starter.active()) starter.draw(rt);
                else if (shop.active()) shop.draw(rt);
                else if (battle.active()) battle.draw(rt);
                else if (games.active()) games.draw(rt);
                else {
                    draw_scene(rt, sess, &healfx, &vm); box.draw(rt);
                    draw_banner(rt, ban_font, banner, banner_t);
                    menu.draw(rt);
                    if (yesno.active()) yesno.draw(rt);
                    if (picker.active()) picker.draw(rt);
                    if (multichoice.active()) multichoice.draw(rt);
                    if (fade > 0.f) {
                        rt.setView(rt.getDefaultView());
                        sf::RectangleShape f(rt.getView().getSize());
                        f.setFillColor(sf::Color(0, 0, 0, (sf::Uint8)(std::min(1.f, fade) * 255)));
                        rt.draw(f);
                    }
                }
                rt.display();
                char name[512];
                if (frames == 1) std::snprintf(name, sizeof(name), "%s", shot);
                else std::snprintf(name, sizeof(name), "%s_%03d.png", shot, i);
                rt.getTexture().copyToImage().saveToFile(name);
            }
        }
        free_session(sess);
        return 0;
    }

    // --- interactive game --------------------------------------------------
    Audio audio; audio.load("assets");
    audio.play_bgm(sess->map->music());
    battle.set_audio(&audio);
    // Re-attach the real Audio* now that it exists -- run_load_triggers()
    // already ran once for this starting session above (nullptr Audio), and
    // must NOT run again here: it isn't idempotent for every onload script
    // (only ones that fully complete before their first blocking op are),
    // so calling it twice in a row can silently double a `giveitem`/`setvar`
    // that comes before a script's first msgbox/movement wait -- invisible
    // to the player (they only ever see the one on-screen dialogue) but
    // repeating on every single launch while the guard condition still holds.
    vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player, &sess->actors, &sess->localid_map);
    Window scr(win_w, win_h, "Codemon!");

    sf::Clock clock; float npc_accum = 0.f; float move_cooldown = 0.f;
    bool pending_surf = false;   // Ja/Nein prompt is up for a Surf attempt
    bool pending_waterfall = false;   // ... for a Waterfall climb attempt
    std::string pending_dive;         // ... for a Dive/emerge attempt
    int pending_dive_x = -1, pending_dive_y = -1;   // -1 = keep the current tile
    while (scr.get_window()->isOpen()) {
        handle_whiteout(&audio);
        sf::Event event;
        while (scr.get_event(&event)) {
            if (event.type == sf::Event::Closed) scr.close();
            else if (event.type == sf::Event::KeyPressed) {
                if (starter.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::A: starter.input(BTN_LEFT); break;
                    case sf::Keyboard::D: starter.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: starter.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (yesno.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: yesno.input(BTN_UP); break;
                    case sf::Keyboard::S: yesno.input(BTN_DOWN); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: yesno.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (picker.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: picker.input(BTN_UP); break;
                    case sf::Keyboard::S: picker.input(BTN_DOWN); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: picker.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (multichoice.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: multichoice.input(BTN_UP); break;
                    case sf::Keyboard::S: multichoice.input(BTN_DOWN); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: multichoice.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (shop.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: shop.input(BTN_UP); break;
                    case sf::Keyboard::S: shop.input(BTN_DOWN); break;
                    case sf::Keyboard::A: shop.input(BTN_LEFT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: shop.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (speciespicker.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: speciespicker.input(BTN_UP); break;
                    case sf::Keyboard::S: speciespicker.input(BTN_DOWN); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: speciespicker.input(BTN_CONFIRM); break;
                    case sf::Keyboard::BackSpace: speciespicker.cancel(); break;
                    default: break;
                    }
                    if (speciespicker.done()) {
                        if (team.size() < 6)
                            team.push_back(bdata.make_mon(speciespicker.chosen(), 25, &rng));
                        speciespicker.ack();
                    }
                } else if (debugmenu.active()) {
                    int action = -1;
                    switch (event.key.code) {
                    case sf::Keyboard::W: debugmenu.input(BTN_UP); break;
                    case sf::Keyboard::S: debugmenu.input(BTN_DOWN); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: action = debugmenu.input(BTN_CONFIRM); break;
                    case sf::Keyboard::H: debugmenu.close(); break;
                    default: break;
                    }
                    if (action >= 0) {
                        switch (action) {
                        case DebugMenu::HEAL_TEAM:
                            for (Mon& m : team) {
                                m.hp = m.max_hp;
                                m.status = Status::NONE; m.status_turns = 0; m.confusion_turns = 0;
                                bdata.restore_pp(m);
                            }
                            break;
                        case DebugMenu::ADD_MONEY:
                            gs.money += 50000;
                            break;
                        case DebugMenu::ALL_BADGES:
                            for (int i = 1; i <= 8; ++i)
                                gs.set_flag("FLAG_BADGE0" + std::to_string(i) + "_GET");
                            break;
                        case DebugMenu::GIVE_STARTER:
                            starter.open();
                            break;
                        case DebugMenu::GIVE_ANY_POKEMON:
                            speciespicker.open();
                            break;
                        case DebugMenu::TEACH_HMS:
                            if (!team.empty()) {
                                Mon& m = team[0];
                                static const char* HMS[] = {
                                    "CUT", "SURF", "STRENGTH", "WATERFALL",
                                    "FLY", "DIVE", "ROCK_SMASH"};
                                for (const char* mv : HMS) {
                                    if (m.moves.size() >= 4) break;
                                    if (std::find(m.moves.begin(), m.moves.end(), mv) != m.moves.end())
                                        continue;
                                    const MoveInfo* mi = bdata.move(mv);
                                    m.moves.push_back(mv);
                                    m.pp.push_back(mi ? mi->pp : 20);
                                }
                                gs.set_flag("FLAG_SYS_USE_STRENGTH");
                            }
                            break;
                        case DebugMenu::GIVE_ITEMS:
                            gs.give_item("ITEM_POKE_BALL", 99);
                            gs.give_item("ITEM_POTION", 99);
                            gs.give_item("ITEM_RARE_CANDY", 99);
                            break;
                        case DebugMenu::GIVE_XP: {
                            std::vector<std::string> xm;   // level-up/evolution
                            for (Mon& m : team)             // text, unused here
                                bdata.grant_exp(m, 1000, xm);
                            break;
                        }
                        case DebugMenu::SKIP_SCRIPT:
                            if (vm.running()) vm.abort();
                            if (box.is_active()) box.close();
                            break;
                        default: break;   // CLOSE: nothing else to do
                        }
                    }
                } else if (event.key.code == sf::Keyboard::H &&
                           !box.is_active() && !vm.running() && !menu.active() &&
                           !battle.active() && !shop.active() && !games.active()) {
                    debugmenu.open();
                } else if (battle.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: battle.input(BTN_UP); break;
                    case sf::Keyboard::S: battle.input(BTN_DOWN); break;
                    case sf::Keyboard::A: battle.input(BTN_LEFT); break;
                    case sf::Keyboard::D: battle.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: battle.input(BTN_CONFIRM); break;
                    case sf::Keyboard::BackSpace: battle.input(BTN_CANCEL); break;
                    default: break;
                    }
                } else if (games.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: games.input(BTN_UP); break;
                    case sf::Keyboard::S: games.input(BTN_DOWN); break;
                    case sf::Keyboard::A: games.input(BTN_LEFT); break;
                    case sf::Keyboard::D: games.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: games.input(BTN_CONFIRM); break;
                    default: break;
                    }
                } else if (event.key.code == sf::Keyboard::G &&
                           !box.is_active() && !vm.running() && !menu.active()) {
                    games.open();
                } else if (menu.active()) {
                    switch (event.key.code) {
                    case sf::Keyboard::W: menu.input(BTN_UP); break;
                    case sf::Keyboard::S: menu.input(BTN_DOWN); break;
                    case sf::Keyboard::A: menu.input(BTN_LEFT); break;
                    case sf::Keyboard::D: menu.input(BTN_RIGHT); break;
                    case sf::Keyboard::Space:
                    case sf::Keyboard::Return: menu.input(BTN_CONFIRM); break;
                    case sf::Keyboard::M: menu.close(); break;
                    default: break;
                    }
                    // The OPTIONS screen's "Ton" row toggles gs.sound_on
                    // directly; sync Audio's live mute state right after
                    // any menu input that could have flipped it.
                    audio.set_muted(!gs.sound_on);
                } else if (event.key.code == sf::Keyboard::M &&
                           !box.is_active() && !vm.running()) {
                    menu.open();
                } else if (event.key.code == sf::Keyboard::Space ||
                           event.key.code == sf::Keyboard::Return) {
                    if (vm.running()) { vm.on_key(); }       // advance a script message
                    else {
                        int dvx = -1, dvy = -1;
                        std::string dive_to = dive_target(sess, team, gs, dvx, dvy);
                        if (!dive_to.empty() && !box.is_active()) {
                            pending_dive = dive_to;
                            pending_dive_x = dvx; pending_dive_y = dvy;
                            yesno.open(sess->map->emerge_dest().empty()
                                ? "Das Wasser ist hier tiefblau... Möchtest du TAUCHEN einsetzen?"
                                : "Über dir schimmert Licht... Möchtest du auftauchen?");
                        } else {
                            interact(sess, box, &audio, vm);  // talk / advance / dismiss
                        }
                    }
                }
            }
        }
        float dt = clock.restart().asSeconds();
        for (Character* a : sess->actors) a->tick(dt);
        // Overworld movement: polled every frame (not event-driven) so it
        // runs at a steady, predictable cadence instead of the OS's key
        // repeat timing -- that mismatch (a long initial delay, then an
        // irregular repeat rate) was what made walking feel laggy/choppy.
        // Tapping a new direction just turns to face it first, like the
        // real games; holding it keeps stepping every MOVE_INTERVAL.
        {
            bool ui_blocked = starter.active() || yesno.active() || picker.active() ||
                              multichoice.active() ||
                              shop.active() || battle.active() || games.active() ||
                              menu.active() || debugmenu.active() || speciespicker.active() ||
                              box.is_active() || vm.running() ||
                              pending_surf || pending_waterfall ||
                              !pending_dive.empty();
            bool run_held = !ui_blocked && gs.flag("FLAG_SYS_B_DASH") &&
                            (sf::Keyboard::isKeyPressed(sf::Keyboard::LShift) ||
                             sf::Keyboard::isKeyPressed(sf::Keyboard::RShift));
            float interval = run_held ? RUN_MOVE_INTERVAL : MOVE_INTERVAL;
            sess->player->set_running(run_held);
            DIR held = DIR::NONE;
            if (!ui_blocked) {
                if (sf::Keyboard::isKeyPressed(sf::Keyboard::W)) held = DIR::N;
                else if (sf::Keyboard::isKeyPressed(sf::Keyboard::S)) held = DIR::S;
                else if (sf::Keyboard::isKeyPressed(sf::Keyboard::A)) held = DIR::W;
                else if (sf::Keyboard::isKeyPressed(sf::Keyboard::D)) held = DIR::E;
            }
            if (held == DIR::NONE) {
                if (!ui_blocked) sess->player->set_idle();
                move_cooldown = 0.f;
            } else if (sess->player->get_facing() != held) {
                sess->player->face(held);
                move_cooldown = interval;
            } else {
                move_cooldown -= dt;
                if (move_cooldown <= 0.f) {
                    int pbx = sess->player->get_tile_x();
                    int pby = sess->player->get_tile_y();
                    Session* before = sess;
                    bool needs_surf = false, needs_waterfall = false;
                    sess = player_step(sess, held, &audio, &gs, false, &needs_surf,
                                       false, &needs_waterfall);
                    move_cooldown += interval;
                    if (needs_surf && team_knows_move(team, "SURF")) {
                        pending_surf = true;
                        yesno.open("Das Wasser schimmert tiefblau... Möchtest du SURFER einsetzen?");
                    } else if (needs_waterfall && team_knows_move(team, "WATERFALL")) {
                        pending_waterfall = true;
                        yesno.open("Ein gewaltiger Wasserfall stürzt herab... Möchtest du WASSERFALL einsetzen?");
                    }
                    if (sess != before) {
                        vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player, &sess->actors, &sess->localid_map);
                        run_load_triggers(sess->map, gs, vm);
                        check_trigger(sess, vm, gs);
                        on_map_change(sess->path, &audio);
                    } else if (sess->player->get_tile_x() != pbx ||
                               sess->player->get_tile_y() != pby) {
                        if (!team.empty())
                            try_encounter(sess, battle, team[0], rng, false);
                        check_trigger(sess, vm, gs);
                    }
                }
            }
        }
        if (starter.done()) {
            if (vm.wants_starter()) vm.resolve_starter(starter.chosen());
            else if (team.empty()) team.push_back(bdata.make_mon(starter.chosen(), 5, &rng));
                        else team[0] = bdata.make_mon(starter.chosen(), 5, &rng);
            gs.mark_caught(starter.chosen());
            starter.ack();
        } else if (!starter.active() && vm.wants_starter()) {
            starter.open();
        }
        if (!pending_dive.empty() && yesno.done()) {
            std::string dest = pending_dive;
            pending_dive.clear();
            if (yesno.yes()) {
                // Same tile on the map above/below -- the pair is aligned.
                const int ax = (pending_dive_x >= 0) ? pending_dive_x
                                                     : sess->player->get_tile_x();
                const int ay = (pending_dive_y >= 0) ? pending_dive_y
                                                     : sess->player->get_tile_y();
                Session* ns = load_session("maps/" + dest + ".map", ax, ay, &gs);
                if (ns->map->ready()) {
                    // Underwater and open sea both count as being on the
                    // water, so surfacing onto ocean keeps the player surfing
                    // rather than stranding them on a tile they can't stand on.
                    // A setdivewarp can also land on dry ground (the Abandoned
                    // Ship's flooded rooms), where surfing must end.
                    ns->player->set_surfing(
                        ns->map->is_water(ax, ay) || !ns->map->emerge_dest().empty()
                        || !ns->map->dive_dest().empty());
                    ns->player->face(sess->player->get_facing());
                    free_session(sess);
                    sess = ns;
                    vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player,
                                 &sess->actors, &sess->localid_map);
                    run_load_triggers(sess->map, gs, vm);
                    check_trigger(sess, vm, gs);
                    on_map_change(sess->path, &audio);
                } else {
                    free_session(ns);
                }
            }
            yesno.ack();
        } else if (pending_surf && yesno.done()) {
            pending_surf = false;
            if (yesno.yes()) {
                int pbx = sess->player->get_tile_x();
                int pby = sess->player->get_tile_y();
                Session* before = sess;
                sess = player_step(sess, sess->player->get_facing(), &audio, &gs, true);
                if (sess != before) {
                    vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player, &sess->actors, &sess->localid_map);
                    run_load_triggers(sess->map, gs, vm);
                    check_trigger(sess, vm, gs);
                    on_map_change(sess->path, &audio);
                } else if (sess->player->get_tile_x() != pbx ||
                           sess->player->get_tile_y() != pby) {
                    if (!team.empty())
                        try_encounter(sess, battle, team[0], rng, false);
                    check_trigger(sess, vm, gs);
                }
            }
            yesno.ack();
        } else if (pending_waterfall && yesno.done()) {
            pending_waterfall = false;
            if (yesno.yes()) {
                int pbx = sess->player->get_tile_x();
                int pby = sess->player->get_tile_y();
                Session* before = sess;
                sess = player_step(sess, sess->player->get_facing(), &audio, &gs, false, nullptr, true);
                if (sess != before) {
                    vm.configure(sess->map, &gs, &box, &battle, &audio, sess->player, &sess->actors, &sess->localid_map);
                    run_load_triggers(sess->map, gs, vm);
                    check_trigger(sess, vm, gs);
                    on_map_change(sess->path, &audio);
                } else if (sess->player->get_tile_x() != pbx ||
                           sess->player->get_tile_y() != pby) {
                    if (!team.empty())
                        try_encounter(sess, battle, team[0], rng, false);
                    check_trigger(sess, vm, gs);
                }
            }
            yesno.ack();
        } else if (yesno.done()) {
            if (vm.wants_yesno()) vm.resolve_yesno(yesno.yes());
            yesno.ack();
        } else if (!yesno.active() && vm.wants_yesno()) {
            yesno.open();
        }
        if (picker.done()) {
            if (vm.wants_choose_party_mon()) vm.resolve_choose_party_mon(picker.chosen());
            picker.ack();
        } else if (!picker.active() && vm.wants_choose_party_mon()) {
            picker.open();
        }
        if (multichoice.done()) {
            if (vm.wants_multichoice()) vm.resolve_multichoice(multichoice.chosen());
            multichoice.ack();
        } else if (!multichoice.active() && vm.wants_multichoice()) {
            multichoice.open(vm.multichoice_options(), vm.multichoice_default());
        }
        if (shop.done()) {
            vm.close_shop();
            shop.ack();
        } else if (!shop.active() && vm.wants_shop()) {
            const std::vector<std::string>* sitems = vm.shop_items();
            if (sitems && !sitems->empty()) shop.open(*sitems);
            else vm.close_shop();
        }
        if (!healfx.active() && vm.wants_heal_fx()) healfx.start((int)team.size());
        if (menu.wants_save()) {
            bool ok = SaveGame::save(SAVE_PATH, gs, team, pc_box, sess->path,
                                      sess->player->get_tile_x(), sess->player->get_tile_y());
            menu.set_flash(ok ? "Spiel gespeichert!" : "Speichern fehlgeschlagen.");
            menu.ack_save();
        }
        do_pending_warp(&audio);
        do_pending_fly(&audio);
        healfx.tick(dt);
        if (vm.running()) vm.update(dt);
        battle.tick(dt);
        games.tick(dt);
        if (banner_t > 0.f) banner_t -= dt;
        if (fade > 0.f) fade -= dt * 1.6f;
        // NPCs freeze while a dialog/battle/script/menu/minigame is running.
        npc_accum += dt;
        if (!box.is_active() && !battle.active() && !vm.running() &&
            !menu.active() && !games.active()) {
            while (npc_accum >= NPC_TICK) { tick_npcs(sess, rng); npc_accum -= NPC_TICK; }
        } else {
            npc_accum = 0.f;
        }

        scr.clear();
        if (starter.active()) { starter.draw(*scr.get_window()); }
        else if (shop.active()) { shop.draw(*scr.get_window()); }
        else if (battle.active()) { battle.draw(*scr.get_window()); }
        else if (games.active()) { games.draw(*scr.get_window()); }
        else {
            draw_scene(*scr.get_window(), sess, &healfx, &vm);
            box.draw(*scr.get_window());
            draw_banner(*scr.get_window(), ban_font, banner, banner_t);
            menu.draw(*scr.get_window());
            if (yesno.active()) yesno.draw(*scr.get_window());
            if (picker.active()) picker.draw(*scr.get_window());
            if (multichoice.active()) multichoice.draw(*scr.get_window());
            if (debugmenu.active()) debugmenu.draw(*scr.get_window());
            if (speciespicker.active()) speciespicker.draw(*scr.get_window());
            if (fade > 0.f) {
                sf::View sv = scr.get_window()->getView();
                scr.get_window()->setView(scr.get_window()->getDefaultView());
                sf::RectangleShape f(scr.get_window()->getView().getSize());
                f.setFillColor(sf::Color(0, 0, 0, (sf::Uint8)(std::min(1.f, fade) * 255)));
                scr.get_window()->draw(f);
                scr.get_window()->setView(sv);
            }
        }
        scr.display();
        sf::sleep(sf::milliseconds(16));
    }

    free_session(sess);
    return 0;
}
