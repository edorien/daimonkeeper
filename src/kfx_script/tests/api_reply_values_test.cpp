// kfx_script: api.c -- the values in a few replies, which the golden hashes (all-zero state) cannot see:
// get_all_player_flags gives each player its own flags (it gave every player the asking player's), numbers
// past 32 bits come back whole with an ack and in VAR_UPDATE, and an event name too long to keep is refused
// instead of being stored cut short (where it never matched).
#include <catch2/catch_test_macros.hpp>

#include "api.h"
#include "kfx_game_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"

#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace {

struct LocalGame {
    LocalGame() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        api_clear_all_subscriptions();
        net_clear_external_seats();
        kfx_sim_state.game_kind = GKind_LocalGame;
    }
    ~LocalGame() { api_clear_all_subscriptions(); }
};

std::string reply_to(const std::string &request) {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    api_process_message_for_test(request.c_str(), request.size() + 1, sv[0]);
    std::string out;
    char buf[65536];
    for (;;) {
        const ssize_t n = recv(sv[1], buf, sizeof(buf), MSG_DONTWAIT);
        if (n <= 0)
            break;
        out.append(buf, (size_t)n);
    }
    close(sv[0]);
    close(sv[1]);
    return out;
}

/** The text of `"key":{...}` in a minimised reply. */
std::string object_of(const std::string &reply, const std::string &key) {
    const size_t at = reply.find("\"" + key + "\":{");
    REQUIRE(at != std::string::npos);
    const size_t end = reply.find('}', at);
    REQUIRE(end != std::string::npos);
    return reply.substr(at, end - at + 1);
}

} // namespace

TEST_CASE_METHOD(LocalGame, "get_all_player_flags gives each player its own flags", "[kfx_script][api]") {
    kfx_sim_state.dungeon[0].script_flags[1] = 11;
    kfx_sim_state.dungeon[2].script_flags[1] = 22;
    kfx_sim_state.dungeon[2].script_flags[3] = 5000000000LL;
    const std::string reply = reply_to("{\"action\":\"get_all_player_flags\",\"player\":0}");
    const std::string p0 = object_of(reply, "PLAYER0");
    const std::string p2 = object_of(reply, "PLAYER2");
    CHECK(p0.find("\"FLAG1\":11") != std::string::npos);
    CHECK(p2.find("\"FLAG1\":22") != std::string::npos);
    CHECK(p2.find("\"FLAG3\":5000000000") != std::string::npos);
    CHECK(object_of(reply, "PLAYER1").find("\"FLAG1\":0") != std::string::npos);
}

TEST_CASE_METHOD(LocalGame, "read_var answers a value past 32 bits whole, with or without an ack", "[kfx_script][api]") {
    kfx_sim_state.dungeon[0].total_money_owned = 5000000000LL;
    CHECK(reply_to("{\"action\":\"read_var\",\"var\":\"MONEY\",\"player\":0}").find("\"data\":5000000000") !=
          std::string::npos);
    CHECK(reply_to("{\"action\":\"read_var\",\"var\":\"MONEY\",\"player\":0,\"ack\":1}").find("\"data\":5000000000") !=
          std::string::npos);
}

TEST_CASE_METHOD(LocalGame, "an event name too long to keep is refused, one that fits is kept whole", "[kfx_script][api]") {
    const std::string fits(COMMAND_WORD_LEN - 1, 'e');
    const std::string too_long(COMMAND_WORD_LEN, 'e');
    CHECK(reply_to("{\"action\":\"subscribe_event\",\"event\":\"" + too_long + "\"}").find("STRING_TOO_LONG") !=
          std::string::npos);
    CHECK_FALSE(api_is_subscribed_to_event(fits.c_str()));
    CHECK(reply_to("{\"action\":\"subscribe_event\",\"event\":\"" + fits + "\"}").find("\"success\":true") !=
          std::string::npos);
    CHECK(api_is_subscribed_to_event(fits.c_str()));
}
