// kfx_render: local_camera.c's update_local_cameras() -- the prediction of the
// authoritative packet camera. Everything it reaches outside kfx_render goes through
// render_overlay callbacks, so the recorded calls below are enough to pin the rule:
// a parchment-map jump (PckA_ZoomFromMap) moves the camera to the clicked location
// and ignores that packet's camera controls, exactly as the packet camera in
// kfx_net's process_camera_action() does (controls first, then the jump overwrites
// them). Applying controls locally AFTER the jump made the predicted camera drift
// off the authoritative one by one tick of scrolling (upstream #5315).
#include <catch2/catch_test_macros.hpp>

#include "local_camera.h"
#include "render_overlay.h"
#include "player_data.h"
#include "packet_data.h"
#include "kfx_sim_state.h"

#include <cstring>

namespace {
struct CameraCallbackSpy {
    static inline int controls_calls;
    static inline int action_calls;
    static inline struct Packet packet;

    static const struct Packet *history_packet(NetUserId, GameTurn) { return &packet; }
    static void camera_action(struct Camera *, const struct Packet *) { action_calls++; }
    static void camera_controls(struct Camera *, const struct Packet *, struct PlayerInfo *, TbBool) { controls_calls++; }

    struct RenderOverlayCallbacks callbacks;
    CameraCallbackSpy() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&packet, 0, sizeof(packet));
        controls_calls = 0;
        action_calls = 0;
        callbacks = *render_overlay; // keep every other default no-op
        callbacks.get_history_packet = &CameraCallbackSpy::history_packet;
        callbacks.process_camera_action = &CameraCallbackSpy::camera_action;
        callbacks.process_camera_controls = &CameraCallbackSpy::camera_controls;
        set_render_overlay_callbacks(&callbacks);

        my_player_number = 0;
        struct PlayerInfo *player = &kfx_sim_state.players[0];
        player->id_number = 0;
        player->user_id = 0;
        init_local_cameras(player);
    }
    ~CameraCallbackSpy() { set_render_overlay_callbacks(nullptr); }
};
}

TEST_CASE_METHOD(CameraCallbackSpy, "update_local_cameras applies the packet's camera controls on a normal packet", "[kfx_render][local_camera]") {
    packet.action = PckA_None;
    update_local_cameras();
    CHECK(action_calls == 1);
    CHECK(controls_calls == 1);
}

TEST_CASE_METHOD(CameraCallbackSpy, "update_local_cameras ignores the packet's camera controls on a parchment jump (PckA_ZoomFromMap)", "[kfx_render][local_camera]") {
    packet.action = PckA_ZoomFromMap;
    update_local_cameras();
    CHECK(action_calls == 1);   // the jump itself is still applied
    CHECK(controls_calls == 0); // but nothing is scrolled on top of it
}
