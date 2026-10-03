/******************************************************************************/
// Free implementation of the Dungeon Keeper strategy game.
/******************************************************************************/
/** @file local_camera.c
 *     Client-side camera rendering for multiplayer with reduced input lag.
 * @par Purpose:
 *     Renders the local player's camera based on more up-to-date packets
 *     instead of input-lagged packets for smoother multiplayer experience.
 * @par Comment:
 *     None.
 * @author   KeeperFX Team
 * @date     04 Dec 2025
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "local_camera.h"
#include "engine_camera.h"
#include "player_camera.h"
#include "engine_render.h"
#include "packet_data.h"
#include "player_data.h"
#include "config_creature.h"
#include "thing_creature.h"
#include "dungeon_data.h"
#include "map_data.h"
#include "bflib_math.h"

#include <math.h>
#include "kfx_sim_state.h"
#include "ports/ui_port.h"
#include "ports/net_port.h"
#include "local_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
// TODO -- move these to LocalState
static struct Camera local_cameras[CamIV_EndList];
static struct Camera previous_local_cameras[CamIV_EndList];
static struct Camera destination_local_cameras[CamIV_EndList];
static double previous_deviation_x;
static double previous_deviation_y;
static double destination_deviation_x;
static double destination_deviation_y;
static TbBool local_camera_ready;
static MapCoord local_camera_move_target[2];
static MapCoordDelta local_camera_move_delta[2];
static struct Camera *local_camera_move_cam;
static struct Packet freecam_packet;
// The kfx_sim_view_signals counters (player_camera.h) as last applied here,
// for the local player.
static uint64_t seen_camera_init_seq;
static uint64_t seen_camera_sync_seq;
static uint64_t seen_camera_retarget_seq;
static void apply_sim_camera_signals(void);
/******************************************************************************/

static TbBool replay_is_detached(void)
{
    return replay.load_enable && local_state.replay_detached;
}

void camera_packet_set_state(struct Packet *pckt)
{
    apply_sim_camera_signals();
    struct PlayerInfo* player = get_my_player();
    if (!local_camera_ready || (get_local_view_type(player) != PVT_DungeonTop) || (player->view_type != PVT_DungeonTop)) {
        // senseless to transmit camera coords during these times
        packet_clear_camera_position(pckt);
        return;
    }
    const int64_t cam_idx = get_local_active_camera(player) - local_cameras;
    if ((cam_idx != CamIV_Isometric) && (cam_idx != CamIV_FrontView)) {
        packet_clear_camera_position(pckt);
        return;
    }
    const struct Camera *cam = &destination_local_cameras[cam_idx];
    // correct the camera rotation if nothing else to do (very low priority, could do this every n frames even...)
    if ((pckt->action == PckA_None) && (cam->velocity_rotation == 0)
     && ((pckt->control_flags & (PCtr_ViewRotateCW | PCtr_ViewRotateCCW)) == 0)
     && (cam->rotation_angle_x != player->cameras[cam_idx].rotation_angle_x))
        set_packet_action(pckt, PckA_SetMapRotation, cam->rotation_angle_x, 0, 0, 0);
    packet_set_camera_position(pckt, cam->mappos.x.val, cam->mappos.y.val);
}

void set_packet_power_on_thing(struct Packet *pckt, PowerKind pwkind, ThingIndex thing_idx)
{
    struct PlayerInfo* player = get_my_player();
    
    // transmit the local camera angle to ensure slap direction looks right
    set_packet_action(pckt, PckA_UsePwrOnThing, pwkind, thing_idx,
        get_local_active_camera(player)->rotation_angle_x, get_local_active_camera_index(player));
}

static void sync_camera_state(int64_t cam_idx, struct Camera *cam)
{
    local_cameras[cam_idx] = *cam;
    destination_local_cameras[cam_idx] = *cam;
    previous_local_cameras[cam_idx] = *cam;
}

static void sync_first_person_camera(struct Camera *cam, struct PlayerInfo *player)
{
    if (player->controlled_thing_idx <= 0) {
        return;
    }
    struct Thing *ctrltng = thing_get(player->controlled_thing_idx);
    if (!thing_exists(ctrltng)) {
        return;
    }
    struct Camera corrected_cam = *cam;
    int64_t eye_height = get_creature_eye_height(ctrltng);
    update_first_person_position(&corrected_cam, ctrltng, eye_height);
    corrected_cam.rotation_angle_x = ctrltng->move_angle_xy;
    corrected_cam.rotation_angle_y = ctrltng->move_angle_z;
    sync_camera_state(CamIV_FirstPerson, &corrected_cam);
}

void init_local_cameras(struct PlayerInfo *player)
{
    if (!is_my_player(player)) {
        return;
    }
    for (int64_t i = 0; i < CamIV_EndList; i++) {
        sync_camera_state(i, &player->cameras[i]);
    }
    local_camera_move_cam = NULL;
    local_camera_ready = true;
}

void move_local_camera_to_position(MapCoord x, MapCoord y)
{
    apply_sim_camera_signals();
    if (!local_camera_ready) {
        return;
    }
    int64_t cam_idx = get_local_active_camera(get_my_player()) - local_cameras;
    struct Camera *cam = &destination_local_cameras[cam_idx];
    local_camera_move_cam = cam;
    local_camera_move_target[0] = x;
    local_camera_move_target[1] = y;
    view_set_camera_move_to_position(cam, x, y, &local_camera_move_delta[0], &local_camera_move_delta[1]);
}

static void process_local_camera_movement(struct Camera *cam, const struct PlayerInfo *player)
{
    const int64_t rate = camera_move_rate(cam, player, local_state.camera_speedup_pressed);
    if (local_state.camera_movement_y != 0.0) {
        view_set_camera_y_velocity(cam, (int64_t)(local_state.camera_movement_y * rate / 4.0), (int64_t)(local_state.camera_movement_y * rate));
    }
    if (local_state.camera_movement_x != 0.0) {
        view_set_camera_x_velocity(cam, (int64_t)(local_state.camera_movement_x * rate / 4.0), (int64_t)(local_state.camera_movement_x * rate));
    }
}

static void update_local_first_person_camera(struct Thing *ctrltng, const struct Packet *pckt)
{
    struct Camera* cam = &destination_local_cameras[CamIV_FirstPerson];
    int64_t eye_height = get_creature_eye_height(ctrltng);
    update_first_person_position(cam, ctrltng, eye_height);

    if ((flag_is_set(kfx_sim_state.operation_flags, GOF_Paused) && network_is_active())
        || ! can_process_creature_input(ctrltng))
    {
        cam->rotation_angle_x = ctrltng->move_angle_xy;
        cam->rotation_angle_y = ctrltng->move_angle_z;
        return;
    }
    if (pckt == NULL) {
        return;
    }

    const int64_t current_horizontal = cam->rotation_angle_x;
    const int64_t current_vertical = cam->rotation_angle_y;
    int64_t new_horizontal, new_vertical, new_roll;
    process_first_person_look(ctrltng, pckt, current_horizontal, current_vertical, &new_horizontal, &new_vertical, &new_roll);
    cam->rotation_angle_x = new_horizontal;
    cam->rotation_angle_y = new_vertical;
    if ((ctrltng->movement_flags & TMvF_Flying) != 0) {
        cam->rotation_angle_z = new_roll;
    }
}

void update_local_cameras(void)
{
    apply_sim_camera_signals();
    if (!local_camera_ready) {
        return;
    }
    struct PlayerInfo *player = get_my_player();
    struct Thing *ctrltng = thing_get(player->controlled_thing_idx);
    const struct Packet *pckt = netport_get_history_packet(get_local_user(), get_gameturn());
    previous_deviation_x = destination_deviation_x;
    previous_deviation_y = destination_deviation_y;
    destination_deviation_x = 0;
    destination_deviation_y = 0;
    // Must snapshot before process_camera_action() mutates destination, or interpolation has nothing to ease from.
    memcpy(previous_local_cameras, destination_local_cameras, sizeof(previous_local_cameras));
    if (replay_is_detached()) {
        if (local_state.replay_view_type == PVT_DungeonTop) {
            process_camera_action(destination_local_cameras, &freecam_packet);
            struct Camera *cam = &destination_local_cameras[local_state.replay_cam_idx];
            process_local_camera_movement(cam, player);
            process_camera_view_controls(cam, &freecam_packet, player);
            view_process_camera_velocity(cam);
        }
        local_state.camera_movement_x = 0.0;
        local_state.camera_movement_y = 0.0;
        memset(&freecam_packet, 0, sizeof(freecam_packet));
        return;
    }
    if (replay_playback_is_paused()) {
        pckt = NULL;
    }
    if (pckt != NULL) {
        process_camera_action(destination_local_cameras, pckt);
        // Skip interpolation for parchment jumps, while retaining it for minimap dragging.
        if (pckt->action == PckA_ZoomFromMap) {
            memcpy(previous_local_cameras, destination_local_cameras, sizeof(previous_local_cameras));
        }
    }

    int64_t active_cam_idx = get_local_active_camera(player) - local_cameras;
    if (active_cam_idx == CamIV_FirstPerson && thing_exists(ctrltng)) {
        update_local_first_person_camera(ctrltng, pckt);
        return;
    }
    if (pckt == NULL) {
        return;
    }

    struct Camera *cam = &destination_local_cameras[active_cam_idx];
    if (active_cam_idx == CamIV_Parchment) {
        cam->mappos.x.val = player->cameras[CamIV_Parchment].mappos.x.val;
        cam->mappos.y.val = player->cameras[CamIV_Parchment].mappos.y.val;
    }
    if (local_camera_move_cam != NULL) {
        if (view_move_camera_to_position(local_camera_move_cam, local_camera_move_target[0], local_camera_move_target[1], local_camera_move_delta[0], local_camera_move_delta[1])) {
            local_camera_move_cam = NULL;
        }
    }
    if (local_camera_move_cam != cam) {
        // Same as the packet camera: a parchment map jump ignores the packet's camera controls.
        if (pckt->action != PckA_ZoomFromMap) {
            if (!replay.load_enable && cam->view_mode != PVM_ParchmentView) {
                process_local_camera_movement(cam, player);
                process_camera_view_controls(cam, pckt, player);
            } else {
                process_camera_controls(cam, pckt, player);
            }
            local_state.camera_movement_x = 0.0;
            local_state.camera_movement_y = 0.0;
        }
        view_process_camera_velocity(cam);
    }

    if (active_cam_idx == CamIV_Isometric) {
        struct Dungeon* dungeon = get_players_num_dungeon(my_player_number);
        if (kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] != 0) {
            int64_t angle = cam->rotation_angle_x;
            destination_deviation_x += ( (kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] * LbSinL(angle) >> 8) >> 8);
            destination_deviation_y += (-(kfx_sim_view_signals.camera_deviate_jump[dungeon->owner] * LbCosL(angle) >> 8) >> 8);
        }
    }
}

static void interpolate_camera_deviations(void)
{
    struct PlayerInfo* my_player = get_my_player();
    if (!player_exists(my_player)) {
        return;
    }
    struct Camera *active_camera = get_local_active_camera(my_player);
    if (active_camera->view_mode != PVM_IsoWibbleView && active_camera->view_mode != PVM_IsoStraightView) {
        return;
    }
    const double interpolated_deviation_x = interpolate(previous_deviation_x, destination_deviation_x);
    const double interpolated_deviation_y = interpolate(previous_deviation_y, destination_deviation_y);
    int64_t total_deviation_x = (int64_t)interpolated_deviation_x;
    int64_t total_deviation_y = (int64_t)interpolated_deviation_y;
    struct Dungeon* dungeon = get_players_num_dungeon(my_player_number);
    if (kfx_sim_view_signals.camera_deviate_quake[dungeon->owner] != 0) {
        total_deviation_x += UNSYNC_RANDOM(80) - 40;
        total_deviation_y += UNSYNC_RANDOM(80) - 40;
    }
    if (total_deviation_x == 0 && total_deviation_y == 0) {
        return;
    }
    struct Camera* cam = &local_cameras[CamIV_Isometric];
    const MapCoord max_x = (kfx_sim_state.map_subtiles_x + 1) * COORD_PER_STL - 1;
    const MapCoord max_y = (kfx_sim_state.map_subtiles_y + 1) * COORD_PER_STL - 1;
    cam->mappos.x.val = clamp(cam->mappos.x.val + total_deviation_x, 0, max_x);
    cam->mappos.y.val = clamp(cam->mappos.y.val + total_deviation_y, 0, max_y);
}

void interpolate_local_cameras(void)
{
    apply_sim_camera_signals();
    if (!local_camera_ready) {
        return;
    }
    for (int64_t i = 0; i < CamIV_EndList; i++) {
        struct Camera* prev = &previous_local_cameras[i];
        struct Camera* desired = &destination_local_cameras[i];
        struct Camera* out = &local_cameras[i];
        const double mappos_x = interpolate(prev->mappos.x.val, desired->mappos.x.val);
        const double mappos_y = interpolate(prev->mappos.y.val, desired->mappos.y.val);
        const double mappos_z = interpolate(prev->mappos.z.val, desired->mappos.z.val);
        const double angle_x = interpolate_angle(prev->rotation_angle_x, desired->rotation_angle_x);
        const double angle_y = interpolate_angle(prev->rotation_angle_y, desired->rotation_angle_y);
        const double angle_z = interpolate_angle(prev->rotation_angle_z, desired->rotation_angle_z);
        const double zoom = interpolate(prev->zoom, desired->zoom);
        out->mappos.x.val = lround(mappos_x);
        out->mappos.y.val = lround(mappos_y);
        out->mappos.z.val = lround(mappos_z);
        out->rotation_angle_x = lround(angle_x) & ANGLE_MASK;
        out->rotation_angle_y = lround(angle_y) & ANGLE_MASK;
        out->rotation_angle_z = lround(angle_z) & ANGLE_MASK;
        out->zoom = lround(zoom);
    }
    interpolate_camera_deviations();
}

static void sync_local_camera_now(struct PlayerInfo *player)
{
    if (!is_my_player(player) || !local_camera_ready) {
        return;
    }
    struct Camera *camera = get_player_active_camera(player);
    if (camera == &player->cameras[CamIV_Parchment] || player->view_mode == PVM_ParchmentView) {
        return;
    }
    if (camera == &player->cameras[CamIV_FirstPerson]) {
        sync_first_person_camera(camera, player);
        return;
    }
    for (int64_t cam_idx = 0; cam_idx < CamIV_EndList; cam_idx++) {
        sync_camera_state(cam_idx, &player->cameras[cam_idx]);
    }
}

static void set_local_camera_destination_now(struct PlayerInfo *player)
{
    if (!is_my_player(player) || !local_camera_ready || get_local_view_type(player) == PVT_MapScreen) {
        return;
    }
    memcpy(destination_local_cameras, player->cameras, sizeof(destination_local_cameras));
    struct Thing *ctrltng = thing_get(player->controlled_thing_idx);
    if (thing_exists(ctrltng)) {
        destination_local_cameras[CamIV_FirstPerson].rotation_angle_x = ctrltng->move_angle_xy;
        destination_local_cameras[CamIV_FirstPerson].rotation_angle_y = ctrltng->move_angle_z;
    }
}

// The two entry points below write the local cameras, so they apply any
// pending sim signal first: one applied later would overwrite what they
// write. At level start, init_player_cameras() signals an init and the heart
// zoom's set_engine_view() syncs the first-person camera to the spectator
// straight after; applying that init on the next frame instead reset its
// heading to the synced camera's ANGLE_EAST, so the zoom looked sideways.
void sync_local_camera(struct PlayerInfo *player)
{
    apply_sim_camera_signals();
    sync_local_camera_now(player);
}

void set_local_camera_destination(struct PlayerInfo *player)
{
    apply_sim_camera_signals();
    set_local_camera_destination_now(player);
}

/**
 * Applies whatever kfx_sim asked of the local camera since the last call:
 * re-initialise (init_player_cameras() ran), snap to the synced cameras, then
 * ease towards them -- in that order, so a snap sets the base a retarget in
 * the same batch starts from. The sim used to make these three calls
 * directly; it now only bumps kfx_sim_view_signals (refactor pass 2, S07).
 * Called at the start of every function here that reads or writes the local
 * cameras, so a change is seen by the next reader, and not applied over a
 * later write, just as the direct call was.
 */
static void apply_sim_camera_signals(void)
{
    const PlayerNumber plyr_idx = my_player_number;
    if (plyr_idx >= PLAYERS_COUNT) {
        return;
    }
    struct PlayerInfo *player = get_player(plyr_idx);
    const uint64_t init_seq = kfx_sim_view_signals.camera_init_seq[plyr_idx];
    const uint64_t sync_seq = kfx_sim_view_signals.camera_sync_seq[plyr_idx];
    const uint64_t retarget_seq = kfx_sim_view_signals.camera_retarget_seq[plyr_idx];
    if (init_seq != seen_camera_init_seq) {
        seen_camera_init_seq = init_seq;
        init_local_cameras(player);
    }
    if (sync_seq != seen_camera_sync_seq) {
        seen_camera_sync_seq = sync_seq;
        sync_local_camera_now(player);
    }
    if (retarget_seq != seen_camera_retarget_seq) {
        seen_camera_retarget_seq = retarget_seq;
        set_local_camera_destination_now(player);
    }
}

/**
 * Moves the local camera to the local player's next creature of the given
 * model and GUI job (the creature panel's and the console's "go to").
 * Moved here from kfx_sim's thing_creature.c in refactor pass 2 (S07): it
 * only moves the local camera, and every caller is local UI.
 *
 * @param crmodel
 * @param job_idx
  * @param pick_flags
 * @note originally was go_to_next_creature_of_breed_and_job()
 */
void go_to_next_creature_of_model_and_gui_job(int64_t crmodel, int64_t job_idx, unsigned char pick_flags)
{
    struct Thing* creatng = find_players_next_creature_of_breed_and_gui_job(crmodel, job_idx, my_player_number, pick_flags);
    if (!thing_is_invalid(creatng))
    {
        move_local_camera_to_position(creatng->mappos.x.val, creatng->mappos.y.val);
    }
}

void update_local_view_prediction(const struct Packet *pckt)
{
    if (pckt->action == PckA_ZoomFromMap) {
        local_camera_move_cam = NULL;
    }
    if (pckt->action == PckA_SaveViewType && pckt->actn_par1 == PVT_MapScreen) {
        local_state.view_type = PVT_MapScreen;
        ui_toggle_status_menu(0);
    } else if ((pckt->action == PckA_LoadViewType && pckt->actn_par1 == PVT_DungeonTop) || pckt->action == PckA_ZoomFromMap) {
        local_state.view_type = PVT_DungeonTop;
        ui_toggle_status_menu((kfx_sim_state.operation_flags & GOF_ShowPanel) != 0);
    }
}

struct Packet *get_freecam_packet(void)
{
    return &freecam_packet;
}

TbBool replay_camera_detached(void)
{
    return replay_is_detached();
}

// Replay playback held while a menu is open (upstream #5370). Set by kfx_game's
// set_replay_playback_paused(); kept here, with the replay camera, so the camera can read it.
static TbBool replay_playback_paused;

TbBool replay_playback_is_paused(void)
{
    return replay.load_enable && replay_playback_paused;
}

void local_camera_set_replay_paused(TbBool paused)
{
    replay_playback_paused = paused;
}

void replay_detach(void)
{
    if (replay_is_detached() || !local_camera_ready) {
        return;
    }
    struct PlayerInfo *player = get_my_player();
    int cam_idx = get_player_active_camera(player) - player->cameras;
    if ((cam_idx != CamIV_Isometric) && (cam_idx != CamIV_FrontView)) {
        cam_idx = (player->view_mode_restore == PVM_FrontView) ? CamIV_FrontView : CamIV_Isometric;
    }
    sync_camera_state(cam_idx, &player->cameras[cam_idx]);
    local_camera_move_cam = NULL;
    memset(&freecam_packet, 0, sizeof(freecam_packet));
    local_state.replay_detached = true;
    local_state.replay_view_type = PVT_DungeonTop;
    local_state.replay_cam_idx = cam_idx;
}

void replay_attach(void)
{
    if (!local_state.replay_detached) {
        return;
    }
    if (local_state.replay_view_type == PVT_MapScreen) {
        replay_freecam_set_map(false);
    }
    local_state.replay_detached = false;
    init_local_cameras(get_my_player());
}

void replay_freecam_set_map(TbBool on)
{
    if (!replay_is_detached()) {
        return;
    }
    local_state.replay_view_type = on ? PVT_MapScreen : PVT_DungeonTop;
    ui_toggle_status_menu(on ? 0 : ((kfx_sim_state.operation_flags & GOF_ShowPanel) != 0));
}

void replay_freecam_jump(MapSubtlCoord stl_x, MapSubtlCoord stl_y)
{
    if (!replay_is_detached()) {
        return;
    }
    struct Packet jump;
    memset(&jump, 0, sizeof(jump));
    jump.action = PckA_ZoomFromMap;
    jump.actn_par1 = stl_x;
    jump.actn_par2 = stl_y;
    process_camera_action(destination_local_cameras, &jump);
    memcpy(previous_local_cameras, destination_local_cameras, sizeof(previous_local_cameras));
    memcpy(local_cameras, destination_local_cameras, sizeof(local_cameras));
    replay_freecam_set_map(false);
}

TbBool local_view_type_settle(int64_t nview)
{
    if (local_state.view_type == nview)
        local_state.view_type = PVT_None;
    return local_state.view_type != PVT_None;
}

unsigned char get_local_view_type(const struct PlayerInfo *player)
{
    if (replay_is_detached() && is_my_player(player)) {
        return local_state.replay_view_type;
    }
    if (!is_my_player(player) || local_state.view_type == PVT_None) {
        return player->view_type;
    }
    if (local_state.view_type == PVT_MapScreen || player->view_type == PVT_MapScreen) {
        return local_state.view_type;
    }
    return player->view_type;
}

int get_local_active_camera_index(struct PlayerInfo *player)
{
    apply_sim_camera_signals();
    if (!is_my_player(player) || !local_camera_ready)
        return player->active_camera_idx;
    return get_local_active_camera(player) - local_cameras;
}

struct Camera* get_local_active_camera(struct PlayerInfo *player)
{
    apply_sim_camera_signals();
    struct Camera *camera = get_player_active_camera(player);
    if (camera == NULL || !is_my_player(player) || !local_camera_ready) {
        return camera;
    }
    if (replay_is_detached()) {
        if (local_state.replay_view_type == PVT_MapScreen) {
            return &local_cameras[CamIV_Parchment];
        }
        return &local_cameras[local_state.replay_cam_idx];
    }
    unsigned char view_type = get_local_view_type(player);
    if (view_type == PVT_MapScreen) {
        return &local_cameras[CamIV_Parchment];
    }
    if (view_type == PVT_DungeonTop && player->view_type == PVT_MapScreen) {
        if (player->view_mode_restore == PVM_FrontView) {
            return &local_cameras[CamIV_FrontView];
        }
        return &local_cameras[CamIV_Isometric];
    }
    return &local_cameras[camera - player->cameras];
}

// Pre-#5226 compatibility shim: some kfx_sim callers (routed through
// a port, since kfx_sim can't call get_local_active_camera()
// directly) still resolve a specific struct Camera* they already hold
// (e.g. &player->cameras[CamIV_FirstPerson]) to its local_cameras[]
// counterpart, rather than resolving fresh from a PlayerInfo* the way
// get_local_active_camera() does. Only covers CamIV_Isometric..
// CamIV_FrontView, matching upstream's own pre-removal behavior --
// FirstPerson/Parchment callers fall through to the passed-in cam
// unchanged, same as before.
struct Camera* get_local_camera(struct Camera* cam)
{
    if (!local_camera_ready) {
        return cam;
    }
    struct PlayerInfo *player = get_my_player();
    for (int64_t cam_idx = CamIV_Isometric; cam_idx <= CamIV_FrontView; cam_idx++) {
        if (cam == &player->cameras[cam_idx]) {
            return &local_cameras[cam_idx];
        }
    }
    return cam;
}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
