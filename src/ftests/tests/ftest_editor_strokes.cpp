#include "ftest_editor_strokes.h"

#ifdef FUNCTESTING

extern "C" {
#include "pre_inc.h"
#include "../ftest.h"
#include "../ftest_util.h"
#include "game_legacy.h"
#include "config_keeperfx.h"
#include "config.h"
#include "kfx_editor.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "lvl_script_commands.h"
#include "bflib_fileio.h"
#include "post_inc.h"
}
#include "editor_journal.h"
#include "editor_reinforce.h"
#include "editor_points.h"
#include "editor_texture_paint.h"
#include "editor_texture_packs.h"
#include "engine_textures.h"
#include "player_instances.h"
#include "room_util.h"
#include "map_columns.h"
#include "packets.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "thing_navigate.h"
#include "config_terrain.h"
#include "config_trapdoor.h"
#include "thing_doors.h"
#include "thing_list.h"
#include "config_creature.h"
#include "editor_script_validate.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

extern "C" {

FTestActionResult ftest_editor_strokes_action001__strokes(struct FTestActionArgs* const args);

static int s_unused;

TbBool ftest_editor_strokes_init()
{
    ftest_append_action(ftest_editor_strokes_action001__strokes, 20, &s_unused);
    return true;
}

static const char* lookup_args(const std::string& name)
{
    for (int i = 0; command_desc[i].textptr != NULL; i++)
        if (name == command_desc[i].textptr)
            return command_desc[i].args;
    return nullptr;
}

FTestActionResult ftest_editor_strokes_action001__strokes(struct FTestActionArgs* const args)
{
    editor_open(1, false);
    if (!editor_is_active())
    {
        FTEST_FAIL_TEST("editor_open did not activate the session");
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_reset();

    // --- stroke-level undo/redo over three slabs -------------------------
    const MapSlabCoord sx = 30, sy = 30;
    struct SlabMap* slb[3] = { get_slabmap_block(sx, sy), get_slabmap_block(sx + 1, sy), get_slabmap_block(sx + 2, sy) };
    SlabKind kind_before[3];
    for (int i = 0; i < 3; i++)
        kind_before[i] = slb[i]->kind;
    const SlabKind painted = (kind_before[0] == SlbT_GOLD) ? SlbT_EARTH : SlbT_GOLD;

    editor_journal_stroke_begin();
    for (int i = 0; i < 3; i++)
    {
        place_slab_type_on_map(painted, slab_subtile(sx + i, 0), slab_subtile(sy, 0), PLAYER0, 0);
        do_slab_efficiency_alteration(sx + i, sy);
    }
    if (!editor_journal_stroke_end("Test paint"))
    {
        FTEST_FAIL_TEST("stroke_end recorded nothing for a real change");
        return FTRs_Go_To_Next_Action;
    }
    if (editor_journal_undo_count() != 1)
    {
        FTEST_FAIL_TEST("expected one undo entry, found %d", editor_journal_undo_count());
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_do_undo();
    for (int i = 0; i < 3; i++)
    {
        if (get_slabmap_block(sx + i, sy)->kind != kind_before[i])
        {
            FTEST_FAIL_TEST("undo did not restore slab %d", i);
            return FTRs_Go_To_Next_Action;
        }
    }
    editor_journal_do_redo();
    for (int i = 0; i < 3; i++)
    {
        if (get_slabmap_block(sx + i, sy)->kind != painted)
        {
            FTEST_FAIL_TEST("redo did not reapply slab %d", i);
            return FTRs_Go_To_Next_Action;
        }
    }
    editor_journal_do_undo(); // leave the map as found

    // A stroke that changes nothing must not journal.
    const int before_count = editor_journal_undo_count();
    editor_journal_stroke_begin();
    if (editor_journal_stroke_end("Nothing") || editor_journal_undo_count() != before_count)
    {
        FTEST_FAIL_TEST("an empty stroke was journaled");
        return FTRs_Go_To_Next_Action;
    }

    // --- reinforce perimeter ---------------------------------------------
    // Claimed floor next to earth: one owned floor slab inside an earth block.
    const MapSlabCoord rx = 40, ry = 40;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
        {
            place_slab_type_on_map(SlbT_EARTH, slab_subtile(rx + dx, 0), slab_subtile(ry + dy, 0), kfx_config_state.neutral_player_num, 0);
            do_slab_efficiency_alteration(rx + dx, ry + dy);
        }
    place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(rx, 0), slab_subtile(ry, 0), PLAYER0, 0);
    do_slab_efficiency_alteration(rx, ry);
    const int changed = editor_reinforce_perimeter(PLAYER0);
    if (changed < 4)
    {
        FTEST_FAIL_TEST("reinforce changed %d slabs, expected at least the 4 neighbours", changed);
        return FTRs_Go_To_Next_Action;
    }
    if (get_slabmap_block(rx + 1, ry)->kind == SlbT_EARTH)
    {
        FTEST_FAIL_TEST("neighbour of owned floor is still earth after reinforcing");
        return FTRs_Go_To_Next_Action;
    }
    editor_journal_do_undo();
    // Placing earth may pick its torch variant, so either counts as restored.
    if (get_slabmap_block(rx + 1, ry)->kind != SlbT_EARTH && get_slabmap_block(rx + 1, ry)->kind != SlbT_TORCHDIRT)
    {
        FTEST_FAIL_TEST("undo did not put the earth back (kind %d, undo entries %d)", (int)get_slabmap_block(rx + 1, ry)->kind, editor_journal_undo_count());
        return FTRs_Go_To_Next_Action;
    }

    // A room floor counts as owned ground too.
    {
        const MapSlabCoord qx = 50, qy = 50;
        for (int dy = -1; dy <= 1; dy++)
            for (int dx = -1; dx <= 1; dx++)
            {
                place_slab_type_on_map(SlbT_EARTH, slab_subtile(qx + dx, 0), slab_subtile(qy + dy, 0), kfx_config_state.neutral_player_num, 0);
                do_slab_efficiency_alteration(qx + dx, qy + dy);
            }
        place_slab_type_on_map(SlbT_TREASURE, slab_subtile(qx, 0), slab_subtile(qy, 0), PLAYER0, 0);
        do_slab_efficiency_alteration(qx, qy);
        if (editor_reinforce_perimeter(PLAYER0) < 4 || get_slabmap_block(qx + 1, qy)->kind == SlbT_EARTH)
        {
            FTEST_FAIL_TEST("reinforce ignored the earth around a room floor");
            return FTRs_Go_To_Next_Action;
        }
    }

    // --- deleting things is undoable ------------------------------------------
    {
        struct Coord3d cp;
        set_coords_to_slab_center(&cp, 70, 70);
        ThingModel model = 0;
        for (ThingModel m = 1; m < kfx_config_state.conf.crtr_conf.model_count && model == 0; m++)
            if ((creature_stats_get(m)->model_flags & CMF_IsSpectator) == 0)
                model = m;
        struct Thing* c = create_creature(&cp, model, PLAYER0);
        if (thing_is_invalid(c))
        {
            FTEST_FAIL_TEST("could not create a creature to delete");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_stroke_begin();
        destroy_object(c);
        if (!editor_journal_stroke_end("Test delete"))
        {
            FTEST_FAIL_TEST("deleting a creature was not journaled");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_do_undo();
        struct Thing* back = get_creature_near(cp.x.val, cp.y.val);
        if (thing_is_invalid(back) || !thing_is_creature(back) || back->model != model)
        {
            FTEST_FAIL_TEST("undo did not bring the deleted creature back");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_do_redo();
        back = get_creature_near(cp.x.val, cp.y.val);
        if (!thing_is_invalid(back) && thing_is_creature(back) && back->model == model)
        {
            FTEST_FAIL_TEST("redo did not delete the creature again");
            return FTRs_Go_To_Next_Action;
        }
    }

    // --- Fill --------------------------------------------------------------
    {
        // A 3x3 pocket of earth inside the block from the reinforce test's row.
        const MapSlabCoord fx = 20, fy = 20;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
            {
                const bool inner = (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
                place_slab_type_on_map(inner ? SlbT_EARTH : SlbT_ROCK, slab_subtile(fx + dx, 0), slab_subtile(fy + dy, 0),
                    kfx_config_state.neutral_player_num, 0);
                do_slab_efficiency_alteration(fx + dx, fy + dy);
            }
        editor_flood_fill_terrain(fx, fy, SlbT_GOLD, kfx_config_state.neutral_player_num);
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
            {
                const bool inner = (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
                const SlabKind k = get_slabmap_block(fx + dx, fy + dy)->kind;
                if (inner ? (k != SlbT_GOLD) : (k != SlbT_ROCK))
                {
                    FTEST_FAIL_TEST("fill: slab (%d,%d) is kind %d", (int)(fx + dx), (int)(fy + dy), (int)k);
                    return FTRs_Go_To_Next_Action;
                }
            }
    }

    // --- several traps in one slab (subtile placement), rooms repainted cleanly --
    {
        const MapSlabCoord tx = 12, ty = 12;
        place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(tx, 0), slab_subtile(ty, 0), PLAYER0, 0);
        do_slab_efficiency_alteration(tx, ty);
        const MapSubtlCoord bx = slab_subtile(tx, 0), by = slab_subtile(ty, 0);
        if (!player_place_trap_at_subtile_without_check(bx, by, PLAYER0, 1, true)
            || !player_place_trap_at_subtile_without_check(bx + 2, by + 1, PLAYER0, 1, true))
        {
            FTEST_FAIL_TEST("could not place two traps in one slab");
            return FTRs_Go_To_Next_Action;
        }
        if (thing_is_invalid(find_base_thing_on_mapwho(TCls_Trap, 0, bx, by))
            || thing_is_invalid(find_base_thing_on_mapwho(TCls_Trap, 0, bx + 2, by + 1)))
        {
            FTEST_FAIL_TEST("the two traps did not land on their own subtiles");
            return FTRs_Go_To_Next_Action;
        }
        // A room painted over a room: same result as on fresh ground.
        auto paint = [&](int px, int py, SlabKind k) {
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++)
                    place_slab_type_replacing_room(k, (MapSlabCoord)(px + dx), (MapSlabCoord)(py + dy), PLAYER0);
        };
        auto earth = [&](int px, int py) {
            for (int dy = -3; dy <= 3; dy++)
                for (int dx = -3; dx <= 3; dx++)
                {
                    place_slab_type_on_map(SlbT_EARTH, slab_subtile(px + dx, 0), slab_subtile(py + dy, 0), kfx_config_state.neutral_player_num, 0);
                    do_slab_efficiency_alteration(px + dx, py + dy);
                }
        };
        earth(30, 30); earth(50, 30);
        paint(30, 30, SlbT_TEMPLE);
        paint(50, 30, SlbT_TREASURE);
        paint(50, 30, SlbT_TEMPLE);
        for (int psy = -6; psy < 9; psy++)
            for (int psx = -6; psx < 9; psx++)
            {
                const struct Map* m1 = get_map_block_at(slab_subtile(30, 0) + psx, slab_subtile(30, 0) + psy);
                const struct Map* m2 = get_map_block_at(slab_subtile(50, 0) + psx, slab_subtile(30, 0) + psy);
                if (memcmp(get_map_column(m1), get_map_column(m2), sizeof(struct Column)) != 0)
                {
                    FTEST_FAIL_TEST("a room painted over a room differs from a fresh one at subtile offset (%d,%d)", psx, psy);
                    return FTRs_Go_To_Next_Action;
                }
            }
    }

    // --- texture pack discovery ---------------------------------------------------
    {
        const EditorTexturePackChoice* choices = nullptr;
        const int n = editor_texture_pack_choices(1, &choices);
        if (n < 15 || choices[0].id != 0 || choices[1].id != 1)
        {
            FTEST_FAIL_TEST("texture pack list has %d entries, expected the 15 built-in ones first", n);
            return FTRs_Go_To_Next_Action;
        }
        if (!texture_pack_available(1, 1, get_level_fgroup(1)) || texture_pack_available(250, 1, get_level_fgroup(1)))
        {
            FTEST_FAIL_TEST("texture_pack_available() is wrong for a stock pack or a missing one");
            return FTRs_Go_To_Next_Action;
        }
        JUSTLOG("Texture packs offered: %d", n);
    }

    // --- resize preview -------------------------------------------------------------
    {
        int t = -1, l = -1, a = -1;
        if (!editor_resize_preview(40, 40, 0, &t, &l, &a) || t <= 0)
        {
            FTEST_FAIL_TEST("shrinking the level to 40 x 40 should report things that would be lost (got %d)", t);
            return FTRs_Go_To_Next_Action;
        }
        if (editor_resize_preview(4, 4, 0, &t, &l, &a))
        {
            FTEST_FAIL_TEST("a 4 x 4 map should be refused");
            return FTRs_Go_To_Next_Action;
        }
    }

    // --- texture painting: rectangle and fill ---------------------------------------
    {
        editor_texture_paint_rect(5, 5, 7, 6, 5);
        int inside = 0;
        for (int y = 5; y <= 6; y++)
            for (int x = 5; x <= 7; x++)
                inside += (kfx_config_state.slab_ext_data[get_slab_number(x, y)] == 5);
        if (inside != 6 || kfx_config_state.slab_ext_data[get_slab_number(8, 5)] == 5)
        {
            FTEST_FAIL_TEST("texture rectangle painted %d of 6 slabs (or spilled)", inside);
            return FTRs_Go_To_Next_Action;
        }
        // Fill: a pocket of earth inside rock.
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++)
            {
                const bool pocket = (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1);
                place_slab_type_on_map(pocket ? SlbT_EARTH : SlbT_ROCK, slab_subtile(40 + dx, 0), slab_subtile(75 + dy, 0), kfx_config_state.neutral_player_num, 0);
                do_slab_efficiency_alteration(40 + dx, 75 + dy);
                kfx_config_state.slab_ext_data[get_slab_number(40 + dx, 75 + dy)] = 0;
            }
        const int n = editor_texture_paint_fill(40, 75, 9);
        if (n != 9 || kfx_config_state.slab_ext_data[get_slab_number(38, 75)] == 9)
        {
            FTEST_FAIL_TEST("texture fill changed %d slabs, expected the 9-slab pocket", n);
            return FTRs_Go_To_Next_Action;
        }
    }

    // --- Stamp captures and re-places points -------------------------------
    {
        EditorPointSnapshot light;
        std::memset(&light, 0, sizeof(light));
        light.kind = EPK_Light;
        light.x = 60 * 256 + 128; light.y = 60 * 256 + 128; light.z = 384;
        light.radius = 1280; light.intensity = 40;
        EditorPointSnapshot apt;
        std::memset(&apt, 0, sizeof(apt));
        apt.kind = EPK_ActionPoint;
        apt.id = 0; apt.x = 61 * 256; apt.y = 60 * 256; apt.radius = 512;
        if (!editor_points_create(&light) || !editor_points_create(&apt))
        {
            FTEST_FAIL_TEST("could not create the points to capture");
            return FTRs_Go_To_Next_Action;
        }
        EditorPointSnapshot got[16];
        const int n = editor_points_capture_in_box(60 * 256, 60 * 256, 66 * 256, 63 * 256, got, 16);
        if (n < 2)
        {
            FTEST_FAIL_TEST("capture found %d points, expected the light and the action point", n);
            return FTRs_Go_To_Next_Action;
        }
        int stamped = 0;
        for (int i = 0; i < n; i++)
            if (editor_points_stamp(&got[i], 30 * 256, 0))
                stamped++;
        EditorPointSnapshot again[16];
        const int m = editor_points_capture_in_box(90 * 256, 60 * 256, 96 * 256, 63 * 256, again, 16);
        if (stamped < 2 || m < 2)
        {
            FTEST_FAIL_TEST("stamped %d, found %d at the target", stamped, m);
            return FTRs_Go_To_Next_Action;
        }
    }

    // --- property edits are undoable ---------------------------------------
    {
        // A point's inspector edit (action point in place, light recreated).
        EditorPointSnapshot apt, light;
        std::memset(&apt, 0, sizeof(apt));
        apt.kind = EPK_ActionPoint;
        apt.x = 40 * 256; apt.y = 40 * 256; apt.radius = 512;
        std::memset(&light, 0, sizeof(light));
        light.kind = EPK_Light;
        light.x = 42 * 256 + 128; light.y = 40 * 256 + 128; light.z = 384; light.radius = 1280; light.intensity = 40;
        if (!editor_points_create(&apt) || !editor_points_create(&light))
        {
            FTEST_FAIL_TEST("could not create the points to edit");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_reset();
        EditorPointSnapshot* pts[2] = { &apt, &light };
        for (int k = 0; k < 2; k++)
        {
            EditorPointSnapshot before = *pts[k], after = *pts[k];
            after.x += 3 * 256;
            after.radius += 256;
            if (!editor_points_replace(&before, &after))
            {
                FTEST_FAIL_TEST("point edit %d failed", k);
                return FTRs_Go_To_Next_Action;
            }
            editor_journal_record_point_edit(&before, &after);
        }
        EditorPointSnapshot got[8];
        editor_journal_do_undo();
        editor_journal_do_undo();
        int n = editor_points_capture_in_box(40 * 256, 40 * 256, 43 * 256, 41 * 256, got, 8);
        if (n != 2)
        {
            FTEST_FAIL_TEST("after undoing both edits %d points sit at the original spots, expected 2", n);
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_do_redo();
        editor_journal_do_redo();
        n = editor_points_capture_in_box(43 * 256, 40 * 256, 47 * 256, 41 * 256, got, 8);
        if (n != 2)
        {
            FTEST_FAIL_TEST("after redoing both edits %d points sit at the moved spots, expected 2", n);
            return FTRs_Go_To_Next_Action;
        }
    }
    {
        // A gold pile's position and amount.
        struct Coord3d gp;
        set_coords_to_slab_center(&gp, 72, 72);
        struct Thing* gold = NULL;
        for (ThingModel m : { (ThingModel)ObjMdl_Goldl, (ThingModel)ObjMdl_GoldBag, (ThingModel)ObjMdl_GoldChest, (ThingModel)ObjMdl_GoldPot })
        {
            gold = create_object(&gp, m, PLAYER0, -1);
            if (!thing_is_invalid(gold) && object_is_gold(gold))
                break;
            if (!thing_is_invalid(gold))
                destroy_object(gold);
            gold = NULL;
        }
        if (gold == NULL)
        {
            FTEST_FAIL_TEST("could not create a gold object");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_reset();
        EditorThingProps before, after;
        editor_journal_thing_props(gold->index, &before);
        after = before;
        after.gold = before.gold + 123;
        after.x = before.x + 256;
        editor_journal_record_thing_edit(gold->index, &before, &after);
        struct Coord3d np = gold->mappos;
        np.x.val = (MapCoord)after.x;
        move_thing_in_map(gold, &np);
        gold->valuable.gold_stored = after.gold;
        editor_journal_do_undo();
        if (gold->valuable.gold_stored != before.gold || gold->mappos.x.val != before.x)
        {
            FTEST_FAIL_TEST("undo did not restore the gold pile (amount %ld, x %ld)", (long)gold->valuable.gold_stored, (long)gold->mappos.x.val);
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_do_redo();
        if (gold->valuable.gold_stored != after.gold || gold->mappos.x.val != after.x)
        {
            FTEST_FAIL_TEST("redo did not reapply the gold pile edit");
            return FTRs_Go_To_Next_Action;
        }
        destroy_object(gold);
    }
    {
        // A wounded creature comes back wounded after its deletion is undone.
        struct Coord3d cp;
        set_coords_to_slab_center(&cp, 74, 70);
        ThingModel model = 0;
        for (ThingModel m = 1; m < kfx_config_state.conf.crtr_conf.model_count && model == 0; m++)
            if ((creature_stats_get(m)->model_flags & CMF_IsSpectator) == 0)
                model = m;
        struct Thing* c = create_creature(&cp, model, PLAYER0);
        if (thing_is_invalid(c))
        {
            FTEST_FAIL_TEST("could not create a creature to wound");
            return FTRs_Go_To_Next_Action;
        }
        const short wounded = (short)(c->health / 2);
        c->health = wounded;
        editor_journal_reset();
        editor_journal_stroke_begin();
        destroy_object(c);
        editor_journal_stroke_end("Test delete wounded");
        editor_journal_do_undo();
        struct Thing* back = get_creature_near(cp.x.val, cp.y.val);
        if (thing_is_invalid(back) || back->health != wounded)
        {
            FTEST_FAIL_TEST("the restored creature has health %d, expected %d", thing_is_invalid(back) ? -1 : (int)back->health, (int)wounded);
            return FTRs_Go_To_Next_Action;
        }
    }

    {
        // A door's lock toggle, and a locked door restored locked.
        const MapSlabCoord dx = 76, dy = 70;
        // A door needs a corridor: rock either side, path along it.
        for (int k = -1; k <= 1; k++)
        {
            place_slab_type_on_map(SlbT_ROCK, slab_subtile(dx + k, 0), slab_subtile(dy - 1, 0), PLAYER0, 0);
            place_slab_type_on_map(SlbT_ROCK, slab_subtile(dx + k, 0), slab_subtile(dy + 1, 0), PLAYER0, 0);
        }
        place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(dx - 1, 0), slab_subtile(dy, 0), PLAYER0, 0);
        place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(dx + 1, 0), slab_subtile(dy, 0), PLAYER0, 0);
        place_slab_type_on_map(SlbT_CLAIMED, slab_subtile(dx, 0), slab_subtile(dy, 0), PLAYER0, 0);
        for (int k = -2; k <= 2; k++)
            for (int m = -2; m <= 2; m++)
                do_slab_efficiency_alteration(dx + k, dy + m);
        if (!player_place_door_without_check_at(slab_subtile(dx, 1), slab_subtile(dy, 1), PLAYER0, 1, true))
        {
            FTEST_FAIL_TEST("could not place a door to lock");
            return FTRs_Go_To_Next_Action;
        }
        struct Thing* door = find_base_thing_on_mapwho(TCls_Door, 1, slab_subtile(dx, 1), slab_subtile(dy, 1));
        if (thing_is_invalid(door))
        {
            FTEST_FAIL_TEST("the placed door was not found");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_reset();
        const ThingIndex di = door->index;
        editor_journal_record_door_lock(di, door->door.is_locked != 0);
        lock_door(door);
        editor_journal_do_undo();
        if (thing_get(di)->door.is_locked)
        {
            FTEST_FAIL_TEST("undo did not unlock the door");
            return FTRs_Go_To_Next_Action;
        }
        editor_journal_do_redo();
        if (!thing_get(di)->door.is_locked)
        {
            FTEST_FAIL_TEST("redo did not lock the door again");
            return FTRs_Go_To_Next_Action;
        }
        // Delete the locked door, undo: comes back locked. The door is in a
        // corridor (rock either side), so it has a valid orientation.
        if (!slab_kind_is_door(get_slabmap_block(dx, dy)->kind))
        {
            FTEST_FAIL_TEST("the placed door did not turn its slab into a door slab (kind %d)", (int)get_slabmap_block(dx, dy)->kind);
            return FTRs_Go_To_Next_Action;
        }
        {
            editor_journal_reset();
            editor_journal_stroke_begin();
            destroy_door(thing_get(di));
            {
                const struct SlabMap* dslb = get_slabmap_block(dx, dy);
                if (dslb->kind != SlbT_CLAIMED || slabmap_owner(dslb) != PLAYER0)
                {
                    FTEST_FAIL_TEST("deleting a door left slab kind %d (%s) owner %d, expected the claimed path owned by the door's owner",
                        (int)dslb->kind, slab_code_name(dslb->kind), (int)slabmap_owner(dslb));
                    return FTRs_Go_To_Next_Action;
                }
            }
            editor_journal_stroke_end("Test delete door");
            editor_journal_do_undo();
            struct Thing* back = find_base_thing_on_mapwho(TCls_Door, 1, slab_subtile(dx, 1), slab_subtile(dy, 1));
            if (thing_is_invalid(back) || !back->door.is_locked)
            {
                FTEST_FAIL_TEST("the restored door is %s", thing_is_invalid(back) ? "missing" : "not locked");
                return FTRs_Go_To_Next_Action;
            }
        }
    }

    // --- script validator on the level's real script -----------------------
    char* fname = prepare_file_fmtpath(FGrp_CmpgLvls, "map%05lu.txt", 1UL);
    std::ifstream in(fname);
    if (in)
    {
        std::stringstream ss;
        ss << in.rdbuf();
        int errors = 0;
        std::string first;
        for (const ScriptIssue& is : editor_script_validate(ss.str(), lookup_args))
        {
            if (is.severity == ScrIssue_Error)
            {
                if (errors++ < 6)
                    first += " [line " + std::to_string(is.line + 1) + ": " + is.message + "]";
            }
        }
        JUSTLOG("Script validate: %d errors in level 1 script%s%s", errors, errors ? "; first " : "", first.c_str());
        if (errors != 0)
        {
            FTEST_FAIL_TEST("validator reports %d errors on a shipped script (%s)", errors, first.c_str());
            return FTRs_Go_To_Next_Action;
        }
    }
    else
        JUSTLOG("Script validate: no script file at %s, skipped", fname);

    // Every script in the campaigns folder: the validator must not cry wolf.
    {
        int files = 0, bad_files = 0, total_errors = 0;
        std::string samples;
        for (const auto& ent : std::filesystem::recursive_directory_iterator("../../core_files"))
        {
            const std::string fn = ent.path().filename().string();
            if (!ent.is_regular_file() || fn.size() != 12 || fn.compare(0, 3, "map") != 0 || fn.substr(fn.size() - 4) != ".txt")
                continue;
            std::ifstream f(ent.path());
            std::stringstream ss;
            ss << f.rdbuf();
            files++;
            int e = 0;
            for (const ScriptIssue& is : editor_script_validate(ss.str(), lookup_args))
                if (is.severity == ScrIssue_Error)
                {
                    if (e++ == 0 && samples.size() < 900)
                        samples += " [" + fn + ":" + std::to_string(is.line + 1) + " " + is.message + "]";
                }
            if (e) { bad_files++; total_errors += e; }
        }
        if (files > 0 && bad_files * 20 > files)
        {
            FTEST_FAIL_TEST("validator flags %d of %d shipped scripts with errors -- too many false positives", bad_files, files);
            return FTRs_Go_To_Next_Action;
        }
        JUSTLOG("Script validate sweep: %d files, %d with errors (%d errors)%s", files, bad_files, total_errors, samples.c_str());
    }
    return FTRs_Go_To_Next_Action;
}

} // extern "C"

#endif // FUNCTESTING
