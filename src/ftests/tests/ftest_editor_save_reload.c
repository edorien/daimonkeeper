#include "ftest_editor_save_reload.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "config_campaigns.h" // struct LevelInformation, get_level_info() -- TEST_LEVEL_NAME assertions
#include "kfx_editor.h"
#include "packet_data.h"
#include "player_instances.h"
#include "thing_list.h"
#include "lvl_filesdk1.h"

#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// docs/refactor/editor/phase3/00-slice1-native-save.md -- see this test's
// own header comment for what it proves.
//
// TEST_SAVE_LVNUM deliberately doesn't match any level number another
// registered ftest loads (1/8/11/15/80/103 are all spoken for across
// ftest_list.c at the time this was written) -- editor_save_map() writes
// into the real staged campaign directory (get_level_fgroup() always
// resolves to FGrp_CmpgLvls regardless of lvnum), so a colliding number
// would silently overwrite another test's fixture data out from under it
// in the same -ftests sweep.
#define TEST_SAVE_LVNUM 90001
// docs/refactor/editor/phase3/01-slice2-classic-save.md -- a second,
// distinct scratch lvnum for the Force-Classic half of this test, so it
// doesn't overwrite the KFX-native save this same test already wrote.
#define TEST_SAVE_LVNUM_CLASSIC 90002
// docs/refactor/editor/phase3/03-slice4-file-dialogs.md -- exercises the
// new level_name plumbing through both writers (.lof's NAME_TEXT), read
// back via get_level_info() after the reload actions below.
#define TEST_LEVEL_NAME "Editor Save Reload Test"

struct ftest_editor_save_reload__variables
{
    MapSlabCoord creature_slb_x, creature_slb_y;
    MapSlabCoord trap_slb_x, trap_slb_y;
    ThingModel creature_model;
    ThingModel trap_model;
    PlayerNumber owner;
    CrtrExpLevel exp_level;
    ThingIndex creature_idx;
    ThingIndex trap_idx;
    uint64_t poll_count;
};
struct ftest_editor_save_reload__variables ftest_editor_save_reload__vars = {
    .creature_slb_x = 17, .creature_slb_y = 74,
    .trap_slb_x = 18, .trap_slb_y = 74,
    .creature_model = 0, // resolved at runtime (action001), 0 = "not yet chosen"
    .trap_model = 1,     // first configured trap kind, same as ftest_editor_undo.c
    .owner = 0,           // PLAYER0
    .exp_level = 2,
    .creature_idx = 0,
    .trap_idx = 0,
    .poll_count = 0,
};

FTestActionResult ftest_editor_save_reload_action001__open_editor(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action002__place_creature(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action003__assert_creature_placed(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action004__place_trap(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action005__assert_trap_placed(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action006__save_map(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action007__reload_map(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action008__assert_reloaded(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action009__save_map_classic(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action010__reload_map_classic(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_save_reload_action011__assert_reloaded_classic(struct FTestActionArgs* const args);

TbBool ftest_editor_save_reload_init()
{
    ftest_append_action(ftest_editor_save_reload_action001__open_editor, 20, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action002__place_creature, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action003__assert_creature_placed, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action004__place_trap, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action005__assert_trap_placed, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action006__save_map, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action007__reload_map, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action008__assert_reloaded, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action009__save_map_classic, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action010__reload_map_classic, 0, &ftest_editor_save_reload__vars);
    ftest_append_action(ftest_editor_save_reload_action011__assert_reloaded_classic, 0, &ftest_editor_save_reload__vars);
    return true;
}

FTestActionResult ftest_editor_save_reload_action001__open_editor(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);
    ftest_util_move_camera_to_slab(vars->creature_slb_x, vars->creature_slb_y, PLAYER0);

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

    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not mark the session active");
        return FTRs_Go_To_Next_Action;
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action002__place_creature(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->creature_slb_x, vars->creature_slb_y);

    struct PlayerInfo* player = get_player(vars->owner);
    int64_t packed_owner_exp = (int64_t)vars->owner | ((int64_t)vars->exp_level << 8);
    set_players_packet_action(player, PckA_EditorRedoCreature, pos.x.val, pos.y.val, vars->creature_model, packed_owner_exp);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action003__assert_creature_placed(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->creature_slb_x, vars->creature_slb_y);

    struct Thing* thing = get_creature_near(pos.x.val, pos.y.val);
    if (thing_is_invalid(thing) || !thing_is_creature(thing))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Creature never appeared near (%" PRId64 ",%" PRId64 ") after placement", (int64_t)vars->creature_slb_x, (int64_t)vars->creature_slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    vars->creature_idx = thing->index;
    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action004__place_trap(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->trap_slb_x, vars->trap_slb_y);

    struct PlayerInfo* player = get_player(vars->owner);
    set_players_packet_action(player, PckA_EditorRedoTrap, pos.x.val, pos.y.val, vars->trap_model, vars->owner);

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action005__assert_trap_placed(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    // subnum=1 (the slab's *center* subtile) -- see ftest_editor_undo.c's
    // own identical comment: player_place_trap_without_check_at() re-centers
    // a non-"place on subtile" trap kind onto its slab's center regardless
    // of the subtile it was asked for.
    MapSubtlCoord stl_x = slab_subtile(vars->trap_slb_x, 1);
    MapSubtlCoord stl_y = slab_subtile(vars->trap_slb_y, 1);
    struct Thing* thing = find_base_thing_on_mapwho(TCls_Trap, vars->trap_model, stl_x, stl_y);
    if (thing_is_invalid(thing))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Trap never appeared at slab (%" PRId64 ",%" PRId64 ") after placement", (int64_t)vars->trap_slb_x, (int64_t)vars->trap_slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    vars->trap_idx = thing->index;
    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action006__save_map(struct FTestActionArgs* const args)
{
    // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md --
    // editor_level_save_dir() (editor_session.cpp) factors out what used to
    // be duplicated inline here: get_level_fgroup() always resolves to
    // FGrp_CmpgLvls regardless of lvnum, so this lands in the same real,
    // writable, already-staged campaign directory as before.
    char full_path[512];
    editor_level_save_dir(TEST_SAVE_LVNUM, full_path, sizeof(full_path));

    // Explicit KFX-native, not Auto -- this test is specifically about the
    // KFX-native path (ftest_editor_save_reload_classic.c covers the
    // classic path); Auto's own format choice is exercised by
    // map_content_compat_test.cpp (Catch2), not here.
    if (!editor_save_map(TEST_SAVE_LVNUM, full_path, EdSaveFmt_ForceKeeperFX, TEST_LEVEL_NAME, 1, 0, NULL))
    {
        FTEST_FAIL_TEST("editor_save_map() failed to save to %s", full_path);
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action007__reload_map(struct FTestActionArgs* const args)
{
    if (!load_map_file(TEST_SAVE_LVNUM))
    {
        FTEST_FAIL_TEST("load_map_file() failed to reload the level editor_save_map() just wrote");
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action008__assert_reloaded(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->creature_slb_x, vars->creature_slb_y);
    struct Thing* creature = get_creature_near(pos.x.val, pos.y.val);
    if (thing_is_invalid(creature) || !thing_is_creature(creature))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Creature not found after reload at slab (%" PRId64 ",%" PRId64 ")", (int64_t)vars->creature_slb_x, (int64_t)vars->creature_slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    if (creature->model != vars->creature_model)
    {
        FTEST_FAIL_TEST("Reloaded creature has model %" PRId64 ", expected %" PRId64, (int64_t)creature->model, (int64_t)vars->creature_model);
        return FTRs_Go_To_Next_Action;
    }
    if (creature->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Reloaded creature has owner %" PRId64 ", expected %" PRId64, (int64_t)creature->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    MapSubtlCoord trap_stl_x = slab_subtile(vars->trap_slb_x, 1);
    MapSubtlCoord trap_stl_y = slab_subtile(vars->trap_slb_y, 1);
    struct Thing* trap = find_base_thing_on_mapwho(TCls_Trap, vars->trap_model, trap_stl_x, trap_stl_y);
    if (thing_is_invalid(trap))
    {
        FTEST_FAIL_TEST("Trap not found after reload at slab (%" PRId64 ",%" PRId64 ")", (int64_t)vars->trap_slb_x, (int64_t)vars->trap_slb_y);
        return FTRs_Go_To_Next_Action;
    }
    if (trap->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Reloaded trap has owner %" PRId64 ", expected %" PRId64, (int64_t)trap->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    // find_and_load_lof_files() (editor_save_map()'s own post-save call)
    // already scanned the .lof this test's save wrote -- get_level_info()
    // should reflect it without needing anything else.
    struct LevelInformation* lvinfo = get_level_info(TEST_SAVE_LVNUM);
    if ((lvinfo == NULL) || (strcmp(lvinfo->name, TEST_LEVEL_NAME) != 0))
    {
        FTEST_FAIL_TEST("Reloaded level's .lof name is \"%s\", expected \"%s\"",
            (lvinfo != NULL) ? lvinfo->name : "(no LevelInformation)", TEST_LEVEL_NAME);
        return FTRs_Go_To_Next_Action;
    }

    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

// docs/refactor/editor/phase3/01-slice2-classic-save.md -- proves
// ClassicMapContentWriter/Reader round-trip through the real engine too,
// not just KfxNativeMapContentWriter/Reader (actions 006-008 above). Runs
// straight after the KFX-native reload (action008) succeeded, reusing
// that same live state (the creature/trap are already there from the
// reload -- no need to re-place them) rather than a separate fixture.
FTestActionResult ftest_editor_save_reload_action009__save_map_classic(struct FTestActionArgs* const args)
{
    char full_path[512];
    editor_level_save_dir(TEST_SAVE_LVNUM_CLASSIC, full_path, sizeof(full_path));

    // Force, not Auto: "keeporig" is far larger than 85x85 (confirmed
    // live earlier this session -- 255x255), so it's never going to be
    // map_is_legacy_compatible() regardless of thing content. Force
    // exercises the writer/format-selection machinery directly rather
    // than depending on a stock level happening to be classic-sized.
    if (!editor_save_map(TEST_SAVE_LVNUM_CLASSIC, full_path, EdSaveFmt_ForceClassic, TEST_LEVEL_NAME, 1, 0, NULL))
    {
        FTEST_FAIL_TEST("editor_save_map(ForceClassic) failed to save to %s", full_path);
    }

    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action010__reload_map_classic(struct FTestActionArgs* const args)
{
    if (!load_map_file(TEST_SAVE_LVNUM_CLASSIC))
    {
        FTEST_FAIL_TEST("load_map_file() failed to reload the classic-format level editor_save_map() just wrote");
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_save_reload_action011__assert_reloaded_classic(struct FTestActionArgs* const args)
{
    struct ftest_editor_save_reload__variables* const vars = args->data;

    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->creature_slb_x, vars->creature_slb_y);
    struct Thing* creature = get_creature_near(pos.x.val, pos.y.val);
    if (thing_is_invalid(creature) || !thing_is_creature(creature))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("Creature not found after classic reload at slab (%" PRId64 ",%" PRId64 ")", (int64_t)vars->creature_slb_x, (int64_t)vars->creature_slb_y);
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    if (creature->model != vars->creature_model)
    {
        FTEST_FAIL_TEST("Classic-reloaded creature has model %" PRId64 ", expected %" PRId64, (int64_t)creature->model, (int64_t)vars->creature_model);
        return FTRs_Go_To_Next_Action;
    }
    if (creature->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Classic-reloaded creature has owner %" PRId64 ", expected %" PRId64, (int64_t)creature->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    MapSubtlCoord trap_stl_x = slab_subtile(vars->trap_slb_x, 1);
    MapSubtlCoord trap_stl_y = slab_subtile(vars->trap_slb_y, 1);
    struct Thing* trap = find_base_thing_on_mapwho(TCls_Trap, vars->trap_model, trap_stl_x, trap_stl_y);
    if (thing_is_invalid(trap))
    {
        FTEST_FAIL_TEST("Trap not found after classic reload at slab (%" PRId64 ",%" PRId64 ")", (int64_t)vars->trap_slb_x, (int64_t)vars->trap_slb_y);
        return FTRs_Go_To_Next_Action;
    }
    if (trap->owner != vars->owner)
    {
        FTEST_FAIL_TEST("Classic-reloaded trap has owner %" PRId64 ", expected %" PRId64, (int64_t)trap->owner, (int64_t)vars->owner);
        return FTRs_Go_To_Next_Action;
    }

    // Base MapContentWriter::write_level_info() (moved off the two
    // subclasses this slice) -- proves the classic writer also gets .lof
    // naming now, not just KFX-native.
    struct LevelInformation* lvinfo = get_level_info(TEST_SAVE_LVNUM_CLASSIC);
    if ((lvinfo == NULL) || (strcmp(lvinfo->name, TEST_LEVEL_NAME) != 0))
    {
        FTEST_FAIL_TEST("Classic-reloaded level's .lof name is \"%s\", expected \"%s\"",
            (lvinfo != NULL) ? lvinfo->name : "(no LevelInformation)", TEST_LEVEL_NAME);
        return FTRs_Go_To_Next_Action;
    }

    vars->poll_count = 0;
    return FTRs_Go_To_Next_Action;
}

#endif
