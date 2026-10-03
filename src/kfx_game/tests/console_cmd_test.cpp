// kfx_game: console_cmd.c's dispatcher, cmd_exec(). A cheat command is refused with
// "require 'cheat mode'" unless cheat mode is on (refactor pass 3, S07 A: the check
// moved from 63 commands' own first lines into the command table).
#include <catch2/catch_test_macros.hpp>

#include "console_cmd.h"
#include "player_data.h"
#include "kfx_game_state.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "ports/ui_port.h"
#include "kfx_config/tests/scoped_port_override.h"

#include <cstring>
#include <string>

namespace {
// console_cmd.c sends its replies through UiPort (kfx_frontend's messages sit above kfx_game).
int reply_count = 0;
std::string reply_text;
void record_reply(char, PlayerNumber, PlayerNumber, uint64_t, const char *msg) {
    reply_count++;
    reply_text = msg;
}

struct ConsoleFixture {
    ScopedPortOverride<UiPort> ui{ui_port, set_ui_port};
    ConsoleFixture() {
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL;
        for (PlayerNumber i = 0; i < PLAYERS_COUNT; i++)
            kfx_sim_state.players[i].id_number = i;
        kfx_sim_state.players[0].allocflags |= PlaF_Allocated;
        my_player_number = 0;
        ui->targeted_message_add = record_reply;
        reply_count = 0;
        reply_text.clear();
    }
    TbBool exec(const char *line) {
        std::string buf = line;
        return cmd_exec(0, &buf[0]);
    }
    std::string last_reply() const { return reply_text; }
};
}

TEST_CASE_METHOD(ConsoleFixture, "cheat commands are refused with the same reply when cheat mode is off", "[kfx_game][console_cmd]") {
    kfx_sim_state.easter_eggs_enabled = false;
    for (const char *line : {"reveal", "conceal", "comp.kill 1", "player.score", "magic.instance", "compuchat scripted"}) {
        INFO(line);
        reply_count = 0;
        CHECK_FALSE(exec(line));
        CHECK(reply_count == 1);
        CHECK(last_reply() == "require 'cheat mode'");
    }
}

TEST_CASE_METHOD(ConsoleFixture, "with cheat mode on a cheat command runs (and reports its own argument errors)", "[kfx_game][console_cmd]") {
    kfx_sim_state.easter_eggs_enabled = true;
    CHECK_FALSE(exec("magic.instance"));
    CHECK(last_reply() == "require parameter 1 as creature");
}

TEST_CASE_METHOD(ConsoleFixture, "an unknown command is reported only in cheat mode", "[kfx_game][console_cmd]") {
    kfx_sim_state.easter_eggs_enabled = false;
    CHECK_FALSE(exec("no.such.command"));
    CHECK(reply_count == 0);
    kfx_sim_state.easter_eggs_enabled = true;
    CHECK_FALSE(exec("no.such.command"));
    CHECK(last_reply() == "unsupported command");
}

TEST_CASE_METHOD(ConsoleFixture, "creature.pool.add and .sub change a model's pool count by the amount", "[kfx_game][console_cmd]") {
    kfx_sim_state.easter_eggs_enabled = true;
    const auto saved_model_count = kfx_config_state.conf.crtr_conf.model_count;
    kfx_config_state.conf.crtr_conf.model_count = 10;
    CHECK(exec("creature.pool.add 3 5"));
    CHECK(kfx_sim_state.pool.crtr_kind[3] == 5);
    CHECK(exec("creature.pool.sub 3 2"));
    CHECK(kfx_sim_state.pool.crtr_kind[3] == 3);
    CHECK(exec("creature.pool.remove 3 -4"));
    CHECK(kfx_sim_state.pool.crtr_kind[3] == 7);
    CHECK_FALSE(exec("creature.pool.add 3"));
    CHECK(last_reply() == "require parameter 2 as creature amount");
    CHECK_FALSE(exec("creature.pool.sub 0 1"));
    CHECK(last_reply() == "invalid creature model");
    // a model the config doesn't have (P5-F11: 'any' is 254, past crtr_kind's 128 entries)
    CHECK_FALSE(exec("creature.pool.add 10 1"));
    CHECK_FALSE(exec("creature.pool.add any 1"));
    CHECK(last_reply() == "invalid creature model");
    kfx_config_state.conf.crtr_conf.model_count = saved_model_count;
}

TEST_CASE_METHOD(ConsoleFixture, "action point and player index arguments are checked with the same replies", "[kfx_game][console_cmd]") {
    kfx_sim_state.easter_eggs_enabled = true;
    CHECK_FALSE(exec("actionpoint.pos"));
    CHECK(last_reply() == "require parameter 1 as actionpoint number");
    CHECK_FALSE(exec("actionpoint.pos 7"));
    CHECK(last_reply() == "actionpoint no exist");
    CHECK_FALSE(exec("comp.checks"));
    CHECK(last_reply() == "require parameter 1 as player idx");
    CHECK_FALSE(exec("comp.checks 99"));
    CHECK(last_reply().rfind("player idx [99] exceeds [0,", 0) == 0);
}

TEST_CASE_METHOD(ConsoleFixture, "in a multiplayer game cheat commands are refused even with cheat mode on", "[kfx_game][console_cmd]") {
    kfx_sim_state.easter_eggs_enabled = true;
    kfx_sim_state.game_kind = GKind_MultiGame;
    CHECK_FALSE(exec("reveal"));
    CHECK(last_reply() == "require 'cheat mode'");
    CHECK_FALSE(exec("thing.get 1"));
    CHECK(last_reply() == "require 'cheat mode'");
}
