#include "ftest_editor_place_creature.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "player_instances.h"
#include "creature_control.h"
#include "kfx_editor.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// docs/refactor/editor/02-editing-toolbox.md §6's own suggested test.
// Placement is dispatched via PckA_EditorRedoCreature -- deliberately not
// PckA_CheatMakeCreature (the verb a real toolbox click normally sends).
// PckA_CheatMakeCreature reads its target position from the packet's own
// *ambient* pos_x/pos_y, which get_dungeon_control_nonaction_inputs()
// (called from input(), which runs every real turn regardless of what an
// ftest action wants) unconditionally overwrites from whatever the
// (headless, meaningless) cursor position happens to be -- the exact bug
// this session's own Redo work found and fixed for kfx_editor's render-
// phase callbacks. A test action has the identical timing problem, so it
// needs the identical fix: PckA_EditorRedoCreature carries position
// explicitly in actn_par1/actn_par2 instead, immune to that overwrite,
// while still exercising real production code (it's the same handler
// Ctrl+Y/Redo uses, which mirrors PckA_CheatMakeCreature's own logic
// exactly and journals the result the same way).
//
// Also found live (well, live in the sense of reading the code, not
// running it yet): editor_open() sets kfx_sim_state.simulation_suspended
// = true, and get_gameturn() (game_legacy_get_gameturn() ->
// kfx_game_state.play_gameturn) only increments *inside*
// game_session_loop.cpp's own "!GOF_Paused && !simulation_suspended" gate
// -- so the game turn counter freezes the instant the editor session
// opens. ftest.c's own action scheduler (ftest_update()) gates moving to
// the *next* queued action on "get_gameturn() >= intended_start_at_game_turn",
// computed once at init time as a cumulative sum of every action's own
// turn_delay -- so any action appended with turn_delay > 0 *after* the
// point where editor_open() runs would never start, stalling the test
// forever (the same failure shape already documented for
// bug_invisible_units_cant_select in ftest_list.c, just a different
// cause). Every action below the editor-open one therefore uses
// turn_delay=0 and polls via FTRs_Repeat_Current_Action instead of
// waiting on turn advancement -- safe because process_packets() (which
// actually dispatches the packets this test sends) runs unconditionally
// at the top of update(), before the simulation_suspended check, so it
// keeps running every real loop iteration regardless of the frozen
// counter.
struct ftest_editor_place_creature__variables
{
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    ThingModel creature_model;
    PlayerNumber owner;
    CrtrExpLevel exp_level; // 0-indexed, matches set_creature_level()'s own convention
    ThingIndex placed_thing_idx;
    uint64_t poll_count;
};
struct ftest_editor_place_creature__variables ftest_editor_place_creature__vars = {
    .slb_x = 17,
    .slb_y = 74,
    .creature_model = 0, // resolved at runtime (action001), 0 = "not yet chosen"
    .owner = 0,
    .exp_level = 4, // level 5, 1-indexed for humans
    .placed_thing_idx = 0,
    .poll_count = 0,
};

FTestActionResult ftest_editor_place_creature_action001__open_editor(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_place_creature_action002__place_creature(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_place_creature_action003__assert_placed(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_place_creature_action004__undo(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_place_creature_action005__assert_undone(struct FTestActionArgs* const args);

TbBool ftest_editor_place_creature_init()
{
    ftest_append_action(ftest_editor_place_creature_action001__open_editor, 20, &ftest_editor_place_creature__vars);
    ftest_append_action(ftest_editor_place_creature_action002__place_creature, 0, &ftest_editor_place_creature__vars);
    ftest_append_action(ftest_editor_place_creature_action003__assert_placed, 0, &ftest_editor_place_creature__vars);
    ftest_append_action(ftest_editor_place_creature_action004__undo, 0, &ftest_editor_place_creature__vars);
    ftest_append_action(ftest_editor_place_creature_action005__assert_undone, 0, &ftest_editor_place_creature__vars);
    return true;
}

FTestActionResult ftest_editor_place_creature_action001__open_editor(struct FTestActionArgs* const args)
{
    struct ftest_editor_place_creature__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);
    ftest_util_move_camera_to_slab(vars->slb_x, vars->slb_y, PLAYER0);

    // First non-spectator creature model, deterministically (not
    // GAME_RANDOM -- this test doesn't need variety, just a valid model).
    for (ThingModel m = 1; m < kfx_config_state.conf.crtr_conf.model_count; m++)
    {
        struct CreatureModelConfig* crconf = creature_stats_get(m);
        if ((crconf->model_flags & CMF_IsSpectator) == 0)
        {
            vars->creature_model = m;
            break;
        }
    }
    if (vars->creature_model == 0)
    {
        FTEST_FAIL_TEST("Could not find a non-spectator creature model");
        return FTRs_Go_To_Next_Action;
    }
    vars->owner = PLAYER0;

    // docs/refactor/editor/01-entry-and-editor-session.md: the lvnum param
    // is only ever used for a log line, and is_new only affects the
    // dirty-flag -- safe to call directly on an already-loaded level like
    // this, no need to replicate the full menu-driven Tools->Editor flow.
    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not mark the session active");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_place_creature_action002__place_creature(struct FTestActionArgs* const args)
{
    struct ftest_editor_place_creature__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->slb_x, vars->slb_y);

    struct PlayerInfo* player = get_player(vars->owner);
    int64_t packed_owner_exp = (int64_t)vars->owner | ((int64_t)vars->exp_level << 8);
    set_players_packet_action(player, PckA_EditorRedoCreature, pos.x.val, pos.y.val, vars->creature_model, packed_owner_exp);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_place_creature_action003__assert_placed(struct FTestActionArgs* const args)
{
    struct ftest_editor_place_creature__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->slb_x, vars->slb_y);

    struct Thing* thing = get_creature_near(pos.x.val, pos.y.val);
    if (thing_is_invalid(thing) || !thing_is_creature(thing))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Creature never appeared near (%" PRId64 ",%" PRId64 ") after placement", (int64_t)vars->slb_x, (int64_t)vars->slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    if (thing->model != vars->creature_model)
    {
        FTEST_FAIL_TEST("Placed creature has model %" PRId64 ", expected %" PRId64, (int64_t)thing->model, (int64_t)vars->creature_model);
        return FTRs_Go_To_Next_Action;
    }
    if (thing->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Placed creature has owner %" PRId64 ", expected %" PRId64, (int64_t)thing->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }
    struct CreatureControl* cctrl = creature_control_get_from_thing(thing);
    if (cctrl->exp_level != vars->exp_level)
    {
        FTEST_FAIL_TEST("Placed creature has exp_level %" PRId64 ", expected %" PRId64, (int64_t)cctrl->exp_level, (int64_t)vars->exp_level);
        return FTRs_Go_To_Next_Action;
    }

    vars->placed_thing_idx = thing->index;
    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_place_creature_action004__undo(struct FTestActionArgs* const args)
{
    struct ftest_editor_place_creature__variables* const vars = args->data;

    if (vars->placed_thing_idx == 0)
    {
        FTEST_FAIL_TEST("No placed thing index recorded -- previous action must have failed");
        return FTRs_Go_To_Next_Action;
    }

    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorUndo, vars->placed_thing_idx, 0, 0, 0);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_place_creature_action005__assert_undone(struct FTestActionArgs* const args)
{
    struct ftest_editor_place_creature__variables* const vars = args->data;

    struct Thing* thing = thing_get(vars->placed_thing_idx);
    if (!thing_is_invalid(thing) && thing->class_id == TCls_Creature)
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Placed creature (thing #%" PRId64 ") still exists after PckA_EditorUndo", (int64_t)vars->placed_thing_idx);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }

    return FTRs_Go_To_Next_Action;
}

#endif
