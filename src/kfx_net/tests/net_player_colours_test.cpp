// kfx_net: player colours chosen in the lobby (upstream #5439, "set your player colour in the lobby").
//  * The host packs every user's colour into its ScreenPacket.stored_data1; clients unpack it, and a value no colour
//    packs to (a host of another build, or a forged packet) reads as no user.
//  * At game start the host resolves the colours to player slots: a slot chosen once is kept, random and
//    out-of-map choices take the free slots, more users than the map has slots fails; the lobby's alliances move from
//    users to their slots.
//  * Every peer checks the host's slots before using them (setup_network_player_slots).
#include <catch2/catch_test_macros.hpp>

#include "net_lobby.h"
#include "net_main.h"
#include "net_game.h"
#include "player_data.h"
#include "kfx_sim_state.h"

#include <cstring>
#include <set>

namespace {
const int8_t R = NET_PLAYER_COLOUR_RANDOM;
const int8_t N = NET_PLAYER_COLOUR_NONE;

struct ColourFixture {
    unsigned char saved_flags = local_system_flags;
    struct TbNetworkUserInfo saved_users[MAX_NET_USERS];
    NetUserId saved_my_id = netstate.my_id;
    int64_t saved_max_users = netstate.max_users;
    PlayerNumber saved_my_player = my_player_number;
    ColourFixture()
    {
        std::memcpy(saved_users, net_user_info, sizeof(net_user_info));
        std::memset(net_user_info, 0, sizeof(net_user_info));
        local_system_flags |= GSF_NetworkActive;
        netstate.my_id = SERVER_ID;
        net_player_colours_reset();
    }
    ~ColourFixture()
    {
        std::memcpy(net_user_info, saved_users, sizeof(net_user_info));
        local_system_flags = saved_flags;
        netstate.my_id = saved_my_id;
        netstate.max_users = saved_max_users;
        my_player_number = saved_my_player;
        net_player_colours_reset();
        setup_network_player_numbers();
    }
    void activate(std::initializer_list<int> users)
    {
        for (int u : users)
            net_user_info[u].network_user_active = 1;
    }
};

char lobby_ally(NetUserId a, NetUserId b) { return (char)alliance_grid[a][b]; }
}

TEST_CASE("lobby colours survive the host's packet", "[kfx_net][net_lobby][colours]") {
    const int8_t cases[][MAX_NET_USERS] = {
        {N, N, N, N}, {0, 1, 2, 3}, {3, 2, 1, 0}, {R, R, R, R}, {0, N, R, 3}, {2, 2, N, 1},
    };
    for (const auto &colours : cases)
    {
        int8_t back[MAX_NET_USERS];
        net_unpack_player_colours(net_pack_player_colours(colours), back);
        CHECK(std::memcmp(colours, back, sizeof(back)) == 0);
    }
}

TEST_CASE("a packed colour value no colour packs to reads as no user", "[kfx_net][net_lobby][colours][security]") {
    for (int nibble = 0; nibble < 16; nibble++)
    {
        INFO(nibble);
        int8_t back[MAX_NET_USERS];
        const uint16_t packed = (uint16_t)(nibble | (nibble << 4) | (nibble << 8) | (nibble << 12));
        net_unpack_player_colours((int16_t)packed, back);
        for (int u = 0; u < MAX_NET_USERS; u++)
        {
            CHECK(back[u] >= N);
            CHECK(back[u] <= R);
            if (nibble > R + 1)
                CHECK(back[u] == N);
            else
                CHECK(back[u] == nibble - 1);
        }
    }
    // out-of-range colours are never packed as something else
    const int8_t bad[MAX_NET_USERS] = {-5, 17, 0, 127};
    int8_t back[MAX_NET_USERS];
    net_unpack_player_colours(net_pack_player_colours(bad), back);
    CHECK(back[0] == N);
    CHECK(back[1] == N);
    CHECK(back[2] == 0);
    CHECK(back[3] == N);
}

TEST_CASE_METHOD(ColourFixture, "the host only accepts a slot or random as a requested colour", "[kfx_net][net_lobby][colours][security]") {
    CHECK(net_host_set_player_colour(1, 2));
    CHECK(net_player_colour[1] == 2);
    CHECK(net_host_set_player_colour(1, R));
    CHECK(net_player_colour[1] == R);
    for (int64_t bad : {(int64_t)-1, (int64_t)-32768, (int64_t)R + 1, (int64_t)255, (int64_t)32767})
    {
        INFO(bad);
        CHECK_FALSE(net_host_set_player_colour(1, bad));
        CHECK(net_player_colour[1] == R);
    }
    CHECK_FALSE(net_host_set_player_colour(-1, 0));
    CHECK_FALSE(net_host_set_player_colour(MAX_NET_USERS, 0));
}

TEST_CASE_METHOD(ColourFixture, "the host gives a joining user the first free colour and frees a leaver's", "[kfx_net][net_lobby][colours]") {
    activate({0, 1});
    net_host_update_player_colours();
    CHECK(net_player_colour[0] == 0);
    CHECK(net_player_colour[1] == 1);
    CHECK(net_player_colour[2] == N);
    net_host_set_player_colour(0, 2);
    activate({3});
    net_host_update_player_colours();
    CHECK(net_player_colour[3] == 0);
    net_user_info[1].network_user_active = 0;
    net_host_update_player_colours();
    CHECK(net_player_colour[1] == N);
    netstate.max_users = 3;
    struct ScreenPacket host = {};
    net_lobby_write_host_colours(&host);
    net_player_colours_reset();
    net_lobby_read_host_colours(&host);
    CHECK(net_player_colour[0] == 2);
    CHECK(net_player_colour[1] == N);
    CHECK(net_player_colour[3] == 0);
    CHECK(net_lobby_host_max_users == 3);
    host.stored_data2 = 77; // a host's max users outside the game's range is not taken
    net_lobby_read_host_colours(&host);
    CHECK(net_lobby_host_max_users == 3);
}

TEST_CASE("two users asking for the same slot is a conflict; random is not", "[kfx_net][net_lobby][colours]") {
    const int8_t ok[MAX_NET_USERS] = {0, 1, R, R};
    const int8_t clash[MAX_NET_USERS] = {0, 2, N, 2};
    CHECK_FALSE(net_player_colours_conflict(ok));
    CHECK(net_player_colours_conflict(clash));
}

TEST_CASE("chosen slots are kept and random ones take the free slots", "[kfx_net][net_lobby][colours]") {
    for (uint32_t s = 1; s < 50; s++)
    {
        uint32_t seed = s * 2654435761u;
        const int8_t colours[MAX_NET_USERS] = {3, R, 1, R};
        int8_t slots[MAX_NET_USERS];
        char alliances = 0;
        REQUIRE(net_resolve_player_colours(colours, 4, 0, &seed, slots, &alliances));
        CHECK(slots[0] == 3);
        CHECK(slots[2] == 1);
        CHECK(((slots[1] == 0 && slots[3] == 2) || (slots[1] == 2 && slots[3] == 0)));
        CHECK(alliances == 0);
    }
}

TEST_CASE("random slots are shuffled by the seed", "[kfx_net][net_lobby][colours]") {
    const int8_t colours[MAX_NET_USERS] = {R, R, R, R};
    std::set<int> firsts;
    for (uint32_t s = 1; s < 200; s++)
    {
        uint32_t seed = s * 2654435761u;
        int8_t slots[MAX_NET_USERS];
        char alliances;
        REQUIRE(net_resolve_player_colours(colours, 4, 0, &seed, slots, &alliances));
        std::set<int> used(slots, slots + MAX_NET_USERS);
        CHECK(used == std::set<int>{0, 1, 2, 3});
        firsts.insert(slots[0]);
    }
#ifndef FUNCTESTING // LbRandomSeries keeps its seed fixed under FUNCTESTING
    CHECK(firsts.size() > 1);
#endif
}

TEST_CASE("a conflict keeps the first user's choice and moves the second", "[kfx_net][net_lobby][colours]") {
    uint32_t seed = 12345;
    const int8_t colours[MAX_NET_USERS] = {1, 1, N, N};
    int8_t slots[MAX_NET_USERS];
    char alliances;
    REQUIRE(net_resolve_player_colours(colours, 2, 0, &seed, slots, &alliances));
    CHECK(slots[0] == 1);
    CHECK(slots[1] == 0);
    CHECK(slots[2] == N);
    CHECK(slots[3] == N);
}

TEST_CASE("a choice the map doesn't have becomes a free slot of the map", "[kfx_net][net_lobby][colours]") {
    uint32_t seed = 7;
    const int8_t colours[MAX_NET_USERS] = {3, 0, N, N};
    int8_t slots[MAX_NET_USERS];
    char alliances;
    REQUIRE(net_resolve_player_colours(colours, 2, 0, &seed, slots, &alliances));
    CHECK(slots[0] == 1);
    CHECK(slots[1] == 0);
}

TEST_CASE("three users on a two-player map fail to resolve", "[kfx_net][net_lobby][colours]") {
    uint32_t seed = 99;
    const int8_t colours[MAX_NET_USERS] = {0, 1, 2, N};
    int8_t slots[MAX_NET_USERS];
    char alliances;
    CHECK_FALSE(net_resolve_player_colours(colours, 2, 0, &seed, slots, &alliances));
    CHECK(slots[0] == 0);
    CHECK(slots[1] == 1);
    CHECK(slots[2] == N);
    const int8_t randoms[MAX_NET_USERS] = {R, R, R, N};
    CHECK_FALSE(net_resolve_player_colours(randoms, 2, 0, &seed, slots, &alliances));
}

TEST_CASE("lobby alliances between users become alliances between their slots", "[kfx_net][net_lobby][colours]") {
    uint32_t seed = 1;
    // users 0 and 2 allied, user 1 alone; they play slots 3, 0 and 1
    const int8_t colours[MAX_NET_USERS] = {3, 0, 1, N};
    int8_t slots[MAX_NET_USERS];
    char alliances;
    const char lobby = lobby_ally(0, 2);
    REQUIRE(net_resolve_player_colours(colours, 4, lobby, &seed, slots, &alliances));
    CHECK(alliances == (char)alliance_grid[3][1]);
    // an alliance with a user who isn't playing is dropped
    const char with_absent = lobby_ally(0, 2) | lobby_ally(1, 3);
    REQUIRE(net_resolve_player_colours(colours, 4, with_absent, &seed, slots, &alliances));
    CHECK(alliances == (char)alliance_grid[3][1]);
}

TEST_CASE_METHOD(ColourFixture, "the host's slots are used when every active user has its own", "[kfx_net][net_game][colours]") {
    activate({0, 1, 3});
    netstate.my_id = 3;
    const int8_t slots[MAX_NET_USERS] = {2, 0, N, 1};
    REQUIRE(setup_network_player_slots(slots, 4));
    CHECK(get_net_user_player_number(0) == 2);
    CHECK(get_net_user_player_number(1) == 0);
    CHECK(get_net_user_player_number(2) == -1);
    CHECK(get_net_user_player_number(3) == 1);
    CHECK(my_player_number == 1);
}

TEST_CASE_METHOD(ColourFixture, "host slots that are shared, missing or outside the map are refused", "[kfx_net][net_game][colours][security]") {
    activate({0, 1});
    const int8_t bad[][MAX_NET_USERS] = {
        {0, 0, N, N},    // shared
        {0, N, N, N},    // an active user without a slot
        {0, 2, N, N},    // past a 2-player map
        {0, 4, N, N},    // past every map
        {-7, 1, N, N},   // negative
        {0, 127, N, N},
    };
    for (const auto &slots : bad)
    {
        CHECK_FALSE(setup_network_player_slots(slots, 2));
        CHECK(get_net_user_player_number(0) == -1); // nothing half-applied
        CHECK(get_net_user_player_number(1) == -1);
    }
    // an inactive user's slot doesn't matter
    const int8_t ok[MAX_NET_USERS] = {1, 0, 0, 77};
    CHECK(setup_network_player_slots(ok, 2));
}

TEST_CASE_METHOD(ColourFixture, "setup_network_player_numbers still compacts users in id order", "[kfx_net][net_game][colours]") {
    activate({0, 2});
    setup_network_player_numbers();
    CHECK(get_net_user_player_number(0) == 0);
    CHECK(get_net_user_player_number(2) == 1);
    CHECK(get_net_user_player_number(1) == -1);
}
