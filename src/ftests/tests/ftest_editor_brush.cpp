#include "ftest_editor_brush.h"

#ifdef FUNCTESTING

extern "C" {
#include "pre_inc.h"
#include "../ftest.h"
#include "../ftest_util.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "kfx_editor.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "thing_list.h"
#include "thing_creature.h"
#include "config_creature.h"
#include "player_instances.h"
#include "post_inc.h"
}
#include "editor_brush.h"
#include "editor_journal.h"
#include "editor_points.h"
#include "editor_query.h"
#include "editor_things.h"

extern "C" {

FTestActionResult ftest_editor_brush_action001__all(struct FTestActionArgs* const args);

static int64_t s_unused;

TbBool ftest_editor_brush_init()
{
    ftest_append_action(ftest_editor_brush_action001__all, 20, &s_unused);
    return true;
}

static int64_t count_traps_in_slab(MapSlabCoord sx, MapSlabCoord sy)
{
    int64_t n = 0;
    for (int64_t dy = 0; dy < 3; dy++)
        for (int64_t dx = 0; dx < 3; dx++)
            if (!thing_is_invalid(find_base_thing_on_mapwho(TCls_Trap, 0, slab_subtile(sx, 0) + dx, slab_subtile(sy, 0) + dy)))
                n++;
    return n;
}

FTestActionResult ftest_editor_brush_action001__all(struct FTestActionArgs* const args)
{
    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open did not activate the session");
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_reset();
    editor_brush_clear();

    // A 2x2 source area at (60..61, 60..61): gold and earth slabs, two traps in
    // one slab, a creature, a light.
    const MapSlabCoord sx = 60, sy = 60;
    for (int64_t dy = 0; dy < 2; dy++)
        for (int64_t dx = 0; dx < 2; dx++)
        {
            place_slab_type_on_map(((dx + dy) & 1) ? SlbT_GOLD : SlbT_CLAIMED, slab_subtile(sx + dx, 0), slab_subtile(sy + dy, 0), PLAYER0, 0);
            do_slab_efficiency_alteration(sx + dx, sy + dy);
        }
    player_place_trap_at_subtile_without_check(slab_subtile(sx, 0), slab_subtile(sy, 0), PLAYER0, 1, true);
    player_place_trap_at_subtile_without_check(slab_subtile(sx, 0) + 2, slab_subtile(sy, 0) + 2, PLAYER0, 1, true);
    struct Coord3d cp;
    set_coords_to_slab_center(&cp, sx, sy);
    ThingModel model = 0;
    for (ThingModel m = 1; m < kfx_config_state.conf.crtr_conf.model_count && model == 0; m++)
        if ((creature_stats_get(m)->model_flags & CMF_IsSpectator) == 0)
            model = m;
    create_creature(&cp, model, PLAYER0);
    EditorPointSnapshot light;
    memset(&light, 0, sizeof(light));
    light.kind = EPK_Light;
    light.x = sx * 768 + 200; light.y = sy * 768 + 200; light.z = 384; light.radius = 1280; light.intensity = 40;
    editor_points_create(&light);

    editor_brush_capture(sx, sy, sx + 1, sy + 1);
    if (editor_brush_slab_count() != 4 || editor_brush_thing_count() < 3 || editor_brush_point_count() < 1)
    {
        FTEST_FAIL_TEST("capture found %zu slabs, %zu things, %zu points", editor_brush_slab_count(),
            editor_brush_thing_count(), editor_brush_point_count());
        return FTRs_Go_To_Next_Action;
    }

    // Stamp at (80, 60).
    const MapSlabCoord tx = 80, ty = 60;
    const int64_t undo_before = editor_journal_undo_count();
    editor_brush_stamp(tx, ty);
    if (editor_journal_undo_count() != undo_before + 2) // one for the stamp, one for the light
    {
        FTEST_FAIL_TEST("stamp made %" PRId64 " journal entries", (int64_t)(editor_journal_undo_count() - undo_before));
        return FTRs_Go_To_Next_Action;
    }
    if (get_slabmap_block(tx + 1, ty)->kind != SlbT_GOLD || get_slabmap_block(tx, ty)->kind != SlbT_CLAIMED)
    {
        FTEST_FAIL_TEST("stamped slabs are not the captured ones");
        return FTRs_Go_To_Next_Action;
    }
    if (count_traps_in_slab(tx, ty) != 2 || thing_is_invalid(find_base_thing_on_mapwho(TCls_Trap, 0, slab_subtile(tx, 0) + 2, slab_subtile(ty, 0) + 2)))
    {
        FTEST_FAIL_TEST("the two subtile-placed traps were not both stamped");
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_do_undo(); // the stamp itself (the light is undone first: it was pushed last)
    editor_journal_do_undo();
    if (get_slabmap_block(tx + 1, ty)->kind == SlbT_GOLD && get_slabmap_block(tx, ty)->kind == SlbT_CLAIMED
        && !thing_is_invalid(find_base_thing_on_mapwho(TCls_Trap, 0, slab_subtile(tx, 0), slab_subtile(ty, 0))))
    {
        FTEST_FAIL_TEST("undo did not remove the stamped area");
        return FTRs_Go_To_Next_Action;
    }

    // Query and delete at the source creature's position.
    ThingIndex cidx = 0;
    EditorQueryResult r = editor_query_at(&cp, &cidx);
    if (cidx == 0)
    {
        FTEST_FAIL_TEST("query at a creature did not report the creature");
        return FTRs_Go_To_Next_Action;
    }
    (void)r;
    struct Coord3d trap_pos;
    trap_pos.x.val = subtile_coord_center(slab_subtile(sx, 0));
    trap_pos.y.val = subtile_coord_center(slab_subtile(sy, 0));
    trap_pos.z.val = 0;
    EditorQueryResult tr = editor_query_at(&trap_pos, &cidx);
    JUSTLOG("Query at a trap position: kind %" PRId64, (int64_t)tr.kind); // hidden traps may not be selectable
    if (!editor_delete_thing_at(&cp))
    {
        FTEST_FAIL_TEST("editor_delete_thing_at found no creature");
        return FTRs_Go_To_Next_Action;
    }
    if (!thing_is_invalid(get_creature_near(cp.x.val, cp.y.val)) && thing_is_creature(get_creature_near(cp.x.val, cp.y.val)))
    {
        FTEST_FAIL_TEST("the creature is still there after deleting it");
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_do_undo();
    struct Thing* back = get_creature_near(cp.x.val, cp.y.val);
    if (thing_is_invalid(back) || !thing_is_creature(back))
    {
        FTEST_FAIL_TEST("undo did not bring the deleted creature back");
        return FTRs_Go_To_Next_Action;
    }
    JUSTLOG("Brush / delete / query: ok");
    return FTRs_Go_To_Next_Action;
}

} // extern "C"

#endif // FUNCTESTING
