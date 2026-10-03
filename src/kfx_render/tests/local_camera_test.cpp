// kfx_render: local_camera.c's update_local_cameras() -- the prediction of the
// authoritative packet camera. The rule pinned here: a parchment-map jump
// (PckA_ZoomFromMap) moves the camera to the clicked location and ignores that
// packet's camera controls, exactly as the packet camera in kfx_sim's
// process_camera_action() does (controls first, then the jump overwrites them).
// Applying controls locally AFTER the jump made the predicted camera drift off
// the authoritative one by one tick of scrolling (upstream #5315).
//
// The packet camera handlers used to be kfx_net functions reached through
// render_overlay callbacks, and this test spied on those callbacks. Since
// refactor pass 2's S07 they are kfx_sim functions called directly, so the test
// checks where the camera ends up instead. With the delta-time feature off,
// interpolate() returns the destination, so get_local_active_camera() shows it.
#include <catch2/catch_test_macros.hpp>

#include "local_camera.h"
#include "player_data.h"
#include "packet_data.h"
#include "kfx_sim_state.h"
#include "ports/net_port.h"
#include "local_state.h"
#include "player_camera.h"
#include "thing_data.h"

#include <cstring>

namespace {
constexpr MapCoord start_pos = 5000;

struct LocalCameraFixture {
    static inline struct Packet packet;
    static const struct Packet *history_packet(NetUserId, GameTurn) { return &packet; }

    struct NetPort callbacks;
    struct PlayerInfo *player;
    LocalCameraFixture() {
        std::memset(&kfx_sim_state, 0, sizeof(kfx_sim_state));
        std::memset(&local_state, 0, sizeof(local_state));
        std::memset(&packet, 0, sizeof(packet));
        kfx_sim_state.map_subtiles_x = 100;
        kfx_sim_state.map_subtiles_y = 100;
        callbacks = *net_port; // keep every other default no-op
        callbacks.get_history_packet = &LocalCameraFixture::history_packet;
        set_net_port(&callbacks);

        my_player_number = 0;
        player = &kfx_sim_state.players[0];
        player->id_number = 0;
        player->user_id = 0;
        player->view_type = PVT_DungeonTop;
        player->active_camera_idx = CamIV_Isometric;
        struct Camera *cam = &player->cameras[CamIV_Isometric];
        cam->view_mode = PVM_IsoWibbleView;
        cam->mappos.x.val = start_pos;
        cam->mappos.y.val = start_pos;
        cam->zoom = CAMERA_ZOOM_MAX;
        init_local_cameras(player);
    }
    ~LocalCameraFixture() { set_net_port(nullptr); }

    const struct Camera *predicted() {
        interpolate_local_cameras();
        return get_local_active_camera(player);
    }
};
}

TEST_CASE_METHOD(LocalCameraFixture, "update_local_cameras applies the local scroll on a normal packet", "[kfx_render][local_camera]") {
    packet.action = PckA_None;
    local_state.camera_movement_x = 1.0;
    update_local_cameras();
    CHECK(predicted()->mappos.x.val != start_pos);
}

TEST_CASE_METHOD(LocalCameraFixture, "update_local_cameras ignores the local scroll on a parchment jump (PckA_ZoomFromMap)", "[kfx_render][local_camera]") {
    packet.action = PckA_ZoomFromMap;
    packet.actn_par1 = 10;
    packet.actn_par2 = 20;
    local_state.camera_movement_x = 1.0;
    update_local_cameras();
    const struct Camera *cam = predicted();
    CHECK(cam->mappos.x.val == subtile_coord_center(10)); // the jump itself is applied
    CHECK(cam->mappos.y.val == subtile_coord_center(20)); // and nothing is scrolled on top of it
}

// Level start: init_player_cameras() only signals the local cameras to
// re-seed (camera_init_seq), then the heart zoom's set_engine_view() syncs the
// first-person camera to the spectator it just created. The sync must apply
// the pending init first; applied afterwards (by the next reader of the local
// cameras), the init reset the heading to the synced first-person
// camera's ANGLE_EAST, and the zoom north to the heart looked sideways.
TEST_CASE_METHOD(LocalCameraFixture, "sync_local_camera applies a pending camera init before syncing the first-person heading", "[kfx_render][local_camera]") {
    const ThingIndex spectator_idx = 1;
    struct Thing *spectator = thing_get(spectator_idx);
    std::memset(spectator, 0, sizeof(*spectator));
    spectator->index = spectator_idx;
    spectator->class_id = TCls_Object;
    spectator->alloc_flags = TAlF_Exists;
    spectator->mappos.x.val = start_pos;
    spectator->mappos.y.val = start_pos;
    spectator->move_angle_xy = ANGLE_NORTH;

    player->cameras[CamIV_FirstPerson].view_mode = PVM_CreatureView;
    player->cameras[CamIV_FirstPerson].rotation_angle_x = ANGLE_EAST; // as init_player_cameras() sets it
    kfx_sim_view_signals.camera_init_seq[player->id_number]++;

    player->controlled_thing_idx = spectator_idx;
    player->active_camera_idx = CamIV_FirstPerson;
    sync_local_camera(player);

    // predicted() reads the local cameras, which applies any signal still
    // pending -- before the fix, the init, over the synced heading.
    CHECK(predicted()->rotation_angle_x == ANGLE_NORTH);
}
