// Security pass S05 (docs/security-pass/README.md): a chat message names its sender inside the message, so the
// receiver has to check that the peer it arrived from may say so (net_exchange_common.h).
#include <catch2/catch_test_macros.hpp>

#include "net_exchange_common.h"
#include "net_main.h"
#include "net_resync.h"
#include "kfx_config_state.h"
#include "state_versions.h"

#include <cstring>
#include <vector>

TEST_CASE("a peer can only chat as itself", "[kfx_net][security]") {
    const int64_t users = MAX_NET_USERS;
    for (NetUserId peer = 1; peer < users; peer++) {
        INFO(peer);
        CHECK(network_chat_sender_valid(peer, peer, users));
        for (NetUserId other = 0; other < users; other++)
            if (other != peer)
                CHECK_FALSE(network_chat_sender_valid(peer, other, users));
    }
}

TEST_CASE("the host relays for the others, so it may name any user the game has", "[kfx_net][security]") {
    const int64_t users = MAX_NET_USERS;
    for (NetUserId sender = 0; sender < users; sender++)
        CHECK(network_chat_sender_valid(SERVER_ID, sender, users));
}

TEST_CASE("a sender that is not a user of the game is refused, from anyone", "[kfx_net][security]") {
    const int64_t users = MAX_NET_USERS;
    for (int64_t sender : {(int64_t)-1, (int64_t)-56, users, users + 1, (int64_t)200, (int64_t)255, (int64_t)INT32_MAX}) {
        INFO(sender);
        CHECK_FALSE(network_chat_sender_valid(SERVER_ID, sender, users));
        CHECK_FALSE(network_chat_sender_valid(1, sender, users));
    }
    // fewer users in this game than the maximum
    CHECK_FALSE(network_chat_sender_valid(SERVER_ID, 2, 2));
    CHECK(network_chat_sender_valid(SERVER_ID, 1, 2));
}

TEST_CASE("a resync can't ask a client to allocate more than a resync holds", "[kfx_net][security]") {
    const size_t cap = resync_max_original_length();
    // at least every blob it is made of, and nothing near what a few kilobytes of zlib could claim
    CHECK(cap >= (size_t)KFX_SIM_STATE_SIZE + KFX_NET_STATE_SIZE + KFX_GAME_STATE_SIZE + KFX_FRONTEND_STATE_SIZE + KFX_CONFIG_STATE_SIZE);
    CHECK(cap < (size_t)1024 * 1024 * 1024);
}

// Security pass S06: the simulation state a host sends in a resync is checked before it replaces ours.
#include "kfx_sim_state.h"
#include "thing_list.h"
#include <cstring>
#include <vector>

TEST_CASE("a received simulation state is accepted only if its links stay inside it", "[kfx_net][security]") {
    std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
    // a buffer that starts 4 bytes into an allocation: the struct's alignment is not guaranteed on the wire
    std::vector<char> raw(sizeof(kfx_sim_state) + 8);
    char *wire = raw.data() + 4;
    std::memcpy(wire, &kfx_sim_state, sizeof(kfx_sim_state));
    CHECK(resync_sim_state_acceptable(wire));

    struct KfxSimState *state = reinterpret_cast<struct KfxSimState *>(raw.data() + 8); // aligned for the other path
    std::memmove(raw.data() + 8, wire, sizeof(kfx_sim_state));
    raw.resize(sizeof(kfx_sim_state) + 8);
    CHECK(resync_sim_state_acceptable(reinterpret_cast<const char *>(state)));

    // a thing whose neighbour on the map is far outside the thing array
    const size_t thing_at = offsetof(struct KfxSimState, things_data) + 9 * sizeof(struct Thing);
    struct Thing bad;
    std::memset(&bad, 0, sizeof(bad));
    bad.alloc_flags = TAlF_Exists;
    bad.index = 9;
    bad.class_id = TCls_Creature;
    bad.next_on_mapblk = 1 << 28;
    std::memcpy(wire + thing_at, &bad, sizeof(bad));
    CHECK_FALSE(resync_sim_state_acceptable(wire));                                        // not aligned
    std::memcpy(reinterpret_cast<char *>(state) + thing_at, &bad, sizeof(bad));
    CHECK_FALSE(resync_sim_state_acceptable(reinterpret_cast<const char *>(state)));      // aligned
}

TEST_CASE("a resync carrying a configuration that names a function outside its table is refused", "[kfx_net][security]") {
    std::memset(&kfx_config_state, 0, sizeof(kfx_config_state));
    std::vector<char> raw(KFX_CONFIG_STATE_SAVED_LEN + 4);
    char *wire = raw.data() + 1; // not aligned for the struct
    std::memcpy(wire, &kfx_config_state, KFX_CONFIG_STATE_SAVED_LEN);
    CHECK(resync_config_state_acceptable(wire));

    std::vector<char> bad(sizeof(struct KfxConfigState)); // 11 MB: not on the stack
    reinterpret_cast<struct KfxConfigState *>(bad.data())->conf.crtr_conf.states[2].process_state = 1 << 20;
    std::memcpy(wire, bad.data(), KFX_CONFIG_STATE_SAVED_LEN);
    CHECK_FALSE(resync_config_state_acceptable(wire));
}
