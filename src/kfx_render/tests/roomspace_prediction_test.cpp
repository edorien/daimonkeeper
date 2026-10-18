// kfx_render: roomspace_prediction.c, the local dig prediction of a network game with input lag. While it runs, its
// PckA_ApplyRoomspaceDigTag actions are this client's only dig tags, and the packet says so (PCtr_DigTagsAsActions)
// for the game not to tag again from the held button at the packet's lagged cursor (upstream #5466's misclick: a
// power-hand click on a creature that had moved on dug where it had been).
#include <catch2/catch_test_macros.hpp>

#include "roomspace_prediction.h"
#include "packet_data.h"
#include "kfx_sim_state.h"
#include "globals.h"

#include <cstring>

namespace {
struct ResetPredictionState {
    ResetPredictionState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(sim_packets, 0, sizeof(sim_packets));
        replay.load_enable = 0;
        reset_local_dig_prediction();
    }
    ~ResetPredictionState() {
        clear_flag(local_system_flags, GSF_NetworkActive);
        reset_local_dig_prediction();
    }
};
}

TEST_CASE_METHOD(ResetPredictionState, "the local packet says its dig tags come as actions only while the prediction runs", "[kfx_render][roomspace_prediction]") {
    set_flag(local_system_flags, GSF_NetworkActive);
    update_local_dig_tag_prediction(2);
    CHECK((get_local_packet()->control_flags & PCtr_DigTagsAsActions) != 0);

    get_local_packet()->control_flags = 0;
    update_local_dig_tag_prediction(0); // no input lag: no prediction, the game tags from the held button
    CHECK((get_local_packet()->control_flags & PCtr_DigTagsAsActions) == 0);

    clear_flag(local_system_flags, GSF_NetworkActive);
    update_local_dig_tag_prediction(2); // a single-player game
    CHECK((get_local_packet()->control_flags & PCtr_DigTagsAsActions) == 0);

    set_flag(local_system_flags, GSF_NetworkActive);
    replay.load_enable = 1; // a replay plays the recorded packets as they are
    update_local_dig_tag_prediction(2);
    CHECK((get_local_packet()->control_flags & PCtr_DigTagsAsActions) == 0);
    replay.load_enable = 0;
}
