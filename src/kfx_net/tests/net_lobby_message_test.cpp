// Security pass S08: the lobby messages a host sends. A login or user-update message names a user by one byte, and the
// length of the message is the host's too: a negative or large number indexed netstate.users[] (found by the network
// fuzzer), a short message was read past its end, and two malformed ones ended the game with abort().
#include <catch2/catch_test_macros.hpp>

#include "net_lobby.h"
#include "net_main.h"

#include <cstring>

namespace {
struct LobbyFixture {
    LobbyFixture()
    {
        std::memset(&netstate, 0, sizeof(netstate));
        netstate.max_users = MAX_NET_USERS;
        netstate.my_id = 2;
        for (int u = 0; u < MAX_NET_USERS; u++)
            netstate.users[u].id = u;
    }
};
}

TEST_CASE_METHOD(LobbyFixture, "a login from the host that assigns a user outside the game is ignored", "[kfx_net][net_lobby][security]") {
    char msg[1 + sizeof(struct GameVersionPacket)] = {0};
    for (int value : {4, 5, 100, 127, 128, 200, 255})
    {
        INFO(value);
        msg[0] = (char)value;
        CHECK(process_login_message(SERVER_ID, msg, msg + sizeof(msg)) == Lb_OK);
        CHECK(netstate.my_id == 2); // unchanged
    }
    msg[0] = 1;
    CHECK(process_login_message(SERVER_ID, msg, msg + sizeof(msg)) == Lb_OK);
    CHECK(netstate.my_id == 1);
}

TEST_CASE_METHOD(LobbyFixture, "a login from the host that is too short to hold what it says is ignored", "[kfx_net][net_lobby][security]") {
    char msg[1 + sizeof(struct GameVersionPacket)] = {0};
    msg[0] = 3;
    CHECK(process_login_message(SERVER_ID, msg, msg + 1) == Lb_OK);
    CHECK(process_login_message(SERVER_ID, msg, msg + sizeof(msg) - 1) == Lb_OK);
    CHECK(process_login_message(SERVER_ID, msg, msg) == Lb_OK);
    CHECK(netstate.my_id == 2);
}

TEST_CASE_METHOD(LobbyFixture, "a login from a source that is not a user does nothing", "[kfx_net][net_lobby][security]") {
    char msg[64] = {0};
    CHECK(process_login_message(-3, msg, msg + sizeof(msg)) == Lb_OK);
    CHECK(process_login_message(77, msg, msg + sizeof(msg)) == Lb_OK);
}

TEST_CASE_METHOD(LobbyFixture, "a user update from the host that is malformed is ignored, not fatal", "[kfx_net][net_lobby][security]") {
    // (the message lives in the network layer's buffer, as the name reader expects)
    char *msg = netstate.msg_buffer;
    const size_t kLen = 64;
    std::memset(msg, 0, kLen);
    // too short; a user outside the game; a stage that does not exist; a name that never ends
    CHECK(process_user_update_message(SERVER_ID, msg, msg + 1) == Lb_OK);
    msg[0] = 9;
    CHECK(process_user_update_message(SERVER_ID, msg, msg + kLen) == Lb_OK);
    msg[0] = 1;
    msg[1] = 77;
    CHECK(process_user_update_message(SERVER_ID, msg, msg + kLen) == Lb_OK);
    msg[1] = USER_LOGGEDIN;
    std::memset(msg + 2, 'a', sizeof(netstate.msg_buffer) - 2); // no end anywhere in the buffer
    CHECK(process_user_update_message(SERVER_ID, msg, msg + kLen) == Lb_OK);
    CHECK(netstate.users[1].progress != 77);
}
