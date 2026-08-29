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
#include "HealFx.h"
#include "StarterSelect.h"
#include "YesNoPrompt.h"
#include "PartyPicker.h"
#include "MultiChoicePrompt.h"
#include "DebugMenu.h"
#include "Shop.h"
#include "TitleScreen.h"
#include "GenderSelect.h"
#include "EarlyAccessNotice.h"
#include "NameEntry.h"
#include "InputRouter.h"

#include <algorithm>
#include <cctype>
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


static void draw_scene(sf::RenderTarget& target, Session* s, const HealFx* healfx = nullptr) {
    target.setView(camera_for(s));
    s->map->render_to(target);
    std::sort(s->actors.begin(), s->actors.end(),
              [](Character* a, Character* b) { return a->get_tile_y() < b->get_tile_y(); });
    for (Character* a : s->actors) {
        a->update_sprite(s->map->get_tile_size());
        target.draw(*a->get_current_sprite());
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

// Game - owns every piece of session/UI state that used to live as locals
// and capturing lambdas inside main(), and drives the two run modes
// (headless screenshot/animation, real interactive play). Splitting this out
// of main() doesn't change any behavior: every method body below is the same
// code that used to run inline, just addressing what were main()'s locals as
// member variables instead (a lambda capturing `sess` by reference and a
// member function reading `this->sess` see the same thing).
class Game {
public:
    int run();

private:
    void start_new_game();
    void run_title_and_character_creation();
    void run_headless();
    void run_interactive();

    void on_map_change(const std::string& path, Audio* aud);
    void do_pending_warp(Audio* aud);
    void do_pending_fly(Audio* aud);
    void handle_whiteout(Audio* aud);

    // Map a walk token to a battle button (for scripted battle demos).
    static BtnInput token_btn(char t) {
        switch (t) { case 'N': return BTN_UP; case 'S': return BTN_DOWN;
                     case 'W': return BTN_LEFT; case 'E': return BTN_RIGHT;
                     default: return BTN_CONFIRM; }
    }

    static constexpr const char* SAVE_PATH = "savegame.dat";

    const char* map_env = nullptr;
    std::mt19937 rng{1234};
    GameState gs;
    BattleData bdata;
    std::vector<Mon> team;      // the player's party
    std::vector<Mon> pc_box;    // PC storage

    std::string start_map;
    int start_x = -1, start_y = -1;
    bool resumed = false;

    Session* sess = nullptr;
    unsigned win_w = 0, win_h = 0;

    DialogBox box;
    Battle battle;
    ScriptVM vm;
    Menu menu;
    Minigame games;
    StarterSelect starter;
    YesNoPrompt yesno;
    PartyPicker picker;
    MultiChoicePrompt multichoice;
    std::unordered_map<std::string, int> item_prices;
    Shop shop;
    HealFx healfx;
    DebugMenu debugmenu;
    bool force_enc = false;

    // map-name banner + warp fade-in state
    std::string banner;
    float banner_t = 2.2f, fade = 1.0f;
    sf::Font ban_font;

    // Whiteout recovery for battles the VM never knew about (random wild
    // encounters started directly by try_encounter()); ScriptVM handles its
    // own scripted battles (trainerbattle/dowildbattle) itself, the same way.
    bool battle_was_active = false;
};

// Story start: the player rides in on the moving truck (InsideOfTruck),
// which is where pokeemerald's own new-game intro begins -- its own
// imported script (checkplayergender + SetIntroFlagsMale/Female) hides
// the unused house's occupants, sets the respawn point and points the
// truck's WARP_ID_DYNAMIC exit at the right house, all keyed off
// gs.female (see below). Called both for a genuinely fresh run and when
// "NEUES SPIEL" is chosen on the title screen after a save was already
// loaded (discarding it, same as any other save file that's simply never
// opened again).
void Game::start_new_game() {
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
}

// Real interactive play only: headless screenshot tests and CODEMON_MAP
// demos need deterministic, immediate map loading, same reasoning as every
// other CODEMON_* test hook bypassing normal flow.
void Game::run_title_and_character_creation() {
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

void Game::on_map_change(const std::string& path, Audio* aud) {
    banner = pretty_map(path); banner_t = 2.2f; fade = 1.0f;
    menu.set_location(banner);
    menu.set_mapsec(sess->map->has_mapsec(), sess->map->mapsec_x(),
                     sess->map->mapsec_y(), sess->map->mapsec_w(), sess->map->mapsec_h());
    if (aud) aud->play_bgm(sess->map->music());
}

// A script-driven `warp` (e.g. Route 101 Birch's bag sending the player
// to his lab after picking a starter) swaps the session the same way
// stepping onto a warp tile does.
void Game::do_pending_warp(Audio* aud) {
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
}

// FLIEGEN: the menu can't touch the session either (same reasoning as
// do_pending_warp above), so it just names a destination and the game
// loop performs the actual load_session + player placement.
void Game::do_pending_fly(Audio* aud) {
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
}

void Game::handle_whiteout(Audio* aud) {
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
}

int Game::run() {
    map_env = std::getenv("CODEMON_MAP");

    bdata.load("assets/battle");
    team.reserve(6);   // keep &team[0] stable

    // A saved run resumes exactly where it left off (map, position, flags,
    // bag, money, party, PC box). CODEMON_MAP/CODEMON_NO_SAVE force a fresh
    // start for demos/tests even when a savegame.dat is lying around.
    if (!map_env && !std::getenv("CODEMON_NO_SAVE")) {
        resumed = SaveGame::load(SAVE_PATH, gs, team, pc_box, start_map, start_x, start_y);
        // A save written before PP tracking existed has no `pp` field at all
        // for its party/box mons -- give them full PP rather than leaving
        // the vector empty (which would read as "0 PP, can't move").
        for (Mon& m : team) if (m.pp.size() != m.moves.size()) bdata.restore_pp(m);
        for (Mon& m : pc_box) if (m.pp.size() != m.moves.size()) bdata.restore_pp(m);
    }
    if (!resumed) start_new_game();

    // --- title screen --------------------------------------------------
    if (!g_headless && !map_env) run_title_and_character_creation();

    // CODEMON_START_X/Y (with CODEMON_MAP): land on a specific tile instead
    // of the map's own default start position -- for headlessly reaching an
    // object/NPC that isn't right next to the map's normal entrance.
    if (map_env) {
        if (const char* ex = std::getenv("CODEMON_START_X")) start_x = atoi(ex);
        if (const char* ey = std::getenv("CODEMON_START_Y")) start_y = atoi(ey);
    }
    sess = load_session(start_map, start_x, start_y, &gs);

    win_w = VIEW_TW * sess->map->get_tile_size() * SCALE;
    win_h = VIEW_TH * sess->map->get_tile_size() * SCALE;

    box.load_font();
    box.configure(&gs);
    battle.configure(&bdata, &rng);
    battle.set_capture(&gs, &team, &pc_box);
    vm.set_battle_data(&bdata, &team, &rng, &pc_box);
    vm.configure(sess->map, &gs, &box, &battle, nullptr, sess->player, &sess->actors, &sess->localid_map);
    run_load_triggers(sess->map, gs, vm);
    check_trigger(sess, vm, gs);
    if (const char* ts = std::getenv("CODEMON_TEST_SCRIPT")) vm.start(ts, sess->player);
    menu.load_font();
    menu.configure(&gs, &team, &pc_box, &bdata);
    games.load_font();
    games.configure(&gs, &rng);
    starter.load();
    if (std::getenv("CODEMON_CHOOSE_STARTER")) starter.open();
    yesno.load();
    picker.load();
    picker.configure(&team);
    multichoice.load();
    {
        std::ifstream pf("assets/items/prices.tsv");
        std::string ln;
        while (std::getline(pf, ln)) {
            size_t tab = ln.find('\t');
            if (tab == std::string::npos) continue;
            item_prices[ln.substr(0, tab)] = std::atoi(ln.c_str() + tab + 1);
        }
    }
    shop.load();
    shop.configure(&gs, &item_prices);
    healfx.load();
    debugmenu.load();
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
    force_enc = std::getenv("CODEMON_FORCE_ENCOUNTER") != nullptr;

    banner = pretty_map(start_map);
    banner_t = 2.2f; fade = 1.0f;
    ban_font.loadFromFile("assets/fonts/DejaVuSans.ttf");
    menu.set_location(banner);
    menu.set_mapsec(sess->map->has_mapsec(), sess->map->mapsec_x(),
                     sess->map->mapsec_y(), sess->map->mapsec_w(), sess->map->mapsec_h());

    if (std::getenv("CODEMON_SCREENSHOT")) {
        run_headless();
        return 0;
    }
    run_interactive();
    return 0;
}

// --- headless screenshot / animation mode -----------------------------
void Game::run_headless() {
    const char* shot = std::getenv("CODEMON_SCREENSHOT");
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
                draw_scene(rt, sess, &healfx); box.draw(rt);
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
}

// --- interactive game ---------------------------------------------------
void Game::run_interactive() {
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
                BtnInput btn;
                bool has_btn = key_to_btn(event.key.code, btn);
                if (starter.active()) {
                    if (has_btn) starter.input(btn);
                } else if (yesno.active()) {
                    if (has_btn) yesno.input(btn);
                } else if (picker.active()) {
                    if (has_btn) picker.input(btn);
                } else if (multichoice.active()) {
                    if (has_btn) multichoice.input(btn);
                } else if (shop.active()) {
                    if (has_btn) shop.input(btn);
                } else if (debugmenu.active()) {
                    int action = has_btn ? debugmenu.input(btn) : -1;
                    if (event.key.code == sf::Keyboard::H) debugmenu.close();
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
                    if (has_btn) battle.input(btn);
                } else if (games.active()) {
                    if (has_btn) games.input(btn);
                } else if (event.key.code == sf::Keyboard::G &&
                           !box.is_active() && !vm.running() && !menu.active()) {
                    games.open();
                } else if (menu.active()) {
                    if (has_btn) menu.input(btn);
                    if (event.key.code == sf::Keyboard::M) menu.close();
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
                              menu.active() || debugmenu.active() ||
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
            draw_scene(*scr.get_window(), sess, &healfx);
            box.draw(*scr.get_window());
            draw_banner(*scr.get_window(), ban_font, banner, banner_t);
            menu.draw(*scr.get_window());
            if (yesno.active()) yesno.draw(*scr.get_window());
            if (picker.active()) picker.draw(*scr.get_window());
            if (multichoice.active()) multichoice.draw(*scr.get_window());
            if (debugmenu.active()) debugmenu.draw(*scr.get_window());
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
}

int main() {
    Game game;
    return game.run();
}
