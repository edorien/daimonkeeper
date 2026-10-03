/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file player_camera.h
 *     Header file for player_camera.c.
 * @par Purpose:
 *     The synced player cameras, moved from kfx_render's engine_camera.h and
 *     kfx_net's packets.h (docs/refactor-pass2/stage-07-camera-to-sim.md).
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_PLAYER_CAMERA_H
#define DK_PLAYER_CAMERA_H

#include "bflib_basics.h"
#include "globals.h"
#include "camera_data.h"
#include "kfx_config_state.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define CAMERA_TILT_DEFAULT -266
#define CAMERA_TILT_MIN -350
#define CAMERA_TILT_MAX -200

struct Coord3d;
struct Packet;
struct PlayerInfo;
struct Thing;

/**
 * Sim -> local camera signals.
 *
 * The sim used to call kfx_render's local camera directly (through
 * SimFeedbackCallbacks) to re-seed it or move its target after changing a
 * player's synced camera. Now it bumps one of these per-player counters
 * instead, and kfx_render's local_camera.c compares them with the values
 * it last saw before it reads its own cameras, applying init, then snap,
 * then retarget:
 *  - camera_init_seq: init_player_cameras() ran (level start);
 *  - camera_sync_seq: snap the local cameras to the synced ones;
 *  - camera_retarget_seq: ease the local cameras towards the synced ones.
 *
 * Deliberately outside kfx_sim_state: not saved, not resynced, not
 * checksummed. The sim only ever writes it, so a counter can differ between
 * machines without affecting the game.
 */
struct KfxSimViewSignals {
    uint64_t camera_init_seq[PLAYERS_COUNT];
    uint64_t camera_sync_seq[PLAYERS_COUNT];
    uint64_t camera_retarget_seq[PLAYERS_COUNT];
    /* Screen shake for the local camera, per player: the sim sets and
       decays these (a quake's remaining turns, a jump's size); only
       kfx_render's local_camera.c reads them. Moved out of the saved,
       synced struct Dungeon in refactor pass 2 (S10). Indexed by
       dungeon->owner; the extra slot takes writes to a cleared dungeon
       (owner PLAYERS_COUNT), as bad_dungeon did. */
    int64_t camera_deviate_quake[PLAYERS_COUNT + 1];
    int64_t camera_deviate_jump[PLAYERS_COUNT + 1];
};
extern struct KfxSimViewSignals kfx_sim_view_signals;
// Ask the local camera to snap to (sync) or ease towards (retarget) this
// player's synced cameras. A no-op on machines where it isn't the local player.
void signal_local_camera_sync(const struct PlayerInfo *player);
void signal_local_camera_retarget(const struct PlayerInfo *player);

void view_zoom_camera_in(struct Camera *cam, int64_t limit_max, int64_t limit_min);
void view_zoom_camera_in_to(struct Camera *cam, int64_t limit_max, int64_t limit_min, MapCoord x, MapCoord y);
void set_camera_zoom(struct Camera *cam, int64_t val);
void view_zoom_camera_out(struct Camera *cam, int64_t limit_max, int64_t limit_min);
void view_zoom_camera_out_from(struct Camera *cam, int64_t limit_max, int64_t limit_min, MapCoord x, MapCoord y);
int64_t get_camera_zoom(struct Camera *cam);
void update_camera_zoom_bounds(struct Camera *cam,uint64_t zoom_max,uint64_t zoom_min);
void view_set_camera_y_velocity(struct Camera *cam, int64_t delta, int64_t ilimit);
void view_set_camera_x_velocity(struct Camera *cam, int64_t delta, int64_t ilimit);
void view_set_camera_rotation_velocity(struct Camera *cam, int64_t delta, int64_t ilimit);
void view_set_camera_rotation_velocity_around(struct Camera *cam, int64_t delta, int64_t ilimit, MapCoord x, MapCoord y);
void view_set_camera_tilt(struct Camera *cam, unsigned char mode);
void view_process_camera_velocity(struct Camera *cam);
int64_t camera_move_rate(const struct Camera* cam, const struct PlayerInfo* player, TbBool speedup);
void view_set_camera_position(struct Camera *cam, MapCoord x, MapCoord y);
void view_set_camera_move_to_position(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta *move_x, MapCoordDelta *move_y);
TbBool view_move_camera_to_position(struct Camera *cam, MapCoord x, MapCoord y, MapCoordDelta move_x, MapCoordDelta move_y);
void update_all_players_cameras(void);
void init_player_cameras(struct PlayerInfo *player);
void update_first_person_position(struct Camera *cam, struct Thing *thing, int64_t eye_height);
void update_first_person_camera(struct Camera *cam, struct Thing *thing);
void set_player_cameras_position(struct PlayerInfo *player, int64_t pos_x, int64_t pos_y);
TbBool any_player_close_enough_to_see(const struct Coord3d *pos);
uint64_t lightning_is_close_to_player(struct PlayerInfo *player, struct Coord3d *pos);

// Packet camera handlers, run for the synced cameras by kfx_net's packet
// processing and for the predicted local cameras by kfx_render's
// local_camera.c.
void process_camera_controls(struct Camera* cam, const struct Packet* pckt, struct PlayerInfo* player);
void process_camera_view_controls(struct Camera* cam, const struct Packet* pckt, struct PlayerInfo* player);
void process_camera_action(struct Camera *cams, const struct Packet *pckt);
void process_first_person_look(struct Thing *thing, const struct Packet *pckt, int64_t current_horizontal, int64_t current_vertical, int64_t *out_horizontal, int64_t *out_vertical, int64_t *out_roll);
TbBool can_process_creature_input(struct Thing *thing);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
