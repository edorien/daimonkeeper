/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_journal.cpp
 *     docs/refactor/editor/02-editing-toolbox.md §4 -- the editor's undo/
 *     redo journal. Two kinds of entry:
 *
 *     - Placement (creature/hero/digger, object, trap, door): each is a
 *       one-shot "create exactly one thing" action with a clean "delete
 *       that thing" inverse.
 *     - Rect-terrain (docs/refactor/editor/09-toolbox-remainder.md §1,
 *       added later): PckA_EditorPlaceTerrainRect/_RectClearEarth/
 *       _RectSetOwner each mutate a whole box of slabs in one shot, with a
 *       clean release-of-drag boundary to journal against -- unlike
 *       free-hand Terrain brush painting or Flood Fill, which dispatch
 *       per-tile-per-frame server-side with no client-visible per-stroke
 *       boundary at all, and stay deferred for that reason.
 *       PckA_EditorRectDeleteThings also stays unjournaled: restoring
 *       arbitrary deleted things (creatures with full CreatureControl
 *       state, etc.) faithfully needs a much bigger snapshot than a slab
 *       kind/owner pair, out of scope here.
 *
 *     Redo replays the packet action that created the thing (pcktype/
 *     par1-4, recorded by record_placement()) -- but several of these
 *     verbs (PckA_CheatMakeCreature/_MakeDigger/PckA_EditorPlaceTrap/
 *     _PlaceDoor) read position from the packet's own *ambient* pos_x/pos_y
 *     rather than a param (only PckA_EditorPlaceObject carries position in
 *     its own actn_par1/actn_par2).
 *
 *     First attempt: write the recorded pos_x/pos_y directly onto the
 *     local packet before resending one of those verbs, relying on
 *     set_players_packet_action() never touching pos_x/pos_y itself.
 *     Confirmed live as broken -- "redo places at the current cursor
 *     position, not the original spot". Root cause: that write happens
 *     from this render-phase callback, but get_dungeon_control_nonaction_inputs()
 *     (called from input() for the *next* real turn, which runs again
 *     before this render-phase-originated packet is actually processed)
 *     unconditionally overwrites pos_x/pos_y with whatever's under the
 *     mouse *then* -- the same "packet field written from the wrong phase
 *     gets clobbered before it's read" bug class as the original
 *     PckA_EditorPlaceObject issue, just one layer removed (that bug was
 *     about *reading* a stale field; this one is about a *write* getting
 *     silently overwritten before the read it was meant for).
 *
 *     Fix: give each of the four ambient-position verbs a dedicated Redo
 *     counterpart (PckA_EditorRedoCreature/_RedoDigger/_RedoTrap/_RedoDoor,
 *     packet_data.h) that carries position explicitly in actn_par1/
 *     actn_par2 instead -- same shape PckA_EditorPlaceObject already uses,
 *     and the *only* shape that survives a multi-frame gap between when
 *     kfx_editor decides the value and when the packet is actually
 *     processed. These can't just be the *normal* creation verbs
 *     (PckA_CheatMakeCreature/_MakeDigger in particular) taking an
 *     explicit-position param instead, since those are also the classic
 *     (non-editor) cheat menu's own verbs and changing their param layout
 *     would affect that unrelated path too.
 *
 *     Undo-of-a-redo (and redo-of-a-redo) both fall out for free for
 *     placements: Redo's resent action flows through the exact same
 *     packets_cheats.c handler that any fresh placement does, which
 *     already calls record_placement() on success -- so the newly
 *     (re)created thing gets journaled again automatically, no
 *     special-case bookkeeping needed. Rect-terrain entries can't use that
 *     trick (their Undo/Redo apply directly, client-side -- see below), so
 *     Redo re-pushes the same entry onto the undo stack itself, by hand.
 *     One accepted v1 gap, both kinds: a genuinely *new* action doesn't
 *     clear the redo stack (most undo/redo systems do), since detecting
 *     "this record call came from a fresh action, not from Redo replaying
 *     an old entry" would need a flag that survives across the turn
 *     boundary between Redo sending the packet and the server actually
 *     processing it -- not attempted here.
 *
 *     Rect-terrain Undo/Redo apply directly against kfx_sim (
 *     place_slab_type_on_map()/place_animating_slab_type_on_map()/
 *     delete_room_slab()/do_slab_efficiency_alteration()), not through a
 *     packet, mirroring editor_toolbox.cpp's own Brush/Stamp -- same D2
 *     single-player-local exception (the doc's own sanctioned fallback):
 *     the mutation itself happens server-side in packets_cheats.c's
 *     handler, entirely outside this file's visibility, and the only thing
 *     this file has to work with afterward is the per-slab "before"
 *     snapshot record_rect_terrain() captured -- there's no packet shape
 *     that could carry a variable-length per-slab buffer back out to a
 *     server handler even if a round trip were wanted.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_journal.h"
#include "editor_journal_callbacks.h"
#include "editor_points.h"
#include "kfx_editor.h"
#include "packet_data.h"
#include "player_data.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "map_data.h"
#include "thing_list.h"
#include "thing_data.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "thing_doors.h"
#include "thing_physics.h"
#include "thing_navigate.h"
#include "creature_control.h"
#include "player_instances.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "room_util.h"
#include "config_terrain.h"
#include "config_creature.h"
#include "config_objects.h"
#include "config_trapdoor.h"
#include <utility>
#include <vector>
#include <cstdio>
#include <imgui.h>
#include "post_inc.h"

/******************************************************************************/
namespace {

    enum EditorJournalEntryKind { EJK_Placement, EJK_RectTerrain, EJK_Point, EJK_SlabDiff, EJK_PointEdit, EJK_ThingEdit };

    // One slab's state for stroke-level undo: what the terrain tools and the
    // texture painter change.
    struct SlabState {
        SlabKind kind = 0;
        PlayerNumber owner = 0;
        unsigned char texture = 0;
        bool operator==(const SlabState &o) const { return kind == o.kind && owner == o.owner && texture == o.texture; }
    };
    // A creature / object / trap / door as the stroke tracker sees it: enough
    // to delete it again (matched by class, model, owner and position) or to
    // create it again.
    struct ThingRec {
        ThingClass cls = 0;
        ThingModel model = 0;
        PlayerNumber owner = 0;
        long x = 0, y = 0, z = 0; // raw map units
        int exp_level = 0;        // creatures
        long gold = 0;            // gold objects
        long health = 0;          // reset when a motion preview is undone
        int locked = 0;           // doors
        bool same_thing(const ThingRec &o) const { return cls == o.cls && model == o.model && owner == o.owner; }
    };
    struct SlabChange {
        MapSlabCoord x = 0, y = 0;
        SlabState before, after;
    };

    struct EditorJournalEntry {
        EditorJournalEntryKind kind = EJK_Placement;
        // EJK_Placement fields.
        long thing_idx = 0;
        unsigned char pcktype = 0;
        unsigned long par1 = 0, par2 = 0;
        unsigned short par3 = 0, par4 = 0;
        long pos_x = 0, pos_y = 0;
        // EJK_RectTerrain fields (docs/refactor/editor/09-toolbox-remainder.md
        // §1). rect_before is one entry per slab in the box, row-major (sy
        // outer, sx inner), matching the capture order in packets_cheats.c's
        // editor_snapshot_slab_rect() -- Undo replays it verbatim; Redo
        // instead reapplies rect_new_kind/rect_new_owner uniformly (or just
        // rect_new_owner for PckA_EditorRectSetOwner, dispatched on
        // pcktype), same as the original action did.
        MapSlabCoord rect_beg_x = 0, rect_beg_y = 0, rect_end_x = 0, rect_end_y = 0;
        SlabKind rect_new_kind = 0;
        PlayerNumber rect_new_owner = 0;
        std::vector<EditorRectSlabSnapshot> rect_before;
        // EJK_Point fields (phase5/06-slices6-8-points-tool.md): a placed or
        // deleted light / action point / effect generator. point_placed says
        // which -- Undo of a placement deletes, Undo of a deletion
        // recreates, Redo is the mirror. Applied directly (D2 exception),
        // like rect terrain, so Redo has to push the entry back itself.
        EditorPointSnapshot point = {};
        bool point_placed = false;
        // EJK_SlabDiff (fx-plans/00 item A7): every slab a whole stroke or
        // one-shot tool changed, with its state before and after.
        std::vector<SlabChange> slab_changes;
        std::vector<ThingRec> things_removed, things_added;
        // EJK_PointEdit: point_before / point_after (ids current); EJK_ThingEdit:
        // the thing's index and identity plus its properties either side.
        EditorPointSnapshot point_before = {}, point_after = {};
        ThingRec thing_id;
        EditorThingProps props_before = {}, props_after = {};
        char label[32] = "";
    };

    // Doc-specified cap (§4: "Cap the journal (e.g. 200 entries)"). Plain
    // arrays, not ring buffers -- actions aren't a hot path (one push per
    // click/drag-release, not per frame), so shifting on overflow is cheap
    // enough. Element-wise move on overflow (not memmove): EditorJournalEntry
    // now owns a std::vector (rect_before), which memmove'ing between array
    // slots would corrupt (raw byte copy skips the vector's own
    // move-construction bookkeeping).
    const int kJournalCapacity = 200;
    EditorJournalEntry s_undo_stack[kJournalCapacity];
    int s_undo_count = 0;
    EditorJournalEntry s_redo_stack[kJournalCapacity];
    int s_redo_count = 0;

    // Test-only override -- see editor_journal_test_force_active()'s own
    // declaration (editor_journal.h) for why this exists. false in
    // production; a Catch2 test flips it on to exercise
    // record_placement()/record_rect_terrain() without a real editor_open().
    TbBool s_test_force_active = false;

    void stack_push(EditorJournalEntry *stack, int *count, const EditorJournalEntry &entry)
    {
        if (*count < kJournalCapacity)
        {
            stack[(*count)++] = entry;
            return;
        }
        for (int i = 1; i < kJournalCapacity; i++)
            stack[i - 1] = std::move(stack[i]);
        stack[kJournalCapacity - 1] = entry;
    }

    // Shared by rect-terrain Undo (replays the per-slab "before" snapshot)
    // and Redo (replays a uniform kind/owner, or owner-only) -- both just
    // "apply this per-slab kind+owner to the box", sourced differently.
    // Deliberately re-implemented here rather than calling into
    // packets_cheats.c's own editor_apply_slab_rect()/editor_set_owner_rect():
    // those are file-local `static` helpers in a lower-ranked library kfx_editor
    // can't reach anyway, and this is the exact same direct-kfx_sim-call shape
    // editor_toolbox.cpp's own Brush/Stamp already established for
    // single-player-local mutation.
    void apply_slab_at(MapSlabCoord sx, MapSlabCoord sy, SlabKind kind, PlayerNumber owner)
    {
        place_slab_type_replacing_room(kind, sx, sy, owner);
    }

    // Undo: restore the box to its exact pre-mutation per-slab state.
    void apply_rect_snapshot(const EditorJournalEntry &entry)
    {
        long i = 0;
        for (MapSlabCoord sy = entry.rect_beg_y; sy <= entry.rect_end_y; sy++)
        {
            for (MapSlabCoord sx = entry.rect_beg_x; sx <= entry.rect_end_x; sx++)
            {
                if ((size_t)i < entry.rect_before.size())
                    apply_slab_at(sx, sy, entry.rect_before[i].kind, entry.rect_before[i].owner);
                i++;
            }
        }
    }

    // Redo: reapply the original mutation. PckA_EditorRectSetOwner is
    // inlined verbatim from packets_cheats.c's own editor_set_owner_rect()
    // (skip ownerless kinds, kind read fresh per-slab, never the animated
    // variant even for an animated kind) rather than routed through
    // apply_slab_at() below, to stay exactly faithful to what the original
    // action actually did -- apply_slab_at()'s animated-kind branch exists
    // for the terrain-paint ops (PlaceTerrainRect/RectClearEarth), which
    // editor_apply_slab_rect() (its own packets_cheats.c counterpart) does
    // check.
    void apply_rect_redo(const EditorJournalEntry &entry)
    {
        for (MapSlabCoord sy = entry.rect_beg_y; sy <= entry.rect_end_y; sy++)
        {
            for (MapSlabCoord sx = entry.rect_beg_x; sx <= entry.rect_end_x; sx++)
            {
                if (entry.pcktype == PckA_EditorRectSetOwner)
                {
                    MapSubtlCoord tile_stl_x = slab_subtile(sx, 0);
                    MapSubtlCoord tile_stl_y = slab_subtile(sy, 0);
                    struct SlabMap *slb = get_slabmap_block(sx, sy);
                    if (slab_kind_has_no_ownership(slb->kind))
                        continue;
                    (void)tile_stl_x; (void)tile_stl_y;
                    place_slab_type_replacing_room(slb->kind, sx, sy, entry.rect_new_owner);
                }
                else
                {
                    apply_slab_at(sx, sy, entry.rect_new_kind, entry.rect_new_owner);
                }
            }
        }
    }

    void apply_slab_state(const SlabChange &c, bool use_after)
    {
        const SlabState &st = use_after ? c.after : c.before;
        const SlabState &other = use_after ? c.before : c.after;
        // A door slab is written by creating the door thing (things are
        // restored before slabs); placing the slab kind on its own would
        // remove the door again.
        if ((st.kind != other.kind || st.owner != other.owner) && !slab_kind_is_door(st.kind))
            apply_slab_at(c.x, c.y, st.kind, st.owner);
        kfx_config_state.slab_ext_data[get_slab_number(c.x, c.y)] = st.texture;
    }

    bool thing_is_tracked(const struct Thing *t)
    {
        if (!thing_exists(t))
            return false;
        // A locked door's keyhole belongs to the door: locking creates it,
        // unlocking and destroying remove it. Tracking it separately would
        // recreate a second one.
        if (t->class_id == TCls_Object && t->model == ObjMdl_SpinningKey)
            return false;
        return t->class_id == TCls_Creature || t->class_id == TCls_Object
            || t->class_id == TCls_Trap || t->class_id == TCls_Door;
    }

    // index -> record, for every tracked thing.
    typedef std::vector<std::pair<ThingIndex, ThingRec>> ThingSet;

    void capture_things(ThingSet &out)
    {
        out.clear();
        for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        {
            const struct Thing *t = thing_get(i);
            if (!thing_is_tracked(t))
                continue;
            ThingRec r;
            r.cls = t->class_id;
            r.model = t->model;
            r.owner = t->owner;
            r.x = t->mappos.x.val; r.y = t->mappos.y.val; r.z = t->mappos.z.val;
            r.health = t->health;
            if (t->class_id == TCls_Creature)
            {
                const struct CreatureControl *cctrl = creature_control_get_from_thing(t);
                r.exp_level = (cctrl != NULL) ? (int)cctrl->exp_level : 0;
            }
            else if (t->class_id == TCls_Object && object_is_gold(t))
                r.gold = t->valuable.gold_stored;
            else if (t->class_id == TCls_Door)
                r.locked = t->door.is_locked;
            out.push_back(std::make_pair(i, r));
        }
    }

    // Removes the live thing that matches `r` (same kind of thing, same spot).
    void delete_thing_like(const ThingRec &r)
    {
        for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        {
            struct Thing *t = thing_get(i);
            if (!thing_is_tracked(t) || t->class_id != r.cls || t->model != r.model || t->owner != r.owner)
                continue;
            if (labs((long)t->mappos.x.val - r.x) > 256 || labs((long)t->mappos.y.val - r.y) > 256)
                continue;
            if (t->class_id == TCls_Door)
                destroy_door(t);
            else
                destroy_object(t);
            return;
        }
    }

    void create_thing_like(const ThingRec &r)
    {
        struct Coord3d pos;
        pos.x.val = (MapCoord)r.x;
        pos.y.val = (MapCoord)r.y;
        pos.z.val = (MapCoord)r.z;
        switch (r.cls)
        {
            case TCls_Creature:
            {
                struct Thing *t = create_creature(&pos, r.model, r.owner);
                if (!thing_is_invalid(t))
                {
                    t->mappos.z.val = (MapCoord)get_thing_height_at(t, &t->mappos);
                    t->previous_mappos = t->mappos;
                    set_creature_level(t, (CrtrExpLevel)r.exp_level);
                    // As it was when deleted (a wounded creature stays wounded);
                    // never above the level's own maximum.
                    if (r.health > 0 && r.health < t->health)
                        t->health = (short)r.health;
                }
                break;
            }
            case TCls_Object:
            {
                struct Thing *t = create_object(&pos, r.model, r.owner, -1);
                if (!thing_is_invalid(t) && object_is_gold(t))
                    t->valuable.gold_stored = r.gold;
                break;
            }
            case TCls_Trap:
                player_place_trap_at_subtile_without_check(coord_subtile(r.x), coord_subtile(r.y), r.owner, r.model, true);
                break;
            case TCls_Door:
                if (player_place_door_without_check_at(coord_subtile(r.x), coord_subtile(r.y), r.owner, r.model, true)
                    && r.locked)
                {
                    struct Thing *d = find_base_thing_on_mapwho(TCls_Door, r.model, coord_subtile(r.x), coord_subtile(r.y));
                    if (!thing_is_invalid(d))
                        lock_door(d);
                }
                break;
            default:
                break;
        }
    }

    // Sets the editable properties of thing `idx` if it is still the same
    // kind of thing; false if not.
    bool apply_thing_props(long idx, const ThingRec &id, const EditorThingProps &p)
    {
        struct Thing *t = thing_get((ThingIndex)idx);
        if (!thing_is_tracked(t) || t->class_id != id.cls || t->model != id.model || t->owner != id.owner)
            return false;
        if (t->class_id == TCls_Object)
        {
            struct Coord3d pos;
            pos.x.val = (MapCoord)p.x;
            pos.y.val = (MapCoord)p.y;
            pos.z.val = (MapCoord)p.z;
            move_thing_in_map(t, &pos);
            t->previous_mappos = t->mappos;
            if (object_is_gold(t))
                t->valuable.gold_stored = p.gold;
        }
        else if (t->class_id == TCls_Door)
        {
            if (p.locked && !t->door.is_locked)
                lock_door(t);
            else if (!p.locked && t->door.is_locked)
                unlock_door(t);
        }
        return true;
    }

    // Slab snapshot taken when a stroke starts.
    ThingSet s_stroke_things_before;
    std::vector<SlabState> s_stroke_before;
    bool s_stroke_open = false;

    void capture_slabs(std::vector<SlabState> &out)
    {
        const long w = kfx_sim_state.map_tiles_x;
        const long h = kfx_sim_state.map_tiles_y;
        out.assign((size_t)(w * h), SlabState());
        for (long y = 0; y < h; y++)
        {
            for (long x = 0; x < w; x++)
            {
                const struct SlabMap *slb = get_slabmap_block(x, y);
                SlabState &st = out[(size_t)(y * w + x)];
                st.kind = slb->kind;
                st.owner = (PlayerNumber)slabmap_owner(slb);
                st.texture = kfx_config_state.slab_ext_data[get_slab_number(x, y)];
            }
        }
    }

} // namespace

extern "C" void editor_journal_reset(void)
{
    s_undo_count = 0;
    s_redo_count = 0;
    s_stroke_open = false;
}

extern "C" void editor_journal_stroke_begin(void)
{
    if (!editor_is_active() && !s_test_force_active)
        return;
    capture_slabs(s_stroke_before);
    capture_things(s_stroke_things_before);
    s_stroke_open = true;
}

// Preview Motion: the simulation runs and creatures walk away from where the
// mapmaker put them. begin() remembers every slab and every creature / object /
// trap / door; restore() puts it all back (positions, health, what exists),
// so previewing never changes the map. Not journaled.
namespace {
    std::vector<SlabState> s_preview_slabs;
    ThingSet s_preview_things;
    bool s_preview_open = false;
}

extern "C" void editor_journal_preview_begin(void)
{
    capture_slabs(s_preview_slabs);
    capture_things(s_preview_things);
    s_preview_open = true;
}

extern "C" void editor_journal_preview_restore(void)
{
    if (!s_preview_open)
        return;
    s_preview_open = false;
    ThingSet now;
    capture_things(now);
    // Anything that appeared during the preview (spawned creatures, dropped
    // gold, ...) goes; anything that vanished comes back.
    for (const auto &a : now)
    {
        bool was_there = false;
        for (const auto &b : s_preview_things)
            if (a.first == b.first && a.second.same_thing(b.second))
            {
                was_there = true;
                break;
            }
        if (!was_there)
            delete_thing_like(a.second);
    }
    for (const auto &b : s_preview_things)
    {
        struct Thing *t = thing_get(b.first);
        if (thing_is_tracked(t) && t->class_id == b.second.cls && t->model == b.second.model && t->owner == b.second.owner)
        {
            struct Coord3d p;
            p.x.val = (MapCoord)b.second.x;
            p.y.val = (MapCoord)b.second.y;
            p.z.val = (MapCoord)b.second.z;
            if (t->class_id != TCls_Door && t->class_id != TCls_Trap)
            {
                move_thing_in_map(t, &p);
                t->previous_mappos = t->mappos;
            }
            t->health = (short)b.second.health;
            if (t->class_id == TCls_Door)
            {
                if (b.second.locked && !t->door.is_locked)
                    lock_door(t);
                else if (!b.second.locked && t->door.is_locked)
                    unlock_door(t);
            }
        }
        else
            create_thing_like(b.second);
    }
    // Slabs (digging, claiming) last, as in undo.
    const long w = kfx_sim_state.map_tiles_x;
    std::vector<SlabState> after;
    capture_slabs(after);
    if (after.size() == s_preview_slabs.size())
        for (size_t i = 0; i < after.size(); i++)
            if (!(after[i] == s_preview_slabs[i]))
            {
                SlabChange c;
                c.x = (MapSlabCoord)((long)i % w);
                c.y = (MapSlabCoord)((long)i / w);
                c.before = s_preview_slabs[i];
                c.after = after[i];
                apply_slab_state(c, false);
            }
    s_preview_things.clear();
    s_preview_slabs.clear();
}

extern "C" TbBool editor_journal_stroke_open(void)
{
    return s_stroke_open;
}

extern "C" TbBool editor_journal_stroke_end(const char *label)
{
    if (!s_stroke_open)
        return false;
    s_stroke_open = false;
    std::vector<SlabState> after;
    capture_slabs(after);
    if (after.size() != s_stroke_before.size())
        return false; // the map changed size mid-stroke: nothing sensible to record
    const long w = kfx_sim_state.map_tiles_x;
    EditorJournalEntry entry;
    entry.kind = EJK_SlabDiff;
    for (size_t i = 0; i < after.size(); i++)
    {
        if (after[i] == s_stroke_before[i])
            continue;
        SlabChange c;
        c.x = (MapSlabCoord)((long)i % w);
        c.y = (MapSlabCoord)((long)i / w);
        c.before = s_stroke_before[i];
        c.after = after[i];
        entry.slab_changes.push_back(c);
    }
    s_stroke_before.clear();
    // Things: same index and same kind of thing = unchanged (they may have
    // moved); anything else was removed or added.
    {
        ThingSet now;
        capture_things(now);
        for (const auto &b : s_stroke_things_before)
        {
            bool kept = false;
            for (const auto &a : now)
                if (a.first == b.first && a.second.same_thing(b.second))
                {
                    kept = true;
                    break;
                }
            if (!kept)
                entry.things_removed.push_back(b.second);
        }
        for (const auto &a : now)
        {
            bool kept = false;
            for (const auto &b : s_stroke_things_before)
                if (a.first == b.first && a.second.same_thing(b.second))
                {
                    kept = true;
                    break;
                }
            if (!kept)
                entry.things_added.push_back(a.second);
        }
        s_stroke_things_before.clear();
    }
    if (entry.slab_changes.empty() && entry.things_removed.empty() && entry.things_added.empty())
        return false;
    snprintf(entry.label, sizeof(entry.label), "%s", (label != NULL) ? label : "Edit");
    editor_mark_dirty();
    stack_push(s_undo_stack, &s_undo_count, entry);
    // A new edit invalidates whatever could have been redone.
    s_redo_count = 0;
    return true;
}

extern "C" void editor_journal_test_force_active(TbBool force_active)
{
    s_test_force_active = force_active;
}

// Wired into EditorJournalCallbacks::record_placement (main.cpp). Called
// unconditionally by packets_cheats.c after every placement tool's
// create_*() call, including outside an editor session (PckA_CheatMakeCreature/
// PckA_CheatMakeDigger are also the classic cheat menu's own verbs) -- the
// editor_is_active() check belongs here, in the implementation, not on the
// (always-non-NULL) caller side, same convention EditorCallbacks already
// established.
extern "C" void editor_journal_record_placement(long thing_idx, unsigned char pcktype,
    unsigned long par1, unsigned long par2, unsigned short par3, unsigned short par4,
    long pos_x, long pos_y)
{
    if (!editor_is_active() && !s_test_force_active)
        return;
    editor_mark_dirty();
    EditorJournalEntry entry;
    entry.kind = EJK_Placement;
    entry.thing_idx = thing_idx;
    entry.pcktype = pcktype;
    entry.par1 = par1; entry.par2 = par2;
    entry.par3 = par3; entry.par4 = par4;
    entry.pos_x = pos_x; entry.pos_y = pos_y;
    stack_push(s_undo_stack, &s_undo_count, entry);
}

extern "C" void editor_journal_record_point_edit(const struct EditorPointSnapshot *before, const struct EditorPointSnapshot *after)
{
    if (!editor_is_active() && !s_test_force_active)
        return;
    editor_mark_dirty();
    EditorJournalEntry entry;
    entry.kind = EJK_PointEdit;
    entry.point_before = *before;
    entry.point_after = *after;
    stack_push(s_undo_stack, &s_undo_count, entry);
    s_redo_count = 0;
}

extern "C" void editor_journal_thing_props(long thing_idx, struct EditorThingProps *out)
{
    *out = EditorThingProps();
    const struct Thing *t = thing_get((ThingIndex)thing_idx);
    if (!thing_exists(t))
        return;
    out->x = t->mappos.x.val;
    out->y = t->mappos.y.val;
    out->z = t->mappos.z.val;
    if (t->class_id == TCls_Object && object_is_gold(t))
        out->gold = t->valuable.gold_stored;
    if (t->class_id == TCls_Door)
        out->locked = t->door.is_locked;
}

extern "C" void editor_journal_record_thing_edit(long thing_idx, const struct EditorThingProps *before, const struct EditorThingProps *after)
{
    if (!editor_is_active() && !s_test_force_active)
        return;
    if (before->x == after->x && before->y == after->y && before->z == after->z
        && before->gold == after->gold && before->locked == after->locked)
        return;
    const struct Thing *t = thing_get((ThingIndex)thing_idx);
    if (!thing_exists(t))
        return;
    editor_mark_dirty();
    EditorJournalEntry entry;
    entry.kind = EJK_ThingEdit;
    entry.thing_idx = thing_idx;
    entry.thing_id.cls = t->class_id;
    entry.thing_id.model = t->model;
    entry.thing_id.owner = t->owner;
    entry.props_before = *before;
    entry.props_after = *after;
    stack_push(s_undo_stack, &s_undo_count, entry);
    s_redo_count = 0;
}

extern "C" void editor_journal_record_door_lock(long thing_idx, TbBool was_locked)
{
    struct EditorThingProps before, after;
    editor_journal_thing_props(thing_idx, &before);
    before.locked = was_locked ? 1 : 0;
    after = before;
    after.locked = was_locked ? 0 : 1;
    editor_journal_record_thing_edit(thing_idx, &before, &after);
}

extern "C" void editor_journal_record_point(TbBool placed, const struct EditorPointSnapshot *snap)
{
    if (!editor_is_active() && !s_test_force_active)
        return;
    editor_mark_dirty();
    EditorJournalEntry entry;
    entry.kind = EJK_Point;
    entry.point = *snap;
    entry.point_placed = (placed != 0);
    stack_push(s_undo_stack, &s_undo_count, entry);
}

// Wired into EditorJournalCallbacks::record_rect_terrain (main.cpp). Called
// by packets_cheats.c's PckA_EditorPlaceTerrainRect/_RectClearEarth/
// _RectSetOwner handlers BEFORE applying the mutation -- see this
// function's own declaration (kfx_editor.h) and the callback's comment
// (editor_journal_callbacks.h) for why. Copies `before` into rect_before
// immediately, since the caller frees its own buffer right after this call
// returns.
extern "C" void editor_journal_record_rect_terrain(unsigned char pcktype,
    long box_beg_x, long box_beg_y, long box_end_x, long box_end_y,
    SlabKind new_kind, PlayerNumber new_owner,
    const struct EditorRectSlabSnapshot *before, long count)
{
    if (!editor_is_active() && !s_test_force_active)
        return;
    editor_mark_dirty();
    EditorJournalEntry entry;
    entry.kind = EJK_RectTerrain;
    entry.pcktype = pcktype;
    entry.rect_beg_x = (MapSlabCoord)box_beg_x;
    entry.rect_beg_y = (MapSlabCoord)box_beg_y;
    entry.rect_end_x = (MapSlabCoord)box_end_x;
    entry.rect_end_y = (MapSlabCoord)box_end_y;
    entry.rect_new_kind = new_kind;
    entry.rect_new_owner = new_owner;
    entry.rect_before.assign(before, before + count);
    stack_push(s_undo_stack, &s_undo_count, entry);
}

// docs/refactor/editor/09-toolbox-remainder.md backlog item -- toolbox
// Undo/Redo buttons need the same trigger logic Ctrl+Z already has,
// without duplicating it, so the keyboard shortcut (editor_journal_frame(),
// still hardcoded -- standard everywhere, no need to route it through D6)
// and a button click both call this.
extern "C" void editor_journal_do_undo(void)
{
    if (s_undo_count == 0)
        return;
    EditorJournalEntry entry = std::move(s_undo_stack[--s_undo_count]);
    if (entry.kind == EJK_Point)
    {
        // A no-op when the target no longer matches (slot reused, or already
        // removed by another tool) -- the entry still moves between stacks
        // so the stacks stay in step.
        if (entry.point_placed)
            editor_points_delete(&entry.point);
        else
            editor_points_create(&entry.point);
        stack_push(s_redo_stack, &s_redo_count, entry);
        return;
    }
    if (entry.kind == EJK_PointEdit)
    {
        // after -> before; a recreated point's new id lands in point_before.
        editor_points_replace(&entry.point_after, &entry.point_before);
        stack_push(s_redo_stack, &s_redo_count, entry);
        return;
    }
    if (entry.kind == EJK_ThingEdit)
    {
        apply_thing_props(entry.thing_idx, entry.thing_id, entry.props_before);
        stack_push(s_redo_stack, &s_redo_count, entry);
        return;
    }
    if (entry.kind == EJK_SlabDiff)
    {
        // Things first: (re)placing a door writes its slab, which the slab
        // states below then set to exactly what it was.
        for (const ThingRec &r : entry.things_added)
            delete_thing_like(r);
        for (const ThingRec &r : entry.things_removed)
            create_thing_like(r);
        for (const SlabChange &c : entry.slab_changes)
            apply_slab_state(c, false);
        stack_push(s_redo_stack, &s_redo_count, entry);
        return;
    }
    if (entry.kind == EJK_RectTerrain)
    {
        // §1 -- direct client-side restore (D2 exception), no packet: see
        // this file's own header comment for why a round trip isn't
        // possible here anyway (a packet can't carry a variable-length
        // per-slab buffer).
        apply_rect_snapshot(entry);
        stack_push(s_redo_stack, &s_redo_count, entry);
        return;
    }
    struct PlayerInfo *player = get_my_player();
    set_players_packet_action(player, PckA_EditorUndo, entry.thing_idx, 0, 0, 0);
    stack_push(s_redo_stack, &s_redo_count, entry);
}

extern "C" void editor_journal_do_redo(void)
{
    if (s_redo_count == 0)
        return;
    EditorJournalEntry entry = std::move(s_redo_stack[--s_redo_count]);
    if (entry.kind == EJK_Point)
    {
        if (entry.point_placed)
            editor_points_create(&entry.point);
        else
            editor_points_delete(&entry.point);
        stack_push(s_undo_stack, &s_undo_count, entry);
        return;
    }
    if (entry.kind == EJK_PointEdit)
    {
        editor_points_replace(&entry.point_before, &entry.point_after);
        stack_push(s_undo_stack, &s_undo_count, entry);
        return;
    }
    if (entry.kind == EJK_ThingEdit)
    {
        apply_thing_props(entry.thing_idx, entry.thing_id, entry.props_after);
        stack_push(s_undo_stack, &s_undo_count, entry);
        return;
    }
    if (entry.kind == EJK_SlabDiff)
    {
        for (const ThingRec &r : entry.things_removed)
            delete_thing_like(r);
        for (const ThingRec &r : entry.things_added)
            create_thing_like(r);
        for (const SlabChange &c : entry.slab_changes)
            apply_slab_state(c, true);
        stack_push(s_undo_stack, &s_undo_count, entry);
        return;
    }
    if (entry.kind == EJK_RectTerrain)
    {
        // §1 -- direct client-side reapply, same reasoning as Undo above.
        // Unlike placement Redo (below), there's no server-side record
        // call to re-journal this automatically, since nothing is sent
        // through packets_cheats.c at all -- push the same entry back onto
        // the undo stack by hand instead (rect_before is still valid: it's
        // exactly the state Undo just restored FROM, i.e. exactly what
        // Redo is about to move away from again).
        apply_rect_redo(entry);
        stack_push(s_undo_stack, &s_undo_count, entry);
        return;
    }
    struct PlayerInfo *player = get_my_player();
    // Found live: writing entry.pos_x/pos_y directly onto the local packet
    // and resending the *original* creation verb (the first version of
    // this function) didn't work -- "redo places at the current cursor
    // position, not the original spot". Root cause: that write happens
    // from this render-phase callback, but
    // get_dungeon_control_nonaction_inputs() (input(), called again for
    // the next real turn before this packet is actually processed)
    // unconditionally overwrites pos_x/pos_y with whatever's under the
    // mouse *then* -- the exact same "packet field written from the wrong
    // phase gets clobbered before it's read" bug class as the original
    // PckA_EditorPlaceObject issue. Fix: for the four verbs that normally
    // rely on ambient pos_x/pos_y, redo through a dedicated counterpart
    // verb that carries position explicitly in its own params instead
    // (same shape PckA_EditorPlaceObject already uses) -- see
    // packet_data.h's own comment on these four verbs.
    switch (entry.pcktype)
    {
        case PckA_EditorPlaceObject:
            // Already carries position in its own par1/par2 -- no
            // ambient-position problem here, resend as-is.
            set_players_packet_action(player, PckA_EditorPlaceObject, entry.par1, entry.par2, entry.par3, entry.par4);
            break;
        case PckA_CheatMakeCreature:
            set_players_packet_action(player, PckA_EditorRedoCreature, entry.pos_x, entry.pos_y, entry.par1, entry.par2);
            break;
        case PckA_CheatMakeDigger:
            set_players_packet_action(player, PckA_EditorRedoDigger, entry.pos_x, entry.pos_y, entry.par1, entry.par2);
            break;
        case PckA_EditorPlaceTrap:
            set_players_packet_action(player, PckA_EditorRedoTrap, entry.pos_x, entry.pos_y, entry.par1, entry.par2);
            break;
        case PckA_EditorPlaceDoor:
            set_players_packet_action(player, PckA_EditorRedoDoor, entry.pos_x, entry.pos_y, entry.par1, entry.par2);
            break;
        default:
            break;
    }
}

extern "C" void editor_journal_frame(void)
{
    ImGuiIO &io = ImGui::GetIO();
    // ImGui::IsKeyPressed(..., false) is edge-triggered (fires once per
    // physical press, not once per frame while held) -- same convention
    // editor_frame() already uses for its own F10 toggle.
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        editor_journal_do_undo();
    else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
        editor_journal_do_redo();
}

extern "C" int editor_journal_undo_count(void) { return s_undo_count; }
extern "C" int editor_journal_redo_count(void) { return s_redo_count; }

namespace {

    // Human-readable one-liner for the toolbox's History tab. Resolves
    // model numbers to their config names (creature_code_name() etc.) --
    // the same lookups the toolbox's own pickers already use -- rather
    // than showing raw numbers. Returned buffer is shared/static: callers
    // must render/consume it immediately, before the next describe_entry()
    // call overwrites it (same one-buffer-per-call convention as e.g.
    // thing_model_name() elsewhere in this codebase).
    const char *describe_entry(const EditorJournalEntry &entry)
    {
        static char buf[64];
        if (entry.kind == EJK_Point)
        {
            snprintf(buf, sizeof(buf), "%s", editor_points_describe(&entry.point, entry.point_placed));
            return buf;
        }
        if (entry.kind == EJK_PointEdit)
        {
            snprintf(buf, sizeof(buf), "Edit %s", editor_points_describe(&entry.point_after, true));
            return buf;
        }
        if (entry.kind == EJK_ThingEdit)
        {
            snprintf(buf, sizeof(buf), "%s",
                (entry.thing_id.cls == TCls_Door) ? "Door lock"
                : (entry.props_before.gold != entry.props_after.gold) ? "Gold amount" : "Move thing");
            return buf;
        }
        if (entry.kind == EJK_SlabDiff)
        {
            snprintf(buf, sizeof(buf), "%s (%zu slabs, %zu things)", entry.label, entry.slab_changes.size(),
                entry.things_added.size() + entry.things_removed.size());
            return buf;
        }
        if (entry.kind == EJK_RectTerrain)
        {
            switch (entry.pcktype)
            {
                case PckA_EditorPlaceTerrainRect:
                    snprintf(buf, sizeof(buf), "Terrain Rect: %s", slab_code_name(entry.rect_new_kind));
                    break;
                case PckA_EditorRectClearEarth:
                    snprintf(buf, sizeof(buf), "Clear Earth");
                    break;
                case PckA_EditorRectSetOwner:
                    snprintf(buf, sizeof(buf), "Set Owner");
                    break;
                default:
                    snprintf(buf, sizeof(buf), "Rect terrain op");
                    break;
            }
            return buf;
        }
        switch (entry.pcktype)
        {
            case PckA_CheatMakeCreature:
                snprintf(buf, sizeof(buf), "Creature: %s", creature_code_name((ThingModel)entry.par1));
                break;
            case PckA_CheatMakeDigger:
                snprintf(buf, sizeof(buf), "Digger");
                break;
            case PckA_EditorPlaceObject:
                snprintf(buf, sizeof(buf), "Object: %s", object_code_name((ThingModel)entry.par3));
                break;
            case PckA_EditorPlaceTrap:
                snprintf(buf, sizeof(buf), "Trap: %s", trap_code_name((int)entry.par1));
                break;
            case PckA_EditorPlaceDoor:
                snprintf(buf, sizeof(buf), "Door: %s", door_code_name((int)entry.par1));
                break;
            default:
                snprintf(buf, sizeof(buf), "Placement");
                break;
        }
        return buf;
    }

} // namespace

// index_from_top 0 = the entry Ctrl+Z/the Undo button would act on next
// (top of stack, most recently pushed), counting up toward the oldest.
// Returns NULL if index_from_top is out of range.
extern "C" const char *editor_journal_describe_undo(int index_from_top)
{
    if ((index_from_top < 0) || (index_from_top >= s_undo_count))
        return NULL;
    return describe_entry(s_undo_stack[s_undo_count - 1 - index_from_top]);
}

extern "C" const char *editor_journal_describe_redo(int index_from_top)
{
    if ((index_from_top < 0) || (index_from_top >= s_redo_count))
        return NULL;
    return describe_entry(s_redo_stack[s_redo_count - 1 - index_from_top]);
}
/******************************************************************************/
