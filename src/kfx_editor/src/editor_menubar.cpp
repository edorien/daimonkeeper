/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_menubar.cpp
 *     See editor_menubar.h. Replaces the toolbox header's old Menu/Undo/
 *     Redo buttons (editor_toolbox.cpp) with a real File/Edit/View/Script
 *     pull-down bar (docs/refactor/editor/phase3/02-slice3-dialogs-menubar.md).
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_menubar.h"
#include "kfx_editor.h"
#include "editor_journal.h"
#include "editor_dialogs.h"

#include "frontgui_widgets.h"
#include "player_data.h"
#include "packet_data.h"
#include "camera_data.h" // struct Camera::mappos, for View > 1st Person's spawn point
#include "light_data.h" // lish.light_enabled, View > Lights
#include "gui_parchment.h" // zoom_to_parchment_map()/zoom_from_parchment_map(), View > Map View
#include "editor_overlay.h" // View > Slab Grid/Coordinates/Ownership Tint
#include "editor_toolbox.h" // View > Toolbox
#include "editor_script.h" // Script > Edit Script...
#include "editor_availability.h" // Script > Availability...
#include "editor_message_helper.h" // Script > Objective / Message...
#include "editor_command_browser.h" // Script > Commands...

#include <cstdio>
#include "post_inc.h"

void editor_menubar_frame(void)
{
    bool bar_open = FeBeginMenuBar();
    if (bar_open)
    {
        bool file_open = FeBeginMenu("File");
        if (file_open)
        {
            if (FeMenuItem("New..."))
                editor_dialogs_open_new_map();
            if (FeMenuItem("Open..."))
                editor_dialogs_open_open_map();
            FeSeparator();
            if (FeMenuItem("Save", "Ctrl+S"))
                editor_dialogs_save_now();
            if (FeMenuItem("Save As..."))
                editor_dialogs_open_save_as();
            if (FeMenuItem("Level Settings..."))
                editor_dialogs_open_level_settings();
            // docs/refactor/editor/phase3/06-slice7-verify-map.md -- purely
            // informational, never blocks Save -- reachable any time, not
            // just before saving.
            if (FeMenuItem("Verify Map"))
                editor_dialogs_open_verify_map();
            FeSeparator();
            // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
            // -- saves to a scratch slot and launches the same way `-level`
            // does; the confirm dialog itself carries the "no script" caveat.
            if (FeMenuItem("Playtest", "Ctrl+P"))
                editor_dialogs_open_playtest_confirm();
            FeSeparator();
            if (FeMenuItem("Exit to Main Menu"))
                editor_dialogs_request_exit();
        }
        FeEndMenu(file_open);

        bool edit_open = FeBeginMenu("Edit");
        if (edit_open)
        {
            if (FeMenuItem("Undo", "Ctrl+Z", editor_journal_undo_count() > 0))
                editor_journal_do_undo();
            if (FeMenuItem("Redo", "Ctrl+Y", editor_journal_redo_count() > 0))
                editor_journal_do_redo();
        }
        FeEndMenu(edit_open);

        bool view_open = FeBeginMenu("View");
        if (view_open)
        {
            struct PlayerInfo *player = get_my_player();

            bool toolbox_cb = editor_toolbox_is_open();
            if (FeCheckbox("Toolbox", &toolbox_cb))
                editor_toolbox_set_open(toolbox_cb);
            FeSeparator();

            // docs/refactor/editor/04-views-camera-overlays.md -- "Plan"
            // (the cursor-following zoom-box magnifier) and "full map" from
            // the original design turned out to be the same engine screen,
            // not two: draw_zoom_box() (gui_parchment.c) runs
            // unconditionally as part of redraw_parchment_view() whenever
            // PVT_MapScreen is active -- there's no separate "map without
            // the magnifier" mode to toggle independently. One item covers
            // both. A checkbox, like every other toggle in this menu.
            bool map_view_on = (player->view_type == PVT_MapScreen);
            bool map_label_cb = map_view_on;
            if (FeCheckbox("Map View", &map_label_cb))
            {
                if (map_view_on)
                    zoom_from_parchment_map();
                else
                    zoom_to_parchment_map();
            }

            // 1st Person -- spawns (or already controls) a spectator
            // creature. level_editor_go_spectator_at() (player_instances.c)
            // spawns at an explicit position rather than
            // level_lost_go_first_person()'s own "find an owned creature"
            // search, which an editor map often has nothing for (a fresh
            // New Map has no creatures at all). Spawns at the active
            // camera's own center -- wherever the isometric/front view is
            // currently looking -- rather than tracking a separate cursor
            // position, which nothing in kfx_editor does yet.
            bool first_person_on = (player->view_type == PVT_CreatureContrl);
            bool fp_label_cb = first_person_on;
            if (FeCheckbox("1st Person", &fp_label_cb))
            {
                if (first_person_on)
                {
                    set_players_packet_action(player, PckA_DirectCtrlExit, player->controlled_thing_idx, 0, 0, 0);
                }
                else
                {
                    // editor_frame() re-freezes the simulation every frame
                    // unless Preview Motion is on (editor_session.cpp) -- a
                    // frozen spectator can't move at all, which would look
                    // like a bug rather than a deliberate editor state.
                    // Turning Preview Motion on here is less surprising
                    // than shipping a 1st Person view that can't walk.
                    if (!editor_preview_motion())
                        editor_set_preview_motion(true);
                    struct Camera *cam = get_player_active_camera(player);
                    set_players_packet_action(player, PckA_EditorGoSpectator,
                        cam->mappos.x.val, cam->mappos.y.val, 0, 0);
                }
            }

            bool lights_on = (lish.light_enabled != 0);
            bool lights_label_cb = lights_on;
            if (FeCheckbox("Lights", &lights_label_cb))
                set_players_packet_action(player, PckA_ToggleLights, 0, 0, 0, 0);

            // No Low Walls (cluedo) item here -- settings.video_cluedo_mode is
            // already reachable via the normal in-game Options > Graphics
            // menu, which the editor session doesn't hide; a second control
            // for the same setting in View would just be a duplicate.

            FeSeparator();

            // docs/refactor/editor/04-views-camera-overlays.md phase 4
            // slice 4 -- world-space overlays, each independently
            // toggleable. Checkboxes, like every other toggle in this menu.
            bool grid_on = editor_overlay_slab_grid_enabled();
            bool grid_label_cb = grid_on;
            if (FeCheckbox("Slab Grid", &grid_label_cb))
                editor_overlay_set_slab_grid_enabled(!grid_on);

            bool coords_on = editor_overlay_coordinates_enabled();
            bool coords_label_cb = coords_on;
            if (FeCheckbox("Coordinates", &coords_label_cb))
                editor_overlay_set_coordinates_enabled(!coords_on);

            bool tint_on = editor_overlay_ownership_tint_enabled();
            bool tint_label_cb = tint_on;
            if (FeCheckbox("Ownership Tint", &tint_label_cb))
                editor_overlay_set_ownership_tint_enabled(!tint_on);

            // docs/refactor/editor/04-views-camera-overlays.md phase 4
            // slice 5 -- marker overlays, same toggle convention as above.
            bool things_on = editor_overlay_thing_markers_enabled();
            bool things_label_cb = things_on;
            if (FeCheckbox("Thing Markers", &things_label_cb))
                editor_overlay_set_thing_markers_enabled(!things_on);

            bool lightm_on = editor_overlay_light_markers_enabled();
            bool lightm_label_cb = lightm_on;
            if (FeCheckbox("Light Markers", &lightm_label_cb))
                editor_overlay_set_light_markers_enabled(!lightm_on);

            bool apm_on = editor_overlay_ap_herogate_markers_enabled();
            bool apm_label_cb = apm_on;
            if (FeCheckbox("AP / Hero Gate Markers", &apm_label_cb))
                editor_overlay_set_ap_herogate_markers_enabled(!apm_on);

            FeSeparator();

            // §3's "Preview motion" -- moved here from the Esc-hub this
            // slice (editor_session.cpp's own s_preview_motion comment for
            // what it does). A checkbox, like every other toggle in this menu.
            bool on = editor_preview_motion();
            bool label_cb = on;
            if (FeCheckbox("Preview Motion (unpause)", &label_cb))
                editor_set_preview_motion(!on);
        }
        FeEndMenu(view_open);

        // docs/refactor/editor/05-script-and-level-settings.md §4.1 -- was a
        // disabled stub (phase3/02-slice3-dialogs-menubar.md) until this
        // slice gave it a real first item.
        bool script_open = FeBeginMenu("Script");
        if (script_open)
        {
            if (FeMenuItem("Edit Script..."))
                editor_dialogs_open_script();
            if (FeMenuItem("Availability..."))
                editor_dialogs_open_availability();
            if (FeMenuItem("Commands..."))
                editor_dialogs_open_command_browser();
            if (FeMenuItem("Objective / Message..."))
                editor_dialogs_open_message_helper();
            FeSeparator();
            bool wrap_cb = editor_script_word_wrap();
            if (FeCheckbox("Word Wrap", &wrap_cb))
                editor_script_set_word_wrap(wrap_cb);
        }
        FeEndMenu(script_open);

        // Map name + dirty marker (08-gui-layout.md's own mockup) -- real
        // now that lvnum/dirty/name are all tracked (editor_session.cpp).
        // Falls back to the lvnum alone when no name has been set yet
        // (Level Settings/Save As, or none found in an existing .lof).
        char title[64];
        const char *name = editor_current_level_name();
        if (name[0] != '\0')
            snprintf(title, sizeof(title), "%s (Level %lu)%s",
                name, (unsigned long)editor_current_lvnum(), editor_is_dirty() ? " *" : "");
        else
            snprintf(title, sizeof(title), "Level %lu%s",
                (unsigned long)editor_current_lvnum(), editor_is_dirty() ? " *" : "");
        FeMenuItem(title, nullptr, false);
    }
    FeEndMenuBar(bar_open);
}
