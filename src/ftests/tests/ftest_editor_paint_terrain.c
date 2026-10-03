#include "ftest_editor_paint_terrain.h"
#include "game_commands.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "kfx_editor.h"
#include "packet_data.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "map_data.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// docs/refactor/editor/09-toolbox-remainder.md's own suggested
// `editor_paint_terrain` ftest.
//
// PckA_EditorPlaceTerrainRect (like every terrain-paint/fill/rect-op verb
// -- PckA_CheatPlaceTerrain, PckA_EditorFloodFill, PckA_EditorRectClearEarth/
// _RectDeleteThings/_RectSetOwner) reads its "release corner" (corner 2)
// from the packet's own *ambient* pos_x/pos_y; corner 1 (the drag start) is
// the only fully-explicit part, carried in actn_par1/actn_par2.
//
// Found live: an ftest action's own read of get_local_packet()->pos_x/pos_y
// is *structurally* unable to observe a meaningful ambient value.
// gameplay_loop_logic() (game_session_loop.cpp) calls ftest_update() *before*
// input() each tick, and clear_packets() (packets_misc.c) memsets every
// packet back to zero once process_packets() has dispatched it -- so by the
// time an ftest action's own turn comes around again, pos_x/pos_y has
// already been wiped back to 0 by the *previous* tick's processing, and
// this tick's own fresh ambient value hasn't been computed yet (that
// happens in input(), which runs *after* ftest_update() this same tick).
// An ftest action can therefore never read the value the real packet
// handler will actually see -- only production code running *between*
// input() and clear_packets() (i.e. process_packets() itself) ever
// observes it. Confirmed further: even that real value is not something a
// camera move can steer -- probing it (temporarily, not committed) showed
// it converges to the same fixed subtile-120 X regardless of camera
// target, and a Y pinned right at the map's own border, under the SDL
// dummy video driver's headless mouse/camera projection. It cannot be
// predicted, controlled, or read from test code.
//
// This test therefore never reads or reasons about corner 2 at all. It
// only relies on corner 1 (the drag start), which editor_apply_slab_rect()
// always includes as one of the box's own boundary corners regardless of
// where corner 2 lands -- so asserting *only* that single, fully-explicit
// slab is correct no matter what the untestable ambient corner turns out
// to be.
struct ftest_editor_paint_terrain__variables
{
    SlabKind terrain_kind;
    PlayerNumber owner;
    MapSlabCoord drag_slb_x, drag_slb_y;
    uint64_t poll_count;
};
struct ftest_editor_paint_terrain__variables ftest_editor_paint_terrain__vars = {
    .terrain_kind = SlbT_CLAIMED,
    .owner = 0, // PLAYER0, matches other editor ftests
    .drag_slb_x = 50, .drag_slb_y = 50, // comfortably interior, far from any map border
    .poll_count = 0,
};

FTestActionResult ftest_editor_paint_terrain_action001__open_editor(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_paint_terrain_action002__paint_rect(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_paint_terrain_action003__assert_painted(struct FTestActionArgs* const args);

TbBool ftest_editor_paint_terrain_init()
{
    ftest_append_action(ftest_editor_paint_terrain_action001__open_editor, 20, &ftest_editor_paint_terrain__vars);
    ftest_append_action(ftest_editor_paint_terrain_action002__paint_rect, 0, &ftest_editor_paint_terrain__vars);
    ftest_append_action(ftest_editor_paint_terrain_action003__assert_painted, 0, &ftest_editor_paint_terrain__vars);
    return true;
}

FTestActionResult ftest_editor_paint_terrain_action001__open_editor(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(PLAYER0);
    ftest_util_move_camera_to_slab(17, 74, PLAYER0);

    // docs/refactor/editor/01-entry-and-editor-session.md: safe to call
    // directly on an already-loaded level, same as ftest_editor_place_creature.
    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not mark the session active");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

// simulation_suspended (set by editor_open()) freezes get_gameturn(), so
// every action from here on uses turn_delay=0 and polls via
// FTRs_Repeat_Current_Action instead of waiting on turn advancement --
// same reasoning as ftest_editor_place_creature.c's own comment.
FTestActionResult ftest_editor_paint_terrain_action002__paint_rect(struct FTestActionArgs* const args)
{
    struct ftest_editor_paint_terrain__variables* const vars = args->data;

    MapSubtlCoord drag_stl_x = slab_subtile_center(vars->drag_slb_x);
    MapSubtlCoord drag_stl_y = slab_subtile_center(vars->drag_slb_y);

    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorPlaceTerrainRect, drag_stl_x, drag_stl_y, vars->terrain_kind, vars->owner);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_paint_terrain_action003__assert_painted(struct FTestActionArgs* const args)
{
    struct ftest_editor_paint_terrain__variables* const vars = args->data;

    struct SlabMap* slb = get_slabmap_block(vars->drag_slb_x, vars->drag_slb_y);
    if (slb->kind != vars->terrain_kind)
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Slab (%" PRId64 ",%" PRId64 ") has kind %" PRId64 ", expected %" PRId64, (int64_t)vars->drag_slb_x, (int64_t)vars->drag_slb_y, (int64_t)slb->kind, (int64_t)vars->terrain_kind);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    if (slabmap_owner(slb) != vars->owner)
    {
        FTEST_FAIL_TEST("Slab (%" PRId64 ",%" PRId64 ") has owner %" PRId64 ", expected %" PRId64, (int64_t)vars->drag_slb_x, (int64_t)vars->drag_slb_y, (int64_t)slabmap_owner(slb), (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

#endif
