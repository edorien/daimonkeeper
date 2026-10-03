// kfx_script: golden hashes of the API's replies (refactor pass 4, S06: api_process_buffer's 51 inline
// handlers become a table of functions; these hashes must not change when they do).
//
// Each request is handed to api_process_buffer (through api_process_message_for_test) with a socketpair
// end as the client, and the bytes it sends back are hashed. The state is reset before each request:
// "none" (no game), "local" (a local game), "paused" (a paused local game), "player1" (a local game
// where player 1 exists but isn't an external seat) or "seat1" (player 1 is an external seat, user 1). With no level loaded, most actions answer with an
// error; that covers every action's routing, its argument checks and the simple successes. The seat
// actions' successes need a running game with external seats: the AI-bridge ftests cover those.
// get_log_tail is left out: its reply is the log file. The product version (VER_STRING: the build number a
// packaging configure sets, the release) is replaced by "<version>" before hashing: it isn't the API's behaviour.
//
// To print the hashes (only on a commit meant to change a reply):
//     KFX_SCRIPT_GOLDEN_PRINT=1 kfx_script_utest "[api_golden]"
#include <catch2/catch_test_macros.hpp>

#include "agent_memory.h"
#include "api.h"
#include "bflib_datetm.h"
#include "kfx_game_state.h"
#include "kfx_net_state.h"
#include "kfx_sim_state.h"
#include "net_game.h"
#include "player_data.h"
#include "version.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

struct Fnv {
    uint64_t v = 1469598103934665603ULL;
    void add(const void *p, size_t n) {
        const unsigned char *b = static_cast<const unsigned char *>(p);
        for (size_t i = 0; i < n; i++) { v ^= b[i]; v *= 1099511628211ULL; }
    }
};

struct Case { const char *state; std::string request; };

// The seat actions note the time of the agent's last activity; no timer is installed in a unit test.
TbClockMSec fixed_clock(void) { return 1000; }

std::vector<Case> cases() {
    const std::string long_event(300, 'e');
    std::vector<Case> c = {
        // parsing
        {"none", "   "}, {"none", "not json"}, {"none", "[1,2]"}, {"none", "{}"}, {"none", "{\"ack\":7}"},
        {"none", "{\"action\":\"no_such_action\"}"}, {"local", "{\"action\":\"no_such_action\",\"ack\":\"a1\"}"},
        {"none", "{\"action\":\"get_kfx_info\",\"ack\":5}"},
        {"none", "{\"action\":\"GET_KFX_INFO\"}"}, {"none", "{\"action\":42}"},
        // an ack that owns memory is copied into the reply (pass 4 finding P4-F4: it was freed twice)
        {"none", "{\"action\":\"get_kfx_info\",\"ack\":{\"n\":[1,2],\"s\":\"x\"}}"},
        {"none", "{\"action\":\"no_such_action\",\"ack\":[1,\"two\",{\"three\":3}]}"},
        {"local", "{\"action\":\"read_var\",\"var\":\"MONEY\",\"ack\":\"" + std::string(100, 'a') + "\"}"},
        {"none", "{\"ack\":{\"deep\":{\"er\":[[]]}}}"},
        // always
        {"none", "{\"action\":\"subscribe_var\",\"var\":\"MONEY\",\"ack\":1}"},
        {"none", "{\"action\":\"subscribe_var\",\"var\":\"MONEY\",\"player\":\"PLAYER1\"}"},
        {"none", "{\"action\":\"subscribe_var\",\"var\":\"NO_SUCH_VAR\"}"}, {"none", "{\"action\":\"subscribe_var\"}"},
        {"none", "{\"action\":\"subscribe_var\",\"var\":\"\"}"},
        {"none", "{\"action\":\"unsubscribe_var\",\"var\":\"MONEY\"}"}, {"none", "{\"action\":\"unsubscribe_var\",\"var\":\"NO_SUCH_VAR\"}"},
        {"none", "{\"action\":\"unsubscribe_var\"}"},
        {"none", "{\"action\":\"subscribe_event\",\"event\":\"onLevelUp\"}"}, {"none", "{\"action\":\"subscribe_event\"}"},
        {"none", "{\"action\":\"subscribe_event\",\"event\":\"" + long_event + "\"}"},
        {"none", "{\"action\":\"unsubscribe_event\",\"event\":\"onLevelUp\"}"}, {"none", "{\"action\":\"unsubscribe_event\"}"},
        {"none", "{\"action\":\"unsubscribe_all\",\"ack\":3}"},
        // a local game
        {"local", "{\"action\":\"map_command\",\"command\":\"REM hello\"}"}, {"local", "{\"action\":\"map_command\"}"},
        {"paused", "{\"action\":\"map_command\",\"command\":\"REM hello\"}"},
        {"local", "{\"action\":\"console_command\",\"command\":\"no_such_command\"}"},
        {"local", "{\"action\":\"console_command\",\"command\":\"\"}"}, {"local", "{\"action\":\"console_command\"}"},
        {"paused", "{\"action\":\"console_command\",\"command\":\"no_such_command\"}"},
        {"local", "{\"action\":\"get_all_player_flags\"}"},
        {"local", "{\"action\":\"claim_seat\"}"}, {"local", "{\"action\":\"claim_seat\",\"player\":\"NO_SUCH_PLAYER\"}"},
        {"local", "{\"action\":\"release_seat\"}"}, {"local", "{\"action\":\"release_seat\",\"player\":1}"},
        {"local", "{\"action\":\"set_takeover\",\"enabled\":false}"}, {"local", "{\"action\":\"set_takeover\",\"enabled\":0}"},
        {"local", "{\"action\":\"set_takeover\"}"},
        {"local", "{\"action\":\"set_decision_policy\",\"min_interval_turns\":100}"}, {"local", "{\"action\":\"set_decision_policy\"}"},
        {"local", "{\"action\":\"set_game_speed\",\"turns_per_second\":20}"}, {"local", "{\"action\":\"set_game_speed\",\"turns_per_second\":0}"},
        {"local", "{\"action\":\"set_game_speed\",\"turns_per_second\":200}"}, {"local", "{\"action\":\"set_game_speed\"}"},
        {"local", "{\"action\":\"get_seats\"}"},
        {"local", "{\"action\":\"read_var\",\"var\":\"MONEY\"}"}, {"local", "{\"action\":\"read_var\",\"var\":\"NO_SUCH_VAR\"}"},
        {"local", "{\"action\":\"read_var\"}"},
        {"local", "{\"action\":\"set_var\",\"var\":\"FLAG0\",\"value\":3}"}, {"local", "{\"action\":\"set_var\",\"var\":\"MONEY\",\"value\":3}"},
        {"local", "{\"action\":\"set_var\",\"var\":\"FLAG0\",\"value\":\"x\"}"}, {"local", "{\"action\":\"set_var\",\"var\":\"NO_SUCH_VAR\"}"},
        {"local", "{\"action\":\"set_var\"}"},
        {"local", "{\"action\":\"get_current_game_info\"}"},
        // no level loaded (pass 4 finding P4-F3: its level information was read through a NULL pointer)
        {"local", "{\"action\":\"get_level_info\"}"}, {"local", "{\"action\":\"get_map_info\",\"ack\":2}"},
    };
    // a seat's own argument checks
    const std::string sa = "{\"player\":1,\"action\":\"submit_action\"";
    for (const std::string &tail : std::vector<std::string>{
            "}", ",\"verb\":\"no_such_verb\"}", ",\"verb\":\"slap\",\"direction\":\"UP\"}",
            ",\"verb\":\"pick_up\",\"thing_ids\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33]}",
            ",\"verb\":\"pick_up\",\"thing_ids\":[1,\"x\"]}",
            ",\"verb\":\"send_message\",\"message\":\"" + std::string(400, 'm') + "\"}",
            ",\"verb\":\"slap\",\"thing_id\":5,\"dry_run\":true}", ",\"verb\":\"mark_dig\",\"slab_rect\":[1,1,2,2],\"dry_run\":1}"})
        c.push_back({"seat1", sa + tail});
    for (const std::string &r : std::vector<std::string>{
            "{\"action\":\"set_pause\",\"player\":1}", "{\"action\":\"set_pause\",\"player\":1,\"paused\":\"yes\"}",
            "{\"action\":\"advance_turns\",\"player\":1}", "{\"action\":\"advance_turns\",\"player\":1,\"turns\":0}",
            "{\"action\":\"set_agent_memory\",\"player\":1}", "{\"action\":\"set_agent_memory\",\"player\":1,\"data\":\"remember this\"}",
            "{\"action\":\"get_agent_memory\",\"player\":1}", "{\"action\":\"get_agent_memory\",\"player\":1,\"ack\":\"m\"}",
            "{\"action\":\"set_player_name\",\"player\":1}", "{\"action\":\"set_player_name\",\"player\":1,\"name\":\"Agent Smith\"}",
            "{\"action\":\"set_player_name\",\"player\":1,\"name\":\"" + std::string(100, 'n') + "\"}",
            "{\"action\":\"set_player_name\",\"player\":1,\"name\":\"bad\\u0001name\"}",
            "{\"action\":\"get_seats\"}", "{\"action\":\"release_seat\",\"player\":1}"})
        c.push_back({"seat1", r});
    for (const char *seat_action : {"get_player_view", "submit_action", "set_pause", "advance_turns", "set_agent_memory",
                                    "get_agent_memory", "set_player_name"}) {
        const std::string a = std::string("{\"action\":\"") + seat_action + "\"";
        c.push_back({"local", a + "}"});
        c.push_back({"local", a + ",\"player\":99}"});
        c.push_back({"player1", a + ",\"player\":1}"});
        c.push_back({"player1", a + ",\"player\":\"PLAYER1\",\"ack\":9}"});
    }
    return c;
}

void reset(const std::string &state) {
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    std::memset(&kfx_game_state, 0, sizeof(kfx_game_state));
    std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
    api_clear_all_subscriptions();
    if (state != "none")
        kfx_sim_state.game_kind = GKind_LocalGame;
    if (state == "paused")
        kfx_sim_state.operation_flags |= GOF_Paused;
    net_clear_external_seats();
    agent_memory_clear_all();
    if (state == "player1")
        kfx_sim_state.players[1].allocflags |= PlaF_Allocated;
    if (state == "seat1")
        REQUIRE(net_add_external_seat(1) == 1);
}

/** The reply bytes api_process_buffer sends for one request. */
std::string reply_to(const std::string &request) {
    int sv[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0);
    api_process_message_for_test(request.c_str(), request.size() + 1, sv[0]);
    std::string out;
    char buf[4096];
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

/** The reply with the product version replaced by a placeholder. */
std::string without_version(std::string s) {
    const std::string version = VER_STRING;
    for (size_t p = s.find(version); p != std::string::npos; p = s.find(version, p))
        s.replace(p, version.size(), "<version>");
    return s;
}

std::string quoted(const std::string &s) {
    std::string q;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') q += '\\';
        q += ch;
    }
    return q;
}

} // namespace

TEST_CASE("golden: what the API answers to each request", "[kfx_script][api_golden]")
{
    const std::map<std::string, uint64_t> expected = {
#include "api_request_golden.inc"
    };
    const bool print = std::getenv("KFX_SCRIPT_GOLDEN_PRINT") != nullptr;
    TbClockMSec (*saved_clock)(void) = LbTimerClock;
    LbTimerClock = fixed_clock;
    int64_t checked = 0;
    int64_t replied = 0;
    for (const Case &c : cases()) {
        reset(c.state);
        const std::string reply = without_version(reply_to(c.request));
        replied += !reply.empty();
        Fnv h;
        h.add(reply.data(), reply.size());
        const std::string name = std::string(c.state) + ":" + (c.request.size() > 80 ? c.request.substr(0, 60) + "..." : c.request);
        if (print) {
            std::printf("    {\"%s\", 0x%016llxULL}, // %s\n", quoted(name).c_str(), (unsigned long long)h.v,
                reply.substr(0, reply.find('\n')).substr(0, 120).c_str());
            continue;
        }
        auto it = expected.find(name);
        INFO(name << " -> " << reply);
        REQUIRE(it != expected.end());
        CHECK(it->second == h.v);
        checked++;
    }
    reset("none");
    LbTimerClock = saved_clock;
    // Every request gets an answer, or the hashes prove little.
    CHECK(replied == (int64_t)cases().size());
    if (!print)
        CHECK(checked == (int64_t)expected.size());
}
