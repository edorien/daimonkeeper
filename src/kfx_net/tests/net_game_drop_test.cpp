// kfx_net: who still counts once a multiplayer user drops (upstream #5300, "improved
// handling for users dropping"; victory rules reworked by #5365).
//  * user_present(): a user exists to the game layer while the game still maps them to a
//    player -- even if their connection has already dropped -- so the game hears about the
//    drop through the proper channel first (determinism).
//  * human_victory_kernel_exists() / player_defeat_settled() / resolve_placeholders() (kfx_sim):
//    what ALL_DUNGEONS_DESTROYED and a dropped human's AI placeholder go by (the [victory] cases).
#include <catch2/catch_test_macros.hpp>

#include "net_game.h"
#include "packets.h"
#include "player_data.h"
#include "player_utils.h"
#include "dungeon_data.h"
#include "thing_data.h"
#include "thing_objects.h"
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
        local_system_flags |= GSF_NetworkActive;
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
    local_system_flags &= ~GSF_NetworkActive;
    CHECK_FALSE(user_present(1));
}

// Upstream #5365 ("Remove all non-scripted victory conditions") replaced
// victory_candidates_fully_allied() with human_victory_kernel_exists() (kfx_sim): the game
// goes on while two unallied, undefeated humans remain -- computer keepers never count.
TEST_CASE_METHOD(DropFixture, "human_victory_kernel_exists is false while two unallied humans are undefeated", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    CHECK_FALSE(human_victory_kernel_exists());
}

TEST_CASE_METHOD(DropFixture, "human_victory_kernel_exists goes by victory state, not by the heart", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    get_dungeon(1)->dnheart_idx = 0; // heartless but undecided: still in the kernel
    CHECK_FALSE(human_victory_kernel_exists());
    kfx_sim_state.players[1].victory_state = VicS_LostLevel;
    CHECK(human_victory_kernel_exists());
}

TEST_CASE_METHOD(DropFixture, "human_victory_kernel_exists is true when the remaining humans are mutual allies", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    kfx_sim_state.players[0].allied_players |= to_flag(1);
    kfx_sim_state.players[1].allied_players |= to_flag(0);
    CHECK(human_victory_kernel_exists());
    // a one-sided alliance proposal does not count
    kfx_sim_state.players[1].allied_players &= ~to_flag(0);
    CHECK_FALSE(human_victory_kernel_exists());
}

TEST_CASE_METHOD(DropFixture, "human_victory_kernel_exists ignores computer keepers, placeholders included", "[kfx_net][net_game][victory]") {
    make_player(1);
    kfx_sim_state.players[1].allocflags |= PlaF_CompCtrl;
    CHECK(human_victory_kernel_exists());
    kfx_sim_state.players[1].allocflags |= PlaF_Placeholder; // AI took over a dropped human
    CHECK(player_is_placeholder(&kfx_sim_state.players[1]));
    CHECK(human_victory_kernel_exists());
}

TEST_CASE_METHOD(DropFixture, "player_defeat_settled waits for the heart to finish exploding", "[kfx_net][net_game][victory]") {
    make_human_enemy(1);
    CHECK_FALSE(player_defeat_settled(1)); // undecided
    kfx_sim_state.players[1].victory_state = VicS_LostLevel;
    thing_get(11)->active_state = ObSt_BeingDestroyed;
    CHECK_FALSE(player_defeat_settled(1));
    thing_get(11)->active_state = 0;
    CHECK(player_defeat_settled(1));
}

TEST_CASE_METHOD(DropFixture, "resolve_placeholders defeats and frees a placeholder with no human ally left", "[kfx_net][net_game][victory]") {
    make_player(1);
    kfx_sim_state.players[1].allocflags |= PlaF_CompCtrl | PlaF_Placeholder;

    resolve_placeholders();

    CHECK(kfx_sim_state.players[1].victory_state == VicS_LostLevel);
    CHECK_FALSE(player_exists(&kfx_sim_state.players[1]));
}

TEST_CASE_METHOD(DropFixture, "resolve_placeholders keeps a placeholder that a human still allies with", "[kfx_net][net_game][victory]") {
    make_player(1);
    kfx_sim_state.players[1].allocflags |= PlaF_CompCtrl | PlaF_Placeholder;
    kfx_sim_state.players[0].allied_players |= to_flag(1);
    kfx_sim_state.players[1].allied_players |= to_flag(0);

    resolve_placeholders();

    CHECK(kfx_sim_state.players[1].victory_state == VicS_Undecided);
    CHECK(player_exists(&kfx_sim_state.players[1]));
}

TEST_CASE_METHOD(DropFixture, "resolve_placeholders leaves ordinary computer keepers alone", "[kfx_net][net_game][victory]") {
    make_player(1);
    kfx_sim_state.players[1].allocflags |= PlaF_CompCtrl;

    resolve_placeholders();

    CHECK(kfx_sim_state.players[1].victory_state == VicS_Undecided);
    CHECK(player_exists(&kfx_sim_state.players[1]));
}

// Upstream #5376: each user's start settings (network startup sync, replay header) go to its own player.
// Cheats and skipping the heart zoom need the host to allow them too.
TEST_CASE_METHOD(DropFixture, "apply_user_start_settings gives a player its own zoom, highlight and tendencies; cheats need the host's too", "[kfx_net][net_game]") {
    struct PlayerInfo *player = &kfx_sim_state.players[1];
    player->id_number = 1;
    struct UserStartSettings us;
    std::memset(&us, 0, sizeof(us));
    us.video_rotate_mode = 2; // front view
    us.isometric_view_zoom_level = 5000;
    us.frontview_zoom_level = 6000;
    us.zoom_distance = 3000;
    us.frontview_zoom_distance = 7000;
    us.highlight_mode = 1;
    us.tendencies = CrTend_Flee;
    us.flags = USF_CheatsEnabled | USF_SkipHeartZoom;
    struct UserStartSettings host;
    std::memset(&host, 0, sizeof(host));

    apply_user_start_settings(player, &us, &host);
    CHECK(player->view_mode_restore == PVM_FrontView);
    CHECK(player->isometric_view_zoom_level == 5000);
    CHECK(player->frontview_zoom_level == 6000);
    CHECK(player->zoom_distance == 3000);
    CHECK(player->frontview_zoom_distance == 7000);
    CHECK(player->highlight_mode == 1);
    CHECK_FALSE(player->cheats_allowed); // the host didn't allow cheats
    CHECK_FALSE(player->skip_heart_zoom);

    host.flags = USF_CheatsEnabled | USF_SkipHeartZoom;
    apply_user_start_settings(player, &us, &host);
    CHECK(player->cheats_allowed);
    CHECK(player->skip_heart_zoom);
}
