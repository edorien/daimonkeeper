#include "ftest_editor_points.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "kfx_editor.h"
#include "editor_points.h"
#include "editor_journal.h"
#include "actionpt.h"
#include "light_data.h"
#include "thing_list.h"
#include "lvl_filesdk1.h"
#include "game_lifecycle.h"

#include <string.h>

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

// A scratch level number no other registered ftest uses (see
// ftest_editor_save_reload.c's own comment on why that matters).
#define TEST_SAVE_LVNUM 90003
#define TEST_AP_NUMBER 7

struct ftest_editor_points__variables
{
    MapSlabCoord slb_x, slb_y;
    int64_t light_x, light_y, light_z, light_radius;
    int64_t light_intensity;
    int64_t ap_x, ap_y, ap_range;
    int64_t fx_x, fx_y, fx_range;
    int64_t fx_model;
    int64_t level_lights_before_save;
    uint64_t poll_count;
};
struct ftest_editor_points__variables ftest_editor_points__vars = {
    .slb_x = 17, .slb_y = 74,
    .light_radius = 1280, .light_intensity = 40, .light_z = 384,
    .ap_range = 768,
    .fx_range = 512, .fx_model = 1,
    .level_lights_before_save = 0,
    .poll_count = 0,
};

FTestActionResult ftest_editor_points_action001__open_editor(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_points_action002__create_points(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_points_action003__undo_redo(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_points_action004__save_and_reload(struct FTestActionArgs* const args);
FTestActionResult ftest_editor_points_action005__assert_reloaded(struct FTestActionArgs* const args);

TbBool ftest_editor_points_init()
{
    ftest_append_action(ftest_editor_points_action001__open_editor, 20, &ftest_editor_points__vars);
    ftest_append_action(ftest_editor_points_action002__create_points, 0, &ftest_editor_points__vars);
    ftest_append_action(ftest_editor_points_action003__undo_redo, 0, &ftest_editor_points__vars);
    ftest_append_action(ftest_editor_points_action004__save_and_reload, 0, &ftest_editor_points__vars);
    ftest_append_action(ftest_editor_points_action005__assert_reloaded, 0, &ftest_editor_points__vars);
    return true;
}

// Static, non-thing-owned lights: what a save persists as level lights.
static int64_t count_level_lights(void)
{
    unsigned char owned[LIGHTS_COUNT];
    editor_points_mark_thing_owned_lights(owned);
    int64_t n = 0;
    for (int64_t i = 1; i < LIGHTS_COUNT; i++)
    {
        const struct Light* lgt = &lish.lights[i];
        if (((lgt->flags & LgtF_Allocated) != 0) && ((lgt->flags & LgtF_Dynamic) == 0) && !owned[i])
            n++;
    }
    return n;
}

static const struct Light* find_level_light_at(int64_t x, int64_t y)
{
    unsigned char owned[LIGHTS_COUNT];
    editor_points_mark_thing_owned_lights(owned);
    for (int64_t i = 1; i < LIGHTS_COUNT; i++)
    {
        const struct Light* lgt = &lish.lights[i];
        if (((lgt->flags & LgtF_Allocated) != 0) && ((lgt->flags & LgtF_Dynamic) == 0) && !owned[i]
            && (lgt->mappos.x.val == x) && (lgt->mappos.y.val == y))
            return lgt;
    }
    return NULL;
}

static struct Thing* find_effectgen_at(int64_t x, int64_t y)
{
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
    {
        struct Thing* thing = thing_get(i);
        if (thing_exists(thing) && (thing->class_id == TCls_EffectGen)
            && (thing->mappos.x.val == x) && (thing->mappos.y.val == y))
            return thing;
    }
    return NULL;
}

static TbBool all_three_present(const struct ftest_editor_points__variables* vars)
{
    return (find_level_light_at(vars->light_x, vars->light_y) != NULL)
        && (action_point_get_by_number(TEST_AP_NUMBER) != NULL) && action_point_exists(action_point_get_by_number(TEST_AP_NUMBER))
        && (find_effectgen_at(vars->fx_x, vars->fx_y) != NULL);
}

static TbBool none_present(const struct ftest_editor_points__variables* vars)
{
    return (find_level_light_at(vars->light_x, vars->light_y) == NULL)
        && !action_point_exists(action_point_get_by_number(TEST_AP_NUMBER))
        && (find_effectgen_at(vars->fx_x, vars->fx_y) == NULL);
}

FTestActionResult ftest_editor_points_action001__open_editor(struct FTestActionArgs* const args)
{
    struct ftest_editor_points__variables* const vars = args->data;

    ftest_util_reveal_map(PLAYER0);
    ftest_util_move_camera_to_slab(vars->slb_x, vars->slb_y, PLAYER0);

    // Three distinct subtile-centre positions inside one open slab.
    struct Coord3d pos;
    set_coords_to_slab_center(&pos, vars->slb_x, vars->slb_y);
    vars->light_x = pos.x.val; vars->light_y = pos.y.val;
    vars->ap_x = pos.x.val + 256; vars->ap_y = pos.y.val;
    vars->fx_x = pos.x.val; vars->fx_y = pos.y.val + 256;

    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open() did not mark the session active");
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_points_action002__create_points(struct FTestActionArgs* const args)
{
    struct ftest_editor_points__variables* const vars = args->data;

    if (editor_points_effectgen_kind_count() < 2)
    {
        FTEST_FAIL_TEST("Effect generator picker offers %" PRId64 " kind(s); config defines several", (int64_t)(editor_points_effectgen_kind_count()));
        return FTRs_Go_To_Next_Action;
    }

    struct EditorPointSnapshot light = { .kind = EPK_Light, .x = vars->light_x, .y = vars->light_y, .z = vars->light_z,
        .radius = vars->light_radius, .intensity = vars->light_intensity };
    struct EditorPointSnapshot ap = { .kind = EPK_ActionPoint, .id = TEST_AP_NUMBER, .x = vars->ap_x, .y = vars->ap_y,
        .radius = vars->ap_range };
    struct EditorPointSnapshot fx = { .kind = EPK_EffectGen, .x = vars->fx_x, .y = vars->fx_y,
        .radius = vars->fx_range, .model = vars->fx_model, .owner = PLAYER_NEUTRAL, .parent = -1 };

    if (!editor_points_create(&light) || !editor_points_create(&ap) || !editor_points_create(&fx))
    {
        FTEST_FAIL_TEST("editor_points_create() failed for one of light/action point/effect generator");
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_record_point(true, &light);
    editor_journal_record_point(true, &ap);
    editor_journal_record_point(true, &fx);

    if (!all_three_present(vars))
    {
        FTEST_FAIL_TEST("Created light/action point/effect generator are not all findable");
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_points_action003__undo_redo(struct FTestActionArgs* const args)
{
    struct ftest_editor_points__variables* const vars = args->data;

    for (int64_t i = 0; i < 3; i++)
        editor_journal_do_undo();
    if (!none_present(vars))
    {
        FTEST_FAIL_TEST("Undo x3 did not remove the light, action point and effect generator");
        return FTRs_Go_To_Next_Action;
    }
    for (int64_t i = 0; i < 3; i++)
        editor_journal_do_redo();
    if (!all_three_present(vars))
    {
        FTEST_FAIL_TEST("Redo x3 did not bring the light, action point and effect generator back");
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_points_action004__save_and_reload(struct FTestActionArgs* const args)
{
    struct ftest_editor_points__variables* const vars = args->data;

    vars->level_lights_before_save = count_level_lights();

    char full_path[512];
    editor_level_save_dir(TEST_SAVE_LVNUM, full_path, sizeof(full_path));
    if (!editor_save_map(TEST_SAVE_LVNUM, full_path, EdSaveFmt_ForceKeeperFX, "Editor Points Test", 1, 0, NULL))
    {
        FTEST_FAIL_TEST("editor_save_map() failed to save to %s", full_path);
        return FTRs_Go_To_Next_Action;
    }
    // load_map_file() alone doesn't reset anything (a real level start runs
    // clear_game()/init_level() first): without clearing, the old level's
    // things, lights and action points stay allocated next to the freshly
    // loaded ones, so "found after reload" could be the leftover original
    // and the light count would compare against a pile of duplicates. Do the
    // relevant part of that reset here.
    clear_things_and_persons_data();
    light_initialise();
    delete_all_action_point_structures();
    if (!load_map_file(TEST_SAVE_LVNUM))
    {
        FTEST_FAIL_TEST("load_map_file() failed to reload the level editor_save_map() just wrote");
    }
    return FTRs_Go_To_Next_Action;
}

FTestActionResult ftest_editor_points_action005__assert_reloaded(struct FTestActionArgs* const args)
{
    struct ftest_editor_points__variables* const vars = args->data;

    const struct Light* lgt = find_level_light_at(vars->light_x, vars->light_y);
    struct ActionPoint* apt = action_point_get_by_number(TEST_AP_NUMBER);
    struct Thing* fx = find_effectgen_at(vars->fx_x, vars->fx_y);
    if ((lgt == NULL) || !action_point_exists(apt) || (fx == NULL))
    {
        if (++vars->poll_count > 40)
        {
            FTEST_FAIL_TEST("After reload: light %s, action point %s, effect generator %s",
                (lgt != NULL) ? "found" : "MISSING", action_point_exists(apt) ? "found" : "MISSING",
                (fx != NULL) ? "found" : "MISSING");
            return FTRs_Go_To_Next_Action;
        }
        return FTRs_Repeat_Current_Action;
    }
    if ((lgt->radius != vars->light_radius) || (lgt->intensity != vars->light_intensity))
    {
        FTEST_FAIL_TEST("Reloaded light has radius %" PRId64 " intensity %" PRId64 ", expected %" PRId64 "/%" PRId64,
            (int64_t)lgt->radius, (int64_t)lgt->intensity, (int64_t)(vars->light_radius), (int64_t)(vars->light_intensity));
        return FTRs_Go_To_Next_Action;
    }
    if ((apt->range != vars->ap_range) || (apt->mappos.x.val != vars->ap_x) || (apt->mappos.y.val != vars->ap_y))
    {
        FTEST_FAIL_TEST("Reloaded action point range %" PRId64 " at (%" PRId64 ",%" PRId64 "), expected %" PRId64 " at (%" PRId64 ",%" PRId64 ")",
            (int64_t)apt->range, (int64_t)apt->mappos.x.val, (int64_t)apt->mappos.y.val, (int64_t)(vars->ap_range), (int64_t)(vars->ap_x), (int64_t)(vars->ap_y));
        return FTRs_Go_To_Next_Action;
    }
    if ((fx->model != vars->fx_model) || (fx->effect_generator.range != vars->fx_range))
    {
        FTEST_FAIL_TEST("Reloaded effect generator model %" PRId64 " range %" PRId64 ", expected %" PRId64 "/%" PRId64,
            (int64_t)fx->model, (int64_t)fx->effect_generator.range, (int64_t)(vars->fx_model), (int64_t)(vars->fx_range));
        return FTRs_Go_To_Next_Action;
    }
    int64_t after = count_level_lights();
    if (after != vars->level_lights_before_save)
    {
        FTEST_FAIL_TEST("Level-owned light count changed across save/reload: %" PRId64 " before, %" PRId64 " after",
            (int64_t)(vars->level_lights_before_save), (int64_t)(after));
    }
    return FTRs_Go_To_Next_Action;
}

#endif
