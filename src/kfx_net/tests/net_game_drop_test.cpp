// kfx_net: the "who still counts as an opponent" rule behind multiplayer disconnect
// victories (upstream #5300, "improved handling for users dropping").
//  * user_present(): a user exists to the game layer while the game still maps them to a
//    player -- even if their connection has already dropped -- so the game hears about the
//    drop through the proper channel first (determinism).
//  * player_has_enemies_to_defeat(): what check_players_won() and the disconnect-victory
//    resolution ask; a human enemy only counts while user_present(), an enemy with no
//    heart never counts.
#include <catch2/catch_test_macros.hpp>

#include "net_game.h"
#include "player_data.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "kfx_sim_state.h"
#include "kfx_net_state.h"
#include "kfx_config_state.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {
struct DropFixture {
    DropFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_net_state, 0, sizeof(kfx_net_state));
        kfx_config_state.neutral_player_num = PLAYER_NEUTRAL; // zeroed state would make player 0 the neutral one
        std::memset(net_user_info, 0, sizeof(net_user_info));
        kfx_sim_state.system_flags |= GSF_NetworkActive;
        net_user_info[SERVER_ID].network_user_active = 1;
        setup_network_player_numbers(); // host -> player 0
        for (int64_t i = 0; i < PLAYERS_COUNT; i++) {
            kfx_sim_state.players[i].id_number = (PlayerNumber)i;
            kfx_sim_state.players[i].user_id = -1;
        }
        kfx_sim_state.players[0].user_id = SERVER_ID;
        make_player(0);
    }
    void make_player(int64_t i) {
        struct PlayerInfo *player = &kfx_sim_state.players[i];
        player->allocflags |= PlaF_Allocated;
        player->is_active = 1;
        player->victory_state = VicS_Undecided;
        // a living heart: player_cannot_win() is false while the soul container exists
        struct Thing *heart = thing_get(10 + i);
        heart->index = (ThingIndex)(10 + i);
        heart->class_id = TCls_Object;
        heart->alloc_flags |= TAlF_Exists;
        get_dungeon(i)->dnheart_idx = (ThingIndex)(10 + i);
    }
    // A connected, human-driven opponent (user i controls player i).
    void make_human_enemy(int64_t i) {
        make_player(i);
        kfx_sim_state.players[i].user_id = i;
        net_user_info[i].network_user_active = 1;
        setup_network_player_numbers();
    }
};
}

TEST_CASE_METHOD(DropFixture, "user_present is false for an unmapped user and true once a player is mapped", "[kfx_net][net_game]") {
    CHECK_FALSE(user_present(1));
    make_human_enemy(1);
    CHECK(user_present(1));
    CHECK_FALSE(user_present(-1));
    CHECK_FALSE(user_present(MAX_NET_USERS));
}

TEST_CASE_METHOD(DropFixture, "user_present is false when no network game is active", "[kfx_net][net_game]") {
    make_human_enemy(1);
    kfx_sim_state.system_flags &= ~GSF_NetworkActive;
    CHECK_FALSE(user_present(1));
}

TEST_CASE_METHOD(DropFixture, "player_has_enemies_to_defeat is true while a connected human enemy has a heart", "[kfx_net][net_game]") {
    make_human_enemy(1);
    CHECK(player_has_enemies_to_defeat(&kfx_sim_state.players[0]));
    CHECK(player_has_enemies_to_defeat(&kfx_sim_state.players[1])); // symmetric
}

TEST_CASE_METHOD(DropFixture, "player_has_enemies_to_defeat is false when the only enemy has no heart", "[kfx_net][net_game]") {
    make_human_enemy(1);
    get_dungeon(1)->dnheart_idx = 0;
    CHECK_FALSE(player_has_enemies_to_defeat(&kfx_sim_state.players[0]));
}

TEST_CASE_METHOD(DropFixture, "player_has_enemies_to_defeat is false when the human enemy is no longer bound to a user", "[kfx_net][net_game]") {
    make_human_enemy(1);
    kfx_sim_state.players[1].user_id = -1; // what remove_user_from_game() does on a drop
    net_user_info[1].network_user_active = 0;
    setup_network_player_numbers();
    CHECK_FALSE(player_has_enemies_to_defeat(&kfx_sim_state.players[0]));
}

TEST_CASE_METHOD(DropFixture, "player_has_enemies_to_defeat ignores a player who has already lost", "[kfx_net][net_game]") {
    make_human_enemy(1);
    kfx_sim_state.players[1].victory_state = VicS_LostLevel;
    CHECK_FALSE(player_has_enemies_to_defeat(&kfx_sim_state.players[0]));
}

// Upstream #5317 (multiplayer -packetload): a replay header records which player each
// network user controlled; restore_users_from_packet_save() rebuilds that user<->player
// mapping (and each user's name) so the replayed packets reach the right players.
TEST_CASE_METHOD(DropFixture, "restore_users_from_packet_save maps recorded users to their players", "[kfx_net][net_game][replay]") {
    std::memset(&kfx_net_state.packet_save_head, 0, sizeof(kfx_net_state.packet_save_head));
    std::memset(kfx_net_state.packet_save_head.user_players, -1, sizeof(kfx_net_state.packet_save_head.user_players));
    kfx_net_state.packet_save_head.players_exist = (1 << 0) | (1 << 2);
    kfx_net_state.packet_save_head.user_players[1] = 2; // user 1 drove player 2
    kfx_net_state.packet_save_head.user_players[0] = 0; // the host drove player 0
    std::snprintf(kfx_net_state.packet_save_head.user_names[1], sizeof(kfx_net_state.packet_save_head.user_names[1]), "Guest");
    my_player_number = 0;

    restore_users_from_packet_save();

    CHECK(get_net_user_player_number(1) == 2);
    CHECK(get_net_user_player_number(0) == 0);
    CHECK(kfx_sim_state.players[2].user_id == 1);
    CHECK(std::string(kfx_sim_state.players[2].player_name) == "Guest");
}

TEST_CASE_METHOD(DropFixture, "restore_users_from_packet_save ignores a user mapped to a player the file says did not exist", "[kfx_net][net_game][replay]") {
    std::memset(&kfx_net_state.packet_save_head, 0, sizeof(kfx_net_state.packet_save_head));
    std::memset(kfx_net_state.packet_save_head.user_players, -1, sizeof(kfx_net_state.packet_save_head.user_players));
    kfx_net_state.packet_save_head.players_exist = (1 << 0);
    kfx_net_state.packet_save_head.user_players[0] = 0;
    kfx_net_state.packet_save_head.user_players[3] = 4; // player 4 not in players_exist
    my_player_number = 0;

    restore_users_from_packet_save();

    CHECK(get_net_user_player_number(0) == 0);
    CHECK(get_net_user_player_number(3) == -1);
}
