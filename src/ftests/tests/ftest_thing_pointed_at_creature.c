// The engine picks the creature under the pointer while it draws the creature's sprite
// (draw_single_keepersprite*() in engine_render.c, kfx_render_state.thing_pointed_at), and a picked creature of
// the local player shows its thought bubble. The unflipped rotating sprite (facing directions 0-4) compared the
// pointer with the sprite's offset in its frame instead of its place on the screen, so it was picked with the
// pointer near the top-left corner of the view and never under the pointer. Here a creature faces each of the 8
// directions in turn with the pointer over its body, then the pointer moves away.
#include "ftest_thing_pointed_at_creature.h"

#ifdef FUNCTESTING

#include "pre_inc.h"

#include "../ftest.h"
#include "../ftest_util.h"

#include "bflib_mouse.h"
#include "bflib_video.h"
#include "config_creature.h"
#include "config_keeperfx.h"
#include "engine_arrays.h"
#include "engine_render.h"
#include "keeper_sprites.h"
#include "kfx_render_state.h"
#include "kfx_sim_state.h"
#include "local_camera.h"
#include "local_state.h"
#include "map_data.h"
#include "player_data.h"
#include "slab_data.h"
#include "thing_creature.h"
#include "thing_data.h"
#include "thing_navigate.h"
#include "thing_stats.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FACINGS_COUNT 8
// Turns to wait for the frame that shows the pick, before failing.
#define TURNS_TO_PICK 40
// Turns the pointer stays away before checking the creature isn't picked.
#define TURNS_AWAY 4

static struct {
    MapSlabCoord slb_x;
    MapSlabCoord slb_y;
    struct Coord3d pos;
    struct Thing *unit;
    int64_t facing;
    int64_t turns;
    int64_t offscreen_turns;
    TbBool away;
} s_vars;

static TbBool creature_screen_pos(struct Thing *thing, int64_t *x, int64_t *y)
{
    int64_t scr_x;
    int64_t scr_y;
    // Half way up its body, well inside the sprite.
    if (!project_world_position_to_screen(thing->mappos.x.val, thing->mappos.y.val,
            thing->mappos.z.val + thing->clipbox_size_z / 2, &scr_x, &scr_y)) {
        return false;
    }
    *x = local_state.engine_window_x + scr_x * pixel_size;
    *y = local_state.engine_window_y + scr_y * pixel_size;
    return true;
}

static FTestActionResult ftest_thing_pointed_at_creature_action001__spawn(struct FTestActionArgs* const args)
{
    const ThingModel model = creature_model_id("TROLL");
    set_coords_to_slab_center(&s_vars.pos, s_vars.slb_x, s_vars.slb_y);
    s_vars.unit = ftest_util_create_creature(s_vars.pos.x.val, s_vars.pos.y.val, PLAYER0, 1, model);
    if (thing_is_invalid(s_vars.unit))
    {
        FTEST_FAIL_TEST("Failed to create creature %s", creature_code_name(model));
        return FTRs_Go_To_Next_Action;
    }
    s_vars.pos = s_vars.unit->mappos;
    // The drawn view is the local camera's: snap it to the synced one just moved.
    ftest_util_move_camera_to_thing(s_vars.unit, PLAYER0);
    sync_local_camera(get_player(PLAYER0));
    s_vars.facing = 0;
    s_vars.turns = 0;
    s_vars.offscreen_turns = 0;
    s_vars.away = false;
    return FTRs_Go_To_Next_Action;
}

static FTestActionResult ftest_thing_pointed_at_creature_action002__point(struct FTestActionArgs* const args)
{
    struct Thing *unit = s_vars.unit;
    if (thing_is_invalid(unit) || !thing_is_creature(unit))
    {
        FTEST_FAIL_TEST("The creature is gone");
        return FTRs_Go_To_Next_Action;
    }
    const int64_t rotable = keepersprite_rotable(get_render_animation_sprite(unit->anim_sprite));
    if (rotable != 2)
    {
        FTEST_FAIL_TEST("%s's sprite %" PRId64 " is Rotable %" PRId64 ", not 2", thing_model_name(unit),
            (int64_t)unit->anim_sprite, rotable);
        return FTRs_Go_To_Next_Action;
    }
    // What the frame drawn since the last call picked, with the pointer and facing that call set.
    struct Thing *picked = kfx_render_state.thing_pointed_at;
    const struct Camera *cam = get_local_active_camera(get_my_player());
    const int64_t angle = s_vars.facing * DEGREES_45;
    const int64_t sprite_angle = (angle - cam->rotation_angle_x) & ANGLE_MASK;
    if (s_vars.turns > 0)
    {
        if (!s_vars.away)
        {
            if (picked == unit)
            {
                FTESTLOG("facing %" PRId64 " (sprite angle %" PRId64 ", %s): picked after %" PRId64 " turns",
                    s_vars.facing, sprite_angle, ((sprite_angle > 1151) && (sprite_angle < 1919)) ? "flipped" : "unflipped",
                    s_vars.turns);
                s_vars.facing++;
                s_vars.turns = 0;
                if (s_vars.facing >= FACINGS_COUNT) {
                    s_vars.away = true;
                }
            }
            else if (s_vars.turns >= TURNS_TO_PICK)
            {
                FTEST_FAIL_TEST("facing %" PRId64 " (sprite angle %" PRId64 "): pointer over %s %" PRId64 " for %" PRId64 " turns, picked %s %" PRId64,
                    s_vars.facing, sprite_angle, thing_model_name(unit), (int64_t)unit->index, s_vars.turns,
                    thing_is_invalid(picked) ? "nothing" : thing_model_name(picked), thing_is_invalid(picked) ? (int64_t)0 : (int64_t)picked->index);
                return FTRs_Go_To_Next_Action;
            }
        }
        else if (s_vars.turns >= TURNS_AWAY)
        {
            if (picked == unit)
            {
                FTEST_FAIL_TEST("pointer away from %s %" PRId64 ", still picked", thing_model_name(unit), (int64_t)unit->index);
            } else {
                FTESTLOG("pointer away: not picked");
            }
            return FTRs_Go_To_Next_Action;
        }
    }
    // Keep the creature where it was put, facing this direction, for the next frame.
    move_thing_in_map(unit, &s_vars.pos);
    unit->move_angle_xy = s_vars.facing < FACINGS_COUNT ? s_vars.facing * DEGREES_45 : 0;
    int64_t x;
    int64_t y;
    if (!creature_screen_pos(unit, &x, &y))
    {
        // The view settles on the camera's position over the level's first turns.
        if (++s_vars.offscreen_turns >= TURNS_TO_PICK)
        {
            FTEST_FAIL_TEST("%s %" PRId64 " at (%" PRId64 ",%" PRId64 ") isn't on the screen, camera at (%" PRId64 ",%" PRId64 ")",
                thing_model_name(unit), (int64_t)unit->index, (int64_t)unit->mappos.x.val, (int64_t)unit->mappos.y.val,
                (int64_t)cam->mappos.x.val, (int64_t)cam->mappos.y.val);
            return FTRs_Go_To_Next_Action;
        }
        s_vars.turns = 0;
        return FTRs_Repeat_Current_Action;
    }
    if (s_vars.away)
    {
        // A quarter of the view to the side and down: off the creature, and not the top-left corner either.
        x += (local_state.engine_window_width / 4) * ((x < local_state.engine_window_x + local_state.engine_window_width / 2) ? 1 : -1);
        y += local_state.engine_window_height / 4;
    }
    LbMouseSetPosition(x, y);
    s_vars.turns++;
    return FTRs_Repeat_Current_Action;
}

TbBool ftest_thing_pointed_at_creature_init()
{
    s_vars.slb_x = 24;
    s_vars.slb_y = 62;
    s_vars.unit = INVALID_THING;
    ftest_util_reveal_map(PLAYER0);
    // An empty room of claimed floor, so nothing else is under the pointer
    ftest_util_replace_slabs(s_vars.slb_x - 6, s_vars.slb_y - 6, s_vars.slb_x + 6, s_vars.slb_y + 6, SlbT_WALLDRAPE, PLAYER0);
    ftest_util_replace_slabs(s_vars.slb_x - 5, s_vars.slb_y - 5, s_vars.slb_x + 5, s_vars.slb_y + 5, SlbT_CLAIMED, PLAYER0);

    ftest_append_action(ftest_thing_pointed_at_creature_action001__spawn, 10, NULL);
    ftest_append_action(ftest_thing_pointed_at_creature_action002__point, 5, NULL);
    return true;
}

#ifdef __cplusplus
}
#endif

#endif // FUNCTESTING
