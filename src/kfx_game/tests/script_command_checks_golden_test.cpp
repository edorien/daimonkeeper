// kfx_game: golden hashes of what script commands' *_check functions store (refactor pass 3, S07 B:
// the check pairs of lvl_script_commands.c share helpers now; these must not change when they do).
//
// Each case loads a one-command script through level_script_override_set(), the command inside an IF
// block so its value is stored instead of run, and hashes the level script state (values, conditions,
// strings) and the quick message texts. A few creature, room and power names are registered first so
// the checks' success paths run too.
//
// To print the hashes (only when a commit is meant to change what a check stores):
//     KFX_GAME_GOLDEN_PRINT=1 kfx_game_utest "[script_golden]"
#include <catch2/catch_test_macros.hpp>

#include "lvl_script.h"
#include "lvl_script_lib.h"
#include "lvl_script_conditions.h"
#include "lvl_filesdk1.h"
#include "level_script_override.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "config_creature.h"
#include "config_magic.h"
#include "config_terrain.h"
#include "ports/ui_port.h"
#include "kfx_config/tests/scoped_port_override.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

const LevelNumber kLevel = 9877;

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
};

struct NameTables {
    struct NamedCommand creatures[4], rooms[3], powers[3];
    NameTables() {
        std::memcpy(creatures, creature_desc, sizeof(creatures));
        std::memcpy(rooms, room_desc, sizeof(rooms));
        std::memcpy(powers, power_desc, sizeof(powers));
        creature_desc[0] = {"WIZARD", 1};
        creature_desc[1] = {"IMP", 2};
        creature_desc[2] = {"TROLL", 3};
        creature_desc[3] = {nullptr, 0};
        room_desc[0] = {"LIBRARY", 3};
        room_desc[1] = {"TREASURE", 2};
        room_desc[2] = {nullptr, 0};
        power_desc[0] = {"POWER_HAND", 1};
        power_desc[1] = {"POWER_IMP", 2};
        power_desc[2] = {nullptr, 0};
        kfx_config_state.conf.crtr_conf.model_count = 4;
    }
    ~NameTables() {
        std::memcpy(creature_desc, creatures, sizeof(creatures));
        std::memcpy(room_desc, rooms, sizeof(rooms));
        std::memcpy(power_desc, powers, sizeof(powers));
        kfx_config_state.conf.crtr_conf.model_count = 0;
    }
};

// A top-level quick message creates an event, which asks UiPort for the event kind's timings.
const struct EventTypeInfo *stub_event_button_info(EventKind) {
    static const struct EventTypeInfo info = {};
    return &info;
}

uint64_t load_and_hash(const std::string &body) {
    // A top-level line runs at once (messages, events), so start from empty game and sim state.
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    level_script_override_clear();
    level_script_override_set(kLevel, "REM nothing\n", ("LEVEL_VERSION(1)\n" + body).c_str());
    preload_script(kLevel);
    load_script(kLevel);
    Fnv h;
    h.add(&kfx_game_state.script, sizeof(kfx_game_state.script));
    h.add(kfx_sim_state.quick_messages, sizeof(kfx_sim_state.quick_messages));
    level_script_override_clear();
    return h.v;
}

const std::vector<const char *> kLines = {
    // quick messages
    "QUICK_OBJECTIVE(1,\"Hello\",PLAYER0)", "QUICK_OBJECTIVE(1,\"Hello\",ALL_PLAYERS)", "QUICK_OBJECTIVE(1,\"Hello\")",
    "QUICK_OBJECTIVE(-1,\"Hello\",PLAYER0)", "QUICK_OBJECTIVE(999,\"Hello\",PLAYER0)", "QUICK_OBJECTIVE(2,\"Hello\",NOT_A_PLACE)",
    "QUICK_OBJECTIVE(3,\"Hello\",PLAYER0,NOT_AN_ICON)", "QUICK_OBJECTIVE_WITH_POS(4,\"Hello\",10,20)",
    "QUICK_OBJECTIVE_WITH_POS(4,\"Hello\",10,20,NOT_AN_ICON)", "QUICK_INFORMATION(5,\"Info\",PLAYER1)",
    "QUICK_INFORMATION(5,\"Info\",PLAYER1,NOT_AN_ICON)", "QUICK_INFORMATION_WITH_POS(6,\"Info\",30,40)",
    "QUICK_INFORMATION(-3,\"Info\",PLAYER1)", "QUICK_PLAYER_OBJECTIVE(7,PLAYER0,\"Hi\",PLAYER1)",
    "QUICK_PLAYER_OBJECTIVE(7,PLAYER0,\"Hi\",PLAYER1,NOT_AN_ICON)", "QUICK_PLAYER_OBJECTIVE_WITH_POS(8,PLAYER0,\"Hi\",5,6)",
    "QUICK_PLAYER_OBJECTIVE(8,PLAYER0,\"Hi\",NOWHERE)", "QUICK_PLAYER_INFORMATION(9,PLAYER2,\"Pi\",PLAYER0)",
    "QUICK_PLAYER_INFORMATION_WITH_POS(10,PLAYER2,\"Pi\",7,8)", "QUICK_PLAYER_INFORMATION(999,PLAYER2,\"Pi\",PLAYER0)",
    // conditions
    "IF_AVAILABLE(PLAYER0,IMP > 0)", "IF_AVAILABLE(PLAYER0,LIBRARY >= 1)", "IF_AVAILABLE(PLAYER0,POWER_HAND == 1)",
    "IF_AVAILABLE(PLAYER0,NOT_A_THING > 0)", "IF_CONTROLS(PLAYER0,IMP > 0)", "IF_CONTROLS(PLAYER1,TOTAL_CREATURES > 3)",
    "IF_CONTROLS(PLAYER0,NOT_A_THING > 0)", "IF(PLAYER0,MONEY > 100)", "IF(PLAYER0,IMP >= 2)", "IF(PLAYER0,FLAG0 == 1)",
    "IF(PLAYER0,CAMPAIGN_FLAG3 == 1)", "IF(PLAYER0,BOX4_ACTIVATED == 1)", "IF(PLAYER0,LIBRARY > 2)",
    "IF(PLAYER0,NOT_A_VARIABLE > 2)", "IF(PLAYER0,MONEY > PLAYER1,MONEY)",
    // player modifiers
    "SET_PLAYER_MODIFIER(PLAYER0,Health,120)", "SET_PLAYER_MODIFIER(PLAYER0,Speed,-5)", "SET_PLAYER_MODIFIER(PLAYER0,NoSuch,1)",
    "ADD_TO_PLAYER_MODIFIER(PLAYER1,Health,10)", "ADD_TO_PLAYER_MODIFIER(PLAYER1,Loyalty,-2)", "ADD_TO_PLAYER_MODIFIER(PLAYER1,NoSuch,1)",
    // computer settings
    "SET_COMPUTER_PROCESS(PLAYER1,\"BUILD ALL ROOM 3x3\",1,2,3,4,5)", "SET_COMPUTER_CHECKS(PLAYER1,\"CHECK MONEY\",1,2,3,4,5)",
    "SET_COMPUTER_PROCESS(ALL_PLAYERS,\"\",1,2,3,4,5)", "SET_COMPUTER_CHECKS(PLAYER9,\"X\",1,2,3,4,5)",
    // creature configuration
    "SET_CREATURE_CONFIGURATION(IMP,Health,300)", "SET_CREATURE_CONFIGURATION(IMP,Size,100,200)",
    "SET_CREATURE_CONFIGURATION(IMP,NoSuchKey,1)", "SET_CREATURE_CONFIGURATION(TROLL,PrimaryJobs,DIG)",
    "SET_CREATURE_CONFIGURATION(WIZARD,Properties,FLYING)", "SET_CREATURE_CONFIGURATION(NOBODY,Health,1)",
    "SET_CREATURE_CONFIGURATION(IMP,HostileTowards,TROLL)", "SET_CREATURE_CONFIGURATION(IMP,LairEnemy,TROLL)",
    "SET_CREATURE_CONFIGURATION(IMP,LairEnemy,TROLL,WIZARD)", "SET_CREATURE_CONFIGURATION(IMP,LairEnemy,TROLL,WIZARD,IMP)",
    "SET_CREATURE_CONFIGURATION(IMP,LairEnemy,ANY_CREATURE)", "SET_CREATURE_CONFIGURATION(IMP,LairEnemy,NULL)",
    "SET_CREATURE_CONFIGURATION(IMP,LairEnemy,NOBODY)",
    // refused after the script value is allocated (pass 3 F14 and its siblings)
    "SET_HAND_RULE(PLAYER0,IMP,NO_SLOT,ALLOW,ALWAYS)", "MOVE_CREATURE(PLAYER0,IMP,NO_CRITERIA,1,1)",
    "MOVE_CREATURE(PLAYER0,IMP,ANY,1,NOWHERE)", "USE_SPELL_ON_CREATURE(PLAYER0,IMP,ANY,NO_SUCH_SPELL,1)",
    "SET_DOOR(NO_STATE,1,1)", "SET_DOOR(LOCKED,999,999)", "CHANGE_SLAB_TYPE(999,1,1)", "HIDE_HERO_GATE(999,1)",
    "CREATE_EFFECT(NO_SUCH_EFFECT,PLAYER0,0)", "PLAY_MESSAGE(PLAYER0,NO_TYPE,1)",
    "SET_CREATURE_INSTANCE(IMP,1,NO_SUCH_INSTANCE,1)", "DISPLAY_INFORMATION(1,NOWHERE)",
    "SET_TEXTURE(PLAYER0,NO_SUCH_PACK)", "SET_PLAYER_COLOR(PLAYER0,NO_COLOUR)",
    // variables
    "SET_FLAG(PLAYER0,FLAG2,5)", "ADD_TO_FLAG(PLAYER0,FLAG2,5)", "SET_FLAG(PLAYER0,NOT_A_FLAG,5)",
    "IF(PLAYER0,SACRIFICED[IMP] > 0)", "IF(PLAYER0,REWARDED[TROLL] >= 2)", "IF(PLAYER0,SACRIFICED[NOBODY] > 0)",
    "IF(PLAYER0,SACRIFICED[IMP > 0)", "IF(PLAYER0,KEEPERS_DESTROYED[PLAYER1] == 1)", "IF(PLAYER0,BOX3_ACTIVATE == 1)",
    "IF(PLAYER0,TRAP2_ACTIVATED == 1)", "SET_FLAG(PLAYER0,SACRIFICED[IMP],1)", "SET_FLAG(PLAYER0,REWARDED[WIZARD],2)",
    "SET_FLAG(PLAYER0,BOX3_ACTIVATE,1)", "SET_FLAG(PLAYER0,BOX3_ACTIVATED,1)",
};

} // namespace

TEST_CASE("golden: what the script command checks store", "[kfx_game][script_golden]")
{
    NameTables names;
    ScopedPortOverride<UiPort> ui{ui_port, set_ui_port};
    ui->get_event_button_info = stub_event_button_info;
    const std::map<std::string, uint64_t> expected = {
#include "script_command_checks_golden.inc"
    };
    const bool print = std::getenv("KFX_GAME_GOLDEN_PRINT") != nullptr;
    int64_t checked = 0;
    for (const char *line : kLines)
    {
        for (int in_if = 0; in_if < 2; in_if++)
        {
            const std::string body = in_if ? (std::string("IF(PLAYER0,MONEY > 0)\n") + line + "\nENDIF\n") : (std::string(line) + "\n");
            const std::string name = std::string(in_if ? "if:" : "top:") + line;
            const uint64_t h = load_and_hash(body);
            if (print)
            {
                std::string quoted;
                for (char c : name) {
                    if (c == '"' || c == '\\') quoted += '\\';
                    quoted += c;
                }
                std::printf("    {\"%s\", 0x%016llxULL},\n", quoted.c_str(), (unsigned long long)h);
                continue;
            }
            auto it = expected.find(name);
            INFO(name);
            REQUIRE(it != expected.end());
            CHECK(it->second == h);
            checked++;
        }
    }
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    if (!print)
        CHECK(checked == (int64_t)(2 * kLines.size()));
}

TEST_CASE("parse_get_varib and parse_set_varib refuse a mistyped BOXn_ACTIVATED (pass 3 F15)", "[kfx_game][script_golden]")
{
    int64_t id = 0, type = 0;
    // "BOX3_ACTIVATE" (no D): sscanf reads the 3 before failing; parse_get_varib used to keep it as campaign flag 3.
    CHECK_FALSE(parse_get_varib("BOX3_ACTIVATE", &id, &type, 1));
    CHECK_FALSE(parse_get_varib("TRAP2_ACTIVATE", &id, &type, 1));
    CHECK_FALSE(parse_set_varib("BOX3_ACTIVATE", &id, &type));
    CHECK(parse_get_varib("BOX3_ACTIVATED", &id, &type, 1));
    CHECK(id == 3);
    CHECK(type == SVar_BOX_ACTIVATED);
}

TEST_CASE("a refused IF opens a never-true condition, so its body doesn't run and its ENDIF closes it (pass 4 P4-F2)", "[kfx_game][script_golden]")
{
    NameTables names;
    ScopedPortOverride<UiPort> ui{ui_port, set_ui_port};
    ui->get_event_button_info = stub_event_button_info;
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
    level_script_override_clear();
    level_script_override_set(kLevel, "REM nothing\n",
        "LEVEL_VERSION(1)\n"
        "IF(PLAYER0,MONEY > 100)\n"
        "IF(PLAYER0,BOX3_ACTIVATE == 1)\n"      // refused (F15), inside a real condition
        "SET_FLAG(PLAYER0,FLAG1,1)\n"
        "ENDIF\n"
        "SET_FLAG(PLAYER0,FLAG2,1)\n"            // back under MONEY > 100
        "ENDIF\n");
    preload_script(kLevel);
    load_script(kLevel);
    level_script_override_clear();
    const struct LevelScript *script = &kfx_game_state.script;
    REQUIRE(script->conditions_num == 2);
    CHECK(script->conditions[0].variabl_type == SVar_MONEY);
    CHECK(script->conditions[1].variabl_type == SVar_NEVER_TRUE);
    CHECK(script->conditions[1].condit_idx == 0);
    REQUIRE(script->values_num == 2);
    CHECK(script->values[0].condit_idx == 1); // FLAG1: under the never-true condition
    CHECK(script->values[1].condit_idx == 0); // FLAG2: under MONEY > 100, not the refused IF
    CHECK(get_script_current_condition() == CONDITION_ALWAYS);
    // A never-true condition stays false even when its parent is true.
    kfx_game_state.script.conditions[0].status = 0x01;
    process_conditions();
    CHECK((kfx_game_state.script.conditions[1].status & 0x01) == 0);
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
}

TEST_CASE("a legacy command's values are the same run at once and run inside an IF", "[kfx_game][script_golden]")
{
    // Refactor pass 4, S09: the legacy commands become check/process pairs; a value kept for later (inside an IF)
    // must still do what it does at once. Script numbers are parsed as 32-bit (LbStrToI32, saturating), so the
    // value's 32-bit storage loses nothing: 3000000000 is 2147483647 both ways.
    ScopedPortOverride<UiPort> ui{ui_port, set_ui_port};
    ui->get_event_button_info = stub_event_button_info;
    NameTables names;
    const char *lines = "SET_FLAG(PLAYER0,FLAG3,3000000000)\nADD_TO_FLAG(PLAYER0,FLAG4,-3000000000)\n"
        "SET_FLAG(PLAYER1,FLAG5,77)\nADD_TO_FLAG(ALL_PLAYERS,FLAG6,-5)\n";
    load_and_hash(lines);
    int64_t at_once[PLAYERS_COUNT][4];
    for (int p = 0; p < PLAYERS_COUNT; p++)
        for (int f = 0; f < 4; f++)
            at_once[p][f] = kfx_sim_state.dungeon[p].script_flags[3 + f];
    CHECK(at_once[0][0] == INT32_MAX);
    CHECK(at_once[0][1] == INT32_MIN);
    CHECK(at_once[1][2] == 77);
    CHECK(at_once[2][3] == -5);
    load_and_hash(std::string("IF(PLAYER0,GAME_TURN >= 0)\n") + lines + "ENDIF\n");
    CHECK(kfx_sim_state.dungeon[0].script_flags[3] == 0);
    process_level_script();
    for (int p = 0; p < PLAYERS_COUNT; p++)
        for (int f = 0; f < 4; f++)
        {
            INFO("player " << p << " flag " << (3 + f));
            CHECK(kfx_sim_state.dungeon[p].script_flags[3 + f] == at_once[p][f]);
        }
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
}
