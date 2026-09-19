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

    enum EditorJournalEntryKind { EJK_Placement, EJK_RectTerrain, EJK_Point };

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
        MapSubtlCoord tile_stl_x = slab_subtile(sx, 0);
        MapSubtlCoord tile_stl_y = slab_subtile(sy, 0);
        if (subtile_is_room(tile_stl_x, tile_stl_y))
            delete_room_slab(sx, sy, true);
        if (slab_kind_is_animated(kind))
            place_animating_slab_type_on_map(kind, 0, tile_stl_x, tile_stl_y, owner);
        else
            place_slab_type_on_map(kind, tile_stl_x, tile_stl_y, owner, 0);
        do_slab_efficiency_alteration(sx, sy);
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
                    SlabKind kind = slb->kind;
                    if (subtile_is_room(tile_stl_x, tile_stl_y))
                        delete_room_slab(sx, sy, true);
                    place_slab_type_on_map(kind, tile_stl_x, tile_stl_y, entry.rect_new_owner, 0);
                    do_slab_efficiency_alteration(sx, sy);
                }
                else
                {
                    apply_slab_at(sx, sy, entry.rect_new_kind, entry.rect_new_owner);
                }
            }
        }
    }

} // namespace

extern "C" void editor_journal_reset(void)
{
    s_undo_count = 0;
    s_redo_count = 0;
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
