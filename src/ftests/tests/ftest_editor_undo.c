#include "ftest_editor_undo.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "player_instances.h"
#include "thing_list.h"
#include "kfx_editor.h"
#include "editor_journal.h"
#include "map_blocks.h"
#include "slab_data.h"
#include "config_terrain.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// docs/refactor/editor/09-toolbox-remainder.md's own suggested `editor_undo`
// ftest. Uses PckA_EditorRedoTrap for the *initial* placement too (not just
// for the actual redo step) -- same trick ftest_editor_place_creature.c's
// own comment explains for creatures: PckA_EditorPlaceTrap (what a real
// toolbox click sends) reads position from the packet's own ambient
// pos_x/pos_y, unreliable here for the same reason every terrain-paint verb
// is (see ftest_editor_paint_terrain.c); PckA_EditorRedoTrap carries
// position explicitly in actn_par1/actn_par2 instead, immune to that,
// while still exercising the real creation/journaling path (it's the exact
// verb editor_journal.cpp's own Ctrl+Y handler sends for a journaled trap).
//
// Also relies on editor_open()'s simulation_suspended freeze the same way
// ftest_editor_place_creature.c does -- every action after the editor opens
// uses turn_delay=0 and polls via FTRs_Repeat_Current_Action.
struct ftest_editor_undo__variables
{
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    ThingModel trap_model;
    PlayerNumber owner;
    ThingIndex placed_thing_idx;
    uint64_t poll_count;
};
struct ftest_editor_undo__variables ftest_editor_undo__vars = {
    .slb_x = 17,
    .slb_y = 74,
    .trap_model = 1, // first configured trap kind -- free placement bypasses workshop-stock checks
    .owner = 0, // PLAYER0
    .placed_thing_idx = 0,
    .poll_count = 0,
};

FTestActionResult ftest_editor_undo_action001__open_editor(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action002__place_trap(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action003__assert_placed(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action004__undo(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action005__assert_undone(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action006__redo(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action007__assert_redone(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action008__place_door(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action009__assert_door_then_undo(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_undo_action010__assert_door_undone(struct FTestActionArgs* const args);

TbBool ftest_editor_undo_init()
{
    ftest_append_action(ftest_editor_undo_action001__open_editor, 20, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action002__place_trap, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action003__assert_placed, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action004__undo, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action005__assert_undone, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action006__redo, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action007__assert_redone, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action008__place_door, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action009__assert_door_then_undo, 0, &ftest_editor_undo__vars);
    ftest_append_action(ftest_editor_undo_action010__assert_door_undone, 0, &ftest_editor_undo__vars);
    return true;
}

FTestActionResult ftest_editor_undo_action001__open_editor(struct FTestActionArgs* const args)
{
    ftest_util_reveal_map(PLAYER0);
    ftest_util_move_camera_to_slab(ftest_editor_undo__vars.slb_x, ftest_editor_undo__vars.slb_y, PLAYER0);

    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not mark the session active");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action002__place_trap(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->slb_x, vars->slb_y);

    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorRedoTrap, pos.x.val, pos.y.val, vars->trap_model, vars->owner);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action003__assert_placed(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;

    // subnum=1 (the slab's *center* subtile), matching where
    // player_place_trap_without_check_at() actually placed it --
    // set_coords_to_slab_center() (action002) and, for a non-"place on
    // subtile" trap kind, its own re-centering (player_instances.c) both
    // resolve to a slab's center subtile, not subnum=0.
    MapSubtlCoord stl_x = slab_subtile(vars->slb_x, 1);
    MapSubtlCoord stl_y = slab_subtile(vars->slb_y, 1);
    struct Thing* thing = find_base_thing_on_mapwho(TCls_Trap, vars->trap_model, stl_x, stl_y);
    if (thing_is_invalid(thing))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Trap never appeared at slab (%" PRId64 ",%" PRId64 ") after placement", (int64_t)vars->slb_x, (int64_t)vars->slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    if (thing->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Placed trap has owner %" PRId64 ", expected %" PRId64, (int64_t)thing->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    vars->placed_thing_idx = thing->index;
    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action004__undo(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;

    if (vars->placed_thing_idx == 0)
    {
        FTEST_FAIL_TEST("No placed thing index recorded -- previous action must have failed");
        return FTRs_Go_To_Next_Action;
    }

    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorUndo, vars->placed_thing_idx, 0, 0, 0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action005__assert_undone(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;

    struct Thing* thing = thing_get(vars->placed_thing_idx);
    if (!thing_is_invalid(thing) && (thing->class_id == TCls_Trap))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Placed trap (thing #%" PRId64 ") still exists after PckA_EditorUndo", (int64_t)vars->placed_thing_idx);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action006__redo(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;

    // Same action editor_journal.cpp's own Ctrl+Y handler would send for a
    // journaled trap -- exercises the real redo dispatch, not just a second
    // fresh placement.
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->slb_x, vars->slb_y);

    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorRedoTrap, pos.x.val, pos.y.val, vars->trap_model, vars->owner);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action007__assert_redone(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;

    // subnum=1 (the slab's *center* subtile), matching where
    // player_place_trap_without_check_at() actually placed it --
    // set_coords_to_slab_center() (action002) and, for a non-"place on
    // subtile" trap kind, its own re-centering (player_instances.c) both
    // resolve to a slab's center subtile, not subnum=0.
    MapSubtlCoord stl_x = slab_subtile(vars->slb_x, 1);
    MapSubtlCoord stl_y = slab_subtile(vars->slb_y, 1);
    struct Thing* thing = find_base_thing_on_mapwho(TCls_Trap, vars->trap_model, stl_x, stl_y);
    if (thing_is_invalid(thing))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Trap never reappeared at slab (%" PRId64 ",%" PRId64 ") after redo", (int64_t)vars->slb_x, (int64_t)vars->slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    if (thing->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Redone trap has owner %" PRId64 ", expected %" PRId64, (int64_t)thing->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

// Undoing a door placement (what the toolbox's Undo button does) must leave the
// owner's claimed path -- not rock. A corridor gives the door a valid orientation.
#define DOOR_SLB_X 30
#define DOOR_SLB_Y 80

FTestActionResult ftest_editor_undo_action008__place_door(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;
    for (int64_t k = -1; k <= 1; k++)
    {
        place_slab_type_on_map(SlbT_ROCK, slab_subtile(DOOR_SLB_X + k, 0), slab_subtile(DOOR_SLB_Y - 1, 0), vars->owner, 0);
        place_slab_type_on_map(SlbT_ROCK, slab_subtile(DOOR_SLB_X + k, 0), slab_subtile(DOOR_SLB_Y + 1, 0), vars->owner, 0);
    }
    for (int64_t k = -1; k <= 1; k++)
        place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(DOOR_SLB_X + k, 0), slab_subtile(DOOR_SLB_Y, 0), vars->owner, 0);
    for (int64_t k = -2; k <= 2; k++)
        for (int64_t m = -2; m <= 2; m++)
            do_slab_efficiency_alteration(DOOR_SLB_X + k, DOOR_SLB_Y + m);
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, DOOR_SLB_X, DOOR_SLB_Y);
    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorRedoDoor, pos.x.val, pos.y.val, 1, vars->owner);
    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action009__assert_door_then_undo(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;
    struct Thing* door = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(DOOR_SLB_X, 1), slab_subtile(DOOR_SLB_Y, 1));
    if (thing_is_invalid(door))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Door never appeared at slab (%" PRId64 ",%" PRId64 ")", (int64_t)(DOOR_SLB_X), (int64_t)(DOOR_SLB_Y));
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    editor_journal_do_undo(); // the toolbox's Undo button
    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_undo_action010__assert_door_undone(struct FTestActionArgs* const args)
{
    struct ftest_editor_undo__variables* const vars = args->data;
    struct Thing* door = find_base_thing_on_mapwho(TCls_Door, 0, slab_subtile(DOOR_SLB_X, 1), slab_subtile(DOOR_SLB_Y, 1));
    if (!thing_is_invalid(door))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("The door is still there after Undo");
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    const struct SlabMap* slb = get_slabmap_block(DOOR_SLB_X, DOOR_SLB_Y);
    if (slb->kind != SlbT_CLAIMED || slabmap_owner(slb) != vars->owner)
    {
        FTEST_FAIL_TEST("Undoing a door placement left slab kind %" PRId64 ", owner %" PRId64 "; expected the claimed path (%" PRId64 ") owned by %" PRId64,
            (int64_t)slb->kind, (int64_t)slabmap_owner(slb), (int64_t)SlbT_CLAIMED, (int64_t)vars->owner);
    }
    return FTRs_Go_To_Next_Action;
}

#endif
