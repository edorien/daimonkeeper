// kfx_net: the "who still counts as an opponent" rule behind multiplayer disconnect
// victories (upstream #5300, "improved handling for users dropping").
//  * user_present(): a user exists to the game layer while the game still maps them to a
//    player -- even if their connection has already dropped -- so the game hears about the
//    drop through the proper channel first (determinism).
//  * victory_candidates_fully_allied(): what check_players_won() and the disconnect-victory
//    resolution ask (see the [victory] cases below).
#include <catch2/catch_test_macros.hpp>

#include "net_game.h"
#include "player_data.h"
#include "player_utils.h"
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

// Upstream #5350 replaced player_has_enemies_to_defeat() with victory_candidates_fully_allied()
// (kfx_sim): the game goes on while two unallied players can both still plausibly win.
TEST_CASE_METHOD(DropFixture, "victory_candidates_fully_allied is false while two unallied humans have hearts", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    CHECK_FALSE(victory_candidates_fully_allied(false));
    CHECK_FALSE(victory_candidates_fully_allied(true));
}

TEST_CASE_METHOD(DropFixture, "victory_candidates_fully_allied is true when the only enemy has no heart", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    get_dungeon(1)->dnheart_idx = 0;
    CHECK(victory_candidates_fully_allied(false));
}

TEST_CASE_METHOD(DropFixture, "victory_candidates_fully_allied ignores a player who has already lost", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    kfx_sim_state.players[1].victory_state = VicS_LostLevel;
    CHECK(victory_candidates_fully_allied(false));
}

TEST_CASE_METHOD(DropFixture, "victory_candidates_fully_allied is true when the remaining players are mutual allies", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    kfx_sim_state.players[0].allied_players |= to_flag(1);
    kfx_sim_state.players[1].allied_players |= to_flag(0);
    CHECK(victory_candidates_fully_allied(false));
    // a one-sided alliance proposal does not count
    kfx_sim_state.players[1].allied_players &= ~to_flag(0);
    CHECK_FALSE(victory_candidates_fully_allied(false));
}

TEST_CASE_METHOD(DropFixture, "humans_only ignores computer players; a dropped human's AI stand-in is never a candidate", "[kfx_net][net_game][victory]") {
    make_player(1);
    kfx_sim_state.players[1].allocflags |= PlaF_CompCtrl;
    CHECK_FALSE(victory_candidates_fully_allied(false)); // an unallied computer keeper still counts
    CHECK(victory_candidates_fully_allied(true));
    kfx_sim_state.players[1].allocflags |= PlaF_OriginallyHuman; // AI took over a dropped human
    CHECK_FALSE(player_is_victory_candidate(&kfx_sim_state.players[1]));
    CHECK(victory_candidates_fully_allied(false));
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
