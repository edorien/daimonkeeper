/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_brush.cpp
 *     See editor_brush.h. The original comments of the capture and stamp code
 *     (editor_toolbox.cpp, docs/refactor/editor/02-editing-toolbox.md §2.4 and
 *     09-toolbox-remainder.md §1) are kept where they were written.
 */
#include "pre_inc.h"
#include "editor_brush.h"
#include "editor_journal.h"
#include "editor_points.h"
#include "kfx_editor.h"
#include "kfx_sim_state.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "map_data.h"
#include "room_util.h"
#include "thing_list.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "thing_physics.h"
#include "creature_control.h"
#include "player_instances.h"
#include "config_terrain.h"
#include <vector>
#include "list_walk.h"
#include "post_inc.h"

namespace {

struct BrushSlabEntry { int64_t dx, dy; SlabKind kind; PlayerNumber owner; };
std::vector<BrushSlabEntry> s_brush_buffer;
struct BrushThingEntry { int64_t dx, dy; ThingClass class_id; ThingModel model; PlayerNumber owner; CrtrExpLevel exp_level; };
std::vector<BrushThingEntry> s_brush_thing_buffer;
// Lights / action points / effect generators, positions relative to the
// captured box's top-left in raw map units.
std::vector<EditorPointSnapshot> s_brush_point_buffer;
int64_t s_brush_point_origin_x = 0, s_brush_point_origin_y = 0;

bool is_brush_capturable_thing_class(ThingClass class_id)
{
    return (class_id == TCls_Creature) || (class_id == TCls_Object)
        || (class_id == TCls_Trap) || (class_id == TCls_Door);
}

// Original restriction (§2.4): "Gems / Guard Post / Bridge don't
// survive a grab" -- these are engine-derived slabs (auto-computed
// from adjacency/resources), not freely paintable kinds, so capturing
// and later re-stamping them elsewhere wouldn't reproduce anything
// meaningful.
bool is_engine_derived_slab(SlabKind kind)
{
    return (kind == SlbT_BRIDGE) || (kind == SlbT_GEMS) || (kind == SlbT_GUARDPOST);
}

// Found live: capturing a Heart room's own SlbT_DUNGHEART slabs and
// stamping them elsewhere spawned extra, unwanted Dungeon Heart
// objects (a non-standard 4x4 heart room produced 4 duplicate hearts
// clustered at the stamp target, not the 1 the source room actually
// had). Root cause: place_slab_type_on_map_f() (map_blocks.c) calls
// place_slab_object() per placed slab, which spawns whatever decorative
// objects that slab kind's config attaches -- for a heart room's own
// slabs, that includes the heart object itself, on however many of the
// room's slabs carry that decoration (room-shape-dependent, so a
// non-standard room size can carry it on more than one slab). Existing
// §2.4 restriction ("can't stamp over a Heart or Portal") only checked
// the *destination* slab in the stamp loop below -- it never stopped a
// Heart/Portal slab from being captured as *source* material in the
// first place, which is the actual gap: excluding it at capture time
// means it can never reach the stamp loop at all, regardless of what
// destination-side checks exist.
bool is_heart_or_portal_slab(SlabKind kind)
{
    return (kind == SlbT_DUNGHEART) || (kind == SlbT_DUNGHEART_WALL)
        || (kind == SlbT_ENTRANCE) || (kind == SlbT_ENTRANCE_WALL);
}



} // namespace

void editor_brush_clear(void)
{
    s_brush_buffer.clear();
    s_brush_thing_buffer.clear();
    s_brush_point_buffer.clear();
}

bool editor_brush_is_empty(void)
{
    return s_brush_buffer.empty();
}

size_t editor_brush_slab_count(void) { return s_brush_buffer.size(); }
size_t editor_brush_thing_count(void) { return s_brush_thing_buffer.size(); }
size_t editor_brush_point_count(void) { return s_brush_point_buffer.size(); }

void editor_brush_capture(MapSlabCoord box_beg_x, MapSlabCoord box_beg_y, MapSlabCoord box_end_x, MapSlabCoord box_end_y)
{
            s_brush_buffer.clear();
            for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
            {
                for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
                {
                    struct SlabMap *slb = get_slabmap_block(sx, sy);
                    if (is_engine_derived_slab(slb->kind) || is_heart_or_portal_slab(slb->kind))
                        continue;
                    BrushSlabEntry entry;
                    entry.dx = sx - box_beg_x;
                    entry.dy = sy - box_beg_y;
                    entry.kind = slb->kind;
                    entry.owner = slabmap_owner(slb);
                    s_brush_buffer.push_back(entry);
                }
            }

            // Things capture -- subtile-precision offsets (not slab-snapped
            // like the terrain buffer above), same nested
            // slab-then-subtile-then-mapwho-chain scan
            // editor_delete_things_in_rect() (packets_cheats.c) already
            // uses to sweep an area for things, just reading instead of
            // deleting.
            //
            // Deduplicated by thing index (captured_indices) -- found live:
            // a large-sprite Dungeon Heart (place_thing_in_mapwho() only
            // ever links a thing into the one mapblock at its own mappos,
            // but a big/scaled object's clipbox can still get visited from
            // more than one of this scan's subtile positions depending on
            // how it's registered) got captured 4 times over instead of
            // once, and stamped as 4 overlapping hearts. Capturing the same
            // live thing more than once can never be correct for this tool
            // -- a single thing has exactly one position -- so guarding on
            // "already recorded this index this pass" is a pure
            // correctness fix with no cost to the normal (one-thing-once)
            // case.
            s_brush_thing_buffer.clear();
            {
                s_brush_point_origin_x = (int64_t)slab_subtile(box_beg_x, 0) * 256;
                s_brush_point_origin_y = (int64_t)slab_subtile(box_beg_y, 0) * 256;
                const int64_t x1 = ((int64_t)slab_subtile(box_end_x, 0) + STL_PER_SLB) * 256;
                const int64_t y1 = ((int64_t)slab_subtile(box_end_y, 0) + STL_PER_SLB) * 256;
                s_brush_point_buffer.assign(256, EditorPointSnapshot());
                const int64_t got = editor_points_capture_in_box(s_brush_point_origin_x, s_brush_point_origin_y, x1, y1,
                    s_brush_point_buffer.data(), (int64_t)s_brush_point_buffer.size());
                s_brush_point_buffer.resize((size_t)got);
            }
            std::vector<ThingIndex> captured_indices;
            MapSubtlCoord anchor_stl_x = slab_subtile(box_beg_x, 0);
            MapSubtlCoord anchor_stl_y = slab_subtile(box_beg_y, 0);
            for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
            {
                for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
                {
                    for (int64_t sub_y = 0; sub_y < STL_PER_SLB; sub_y++)
                    {
                        for (int64_t sub_x = 0; sub_x < STL_PER_SLB; sub_x++)
                        {
                            MapSubtlCoord tstl_x = slab_subtile(sx, sub_x);
                            MapSubtlCoord tstl_y = slab_subtile(sy, sub_y);
                            struct Map *mapblk = get_map_block_at(tstl_x, tstl_y);
                            FOR_EACH_THING(thing, thing_walk_map_block(mapblk))
                            {
                                if (!is_brush_capturable_thing_class(thing->class_id))
                                    continue;
                                // Same "Heart/Portal excluded" restriction
                                // as the terrain-slab buffer above -- a map
                                // has exactly one heart per player, so
                                // stamping a captured heart/portal thing
                                // elsewhere would duplicate it, not
                                // reproduce anything meaningful.
                                if (thing_is_dungeon_heart(thing) || object_is_hero_gate(thing))
                                    continue;
                                bool already_captured = false;
                                for (ThingIndex seen : captured_indices)
                                {
                                    if (seen == thing->index)
                                    {
                                        already_captured = true;
                                        break;
                                    }
                                }
                                if (already_captured)
                                    continue;
                                captured_indices.push_back(thing->index);
                                // Anchor the entry on the thing's own actual
                                // position, not the subtile this particular
                                // mapwho lookup happened to find it from --
                                // matters once a thing can be reached from
                                // more than one scanned subtile.
                                BrushThingEntry tentry;
                                tentry.dx = thing->mappos.x.stl.num - anchor_stl_x;
                                tentry.dy = thing->mappos.y.stl.num - anchor_stl_y;
                                tentry.class_id = thing->class_id;
                                tentry.model = thing->model;
                                tentry.owner = thing->owner;
                                tentry.exp_level = 0;
                                if (thing->class_id == TCls_Creature)
                                {
                                    struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
                                    tentry.exp_level = cctrl->exp_level;
                                }
                                s_brush_thing_buffer.push_back(tentry);
                            }
                        }
                    }
                }
            }

}

void editor_brush_stamp(MapSlabCoord cur_slb_x, MapSlabCoord cur_slb_y)
{
        const bool stamping = (!s_brush_buffer.empty() || !s_brush_thing_buffer.empty() || !s_brush_point_buffer.empty());
        if (stamping)
            editor_journal_stroke_begin(); // one undo step for the whole stamp
        if (!s_brush_buffer.empty())
        {
            for (const BrushSlabEntry &entry : s_brush_buffer)
            {
                MapSlabCoord tsx = cur_slb_x + entry.dx;
                MapSlabCoord tsy = cur_slb_y + entry.dy;
                MapSubtlCoord tile_stl_x = slab_subtile(tsx, 0);
                MapSubtlCoord tile_stl_y = slab_subtile(tsy, 0);
                // Original restriction (§2.4): "can't stamp over a Heart
                // or Portal".
                struct SlabMap *target = get_slabmap_block(tsx, tsy);
                if ((target->kind == SlbT_DUNGHEART) || (target->kind == SlbT_DUNGHEART_WALL)
                    || (target->kind == SlbT_ENTRANCE) || (target->kind == SlbT_ENTRANCE_WALL))
                    continue;
                (void)tile_stl_x; (void)tile_stl_y;
                place_slab_type_replacing_room(entry.kind, tsx, tsy, entry.owner);
            }
        }

        // Things stamp -- same one-stamp-per-click trigger as slabs, but
        // subtile-precision anchored (slab_subtile(cur_slb_x/y, 0), the
        // stamp box's own top-left subtile) rather than slab-snapped, same
        // shape the capture step above used. Direct create_*() calls, same
        // D2 single-player-local exception as slab stamping -- see
        // create_creature()/create_object()'s own callers elsewhere in this
        // file (PckA_CheatMakeCreature/PckA_EditorPlaceObject handlers,
        // packets_cheats.c) for the z-height fixup and
        // player_place_trap/door_without_check_at() precedent this mirrors.
        if (!s_brush_point_buffer.empty())
        {
            const int64_t dx = (int64_t)slab_subtile(cur_slb_x, 0) * 256 - s_brush_point_origin_x;
            const int64_t dy = (int64_t)slab_subtile(cur_slb_y, 0) * 256 - s_brush_point_origin_y;
            for (const EditorPointSnapshot &snap : s_brush_point_buffer)
                editor_points_stamp(&snap, dx, dy);
        }
        if (!s_brush_thing_buffer.empty())
        {
            MapSubtlCoord anchor_stl_x = slab_subtile(cur_slb_x, 0);
            MapSubtlCoord anchor_stl_y = slab_subtile(cur_slb_y, 0);
            for (const BrushThingEntry &entry : s_brush_thing_buffer)
            {
                MapSubtlCoord tstl_x = anchor_stl_x + entry.dx;
                MapSubtlCoord tstl_y = anchor_stl_y + entry.dy;
                switch (entry.class_id)
                {
                    case TCls_Creature:
                    {
                        struct Coord3d cpos;
                        cpos.x.val = subtile_coord_center(tstl_x);
                        cpos.y.val = subtile_coord_center(tstl_y);
                        cpos.z.val = 0;
                        struct Thing *newtng = create_creature(&cpos, entry.model, entry.owner);
                        if (!thing_is_invalid(newtng))
                        {
                            newtng->mappos.z.val = get_thing_height_at(newtng, &newtng->mappos);
                            newtng->previous_mappos = newtng->mappos;
                            set_creature_level(newtng, entry.exp_level);
                        }
                        break;
                    }
                    case TCls_Object:
                    {
                        struct Coord3d opos;
                        opos.x.val = subtile_coord_center(tstl_x);
                        opos.y.val = subtile_coord_center(tstl_y);
                        opos.z.val = 0;
                        create_object(&opos, entry.model, entry.owner, -1);
                        break;
                    }
                    case TCls_Trap:
                        player_place_trap_at_subtile_without_check(tstl_x, tstl_y, entry.owner, entry.model, true);
                        break;
                    case TCls_Door:
                        player_place_door_without_check_at(tstl_x, tstl_y, entry.owner, entry.model, true);
                        break;
                    default:
                        break;
                }
            }
        }
        if (stamping)
            editor_journal_stroke_end("Stamp");
}
