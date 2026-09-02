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

// --------------------------------------------------------------------- main --

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

    if (has_display()) {
        test_script_opcodes(bd);
        test_script_text_and_objects(bd);
        test_script_items_and_gifts(bd);
        test_script_trainer_flags(bd);
        test_recoil_uses_hp_actually_dealt(bd);
        test_wild_battle_capture(bd);
        test_battle_lead_skips_fainted(bd);
        test_capture_keeps_the_encounter(bd);
        test_capture_keeps_hp_and_status(bd);
        test_trainer_ai_uses_item(bd);
        test_trainer_sight();
        test_sight_only_while_undefeated(bd);
        test_dive_reaches_sootopolis();
        test_dynamic_warp_out_of_truck(bd);
        test_region_map_sections();
        test_sprite_frame_geometry();
        test_object_state_animation(bd);
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
