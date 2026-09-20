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
#include "editor_reinforce.h"
#include "editor_brush.h"
#include "editor_texture_paint.h"
#include "editor_query.h"
#include "editor_things.h"
#include "thing_effects.h" // destroy_effect_thing -- RMB delete
#include "magic_powers.h"
#include "room_workshop.h"
#include <cstring>

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
#include "editor_icon_grid.h"
#include "frontgui_ingame_cells.h" // fe_hud_cell -- owner symbol tiles
#include "config_spritecolors.h" // get_player_colored_icon_idx
#include "sprites.h" // GPS_plyrsym_*
#include "editor_palette.h"
#include "editor_thumbs.h"
#include "creature_graphics.h" // get_creature_model_graphics, CGI_HandSymbol
#include "config_strings.h"    // get_string
#include "config_magic.h"      // get_power_model_stats
#include "config_terrain.h"
#include "config_objects.h"
#include "config_trapdoor.h"
#include <cstdio>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

    enum EditorTool {
        EdTool_Terrain,
        // Utilities that act on a marked area rather than painting a kind:
        // they were modes of the Terrain tool, but have no palette.
        EdTool_ClearEarth,
        EdTool_DeleteThings,
        EdTool_SetOwner,
        EdTool_Reinforce,
        // Creature, Hero and Digger were one tool with three buttons; the only
        // real difference is the owner (PLAYER_GOOD = hero) and a digger is
        // just the imp/tunneller model, so it is one tool now.
        EdTool_Creature,
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
    int s_texture_paint_mode = 0; // 0 brush, 1 rectangle, 2 fill
    ThingModel s_selected_creature_kind = 1;
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
    // The "Thing" button covers three placement modes -- objects, traps and
    // doors -- each with its own work state. s_active_tool holds the live
    // one (EdTool_Object / EdTool_Trap / EdTool_Door); this remembers which
    // to return to when the button is clicked again from another tool.
    EditorTool s_thing_mode = EdTool_Object;

    // Toolbox window / tab state. The tool strip is three levels of tabs
    // (Terrain|Things|Utility|History, then the tools of that tab, then a
    // tool's own pages) so a tool's picker is only ever on screen inside its
    // own tab. ImGui remembers which tab of each bar is selected; the s_force_*
    // values are one-shot requests from code that changes the active item (the
    // eyedropper, tool shortcuts) asking the matching tabs to follow. -1 = none.
    bool s_toolbox_open = true;
    int s_force_top = -1;              // TopTab
    int s_force_tool = -1;             // EditorTool (Thing = EdTool_Object)
    int s_force_terrain_group = -1;    // EditorTerrainGroup
    int s_force_thing_group = -1;      // ThingGroup
    int s_force_terrain_mode = -1;     // TerrainMode
    bool s_history_visible = false;    // History tab showing: no tool is "live"

    enum TopTab { TT_Terrain, TT_Things, TT_Utility, TT_Area, TT_History };
    enum ThingGroup { TG_Spells, TG_Specials, TG_TrapsDoors, TG_Decor };
    PlayerNumber s_selected_owner = 0;
    // §2.2/§2.4 -- Terrain tool's mode row: Brush (continuous drag-paint,
    // PSt_PlaceTerrain), Rectangle (mark a box, commit on release,
    // PSt_EditorPlaceTerrainRect), Clear Earth (same mark-a-box shape,
    // fixed target, PSt_EditorRectClearEarth), or Delete Things (same
    // shape again, sweeps the box for things instead of repainting slabs,
    // PSt_EditorRectDeleteThings). Toolbox-local only -- it just decides
    // which work state the Terrain button/toggle sends next, nothing
    // server-side needs to know this exists as a persistent selection.
    enum TerrainMode { TerrainMode_Brush, TerrainMode_Rectangle, TerrainMode_Fill };
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
            case TerrainMode_Fill:         return PSt_EditorFill;
            default:                       return PSt_PlaceTerrain;
        }
    }

    // One packet action per click, deliberately -- set_players_packet_action()
    // overwrites the single per-turn packet slot, so a tool switch can't
    // also re-sync the chosen kind in the same click (that would just
    // clobber the PckA_SetPlyrState with a PckA_CheatSwitch* before
    // either is ever processed). Same one-action-per-click shape the
    // classic cheat menu's gf_change_player_state() has.
    unsigned char thing_mode_work_state(EditorTool mode)
    {
        switch (mode)
        {
            case EdTool_Trap: return PSt_EditorPlaceTrap;
            case EdTool_Door: return PSt_EditorPlaceDoor;
            default:          return PSt_EditorPlaceObject;
        }
    }

    unsigned char tool_work_state(EditorTool tool)
    {
        switch (tool)
        {
            case EdTool_Terrain:      return terrain_mode_work_state(s_terrain_mode);
            case EdTool_ClearEarth:   return PSt_EditorRectClearEarth;
            case EdTool_DeleteThings: return PSt_EditorRectDeleteThings;
            case EdTool_SetOwner:     return PSt_EditorRectSetOwner;
            // A button tool: the work state only has to be one a click does nothing in.
            case EdTool_Reinforce:    return PSt_EditorQuery;
            case EdTool_Creature:     return PSt_MkBadCreatr;
            case EdTool_Object:
            case EdTool_Trap:
            case EdTool_Door:         return thing_mode_work_state(s_thing_mode);
            case EdTool_Points:       return PSt_EditorPlacePoint;
            case EdTool_Eyedropper:   return PSt_EditorEyedropper;
            case EdTool_Stamp:        return PSt_EditorStamp;
            case EdTool_PaintTexture: return PSt_EditorPaintTexture;
            case EdTool_Query:        return PSt_EditorQuery;
            default:                  return PSt_DestroyThing; // Erase
        }
    }

    // The three placement modes share one tab ("Thing"), keyed by EdTool_Object.
    EditorTool tab_tool(EditorTool tool)
    {
        return (tool == EdTool_Trap || tool == EdTool_Door) ? EdTool_Object : tool;
    }

    int top_of_tool(EditorTool tool)
    {
        switch (tab_tool(tool))
        {
            case EdTool_Terrain:      return TT_Terrain;
            case EdTool_Creature:
            case EdTool_Object:
            case EdTool_Points:       return TT_Things;
            case EdTool_ClearEarth:
            case EdTool_DeleteThings:
            case EdTool_SetOwner:
            case EdTool_Reinforce:    return TT_Area;
            default:                  return TT_Utility;
        }
    }

    // Makes `tool` the live tool (one work-state packet). "Thing" resumes the
    // last-used placement mode.
    void activate_tool(EditorTool tool)
    {
        s_active_tool = (tab_tool(tool) == EdTool_Object) ? s_thing_mode : tool;
        set_work_state(tool_work_state(s_active_tool));
    }

    // Asks the tab bars to show `tool`'s tab (used when code, not a click on a
    // tab, changes the active tool: the eyedropper, keyboard shortcuts).
    void focus_tool_tabs(EditorTool tool)
    {
        s_force_top = top_of_tool(tool);
        s_force_tool = tab_tool(tool);
    }

    // Keeps the engine's work state in step with the toolbox's active tool.
    // Selections are single-slot packets, and the engine can also change the
    // work state itself (right-click cancel, a finished placement), which left
    // the toolbox showing a tool that no longer did anything until another tab
    // was visited. Re-sends the work state whenever it has drifted, but never
    // over a packet already queued this turn, and only in the plain editing
    // view (first person etc. legitimately use other states).
    void reconcile_work_state()
    {
        if (s_history_visible)
            return;
        struct PlayerInfo *player = get_my_player();
        if (player->view_type != PVT_DungeonTop)
            return;
        if (get_players_packet_action(player) != PckA_None)
            return;
        const unsigned char want = tool_work_state(s_active_tool);
        if (player->work_state != want)
            set_work_state(want);
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

    // §2.1 -- slab palette generated from kfx_config_state.conf.slab_conf, so
    // modded slab kinds appear automatically (F15). phase6/00: split into
    // Terrain / Rooms / Other tabs (editor_palette.cpp decides which), drawn
    // as the in-game icon table -- room slabs show their room icon, the rest
    // are text tiles (no game icon exists for a bare terrain slab).
    // Each nesting level of tabs is tinted a little differently (level 0 is the
    // theme's own colours) so it is clear which bar a tab belongs to. The tint
    // is blended into the theme's tab colours rather than replacing them, so
    // it follows whatever the UI style is.
    struct TabTint
    {
        int pushed = 0;
        explicit TabTint(int level)
        {
            static const ImVec4 kTints[] = {
                ImVec4(0.0f, 0.0f, 0.0f, 0.0f),      // level 0: theme colours
                ImVec4(0.70f, 0.30f, 0.25f, 0.30f),  // level 1: warm red
                ImVec4(0.30f, 0.60f, 0.30f, 0.30f),  // level 2: green
                ImVec4(0.30f, 0.40f, 0.75f, 0.30f),  // level 3: blue
            };
            if (level <= 0 || level >= (int)(sizeof(kTints) / sizeof(kTints[0])))
                return;
            const ImVec4 &t = kTints[level];
            static const ImGuiCol cols[] = {
                ImGuiCol_Tab, ImGuiCol_TabHovered, ImGuiCol_TabSelected, ImGuiCol_TabDimmed, ImGuiCol_TabDimmedSelected,
            };
            for (ImGuiCol c : cols)
            {
                ImVec4 base = ImGui::GetStyleColorVec4(c);
                ImVec4 mixed(base.x + (t.x - base.x) * t.w, base.y + (t.y - base.y) * t.w,
                    base.z + (t.z - base.z) * t.w, base.w);
                ImGui::PushStyleColor(c, mixed);
                pushed++;
            }
        }
        ~TabTint() { if (pushed > 0) ImGui::PopStyleColor(pushed); }
    };

    // One page of a tab bar, honouring a one-shot force request.
    bool tab_page(const char *label, int *force_var, int my_value)
    {
        const bool force = (*force_var == my_value);
        const bool shown = FeTabEx(label, force);
        if (force)
            *force_var = -1;
        return shown;
    }

    short manufacture_icon(ThingClass tngclass, ThingModel tngmodel); // defined with the Thing picker below

    void select_terrain(SlabKind kind)
    {
        s_selected_terrain_kind = kind;
        // Written straight into the local selection (single-player-local, D2)
        // rather than via PckA_CheatSwitchTerrain: a packet set from the render
        // phase can be overwritten by input()'s own packet for the same turn,
        // which left the highlight on the new tile but the old kind painting.
        struct UserState *ustate = get_player_user_state(get_my_player());
        if (ustate != NULL)
            ustate->cheatselection.chosen_terrain_kind = kind;
    }

    // The Dungeon Heart object (the "soul container" in the Things tab).
    ThingModel heart_object_model()
    {
        const long count = kfx_config_state.conf.object_conf.object_types_count;
        for (ThingModel m = 1; m < (ThingModel)count; m++)
        {
            const struct ObjectConfigStats *ostat = get_object_model_stats(m);
            if ((ostat != NULL) && ((ostat->model_flags & OMF_Heart) != 0))
                return m;
        }
        return 0;
    }

    void draw_terrain_group(int group)
    {
        const struct SlabsConfig &slabc = kfx_config_state.conf.slab_conf;
        static const char *const grid_ids[ETG_Count] = {"##EdTerrainGrid", "##EdRoomGrid", "##EdWallGrid", "##EdOtherGrid"};
        editor_icon_grid_begin(grid_ids[group], 280.0f, 5);
        for (int32_t i = 0; i < slabc.slab_types_count; i++)
        {
            if (editor_terrain_group_of((SlabKind)i) != group)
                continue;
            // Doors are placed from Things > Traps & Doors (with their thing);
            // a second door tile here would only duplicate it.
            if (slab_kind_is_door((SlabKind)i))
                continue;
            EditorIconTile tile;
            char id[16];
            snprintf(id, sizeof(id), "s%d", (int)i);
            tile.id = id;
            tile.selected = ((SlabKind)i == s_selected_terrain_kind);
            const char *code = slab_code_name((SlabKind)i);
            tile.label = editor_icon_grid_pretty(code);
            tile.tooltip = editor_icon_grid_pretty(code);
            const RoomKind room = (group == ETG_Rooms) ? editor_room_of_slab((SlabKind)i)
                : ((group == ETG_Walls) ? editor_room_of_wall((SlabKind)i) : 0);
            static char tip[96];
            if ((room == RoK_DUNGHEART) && (heart_object_model() > 0))
            {
                // The heart's pedestal and walls show the Dungeon Heart itself,
                // the same picture as its object in the Things tab.
                const EditorThumb th = editor_thumb_object(heart_object_model());
                tile.thumb = th.texture;
                tile.thumb_w = th.width;
                tile.thumb_h = th.height;
                const struct RoomConfigStats *rs = get_room_kind_stats(room);
                static char heart_tip[96];
                snprintf(heart_tip, sizeof(heart_tip), (group == ETG_Walls) ? "%s wall" : "%s",
                    get_string(rs->name_stridx));
                tile.tooltip = heart_tip;
            }
            else if (room > 0)
            {
                // A room floor, or that room's wall: the room's own icon.
                const struct RoomConfigStats *rs = get_room_kind_stats(room);
                tile.sprite = (short)rs->medsym_sprite_idx;
                tile.ov_kind = EIO_ActiveInactive;
                tile.ov_category = "room";
                tile.ov_code = room_code_name(room);
                if (group == ETG_Walls)
                {
                    snprintf(tip, sizeof(tip), "%s wall", get_string(rs->name_stridx));
                    tile.tooltip = tip;
                }
                else
                    tile.tooltip = get_string(rs->name_stridx);
            }
            else
            {
                // No game icon for a bare slab: its wall front / floor top.
                const EditorThumb th = editor_thumb_slab((SlabKind)i);
                tile.thumb = th.texture;
                tile.thumb_w = th.width;
                tile.thumb_h = th.height;
            }
            if (editor_icon_grid_tile(tile))
                select_terrain((SlabKind)i);
        }
        editor_icon_grid_end();
    }

    void draw_terrain_picker(int tint_level)
    {
        TabTint tint(tint_level);
        bool tabs = FeBeginTabBar("##EdTerrainTabs");
        if (tabs)
        {
            static const char *const labels[ETG_Count] = {"Terrain", "Rooms", "Walls", "Other"};
            for (int g = 0; g < ETG_Count; g++)
                if (tab_page(labels[g], &s_force_terrain_group, g))
                {
                    draw_terrain_group(g);
                    FeEndTab();
                }
        }
        FeEndTabBar(tabs);
    }

    // §2.5 -- one Creature palette for evil creatures, heroes and diggers.
    // What makes a creature "a hero" is only its owner (PLAYER_GOOD, the
    // bottom bar's Hero button), and a digger is just the imp / tunneller
    // model, so they were never different tools. The grid groups by the
    // model's own evil flag purely as a convenience. Custom/campaign-modded
    // creatures appear automatically (F15).
    void select_creature(ThingModel model)
    {
        s_selected_creature_kind = model;
        struct UserState *ustate = get_player_user_state(get_my_player());
        if (ustate != NULL)
            ustate->cheatselection.chosen_creature_kind = model;
    }

    void draw_creature_picker()
    {
        const long model_count = kfx_config_state.conf.crtr_conf.model_count;
        editor_icon_grid_begin("##EdCreatureGrid", 300.0f, 5);
            static const char *const titles[3] = {"Evil creatures", "Heroes", "Other"};
        for (int group = 0; group < 3; group++)
        {
            bool heading_done = false;
            for (ThingModel m = 1; m < (ThingModel)model_count; m++)
            {
                const struct CreatureModelConfig *crconf = creature_stats_get(m);
                int g = ((crconf->model_flags & CMF_IsSpectator) != 0) ? 2
                    : (((crconf->model_flags & CMF_IsEvil) != 0) ? 0 : 1);
                if (g != group)
                    continue;
                if (!heading_done)
                {
                    editor_icon_grid_heading(titles[group]);
                    heading_done = true;
                }
                EditorIconTile tile;
                char id[16];
                snprintf(id, sizeof(id), "c%d", (int)m);
                tile.id = id;
                tile.sprite = get_creature_model_graphics(m, CGI_HandSymbol);
                tile.ov_kind = EIO_Single;
                tile.ov_category = "creature_icon";
                tile.ov_code = creature_code_name(m);
                tile.label = editor_icon_grid_pretty(creature_code_name(m));
                char tip[96];
                snprintf(tip, sizeof(tip), "%s (%s)", get_string(crconf->namestr_idx), creature_code_name(m));
                tile.tooltip = tip;
                tile.selected = (m == s_selected_creature_kind);
                if (editor_icon_grid_tile(tile))
                    select_creature(m);
            }
        }
        editor_icon_grid_end();
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
            EditorThingProps before, after;
            editor_journal_thing_props(thing->index, &before);
            after = before;
            after.x = s_edit_pos_x;
            after.y = s_edit_pos_y;
            after.z = s_edit_pos_z;
            editor_journal_record_thing_edit(thing->index, &before, &after);
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
                EditorThingProps before, after;
                editor_journal_thing_props(thing->index, &before);
                after = before;
                after.gold = s_object_gold_value;
                editor_journal_record_thing_edit(thing->index, &before, &after);
                set_players_packet_action(player, PckA_EditorSetGoldValue, thing->index, s_object_gold_value, 0, 0);
            }
        }
    }

    // phase6/00 -- the "Thing" palette: one picker, four tabs, replacing the
    // separate Object / Trap / Door pickers. Spells (spellbooks, shown with
    // the power's icon), Specials (special boxes), Traps & Doors (the
    // workshop items with their in-game icons, plus the crates that contain
    // them) and Decor (everything else the object config defines).
    // Picking an item switches the placement mode to match: objects use the
    // render-phase click handler below, traps and doors their own work
    // states (packets_cheats.c), so selecting one is also what changes
    // s_active_tool / the work state.
    void select_thing_mode(EditorTool mode)
    {
        s_thing_mode = mode;
        if (s_active_tool != mode)
        {
            s_active_tool = mode;
            set_work_state(thing_mode_work_state(mode));
        }
    }

    // Trap / door: the chosen kind lives in the local user state
    // (UserState::chosen_trap_kind/_door_kind -- what PSt_EditorPlaceTrap/
    // PSt_EditorPlaceDoor read to build the placement packet). It is set
    // directly rather than with PckA_CheatSwitchTrap/Door because picking an
    // item from another placement mode also has to change the work state, and
    // set_players_packet_action() has one slot per turn -- the second packet
    // would overwrite the first. Same single-player-local exception (D2) as
    // the rest of this file's direct edits.
    void select_placement_kind(EditorTool mode, ThingModel kind)
    {
        struct UserState *ustate = get_player_user_state(get_my_player());
        if (ustate != NULL)
        {
            if (mode == EdTool_Trap)
                ustate->chosen_trap_kind = kind;
            else
                ustate->chosen_door_kind = kind;
        }
        select_thing_mode(mode);
    }

    void select_object(ThingModel model)
    {
        s_selected_object_model = model;
        select_thing_mode(EdTool_Object);
    }

    // Icon for a workshop item (trap/door) from the manufacture table, the
    // same source the in-game workshop grid uses.
    short manufacture_icon(ThingClass tngclass, ThingModel tngmodel)
    {
        int idx = get_manufacture_data_index_for_thing(tngclass, tngmodel);
        if (idx <= 0)
            return 0;
        const struct ManufactureData *md = get_manufacture_data(idx);
        return (md != NULL) ? (short)md->medsym_sprite_idx : 0;
    }

    void object_tile_common(EditorIconTile &tile, ThingModel m, int group)
    {
        tile.label = editor_icon_grid_pretty(object_code_name(m));
        tile.tooltip = object_code_name(m);
        tile.selected = (s_active_tool == EdTool_Object) && (m == s_selected_object_model);
        if (group == EOG_Spells)
        {
            const int power = editor_spellbook_power(m);
            if (power > 0)
            {
                const struct PowerConfigStats *ps = get_power_model_stats((PowerKind)power);
                tile.sprite = (short)ps->medsym_sprite_idx;
                tile.ov_kind = EIO_ActiveInactive;
                tile.ov_category = "power";
                tile.ov_code = power_code_name((PowerKind)power);
                tile.tooltip = get_string(ps->name_stridx);
            }
        }
        else if (group == EOG_Crates)
        {
            tile.sprite = manufacture_icon(crate_to_workshop_item_class(m), crate_to_workshop_item_model(m));
        }
        else
        {
            // Specials and decor: the object's own in-world sprite.
            const EditorThumb th = editor_thumb_object(m);
            tile.thumb = th.texture;
            tile.thumb_w = th.width;
            tile.thumb_h = th.height;
        }
    }

    void draw_object_group(int group, int cols, const char *grid_id)
    {
        const long model_count = kfx_config_state.conf.object_conf.object_types_count;
        editor_icon_grid_begin(grid_id, 280.0f, cols);
        for (ThingModel m = 1; m < (ThingModel)model_count; m++)
        {
            if (editor_object_group_of(m) != group)
                continue;
            EditorIconTile tile;
            char id[16];
            snprintf(id, sizeof(id), "o%d", (int)m);
            tile.id = id;
            object_tile_common(tile, m, group);
            if (editor_icon_grid_tile(tile))
                select_object(m);
        }
        editor_icon_grid_end();
    }

    void draw_traps_and_doors()
    {
        editor_icon_grid_begin("##EdTrapDoorGrid", 280.0f, 5);
        editor_icon_grid_heading("Traps");
        const long trap_count = kfx_config_state.conf.trapdoor_conf.trap_types_count;
        for (ThingModel m = 1; m < (ThingModel)trap_count; m++)
        {
            EditorIconTile tile;
            char id[16];
            snprintf(id, sizeof(id), "t%d", (int)m);
            tile.id = id;
            tile.sprite = manufacture_icon(TCls_Trap, m);
            tile.ov_kind = EIO_ActiveInactive;
            tile.ov_category = "trap";
            tile.ov_code = trap_code_name(m);
            tile.label = editor_icon_grid_pretty(trap_code_name(m));
            tile.tooltip = trap_code_name(m);
            tile.selected = (s_active_tool == EdTool_Trap) && (m == s_selected_trap_kind);
            if (editor_icon_grid_tile(tile))
            {
                s_selected_trap_kind = m;
                select_placement_kind(EdTool_Trap, m);
            }
        }
        editor_icon_grid_heading("Doors");
        const long door_count = kfx_config_state.conf.trapdoor_conf.door_types_count;
        for (ThingModel m = 1; m < (ThingModel)door_count; m++)
        {
            EditorIconTile tile;
            char id[16];
            snprintf(id, sizeof(id), "d%d", (int)m);
            tile.id = id;
            tile.sprite = manufacture_icon(TCls_Door, m);
            tile.ov_kind = EIO_ActiveInactive;
            tile.ov_category = "trap"; // doors share the trap override category
            tile.ov_code = door_code_name(m);
            tile.label = editor_icon_grid_pretty(door_code_name(m));
            tile.tooltip = door_code_name(m);
            tile.selected = (s_active_tool == EdTool_Door) && (m == s_selected_door_kind);
            if (editor_icon_grid_tile(tile))
            {
                s_selected_door_kind = m;
                select_placement_kind(EdTool_Door, m);
            }
        }
        editor_icon_grid_heading("Crates");
        const long object_count = kfx_config_state.conf.object_conf.object_types_count;
        for (ThingModel m = 1; m < (ThingModel)object_count; m++)
        {
            if (editor_object_group_of(m) != EOG_Crates)
                continue;
            EditorIconTile tile;
            char id[16];
            snprintf(id, sizeof(id), "k%d", (int)m);
            tile.id = id;
            object_tile_common(tile, m, EOG_Crates);
            if (editor_icon_grid_tile(tile))
                select_object(m);
        }
        editor_icon_grid_end();
    }

    void draw_thing_picker(int tint_level)
    {
        {
            TabTint tint(tint_level);
            bool tabs = FeBeginTabBar("##EdThingTabs");
            if (tabs)
            {
                if (tab_page("Spells", &s_force_thing_group, TG_Spells))
                    { draw_object_group(EOG_Spells, 5, "##EdSpellGrid"); FeEndTab(); }
                if (tab_page("Specials", &s_force_thing_group, TG_Specials))
                    { draw_object_group(EOG_Specials, 5, "##EdSpecialGrid"); FeEndTab(); }
                if (tab_page("Traps & Doors", &s_force_thing_group, TG_TrapsDoors))
                    { draw_traps_and_doors(); FeEndTab(); }
                if (tab_page("Decor", &s_force_thing_group, TG_Decor))
                    { draw_object_group(EOG_Decor, 5, "##EdDecorGrid"); FeEndTab(); }
            }
            FeEndTabBar(tabs);
        }
        draw_object_edit_panel();
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
    // rather than writes: a thing under the cursor first (creature, else the
    // nearest thing), else the slab kind/owner (read straight from the world).
    // The sample is applied to the local selection state directly, and the
    // sample also switches the toolbox to the matching tool.
    // What a sample selected, written straight to the selection state
    // (UserState::cheatselection etc.) rather than through a packet: the
    // sample also switches the toolbox to the matching tool, and that is its
    // own PckA_SetPlyrState -- two packets in one click would overwrite each
    // other (one slot per turn). Same single-player-local
    // exception (D2) as the rest of this file's direct edits.
    void eyedropper_apply_thing(struct PlayerInfo *player, struct Thing *thing)
    {
        struct UserState *ustate = get_player_user_state(player);
        switch (thing->class_id)
        {
            case TCls_Creature:
                // One creature palette: a hero is just owner PLAYER_GOOD.
                s_selected_creature_kind = thing->model;
                s_selected_owner = thing->owner;
                if (ustate != NULL)
                {
                    ustate->cheatselection.chosen_creature_kind = thing->model;
                    ustate->cheatselection.chosen_player = thing->owner;
                }
                focus_tool_tabs(EdTool_Creature);
                activate_tool(EdTool_Creature);
                break;
            case TCls_Object:
            {
                s_selected_object_model = thing->model;
                s_selected_owner = thing->owner;
                s_thing_mode = EdTool_Object;
                const int g = editor_object_group_of(thing->model);
                s_force_thing_group = (g == EOG_Spells) ? TG_Spells : (g == EOG_Specials) ? TG_Specials
                    : (g == EOG_Crates) ? TG_TrapsDoors : TG_Decor;
                focus_tool_tabs(EdTool_Object);
                activate_tool(EdTool_Object);
                break;
            }
            case TCls_Trap:
            case TCls_Door:
            {
                const bool trap = (thing->class_id == TCls_Trap);
                (trap ? s_selected_trap_kind : s_selected_door_kind) = thing->model;
                s_selected_owner = thing->owner;
                if (ustate != NULL)
                {
                    (trap ? ustate->chosen_trap_kind : ustate->chosen_door_kind) = thing->model;
                    ustate->cheatselection.chosen_player = thing->owner;
                }
                s_thing_mode = trap ? EdTool_Trap : EdTool_Door;
                s_force_thing_group = TG_TrapsDoors;
                focus_tool_tabs(EdTool_Object);
                activate_tool(EdTool_Object);
                break;
            }
            default:
                break;
        }
    }

    // §2.10 -- see the comment above handle_eyedropper_click(). Returns true
    // if a thing was sampled (caller stops there), false to fall through to
    // the slab under the cursor.
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
            case TCls_Object:
            case TCls_Trap:
            case TCls_Door:
                eyedropper_apply_thing(player, thing);
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
        struct UserState *ustate = get_player_user_state(player);
        if (ustate != NULL)
        {
            ustate->cheatselection.chosen_terrain_kind = s_selected_terrain_kind;
            ustate->cheatselection.chosen_player = s_selected_owner;
        }
        // Show the sampled slab: Terrain tool, Brush mode, the tab it lives in.
        s_terrain_mode = TerrainMode_Brush;
        s_force_terrain_mode = TerrainMode_Brush;
        s_force_terrain_group = editor_terrain_group_of(s_selected_terrain_kind);
        focus_tool_tabs(EdTool_Terrain);
        activate_tool(EdTool_Terrain);
    }

    void draw_texture_paint_picker()
    {
        editor_texture_pack_combo("Texture", &s_selected_texture_pack, editor_current_lvnum());
        static const char *const kModes[] = {"Brush (drag)", "Rectangle", "Fill"};
        FeCombo("Mode", &s_texture_paint_mode, kModes, 3);
        FeBodyText(s_texture_paint_mode == 0 ? "LMB-drag to paint slabs with this texture set."
            : s_texture_paint_mode == 1 ? "Drag a box; the slabs in it get this texture set."
            : "Click: repaint the connected slabs of the same kind and texture.");
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
        static bool rect_dragging = false;
        static MapSlabCoord rect_x = 0, rect_y = 0;
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        const bool over_map = !io.WantCaptureMouse && screen_to_map(camera, GetMouseX(), GetMouseY(), &pos);
        if (!over_map && !rect_dragging)
            return;
        MapSlabCoord slb_x = 0, slb_y = 0;
        if (over_map)
        {
            slb_x = subtile_slab(coord_subtile(pos.x.val));
            slb_y = subtile_slab(coord_subtile(pos.y.val));
        }
        const unsigned char pack = (unsigned char)s_selected_texture_pack;
        switch (s_texture_paint_mode)
        {
            case 0: // Brush: continuous drag-paint
                if (over_map && ImGui::IsMouseDown(ImGuiMouseButton_Left))
                    editor_texture_paint_slab(slb_x, slb_y, pack);
                break;
            case 1: // Rectangle: mark a box, apply on release
                if (over_map && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    rect_dragging = true;
                    rect_x = slb_x;
                    rect_y = slb_y;
                }
                if (rect_dragging && over_map)
                {
                    const MapSlabCoord bx = min(rect_x, slb_x), by = min(rect_y, slb_y);
                    const MapSlabCoord ex = max(rect_x, slb_x) + 1, ey = max(rect_y, slb_y) + 1;
                    draw_map_volume_box(subtile_coord(slab_subtile(bx, 0), 0), subtile_coord(slab_subtile(by, 0), 0),
                        subtile_coord(slab_subtile(ex, 0), 0), subtile_coord(slab_subtile(ey, 0), 0),
                        floor_height_for_volume_box(player->id_number, slb_x, slb_y), SLC_YELLOW);
                }
                if (rect_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
                {
                    rect_dragging = false;
                    if (over_map)
                        editor_texture_paint_rect(rect_x, rect_y, slb_x, slb_y, pack);
                }
                break;
            default: // Fill
                if (over_map && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                    editor_texture_paint_fill(slb_x, slb_y, pack);
                break;
        }
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
    bool s_brush_dragging = false;
    MapSlabCoord s_brush_drag_slb_x = 0;
    MapSlabCoord s_brush_drag_slb_y = 0;

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
            editor_brush_capture(box_beg_x, box_beg_y, box_end_x, box_end_y);
        }

        // Stamp: LMB click (one stamp per click, at the buffer's captured
        // top-left anchored to the cursor's current slab).
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            editor_brush_stamp(cur_slb_x, cur_slb_y);
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
    EditorQueryResult s_query_result;

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
        ThingIndex creature_idx = 0;
        s_query_result = editor_query_at(&pos, &creature_idx);
        if (creature_idx != 0)
        {
            // Already-migrated ImGui panel owns the screen for a creature.
            query_creature(player, creature_idx, true, false);
        }
    }

    // A plain right-click (no drag: right-drag is camera and Stamp capture)
    // in a placement tool removes the thing under the cursor, as in the
    // original editor. Same per-class teardown as the Erase tool's server
    // side (packets_cheats.c, PSt_DestroyThing).
    struct RmbTrack
    {
        bool down = false;
        ImVec2 start = ImVec2(0, 0);
        double time = 0.0;
    } s_rmb;

    void delete_thing_under_cursor()
    {
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_local_active_camera(player);
        struct Coord3d pos;
        if (screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            editor_delete_thing_at(&pos);
    }

    void handle_rmb_delete()
    {
        const bool placement = (s_active_tool == EdTool_Creature) || (s_active_tool == EdTool_Object)
            || (s_active_tool == EdTool_Trap) || (s_active_tool == EdTool_Door);
        if (!placement)
        {
            s_rmb.down = false;
            return;
        }
        ImGuiIO &io = ImGui::GetIO();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && !io.WantCaptureMouse)
        {
            s_rmb.down = true;
            s_rmb.start = io.MousePos;
            s_rmb.time = ImGui::GetTime();
        }
        else if (s_rmb.down && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        {
            s_rmb.down = false;
            const float dx = io.MousePos.x - s_rmb.start.x;
            const float dy = io.MousePos.y - s_rmb.start.y;
            if (dx * dx + dy * dy < 25.0f && (ImGui::GetTime() - s_rmb.time) < 0.4)
                delete_thing_under_cursor();
        }
    }

    void draw_query_inspector()
    {
        if (s_query_result.kind == EditorQueryResult::QR_None)
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
    // The owner selector, drawn with the same player symbols the in-game
    // query panel uses (the red/blue/green/yellow keeper symbols, white for
    // the hero dungeon) instead of "P0".."P3". Neutral has no symbol of its
    // own: like the in-game chat/message icon for the neutral player it
    // flashes through the four keeper colours (get_chat_icon_sprite_idx_from_id,
    // lvl_script_lib.c), here on wall-clock time so it flashes in a paused
    // editor too. Selecting one sends the same PckA_CheatSwitchPlayer as the
    // old text buttons.
    void draw_owner_row(struct PlayerInfo *player)
    {
        struct OwnerDef { PlayerNumber owner; const char *tip; };
        const PlayerNumber neutral = kfx_config_state.neutral_player_num;
        const OwnerDef owners[] = {
            {PLAYER0, "Player 1 (red)"}, {PLAYER1, "Player 2 (blue)"},
            {PLAYER2, "Player 3 (green)"}, {PLAYER3, "Player 4 (yellow)"},
            {neutral, "Neutral"}, {PLAYER_GOOD, "Hero (white)"},
        };
        const float sz = 34.0f, gap = 4.0f;
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const int flash = ((int)(ImGui::GetTime() * 4.0)) & 3;
        for (size_t i = 0; i < sizeof(owners) / sizeof(owners[0]); i++)
        {
            const PlayerNumber o = owners[i].owner;
            short sprite;
            if (o == PLAYER_GOOD)
                sprite = GPS_plyrsym_symbol_player_white_std;
            else if (o == neutral)
                sprite = (short)(GPS_plyrsym_symbol_player_red_std_b + flash);
            else
                sprite = get_player_colored_icon_idx(GPS_plyrsym_symbol_player_red_std_b, o);
            FeHudCellOpts opts;
            opts.sprite = sprite;
            opts.selected = (o == s_selected_owner);
            opts.tooltip = owners[i].tip;
            char id[12];
            snprintf(id, sizeof(id), "own%d", (int)o);
            if (fe_hud_cell(id, ImVec2(origin.x + i * (sz + gap), origin.y), ImVec2(sz, sz), opts) == 1)
            {
                s_selected_owner = o;
                struct UserState *ustate = get_player_user_state(player);
                if (ustate != NULL)
                    ustate->cheatselection.chosen_player = o;
            }
        }
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(6 * (sz + gap), sz + gap));
    }

    void draw_bottom_bar()
    {
        struct PlayerInfo *player = get_my_player();
        FeSeparator();
        // Bracketed label marks the current selection -- FeButton (unlike
        // FeNavButton/FeListRow) has no built-in "selected" look, and this
        // row is laid out horizontally via SameLine(), not the vertical
        // list FeNavButton assumes. Found live: no feedback at all here
        // made every bottom-bar click indistinguishable from a no-op.
        draw_owner_row(player);

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

    // Terrain tool: its modes (Brush / Rectangle / Clear Earth / Delete Things /
    // Set Owner) are a bar of tabs, and only the two painting modes have a
    // palette -- Clear Earth / Delete Things / Set Owner have no *kind* to
    // pick (a fixed target, none at all, or the bottom bar's owner selector).
    void draw_terrain_body()
    {
        struct ModeDef { const char *label; TerrainMode mode; };
        static const ModeDef modes[] = {
            {"Brush",         TerrainMode_Brush},
            {"Rectangle",     TerrainMode_Rectangle},
            {"Fill",          TerrainMode_Fill},
        };
        TabTint tint(2);
        bool open = FeBeginTabBar("##EdTerrainModes");
        if (open)
        {
            for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++)
            {
                const bool forced = (s_force_terrain_mode == (int)modes[i].mode);
                const bool pending_other = (s_force_terrain_mode != -1) && !forced;
                const bool shown = FeTabEx(modes[i].label, forced);
                if (forced)
                    s_force_terrain_mode = -1;
                if (!shown)
                    continue;
                // A click on the tab (not a forced switch) changes the mode
                // and re-sends the work state, as the old button row did.
                if (!forced && !pending_other && s_terrain_mode != modes[i].mode)
                {
                    s_terrain_mode = modes[i].mode;
                    set_work_state(terrain_mode_work_state(s_terrain_mode));
                }
                ImGui::PushID((int)modes[i].mode);
                draw_terrain_picker(3);
                ImGui::PopID();
                FeEndTab();
            }
        }
        FeEndTabBar(open);
    }

    char s_reinforce_status[48] = "";

    void draw_tool_body(EditorTool tool)
    {
        // A tool's widgets get their own ID scope: on the frame a tab switch is
        // settling, two tools' bodies can both be submitted, and the same
        // picker bar ("##EdTerrainTabs" under Terrain and under Fill) must not
        // then be a duplicate ID.
        ImGui::PushID((int)tool);
        switch (tool)
        {
            case EdTool_Terrain:      draw_terrain_body(); break;
            case EdTool_ClearEarth:   FeBodyText("Mark an area to turn it into claimable earth."); break;
            case EdTool_DeleteThings: FeBodyText("Mark an area to delete the things in it."); break;
            case EdTool_SetOwner:     FeBodyText("Mark an area to give it the owner chosen below."); break;
            case EdTool_Reinforce:
            {
                FeBodyText("Turns the earth around the chosen owner's floor,");
                FeBodyText("rooms and doors into reinforced wall.");
                if (FeButton("Reinforce perimeter", ImVec2(200, 0)))
                {
                    const int n = editor_reinforce_perimeter(s_selected_owner);
                    snprintf(s_reinforce_status, sizeof(s_reinforce_status), "%d slab%s reinforced.", n, n == 1 ? "" : "s");
                }
                if (s_reinforce_status[0] != '\0')
                    FeCaption(s_reinforce_status);
                break;
            }
            case EdTool_Creature:     draw_creature_picker(); break;
            case EdTool_Object:       draw_thing_picker(2); break;
            case EdTool_Points:       editor_points_draw_panel(); break;
            case EdTool_Eyedropper:
                FeBodyText("Click a slab or thing to select it; the toolbox switches to its tool.");
                break;
            case EdTool_Stamp:
            {
                char status[64];
                if (editor_brush_is_empty())
                    snprintf(status, sizeof(status), "RMB-drag to capture a terrain area.");
                else
                    snprintf(status, sizeof(status), "%zu slabs captured. LMB to stamp.", editor_brush_slab_count());
                FeBodyText(status);
                break;
            }
            case EdTool_PaintTexture: draw_texture_paint_picker(); break;
            case EdTool_Query:        draw_query_inspector(); break;
            case EdTool_Erase:        FeBodyText("Click a thing to remove it."); break;
            default: break;
        }
        ImGui::PopID();
    }

    struct ToolTab { const char *label; EditorTool tool; };

    // The tools of one top-level tab, as a bar of tabs. Showing a tool's tab
    // makes it the live tool (so switching top tabs also switches tools), and
    // its palette is drawn inside the tab -- it is on screen only while its
    // tab is.
    void draw_tool_tabs(const char *bar_id, const ToolTab *tabs, size_t count)
    {
        if (count == 1)
        {
            // A lone tool needs no tab row of its own.
            const EditorTool only = tabs[0].tool;
            const bool forced = (s_force_tool == (int)only);
            if (forced)
                s_force_tool = -1;
            if (!forced && s_force_tool == -1 && s_force_top == -1 && tab_tool(s_active_tool) != only)
                activate_tool(only);
            draw_tool_body(only);
            return;
        }
        TabTint tint(1);
        bool open = FeBeginTabBar(bar_id);
        if (open)
        {
            for (size_t i = 0; i < count; i++)
            {
                const bool forced = (s_force_tool == (int)tabs[i].tool);
                const bool shown = FeTabEx(tabs[i].label, forced);
                if (forced)
                    s_force_tool = -1;
                if (!shown)
                    continue;
                // Not while a forced switch is still settling (a tab that was
                // selected a moment ago must not steal the tool back).
                if (!forced && s_force_tool == -1 && s_force_top == -1 && tab_tool(s_active_tool) != tabs[i].tool)
                    activate_tool(tabs[i].tool);
                draw_tool_body(tabs[i].tool);
                FeEndTab();
            }
        }
        FeEndTabBar(open);
    }

    // Top level: Terrain | Things | Utility | History.
    void draw_tool_ui()
    {
        static const ToolTab terrain_tools[] = {
            {"Terrain", EdTool_Terrain},
        };
        static const ToolTab thing_tools[] = {
            {"Creature", EdTool_Creature}, {"Thing", EdTool_Object}, {"Points", EdTool_Points},
        };
        static const ToolTab utility_tools[] = {
            {"Eyedropper", EdTool_Eyedropper}, {"Stamp", EdTool_Stamp}, {"Paint Tex", EdTool_PaintTexture},
            {"Query", EdTool_Query}, {"Erase", EdTool_Erase},
        };
        static const ToolTab area_tools[] = {
            {"Clear", EdTool_ClearEarth}, {"Delete", EdTool_DeleteThings}, {"Owner", EdTool_SetOwner},
            {"Reinforce", EdTool_Reinforce},
        };
        s_history_visible = false;
        bool open = FeBeginTabBar("##EdToolTabs");
        if (open)
        {
            struct TopDef { const char *label; TopTab top; const ToolTab *tools; size_t count; const char *bar; };
            const TopDef tops[] = {
                {"Terrain", TT_Terrain, terrain_tools, sizeof(terrain_tools) / sizeof(terrain_tools[0]), "##EdToolsTerrain"},
                {"Things",  TT_Things,  thing_tools,   sizeof(thing_tools) / sizeof(thing_tools[0]),     "##EdToolsThings"},
                {"Utility", TT_Utility, utility_tools, sizeof(utility_tools) / sizeof(utility_tools[0]), "##EdToolsUtility"},
                {"Area",    TT_Area,    area_tools,    sizeof(area_tools) / sizeof(area_tools[0]),       "##EdToolsArea"},
            };
            for (const TopDef &t : tops)
            {
                const bool forced = (s_force_top == (int)t.top);
                const bool shown = FeTabEx(t.label, forced);
                if (forced)
                    s_force_top = -1;
                if (shown)
                {
                    draw_tool_tabs(t.bar, t.tools, t.count);
                    FeEndTab();
                }
            }
            const bool history_forced = (s_force_top == (int)TT_History);
            if (FeTabEx("History", history_forced))
            {
                s_history_visible = true;
                draw_history_tab();
                FeEndTab();
            }
            if (history_forced)
                s_force_top = -1;
        }
        FeEndTabBar(open);
    }

    // fx-plans/00 item A7 -- stroke-level undo for the tools whose changes
    // arrive as a stream of per-frame packets (free-hand terrain paint, Fill)
    // or direct writes (Paint Texture): one journal entry per mouse-down to
    // mouse-up, made by diffing the map's slabs. While no stroke is under way
    // a snapshot is refreshed every frame, so the "before" state is never one
    // that already contains the first click's own change (packets can be
    // applied before the render phase runs). A few frames of settling after
    // the release let the last packet land before the diff is taken.
    bool s_was_previewing = false;

    struct StrokeTrack
    {
        bool in_stroke = false;
        int settle = 0;
        char label[24] = "";
    } s_stroke;

    const char *stroke_label_for_active_tool()
    {
        if (s_history_visible)
            return NULL;
        if (s_active_tool == EdTool_PaintTexture)
            return "Paint Texture";
        if (s_active_tool == EdTool_Erase)
            return "Erase";
        if (s_active_tool == EdTool_DeleteThings)
            return "Delete Things";
        if (s_active_tool == EdTool_Terrain)
        {
            if (s_terrain_mode == TerrainMode_Brush)
                return "Paint";
            if (s_terrain_mode == TerrainMode_Fill)
                return "Fill";
        }
        return NULL;
    }

    void update_stroke_tracking()
    {
        const char *label = stroke_label_for_active_tool();
        if (s_stroke.in_stroke)
        {
            if (label == NULL || strcmp(label, s_stroke.label) != 0)
            {
                editor_journal_stroke_end(s_stroke.label); // tool switched mid-stroke
                s_stroke.in_stroke = false;
            }
            else if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                s_stroke.settle = 0;
                return;
            }
            else if (++s_stroke.settle >= 4)
            {
                editor_journal_stroke_end(s_stroke.label);
                s_stroke.in_stroke = false;
                return;
            }
            else
                return;
        }
        if (label == NULL)
            return;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().WantCaptureMouse)
        {
            s_stroke.in_stroke = true;
            s_stroke.settle = 0;
            snprintf(s_stroke.label, sizeof(s_stroke.label), "%s", label);
            return;
        }
        editor_journal_stroke_begin();
    }

    // docs/refactor/editor/10-definable-keybindings.md (D6) -- the
    // toolbox's genuinely *definable* tool-switch shortcuts (Ctrl+Z/Ctrl+Y
    // stay hardcoded on purpose, per user direction: standard everywhere,
    // no D6 need). Scoped to the 4 tools picked as the highest-value
    // subset -- toggled back to constantly, and usable standalone with no
    // follow-up model-picker click. Each activates the tool and asks the
    // tab bars to show it, same as clicking its tab.
    //
    // is_editor_key_pressed() (front_input.h, kfx_frontend) reads the classic
    // bflib_keybrd key-state array, a separate listener on the same raw
    // input stream ImGui's own backend reads -- nothing gates it on ImGui's
    // keyboard focus, so guard on !io.WantCaptureKeyboard explicitly: typing
    // "e"/"t"/"q"/"r" into one of this toolbox's own text fields must not
    // also switch tools mid-edit.
    void handle_editor_tool_shortcuts()
    {
        ImGuiIO &io = ImGui::GetIO();
        if (io.WantCaptureKeyboard)
            return;
        EditorTool target;
        if (is_editor_key_pressed(Gkey_EditorEraseTool, true, false))
            target = EdTool_Erase;
        else if (is_editor_key_pressed(Gkey_EditorTerrainTool, true, false))
            target = EdTool_Terrain;
        else if (is_editor_key_pressed(Gkey_EditorQueryTool, true, false))
            target = EdTool_Query;
        else if (is_editor_key_pressed(Gkey_EditorEyedropperTool, true, false))
            target = EdTool_Eyedropper;
        else if (is_editor_key_pressed(Gkey_EditorStampTool, true, false))
            target = EdTool_Stamp;
        else if (is_editor_key_pressed(Gkey_EditorPointsTool, true, false))
            target = EdTool_Points;
        else if (is_editor_key_pressed(Gkey_EditorCreatureTool, true, false))
            target = EdTool_Creature;
        else if (is_editor_key_pressed(Gkey_EditorThingTool, true, false))
            target = EdTool_Object;
        else if (is_editor_key_pressed(Gkey_EditorReinforceTool, true, false))
            target = EdTool_Reinforce;
        else if (is_editor_key_pressed(Gkey_EditorFillTool, true, false))
        {
            // Fill is a mode of the Terrain tool.
            s_terrain_mode = TerrainMode_Fill;
            s_force_terrain_mode = TerrainMode_Fill;
            target = EdTool_Terrain;
        }
        else
            return;
        focus_tool_tabs(target);
        activate_tool(target);
    }

} // namespace

void editor_toolbox_frame(void)
{
    handle_editor_tool_shortcuts();

    if (s_toolbox_open)
    {
    ImGui::SetNextWindowPos(ImVec2(20, 60), ImGuiCond_FirstUseEver);
    ImGui::Begin("##EditorToolbox", &s_toolbox_open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);

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
    if (editor_preview_motion())
    {
        FeBodyText("Motion preview is running: editing is paused.");
        FeBodyText("Turn it off (View > Preview Motion) and everything goes");
        FeBodyText("back exactly where you put it.");
    }
    else
    {
        draw_tool_ui();
        FeSeparator();

        draw_bottom_bar();
    }

    ImGui::End();
    }
    else
    {
        s_history_visible = false;
    }

    // Deliberately outside the toolbox's own Begin/End -- this checks
    // clicks anywhere on screen (the 3D dungeon view), not just inside
    // this window; io.WantCaptureMouse inside the function itself is what
    // stops a click on the toolbox from also registering as a placement.
    // While the History tab is showing no tool is "live": clicking the map
    // must not place things.
    if (s_history_visible)
        return;
    if (editor_preview_motion())
    {
        // Nothing may be edited while the world is moving.
        if (!s_was_previewing)
            set_work_state(PSt_EditorQuery); // a state a click does nothing in
        s_was_previewing = true;
        return;
    }
    s_was_previewing = false;
    reconcile_work_state();
    update_stroke_tracking();
    handle_rmb_delete();
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

extern "C" TbBool editor_toolbox_is_open(void)
{
    return s_toolbox_open;
}

extern "C" void editor_toolbox_set_open(TbBool open)
{
    s_toolbox_open = (open != 0);
}
/******************************************************************************/
