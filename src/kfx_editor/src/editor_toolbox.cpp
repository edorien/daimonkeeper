/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_toolbox.cpp
 *     docs/refactor/editor/02-editing-toolbox.md -- the tool palette.
 *     Phase 2's actual scope is much larger than what's here (marking,
 *     fill, brush, objects, traps/doors, lights/FX/AP stubs, query/eraser,
 *     undo/redo journal, definable keybindings -- see that doc's §5 "what
 *     must exist" list). This first slice covers items 1-4: the toolbox
 *     panel itself, terrain/room paint with a generated palette, the
 *     player+experience bottom bar, and creature/hero/digger placement
 *     with a model-grid picker. Every tool here is a thin UI over an
 *     already-live PSt_ work-state and PckA_Cheat packet pair (doc §1) --
 *     no new world-mutation logic, exactly per the doc's "tools = player
 *     work-states" model.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_toolbox.h"
#include "kfx_editor.h"
#include "editor_journal.h"
#include "editor_points.h"

#include "frontgui_widgets.h"
#include <imgui.h>
#include "player_data.h"
#include "packet_data.h"
#include "config_players.h"
#include "config_terrain.h"
#include "config_creature.h"
#include "config_objects.h"
#include "config_trapdoor.h"
#include "kfx_config_state.h"
#include "kfx_sim_state.h"
#include "local_camera.h"
#include "engine_redraw.h"
#include "engine_render.h"
#include "kjm_input.h"
#include "front_input.h"
#include "slab_data.h"
#include "map_blocks.h"
#include "map_data.h"
#include "room_util.h"
#include "room_data.h"
#include "thing_list.h"
#include "thing_creature.h"
#include "thing_objects.h"
#include "thing_stats.h"
#include "thing_physics.h"
#include "creature_control.h"
#include "player_instances.h"
#include "editor_texture_packs.h" // kTexturePackItems -- Paint Texture tool's picker
#include <cstdio>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

    enum EditorTool {
        EdTool_Terrain,
        EdTool_Fill,
        EdTool_CreatureEvil,
        EdTool_CreatureHero,
        EdTool_Digger,
        EdTool_Object,
        EdTool_Trap,
        EdTool_Door,
        EdTool_Eyedropper,
        // §2.4's own "Brush" tool -- named "Stamp" in the toolbox to avoid
        // colliding with the Terrain tool's own "Brush" mode (continuous
        // drag-paint), a completely different thing.
        EdTool_Stamp,
        EdTool_Query,
        EdTool_Erase,
        // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
        // texture paint" item -- same D2 single-player-local-exception
        // shape as Stamp above: painting kfx_config_state.slab_ext_data[]
        // directly, client-side, every held-mouse frame, no packet and no
        // undo/redo journal entry (matches Stamp's own "no undo for a
        // stamp" precedent -- the fix for an unwanted paint is the same
        // as for an unwanted stamp: paint over it again).
        EdTool_PaintTexture,
        // phase5/06-slices6-8-points-tool.md -- lights, action points and
        // effect generators (editor_points.cpp owns everything about it).
        EdTool_Points,
    };

    EditorTool s_active_tool = EdTool_Terrain;

    // Local shadow of ustate->cheatselection, kept only so the picker can
    // highlight the current selection -- nothing else in an editor session
    // writes cheatselection, so this can't drift out of sync. Starts at the
    // same value clear_game()'s zero-init leaves chosen_terrain_kind at
    // (SlbT_ROCK == 0) -- matching reality matters more than picking a more
    // "visible" default the engine doesn't actually have set yet. Found
    // live: with no highlight at all, a palette click was indistinguishable
    // from one that didn't register -- worth remembering when testing
    // placement on a New Map canvas that starts 100% rock: painting ROCK
    // over ROCK is real, correct behaviour, just invisible: pick a
    // different kind from the (now highlighted) list first.
    SlabKind s_selected_terrain_kind = SlbT_ROCK;
    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- which texture-pack override
    // handle_texture_paint_click() writes into kfx_config_state.
    // slab_ext_data[] next. Purely local UI state, same shadow-not-
    // cheatselection shape as the other s_selected_* fields below (this
    // tool has no packet/CheatSelection field of its own to read back --
    // see EdTool_PaintTexture's own enum comment for why).
    int s_selected_texture_pack = 0;
    ThingModel s_selected_creature_kind = 1;
    ThingModel s_selected_hero_kind = 1;
    ThingModel s_selected_object_model = 1;
    // docs/refactor/editor/09-toolbox-remainder.md §1 -- value-property
    // slice, gold amount only (see draw_object_picker()'s own comment).
    // 0 means "use the engine default" (gold_object_typical_value(), same
    // as create_object() already applies with no value tweak at all).
    int s_object_gold_value = 0;
    // Selected-for-editing object (§1's position-edit follow-up -- clicking
    // an existing object with the Object tool selects it here instead of
    // trying to place a new one on the same square). 0 means nothing
    // selected -- thing index 0 is never a valid live thing. Position
    // fields are buffered (not read live every frame) so typing a new
    // value doesn't get overwritten by the thing's still-unchanged actual
    // position until Apply is pressed.
    ThingIndex s_edit_thing_idx = 0;
    int s_edit_pos_x = 0;
    int s_edit_pos_y = 0;
    int s_edit_pos_z = 0;
    ThingModel s_selected_trap_kind = 1;
    ThingModel s_selected_door_kind = 1;
    PlayerNumber s_selected_owner = 0;
    // §2.2/§2.4 -- Terrain tool's mode row: Brush (continuous drag-paint,
    // PSt_PlaceTerrain), Rectangle (mark a box, commit on release,
    // PSt_EditorPlaceTerrainRect), Clear Earth (same mark-a-box shape,
    // fixed target, PSt_EditorRectClearEarth), or Delete Things (same
    // shape again, sweeps the box for things instead of repainting slabs,
    // PSt_EditorRectDeleteThings). Toolbox-local only -- it just decides
    // which work state the Terrain button/toggle sends next, nothing
    // server-side needs to know this exists as a persistent selection.
    enum TerrainMode { TerrainMode_Brush, TerrainMode_Rectangle, TerrainMode_ClearEarth, TerrainMode_DeleteThings, TerrainMode_SetOwner };
    TerrainMode s_terrain_mode = TerrainMode_Brush;
    unsigned char s_selected_level = 0; // 0-indexed -- see draw_bottom_bar()

    void set_work_state(unsigned char state)
    {
        struct PlayerInfo *player = get_my_player();
        // Exactly gf_change_player_state()'s own call (gui_boxmenu.c) --
        // the classic cheat menu's "change mode" buttons do the same thing.
        set_players_packet_action(player, PckA_SetPlyrState, state, 0, 0, 0);
    }

    unsigned char terrain_mode_work_state(TerrainMode mode)
    {
        switch (mode)
        {
            case TerrainMode_Rectangle:    return PSt_EditorPlaceTerrainRect;
            case TerrainMode_ClearEarth:   return PSt_EditorRectClearEarth;
            case TerrainMode_DeleteThings: return PSt_EditorRectDeleteThings;
            case TerrainMode_SetOwner:     return PSt_EditorRectSetOwner;
            default:                       return PSt_PlaceTerrain;
        }
    }

    struct ToolDef { const char *label; EditorTool tool; unsigned char state; };

    // One packet action per click, deliberately -- set_players_packet_action()
    // overwrites the single per-turn packet slot, so a tool switch can't
    // also re-sync the chosen kind in the same click (that would just
    // clobber the PckA_SetPlyrState with a PckA_CheatSwitch* before
    // either is ever processed). Same one-action-per-click shape the
    // classic cheat menu's gf_change_player_state() has.
    void draw_tool_buttons(const ToolDef *tools, size_t count)
    {
        for (size_t i = 0; i < count; i++)
        {
            const ToolDef &t = tools[i];
            bool selected = (s_active_tool == t.tool);
            if (FeNavButton(t.label, selected))
            {
                s_active_tool = t.tool;
                // Terrain's state depends on s_terrain_mode -- its table
                // entry's `state` is unused, kept only so the table's
                // shape stays uniform across every group.
                unsigned char state = t.state;
                if (t.tool == EdTool_Terrain)
                    state = terrain_mode_work_state(s_terrain_mode);
                set_work_state(state);
            }
        }
    }

    // docs/refactor/editor/09-toolbox-remainder.md backlog item -- the
    // journal's own contents, exposed as a tab rather than bolted onto an
    // existing tool tab (History isn't a "tool" you select to place things
    // with -- it's an always-relevant view, same reasoning that gave the
    // header-row Undo/Redo buttons their own spot rather than folding them
    // into a tool tab too). Doesn't touch s_active_tool -- same "switching
    // tabs only changes what's visible" contract draw_tool_strip()'s other
    // tabs already have.
    void draw_history_tab()
    {
        int undo_count = editor_journal_undo_count();
        int redo_count = editor_journal_redo_count();

        ImGui::BeginDisabled(undo_count == 0);
        if (FeButton("Undo"))
            editor_journal_do_undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(redo_count == 0);
        if (FeButton("Redo"))
            editor_journal_do_redo();
        ImGui::EndDisabled();

        char summary[64];
        snprintf(summary, sizeof(summary), "%d undo / %d redo", undo_count, redo_count);
        FeBodyText(summary);

        // Rows aren't clickable (no "jump to this entry" -- undo/redo only
        // ever act on the top of their own stack, there's no meaningful
        // "undo past this point in one click" without popping everything
        // above it too) -- FeListRow's own selectable-row shape is reused
        // here purely for the scrollable-list layout, always passed
        // selected=false. Each row's label carries a "##<index>" ImGui ID
        // suffix (stripped from what's displayed, kept for the widget's
        // identity) so two entries with the same description (e.g. two
        // "Object: Torch" placements in a row) don't collide on the same
        // Selectable ID.
        char row_label[80];
        FeSubheading("Undo stack (top acts next)");
        bool undo_open = FeBeginListBox("##EdHistoryUndo", ImVec2(240, 140));
        if (undo_open)
        {
            for (int i = 0; i < undo_count; i++)
            {
                const char *label = editor_journal_describe_undo(i);
                snprintf(row_label, sizeof(row_label), "%s##%d", label ? label : "?", i);
                FeListRow(row_label, false);
            }
        }
        FeEndListBox(undo_open);

        FeSubheading("Redo stack (top acts next)");
        bool redo_open = FeBeginListBox("##EdHistoryRedo", ImVec2(240, 140));
        if (redo_open)
        {
            for (int i = 0; i < redo_count; i++)
            {
                const char *label = editor_journal_describe_redo(i);
                snprintf(row_label, sizeof(row_label), "%s##%d", label ? label : "?", i);
                FeListRow(row_label, false);
            }
        }
        FeEndListBox(redo_open);
    }

    // Grouped into tabs (Terrain/Creatures/Things/Utility/History) rather
    // than one flat 12-entry list -- found live: the toolbox had grown
    // tall enough that, combined with a tool's own picker below it, it no
    // longer reliably fit on screen. Switching tabs only changes which
    // tool *buttons* (or, for History, journal contents) are visible; it
    // doesn't change s_active_tool or which picker is showing below --
    // picking a new tool from a different tab still needs its own click,
    // same as before.
    void draw_tool_strip()
    {
        static const ToolDef terrain_tools[] = {
            {"Terrain",  EdTool_Terrain,       PSt_PlaceTerrain},
            {"Fill",     EdTool_Fill,          PSt_EditorFill},
        };
        static const ToolDef creature_tools[] = {
            {"Creature", EdTool_CreatureEvil,  PSt_MkBadCreatr},
            {"Hero",     EdTool_CreatureHero,  PSt_MkGoodCreatr},
            {"Digger",   EdTool_Digger,        PSt_MkDigger},
        };
        static const ToolDef thing_tools[] = {
            {"Object",   EdTool_Object,        PSt_EditorPlaceObject},
            {"Trap",     EdTool_Trap,          PSt_EditorPlaceTrap},
            {"Door",     EdTool_Door,          PSt_EditorPlaceDoor},
            {"Points",   EdTool_Points,        PSt_EditorPlacePoint},
        };
        static const ToolDef utility_tools[] = {
            {"Eyedropper", EdTool_Eyedropper,  PSt_EditorEyedropper},
            {"Stamp",    EdTool_Stamp,         PSt_EditorStamp},
            {"Paint Tex", EdTool_PaintTexture, PSt_EditorPaintTexture},
            {"Query",    EdTool_Query,         PSt_EditorQuery},
            {"Erase",    EdTool_Erase,         PSt_DestroyThing},
        };

        bool tabbar_open = FeBeginTabBar("##EdToolTabs");
        if (tabbar_open)
        {
            if (FeTab("Terrain"))
            {
                draw_tool_buttons(terrain_tools, sizeof(terrain_tools) / sizeof(terrain_tools[0]));
                FeEndTab();
            }
            if (FeTab("Creatures"))
            {
                draw_tool_buttons(creature_tools, sizeof(creature_tools) / sizeof(creature_tools[0]));
                FeEndTab();
            }
            if (FeTab("Things"))
            {
                draw_tool_buttons(thing_tools, sizeof(thing_tools) / sizeof(thing_tools[0]));
                FeEndTab();
            }
            if (FeTab("Utility"))
            {
                draw_tool_buttons(utility_tools, sizeof(utility_tools) / sizeof(utility_tools[0]));
                FeEndTab();
            }
            if (FeTab("History"))
            {
                draw_history_tab();
                FeEndTab();
            }
        }
        FeEndTabBar(tabbar_open);
    }

    // §2.2/§2.4 -- Terrain mode row (Brush/Rectangle/Clear Earth), shown
    // only above the Terrain tool's own picker (not Fill's, which reuses
    // draw_terrain_picker() verbatim but has no modes of its own).
    // Switching modes re-sends set_work_state() immediately so a drag
    // started right after toggling uses the right dispatch -- same
    // "current tool's state follows this toolbox setting" contract the
    // tool strip itself has. FeButton, not FeNavButton, here -- found live:
    // FeNavButton always sizes itself to the *entire remaining window
    // width* (by design, for one-per-row vertical nav lists like the tool
    // strip above), so multiple of them on the same SameLine() row fought
    // over that width and blew the whole (AlwaysAutoResize) toolbox window
    // up to full screen width. FeButton sizes to its own label instead --
    // same widget the bottom bar already uses for its side-by-side
    // P0/P1/.../Neutral row, with the same bracket-the-label convention
    // for "selected" (FeButton has no built-in highlighted look).
    void draw_terrain_mode_toggle()
    {
        struct ModeDef { const char *label; TerrainMode mode; };
        static const ModeDef modes[] = {
            {"Brush",         TerrainMode_Brush},
            {"Rectangle",     TerrainMode_Rectangle},
            {"Clear Earth",   TerrainMode_ClearEarth},
            {"Delete Things", TerrainMode_DeleteThings},
            {"Set Owner",     TerrainMode_SetOwner},
        };
        for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
        {
            if (i > 0)
                ImGui::SameLine();
            bool selected = (s_terrain_mode == modes[i].mode);
            char label[24];
            snprintf(label, sizeof(label), selected ? "[%s]" : "%s", modes[i].label);
            if (FeButton(label))
            {
                s_terrain_mode = modes[i].mode;
                set_work_state(terrain_mode_work_state(s_terrain_mode));
            }
        }
        FeSeparator();
    }

    // §2.1 -- slab palette generated from kfx_config_state.conf.slab_conf,
    // so modded slab kinds appear automatically (F15). Rooms are just more
    // SlabKind entries in the same config (assigned_room on the stats, not
    // a separate array) -- PSt_PlaceTerrain already treats them uniformly,
    // so one flat list covers §2.1's Tiles+Rooms without a second picker.
    void draw_terrain_picker()
    {
        struct PlayerInfo *player = get_my_player();
        const struct SlabsConfig &slabc = kfx_config_state.conf.slab_conf;
        bool open = FeBeginListBox("##EdTerrainPicker", ImVec2(240, 320));
        if (open)
        {
            for (int32_t i = 0; i < slabc.slab_types_count; i++)
            {
                const char *name = slab_code_name((SlabKind)i);
                if (FeListRow(name, i == s_selected_terrain_kind))
                {
                    s_selected_terrain_kind = (SlabKind)i;
                    set_players_packet_action(player, PckA_CheatSwitchTerrain, i, 0, 0, 0);
                }
            }
        }
        FeEndListBox(open);
    }

    // §2.5 -- creature-model grid, split evil/hero by which picker is
    // active (the tool strip's own Creature/Hero split), same "loop
    // [1, model_count)" + creature_code_name() the doc calls for. Custom/
    // campaign-modded creatures appear automatically (F15).
    void draw_creature_picker(TbBool hero)
    {
        struct PlayerInfo *player = get_my_player();
        long model_count = kfx_config_state.conf.crtr_conf.model_count;
        bool open = FeBeginListBox("##EdCreaturePicker", ImVec2(240, 320));
        if (open)
        {
            ThingModel &selected = hero ? s_selected_hero_kind : s_selected_creature_kind;
            for (ThingModel m = 1; m < (ThingModel)model_count; m++)
            {
                const char *name = creature_code_name(m);
                if (FeListRow(name, m == selected))
                {
                    selected = m;
                    set_players_packet_action(player,
                        hero ? PckA_CheatSwitchHero : PckA_CheatSwitchCreature, m, 0, 0, 0);
                }
            }
        }
        FeEndListBox(open);
    }

    // §1 (09-toolbox-remainder.md) -- position-edit follow-up to the gold
    // value-property slice, generalised to any existing object rather than
    // gold-only: X/Y/Z fields (raw map units, 256 per subtile, for the fine
    // control the doc's own worked example needed -- moving a wall torch up
    // its wall isn't representable at subtile granularity) plus the gold
    // Value field when the selected thing happens to be gold-family. Both
    // need an explicit Apply (not live-as-you-type) since the whole point
    // is comparing the buffered edit against the thing's actual current
    // value before committing.
    //
    // Deliberately generic (thing_idx + x/y/z, not "object position edit")
    // in both the packet (PckA_EditorSetThingPosition) and this panel's own
    // shape, since Lights and Action Points (still not-started tools, doc
    // §2) will need the same X/Y/Z editing once they exist -- building it
    // object-specific now would just mean redoing this when those land.
    // Not attempted here: an actual toolbox entry/picker for either, or any
    // property beyond position/gold value -- that's real new-tool work, not
    // a follow-up to this slice.
    void draw_object_edit_panel()
    {
        struct Thing *thing = thing_get(s_edit_thing_idx);
        if (thing_is_invalid(thing) || (thing->class_id != TCls_Object))
            return;
        struct PlayerInfo *player = get_my_player();
        FeSeparator();
        char title[64];
        snprintf(title, sizeof(title), "Editing #%d: %s", (int)thing->index, object_code_name(thing->model));
        FeSubheading(title);
        FeBodyText("Position (raw map units, 256 per subtile):");
        ImGui::SetNextItemWidth(240);
        ImGui::InputInt("X##EdObjPosX", &s_edit_pos_x);
        ImGui::SetNextItemWidth(240);
        ImGui::InputInt("Y##EdObjPosY", &s_edit_pos_y);
        ImGui::SetNextItemWidth(240);
        ImGui::InputInt("Z##EdObjPosZ", &s_edit_pos_z);
        if (FeButton("Apply Position", ImVec2(240, 0)))
        {
            set_players_packet_action(player, PckA_EditorSetThingPosition,
                s_edit_pos_x, s_edit_pos_y, thing->index, s_edit_pos_z);
        }
        if (object_is_gold(thing))
        {
            FeBodyText("Value (0 = leave unchanged):");
            ImGui::SetNextItemWidth(240);
            ImGui::InputInt("##EdGoldValue", &s_object_gold_value);
            if (s_object_gold_value < 0)
                s_object_gold_value = 0;
            if (FeButton("Apply Value", ImVec2(240, 0)))
            {
                set_players_packet_action(player, PckA_EditorSetGoldValue, thing->index, s_object_gold_value, 0, 0);
            }
        }
    }

    // §2.6 -- object-model grid from object_conf, same "loop [1, count)" +
    // *_code_name() shape as the terrain/creature pickers. Unlike those,
    // picking a row here only updates the *local* selection -- there's no
    // CheatSelection field for "chosen object model" (F17), so nothing is
    // sent until the world is actually clicked; see
    // handle_object_placement_click() below, which reads
    // s_selected_object_model directly.
    void draw_object_picker()
    {
        long model_count = kfx_config_state.conf.object_conf.object_types_count;
        bool open = FeBeginListBox("##EdObjectPicker", ImVec2(240, 320));
        if (open)
        {
            for (ThingModel m = 1; m < (ThingModel)model_count; m++)
            {
                const char *name = object_code_name(m);
                if (FeListRow(name, m == s_selected_object_model))
                    s_selected_object_model = m;
            }
        }
        FeEndListBox(open);
        draw_object_edit_panel();
    }

    // §2.7 -- trap/door pickers, same "loop [1, count)" + *_code_name()
    // shape as the object picker, but selecting a row here *does* send a
    // packet immediately (PckA_CheatSwitchTrap/Door), same as terrain/
    // creature: unlike Objects, UserState already has chosen_trap_kind/
    // chosen_door_kind (used by the classic workshop tools), so there's no
    // F17 gap to work around -- placement itself stays a plain click,
    // handled entirely by PSt_EditorPlaceTrap/PSt_EditorPlaceDoor's own
    // packets_cheats.c dispatch, no render-phase click handler needed.
    void draw_trap_picker()
    {
        struct PlayerInfo *player = get_my_player();
        long model_count = kfx_config_state.conf.trapdoor_conf.trap_types_count;
        bool open = FeBeginListBox("##EdTrapPicker", ImVec2(240, 320));
        if (open)
        {
            for (ThingModel m = 1; m < (ThingModel)model_count; m++)
            {
                const char *name = trap_code_name(m);
                if (FeListRow(name, m == s_selected_trap_kind))
                {
                    s_selected_trap_kind = m;
                    set_players_packet_action(player, PckA_CheatSwitchTrap, m, 0, 0, 0);
                }
            }
        }
        FeEndListBox(open);
    }

    void draw_door_picker()
    {
        struct PlayerInfo *player = get_my_player();
        long model_count = kfx_config_state.conf.trapdoor_conf.door_types_count;
        bool open = FeBeginListBox("##EdDoorPicker", ImVec2(240, 320));
        if (open)
        {
            for (ThingModel m = 1; m < (ThingModel)model_count; m++)
            {
                const char *name = door_code_name(m);
                if (FeListRow(name, m == s_selected_door_kind))
                {
                    s_selected_door_kind = m;
                    set_players_packet_action(player, PckA_CheatSwitchDoor, m, 0, 0, 0);
                }
            }
        }
        FeEndListBox(open);
    }

    // §2.6 -- placement itself. Unlike terrain/creature (a click just sets
    // player->work_state/cheatselection and the *existing* dungeon-control
    // click dispatch in packets_cheats.c does the rest, reading pos_x/pos_y
    // from *within* input() -- before the packet is consumed), there is no
    // server-side "chosen object" to read back there (F17) -- so kfx_editor
    // detects the world click and sends PckA_EditorPlaceObject itself.
    //
    // Found live via unconditional JUSTMSG diagnostics ("no object appears",
    // then confirmed with keeperfx.log): reading the *packet's* pos_x/pos_y
    // here -- from the ImGui-render phase, which runs later in the frame
    // than input() -- always saw MapCoordsValid=0, pos=(0,0). The packet is
    // per-turn scratch state that input() populates and the turn-exchange
    // step (exchange_packets(), called right after input() in
    // game_session_loop.cpp) resets before the render phase runs; by the
    // time this ImGui callback fires, get_local_packet() is already back to
    // a blank next-turn packet. Terrain/Fill/Creature never hit this because
    // their dispatch lives *inside* input()'s per-turn packets_cheats.c
    // switch, not in a render-phase callback.
    //
    // Fix: don't read the packet at all -- recompute the world position
    // directly with the same screen_to_map() the input path itself uses,
    // against the current mouse position and camera, right here at click
    // time. Guarded on !io.WantCaptureMouse so a click on this very
    // toolbox's own picker list doesn't also register as a world placement.
    void handle_object_placement_click()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return;
        if (io.WantCaptureMouse)
            return;
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        if (!screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            return;
        // §1 (09-toolbox-remainder.md) -- value/position-edit slice. A
        // square can only hold one object, so a click on an EXISTING object
        // can't mean "place a new one there" -- select it into
        // draw_object_edit_panel() instead, same "detect the click's real
        // target client-side" precedent Query/Eyedropper already use.
        // Buffered edit fields are re-synced from the thing's own current
        // state on every (re-)selection, discarding any unapplied edit --
        // clicking an object always means "show me what it actually has
        // right now", not "keep whatever I was mid-typing".
        MapSubtlCoord stl_x = coord_subtile(pos.x.val);
        MapSubtlCoord stl_y = coord_subtile(pos.y.val);
        struct Thing *existing = get_nearest_thing_at_position(stl_x, stl_y);
        if (!thing_is_invalid(existing) && (existing->class_id == TCls_Object))
        {
            s_edit_thing_idx = existing->index;
            s_edit_pos_x = existing->mappos.x.val;
            s_edit_pos_y = existing->mappos.y.val;
            s_edit_pos_z = existing->mappos.z.val;
            s_object_gold_value = object_is_gold(existing) ? existing->valuable.gold_stored : 0;
            return;
        }
        set_players_packet_action(player, PckA_EditorPlaceObject, pos.x.val, pos.y.val,
            s_selected_object_model, s_selected_owner);
    }

    // §2.10 -- Eyedropper. Same click-detection shape as Objects, but reads
    // rather than writes. Tries a thing under the cursor first
    // (handle_eyedropper_thing_sample(), docs/refactor/editor/
    // 09-toolbox-remainder.md §1), then falls back to sampling the slab
    // kind/owner directly (get_slabmap_block()/slabmap_owner(), kfx_sim --
    // a lower layer than kfx_editor, same "read world truth directly"
    // precedent handle_object_placement_click() established for
    // screen_to_map()); either way it updates the local shadow so the
    // matching picker's highlight jumps to match immediately, and sends a
    // packet so the server-side selection it's backed by picks up the
    // sampled value atomically.
    //
    // Deliberately does NOT also switch back to the Terrain work state
    // here. Found live: an earlier version called set_work_state() (its
    // own set_players_packet_action(..., PckA_SetPlyrState, ...)) right
    // after the sample's own set_players_packet_action(...,
    // PckA_EditorEyedropperTerrain, ...) -- two calls in the same click.
    // Both write through the SAME per-turn packet slot (documented
    // repeatedly elsewhere in this file: only one action fits per click),
    // so the second call silently clobbered the first before either was
    // ever processed -- the sample packet never went out at all, work_state
    // flipped back to PSt_PlaceTerrain immediately, and the very click that
    // was supposed to sample painted with the stale previous selection
    // instead. The toolbox now stays on the Eyedropper tool after a pick
    // (one extra click to return to Terrain/Brush/Rectangle) rather than
    // risk a second same-click packet. Same reasoning is why
    // handle_eyedropper_thing_sample() sends at most one packet per hit.
    //
    // handle_eyedropper_thing_sample(): same "creature first, else nearest
    // thing" precedence Query's own handle_query_click() already
    // established; a hit updates whichever picker's selection matches the
    // thing's class (and sends
    // PckA_EditorEyedropperThing so the server-side field it's backed by --
    // CheatSelection or UserState -- picks it up atomically, same shape as
    // the terrain sample below) instead of falling through to the slab
    // sample. Returns true if a thing was sampled (caller should stop
    // there), false to fall through to terrain.
    bool handle_eyedropper_thing_sample(struct PlayerInfo *player, struct Coord3d *pos)
    {
        MapSubtlCoord stl_x = coord_subtile(pos->x.val);
        MapSubtlCoord stl_y = coord_subtile(pos->y.val);
        struct Thing *thing = get_creature_near(pos->x.val, pos->y.val);
        if (!thing_is_creature(thing))
            thing = get_nearest_thing_at_position(stl_x, stl_y);
        if (thing_is_invalid(thing))
            return false;
        switch (thing->class_id)
        {
            case TCls_Creature:
            {
                bool hero = (thing->owner == PLAYER_GOOD);
                if (hero)
                    s_selected_hero_kind = thing->model;
                else
                {
                    s_selected_creature_kind = thing->model;
                    s_selected_owner = thing->owner;
                }
                set_players_packet_action(player, PckA_EditorEyedropperThing,
                    TCls_Creature, thing->owner, thing->model, 0);
                return true;
            }
            case TCls_Object:
                // No server-side "chosen object" field to sync (F17 -- same
                // gap PckA_EditorPlaceObject's own picker works around):
                // purely a local selection update, same as clicking a row
                // in draw_object_picker() itself -- no packet needed.
                s_selected_object_model = thing->model;
                s_selected_owner = thing->owner;
                return true;
            case TCls_Trap:
                s_selected_trap_kind = thing->model;
                s_selected_owner = thing->owner;
                set_players_packet_action(player, PckA_EditorEyedropperThing,
                    TCls_Trap, thing->owner, thing->model, 0);
                return true;
            case TCls_Door:
                s_selected_door_kind = thing->model;
                s_selected_owner = thing->owner;
                set_players_packet_action(player, PckA_EditorEyedropperThing,
                    TCls_Door, thing->owner, thing->model, 0);
                return true;
            default:
                return false;
        }
    }

    void handle_eyedropper_click()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return;
        if (io.WantCaptureMouse)
            return;
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        if (!screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            return;
        if (handle_eyedropper_thing_sample(player, &pos))
            return;
        MapSlabCoord slb_x = subtile_slab(coord_subtile(pos.x.val));
        MapSlabCoord slb_y = subtile_slab(coord_subtile(pos.y.val));
        struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
        s_selected_terrain_kind = slb->kind;
        s_selected_owner = slab_kind_has_no_ownership(s_selected_terrain_kind)
            ? kfx_config_state.neutral_player_num : (PlayerNumber)slabmap_owner(slb);
        set_players_packet_action(player, PckA_EditorEyedropperTerrain, s_selected_terrain_kind, s_selected_owner, 0, 0);
    }

    void draw_texture_paint_picker()
    {
        FeCombo("Texture", &s_selected_texture_pack, kTexturePackItems, kTexturePackItemCount);
        FeBodyText("LMB-drag to paint slabs with this texture set.");
    }

    // docs/refactor/editor/05-script-and-level-settings.md's "per-slab
    // texture paint" item -- continuous drag-paint like Terrain's own
    // Brush mode, but writing straight into kfx_config_state.slab_ext_
    // data[] every held frame rather than through a packet (this tool's
    // own EdTool_PaintTexture/PSt_EditorPaintTexture comments explain why:
    // the same D2 single-player-local exception Stamp already uses).
    // engine_render.c's own per-frame texture-block lookup reads this
    // array live, so a painted slab's new texture shows up the very next
    // rendered frame with no extra reload step (unlike the Base Texture
    // Set dropdown, which changes which atlas is loaded at all and does
    // need one).
    void handle_texture_paint_click()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            return;
        if (io.WantCaptureMouse)
            return;
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        if (!screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            return;
        MapSlabCoord slb_x = subtile_slab(coord_subtile(pos.x.val));
        MapSlabCoord slb_y = subtile_slab(coord_subtile(pos.y.val));
        kfx_config_state.slab_ext_data[get_slab_number(slb_x, slb_y)] = (unsigned char)s_selected_texture_pack;
    }

    // §2.4 -- Brush (grab region -> stamp). Capture and stamp both happen
    // entirely here, in kfx_editor, calling sim mutation primitives
    // directly (place_slab_type_on_map() etc., kfx_sim -- fair game,
    // kfx_editor sits above it) rather than through a packet: the doc's own
    // sanctioned fallback (D2, single-player-local exception) for a stamp
    // that can't fit in one packet's params and would need an awkward
    // multi-turn packet queue otherwise (each set_players_packet_action()
    // call overwrites the single per-turn packet slot, so queuing a burst
    // of them safely needs a real turn-boundary signal kfx_editor doesn't
    // have -- calling the mutations directly sidesteps that entirely, same
    // as it already does for reads in handle_eyedropper_click()).
    //
    // docs/refactor/editor/09-toolbox-remainder.md §1 -- things capture
    // (creature/hero/digger/object/trap/door) added alongside the original
    // terrain-slab-only capture, same direct-mutation shape. Lights/Action
    // Points are NOT captured -- deferred to phase 5 along with those
    // tools' own placement paths (05-script-and-level-settings.md), since
    // there's nothing to capture until they exist. Neither slab nor thing
    // stamps are undo/redo-journaled (matches this tool's own pre-existing
    // "no undo for a stamp" scope, doc §4 -- staying consistent across both
    // kinds of stamp rather than journaling one and not the other).
    struct BrushSlabEntry { int dx, dy; SlabKind kind; PlayerNumber owner; };
    std::vector<BrushSlabEntry> s_brush_buffer;
    struct BrushThingEntry { int dx, dy; ThingClass class_id; ThingModel model; PlayerNumber owner; CrtrExpLevel exp_level; };
    std::vector<BrushThingEntry> s_brush_thing_buffer;
    bool s_brush_dragging = false;
    MapSlabCoord s_brush_drag_slb_x = 0;
    MapSlabCoord s_brush_drag_slb_y = 0;

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

    void handle_brush_capture_and_stamp()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.WantCaptureMouse)
            return;
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        if (!screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            return;
        MapSlabCoord cur_slb_x = subtile_slab(coord_subtile(pos.x.val));
        MapSlabCoord cur_slb_y = subtile_slab(coord_subtile(pos.y.val));

        // Capture: RMB-drag a box.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        {
            s_brush_dragging = true;
            s_brush_drag_slb_x = cur_slb_x;
            s_brush_drag_slb_y = cur_slb_y;
        }
        if (s_brush_dragging)
        {
            MapSlabCoord box_beg_x = min(s_brush_drag_slb_x, cur_slb_x);
            MapSlabCoord box_beg_y = min(s_brush_drag_slb_y, cur_slb_y);
            MapSlabCoord box_end_x = max(s_brush_drag_slb_x, cur_slb_x) + 1;
            MapSlabCoord box_end_y = max(s_brush_drag_slb_y, cur_slb_y) + 1;
            int floor_height_z = floor_height_for_volume_box(player->id_number, cur_slb_x, cur_slb_y);
            draw_map_volume_box(subtile_coord(slab_subtile(box_beg_x, 0), 0), subtile_coord(slab_subtile(box_beg_y, 0), 0),
                subtile_coord(slab_subtile(box_end_x, 0), 0), subtile_coord(slab_subtile(box_end_y, 0), 0), floor_height_z, SLC_YELLOW);
        }
        if (s_brush_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            s_brush_dragging = false;
            MapSlabCoord box_beg_x = min(s_brush_drag_slb_x, cur_slb_x);
            MapSlabCoord box_beg_y = min(s_brush_drag_slb_y, cur_slb_y);
            MapSlabCoord box_end_x = max(s_brush_drag_slb_x, cur_slb_x);
            MapSlabCoord box_end_y = max(s_brush_drag_slb_y, cur_slb_y);
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
            std::vector<ThingIndex> captured_indices;
            MapSubtlCoord anchor_stl_x = slab_subtile(box_beg_x, 0);
            MapSubtlCoord anchor_stl_y = slab_subtile(box_beg_y, 0);
            for (MapSlabCoord sy = box_beg_y; sy <= box_end_y; sy++)
            {
                for (MapSlabCoord sx = box_beg_x; sx <= box_end_x; sx++)
                {
                    for (int sub_y = 0; sub_y < STL_PER_SLB; sub_y++)
                    {
                        for (int sub_x = 0; sub_x < STL_PER_SLB; sub_x++)
                        {
                            MapSubtlCoord tstl_x = slab_subtile(sx, sub_x);
                            MapSubtlCoord tstl_y = slab_subtile(sy, sub_y);
                            struct Map *mapblk = get_map_block_at(tstl_x, tstl_y);
                            long ti = get_mapwho_thing_index(mapblk);
                            while (ti != 0)
                            {
                                struct Thing *thing = thing_get(ti);
                                if (thing_is_invalid(thing))
                                    break;
                                ti = thing->next_on_mapblk;
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

        // Stamp: LMB click (one stamp per click, at the buffer's captured
        // top-left anchored to the cursor's current slab).
        if (!s_brush_buffer.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
                if (subtile_is_room(tile_stl_x, tile_stl_y))
                    delete_room_slab(tsx, tsy, true);
                if (slab_kind_is_animated(entry.kind))
                    place_animating_slab_type_on_map(entry.kind, 0, tile_stl_x, tile_stl_y, entry.owner);
                else
                    place_slab_type_on_map(entry.kind, tile_stl_x, tile_stl_y, entry.owner, 0);
                do_slab_efficiency_alteration(tsx, tsy);
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
        if (!s_brush_thing_buffer.empty() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
                        player_place_trap_without_check_at(tstl_x, tstl_y, entry.owner, entry.model, true);
                        break;
                    case TCls_Door:
                        player_place_door_without_check_at(tstl_x, tstl_y, entry.owner, entry.model, true);
                        break;
                    default:
                        break;
                }
            }
        }
    }

    // §2.10 -- Query inspector. Creature queries go through the existing,
    // already-ImGui-migrated query_creature() (GMnu_CREATURE_QUERY1-4,
    // frontgui_ingame_creature.cpp's creature_query_panel()) -- called
    // directly rather than via a packet, same "single-player-local
    // exception" reasoning Brush/Stamp already established (it only
    // toggles UI state, no world mutation). Everything else (objects,
    // traps, doors, rooms/slabs) is read directly here and rendered in
    // this tiny panel instead of going through query_thing()/query_room(),
    // both of which call create_message_box() -- a classic, unmigrated
    // GMnu_MSG_BOX popup that doesn't fit inside an otherwise all-ImGui
    // editor session.
    struct QueryResult {
        enum Kind { QR_None, QR_Thing, QR_Room } kind = QR_None;
        char title[32] = "";
        char name[64] = "";
        char owner[24] = "";
        char health[32] = "";
        char extra1[48] = "";
        char extra2[48] = "";
    };
    QueryResult s_query_result;

    void query_result_from_thing(struct Thing *thing, QueryResult *out)
    {
        out->kind = QueryResult::QR_Thing;
        snprintf(out->title, sizeof(out->title), "Thing #%d", thing->index);
        snprintf(out->name, sizeof(out->name), "%s", thing_model_name(thing));
        snprintf(out->owner, sizeof(out->owner), "Owner: %d", (int)thing->owner);
        snprintf(out->extra1, sizeof(out->extra1), "Pos: %d, %d, %d",
            (int)thing->mappos.x.stl.num, (int)thing->mappos.y.stl.num, (int)thing->mappos.z.stl.num);
        out->extra2[0] = '\0';
        switch (thing->class_id)
        {
            case TCls_Trap:
            {
                struct TrapConfigStats *trapst = get_trap_model_stats(thing->model);
                snprintf(out->health, sizeof(out->health), "Health: %d", (int)thing->health);
                snprintf(out->extra2, sizeof(out->extra2), "Shots: %d/%d", (int)thing->trap.num_shots, (int)trapst->shots);
                break;
            }
            case TCls_Object:
            {
                struct ObjectConfigStats *objst = get_object_model_stats(thing->model);
                snprintf(out->health, sizeof(out->health), "Health: %d/%d", (int)thing->health, (int)objst->health);
                if (object_is_gold(thing))
                    snprintf(out->extra2, sizeof(out->extra2), "Amount: %d", (int)thing->valuable.gold_stored);
                break;
            }
            case TCls_Door:
            {
                struct DoorConfigStats *doorst = get_door_model_stats(thing->model);
                snprintf(out->health, sizeof(out->health), "Health: %d/%d", (int)thing->health, (int)doorst->health);
                snprintf(out->extra2, sizeof(out->extra2), "%s", thing->door.is_locked ? "Locked" : "Unlocked");
                break;
            }
            default:
                snprintf(out->health, sizeof(out->health), "Health: %d", (int)thing->health);
                break;
        }
    }

    void query_result_from_room(struct Room *room, QueryResult *out)
    {
        out->kind = QueryResult::QR_Room;
        snprintf(out->title, sizeof(out->title), "Room #%d", room->index);
        snprintf(out->name, sizeof(out->name), "%s", room_code_name(room->kind));
        snprintf(out->owner, sizeof(out->owner), "Owner: %d", (int)room->owner);
        snprintf(out->health, sizeof(out->health), "Health: %d", (int)room->health);
        snprintf(out->extra1, sizeof(out->extra1), "Capacity: %d/%d", (int)room->used_capacity, (int)room->total_capacity);
        float efficiency_pct = ((float)room->efficiency / (float)ROOM_EFFICIENCY_MAX) * 100.0f;
        snprintf(out->extra2, sizeof(out->extra2), "Efficiency: %d", (int)(efficiency_pct + 0.5f));
    }

    void handle_query_click()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            return;
        if (io.WantCaptureMouse)
            return;
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        if (!screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            return;
        MapSubtlCoord stl_x = coord_subtile(pos.x.val);
        MapSubtlCoord stl_y = coord_subtile(pos.y.val);

        // Same "creature first, else nearest thing" precedence
        // packets_cheats.c's own PSt_QueryAll case uses.
        struct Thing *thing = get_creature_near(pos.x.val, pos.y.val);
        if (!thing_is_creature(thing))
            thing = get_nearest_thing_at_position(stl_x, stl_y);

        if (!thing_is_invalid(thing) && thing_is_creature(thing))
        {
            // Already-migrated ImGui panel owns the screen for this --
            // clear our own result so this panel doesn't also show stale
            // data underneath/alongside it.
            s_query_result = QueryResult();
            query_creature(player, thing->index, true, false);
            return;
        }
        if (!thing_is_invalid(thing))
        {
            query_result_from_thing(thing, &s_query_result);
            return;
        }
        struct Room *room = subtile_room_get(stl_x, stl_y);
        if (room_exists(room))
        {
            query_result_from_room(room, &s_query_result);
            return;
        }
        s_query_result = QueryResult();
    }

    void draw_query_inspector()
    {
        if (s_query_result.kind == QueryResult::QR_None)
        {
            FeBodyText("Click a slab, thing, or room to inspect it.");
            return;
        }
        FeSubheading(s_query_result.title);
        FeBodyText(s_query_result.name);
        FeBodyText(s_query_result.owner);
        FeBodyText(s_query_result.health);
        if (s_query_result.extra1[0] != '\0')
            FeBodyText(s_query_result.extra1);
        if (s_query_result.extra2[0] != '\0')
            FeBodyText(s_query_result.extra2);
    }

    // §2 "shared bottom bar" -- player selector (0-3/Neutral) and
    // experience level (1-10), feeding cheatselection for whichever tool
    // is active (terrain owner, creature/digger owner+level). Hero-player
    // omitted from this first slice -- P0-P3 + Neutral covers the normal
    // 4-keeper map.
    void draw_bottom_bar()
    {
        struct PlayerInfo *player = get_my_player();
        FeSeparator();
        // Bracketed label marks the current selection -- FeButton (unlike
        // FeNavButton/FeListRow) has no built-in "selected" look, and this
        // row is laid out horizontally via SameLine(), not the vertical
        // list FeNavButton assumes. Found live: no feedback at all here
        // made every bottom-bar click indistinguishable from a no-op.
        ImGui::TextUnformatted("Owner:");
        for (int p = 0; p < 4; p++)
        {
            ImGui::SameLine();
            char label[10];
            snprintf(label, sizeof(label), (p == s_selected_owner) ? "[P%d]" : "P%d", p);
            if (FeButton(label))
            {
                s_selected_owner = p;
                set_players_packet_action(player, PckA_CheatSwitchPlayer, p, 0, 0, 0);
            }
        }
        ImGui::SameLine();
        {
            bool neutral_selected = (s_selected_owner == kfx_config_state.neutral_player_num);
            if (FeButton(neutral_selected ? "[Neutral]" : "Neutral"))
            {
                s_selected_owner = kfx_config_state.neutral_player_num;
                set_players_packet_action(player, PckA_CheatSwitchPlayer,
                    kfx_config_state.neutral_player_num, 0, 0, 0);
            }
        }

        ImGui::TextUnformatted("Level:");
        for (int lvl = 1; lvl <= 10; lvl++)
        {
            ImGui::SameLine();
            char label[8];
            // chosen_experience_level is 0-indexed on the wire (packets_cheats.c
            // displays it as "+1") -- lvl-1 here, not lvl.
            snprintf(label, sizeof(label), (lvl - 1 == s_selected_level) ? "[%d]" : "%d", lvl);
            if (FeButton(label))
            {
                s_selected_level = lvl - 1;
                set_players_packet_action(player, PckA_CheatSwitchExperience, lvl - 1, 0, 0, 0);
            }
        }
    }

    // docs/refactor/editor/10-definable-keybindings.md (D6) -- the
    // toolbox's genuinely *definable* tool-switch shortcuts (Ctrl+Z/Ctrl+Y
    // stay hardcoded on purpose, per user direction: standard everywhere,
    // no D6 need). Scoped to the 4 tools picked as the highest-value
    // subset -- toggled back to constantly, and usable standalone with no
    // follow-up model-picker click the way Creature/Object/Trap/Door/
    // Fill/Stamp all need -- not all 12; more can be added later if
    // needed. Each just switches s_active_tool + calls set_work_state(),
    // same as clicking that tool's own tool-strip button.
    //
    // is_editor_key_pressed() (front_input.h, kfx_frontend -- this doc's
    // own separate editor-keybinding storage) reads the classic
    // bflib_keybrd key-state array (kfx_platform), a separate listener on
    // the same raw input stream ImGui's own backend reads -- nothing in
    // this codebase gates it on ImGui's keyboard focus
    // (ImGuiContextWantCaptureKeyboard() exists but is never called
    // anywhere). Guarding on !io.WantCaptureKeyboard here explicitly, same
    // idea as the mouse-click handlers' own !io.WantCaptureMouse checks
    // elsewhere in this file: without it, typing "e"/"t"/"q"/"r" into one
    // of this toolbox's own ImGui text fields (the X/Y/Z position editor,
    // say) would *also* switch tools out from under the mapmaker mid-edit.
    void handle_editor_tool_shortcuts()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.WantCaptureKeyboard)
            return;
        if (is_editor_key_pressed(Gkey_EditorEraseTool, true, false))
        {
            s_active_tool = EdTool_Erase;
            set_work_state(PSt_DestroyThing);
        }
        else if (is_editor_key_pressed(Gkey_EditorTerrainTool, true, false))
        {
            s_active_tool = EdTool_Terrain;
            set_work_state(terrain_mode_work_state(s_terrain_mode));
        }
        else if (is_editor_key_pressed(Gkey_EditorQueryTool, true, false))
        {
            s_active_tool = EdTool_Query;
            set_work_state(PSt_EditorQuery);
        }
        else if (is_editor_key_pressed(Gkey_EditorEyedropperTool, true, false))
        {
            s_active_tool = EdTool_Eyedropper;
            set_work_state(PSt_EditorEyedropper);
        }
    }

} // namespace

void editor_toolbox_frame(void)
{
    handle_editor_tool_shortcuts();

    ImGui::SetNextWindowPos(ImVec2(20, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("##EditorToolbox", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

    FeHeading("Toolbox");
    // docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md -- Menu/
    // Undo/Redo moved from here into the new File/Edit menu bar
    // (editor_menubar.cpp): Menu's old items -> File (New/Open/Save/Save
    // As/Exit); Undo/Redo -> Edit. Ctrl+Z/Ctrl+Y (editor_journal_frame(),
    // still hardcoded -- standard everywhere, no D6 need) remain the
    // primary path either way.
    //
    // docs/refactor/editor/09-toolbox-remainder.md backlog item -- "thing
    // counter" (02 §5 item 4). Synced things only (kfx_sim_state's own
    // synced_free_things_count/SYNCED_THINGS_COUNT bookkeeping,
    // thing_data.c's create_thing() already computes the identical "%d/%d
    // slots used" figure for its own out-of-slots warning) -- the unsynced
    // bucket is transient local effects (sparks, etc.), not map content a
    // mapmaker would think of as a "thing" they placed or need to budget
    // for. Deliberately just the raw engine ceiling (THINGS_COUNT's own
    // SYNCED half), not the "classic-compatible vs KeeperFX target mode"
    // ceiling docs/refactor/editor/07-investigation-findings.md's F18
    // describes -- that needs verify_map()'s own target-mode selector
    // (phase 3), which doesn't exist yet.
    {
        long used_synced = SYNCED_THINGS_COUNT - kfx_sim_state.synced_free_things_count;
        char counter[48];
        snprintf(counter, sizeof(counter), "Things: %ld / %d", used_synced, SYNCED_THINGS_COUNT);
        FeCaption(counter);
    }
    FeSeparator();
    draw_tool_strip();
    FeSeparator();

    switch (s_active_tool)
    {
        case EdTool_Terrain:
            draw_terrain_mode_toggle();
            // Clear Earth/Delete Things/Set Owner have no *kind* to pick
            // (a fixed target, none at all, or driven by the bottom bar's
            // owner selector instead) -- the palette would be misleading
            // in any of them.
            if (s_terrain_mode != TerrainMode_ClearEarth && s_terrain_mode != TerrainMode_DeleteThings
                && s_terrain_mode != TerrainMode_SetOwner)
                draw_terrain_picker();
            break;
        // Fill reuses the exact same terrain palette/selection as the
        // Terrain tool -- packets_cheats.c's PSt_EditorFill keys off the
        // same chosen_terrain_kind/chosen_player, no separate fill-target
        // state to pick.
        case EdTool_Fill:         draw_terrain_picker(); break;
        case EdTool_CreatureEvil: draw_creature_picker(false); break;
        case EdTool_CreatureHero: draw_creature_picker(true); break;
        case EdTool_Object:       draw_object_picker(); break;
        case EdTool_Trap:         draw_trap_picker(); break;
        case EdTool_Door:         draw_door_picker(); break;
        case EdTool_Stamp:
        {
            char status[64];
            if (s_brush_buffer.empty())
                snprintf(status, sizeof(status), "RMB-drag to capture a terrain area.");
            else
                snprintf(status, sizeof(status), "%zu slabs captured. LMB to stamp.", s_brush_buffer.size());
            FeBodyText(status);
            break;
        }
        case EdTool_PaintTexture: draw_texture_paint_picker(); break;
        case EdTool_Points:       editor_points_draw_panel(); break;
        case EdTool_Query: draw_query_inspector(); break;
        // Digger/Erase need no picker -- they're a bare work-state switch
        // (§2.5/§2.10); the bottom bar below still applies (owner for
        // diggers).
        default: break;
    }

    draw_bottom_bar();

    ImGui::End();

    // Deliberately outside the toolbox's own Begin/End -- this checks
    // clicks anywhere on screen (the 3D dungeon view), not just inside
    // this window; io.WantCaptureMouse inside the function itself is what
    // stops a click on the toolbox from also registering as a placement.
    if (s_active_tool == EdTool_Object)
        handle_object_placement_click();
    if (s_active_tool == EdTool_Eyedropper)
        handle_eyedropper_click();
    if (s_active_tool == EdTool_Stamp)
        handle_brush_capture_and_stamp();
    if (s_active_tool == EdTool_PaintTexture)
        handle_texture_paint_click();
    if (s_active_tool == EdTool_Query)
        handle_query_click();
    if (s_active_tool == EdTool_Points)
        editor_points_frame();
}
/******************************************************************************/
