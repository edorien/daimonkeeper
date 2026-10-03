/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file kfx_render_state.h
 *     Header file for kfx_render_state.c.
 * @par Purpose:
 *     Holds struct Game's remaining kfx_render-owned fields, migrated out
 *     of game_legacy.h per docs/refactor/stage-13-enforce-and-document.md
 *     (mirrors kfx_sim_state.h/kfx_net_state.h/kfx_game_state.h/
 *     kfx_frontend_state.h's approach from stages 6.7/7.2/8.3/9.3). Unlike
 *     those four, kfx_render never got its own state struct during
 *     stage 7 -- these fields were simply missed. `struct Game` is
 *     raw-serialized wholesale (see kfx_net/src/net_resync.cpp,
 *     kfx_game/src/game_saves.c, kfx_game/src/main_game.c's
 *     clear_complete_game()); this struct is synced/saved/reset the same
 *     way, alongside the other four, at all three call sites.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/

#ifndef DK_KFX_RENDER_STATE_H
#define DK_KFX_RENDER_STATE_H

#include "bflib_basics.h"
#include "globals.h"
#include "light_registry.h"
#include "thing_list.h"
#include "thing_types.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

/** What the shading keeps for a light: the range it shaded (subtiles), whether to shade it again, its flicker. */
struct LightDrawState {
    unsigned char range;
    TbBool need_update;
    TbBool reported_no_radius;
    int64_t intensity_random;
    int64_t previous_intensity_random;
    GameTurn last_turn_randomized;
};

/** A thing's drawing timer: the turn it was last drawn, how long it has been on screen since, and the creation turn
 *  of the thing it was for (an index can hold another thing later). */
struct DrawTimer {
    GameTurn creation_turn;
    GameTurn last_turn_drawn;
    unsigned char display_timer;
};

struct Thing;
struct Map;

// Lens-transition state (LensManager.cpp/lens_api.c): active_lens_type is
// the lens currently being applied/animated, applied_lens_type is read by
// kfx_sim (thing_creature.c) too, hence living in kfx_sim_state.h instead
// -- see docs/refactor/stage-13-enforce-and-document.md.
struct KfxRenderState {
    char active_lens_type;

    // Nearest-light-to-camera search state (light_data.c's
    // update_local_mouse_light() family) -- despite the placeholder name,
    // exclusively render-owned.
    int64_t something_light_x;
    int64_t something_light_y;

    // Mouse-cursor light/spell-cursor world position (engine_redraw.c).
    struct Coord3d mouse_light_pos;

    // Moved from struct Game (stage 13, docs/refactor/
    // stage-13-enforce-and-document.md) -- read by kfx_apploop/
    // kfx_frontend too, but kfx_render is the lowest-ranked of its
    // consumer set.
    double delta_time;

    // Moved from struct Game (stage 13) -- also read by kfx_platform's
    // sound_manager.cpp, which gets pointer access via
    // SoundHostPort instead (kfx_platform is the lowest-ranked
    // library, can't reach kfx_render_state directly). Used to restore
    // custom sprites.
    LevelNumber last_level;

    // Moved from kfx_frontend_state (stage 13.2, docs/refactor/
    // stage-13-enforce-and-document.md) -- mouse-cursor-under-3D-world
    // raycast results, computed and written every frame by
    // engine_render.c; kfx_frontend's front_input.c only ever reads
    // them, so kfx_render (the real lowest-rank owner/writer) is the
    // correct home, not kfx_frontend.
    int64_t pointer_x;
    int64_t pointer_y;
    int64_t block_pointed_at_x;
    int64_t block_pointed_at_y;
    int64_t pointed_at_frac_x;
    int64_t pointed_at_frac_y;
    int64_t top_pointed_at_x;
    int64_t top_pointed_at_y;
    int64_t top_pointed_at_frac_x;
    int64_t top_pointed_at_frac_y;
    struct Thing *thing_pointed_at;
    struct Map *me_pointed_at;

    // Refactor pass 5 S04: the room flags' (per thing index) and the creatures' thought bubbles' (per creature
    // control index) fade-in timers. They were in Thing.roomflag (last_turn_drawn, display_timer) and CreatureControl
    // (thought_bubble_last_turn_drawn, thought_bubble_display_timer), which are simulation state: what was on this
    // machine's screen changed them.
    struct DrawTimer roomflag_draw[THINGS_COUNT];
    struct DrawTimer thought_bubble_draw[CREATURES_COUNT];

    // Refactor pass 5, S11: what the shading keeps for each light (per light registry index). It was in struct
    // Light (range, LgtF_NeedUpdate, intensity_random, previous_intensity_random, last_turn_randomized), which is
    // simulation state.
    struct LightDrawState light_draw[LIGHTS_COUNT];
    /** The local user's cursor: where the mouse points this frame, and whether it points at the map. Its cursor light
     *  is drawn there (it moves in the registry from the user's packets, as the other users'). */
    TbBool local_cursor_valid;
};

#pragma pack()
/******************************************************************************/
extern struct KfxRenderState kfx_render_state;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
