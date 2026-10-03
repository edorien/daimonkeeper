// kfx_render: engine_redraw.c's update_mouse_light() -- targeted per the
// user's request to focus coverage around scripts/check_layering_symbols.py's
// ACCEPTED (architecture.md §8.2) kfx_render -> kfx_net residual:
// get_packet(player->user_id), called here as the fallback when
// netport_get_history_packet() (default no-op) returns NULL --
// which it always does without a fake, so this function's real behavior
// is only reachable through the get_packet accepted-residual call,
// every time, by default.
//
// set_mouse_light() (the static helper update_mouse_light calls into)
// no-ops immediately if the player's UserState::cursor_light_idx is 0 --
// the first test
// below. With a real allocated light, the valid/invalid branches
// (driven by pckt->control_flags & PCtr_MapCoordsValid, read straight
// from the packet the residual stub returns) call light_turn_light_on/
// _off, observable directly via kfx_sim_state.light_registry.lights[idx].flags -- same array
// light_registry_test.cpp (kfx_sim) already exercises.
#include <catch2/catch_test_macros.hpp>

#include "engine_redraw.h"
#include "light_data.h"
#include "player_data.h"
#include "kfx_sim_state.h"
#include "kfx_render_state.h"
#include "packet_data.h"

#include <cstring>

namespace {
struct ResetRedrawState {
    ResetRedrawState() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&kfx_render_state, 0, sizeof(kfx_render_state));
        light_initialise();
        get_packet(0)->control_flags = 0;
        get_packet(0)->pos_x = 0;
        get_packet(0)->pos_y = 0;
    }
};
}

TEST_CASE_METHOD(ResetRedrawState, "update_mouse_light is a no-op when the user has no cursor light allocated", "[kfx_render][engine_redraw]") {
    get_user_state(0)->cursor_light_idx = 0; // nothing allocated
    update_mouse_light(0); // must not crash; nothing to observe, by design
}

TEST_CASE_METHOD(ResetRedrawState, "update_mouse_light turns the cursor light on and positions it when the packet reports valid map coords", "[kfx_render][engine_redraw]") {
    struct Light *lgt = light_allocate_light();
    REQUIRE(lgt != nullptr);

    get_user_state(0)->cursor_light_idx = lgt->index;

    struct Packet *pckt = get_packet(0);
    pckt->control_flags = PCtr_MapCoordsValid;
    pckt->pos_x = 2560; // 10 subtiles * COORD_PER_STL(256)
    pckt->pos_y = 5120; // 20 subtiles

    CHECK_FALSE(kfx_sim_state.light_registry.lights[lgt->index].flags & LgtF_CanTurnOff); // off before

    update_mouse_light(0);

    CHECK((kfx_sim_state.light_registry.lights[lgt->index].flags & LgtF_CanTurnOff) != 0); // light_turn_light_on ran
    CHECK(kfx_sim_state.light_registry.lights[lgt->index].mappos.x.val == 2560);
    CHECK(kfx_sim_state.light_registry.lights[lgt->index].mappos.y.val == 5120);
}

TEST_CASE_METHOD(ResetRedrawState, "update_mouse_light leaves the cursor light off when the packet reports invalid map coords", "[kfx_render][engine_redraw]") {
    struct Light *lgt = light_allocate_light();
    REQUIRE(lgt != nullptr);

    get_user_state(0)->cursor_light_idx = lgt->index;

    get_packet(0)->control_flags = 0; // PCtr_MapCoordsValid not set

    update_mouse_light(0);

    // light_turn_light_off() itself no-ops unless CanTurnOff was already
    // set (a freshly allocated light never has it) -- so the observable
    // contract here is simply "still off", the same state as before the
    // call, confirming the invalid branch never reaches light_turn_light_on.
    CHECK_FALSE(kfx_sim_state.light_registry.lights[lgt->index].flags & LgtF_CanTurnOff);
}
