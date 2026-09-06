// ---------------------------------------------------------------------------
// Regression tests for the actual game logic: BattleData, SaveGame, ScriptVM
// and Battle.
//
// The original tests.cpp only covered Coordinates/Tile/DIR/Linked_list -- three
// trivial legacy value types plus a container nothing in the game uses -- so a
// green CTest run said almost nothing about whether the game worked. Every case
// below pins down a defect that was actually shipped and found in a codebase
// audit; the comment on each names it.
//
// BattleData and SaveGame are pure logic and always run. Map/ScriptVM/Battle
// load textures, which SFML can only do with a GL context, so those are skipped
// (not failed) when there is no display -- see has_display().
// ---------------------------------------------------------------------------
#include "BattleData.h"
#include "SaveGame.h"
#include "QuestLog.h"
#include "Bike.h"
#include "PartySystem.h"
#include "GameState.h"
#include "ScriptVM.h"
#include "Battle.h"
#include "DialogBox.h"
#include "map.h"
#include "character.h"

#include <SFML/Graphics.hpp>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>
#include <algorithm>

static int failures = 0;
static int skipped = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            std::printf("  FAIL: %s (line %d)\n", #cond, __LINE__);       \
            ++failures;                                                   \
        }                                                                 \
    } while (0)

// SFML aborts rather than returning an error when it cannot open the X11
// display, so this has to be decided before touching any graphics type.
static bool has_display() {
#ifdef _WIN32
    return true;
#else
    const char* d = std::getenv("DISPLAY");
    const char* w = std::getenv("WAYLAND_DISPLAY");
    return (d && *d) || (w && *w);
#endif
}

static const char* BATTLE_DIR = "assets/battle";

// --------------------------------------------------------------- BattleData --

static void test_struggle_ignores_type_chart(BattleData& bd) {
    std::printf("[battle_data] struggle bypasses the type chart\n");
    std::mt19937 rng(1);
    Mon atk = bd.make_mon("MACHOP", 100);
    Mon ghost = bd.make_mon("GASTLY", 50);     // GHOST/POISON
    Mon normal = bd.make_mon("RATTATA", 50);

    // Sanity: a plain Normal move really is blocked by Ghost, so the case
    // below is testing the Struggle bypass and not a broken type chart.
    CHECK(BattleData::type_eff("NORMAL", ghost.t1, ghost.t2) == 0.f);

    // Struggle is data-typed NORMAL but the real engine special-cases it to
    // ignore the chart entirely. Battle::do_move() bypassed it while
    // BattleData::damage() recomputed effectiveness internally and did not,
    // so Struggle did nothing at all to a Ghost -- and since recoil is gated
    // on damage > 0, neither side could make progress and the battle could
    // not reach an end state.
    CHECK(bd.damage(atk, ghost, "STRUGGLE", rng, 1.f, 1.f, false) > 0);
    CHECK(bd.damage(atk, normal, "STRUGGLE", rng, 1.f, 1.f, false) > 0);

    // STAB must stay suppressed for Struggle as well.
    Mon norm_atk = bd.make_mon("RATTATA", 100);   // NORMAL, would get STAB
    int struggle = bd.damage(norm_atk, normal, "STRUGGLE", rng, 1.f, 1.f, false);
    int tackle   = bd.damage(norm_atk, normal, "TACKLE",   rng, 1.f, 1.f, false);
    // Same base power bracket, but Tackle gets 1.5x STAB and Struggle must not.
    CHECK(struggle > 0 && tackle > 0);
}

static void test_pp_and_movesets(BattleData& bd) {
    std::printf("[battle_data] real PP values and movesets\n");
    const MoveInfo* tackle = bd.move("TACKLE");
    const MoveInfo* hyper  = bd.move("HYPER_BEAM");
    const MoveInfo* strug  = bd.move("STRUGGLE");
    CHECK(tackle && tackle->pp == 35);
    CHECK(hyper && hyper->pp == 5);
    CHECK(strug && strug->pp == 1);

    Mon m = bd.make_mon("MACHOP", 20);
    CHECK(!m.moves.empty());
    CHECK(m.pp.size() == m.moves.size());     // parallel arrays must stay in step
    for (size_t i = 0; i < m.pp.size(); ++i) CHECK(m.pp[i] > 0);

    // Spending PP then a full heal restores every slot to its move's max.
    m.pp[0] = 0;
    bd.restore_pp(m);
    const MoveInfo* mi = bd.move(m.moves[0]);
    CHECK(mi && m.pp[0] == mi->pp);
}

static void test_species_data(BattleData& bd) {
    std::printf("[battle_data] imported per-species data\n");
    // Spot-checks against the real pokeemerald tables.
    CHECK(bd.catch_rate("ABRA") == 200);
    CHECK(bd.ability("SHEDINJA") == "WONDER_GUARD");
    CHECK(bd.ability("GEODUDE") == "ROCK_HEAD");

    // IVs/nature are only rolled when an RNG is supplied; without one a mon is
    // deterministic, which the headless walk tests depend on.
    Mon a = bd.make_mon("TORCHIC", 5);
    Mon b = bd.make_mon("TORCHIC", 5);
    CHECK(a.max_hp == b.max_hp && a.atk == b.atk && a.nature == b.nature);

    std::mt19937 rng(4);
    Mon c = bd.make_mon("TORCHIC", 5, &rng);
    CHECK(c.iv_hp >= 0 && c.iv_hp <= 31);
    CHECK(c.iv_spe >= 0 && c.iv_spe <= 31);
}


static void test_shiny(BattleData& bd) {
    std::printf("[battle_data] shiny personality values, odds and sprites\n");

    // pokeemerald's GET_SHINY_VALUE: the four 16-bit halves XORed together,
    // shiny below SHINY_ODDS (8). 0x00000000 with a 0 trainer id XORs to 0.
    CHECK(BattleData::is_shiny(0x00000000u, 0, 0));
    CHECK(BattleData::is_shiny(0x00000007u, 0, 0));
    CHECK(!BattleData::is_shiny(0x00000008u, 0, 0));
    // The trainer's own ID pair is part of the sum, so the very same
    // personality value is shiny for one save file and not for the next.
    CHECK(BattleData::is_shiny(0x12345678u, 0x444Cu, 0x0000u));   // 1234^5678^444C = 0
    CHECK(!BattleData::is_shiny(0x12345678u, 0x0000u, 0x0000u));

    // Without an RNG a mon is fully deterministic (the headless walk tests
    // depend on that), which includes never being shiny.
    Mon plain = bd.make_mon("TREECKO", 5);
    CHECK(plain.personality == 0 && !plain.shiny);

    // Rolled mons agree with the formula, and the odds come out at the real
    // 1/8192: 512k rolls average 64 shinies, so a run this far outside that
    // band means the check itself is wrong, not bad luck.
    std::mt19937 rng(7);
    const unsigned tid = 42021, sid = 31337;
    int shinies = 0;
    const int rolls = 8192 * 64;
    for (int i = 0; i < rolls; ++i)
        if (BattleData::is_shiny((unsigned)rng(), tid, sid)) ++shinies;
    CHECK(shinies > 25 && shinies < 120);

    Mon rolled = bd.make_mon("TREECKO", 5, &rng, tid, sid);
    CHECK(rolled.shiny == BattleData::is_shiny(rolled.personality, tid, sid));

    // Both palettes of both views are on disk for every species the engine
    // can put on screen -- a shiny with no artwork would battle as a blank.
    CHECK(BattleData::sprite_path("TREECKO", false) == "assets/pokemon/TREECKO.png");
    CHECK(BattleData::sprite_path("TREECKO", true) == "assets/pokemon/shiny/TREECKO.png");
    CHECK(BattleData::sprite_path("TREECKO", true, true) == "assets/pokemon/shiny/back/TREECKO.png");
    int missing = 0;
    for (int i = 0; i < bd.species_count(); ++i) {
        const std::string sp = bd.species_by_id(i);
        for (bool back : {false, true}) {
            // Only species that have normal artwork can have shiny artwork.
            std::FILE* n = std::fopen(BattleData::sprite_path(sp, false, back).c_str(), "rb");
            if (!n) continue;
            std::fclose(n);
            std::FILE* sh = std::fopen(BattleData::sprite_path(sp, true, back).c_str(), "rb");
            if (!sh) { ++missing; continue; }
            std::fclose(sh);
        }
    }
    CHECK(missing == 0);
}

// ----------------------------------------------------------------- SaveGame --

static void test_save_roundtrip(BattleData& bd) {
    std::printf("[savegame] round-trip\n");
    const char* path = "test_save_roundtrip.dat";

    GameState gs;
    gs.money = 4321;
    gs.female = true;
    gs.player_name = "MAY";
    gs.rival_name = "BRENDAN";
    gs.sound_on = false;
    gs.battle_scene_on = false;
    gs.frame_type = 7;
    gs.trainer_id = 54321; gs.secret_id = 12345;
    gs.set_flag("FLAG_BADGE01_GET");
    gs.give_item("ITEM_POTION", 3);
    gs.mark_caught("TORCHIC");

    std::vector<Mon> team, box;
    std::mt19937 rng(2);
    team.push_back(bd.make_mon("TORCHIC", 12, &rng));
    team[0].pp[0] = 1;
    team[0].held_item = "ORAN_BERRY";
    team[0].personality = 0xDEADBEEFu;
    team[0].shiny = true;
    box.push_back(bd.make_mon("ZIGZAGOON", 4, &rng));

    CHECK(SaveGame::save(path, gs, team, box, "maps/LittlerootTown.map", 5, 7));

    GameState gs2; std::vector<Mon> t2, b2; std::string map2; int x2 = 0, y2 = 0;
    CHECK(SaveGame::load(path, gs2, t2, b2, map2, x2, y2));
    CHECK(gs2.money == 4321);
    CHECK(gs2.female == true);
    CHECK(gs2.player_name == "MAY" && gs2.rival_name == "BRENDAN");
    CHECK(gs2.sound_on == false && gs2.battle_scene_on == false && gs2.frame_type == 7);
    CHECK(gs2.flag("FLAG_BADGE01_GET"));
    CHECK(gs2.is_caught("TORCHIC"));
    CHECK(map2 == "maps/LittlerootTown.map" && x2 == 5 && y2 == 7);
    CHECK(t2.size() == 1 && b2.size() == 1);
    CHECK(t2[0].species == "TORCHIC" && t2[0].level == 12);
    CHECK(t2[0].nature == team[0].nature && t2[0].iv_spe == team[0].iv_spe);
    CHECK(t2[0].held_item == "ORAN_BERRY");
    CHECK(t2[0].pp.size() == t2[0].moves.size() && t2[0].pp[0] == 1);
    // A caught shiny has to still be shiny after a reload -- the trainer ID
    // pair the roll was made against has to survive too, or every Pokemon
    // generated after the reload would be judged against a different one.
    CHECK(t2[0].personality == 0xDEADBEEFu && t2[0].shiny);
    CHECK(b2[0].shiny == box[0].shiny && b2[0].personality == box[0].personality);
    CHECK(gs2.trainer_id == 54321 && gs2.secret_id == 12345);
    std::remove(path);
}

static void test_save_empty_party() {
    std::printf("[savegame] a party-less save must reload\n");
    // A new game has no Pokemon until Birch hands over the starter on Route
    // 101, and SPEICHERN is offered from the first frame. load() used to
    // reject such a save outright, so the file was written and then silently
    // refused on the next launch -- the title screen fell back to NEUES SPIEL
    // and the run was gone.
    const char* path = "test_save_empty.dat";
    GameState gs;
    gs.set_flag("FLAG_STORY_PROGRESS");
    std::vector<Mon> team, box;                 // deliberately empty
    CHECK(SaveGame::save(path, gs, team, box,
                         "maps/LittlerootTown_BrendansHouse_2F.map", 4, 2));

    GameState gs2; std::vector<Mon> t2, b2; std::string map2; int x2 = 0, y2 = 0;
    CHECK(SaveGame::load(path, gs2, t2, b2, map2, x2, y2));
    CHECK(t2.empty());
    CHECK(gs2.flag("FLAG_STORY_PROGRESS"));
    CHECK(map2 == "maps/LittlerootTown_BrendansHouse_2F.map");
    std::remove(path);
}

static void test_save_pre_shiny_file() {
    std::printf("[savegame] a save written before shiny support still loads\n");
    // Mon lines grew two fields (personality value + shiny flag) and the file
    // grew a `trainerid` line. An older save has neither, and must still
    // reload -- with the mon non-shiny, which is exactly what it was.
    const char* path = "test_save_legacy.dat";
    {
        std::FILE* f = std::fopen(path, "w");
        std::fputs("SAVE 1\n"
                   "map\tmaps/LittlerootTown.map\n"
                   "pos\t5\t7\n"
                   "team\t1\n"
                   "TORCHIC\t12\t30\t30\t18\t14\t20\t14\t16\t"
                   "FIRE\tFIRE\t1000\tSCRATCH,GROWL\t0\t0\t0\t"
                   "HARDY\t15\t15\t15\t15\t15\t15\t35,40\tNONE\n",
                   f);
        std::fclose(f);
    }
    GameState gs; std::vector<Mon> t, b; std::string m; int x = 0, y = 0;
    CHECK(SaveGame::load(path, gs, t, b, m, x, y));
    CHECK(t.size() == 1 && t[0].species == "TORCHIC");
    CHECK(t[0].held_item == "NONE" && t[0].personality == 0 && !t[0].shiny);
    CHECK(gs.trainer_id == 0 && gs.secret_id == 0);
    std::remove(path);
}

static void test_save_rejects_garbage() {
    std::printf("[savegame] malformed saves are still rejected\n");
    const char* path = "test_save_bad.dat";
    { std::FILE* f = std::fopen(path, "w"); std::fputs("not a save file\n", f); std::fclose(f); }
    GameState gs; std::vector<Mon> t, b; std::string m; int x = 0, y = 0;
    CHECK(!SaveGame::load(path, gs, t, b, m, x, y));
    // A well-formed header with no map line is incomplete and must not load.
    { std::FILE* f = std::fopen(path, "w"); std::fputs("SAVE 1\nmoney\t10\n", f); std::fclose(f); }
    CHECK(!SaveGame::load(path, gs, t, b, m, x, y));
    std::remove(path);
}

// ----------------------------------------------------------------- ScriptVM --

// Runs one label of the opcode fixture map to completion and hands back the
// GameState it produced.
namespace {
struct VmHarness {
    BattleData& bd;
    Map map;
    GameState gs;
    std::vector<Mon> team, pc_box;
    DialogBox box;
    Battle battle;
    Character player;
    std::vector<Character*> actors;
    ScriptVM vm;
    std::mt19937 rng;

    explicit VmHarness(BattleData& b, const std::string& map_path = "tests/fixtures/OpcodeTest.map")
        : bd(b), map(map_path), player(1, 1), rng(7) {
        team.reserve(6);
        box.load_font();
        battle.configure(&bd, &rng);
        battle.set_capture(&gs, &team, &pc_box);
        vm.set_battle_data(&bd, &team, &rng, &pc_box);
        actors.push_back(&player);
        vm.configure(&map, &gs, &box, &battle, nullptr, &player, &actors);
    }
    void run(const std::string& label, Character* owner = nullptr) {
        vm.start(label, owner);
        for (int i = 0; i < 2000 && vm.running(); ++i) {
            if (vm.waiting_message()) vm.on_key();
            vm.update(0.1f);
        }
    }
};
} // namespace

static void test_script_opcodes(BattleData& bd) {
    std::printf("[scriptvm] arithmetic / money / coins / control flow\n");

    {   // subvar existed as addvar's counterpart in every real script but was
        // never implemented, so counters (the Glass Workshop's ash total, the
        // Devon/Birch VAR_RESULT decrements) silently drifted upward.
        VmHarness h(bd);
        h.run("Test_Arithmetic");
        CHECK(h.gs.get_var("VAR_COUNTER") == 400);   // 500 - 120 + 20
    }
    {   VmHarness h(bd);
        h.gs.money = 1000;
        h.run("Test_Money");
        CHECK(h.gs.money == 1300);                    // +500 -200
        CHECK(h.gs.get_var("VAR_RESULT") == 0);       // 1300 < 5000
    }
    {   VmHarness h(bd);
        h.gs.money = 1000;
        h.run("Test_MoneyEnough");
        CHECK(h.gs.get_var("VAR_RESULT") == 1);       // 1000 >= 100
    }
    {   VmHarness h(bd);
        h.run("Test_Coins");
        CHECK(h.gs.get_var("COINS") == 50);           // +90 -40
        // checkcoins writes into the *named* var, not VAR_RESULT.
        CHECK(h.gs.get_var("VAR_COIN_OUT") == 50);
    }
    {   // call_if_ne was the one missing member of the call_if_* family.
        VmHarness h(bd);
        h.gs.set_var("VAR_INPUT", 0);                 // != 1, so it must call
        h.run("Test_CallIfNe");
        CHECK(h.gs.get_var("VAR_FLAGGED") == 1);
    }
    {   VmHarness h(bd);
        h.gs.set_var("VAR_INPUT", 1);                 // == 1, so it must not
        h.run("Test_CallIfNe");
        CHECK(h.gs.get_var("VAR_FLAGGED") == 0);
    }
    {   VmHarness h(bd);
        h.team.push_back(bd.make_mon("TORCHIC", 5));
        h.team.push_back(bd.make_mon("ZIGZAGOON", 3));
        h.run("Test_PartySize");
        CHECK(h.gs.get_var("VAR_RESULT") == 2);
    }
    {   VmHarness h(bd);
        h.run("Test_Random");
        int r = h.gs.get_var("VAR_RESULT");
        CHECK(r >= 0 && r < 100);
    }
}

static void test_script_text_and_objects(BattleData& bd) {
    std::printf("[scriptvm] bufferstring / getplayerxy / object movement type\n");
    {   // bufferstring's text-label argument used to leak into the exported
        // script as a raw pokeemerald label (e.g. "BerryTree_Text_
        // CareAdverbGood") instead of the actual string -- the importer now
        // resolves it at import time, same as msgbox.
        VmHarness h(bd);
        h.run("Test_BufferString");
        CHECK(h.vm.str_var("STR_VAR_1") == "prettily");
    }
    {   // buffernumberstring was a pure no-op -- Birch's Pokedex-seen/caught
        // rating and every other "shows a live number in dialog" line
        // rendered with the number silently missing.
        VmHarness h(bd);
        h.run("Test_BufferNumberString");
        CHECK(h.vm.str_var("STR_VAR_2") == "42");
    }
    {   VmHarness h(bd);
        h.run("Test_GetPlayerXY");
        CHECK(h.gs.get_var("VAR_TEMP_1") == 1);   // VmHarness starts the player at (1,1)
        CHECK(h.gs.get_var("VAR_TEMP_2") == 1);
    }
    {   // setobjectmovementtype was completely unimplemented; an NPC whose
        // behaviour a script changes at runtime (e.g. Battle Dome audience
        // switching from walking to wandering) just kept its original,
        // map-file-authored movement forever.
        VmHarness h(bd);
        CHECK(h.player.get_move_kind() == 0);
        h.run("Test_ObjectMovement");
        CHECK(h.player.get_move_kind() == 1);   // MOVE_WANDER
    }
    {   // hideplayer/showplayer: the player Character has to actually leave/
        // rejoin the drawn+collidable actor list, not just get a flag no one
        // reads.
        VmHarness h(bd);
        CHECK(h.actors.size() == 1);
        h.run("Test_HidePlayer");
        CHECK(h.actors.empty());
    }
    {   VmHarness h(bd);
        h.run("Test_ShowPlayer");
        CHECK(h.actors.size() == 1);
    }
}

static void test_script_items_and_gifts(BattleData& bd) {
    std::printf("[scriptvm] items and gift Pokemon\n");
    {   VmHarness h(bd);
        h.run("Test_AddItem");
        CHECK(h.gs.item_count("ITEM_POTION") == 3);
    }
    {   VmHarness h(bd);
        h.run("Test_ItemSpace");
        CHECK(h.gs.get_var("VAR_RESULT") == 1);
    }
    {   // givemon was a no-op, so every gift Pokemon in the game (the Johto
        // starters, Beldum, Castform, the revived fossils) never arrived.
        VmHarness h(bd);
        h.run("Test_GiveMon");
        CHECK(h.gs.get_var("VAR_RESULT") == 0);        // 0 = went to the party
        CHECK(h.team.size() == 1);
        if (!h.team.empty()) CHECK(h.team[0].species == "BELDUM");
        CHECK(h.gs.is_caught("BELDUM"));               // gifts count for the dex
    }
    {   // A full party sends the gift to the PC instead (result 1).
        VmHarness h(bd);
        for (int i = 0; i < 6; ++i) h.team.push_back(bd.make_mon("ZIGZAGOON", 3));
        h.run("Test_GiveMon");
        CHECK(h.gs.get_var("VAR_RESULT") == 1);
        CHECK(h.team.size() == 6 && h.pc_box.size() == 1);
    }
}

static void test_script_trainer_flags(BattleData& bd) {
    std::printf("[scriptvm] trainer defeated flags\n");
    {   VmHarness h(bd);
        h.run("Test_TrainerFlagBranch");
        CHECK(h.gs.get_var("VAR_BRANCH") == 0);        // not beaten yet
    }
    {   VmHarness h(bd);
        h.run("Test_TrainerFlagSet");
        h.run("Test_TrainerFlagBranch");
        CHECK(h.gs.get_var("VAR_BRANCH") == 1);        // goto_if_defeated taken
    }
}

// ------------------------------------------------------------------- Battle --

static void test_recoil_uses_hp_actually_dealt(BattleData& bd) {
    std::printf("[battle] recoil is a fraction of HP actually removed\n");
    // pokeemerald clamps gHpDealt to the target's remaining HP before
    // computing gBattleMoveDamage = gHpDealt / 4. Using the raw damage roll
    // meant a heavy overkill hit recoiled for a quarter of a number far larger
    // than the target's entire HP bar: a level-100 Machop using Submission on
    // Route 102's level-3/4 trainer party killed itself.
    std::mt19937 rng(5);
    GameState gs;
    std::vector<Mon> team, box;
    team.reserve(6);
    team.push_back(bd.make_mon("MACHOP", 100));
    team[0].moves = {"SUBMISSION"};
    bd.restore_pp(team[0]);

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);
    CHECK(battle.start_trainer("TRAINER_ALLEN", "Allen", &team[0]));
    for (int i = 0; i < 3000 && battle.active(); ++i) battle.input(BTN_CONFIRM);

    CHECK(!battle.active());
    CHECK(battle.won());
    CHECK(!team[0].fainted());
    // Recoil against those tiny targets can only ever be a few HP.
    CHECK(team[0].hp > team[0].max_hp / 2);
}

static void test_trainer_ai_uses_item(BattleData& bd) {
    std::printf("[battle] trainer AI heals with its own item pool\n");
    // Trainer AI item use was completely unimplemented -- a Gym Leader whose
    // real .items field carries a Full Restore/Hyper Potion just kept
    // attacking forever, even at 1 HP. TRAINER_ALBERT carries exactly one
    // ITEM_FULL_RESTORE (real pokeemerald data, see trainer_items.tsv).
    // A weak, bulky attacker chips MAGNETON (98 max HP) down ~7-8 HP/hit --
    // crosses the 50% threshold around the 7th hit, well before the ~13
    // needed to faint it, so the AI gets a real chance to use its item.
    std::mt19937 rng(42);
    GameState gs;
    std::vector<Mon> team, box;
    team.reserve(6);
    team.push_back(bd.make_mon("MAGIKARP", 100, &rng));
    team[0].moves = {"TACKLE"};
    bd.restore_pp(team[0]);
    team[0].max_hp = team[0].hp = 999;   // never faints to Magneton's/Muk's counterattacks
    team[0].def = team[0].spd = 999;

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);
    CHECK(battle.start_trainer("TRAINER_ALBERT", "Albert", &team[0]));
    CHECK(battle.enemy_items_left() == 1);
    for (int i = 0; i < 20000 && battle.active(); ++i) battle.input(BTN_CONFIRM);

    CHECK(!battle.active());
    CHECK(battle.won());
    // The one Full Restore must have actually been spent by the time both
    // of Albert's mons are down -- not just carried the whole fight.
    CHECK(battle.enemy_items_left() == 0);
}

static void test_battle_lead_skips_fainted(BattleData& bd) {
    std::printf("[battle] a fainted lead is not sent out again\n");
    // Every caller hands Battle the party's slot 0 (`&team[0]`) and the
    // battle used it unconditionally, so a lead that fainted in the previous
    // fight was sent straight back into the next one: the encounter opened
    // with a 0 HP Pokemon, which could do nothing but faint again and force
    // a switch. The real games send out the first member that can fight.
    std::mt19937 rng(5);
    GameState gs;
    std::vector<Mon> team, box;
    team.reserve(6);
    team.push_back(bd.make_mon("MACHOP", 20));
    team.push_back(bd.make_mon("ZIGZAGOON", 18));

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);

    // Healthy lead: still slot 0.
    CHECK(battle.start_wild("POOCHYENA", 3, &team[0]));
    CHECK(battle.active_party_index() == 0);

    team[0].hp = 0;                       // lead fainted in the previous fight
    CHECK(battle.start_wild("POOCHYENA", 3, &team[0]));
    CHECK(battle.active_party_index() == 1);
    CHECK(battle.start_trainer("TRAINER_ALLEN", "Allen", &team[0]));
    CHECK(battle.active_party_index() == 1);

    // Whole party down (a whiteout the caller handles): fall back to slot 0
    // rather than running off the end of the party.
    team[1].hp = 0;
    CHECK(battle.start_wild("POOCHYENA", 3, &team[0]));
    CHECK(battle.active_party_index() == 0);
}

static void test_switch_menu_starts_on_a_usable_mon(BattleData& bd) {
    std::printf("[battle] the party menu opens on a Pokemon that can fight\n");
    // The menu opened on "the first slot that is not the active one", so a
    // party led by a fainted Pokemon pointed the cursor straight at that
    // K.O.'d lead -- confirming it could only ever answer "... kann nicht
    // kaempfen!". It has to start on a member that can actually come out.
    std::mt19937 rng(9);
    GameState gs;
    std::vector<Mon> team, box;
    team.reserve(6);
    team.push_back(bd.make_mon("MACHOP", 30));
    team.push_back(bd.make_mon("ZIGZAGOON", 28));
    team.push_back(bd.make_mon("POOCHYENA", 26));
    team[0].hp = 0;                       // K.O. lead: slot 1 leads the battle

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);
    CHECK(battle.start_wild("POOCHYENA", 3, &team[0]));
    auto to_action = [&]() {
        for (int i = 0; i < 60 && battle.screen() == Battle::SCR_MESSAGE; ++i)
            battle.input(BTN_CONFIRM);
    };
    auto open_party = [&]() {
        to_action();
        CHECK(battle.screen() == Battle::SCR_ACTION);
        for (int k = 0; k < 4; ++k) battle.input(BTN_UP);   // action cursor -> KAMPF
        battle.input(BTN_DOWN);           // KAMPF -> POKéMON
        battle.input(BTN_CONFIRM);
        CHECK(battle.screen() == Battle::SCR_SWITCH);
    };

    CHECK(battle.active_party_index() == 1);
    open_party();
    CHECK(battle.switch_index() == 2);    // not the fainted slot 0
    battle.input(BTN_CONFIRM);            // and confirming really sends it out
    CHECK(battle.active_party_index() == 2);

    // Nothing healthy left in reserve: the cursor still must not sit on the
    // Pokemon that is already fighting, where confirming does nothing.
    team[1].hp = 0;
    open_party();
    CHECK(battle.switch_index() != (int)battle.active_party_index());
}

static void test_capture_keeps_the_encounter(BattleData& bd) {
    std::printf("[battle] a caught mon keeps its held item and identity\n");
    // The caught mon is rebuilt with make_mon() and then given the
    // encounter's rolled values back. The held item was left out of that
    // list, so every Pokemon arrived in the party empty-handed even though
    // the wild one had been holding (and could have been eating) an item.
    // NUMEL's common and rare item are both a Rawst Berry, so make_mon()
    // always hands it one; its catch rate of 255 keeps the loop short.
    std::mt19937 rng(11);
    GameState gs;
    gs.give_item("ITEM_POKE_BALL", 50);
    std::vector<Mon> team, box;
    team.reserve(6);
    team.push_back(bd.make_mon("MACHOP", 50));

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);
    CHECK(battle.start_wild("NUMEL", 5, &team[0]));

    for (int i = 0; i < 4000 && battle.active() && team.size() < 2; ++i) {
        if (battle.screen() == Battle::SCR_MESSAGE) { battle.input(BTN_CONFIRM); continue; }
        if (battle.screen() == Battle::SCR_BALL) { battle.input(BTN_CONFIRM); continue; }
        if (battle.screen() != Battle::SCR_ACTION) break;
        for (int k = 0; k < 4; ++k) battle.input(BTN_UP);      // back to KAMPF
        battle.input(BTN_DOWN); battle.input(BTN_DOWN);        // -> BALL
        battle.input(BTN_CONFIRM);                             // open ball submenu
    }
    CHECK(team.size() == 2);
    if (team.size() == 2) {
        CHECK(team[1].species == "NUMEL");
        CHECK(team[1].held_item == "RAWST_BERRY");
        CHECK(team[1].personality != 0);   // the encounter's, not a fresh mon's
    }
}

static void test_capture_keeps_hp_and_status(BattleData& bd) {
    std::printf("[battle] a caught mon keeps its HP and status condition\n");
    // The caught mon was rebuilt from scratch, so it always joined the party
    // at full HP and healthy: a Numel caught at 3 HP and asleep walked in
    // fully healed. The individual on the field is the one that is caught.
    std::mt19937 rng(13);
    GameState gs;
    gs.give_item("ITEM_POKE_BALL", 300);
    std::vector<Mon> team, box;
    team.reserve(6);
    // A level-5 attacker with 1 Attack chips the level-40 enemy a point or
    // two at a time instead of knocking it out, and a huge HP pool means
    // whatever the enemy hits back with cannot end the battle early.
    team.push_back(bd.make_mon("MACHOP", 5));
    team[0].atk = 1;
    team[0].max_hp = team[0].hp = 9999;
    team[0].moves = {"TACKLE"};
    team[0].pp = {35};

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);
    CHECK(battle.start_wild("NUMEL", 40, &team[0]));

    // KAMPF is the top action row; move slot 0 is whatever `moves[0]` holds
    // right now, so swapping that field mid-battle picks the next move
    // without having to walk the 2x2 move grid.
    auto attack = [&]() {
        for (int i = 0; i < 200 && battle.active(); ++i) {
            if (battle.screen() == Battle::SCR_MESSAGE) { battle.input(BTN_CONFIRM); continue; }
            if (battle.screen() == Battle::SCR_ACTION) {
                for (int k = 0; k < 4; ++k) battle.input(BTN_UP);   // -> KAMPF
                battle.input(BTN_CONFIRM);
                continue;
            }
            if (battle.screen() == Battle::SCR_MOVE) { battle.input(BTN_CONFIRM); return; }
            return;
        }
    };

    for (int i = 0; i < 20 && battle.active() &&
         battle.enemy_hp() == battle.enemy_max_hp(); ++i) attack();
    CHECK(battle.enemy_hp() < battle.enemy_max_hp());   // actually damaged

    // Put it to sleep last, so it is still asleep when the ball lands (the
    // 2x sleep catch bonus also makes that land fast).
    team[0].moves[0] = "SPORE";                // 100% accurate, EFFECT_SLEEP
    team[0].pp[0] = 15;
    for (int i = 0; i < 20 && battle.active() &&
         battle.enemy_status() != Status::SLEEP; ++i) attack();
    CHECK(battle.enemy_status() == Status::SLEEP);

    // Throw balls until one sticks, remembering the enemy's state as it was
    // on the very throw that caught it.
    int hp_before = battle.enemy_hp();
    Status status_before = battle.enemy_status();
    for (int i = 0; i < 4000 && battle.active() && team.size() < 2; ++i) {
        if (battle.screen() == Battle::SCR_MESSAGE) { battle.input(BTN_CONFIRM); continue; }
        if (battle.screen() == Battle::SCR_BALL) {
            hp_before = battle.enemy_hp();
            status_before = battle.enemy_status();
            battle.input(BTN_CONFIRM);
            continue;
        }
        if (battle.screen() != Battle::SCR_ACTION) break;
        for (int k = 0; k < 4; ++k) battle.input(BTN_UP);      // back to KAMPF
        battle.input(BTN_DOWN); battle.input(BTN_DOWN);        // -> BALL
        battle.input(BTN_CONFIRM);                             // open ball submenu
    }
    CHECK(team.size() == 2);
    if (team.size() == 2) {
        CHECK(team[1].species == "NUMEL");
        CHECK(team[1].hp == hp_before && team[1].hp < team[1].max_hp);
        CHECK(team[1].status == status_before && status_before == Status::SLEEP);
        // Confusion is volatile in Gen 3 -- it ends with the battle and is
        // not something the caught mon carries home.
        CHECK(team[1].confusion_turns == 0);
    }
}

static void test_wild_battle_capture(BattleData& bd) {
    std::printf("[battle] capture moves the mon into the party\n");
    std::mt19937 rng(3);
    GameState gs;
    gs.give_item("ITEM_POKE_BALL", 50);
    std::vector<Mon> team, box;
    team.reserve(6);
    team.push_back(bd.make_mon("MACHOP", 50));

    Battle battle;
    battle.configure(&bd, &rng);
    battle.set_capture(&gs, &team, &box);
    CHECK(battle.start_wild("MAGIKARP", 5, &team[0]));

    // BALL is the third action-menu row (KAMPF/POKeMON/BALL/FLUCHT, UP+DOWN
    // only) and now opens a ball-choice submenu (only ITEM_POKE_BALL owned
    // here, so a single CONFIRM there throws it). Drain whatever messages
    // are up, then walk the cursor back to the top before stepping down --
    // overshooting lands on FLUCHT and ends the battle. Magikarp's real
    // catch rate is 255, so this converges fast.
    for (int i = 0; i < 4000 && battle.active() && team.size() < 2; ++i) {
        if (battle.screen() == Battle::SCR_MESSAGE) { battle.input(BTN_CONFIRM); continue; }
        if (battle.screen() == Battle::SCR_BALL) { battle.input(BTN_CONFIRM); continue; }
        if (battle.screen() != Battle::SCR_ACTION) break;
        for (int k = 0; k < 4; ++k) battle.input(BTN_UP);      // back to KAMPF
        battle.input(BTN_DOWN); battle.input(BTN_DOWN);        // -> BALL
        battle.input(BTN_CONFIRM);                             // open ball submenu
    }
    CHECK(team.size() == 2);
    if (team.size() == 2) {
        CHECK(team[1].species == "MAGIKARP");
        CHECK(gs.is_caught("MAGIKARP"));
    }
    // Balls must actually be consumed, not infinite.
    CHECK(gs.item_count("ITEM_POKE_BALL") < 50);
}

// ------------------------------------------------------------ trainer sight --

static void test_trainer_sight() {
    std::printf("[map] trainer line of sight\n");
    // Trainers never challenged the player at all: the engine had no sight
    // check and the importer never read pokeemerald's
    // trainer_sight_or_berry_tree_id, so every trainer had to be walked up to
    // and talked to. 8x8 open fixture map, trainer at (1,1) facing south.
    Map map("tests/fixtures/SightTest.map");
    auto clear = std::function<bool(int, int)>();   // nothing in the way

    // Straight ahead, within range.
    CHECK(trainer_can_see(map, 1, 1, DIR::S, 3, 1, 2, clear));
    CHECK(trainer_can_see(map, 1, 1, DIR::S, 3, 1, 4, clear));
    // One tile past the range.
    CHECK(!trainer_can_see(map, 1, 1, DIR::S, 3, 1, 5, clear));
    // Behind, and off to the side: never seen.
    CHECK(!trainer_can_see(map, 1, 1, DIR::S, 3, 1, 0, clear));
    CHECK(!trainer_can_see(map, 1, 1, DIR::S, 3, 2, 2, clear));
    // Facing away from the player.
    CHECK(!trainer_can_see(map, 1, 1, DIR::N, 3, 1, 2, clear));
    // Standing on the trainer's own tile is not "ahead of" it.
    CHECK(!trainer_can_see(map, 1, 1, DIR::S, 3, 1, 1, clear));
    // A sight range of 0 (an ordinary, non-trainer NPC) never triggers.
    CHECK(!trainer_can_see(map, 1, 1, DIR::S, 0, 1, 2, clear));

    // Someone standing in between breaks the line of sight.
    auto blocked_at_2 = std::function<bool(int, int)>(
        [](int x, int y) { return x == 1 && y == 2; });
    CHECK(!trainer_can_see(map, 1, 1, DIR::S, 3, 1, 3, blocked_at_2));
    // ... but a blocker *behind* the player is irrelevant.
    auto blocked_at_4 = std::function<bool(int, int)>(
        [](int x, int y) { return x == 1 && y == 4; });
    CHECK(trainer_can_see(map, 1, 1, DIR::S, 3, 1, 3, blocked_at_4));

    // The other three directions.
    CHECK(trainer_can_see(map, 5, 5, DIR::N, 2, 5, 3, clear));
    CHECK(trainer_can_see(map, 5, 5, DIR::W, 2, 3, 5, clear));
    CHECK(trainer_can_see(map, 3, 3, DIR::E, 2, 5, 3, clear));
}

static void test_sight_only_while_undefeated(BattleData& bd) {
    std::printf("[scriptvm] a beaten trainer stops challenging on sight\n");
    Map map("tests/fixtures/SightTest.map");
    GameState gs;
    std::vector<Mon> team, pc;
    team.reserve(6);
    DialogBox dbox; dbox.load_font();
    std::mt19937 rng(3);
    Battle battle; battle.configure(&bd, &rng); battle.set_capture(&gs, &team, &pc);
    Character player(1, 7);
    ScriptVM vm;
    vm.set_battle_data(&bd, &team, &rng, &pc);
    vm.configure(&map, &gs, &dbox, &battle, nullptr, &player);

    // npc 0's script fights TRAINER_ALLEN; npc 1's script has no battle at all.
    CHECK(vm.script_has_pending_trainer("Sight_TrainerScript"));
    CHECK(!vm.script_has_pending_trainer("Sight_PlainScript"));
    CHECK(!vm.script_has_pending_trainer("NoSuchScript"));

    gs.set_flag("TRAINER_DEFEATED_TRAINER_ALLEN");
    CHECK(!vm.script_has_pending_trainer("Sight_TrainerScript"));
}

// ----------------------------------------------------------------- dive/map --

static void test_dive_reaches_sootopolis() {
    std::printf("[map] Dive route into Sootopolis City\n");
    // Sootopolis City has no land route, no warp and no map connection from
    // outside -- surfacing out of Underwater_SootopolisCity is the only way
    // in. Dive was entirely unimplemented (the engine never even mentioned
    // it), so the 8th gym, the Cave of Origin and the endgame were cut off
    // and the 14 imported Underwater_* maps were unreachable dead weight.
    //
    // Walks the actual chain the player has to take:
    //   Route126 --dive--> Underwater_Route126 --warp--> Underwater_Sootopolis
    //            --setdivewarp--> SootopolisCity
    Map route126("maps/Route126.map");
    CHECK(route126.dive_dest() == "Underwater_Route126");
    // The dive has to be startable somewhere on the map, not just declared.
    bool any_diveable = false;
    for (unsigned y = 0; y < route126.get_height() && !any_diveable; ++y)
        for (unsigned x = 0; x < route126.get_width(); ++x)
            if (route126.is_diveable((int)x, (int)y)) { any_diveable = true; break; }
    CHECK(any_diveable);

    Map uw126("maps/Underwater_Route126.map");
    CHECK(uw126.emerge_dest() == "Route126");           // and back up again
    bool warps_to_uw_sootopolis = false;
    for (const Warp& w : uw126.warps())
        if (w.dest == "Underwater_SootopolisCity") warps_to_uw_sootopolis = true;
    CHECK(warps_to_uw_sootopolis);

    Map uw_soot("maps/Underwater_SootopolisCity.map");
    CHECK(uw_soot.divewarp_dest() == "SootopolisCity");
    CHECK(uw_soot.divewarp_x() == 29 && uw_soot.divewarp_y() == 53);

    // The return trip: Sootopolis dives back down to its own underwater map.
    Map soot("maps/SootopolisCity.map");
    CHECK(soot.divewarp_dest() == "Underwater_SootopolisCity");
}

static void test_dynamic_warp_out_of_truck(BattleData& bd) {
    std::printf("[map] setdynamicwarp gets the player out of the intro truck\n");
    // InsideOfTruck's own exit tiles (x=4, y=1..3) carry dest "-" in the map
    // data -- pokeemerald resolves them at runtime via a `setdynamicwarp`
    // the intro script runs first (SetIntroFlagsFemale here). Before this
    // was wired up, warp_at() would already find the tile but its dest was
    // always "-", so stepping on it silently did nothing -- the player could
    // never leave the truck at all.
    Map truck("maps/InsideOfTruck.map");
    const Warp* exit_warp = truck.warp_at(4, 1);
    CHECK(exit_warp != nullptr);
    CHECK(exit_warp->dest == "-");

    VmHarness h(bd, "maps/InsideOfTruck.map");
    h.run("InsideOfTruck_EventScript_SetIntroFlagsFemale");
    CHECK(h.gs.dynamic_warp_map == "LittlerootTown");
    CHECK(h.gs.dynamic_warp_x == 12 && h.gs.dynamic_warp_y == 10);
}

static void test_sprite_frame_geometry() {
    std::printf("[gfx] overworld sheets are cut to their real frame size\n");
    // Character used to assume every sheet was the standard 144x32 people
    // layout (9 frames of 16x32). Most imported sheets aren't: an item ball
    // is a single 16x16 frame, a gym leader has three, and Mr. Briney's boat
    // is 32px wide. Reading a fixed 16x32 rect out of those sampled past the
    // end of the texture and drew short sprites a whole tile too high.
    struct Case { const char* sheet; int fw; int fh; };
    const Case cases[] = {
        { "people_man_1",               16, 32 },   // ordinary 9-frame sheet
        { "misc_item_ball",             16, 16 },   // single small frame
        { "misc_cuttable_tree",         16, 16 },
        { "people_gym_leaders_roxanne", 16, 32 },   // 3 frames, no walk cycle
        { "misc_mr_brineys_boat",       32, 32 },   // two tiles wide
        { "misc_truck",                 48, 48 },   // whole set piece
    };
    for (const Case& c : cases) {
        Character ch(3, 4);
        std::string path = std::string("assets/overworld/") + c.sheet + ".png";
        if (!ch.load_sprite_sheet(path)) { CHECK(false); continue; }
        ch.face(DIR::S);
        ch.update_sprite(16);
        sf::Sprite* sp = ch.get_current_sprite();
        CHECK(sp != nullptr);
        if (!sp) continue;
        sf::IntRect r = sp->getTextureRect();
        CHECK(r.width == c.fw);
        CHECK(r.height == c.fh);
        // The frame must lie inside the texture, never past its edge.
        CHECK(r.left >= 0 && r.top >= 0);
        CHECK(r.left + r.width <= (int)ch.get_current_sprite()->getTexture()->getSize().x);
        CHECK(r.top + r.height <= (int)ch.get_current_sprite()->getTexture()->getSize().y);
        // It stands on its tile: the sprite's foot is at the tile's bottom.
        CHECK(sp->getPosition().y + (float)c.fh == 4.f * 16.f + 16.f);
    }

    // An object sheet's frames are states, not facings: talking to a rock
    // turns it towards the player, which must not pick a half-smashed frame.
    {
        Character rock(5, 5);
        if (rock.load_sprite_sheet("assets/overworld/misc_breakable_rock.png")) {
            rock.face(DIR::S); rock.update_sprite(16);
            sf::IntRect intact = rock.get_current_sprite()->getTextureRect();
            for (DIR d : { DIR::N, DIR::W, DIR::E }) {
                rock.face(d); rock.update_sprite(16);
                CHECK(rock.get_current_sprite()->getTextureRect() == intact);
            }
            CHECK(intact.left == 0);   // always the unbroken frame
        }
        Character tree(5, 5);
        if (tree.load_sprite_sheet("assets/overworld/misc_cuttable_tree.png")) {
            tree.face(DIR::W); tree.update_sprite(16);
            CHECK(tree.get_current_sprite()->getTextureRect().left == 0);
        }
        // An ordinary NPC must still turn.
        Character man(5, 5);
        if (man.load_sprite_sheet("assets/overworld/people_man_1.png")) {
            man.face(DIR::S); man.update_sprite(16);
            sf::IntRect south = man.get_current_sprite()->getTextureRect();
            man.face(DIR::N); man.update_sprite(16);
            CHECK(man.get_current_sprite()->getTextureRect() != south);
        }
    }

    // Walk phases on a sheet with no walk frames must fall back to the idle
    // frame instead of addressing a frame the sheet does not have.
    Character gl(0, 0);
    if (gl.load_sprite_sheet("assets/overworld/people_gym_leaders_roxanne.png")) {
        gl.face(DIR::W);
        gl.update_sprite(16);
        sf::IntRect idle = gl.get_current_sprite()->getTextureRect();
        CHECK(idle.left + idle.width <= 48);
    }
}

static void test_object_state_animation(BattleData& bd) {
    std::printf("[gfx] rock smash / tree cut / nurse bow play the object's frames\n");
    // Movement_SmashRock and friends were imported as a bare "delay,end" --
    // the frames the object sheet carries (a rock's break stages) never
    // played, so a smashed rock simply blinked out of existence.
    Character rock(3, 3);
    if (rock.load_sprite_sheet("assets/overworld/misc_breakable_rock.png")) {
        rock.update_sprite(16);
        CHECK(rock.get_current_sprite()->getTextureRect().left == 0);   // intact
        CHECK(!rock.state_anim_active());

        rock.play_state_anim(0.5f, true);
        CHECK(rock.state_anim_active());
        rock.tick_state_anim(0.3f);
        rock.update_sprite(16);
        int mid = rock.get_current_sprite()->getTextureRect().left;
        CHECK(mid > 0);                       // partway through breaking
        CHECK(mid < 4 * 16);

        rock.tick_state_anim(0.3f);
        CHECK(!rock.state_anim_active());     // finished
        rock.update_sprite(16);
        CHECK(rock.get_current_sprite()->getTextureRect().left == 3 * 16);
        // The held last frame must survive being turned towards the player.
        rock.face(DIR::W);
        rock.update_sprite(16);
        CHECK(rock.get_current_sprite()->getTextureRect().left == 3 * 16);
    }

    // The nurse straightens up again rather than holding the bow.
    Character nurse(1, 1);
    if (nurse.load_sprite_sheet("assets/overworld/people_nurse.png")) {
        nurse.play_state_anim(0.7f, false);
        CHECK(nurse.state_anim_active());
        nurse.tick_state_anim(0.7f);
        CHECK(!nurse.state_anim_active());
        nurse.update_sprite(16);
        CHECK(nurse.get_current_sprite()->getTextureRect().left == 0);
    }

    // A single-frame sheet has nothing to animate and must not leave the
    // script waiting forever on it.
    Character ball(2, 2);
    if (ball.load_sprite_sheet("assets/overworld/misc_item_ball.png")) {
        ball.play_state_anim(0.5f, true);
        CHECK(!ball.state_anim_active());
    }

    // End to end: Route 111's own rock-smash script must hold `waitmovement`
    // until the break animation has played, and only then remove the rock.
    VmHarness h(bd, "maps/Route111.map");
    Character target(5, 5);
    if (target.load_sprite_sheet("assets/overworld/misc_breakable_rock.png")) {
        h.actors.push_back(&target);
        h.vm.start("EventScript_SmashRock", &target);
        h.vm.update(0.05f);
        CHECK(target.state_anim_active());    // still breaking, script waiting
        CHECK(h.vm.running());
        for (int i = 0; i < 200 && h.vm.running(); ++i) {
            if (h.vm.waiting_message()) h.vm.on_key();
            h.vm.update(0.1f);
        }
        CHECK(!target.state_anim_active());
        CHECK(target.is_removed());           // removeobject ran afterwards
    }
}

static void test_briney_voyage(BattleData& bd) {
    std::printf("[map] Briney's crossing stays on water and lands somewhere you can leave\n");
    Map r104("maps/Route104.map");
    Map dew("maps/DewfordTown.map");
    CHECK(r104.ready());
    CHECK(dew.ready());
    if (!r104.ready() || !dew.ready()) return;

    // The boat sails Route 104's own water: pokeemerald's own movement crosses
    // map connections mid-cutscene, which this engine cannot do, so the path
    // ran aground on the beach and then off the map entirely.
    int bx = -1, by = -1;
    for (const NpcSpawn& n : r104.npcs())
        if (n.sheet == "misc_mr_brineys_boat") { bx = n.x; by = n.y; }
    CHECK(bx >= 0);
    auto sail_from = [&](const std::string& label, int sx, int sy, int& ex, int& ey) {
        ex = sx; ey = sy;
        for (const std::string& a : r104.movement(label)) {
            if (a == "up") ey--;
            else if (a == "down") ey++;
            else if (a == "left") ex--;
            else if (a == "right") ex++;
            else continue;
            CHECK(r104.in_bounds(ex, ey));                 // never off the map
            if (r104.in_bounds(ex, ey)) CHECK(r104.is_water(ex, ey));   // never over land
        }
    };
    if (bx >= 0) {
        int ex, ey;
        sail_from("Route104_Movement_SailToDewford", bx, by, ex, ey);
        // The Pokenav-call variant is the same voyage split in two: the second
        // half has to pick up exactly where the first one stopped.
        int mx, my;
        sail_from("Route104_Movement_SailToDewfordBeforeDadCalls", bx, by, mx, my);
        int fx, fy;
        sail_from("Route104_Movement_SailToDewfordAfterDadCalls", mx, my, fx, fy);
        CHECK(fx == ex && fy == ey);
    }

    // Where the arrival script actually drops the player -- taken from the
    // script itself rather than hardcoded, so moving the warp moves the test.
    VmHarness h(bd, "maps/Route104.map");
    h.run("Route104_EventScript_LandedInDewford");
    CHECK(h.vm.has_pending_warp());
    if (!h.vm.has_pending_warp()) return;
    std::string dest; int ax = -1, ay = -1;
    h.vm.get_pending_warp(dest, ax, ay);
    CHECK(dest == "DewfordTown");
    CHECK(dew.in_bounds(ax, ay));
    if (!dew.in_bounds(ax, ay)) return;
    CHECK(dew.passable(ax, ay));
    CHECK(!dew.is_water(ax, ay));

    // Mr. Briney spawns on the pier on arrival. The landing tile was chosen
    // one tile too far out, so he stood between the player and the shore with
    // deep water on every other side -- an inescapable tile.
    int nx = -1, ny = -1;
    for (const NpcSpawn& n : dew.npcs())
        if (n.local_id == "LOCALID_DEWFORD_BRINEY") { nx = n.x; ny = n.y; }
    CHECK(nx >= 0);

    // Walk out on foot (no Surf): flood fill over dry, passable tiles, with
    // Briney's tile blocked, and demand it reaches a building entrance.
    const int W = (int)dew.get_width(), H = (int)dew.get_height();
    std::vector<char> seen((size_t)W * H, 0);
    std::vector<std::pair<int,int>> stack{{ax, ay}};
    seen[(size_t)ay * W + ax] = 1;
    while (!stack.empty()) {
        auto [cx, cy] = stack.back();
        stack.pop_back();
        const int dx[4] = {0, 0, -1, 1}, dy[4] = {-1, 1, 0, 0};
        for (int i = 0; i < 4; ++i) {
            int tx = cx + dx[i], ty = cy + dy[i];
            if (!dew.in_bounds(tx, ty)) continue;
            if (seen[(size_t)ty * W + tx]) continue;
            if (tx == nx && ty == ny) continue;            // Briney is in the way
            // A door tile is deliberately impassable metatile-wise -- the warp
            // overrides collision (see player_step) -- so it counts as walkable.
            if (dew.is_water(tx, ty)) continue;
            if (!dew.passable(tx, ty) && !dew.warp_at(tx, ty)) continue;
            seen[(size_t)ty * W + tx] = 1;
            stack.push_back({tx, ty});
        }
    }
    int reachable_doors = 0;
    for (const Warp& wp : dew.warps())
        if (dew.in_bounds(wp.x, wp.y) && seen[(size_t)wp.y * W + wp.x]) ++reachable_doors;
    CHECK(reachable_doors > 0);
}

static void test_briney_voyage_from_dewford(BattleData& bd) {
    std::printf("[map] Briney's Dewford departures stay on water and land on dry ground\n");
    Map dew("maps/DewfordTown.map");
    Map r109("maps/Route109.map");
    CHECK(dew.ready());
    CHECK(r109.ready());
    if (!dew.ready() || !r109.ready()) return;

    // Same problem as Route 104's crossing: pokeemerald sails these voyages
    // across map connections, which this engine cannot do, so the imported
    // movement walked the boat straight off the 20x20 Dewford map.
    int bx = -1, by = -1;
    for (const NpcSpawn& n : dew.npcs())
        if (n.local_id == "LOCALID_DEWFORD_BOAT") { bx = n.x; by = n.y; }
    CHECK(bx >= 0);
    if (bx < 0) return;
    const char* voyages[] = {"DewfordTown_Movement_SailToSlateport",
                             "DewfordTown_Movement_SailToPetalburg"};
    for (const char* label : voyages) {
        int ex = bx, ey = by;
        const std::vector<std::string>& acts = dew.movement(label);
        CHECK(!acts.empty());
        for (const std::string& a : acts) {
            if (a == "up") ey--;
            else if (a == "down") ey++;
            else if (a == "left") ex--;
            else if (a == "right") ex++;
            else continue;
            CHECK(dew.in_bounds(ex, ey));                  // never off the map
            if (dew.in_bounds(ex, ey)) CHECK(dew.is_water(ex, ey));   // never over land
        }
    }

    // Landing in Slateport put the player on open sea off Route 109's beach,
    // standing on water without surfing. Read the tile out of the script.
    VmHarness h(bd, "maps/DewfordTown.map");
    h.run("DewfordTown_EventScript_SailToSlateport");
    CHECK(h.vm.has_pending_warp());
    if (!h.vm.has_pending_warp()) return;
    std::string dest; int ax = -1, ay = -1;
    h.vm.get_pending_warp(dest, ax, ay);
    CHECK(dest == "Route109");
    CHECK(r109.in_bounds(ax, ay));
    if (!r109.in_bounds(ax, ay)) return;
    CHECK(r109.passable(ax, ay));
    CHECK(!r109.is_water(ax, ay));
    // ... and not on top of Mr. Briney, who spawns on the beach on arrival.
    for (const NpcSpawn& n : r109.npcs())
        if (n.local_id == "LOCALID_ROUTE109_BRINEY")
            CHECK(!(n.x == ax && n.y == ay));
}

static void test_plain_dialog_tokens(BattleData& bd) {
    std::printf("[scriptvm] plain NPC lines get the same token substitution as msgbox\n");
    // An NPC with no script of its own has its line shown straight from the
    // map data, which skipped substitution entirely: those NPCs greeted the
    // player as "PLAYER" and Dewford's whole town talked about "STR_VAR_1".
    VmHarness h(bd);
    h.gs.player_name = "ASH";
    h.gs.rival_name = "GARY";
    CHECK(h.vm.expand_text("Hallo PLAYER!") == "Hallo ASH!");
    CHECK(h.vm.expand_text("PLAYER und RIVAL") == "ASH und GARY");
    CHECK(h.vm.expand_text("ohne \u201cSTR_VAR_1\u201d") ==
          "ohne \u201c" + h.gs.trendy_phrase + "\u201d");
    // A script that buffers STR_VAR_1 itself still wins over the fallback.
    h.run("Test_BufferString");
    if (h.vm.str_var("STR_VAR_1") == "prettily")
        CHECK(h.vm.expand_text("STR_VAR_1") == "prettily");
}

static void test_region_map_sections() {
    std::printf("[map] PokeNav region map sections\n");
    // The importer used to drop region_map_sections.json entirely, so every
    // map had no mapsec data and the POKENAV screen couldn't place a marker
    // anywhere -- the region map was completely missing. Pin real grid
    // rectangles (28x15 logical grid, pokeemerald's region_map.c) for a few
    // known towns, including an indoor map that must inherit its outdoor
    // town's section rather than having none.
    Map littleroot("maps/LittlerootTown.map");
    CHECK(littleroot.has_mapsec());
    CHECK(littleroot.mapsec_x() == 4 && littleroot.mapsec_y() == 11);
    CHECK(littleroot.mapsec_name() == "LITTLEROOT TOWN");

    Map littleroot_house("maps/LittlerootTown_BrendansHouse_2F.map");
    CHECK(littleroot_house.has_mapsec());
    CHECK(littleroot_house.mapsec_x() == 4 && littleroot_house.mapsec_y() == 11);

    Map soot("maps/SootopolisCity.map");
    CHECK(soot.has_mapsec());
    CHECK(soot.mapsec_x() == 21 && soot.mapsec_y() == 7);

    Map ever_grande("maps/EverGrandeCity.map");
    CHECK(ever_grande.has_mapsec());
    CHECK(ever_grande.mapsec_x() == 27 && ever_grande.mapsec_y() == 8);
    CHECK(ever_grande.mapsec_w() == 1 && ever_grande.mapsec_h() == 2);
}


// ------------------------------------------------------------ PartySystem --
// The party is a gameplay system, not the party screen's private list: every
// case below is a rule the screen used to have to remember (or, mostly, did
// not) and now cannot get wrong, because it goes through PartySystem.

// Convenience: a party with `n` members, all healthy.
static void fill_party(PartySystem& ps, BattleData& bd,
                       const std::vector<std::string>& species) {
    for (const std::string& sp : species) {
        Mon m = bd.make_mon(sp, 10);
        CHECK(ps.add(m) == PartyResult::OK);
    }
}

static void test_party_slots_and_overflow(BattleData& bd) {
    std::printf("[party] six slots, then the PC\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);

    CHECK(ps.empty() && ps.size() == 0);
    CHECK(ps.at(0) == nullptr);            // an empty slot is no Mon at all
    CHECK(ps.at(-1) == nullptr && ps.at(99) == nullptr);

    fill_party(ps, bd, {"TORCHIC", "MUDKIP", "TREECKO", "ZIGZAGOON",
                        "POOCHYENA", "WURMPLE"});
    CHECK(ps.size() == 6 && ps.full());
    CHECK(ps.at(5) != nullptr && ps.at(6) == nullptr);

    // A seventh goes to the PC rather than being lost or growing the party.
    int slot = 0;
    CHECK(ps.add(bd.make_mon("WINGULL", 8), &slot) == PartyResult::OK);
    CHECK(slot == -1);
    CHECK(ps.size() == 6 && ps.box_size() == 1);

    // Every member has its own id, and nothing exists twice (§22).
    std::vector<unsigned> ids;
    for (int i = 0; i < ps.size(); ++i) ids.push_back(ps.at(i)->uid);
    for (int i = 0; i < ps.box_size(); ++i) ids.push_back(ps.box_at(i)->uid);
    std::sort(ids.begin(), ids.end());
    CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
    CHECK(std::find(ids.begin(), ids.end(), 0u) == ids.end());
}

static void test_party_order(BattleData& bd) {
    std::printf("[party] changing the order keeps the lead on its own mon\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC", "MUDKIP", "TREECKO"});

    unsigned first = ps.at(0)->uid, second = ps.at(1)->uid;
    CHECK(ps.set_active_slot(0) == PartyResult::OK);
    CHECK(ps.swap_slots(0, 1) == PartyResult::OK);
    CHECK(ps.at(0)->uid == second && ps.at(1)->uid == first);
    // The lead followed the pokemon, not the slot number -- swapping the
    // order must not quietly change which mon leads.
    CHECK(ps.active_slot() == 1);
    CHECK(ps.at(2)->species == "TREECKO");   // untouched

    CHECK(ps.swap_slots(0, 0) == PartyResult::SAME_SLOT);
    CHECK(ps.swap_slots(0, 4) == PartyResult::INVALID_SLOT);
    CHECK(ps.swap_slots(-1, 1) == PartyResult::INVALID_SLOT);
}

static void test_party_box_moves(BattleData& bd) {
    std::printf("[party] depositing closes the gap and protects the last mon\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC", "MUDKIP", "TREECKO"});
    unsigned torchic = ps.at(0)->uid, treecko = ps.at(2)->uid;

    // Middle slot out: the ones below move up, no hole is left (§15).
    CHECK(ps.move_to_box(1) == PartyResult::OK);
    CHECK(ps.size() == 2);
    CHECK(ps.at(0)->uid == torchic && ps.at(1)->uid == treecko);
    CHECK(ps.at(2) == nullptr);
    CHECK(ps.box_size() == 1 && ps.box_at(0)->species == "MUDKIP");
    // Same individual, moved -- not copied into both places.
    CHECK(ps.slot_of(ps.box_at(0)->uid) == -1);
    CHECK(ps.find(ps.box_at(0)->uid) != nullptr);

    CHECK(ps.move_to_box(1) == PartyResult::OK);
    CHECK(ps.size() == 1);
    // The last one can never leave (§14).
    CHECK(ps.move_to_box(0) == PartyResult::LAST_POKEMON);
    CHECK(ps.release(0) == PartyResult::LAST_POKEMON);
    CHECK(ps.size() == 1);

    // Back out of the PC.
    int slot = -1;
    CHECK(ps.withdraw_from_box(0, &slot) == PartyResult::OK);
    CHECK(slot == 1 && ps.size() == 2 && ps.box_size() == 1);
    CHECK(ps.withdraw_from_box(9, &slot) == PartyResult::INVALID_SLOT);
}

static void test_party_keeps_an_able_member(BattleData& bd) {
    std::printf("[party] the last able pokemon cannot be stored away\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC", "MUDKIP"});
    // Knock the second one out: the first is now the only one that can fight.
    CHECK(ps.apply_damage(1, 9999) == PartyResult::OK);
    CHECK(ps.at(1)->fainted());
    // A fainted party member stays in the party (§17) ...
    CHECK(ps.size() == 2);
    // ... and the only able one may not be deposited.
    CHECK(ps.move_to_box(0) == PartyResult::LAST_ABLE_POKEMON);
    // The fainted one may: it takes no ability away from the party.
    CHECK(ps.move_to_box(1) == PartyResult::OK);

    // With the rule switched off (§14 makes it optional) the same call works.
    GameState gs2; PartySystem loose; loose.configure(&bd, &gs2);
    loose.set_require_able_member(false);
    fill_party(loose, bd, {"TORCHIC", "MUDKIP"});
    CHECK(loose.apply_damage(1, 9999) == PartyResult::OK);
    CHECK(loose.move_to_box(0) == PartyResult::OK);
}

static void test_party_held_items(BattleData& bd) {
    std::printf("[party] held items move between bag and pokemon exactly once\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC"});
    gs.give_item("ITEM_ORAN_BERRY", 1);
    gs.give_item("ITEM_MAGNET", 1);
    gs.give_item("ITEM_HM_CUT", 1);

    CHECK(ps.give_held_item(0, "ITEM_ORAN_BERRY") == PartyResult::OK);
    CHECK(ps.at(0)->held_item == "ITEM_ORAN_BERRY");
    CHECK(gs.item_count("ITEM_ORAN_BERRY") == 0);

    // Swapping puts the old item straight back in the bag: no duplication,
    // no silent loss (§11).
    CHECK(ps.give_held_item(0, "ITEM_MAGNET") == PartyResult::OK);
    CHECK(ps.at(0)->held_item == "ITEM_MAGNET");
    CHECK(gs.item_count("ITEM_ORAN_BERRY") == 1);
    CHECK(gs.item_count("ITEM_MAGNET") == 0);

    CHECK(ps.take_held_item(0) == PartyResult::OK);
    CHECK(ps.at(0)->held_item == "NONE");
    CHECK(gs.item_count("ITEM_MAGNET") == 1);
    CHECK(ps.take_held_item(0) == PartyResult::NO_HELD_ITEM);

    // Things a pokemon may not hold, and things that are not in the bag.
    CHECK(ps.give_held_item(0, "ITEM_HM_CUT") == PartyResult::ITEM_NOT_HOLDABLE);
    CHECK(ps.give_held_item(0, "ITEM_POKE_BALL") == PartyResult::ITEM_NOT_HOLDABLE);
    CHECK(ps.give_held_item(0, "ITEM_BICYCLE") == PartyResult::ITEM_NOT_HOLDABLE);
    CHECK(ps.give_held_item(0, "ITEM_LEFTOVERS") == PartyResult::ITEM_MISSING);
    CHECK(ps.give_held_item(3, "ITEM_MAGNET") == PartyResult::INVALID_SLOT);
    CHECK(gs.item_count("ITEM_HM_CUT") == 1);   // refusals cost nothing
}

static void test_party_healing(BattleData& bd) {
    std::printf("[party] healing, curing and reviving\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC"});
    const int max_hp = ps.at(0)->max_hp;

    CHECK(ps.heal_hp(0, 10) == PartyResult::NOTHING_TO_DO);   // already full
    CHECK(ps.apply_damage(0, max_hp - 1) == PartyResult::OK);
    CHECK(ps.at(0)->hp == 1);
    CHECK(ps.heal_hp(0, 5) == PartyResult::OK);
    CHECK(ps.at(0)->hp == 6);
    CHECK(ps.heal_hp(0, 0) == PartyResult::OK);               // 0 = full restore
    CHECK(ps.at(0)->hp == max_hp);

    CHECK(ps.cure_status(0) == PartyResult::NOTHING_TO_DO);
    CHECK(ps.revive(0, false) == PartyResult::NOT_FAINTED);

    CHECK(ps.apply_damage(0, 9999) == PartyResult::OK);
    CHECK(ps.at(0)->hp == 0 && ps.at(0)->fainted());
    CHECK(ps.heal_hp(0, 20) == PartyResult::IS_FAINTED);      // needs a Revive
    CHECK(ps.revive(0, false) == PartyResult::OK);
    CHECK(ps.at(0)->hp == std::max(1, max_hp / 2));
    CHECK(ps.apply_damage(0, 9999) == PartyResult::OK);
    CHECK(ps.revive(0, true) == PartyResult::OK);
    CHECK(ps.at(0)->hp == max_hp);
    CHECK(ps.has_able_pokemon() && ps.first_able_slot() == 0);
}

static void test_party_events(BattleData& bd) {
    std::printf("[party] every change tells the listeners what happened\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    std::vector<PartyEvent> seen;
    std::vector<int> slots;
    int token = ps.subscribe([&](const PartyNotice& n) {
        seen.push_back(n.event); slots.push_back(n.slot);
    });
    auto saw = [&](PartyEvent e) {
        return std::find(seen.begin(), seen.end(), e) != seen.end();
    };

    fill_party(ps, bd, {"TORCHIC", "MUDKIP"});
    CHECK(saw(PartyEvent::PokemonAddedToParty) && saw(PartyEvent::PartyChanged));

    seen.clear(); slots.clear();
    CHECK(ps.swap_slots(0, 1) == PartyResult::OK);
    CHECK(saw(PartyEvent::PartyOrderChanged));

    seen.clear();
    CHECK(ps.apply_damage(0, 9999) == PartyResult::OK);
    CHECK(saw(PartyEvent::PokemonUpdated) && saw(PartyEvent::PokemonFainted));

    seen.clear();
    CHECK(ps.revive(0, true) == PartyResult::OK);
    CHECK(saw(PartyEvent::PokemonHealed));

    seen.clear();
    gs.give_item("ITEM_MAGNET", 1);
    CHECK(ps.give_held_item(0, "ITEM_MAGNET") == PartyResult::OK);
    CHECK(saw(PartyEvent::HeldItemChanged));

    seen.clear();
    CHECK(ps.move_to_box(1) == PartyResult::OK);
    CHECK(saw(PartyEvent::PokemonRemovedFromParty) && saw(PartyEvent::BoxChanged));

    // A refused operation must not raise anything at all.
    seen.clear();
    CHECK(ps.move_to_box(0) == PartyResult::LAST_POKEMON);
    CHECK(seen.empty());

    // ... and once unsubscribed, nothing else arrives.
    ps.unsubscribe(token);
    seen.clear();
    CHECK(ps.apply_damage(0, 1) == PartyResult::OK);
    CHECK(seen.empty());
}

static void test_party_partial_ui_updates(BattleData& bd) {
    std::printf("[party] one mon changing dirties only its own row\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC", "MUDKIP", "TREECKO"});

    unsigned before0 = ps.slot_revision(0);
    unsigned before1 = ps.slot_revision(1);
    unsigned before2 = ps.slot_revision(2);
    CHECK(ps.apply_damage(1, 3) == PartyResult::OK);
    // Only slot 1's revision moved, so a screen keyed off these numbers
    // rebuilds one row rather than the whole party (§28).
    CHECK(ps.slot_revision(0) == before0);
    CHECK(ps.slot_revision(1) != before1);
    CHECK(ps.slot_revision(2) == before2);
}

static void test_party_sync_after_raw_writes(BattleData& bd) {
    std::printf("[party] raw battle writes still raise the right events\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC", "MUDKIP"});

    std::vector<PartyEvent> seen;
    ps.subscribe([&](const PartyNotice& n) { seen.push_back(n.event); });
    auto saw = [&](PartyEvent e) {
        return std::find(seen.begin(), seen.end(), e) != seen.end();
    };

    // This is what Battle does: it holds a Mon* into the party's storage and
    // writes HP straight through it.
    ps.party_storage()[0].hp = 0;
    ps.sync();
    CHECK(saw(PartyEvent::PokemonUpdated) && saw(PartyEvent::PokemonFainted));

    // And this is `givemon`: a push straight onto the vector. The mon still
    // has to end up adopted, with an id of its own.
    seen.clear();
    ps.party_storage().push_back(bd.make_mon("WINGULL", 6));
    ps.sync();
    CHECK(saw(PartyEvent::PokemonAddedToParty));
    CHECK(ps.size() == 3 && ps.at(2)->uid != 0);
    CHECK(ps.at(2)->uid != ps.at(0)->uid);

    // A frame in which nothing happened must stay quiet.
    seen.clear();
    ps.sync();
    CHECK(seen.empty());
}

static void test_party_move_learning(BattleData& bd) {
    std::printf("[party] a fifth move asks instead of overwriting one\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);

    Mon m = bd.make_mon("TORCHIC", 5);
    m.moves = {"SCRATCH", "GROWL", "EMBER", "TACKLE"};
    bd.restore_pp(m);
    CHECK(ps.add(m) == PartyResult::OK);

    // Four already: appending is refused, and a duplicate is refused too.
    CHECK(ps.learn_move(0, "PECK", -1) == PartyResult::NO_MOVE);
    CHECK(ps.learn_move(0, "EMBER", 0) == PartyResult::ALREADY_KNOWS_MOVE);
    CHECK(ps.learn_move(0, "NOT_A_REAL_MOVE", 0) == PartyResult::NO_MOVE);

    // Explicit replacement works and resets that slot's PP to the new move's.
    CHECK(ps.learn_move(0, "PECK", 1) == PartyResult::OK);
    CHECK(ps.at(0)->moves[1] == "PECK");
    const MoveInfo* peck = bd.move("PECK");
    CHECK(peck && ps.at(0)->pp[1] == peck->pp);
    CHECK(ps.at(0)->moves.size() == 4);

    // The queued prompt: nothing changes until the player answers it.
    CHECK(!ps.has_pending_move_learn());
    CHECK(ps.queue_move_learn(0, "QUICK_ATTACK") == PartyResult::OK);
    CHECK(ps.has_pending_move_learn());
    CHECK(ps.pending_move_learn()->move == "QUICK_ATTACK");
    std::string kept = ps.at(0)->moves[0];
    CHECK(ps.resolve_move_learn(-1) == PartyResult::OK);     // declined
    CHECK(!ps.has_pending_move_learn());
    CHECK(ps.at(0)->moves[0] == kept);
    CHECK(std::find(ps.at(0)->moves.begin(), ps.at(0)->moves.end(),
                    std::string("QUICK_ATTACK")) == ps.at(0)->moves.end());

    CHECK(ps.queue_move_learn(0, "QUICK_ATTACK") == PartyResult::OK);
    CHECK(ps.resolve_move_learn(0) == PartyResult::OK);
    CHECK(ps.at(0)->moves[0] == "QUICK_ATTACK");
    CHECK(ps.resolve_move_learn(0) == PartyResult::NO_PENDING_REQUEST);
}

static void test_party_level_up_defers_the_fifth_move(BattleData& bd) {
    std::printf("[party] a level-up move that does not fit is deferred\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);

    // Poochyena learns Howl at 5, Sand Attack at 9, Bite at 13 and Odor
    // Sleuth at 17 -- four by level 17, so the next one has nowhere to go.
    Mon m = bd.make_mon("POOCHYENA", 4);
    CHECK(ps.add(m) == PartyResult::OK);
    std::vector<std::string> msgs;
    CHECK(ps.grant_exp(0, 60000, msgs) == PartyResult::OK);
    CHECK(ps.at(0)->level > 4);
    CHECK(ps.at(0)->moves.size() <= 4);           // never more than four
    if (ps.has_pending_move_learn()) {
        // Whatever was deferred is a real move the mon does not know yet,
        // and the moveset is untouched until the player answers.
        const MoveLearnRequest* req = ps.pending_move_learn();
        CHECK(bd.move(req->move) != nullptr);
        CHECK(std::find(ps.at(0)->moves.begin(), ps.at(0)->moves.end(), req->move) ==
              ps.at(0)->moves.end());
    }

    // The old, report-less path is unchanged for callers that have no party
    // system to defer into (the headless drivers).
    Mon plain = bd.make_mon("POOCHYENA", 4);
    std::vector<std::string> pm;
    bd.grant_exp(plain, 60000, pm);
    CHECK(plain.moves.size() <= 4);
    CHECK(plain.level > 4);
}

static void test_party_exp_events(BattleData& bd) {
    std::printf("[party] experience raises level-up and evolution events\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    Mon m = bd.make_mon("WURMPLE", 5);           // evolves at 7
    CHECK(ps.add(m) == PartyResult::OK);

    std::vector<PartyEvent> seen;
    ps.subscribe([&](const PartyNotice& n) { seen.push_back(n.event); });
    auto saw = [&](PartyEvent e) {
        return std::find(seen.begin(), seen.end(), e) != seen.end();
    };

    std::vector<std::string> msgs;
    CHECK(ps.grant_exp(0, 5000, msgs) == PartyResult::OK);
    CHECK(!msgs.empty());
    CHECK(saw(PartyEvent::PokemonLevelUp));
    CHECK(saw(PartyEvent::PokemonUpdated));
    if (ps.at(0)->species != "WURMPLE") {
        CHECK(saw(PartyEvent::PokemonEvolutionStarted));
        CHECK(saw(PartyEvent::PokemonEvolutionCompleted));
        CHECK(gs.is_caught(ps.at(0)->species));   // the dex follows along
    }

    // A fainted mon earns nothing, and neither does a bad slot.
    CHECK(ps.apply_damage(0, 9999) == PartyResult::OK);
    CHECK(ps.grant_exp(0, 100, msgs) == PartyResult::IS_FAINTED);
    CHECK(ps.grant_exp(4, 100, msgs) == PartyResult::INVALID_SLOT);
}

static void test_party_evs(BattleData& bd) {
    std::printf("[party] effort values respect the real caps\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC"});

    CHECK(ps.add_ev(0, 'A', 300) == PartyResult::OK);
    CHECK(ps.at(0)->ev_atk == 255);                     // per-stat cap
    CHECK(ps.add_ev(0, 'A', 10) == PartyResult::NOTHING_TO_DO);
    CHECK(ps.add_ev(0, 'D', 300) == PartyResult::OK);
    CHECK(ps.at(0)->ev_total() == 510);                  // total cap
    CHECK(ps.add_ev(0, 'E', 4) == PartyResult::NOTHING_TO_DO);
    CHECK(ps.add_ev(0, 'A', 0) == PartyResult::NOTHING_TO_DO);

    // A stat with EVs in it really is bigger than the same mon without.
    Mon bare = bd.make_mon("TORCHIC", 10);
    bare.iv_atk = ps.at(0)->iv_atk;
    bare.nature = ps.at(0)->nature;
    bd.recompute_stats(bare, true);
    CHECK(ps.at(0)->atk > bare.atk);
}

static void test_party_companion_is_not_the_lead(BattleData& bd) {
    std::printf("[party] the walking companion is its own state\n");
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    fill_party(ps, bd, {"TORCHIC", "MUDKIP", "TREECKO"});

    CHECK(ps.companion_slot() == -1);
    CHECK(ps.set_active_slot(0) == PartyResult::OK);
    CHECK(ps.set_companion_slot(2) == PartyResult::OK);
    CHECK(ps.active_slot() == 0 && ps.companion_slot() == 2);
    CHECK(ps.set_companion_slot(7) == PartyResult::INVALID_SLOT);

    // Depositing the companion clears it rather than leaving it dangling.
    CHECK(ps.move_to_box(2) == PartyResult::OK);
    CHECK(ps.companion_slot() == -1);
    CHECK(ps.active_slot() == 0);
    CHECK(ps.set_companion_slot(-1) == PartyResult::OK);
}

static void test_party_save_roundtrip(BattleData& bd) {
    std::printf("[party] a saved party comes back exactly as it was\n");
    const char* path = "test_party_save.dat";

    GameState gs;
    gs.player_name = "MAY";
    gs.trainer_id = 12345; gs.secret_id = 54321;
    PartySystem ps; ps.configure(&bd, &gs);

    std::mt19937 rng(7);
    Mon lead = bd.make_mon("TORCHIC", 14, &rng, gs.trainer_id, gs.secret_id);
    PartySystem::stamp_origin(lead, gs, "ITEM_NET_BALL", "Route 101");
    lead.nickname = "FLAMMI";
    lead.friendship = 133;
    lead.ev_spa = 44; lead.ev_spe = 8;
    lead.ribbons.push_back("RIBBON_CHAMPION");
    lead.hp = 3;
    lead.status = Status::PARALYSIS;
    CHECK(ps.add(lead) == PartyResult::OK);
    fill_party(ps, bd, {"MUDKIP", "TREECKO"});
    CHECK(ps.add_to_box(bd.make_mon("ZIGZAGOON", 3)) == PartyResult::OK);
    CHECK(ps.set_active_slot(1) == PartyResult::OK);
    CHECK(ps.set_companion_slot(2) == PartyResult::OK);
    unsigned lead_uid = ps.at(0)->uid;
    unsigned next_uid = ps.next_uid();

    CHECK(SaveGame::save(path, gs, ps, "maps/Route101.map", 3, 4));

    GameState gs2; PartySystem ps2; ps2.configure(&bd, &gs2);
    std::string map2; int x2 = 0, y2 = 0;
    CHECK(SaveGame::load(path, gs2, ps2, map2, x2, y2));
    CHECK(map2 == "maps/Route101.map" && x2 == 3 && y2 == 4);
    CHECK(ps2.size() == 3 && ps2.box_size() == 1);
    const Mon* back = ps2.at(0);
    CHECK(back != nullptr);
    CHECK(back->uid == lead_uid);
    CHECK(back->nickname == "FLAMMI" && back->display_name() == "FLAMMI");
    CHECK(back->ot_name == "MAY" && back->ot_id == 12345 && back->ot_secret == 54321);
    CHECK(back->ball == "ITEM_NET_BALL");
    CHECK(back->met_location == "Route 101" && back->met_level == 14);
    CHECK(back->friendship == 133);
    CHECK(back->ev_spa == 44 && back->ev_spe == 8 && back->ev_hp == 0);
    CHECK(back->ribbons.size() == 1 && back->ribbons[0] == "RIBBON_CHAMPION");
    CHECK(back->hp == 3 && back->status == Status::PARALYSIS);
    // A mon with no ribbons must survive too -- its (empty) last column used
    // to disappear in the split and take the whole record's tail with it.
    CHECK(ps2.at(1)->ribbons.empty());
    CHECK(ps2.at(1)->species == "MUDKIP" && ps2.at(2)->species == "TREECKO");
    CHECK(ps2.box_at(0)->species == "ZIGZAGOON");
    // The bookkeeping the raw-vector format could not carry.
    CHECK(ps2.active_slot() == 1 && ps2.companion_slot() == 2);
    CHECK(ps2.next_uid() >= next_uid);
    // A mon created after the load still gets an id of its own.
    int slot = -1;
    CHECK(ps2.add(bd.make_mon("WINGULL", 5), &slot) == PartyResult::OK);
    CHECK(ps2.at(slot)->uid != lead_uid);
    std::remove(path);
}

static void test_party_loads_a_pre_party_save(BattleData& bd) {
    std::printf("[party] a savegame written before the party system loads\n");
    const char* path = "test_party_old_save.dat";
    // Exactly the record shape the previous format wrote: 27 columns, no uid
    // and no origin data.
    {
        std::FILE* f = std::fopen(path, "w");
        CHECK(f != nullptr);
        if (!f) return;
        std::fprintf(f, "SAVE 1\n");
        std::fprintf(f, "map\tmaps/LittlerootTown.map\n");
        std::fprintf(f, "pos\t5\t7\n");
        std::fprintf(f, "team\t1\n");
        std::fprintf(f,
            "TORCHIC\t9\t20\t28\t14\t12\t15\t13\t14\tFIRE\tFIRE\t500\t"
            "SCRATCH,GROWL\t0\t0\t0\tHARDY\t15\t15\t15\t15\t15\t15\t35,40\t"
            "NONE\t0\t0\n");
        std::fclose(f);
    }
    GameState gs; PartySystem ps; ps.configure(&bd, &gs);
    std::string map; int x = 0, y = 0;
    CHECK(SaveGame::load(path, gs, ps, map, x, y));
    CHECK(ps.size() == 1);
    const Mon* m = ps.at(0);
    CHECK(m && m->species == "TORCHIC" && m->level == 9 && m->hp == 20);
    CHECK(m && m->moves.size() == 2 && m->pp.size() == 2);
    // The fields that did not exist then come back at their defaults, and the
    // mon is adopted so it still has an id of its own.
    CHECK(m && m->uid != 0);
    CHECK(m && m->nickname.empty() && m->display_name() == "TORCHIC");
    CHECK(m && m->ball == "NONE" && m->met_level == 0);
    CHECK(m && m->ev_total() == 0 && m->ribbons.empty());
    std::remove(path);
}

static void test_party_gender_is_stable(BattleData& bd) {
    std::printf("[party] gender is derived once and never drifts\n");
    // Same personality value -> same answer, every time.
    CHECK(BattleData::gender("TORCHIC", 0x1234) == BattleData::gender("TORCHIC", 0x1234));
    CHECK(BattleData::gender("MAGNEMITE", 12345) == 'N');
    CHECK(BattleData::gender("NIDORAN_M", 12345) == 'M');
    CHECK(BattleData::gender("CHANSEY", 12345) == 'F');
    // The 50/50 split is read off the low byte, so both answers are reachable.
    CHECK(BattleData::gender("TORCHIC", 0x00) == 'F');
    CHECK(BattleData::gender("TORCHIC", 0xFF) == 'M');
    CHECK(std::string(BattleData::gender_symbol('N')).empty());
    CHECK(!std::string(BattleData::gender_symbol('M')).empty());
    (void)bd;
}

// --------------------------------------------------------------------- main --


// ---------------------------------------------------------------- QuestLog --
// The quest log owns no state: every step is a predicate over the flags and
// vars the imported scripts already set (QuestLog.h). These cases pin that
// down -- a quest must never be able to disagree with the world, and the one
// piece of player intent it does keep (which quest is pinned to the HUD) has
// to survive a save/load.

static void test_quest_conditions() {
    std::printf("[quest] condition grammar\n");
    GameState gs;
    gs.set_flag("FLAG_BADGE01_GET");
    gs.set_var("VAR_RUSTBORO_CITY_STATE", 3);
    gs.give_item("ITEM_POTION", 2);
    gs.mark_caught("TORCHIC");
    gs.money = 500;

    CHECK(QuestLog::eval_condition("FLAG_BADGE01_GET", gs));
    CHECK(!QuestLog::eval_condition("FLAG_BADGE02_GET", gs));
    // An unknown name reads as "not set" instead of failing the whole quest.
    CHECK(!QuestLog::eval_condition("FLAG_NEVER_HEARD_OF_IT", gs));
    CHECK(QuestLog::eval_condition("!FLAG_BADGE02_GET", gs));
    CHECK(QuestLog::eval_condition("VAR_RUSTBORO_CITY_STATE >= 3", gs));
    CHECK(!QuestLog::eval_condition("VAR_RUSTBORO_CITY_STATE > 3", gs));
    CHECK(QuestLog::eval_condition("VAR_RUSTBORO_CITY_STATE != 4", gs));
    CHECK(QuestLog::eval_condition("ITEM_POTION", gs));
    CHECK(QuestLog::eval_condition("ITEM_POTION >= 2", gs));
    CHECK(!QuestLog::eval_condition("ITEM_POTION >= 3", gs));
    CHECK(QuestLog::eval_condition("caught:TORCHIC", gs));
    CHECK(!QuestLog::eval_condition("caught:MUDKIP", gs));
    CHECK(QuestLog::eval_condition("badges == 1", gs));
    CHECK(QuestLog::eval_condition("money >= 500", gs));
    // & binds tighter than |, and an empty condition is "already satisfied"
    // (that's what makes `start` optional).
    CHECK(QuestLog::eval_condition("FLAG_BADGE01_GET & !FLAG_BADGE02_GET", gs));
    CHECK(!QuestLog::eval_condition("FLAG_BADGE01_GET & FLAG_BADGE02_GET", gs));
    CHECK(QuestLog::eval_condition("FLAG_BADGE02_GET | FLAG_BADGE01_GET", gs));
    CHECK(QuestLog::eval_condition("FLAG_BADGE02_GET & FLAG_BADGE03_GET | FLAG_BADGE01_GET", gs));
    CHECK(QuestLog::eval_condition("", gs));
}

// Writes a throwaway quests.txt and loads it, so the test doesn't depend on
// the shipped story data staying the same shape forever.
static bool write_test_quests(const char* path) {
    std::FILE* f = std::fopen(path, "w");
    if (!f) return false;
    std::fputs("# comment line\n"
               "quest QUEST_A main\n"
               "title Erste Aufgabe\n"
               "desc Eine Beschreibung.\n"
               "step Erreiche die Stadt | FLAG_VISITED_RUSTBORO_CITY | RustboroCity\n"
               "step Besiege Rocko | FLAG_BADGE01_GET | RustboroCity_Gym 27 19\n"
               "\n"
               "quest QUEST_B side\n"
               "title Zweite Aufgabe\n"
               "start FLAG_BADGE01_GET\n"
               "step Hole das Fahrrad | FLAG_RECEIVED_BIKE | MauvilleCity_BikeShop\n",
               f);
    std::fclose(f);
    return true;
}

static void test_quest_progress() {
    std::printf("[quest] progress is derived from world flags\n");
    const char* path = "test_quests.txt";
    CHECK(write_test_quests(path));
    QuestLog log;
    CHECK(log.load(path));
    CHECK(log.quests().size() == 2);

    GameState gs;
    log.refresh(gs);
    // Nothing done yet: the main quest is active on its first step, the side
    // quest is still hidden behind its own start condition.
    CHECK(log.active(QuestKind::MAIN).size() == 1);
    CHECK(log.active(QuestKind::SIDE).empty());
    CHECK(log.quests()[0].percent == 0);
    CHECK(log.quests()[0].current_step == 0);
    CHECK(log.tracked(gs) == 0);
    const QuestStep* st = log.tracked_step(gs);
    CHECK(st && st->target_map == "RustboroCity");
    // An exact tile is optional; the first step only names a map.
    CHECK(st && st->target_x == -1);

    gs.set_flag("FLAG_VISITED_RUSTBORO_CITY");
    log.refresh(gs);
    CHECK(log.quests()[0].percent == 50);
    CHECK(log.quests()[0].current_step == 1);
    st = log.tracked_step(gs);
    CHECK(st && st->target_map == "RustboroCity_Gym" && st->target_x == 27 && st->target_y == 19);

    gs.set_flag("FLAG_BADGE01_GET");
    log.refresh(gs);
    // Main quest done -> off the active list, and the side quest its badge
    // unlocked takes over the HUD without anyone tracking anything by hand.
    CHECK(log.quests()[0].status == QuestStatus::DONE);
    CHECK(log.quests()[0].percent == 100);
    CHECK(log.completed().size() == 1);
    CHECK(log.active(QuestKind::MAIN).empty());
    CHECK(log.active(QuestKind::SIDE).size() == 1);
    CHECK(log.tracked(gs) == 1);

    // A pinned quest wins over the automatic pick -- but only while it is
    // still active, so finishing it can never leave the HUD stuck on it.
    gs.tracked_quest = "QUEST_A";
    CHECK(log.tracked(gs) == 1);
    gs.tracked_quest = "QUEST_B";
    CHECK(log.tracked(gs) == 1);
    // An id from an edited quests.txt falls back instead of blanking the HUD.
    gs.tracked_quest = "QUEST_GONE";
    CHECK(log.tracked(gs) == 1);

    gs.set_flag("FLAG_RECEIVED_BIKE");
    log.refresh(gs);
    CHECK(log.tracked(gs) == -1);          // nothing left to do
    CHECK(log.tracked_step(gs) == nullptr);
    std::remove(path);
}

static void test_quest_hud_choice_survives_a_save(BattleData& bd) {
    std::printf("[quest] tracked quest + HUD toggle round-trip\n");
    GameState gs;
    gs.tracked_quest = "QUEST_BADGE_STONE";
    gs.quest_hud_on = false;
    PartySystem ps; ps.configure(&bd, &gs);
    const char* path = "test_quest_save.dat";
    CHECK(SaveGame::save(path, gs, ps, "maps/LittlerootTown.map", 5, 9));

    GameState loaded; PartySystem lp; lp.configure(&bd, &loaded);
    std::string map; int px = 0, py = 0;
    CHECK(SaveGame::load(path, loaded, lp, map, px, py));
    CHECK(loaded.tracked_quest == "QUEST_BADGE_STONE");
    CHECK(loaded.quest_hud_on == false);
    std::remove(path);
}

// The shipped story data has to actually parse and chain: exactly one main
// mission active on a fresh save, and no step pointing at a map that isn't in
// the game (a typo there would silently kill the world marker).
static void test_shipped_quest_data() {
    std::printf("[quest] assets/quests.txt is consistent\n");
    QuestLog log;
    if (!log.load("assets/quests.txt")) {
        std::printf("  (skipped: assets/quests.txt not staged)\n");
        return;
    }
    GameState gs;
    log.refresh(gs);
    CHECK(log.active(QuestKind::MAIN).size() == 1);
    CHECK(log.tracked(gs) >= 0);
    for (const Quest& q : log.quests()) {
        CHECK(!q.id.empty());
        CHECK(!q.title.empty());
        CHECK(!q.steps.empty());
        for (const QuestStep& st : q.steps) {
            CHECK(!st.text.empty());
            CHECK(!st.done_cond.empty());
            if (st.target_map.empty()) continue;
            std::string mp = "maps/" + st.target_map + ".map";
            std::FILE* f = std::fopen(mp.c_str(), "r");
            if (!f) std::printf("  missing target map: %s\n", mp.c_str());
            CHECK(f != nullptr);
            if (f) std::fclose(f);
        }
    }
}


// -------------------------------------------------------------------- Bike --
// Rydel's script already hands the bike over; these pin the rules that come
// after that -- when you may ride, how fast, and which sheet you are drawn
// with (Bike.h).

static void test_bike_mount_rules() {
    std::printf("[bike] when the bike may be used\n");
    GameState gs;
    Bike bike;
    CHECK(Bike::in_bag(gs) == BikeKind::NONE);
    // No bike in the bag: nothing else matters yet.
    CHECK(bike.toggle(gs, false, false) == BikeResult::NO_BIKE);
    CHECK(!bike.riding());

    gs.give_item("ITEM_MACH_BIKE", 1);
    CHECK(Bike::in_bag(gs) == BikeKind::MACH);
    // Refused indoors and while surfing -- and refusing must not leave the
    // player half-mounted.
    CHECK(bike.toggle(gs, true, false) == BikeResult::INDOORS);
    CHECK(!bike.riding());
    CHECK(bike.toggle(gs, false, true) == BikeResult::SURFING);
    CHECK(!bike.riding());

    CHECK(bike.toggle(gs, false, false) == BikeResult::MOUNTED);
    CHECK(bike.riding() && bike.riding_kind() == BikeKind::MACH);
    // Toggling again gets off, wherever you are.
    CHECK(bike.toggle(gs, false, false) == BikeResult::DISMOUNTED);
    CHECK(!bike.riding());
    // A forced dismount (walked into a building) only reports work it did.
    CHECK(!bike.dismount());
    CHECK(bike.toggle(gs, false, false) == BikeResult::MOUNTED);
    CHECK(bike.dismount());

    // Swapping bikes at the shop swaps which one you ride.
    gs.take_item("ITEM_MACH_BIKE", 1);
    gs.give_item("ITEM_ACRO_BIKE", 1);
    CHECK(bike.toggle(gs, false, false) == BikeResult::MOUNTED);
    CHECK(bike.riding_kind() == BikeKind::ACRO);
}

static void test_bike_speed() {
    std::printf("[bike] the mach bike accelerates, the acro bike doesn't\n");
    const float walk = 0.15f;
    GameState gs;
    gs.give_item("ITEM_ACRO_BIKE", 1);
    Bike acro;
    CHECK(acro.step_interval(walk) == walk);            // on foot: unchanged
    CHECK(acro.toggle(gs, false, false) == BikeResult::MOUNTED);
    for (int i = 0; i < 10; ++i) acro.on_step(DIR::N);
    CHECK(acro.step_interval(walk) == walk / 2.f);      // flat 2x, forever

    gs.take_item("ITEM_ACRO_BIKE", 1);
    gs.give_item("ITEM_MACH_BIKE", 1);
    Bike mach;
    CHECK(mach.toggle(gs, false, false) == BikeResult::MOUNTED);
    CHECK(mach.step_interval(walk) == walk / 2.f);      // starts at 2x
    for (int i = 0; i < Bike::MACH_RAMP_STEPS - 1; ++i) mach.on_step(DIR::N);
    CHECK(mach.step_interval(walk) == walk / 2.f);      // still winding up
    mach.on_step(DIR::N);
    CHECK(mach.step_interval(walk) == walk / 3.f);      // at full speed
    // Turning throws the run-up away, and so does stopping.
    mach.on_step(DIR::E);
    CHECK(mach.step_interval(walk) == walk / 2.f);
    for (int i = 0; i < Bike::MACH_RAMP_STEPS; ++i) mach.on_step(DIR::E);
    CHECK(mach.step_interval(walk) == walk / 3.f);
    mach.on_stop();
    CHECK(mach.step_interval(walk) == walk / 2.f);
    // Off the bike the interval is the walking one again, whatever happened.
    mach.dismount();
    CHECK(mach.step_interval(walk) == walk);
}

static void test_bike_sheets_exist() {
    std::printf("[bike] both bikes have both genders' overworld sheets\n");
    const BikeKind kinds[] = {BikeKind::MACH, BikeKind::ACRO, BikeKind::NONE};
    for (BikeKind k : kinds)
        for (bool female : {false, true}) {
            std::string path = Bike::sheet_for(k, female);
            std::FILE* f = std::fopen(path.c_str(), "r");
            if (!f) std::printf("  missing sheet: %s\n", path.c_str());
            CHECK(f != nullptr);
            if (f) std::fclose(f);
        }
}

static void test_bike_survives_a_save(BattleData& bd) {
    std::printf("[bike] still riding after a save/load\n");
    GameState gs;
    gs.give_item("ITEM_MACH_BIKE", 1);
    gs.on_bike = true;
    PartySystem ps; ps.configure(&bd, &gs);
    const char* path = "test_bike_save.dat";
    CHECK(SaveGame::save(path, gs, ps, "maps/Route110.map", 10, 10));

    GameState loaded; PartySystem lp; lp.configure(&bd, &loaded);
    std::string map; int px = 0, py = 0;
    CHECK(SaveGame::load(path, loaded, lp, map, px, py));
    CHECK(loaded.on_bike);
    Bike bike;
    bike.resume(loaded);
    CHECK(bike.riding() && bike.riding_kind() == BikeKind::MACH);
    std::remove(path);
}

// Which maps refuse the bike. The .map files in the tree carry no map type
// yet (pe_import.py writes one now), so this pins the fallback derivation --
// interiors out, routes/towns/caves in.
static void test_map_indoor_classification() {
    std::printf("[bike] indoor maps are recognised\n");
    Map center("maps/OldaleTown_PokemonCenter_1F.map");
    CHECK(center.ready() && center.is_indoor());
    Map gym("maps/RustboroCity_Gym.map");
    CHECK(gym.ready() && gym.is_indoor());
    Map shop("maps/MauvilleCity_BikeShop.map");
    CHECK(shop.ready() && shop.is_indoor());

    Map town("maps/OldaleTown.map");
    CHECK(town.ready() && !town.is_indoor());
    Map route("maps/Route110.map");
    CHECK(route.ready() && !route.is_indoor());
    // A cave is not an interior: the real games let you ride in one.
    Map cave("maps/GraniteCave_1F.map");
    CHECK(cave.ready() && !cave.is_indoor());
}


static void test_bike_rail_rules() {
    std::printf("[bike] rails only take the acro bike, along their own axis\n");
    // Not a rail: never gated, whatever you are riding.
    CHECK(Bike::can_ride_rail(0, DIR::N, BikeKind::NONE));
    CHECK(Bike::can_ride_rail(0, DIR::E, BikeKind::MACH));
    // A vertical rail takes the ACRO BIKE going north/south and nothing else.
    CHECK(Bike::can_ride_rail(1, DIR::N, BikeKind::ACRO));
    CHECK(Bike::can_ride_rail(1, DIR::S, BikeKind::ACRO));
    CHECK(!Bike::can_ride_rail(1, DIR::E, BikeKind::ACRO));
    CHECK(!Bike::can_ride_rail(1, DIR::N, BikeKind::MACH));
    CHECK(!Bike::can_ride_rail(1, DIR::N, BikeKind::NONE));
    // ... and a horizontal one is the same rule turned 90 degrees.
    CHECK(Bike::can_ride_rail(2, DIR::E, BikeKind::ACRO));
    CHECK(Bike::can_ride_rail(2, DIR::W, BikeKind::ACRO));
    CHECK(!Bike::can_ride_rail(2, DIR::S, BikeKind::ACRO));
    CHECK(!Bike::can_ride_rail(2, DIR::E, BikeKind::MACH));
}

static void test_bike_terrain_data() {
    std::printf("[bike] muddy slopes and rails are imported per map\n");
    // Route 119's rails (the Acro bike ones over the water) and the muddy
    // slope on its southern climb -- the tiles pe_import.py derives from
    // pokeemerald's own metatile behaviours.
    Map r119("maps/Route119.map");
    CHECK(r119.ready());
    CHECK(r119.rail_axis(5, 8) == Map::RailAxis::VERTICAL);
    CHECK(r119.rail_axis(9, 5) == Map::RailAxis::HORIZONTAL);
    CHECK(r119.is_muddy_slope(6, 54));
    // Ordinary ground is neither.
    CHECK(r119.rail_axis(5, 12) == Map::RailAxis::NONE);
    CHECK(!r119.is_muddy_slope(5, 12));
    // A map with a slope but no rails still parses both sections correctly.
    Map r115("maps/Route115.map");
    CHECK(r115.ready());
    bool any_slope = false;
    for (int y = 0; y < r115.get_height() && !any_slope; ++y)
        for (int x = 0; x < r115.get_width(); ++x)
            if (r115.is_muddy_slope(x, y)) { any_slope = true; break; }
    CHECK(any_slope);
    // And a map with neither answers "no" everywhere rather than misreading
    // some other map's ids.
    Map town("maps/OldaleTown.map");
    CHECK(town.ready());
    CHECK(!town.is_muddy_slope(6, 16));
    CHECK(town.rail_axis(6, 16) == Map::RailAxis::NONE);
}

static void test_cycling_track_present() {
    std::printf("[bike] MUS_CYCLING is converted\n");
    // The bike theme is the one track that isn't any map's own music, so a
    // re-import has to be told to convert it (pe_import.py's DEFAULT_SONGS).
    std::FILE* f = std::fopen("assets/sfx/music/mus_cycling.ogg", "r");
    if (!f) std::printf("  missing assets/sfx/music/mus_cycling.ogg\n");
    CHECK(f != nullptr);
    if (f) std::fclose(f);
}

int main() {
    std::printf("Running Codemon engine tests...\n");

    BattleData bd;
    if (!bd.load(BATTLE_DIR)) {
        std::printf("FAILED: could not load %s (run ctest from the build dir)\n",
                    BATTLE_DIR);
        return EXIT_FAILURE;
    }

    test_struggle_ignores_type_chart(bd);
    test_pp_and_movesets(bd);
    test_species_data(bd);
    test_shiny(bd);
    test_save_roundtrip(bd);
    test_save_empty_party();
    test_save_pre_shiny_file();
    test_save_rejects_garbage();

    test_party_slots_and_overflow(bd);
    test_party_order(bd);
    test_party_box_moves(bd);
    test_party_keeps_an_able_member(bd);
    test_party_held_items(bd);
    test_party_healing(bd);
    test_party_events(bd);
    test_party_partial_ui_updates(bd);
    test_party_sync_after_raw_writes(bd);
    test_party_move_learning(bd);
    test_party_level_up_defers_the_fifth_move(bd);
    test_party_exp_events(bd);
    test_party_evs(bd);
    test_party_companion_is_not_the_lead(bd);
    test_party_save_roundtrip(bd);
    test_party_loads_a_pre_party_save(bd);
    test_party_gender_is_stable(bd);

    test_quest_conditions();
    test_quest_progress();
    test_quest_hud_choice_survives_a_save(bd);
    test_shipped_quest_data();

    test_bike_mount_rules();
    test_bike_speed();
    test_bike_sheets_exist();
    test_bike_survives_a_save(bd);
    test_bike_rail_rules();
    test_cycling_track_present();

    if (has_display()) {
        test_script_opcodes(bd);
        test_script_text_and_objects(bd);
        test_script_items_and_gifts(bd);
        test_script_trainer_flags(bd);
        test_recoil_uses_hp_actually_dealt(bd);
        test_wild_battle_capture(bd);
        test_battle_lead_skips_fainted(bd);
        test_switch_menu_starts_on_a_usable_mon(bd);
        test_capture_keeps_the_encounter(bd);
        test_capture_keeps_hp_and_status(bd);
        test_trainer_ai_uses_item(bd);
        test_trainer_sight();
        test_sight_only_while_undefeated(bd);
        test_dive_reaches_sootopolis();
        test_dynamic_warp_out_of_truck(bd);
        test_region_map_sections();
        test_map_indoor_classification();
        test_bike_terrain_data();
        test_sprite_frame_geometry();
        test_object_state_animation(bd);
        test_briney_voyage(bd);
        test_briney_voyage_from_dewford(bd);
        test_plain_dialog_tokens(bd);
    } else {
        std::printf("[skip] no DISPLAY: Map/ScriptVM/Battle tests need a GL "
                    "context (run under xvfb-run to include them)\n");
        skipped = 1;
    }

    if (failures == 0) {
        std::printf(skipped ? "OK: all runnable tests passed (some skipped).\n"
                            : "OK: all engine tests passed.\n");
        return EXIT_SUCCESS;
    }
    std::printf("FAILED: %d check(s) failed.\n", failures);
    return EXIT_FAILURE;
}
