/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_dialogs.cpp
 *     See editor_dialogs.h. New Map/Open Map/Save As are all gated by the
 *     same "explicit show-flag, checked at the top of the draw function"
 *     convention frontgui_screens.cpp's draw_tools_modal() already
 *     established: FeOpenModal() must only run on frames the flag is true,
 *     and every button that dismisses a modal must flip its flag false in
 *     the same click as calling ImGui::CloseCurrentPopup() -- calling
 *     FeOpenModal() unconditionally every frame would reopen a just-closed
 *     popup on the very next frame.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_dialogs.h"
#include "editor_sidecars.h"
#include "editor_lua_validate.h"
#include "editor_script.h"
#include "editor_mappack.h"
#include "thing_list.h"
#include "thing_objects.h"
#include "editor_script_validate.h"
#include "kfx_editor.h"

#include "frontend.h" // frontend_request_editor_relaunch, EDITOR_SCRATCH_LEVEL_NUMBER
#include "frontgui_widgets.h"
#include "frontmenu_landpreview.h" // land_preview_build_minimap + thumbnail accessors
#include "config_campaigns.h" // campaign.single_levels/freeplay_levels, change_campaign
#include "config.h" // get_level_fgroup/prepare_file_fmtpath -- format-badge detection
#include "bflib_fileio.h" // LbFileExists
#include "bflib_filedialogs.h" // platform_pick_folder_dialog/platform_pick_open_file_dialog
#include "editor_map_snapshot.h" // editor_snapshot_current_map -- Verify Map
#include "map_content_verify.h" // verify_map_content
#include "packet_data.h" // PckA_ZoomToPosition
#include "player_data.h" // get_my_player -- Verify Map's own "Zoom" buttons
#include "kfx_sim_state.h" // map_tiles_x/y -- Level Settings' read-only map-size display
#include "engine_render.h" // project_world_position_to_screen -- Verify Map's on-screen markers
#include "kfx_config_state.h" // kfx_config_state.texture_id -- base texture set
#include "engine_textures.h" // load_texture_map_file -- live-preview a texture set change
#include "editor_texture_packs.h" // kTexturePackItems -- shared with editor_toolbox.cpp's Paint Texture tool
#include "editor_script_names.h"
#include "script_setup.h" // managed setup region parse/generate -- §4.2
#include "config_creature.h" // creature_desc/creature_code_name -- creature pool picker

#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "post_inc.h"

namespace {
    bool s_show_new_map = false;
    bool s_show_open_map = false;
    bool s_show_save_as = false;
    bool s_show_unsaved_confirm = false;
    bool s_show_dialog_error = false;

    int s_new_map_w = 85;
    int s_new_map_h = 85;
    int s_new_map_texture = 0;
    bool s_new_map_lua = false;

    int s_save_as_format = 0; // index into kFormatItems below
    // Destination for Save As -- defaults to the session's own save dir
    // each time the dialog opens, overridden by "Browse..." (native folder
    // picker, tinyfiledialogs) for an arbitrary destination.
    char s_save_as_dir[512] = "";
    // Level number Save As writes under (the writer names every file
    // map%05lu.<ext> off this -- there's no independent free-text
    // "filename" in this format, the number *is* the filename). Defaults
    // to the session's own current lvnum each time the dialog opens.
    int s_save_as_lvnum = 0;
    // Display name (.lof's NAME_TEXT) -- independent of the level number
    // above, per the user's own ask. Defaults to the session's own current
    // name each time the dialog opens.
    char s_save_as_name[LINEMSG_SIZE] = "";

    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
    // -- Save As's own overwrite confirm. Stashed here (not reusing
    // s_save_as_* directly) so the confirm can fire on the frame *after*
    // Save As's own modal has already closed -- same sequential
    // "one modal hands off to the next, never nested" pattern the
    // unsaved-changes confirm below already established.
    bool s_show_overwrite_confirm = false;
    bool s_show_sidecar_confirm = false;
    std::vector<std::string> s_sidecars; // files Save As would leave behind
    LevelNumber s_pending_save_lvnum = 0;
    char s_pending_save_dir[512] = "";
    int s_pending_save_format = 0;
    char s_pending_save_name[LINEMSG_SIZE] = "";

    // Set whenever an Open/Save As action fails in a way the user needs to
    // see (an unrecognized campaign/mappack file, no map files found in a
    // browsed folder, ...) -- a small modal of our own rather than routing
    // through kfx_frontend's create_frontend_error_box()/GMnu_FEERROR_BOX,
    // whose draw path is wired to frontend_menu_state and isn't confirmed
    // to run while a local game session (the editor) is active.
    char s_dialog_error[256] = "";

    void show_dialog_error(const char *msg)
    {
        snprintf(s_dialog_error, sizeof(s_dialog_error), "%s", msg);
        s_dialog_error[sizeof(s_dialog_error) - 1] = '\0';
        s_show_dialog_error = true;
    }

    // What the unsaved-changes confirm should do on "Discard" -- stashed
    // by whichever caller opened it (a relaunch target, or a plain exit).
    enum PendingAction { PA_None, PA_Relaunch, PA_Exit };
    PendingAction s_pending_action = PA_None;
    LevelNumber s_pending_lvnum = 0;
    TbBool s_pending_is_new = false;
    MapSlabCoord s_pending_w = 85;
    MapSlabCoord s_pending_h = 85;
    long s_pending_texture = 0;

    void do_relaunch(LevelNumber lvnum, TbBool is_new, MapSlabCoord w, MapSlabCoord h, long texture)
    {
        // Order matters: stash the relaunch target before sending the quit
        // packet, so get_startup_menu_state() (frontend.cpp) sees
        // editor_pending_relaunch set once it processes the quit.
        frontend_request_editor_relaunch(lvnum, is_new, w, h, texture);
        editor_close();
    }

    // Shared by New Map's Create and Open Map's row-click: both end up
    // wanting the same "relaunch, but ask first if there are unsaved
    // changes" behavior.
    void request_relaunch(LevelNumber lvnum, TbBool is_new, MapSlabCoord w, MapSlabCoord h, long texture)
    {
        if (editor_is_dirty())
        {
            s_pending_action = PA_Relaunch;
            s_pending_lvnum = lvnum;
            s_pending_is_new = is_new;
            s_pending_w = w;
            s_pending_h = h;
            s_pending_texture = texture;
            s_show_unsaved_confirm = true;
        }
        else
        {
            do_relaunch(lvnum, is_new, w, h, texture);
        }
    }

    void draw_new_map_dialog(void)
    {
        if (!s_show_new_map)
            return;
        FeOpenModal("##EditorNewMap");
        bool open = FeBeginModal("##EditorNewMap");
        if (open)
        {
            FeHeading("New Map");
            FeSeparator();
            // Width/height/texture only -- matches what
            // editor_request_blank_map() actually supports (main_game.c);
            // name/author/keeper-count need MapLevelInfo round-tripping,
            // phase 5 scope (docs/refactor/editor/phase3/00-slice1-native-save.md).
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Width (slabs)", &s_new_map_w);
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Height (slabs)", &s_new_map_h);
            editor_texture_pack_combo("Texture set", &s_new_map_texture, editor_current_lvnum());
            FeCheckbox("Lua script (instead of a .txt script)", &s_new_map_lua);
            FeSeparator();

            const ImVec2 btn_size(140, 0);
            if (FeButton("Create", btn_size))
            {
                MapSlabCoord w = (MapSlabCoord)((s_new_map_w > 0) ? s_new_map_w : 85);
                MapSlabCoord h = (MapSlabCoord)((s_new_map_h > 0) ? s_new_map_h : 85);
                s_show_new_map = false;
                ImGui::CloseCurrentPopup();
                editor_set_new_map_lua(s_new_map_lua);
                request_relaunch(EDITOR_SCRATCH_LEVEL_NUMBER, true, w, h, (long)s_new_map_texture);
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_new_map = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    // One row's thumbnail: land_preview_build_minimap() + a handful of
    // ImGui::GetWindowDrawList()->AddRectFilled() calls -- deliberately not
    // land_preview_load()/land_preview_draw()'s own pan/zoom/ornate-frame
    // machinery, which is heavier than a small per-row swatch needs and
    // isn't meant to be reused editor-side (see frontmenu_landpreview.h's
    // own comment on the exports this uses).
    void draw_level_thumbnail(LevelNumber lvnum, const ImVec2 &size)
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        if (land_preview_build_minimap(lvnum))
        {
            long map_w = land_preview_minimap_width();
            long map_h = land_preview_minimap_height();
            if ((map_w > 0) && (map_h > 0))
            {
                float cell_w = size.x / (float)map_w;
                float cell_h = size.y / (float)map_h;
                for (long y = 0; y < map_h; y++)
                {
                    for (long x = 0; x < map_w; x++)
                    {
                        unsigned char r, g, b;
                        land_preview_minimap_pixel_rgb(x, y, &r, &g, &b);
                        ImVec2 c0(p0.x + x * cell_w, p0.y + y * cell_h);
                        ImVec2 c1(c0.x + cell_w + 1.0f, c0.y + cell_h + 1.0f);
                        draw_list->AddRectFilled(c0, c1, IM_COL32(r, g, b, 255));
                    }
                }
            }
            land_preview_free_minimap();
        }
        else
        {
            draw_list->AddRectFilled(p0, ImVec2(p0.x + size.x, p0.y + size.y), IM_COL32(30, 26, 22, 255));
        }
        ImGui::Dummy(size);
    }

    // Informational only -- load_level_file() (lvl_filesdk1.c) already
    // auto-detects this itself per level (tries the KFX-native .lgtfx
    // first, falls back to classic .lgt), so this is purely for the row's
    // own "KFX / classic" badge (08-gui-layout.md §5's own Open Map spec),
    // not a format choice that feeds back into loading.
    bool level_is_kfx_native_format(LevelNumber lvnum)
    {
        short fgroup = get_level_fgroup(lvnum);
        char *fname = prepare_file_fmtpath(fgroup, "map%05lu.lgtfx", (unsigned long)lvnum);
        return LbFileExists(fname) != 0;
    }

    void draw_open_map_level_row(LevelNumber lvnum, const char *label_prefix)
    {
        const ImVec2 thumb_size(32, 32);
        draw_level_thumbnail(lvnum, thumb_size);
        ImGui::SameLine();
        char label[64];
        snprintf(label, sizeof(label), "%s %lu (%s)", label_prefix, (unsigned long)lvnum,
            level_is_kfx_native_format(lvnum) ? "KFX" : "classic");
        if (FeListRow(label, false))
        {
            s_show_open_map = false;
            ImGui::CloseCurrentPopup();
            request_relaunch(lvnum, false, 0, 0, 0);
        }
    }

    void draw_open_map_dialog(void)
    {
        if (!s_show_open_map)
            return;
        FeOpenModal("##EditorOpenMap");
        bool open = FeBeginModal("##EditorOpenMap");
        if (open)
        {
            FeHeading("Open Map");
            FeSeparator();

            // docs/refactor/editor/phase3/03-slice4-file-dialogs.md -- the
            // engine addresses a level by (active campaign/mappack, lvnum),
            // not by an arbitrary OS path: change_campaign() switches which
            // one is active (picking from the same in-memory catalog the
            // main menu's own Campaign Select uses), and the list below
            // always shows the *currently* active one's own levels -- so
            // picking a different campaign/mappack here just changes what
            // that list shows next, no separate step/state needed.
            if (FeButton("Browse for Campaign/Mappack...", ImVec2(300, 0)))
            {
                const char *picked = platform_pick_open_file_dialog(
                    "Select a campaign or mappack file", NULL, "*.cfg", "Campaign/mappack config");
                if (picked != NULL)
                {
                    char basename[DISKPATH_SIZE];
                    const char *slash = strrchr(picked, '/');
                    const char *backslash = strrchr(picked, '\\');
                    const char *last_sep = (backslash != NULL && (slash == NULL || backslash > slash)) ? backslash : slash;
                    snprintf(basename, sizeof(basename), "%s", (last_sep != NULL) ? last_sep + 1 : picked);
                    if (!change_campaign(CampgnT_Default, basename))
                    {
                        char msg[300];
                        snprintf(msg, sizeof(msg), "\"%s\" isn't a recognized campaign or mappack file.", basename);
                        show_dialog_error(msg);
                    }
                }
            }
            FeSeparator();

            // Same source frontgui_editorbrowser_frame() used: single-player
            // campaign levels (a known-good baseline to open) plus freeplay
            // levels, not a dedicated "writable maps" enumeration -- of
            // whichever campaign/mappack is currently active.
            bool list_open = FeBeginListBox("##EditorOpenMapList", ImVec2(360, 260));
            if (list_open)
            {
                for (unsigned long i = 0; i < campaign.single_levels_count; i++)
                    draw_open_map_level_row(campaign.single_levels[i], "Level");
                for (unsigned long i = 0; i < campaign.freeplay_levels_count; i++)
                    draw_open_map_level_row(campaign.freeplay_levels[i], "Map");
            }
            FeEndListBox(list_open);
            FeSeparator();

            if (FeButton("Cancel", ImVec2(140, 0)))
            {
                s_show_open_map = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    // Shared by Save As's own direct-save path and the overwrite-confirmed
    // one -- players/is_multiplayer are always the session's own current
    // values (Level Settings' own job now, not Save As's), matching
    // editor_dialogs_save_now()'s own "don't blank out what's already
    // there" reasoning.
    void do_save_as(LevelNumber lvnum, const char *dir, enum EditorSaveFormat fmt, const char *name)
    {
        const bool into_editor_maps = editor_maps_is_dir(dir);
        if (into_editor_maps)
            editor_maps_ensure_dir();
        if (editor_save_map(lvnum, dir, fmt, name, editor_current_level_players(), editor_current_level_is_multiplayer(),
                editor_current_level_description()))
        {
            if (into_editor_maps)
                editor_maps_register();
            // Standard "Save As" semantics: this becomes the session's own
            // identity from now on, same as editor_set_current_lvnum_and_dir()'s
            // own comment.
            editor_set_current_lvnum_and_dir(lvnum, dir);
            editor_set_current_level_name(name);
            editor_clear_dirty();
        }
        else
        {
            show_dialog_error("Save failed -- check the destination folder is writable.");
        }
    }

    // Second half of Save As, once the sidecar warning (if any) is settled:
    // the overwrite check, then the save itself. Works off the s_pending_save_*
    // values the dialog stashed.
    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md --
    // overwrite confirm: check for an existing map%05lu.slb at the target
    // before writing. The directory is an arbitrary path (possibly picked via
    // Browse...), not necessarily fgroup-relative, so the check path is built
    // directly rather than through editor_level_save_dir().
    void continue_save_as(void)
    {
        enum EditorSaveFormat fmt = EdSaveFmt_Auto;
        if (s_pending_save_format == 1)
            fmt = EdSaveFmt_ForceKeeperFX;
        else if (s_pending_save_format == 2)
            fmt = EdSaveFmt_ForceClassic;
        char check_path[600];
        snprintf(check_path, sizeof(check_path), "%s/map%05lu.slb", s_pending_save_dir, (unsigned long)s_pending_save_lvnum);
        if (LbFileExists(check_path))
            s_show_overwrite_confirm = true;
        else
            do_save_as(s_pending_save_lvnum, s_pending_save_dir, fmt, s_pending_save_name);
    }

    void draw_sidecar_confirm(void)
    {
        if (!s_show_sidecar_confirm)
            return;
        FeOpenModal("##EditorSidecarConfirm");
        bool open = FeBeginModal("##EditorSidecarConfirm");
        if (open)
        {
            FeHeading("Files that will not be copied");
            FeSeparator();
            FeBodyText("This level has files the editor does not save. Save As writes the map"
                " to the new location only; these stay behind:");
            const size_t shown = (s_sidecars.size() < 8) ? s_sidecars.size() : 8;
            for (size_t i = 0; i < shown; i++)
                FeBodyText(s_sidecars[i].c_str());
            if (s_sidecars.size() > shown)
            {
                char more[48];
                snprintf(more, sizeof(more), "... and %zu more", s_sidecars.size() - shown);
                FeBodyText(more);
            }
            FeBodyText("Copy them yourself if the new level needs them.");
            FeSeparator();
            const ImVec2 btn_size(140, 0);
            if (FeButton("Save anyway", btn_size))
            {
                s_show_sidecar_confirm = false;
                ImGui::CloseCurrentPopup();
                continue_save_as();
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_sidecar_confirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    void draw_save_as_dialog(void)
    {
        if (!s_show_save_as)
            return;
        FeOpenModal("##EditorSaveAs");
        bool open = FeBeginModal("##EditorSaveAs");
        if (open)
        {
            FeHeading("Save As");
            FeSeparator();
            // docs/refactor/editor/phase3/03-slice4-file-dialogs.md --
            // editor_save_map()'s `dir` is a plain filesystem directory (no
            // campaign/lvnum resolution on the write side at all, unlike
            // Open), so a native folder picker just sets it directly.
            FeBodyText(s_save_as_dir);
            if (FeButton("Browse...", ImVec2(140, 0)))
            {
                const char *picked = platform_pick_folder_dialog("Save map into folder", s_save_as_dir);
                if (picked != NULL)
                {
                    snprintf(s_save_as_dir, sizeof(s_save_as_dir), "%s", picked);
                    s_save_as_dir[sizeof(s_save_as_dir) - 1] = '\0';
                }
            }
            // The writer names every file map%05lu.<ext> off this number --
            // there's no independent free-text "filename" in this format.
            // The display name (below) is a separate, independent field --
            // .lof's own NAME_TEXT, not tied to the number at all.
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Level Number", &s_save_as_lvnum);
            FeTextInput("Level Name", s_save_as_name, sizeof(s_save_as_name));
            static const char *kFormatItems[] = { "Auto", "Force KeeperFX", "Force Classic" };
            FeCombo("Format", &s_save_as_format, kFormatItems, 3);
            FeSeparator();

            const ImVec2 btn_size(140, 0);
            if (FeButton("Save", btn_size))
            {
                LevelNumber save_lvnum = (LevelNumber)((s_save_as_lvnum > 0) ? s_save_as_lvnum : 1);

                // Files the editor doesn't save (Lua, rules, ...) would be
                // left behind by a move: warn first, then continue as usual.
                s_pending_save_lvnum = save_lvnum;
                snprintf(s_pending_save_dir, sizeof(s_pending_save_dir), "%s", s_save_as_dir);
                s_pending_save_dir[sizeof(s_pending_save_dir) - 1] = '\0';
                s_pending_save_format = s_save_as_format;
                snprintf(s_pending_save_name, sizeof(s_pending_save_name), "%s", s_save_as_name);
                s_pending_save_name[sizeof(s_pending_save_name) - 1] = '\0';
                s_sidecars.clear();
                if (editor_save_is_relocation(editor_current_save_dir(), (unsigned long)editor_current_lvnum(),
                        s_save_as_dir, (unsigned long)save_lvnum))
                    s_sidecars = editor_find_sidecars(editor_current_save_dir(), (unsigned long)editor_current_lvnum());
                if (!s_sidecars.empty())
                    s_show_sidecar_confirm = true;
                else
                    continue_save_as();
                s_show_save_as = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_save_as = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    void draw_overwrite_confirm(void)
    {
        if (!s_show_overwrite_confirm)
            return;
        FeOpenModal("##EditorOverwriteConfirm");
        bool open = FeBeginModal("##EditorOverwriteConfirm");
        if (open)
        {
            FeHeading("Overwrite existing map?");
            FeSeparator();
            char msg[128];
            snprintf(msg, sizeof(msg), "A map already exists at level %lu in that folder.",
                (unsigned long)s_pending_save_lvnum);
            FeBodyText(msg);
            FeSeparator();

            const ImVec2 btn_size(140, 0);
            if (FeButton("Overwrite", btn_size))
            {
                enum EditorSaveFormat fmt = EdSaveFmt_Auto;
                if (s_pending_save_format == 1)
                    fmt = EdSaveFmt_ForceKeeperFX;
                else if (s_pending_save_format == 2)
                    fmt = EdSaveFmt_ForceClassic;
                do_save_as(s_pending_save_lvnum, s_pending_save_dir, fmt, s_pending_save_name);
                s_show_overwrite_confirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_overwrite_confirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    void draw_dialog_error(void)
    {
        if (!s_show_dialog_error)
            return;
        FeOpenModal("##EditorDialogError");
        bool open = FeBeginModal("##EditorDialogError");
        if (open)
        {
            FeHeading("Error");
            FeSeparator();
            FeBodyText(s_dialog_error);
            FeSeparator();
            if (FeButton("OK", ImVec2(140, 0)))
            {
                s_show_dialog_error = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    void draw_unsaved_confirm(void)
    {
        if (!s_show_unsaved_confirm)
            return;
        FeOpenModal("##EditorUnsavedConfirm");
        bool open = FeBeginModal("##EditorUnsavedConfirm");
        if (open)
        {
            FeHeading("Unsaved changes");
            FeSeparator();
            FeBodyText("This map has unsaved changes. Discard them?");
            FeSeparator();

            const ImVec2 btn_size(140, 0);
            if (FeButton("Discard", btn_size))
            {
                s_show_unsaved_confirm = false;
                ImGui::CloseCurrentPopup();
                PendingAction action = s_pending_action;
                s_pending_action = PA_None;
                if (action == PA_Relaunch)
                    do_relaunch(s_pending_lvnum, s_pending_is_new, s_pending_w, s_pending_h, s_pending_texture);
                else if (action == PA_Exit)
                    editor_close();
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_pending_action = PA_None;
                s_show_unsaved_confirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
    // -- name/players/is_multiplayer (.lof's NAME_TEXT/PLAYERS/KIND), on
    // their own modal per the user's own ask -- independent of New Map
    // (which only ever asked for size/texture) and Save As (number/
    // destination/format). Applying writes *only* .lof
    // (editor_save_level_info()), not a full map re-save -- these are
    // display/registration metadata, not map content, so there's nothing
    // for the dirty flag to track here.
    bool s_show_level_settings = false;
    char s_level_settings_name[LINEMSG_SIZE] = "";
    int s_level_settings_players = 1;
    bool s_level_settings_multiplayer = false;
    // docs/refactor/editor/05-script-and-level-settings.md §1 -- DESCRIPTION
    // was already a recognized .lof keyword and an existing LevelInformation
    // field, just never wired up on either side before this slice.
    char s_level_settings_description[LEVEL_DESCRIPTION_LEN] = "";
    char s_level_settings_author[LEVEL_AUTHOR_LEN] = "";
    // docs/refactor/editor/05-script-and-level-settings.md's deferred "base
    // texture set" item -- turned out not to need any new persistence at
    // all: snapshot_map() (editor_mapsave.cpp) already reads kfx_config_
    // state.texture_id live at save time (MapContent::texture_id has always
    // round-tripped through .inf, since before this slice), so this is
    // read/written straight against that live value on open/Apply, same as
    // the map-size display above -- no new editor_current_*()/editor_set_
    // current_*() session-state accessor pair needed, unlike name/players/
    // multiplayer/description (those exist independently of any live
    // engine state kfx_editor could just read directly).
    int s_level_settings_texture_id = 0;
    // Resize (fx-plans/00 A14): the size fields, and the confirm that lists what would be lost.
    int s_resize_w = 85, s_resize_h = 85;
    bool s_resize_centered = false;
    bool s_show_resize_confirm = false;
    int s_resize_things = 0, s_resize_lights = 0, s_resize_points = 0;

    // docs/refactor/editor/05-script-and-level-settings.md §4.2 -- the
    // "managed setup region" fields: buffered here exactly like every
    // other Level Settings field (typing doesn't touch the real script
    // text until Apply), seeded on open from editor_script_parse_managed_
    // setup() against whatever managed region the current script already
    // has (empty/all-zero for a level that's never had this Applied
    // before). start_money/max_creatures are reactively resized to match
    // s_level_settings_players every frame the dialog draws (see draw_
    // level_settings_dialog()'s own top-of-function resize) since Players
    // can change while this same dialog is open, before Apply.
    int s_level_settings_generate_speed = 0;
    std::vector<int> s_level_settings_start_money;
    std::vector<int> s_level_settings_max_creatures;
    std::vector<std::pair<ThingModel, int>> s_level_settings_creature_pool;
    // Index into the "pick a creature to add" combo below the pool list --
    // purely local UI state, not part of the pool itself until "Add" is
    // clicked (same shadow-selection shape editor_toolbox.cpp's own
    // s_selected_creature_kind already uses for its picker).
    int s_level_settings_pool_add_index = 0;
    // Win/lose rules (fx: managed block). Variable names offered by the
    // clause combos are the engine's script variables, plus any name a rule
    // already uses.
    std::vector<WinLoseRule> s_level_settings_rules;
    std::vector<std::string> s_win_var_names;

    std::vector<std::string> collect_win_var_names(const std::vector<WinLoseRule> &rules)
    {
        std::vector<std::string> names;
        for (const ScriptNameGroup &g : editor_script_collect_name_groups())
            if (g.title == "Variables")
                names = g.names;
        for (const WinLoseRule &r : rules)
            for (const WinLoseClause &c : r.clauses)
                if (std::find(names.begin(), names.end(), c.variable) == names.end())
                    names.push_back(c.variable);
        return names;
    }

    void draw_win_lose_rules(int script_players)
    {
        FeHeading("Win / Lose Conditions");
        FeCaption("Written to this script's setup block. Conditions written by hand elsewhere in the script are left alone.");
        static const char *const kResult[] = { "Win", "Lose" };
        static const char *const kPlayerNames[] = { "PLAYER0", "PLAYER1", "PLAYER2", "PLAYER3", "PLAYER4", "PLAYER5", "PLAYER6" };
        int op_count = 0;
        const char *const *ops = script_setup_win_lose_operators(&op_count);
        std::vector<const char *> var_ptrs;
        for (const std::string &n : s_win_var_names)
            var_ptrs.push_back(n.c_str());
        const int player_count = (script_players < 7) ? script_players : 7;

        for (size_t r = 0; r < s_level_settings_rules.size(); r++)
        {
            WinLoseRule &rule = s_level_settings_rules[r];
            ImGui::PushID((int)(1000 + r));
            int result = rule.win ? 0 : 1;
            ImGui::SetNextItemWidth(80);
            if (FeCombo("##result", &result, kResult, 2))
                rule.win = (result == 0);
            ImGui::SameLine();
            FeBodyText("when");
            for (size_t c = 0; c < rule.clauses.size(); c++)
            {
                WinLoseClause &cl = rule.clauses[c];
                ImGui::PushID((int)c);
                if (c > 0)
                    FeBodyText("   and");
                int pl = (cl.player < player_count) ? cl.player : 0;
                ImGui::SetNextItemWidth(100);
                if (FeCombo("##player", &pl, kPlayerNames, player_count > 0 ? player_count : 1))
                    cl.player = pl;
                ImGui::SameLine();
                int vi = 0;
                for (size_t k = 0; k < s_win_var_names.size(); k++)
                    if (s_win_var_names[k] == cl.variable)
                        vi = (int)k;
                ImGui::SetNextItemWidth(210);
                if (!var_ptrs.empty() && FeCombo("##var", &vi, var_ptrs.data(), (int)var_ptrs.size()))
                    cl.variable = s_win_var_names[(size_t)vi];
                ImGui::SameLine();
                int oi = 0;
                for (int k = 0; k < op_count; k++)
                    if (cl.op == ops[k])
                        oi = k;
                ImGui::SetNextItemWidth(70);
                if (FeCombo("##op", &oi, ops, op_count))
                    cl.op = ops[oi];
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100);
                ImGui::InputInt("##value", &cl.value);
                if (rule.clauses.size() > 1)
                {
                    ImGui::SameLine();
                    if (FeButton("x", ImVec2(28, 0)))
                    {
                        rule.clauses.erase(rule.clauses.begin() + (long)c);
                        ImGui::PopID();
                        break;
                    }
                }
                ImGui::PopID();
            }
            if (FeButton("Add 'and' condition", ImVec2(180, 0)))
                rule.clauses.push_back(WinLoseClause());
            ImGui::SameLine();
            const bool remove = FeButton("Remove rule", ImVec2(140, 0));
            ImGui::PopID();
            if (remove)
            {
                s_level_settings_rules.erase(s_level_settings_rules.begin() + (long)r);
                break;
            }
            FeSeparator();
        }
        if (FeButton("Add rule", ImVec2(140, 0)))
        {
            WinLoseRule rule;
            rule.clauses.push_back(WinLoseClause());
            s_level_settings_rules.push_back(rule);
        }
        ImGui::SameLine();
        if (FeButton("Add standard rules", ImVec2(180, 0)))
        {
            // Lose when your own dungeon is destroyed; win when every other
            // keeper's is.
            WinLoseRule lose;
            lose.win = false;
            lose.clauses.push_back(WinLoseClause());
            s_level_settings_rules.push_back(lose);
            if (script_players > 1)
            {
                WinLoseRule win;
                for (int p = 1; p < script_players; p++)
                {
                    WinLoseClause c;
                    c.player = p;
                    win.clauses.push_back(c);
                }
                s_level_settings_rules.push_back(win);
            }
        }
    }

    void draw_resize_confirm(void)
    {
        if (!s_show_resize_confirm)
            return;
        FeOpenModal("##EditorResizeConfirm");
        bool open = FeBeginModal("##EditorResizeConfirm");
        if (open)
        {
            FeHeading("Resize map");
            FeSeparator();
            char msg[160];
            snprintf(msg, sizeof(msg), "Resize to %d x %d slabs. New ground is solid rock.", s_resize_w, s_resize_h);
            FeBodyText(msg);
            if (s_resize_things + s_resize_lights + s_resize_points > 0)
            {
                snprintf(msg, sizeof(msg), "Removed: %d things, %d lights, %d action points.",
                    s_resize_things, s_resize_lights, s_resize_points);
                FeBodyText(msg);
            }
            FeBodyText("The editor reopens the resized map; it stays unsaved until you save.");
            FeSeparator();
            const ImVec2 btn_size(140, 0);
            if (FeButton("Resize", btn_size))
            {
                s_show_resize_confirm = false;
                s_show_level_settings = false;
                ImGui::CloseCurrentPopup();
                if (!editor_resize_map(s_resize_w, s_resize_h, s_resize_centered))
                    show_dialog_error("Couldn't resize the map (leave 1st Person first, and check the folder is writable).");
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_resize_confirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    void draw_level_settings_dialog(void)
    {
        if (!s_show_level_settings)
            return;
        FeOpenModal("##EditorLevelSettings");
        bool open = FeBeginModalResizable("##EditorLevelSettings", ImVec2(620, 560), ImVec2(520, 380));
        if (open)
        {
            FeHeading("Level Settings");
            FeSeparator();
            // Kept in step with the Players field every frame, so raising or
            // lowering it grows/shrinks the per-player rows on the Script
            // Setup tab (already-entered values for surviving indices stay).
            int script_players = (s_level_settings_players > 0) ? s_level_settings_players : 1;
            if ((int)s_level_settings_start_money.size() != script_players)
                s_level_settings_start_money.resize((size_t)script_players, 0);
            if ((int)s_level_settings_max_creatures.size() != script_players)
                s_level_settings_max_creatures.resize((size_t)script_players, 0);
            bool tabs = FeBeginTabBar("##LevelSettingsTabs");
            if (tabs && FeTab("Level"))
            {
            ImGui::BeginChild("##LevelTab", ImVec2(0, -48.0f), ImGuiChildFlags_None);
            FeTextInput("Level Name", s_level_settings_name, sizeof(s_level_settings_name));
            FeTextInput("Description", s_level_settings_description, sizeof(s_level_settings_description));
            FeTextInput("Author", s_level_settings_author, sizeof(s_level_settings_author));
            ImGui::SetNextItemWidth(120);
            ImGui::InputInt("Players", &s_level_settings_players);
            FeCheckbox("Multiplayer", &s_level_settings_multiplayer);
            // docs/refactor/editor/05-script-and-level-settings.md §1 -- read-
            // only display, live session state, not persisted by this dialog
            // (map dimensions are fixed at New Map, see that doc's own
            // "read-only after creation for v1" note).
            {
                char map_size_label[48];
                snprintf(map_size_label, sizeof(map_size_label), "Map size: %d x %d",
                    (int)kfx_sim_state.map_tiles_x, (int)kfx_sim_state.map_tiles_y);
                FeBodyText(map_size_label);
                ImGui::SetNextItemWidth(90);
                ImGui::InputInt("New width", &s_resize_w);
                ImGui::SetNextItemWidth(90);
                ImGui::InputInt("New height", &s_resize_h);
                FeCheckbox("Keep the old map centred", &s_resize_centered);
                if (FeButton("Resize...", ImVec2(140, 0)))
                {
                    s_resize_things = s_resize_lights = s_resize_points = 0;
                    if (editor_resize_preview(s_resize_w, s_resize_h, s_resize_centered, &s_resize_things, &s_resize_lights, &s_resize_points))
                    {
                        s_show_resize_confirm = true;
                        s_show_level_settings = false; // one modal at a time
                        ImGui::CloseCurrentPopup();
                    }
                    else
                        show_dialog_error("That size is not supported (8 to 170 slabs each way).");
                }
            }
            {
                // Which keepers have a Dungeon Heart on the map (read-only).
                bool has_heart[PLAYERS_COUNT] = {};
                for (ThingIndex ti = 1; ti < THINGS_COUNT; ti++)
                {
                    const struct Thing *t = thing_get(ti);
                    if (thing_exists(t) && (t->class_id == TCls_Object) && thing_is_dungeon_heart(t) && (t->owner < PLAYERS_COUNT))
                        has_heart[t->owner] = true;
                }
                char hearts[96] = "Dungeon Hearts:";
                int shown = 0;
                for (int p = 0; p < PLAYERS_COUNT; p++)
                    if (has_heart[p] && p != kfx_config_state.neutral_player_num)
                    {
                        char one[16];
                        snprintf(one, sizeof(one), " P%d", p + 1);
                        strncat(hearts, one, sizeof(hearts) - strlen(hearts) - 1);
                        shown++;
                    }
                if (shown == 0)
                    strncat(hearts, " none", sizeof(hearts) - strlen(hearts) - 1);
                FeBodyText(hearts);
            }
            editor_texture_pack_combo("Base Texture Set", &s_level_settings_texture_id, editor_current_lvnum());
            ImGui::EndChild();
            FeEndTab();
            }
            if (tabs && FeTab("Script Setup"))
            {
            ImGui::BeginChild("##ScriptTab", ImVec2(0, -48.0f), ImGuiChildFlags_None);
            FeHeading("Script Setup");
            editor_lua_override_banner();
            ImGui::SetNextItemWidth(140);
            ImGui::InputInt("Generation Speed", &s_level_settings_generate_speed);
            for (int i = 0; i < script_players; i++)
            {
                ImGui::PushID(i);
                char gold_label[24];
                snprintf(gold_label, sizeof(gold_label), "P%d Gold", i);
                ImGui::SetNextItemWidth(140);
                ImGui::InputInt(gold_label, &s_level_settings_start_money[(size_t)i]);
                ImGui::SameLine();
                char max_label[32];
                snprintf(max_label, sizeof(max_label), "P%d Max Creatures", i);
                ImGui::SetNextItemWidth(140);
                ImGui::InputInt(max_label, &s_level_settings_max_creatures[(size_t)i]);
                ImGui::PopID();
            }

            FeBodyText("Creature Pool:");
            {
                bool pool_open = FeBeginListBox("##EdLevelSettingsPool", ImVec2(420, 120));
                if (pool_open)
                {
                    for (size_t i = 0; i < s_level_settings_creature_pool.size(); i++)
                    {
                        ImGui::PushID((int)i);
                        ImGui::TextUnformatted(creature_code_name(s_level_settings_creature_pool[i].first));
                        ImGui::SameLine();
                        ImGui::SetNextItemWidth(100);
                        ImGui::InputInt("##count", &s_level_settings_creature_pool[i].second);
                        ImGui::SameLine();
                        if (FeButton("Remove", ImVec2(70, 0)))
                        {
                            s_level_settings_creature_pool.erase(s_level_settings_creature_pool.begin() + (long)i);
                            ImGui::PopID();
                            break; // vector just mutated -- don't keep indexing into it this frame
                        }
                        ImGui::PopID();
                    }
                }
                FeEndListBox(pool_open);
            }
            {
                // §2.5's own "loop [1, model_count)" + creature_code_name()
                // pattern (editor_toolbox.cpp's draw_creature_picker()) --
                // rebuilt fresh each frame (cheap: model_count is at most a
                // few hundred pointer copies) rather than cached, since it
                // only needs to exist for this one FeCombo call.
                long model_count = kfx_config_state.conf.crtr_conf.model_count;
                std::vector<const char *> pool_add_names;
                std::vector<ThingModel> pool_add_ids;
                for (ThingModel m = 1; m < (ThingModel)model_count; m++)
                {
                    pool_add_names.push_back(creature_code_name(m));
                    pool_add_ids.push_back(m);
                }
                if (!pool_add_names.empty())
                {
                    if (s_level_settings_pool_add_index >= (int)pool_add_names.size())
                        s_level_settings_pool_add_index = 0;
                    ImGui::SetNextItemWidth(200);
                    FeCombo("##PoolAddCreature", &s_level_settings_pool_add_index, pool_add_names.data(), (int)pool_add_names.size());
                    ImGui::SameLine();
                    if (FeButton("Add to Pool", ImVec2(120, 0)))
                    {
                        s_level_settings_creature_pool.push_back(
                            std::make_pair(pool_add_ids[(size_t)s_level_settings_pool_add_index], 1));
                    }
                }
            }
            FeSeparator();
            draw_win_lose_rules(script_players);
            ImGui::EndChild();
            FeEndTab();
            }
            FeEndTabBar(tabs);
            FeSeparator();

            const ImVec2 btn_size(140, 0);
            if (FeButton("Apply", btn_size))
            {
                int players = (s_level_settings_players > 0) ? s_level_settings_players : 1;
                // Before the .lof write below, which reads the session's author.
                editor_set_current_level_author(s_level_settings_author);
                if (editor_save_level_info(editor_current_lvnum(), editor_current_save_dir(),
                        s_level_settings_name, players, s_level_settings_multiplayer, s_level_settings_description))
                {
                    editor_set_current_level_name(s_level_settings_name);
                    editor_set_current_level_players(players);
                    editor_set_current_level_is_multiplayer(s_level_settings_multiplayer);
                    editor_set_current_level_description(s_level_settings_description);
                }
                else
                {
                    show_dialog_error("Couldn't write level settings -- check the save folder is writable.");
                }
                // Independent of the .lof write above (a completely separate
                // file, .inf, with its own already-established live-session-
                // read-at-save-time path) -- applied unconditionally so a
                // failed .lof write doesn't also swallow this. Live-previews
                // immediately via the same load_texture_map_file() call the
                // SET_MAP_TEXTURE script command/Lua API already use to
                // change it mid-session (lua_api_map.c) -- proven safe to
                // call outside of level load.
                if ((unsigned long)s_level_settings_texture_id != kfx_config_state.texture_id)
                {
                    kfx_config_state.texture_id = (unsigned char)s_level_settings_texture_id;
                    load_texture_map_file((unsigned long)s_level_settings_texture_id,
                        editor_current_lvnum(), get_level_fgroup(editor_current_lvnum()));
                }
                // docs/refactor/editor/05-script-and-level-settings.md §4.2
                // -- commits to session state + marks dirty, deferred
                // persistence like every other content edit (the script
                // editor's own Apply button, terrain painting, ...), not
                // an immediate disk write like the .lof/.inf fields above
                // -- there's no separate "script file" to independently
                // write here, this is part of the same map%05lu.txt a
                // plain Save already carries. Always regenerated with the
                // CURRENT dialog values, even if nothing changed -- cheap,
                // and it also self-heals a managed region a hand-edit
                // might have mangled (same "editor generates as the source
                // of truth" reasoning as write_slab_texture() always
                // writing, not skipping when nothing changed).
                {
                    // Start from whatever the block already holds so the
                    // availability grid's lines (slice 5) survive this
                    // dialog's Apply -- only the four fields this dialog
                    // owns are overwritten.
                    ManagedSetupValues managed_values = script_setup_parse(
                        script_setup_extract_region(editor_current_level_script_text()), script_players,
                        script_setup_level_version(editor_current_level_script_text()));
                    managed_values.generate_speed = s_level_settings_generate_speed;
                    managed_values.start_money = s_level_settings_start_money;
                    managed_values.max_creatures = s_level_settings_max_creatures;
                    managed_values.creature_pool = s_level_settings_creature_pool;
                    managed_values.rules = s_level_settings_rules;
                    std::string new_body = script_setup_generate(managed_values, script_players,
                        script_setup_level_version(editor_current_level_script_text()));
                    std::string new_script = script_setup_replace_region(
                        editor_current_level_script_text(), new_body);
                    editor_set_current_level_script_text(new_script.c_str());
                    editor_mark_dirty();
                }
                s_show_level_settings = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_level_settings = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    // docs/refactor/editor/phase3/04-slice5-playtest-settings-overwrite.md
    // -- Playtest saves the current map to its own scratch slot
    // (EDITOR_PLAYTEST_LEVEL_NUMBER, frontend.h) and launches it the same
    // way the `-level` command-line argument does (set_selected_level_number()
    // + a normal FeSt_START_KPRLEVEL game start). Since phase 5 slice 1,
    // editor_save_map() writes back the session's own real script text
    // (editor_current_level_script_text()) rather than always stubbing an
    // empty one -- MapContentWriter::write_script() only falls back to
    // "REM Empty script" when that text is genuinely empty (a level that
    // never had one, or a brand new map). The confirm below reflects
    // whichever is actually true for the current session instead of
    // unconditionally warning about a limitation that no longer exists.
    bool s_show_playtest_confirm = false;

    void draw_playtest_confirm(void)
    {
        if (!s_show_playtest_confirm)
            return;
        FeOpenModal("##EditorPlaytestConfirm");
        bool open = FeBeginModal("##EditorPlaytestConfirm");
        if (open)
        {
            FeHeading("Playtest");
            FeSeparator();
            if (editor_current_level_script_text()[0] == '\0')
            {
                FeBodyText("This map has no script yet -- win/lose conditions, custom");
                FeBodyText("triggers, and other script-driven behavior will not run.");
            }
            else
            {
                FeBodyText("This map's script will run during playtest, same as a normal");
                FeBodyText("level launch.");
            }
            FeSeparator();

            const ImVec2 btn_size(140, 0);
            if (FeButton("Playtest", btn_size))
            {
                char playtest_dir[512];
                editor_level_save_dir(EDITOR_PLAYTEST_LEVEL_NUMBER, playtest_dir, sizeof(playtest_dir));
                if (editor_save_map(EDITOR_PLAYTEST_LEVEL_NUMBER, playtest_dir, EdSaveFmt_Auto,
                        editor_current_level_name(), editor_current_level_players(), editor_current_level_is_multiplayer(),
                        editor_current_level_description()))
                {
                    // The level's sidecars (Lua, rules, ...) are keyed by
                    // level number, so give the scratch level its own copies.
                    editor_remove_sidecars(playtest_dir, (unsigned long)EDITOR_PLAYTEST_LEVEL_NUMBER);
                    editor_copy_sidecars(editor_current_save_dir(), (unsigned long)editor_current_lvnum(),
                        playtest_dir, (unsigned long)EDITOR_PLAYTEST_LEVEL_NUMBER);
                    editor_playtest_begin();
                    frontend_request_editor_playtest(EDITOR_PLAYTEST_LEVEL_NUMBER);
                    editor_close();
                }
                else
                {
                    show_dialog_error("Couldn't save the map to playtest -- check the campaign folder is writable.");
                }
                s_show_playtest_confirm = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (FeButton("Cancel", btn_size))
            {
                s_show_playtest_confirm = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }

    // docs/refactor/editor/phase3/06-slice7-verify-map.md -- purely
    // informational, never blocks anything (same "advisory, not a gate"
    // precedent as map_is_legacy_compatible()'s own Auto-format use).
    // Issues are computed once, when the dialog opens, not re-run every
    // frame -- matches Open Map's own "the underlying data can change
    // between opens, but not while the modal is up" assumption.
    bool s_show_verify_map = false;
    std::vector<MapVerifyIssue> s_verify_issues;

    const char *verify_severity_label(MapVerifyIssueSeverity severity)
    {
        switch (severity)
        {
            case MVI_Error: return "ERROR";
            case MVI_Warn:  return "WARN";
            default:        return "INFO";
        }
    }

    ImU32 verify_severity_marker_color(MapVerifyIssueSeverity severity)
    {
        switch (severity)
        {
            case MVI_Error: return IM_COL32(255, 64, 64, 255);
            case MVI_Warn:  return IM_COL32(255, 220, 0, 255);
            default:        return IM_COL32(120, 200, 255, 255);
        }
    }

    // docs/refactor/editor/04-views-camera-overlays.md -- phase 4's overlay
    // projection spike, first real consumer: a marker on top of the 3D view
    // for each issue with a known position, drawn every frame the dialog is
    // open (not just once) so it tracks the camera as it moves/zooms. Floor
    // level (z=0) is used for every marker -- MapVerifyIssue doesn't carry a
    // height, and a flag sitting on the ground is a reasonable default for
    // "something is wrong around here." Skips anything project_world_
    // position_to_screen() reports as off-screen/behind the camera, same
    // convention the underlying rotpers() clip_flags already establish.
    void draw_verify_map_markers(void)
    {
        if (!s_show_verify_map)
            return;
        ImDrawList *draw_list = ImGui::GetForegroundDrawList();
        for (const MapVerifyIssue &issue : s_verify_issues)
        {
            if (!issue.has_pos)
                continue;
            long screen_x, screen_y;
            if (!project_world_position_to_screen(issue.pos_x, issue.pos_y, 0, &screen_x, &screen_y))
                continue;
            ImU32 color = verify_severity_marker_color(issue.severity);
            draw_list->AddCircleFilled(ImVec2((float)screen_x, (float)screen_y), 7.0f, color);
            draw_list->AddCircle(ImVec2((float)screen_x, (float)screen_y), 7.0f, IM_COL32(0, 0, 0, 255), 0, 1.5f);
        }
    }

    void draw_verify_map_dialog(void)
    {
        if (!s_show_verify_map)
            return;
        FeOpenModal("##EditorVerifyMap");
        bool open = FeBeginModal("##EditorVerifyMap");
        if (open)
        {
            FeHeading("Verify Map");
            FeSeparator();
            if (s_verify_issues.empty())
            {
                FeBodyText("No issues found.");
            }
            else
            {
                bool list_open = FeBeginListBox("##EditorVerifyMapList", ImVec2(460, 280));
                if (list_open)
                {
                    for (size_t i = 0; i < s_verify_issues.size(); i++)
                    {
                        const MapVerifyIssue &issue = s_verify_issues[i];
                        char label[320];
                        snprintf(label, sizeof(label), "[%s] %s", verify_severity_label(issue.severity), issue.message.c_str());
                        FeListRow(label, false);
                        if (issue.has_pos)
                        {
                            ImGui::SameLine();
                            ImGui::PushID((int)i);
                            if (FeButton("Zoom"))
                                set_players_packet_action(get_my_player(), PckA_ZoomToPosition,
                                    (unsigned long)issue.pos_x, (unsigned long)issue.pos_y, 0, 0);
                            ImGui::PopID();
                        }
                    }
                }
                FeEndListBox(list_open);
            }
            FeSeparator();

            if (FeButton("Close", ImVec2(140, 0)))
            {
                s_show_verify_map = false;
                ImGui::CloseCurrentPopup();
            }
        }
        FeEndModal(open);
    }
} // namespace

void editor_dialogs_open_new_map(void)
{
    s_new_map_w = 85;
    s_new_map_h = 85;
    s_new_map_texture = 0;
    s_show_new_map = true;
}

void editor_dialogs_open_open_map(void)
{
    s_show_open_map = true;
}

void editor_dialogs_open_save_as(void)
{
    s_save_as_format = 0;
    snprintf(s_save_as_dir, sizeof(s_save_as_dir), "%s", editor_current_save_dir());
    s_save_as_dir[sizeof(s_save_as_dir) - 1] = '\0';
    s_save_as_lvnum = (int)editor_current_lvnum();
    if (editor_current_lvnum() == EDITOR_SCRATCH_LEVEL_NUMBER)
    {
        // A map that was never saved goes into the Editor Maps mappack by default.
        snprintf(s_save_as_dir, sizeof(s_save_as_dir), "%s", editor_maps_dir().c_str());
        s_save_as_lvnum = (int)editor_maps_next_free_number(s_save_as_dir);
    }
    snprintf(s_save_as_name, sizeof(s_save_as_name), "%s", editor_current_level_name());
    s_save_as_name[sizeof(s_save_as_name) - 1] = '\0';
    s_show_save_as = true;
}

void editor_dialogs_save_now(void)
{
    // An untitled new map has nowhere to be saved yet: ask (the dialog offers
    // the Editor Maps folder and the next free number).
    if (editor_current_lvnum() == EDITOR_SCRATCH_LEVEL_NUMBER)
    {
        editor_dialogs_open_save_as();
        return;
    }
    // Reuses the session's own current name/players/is_multiplayer -- a
    // plain Save must not blank out settings Save As/Level Settings already
    // set (or ones read back from an existing .lof when the session
    // opened).
    if (editor_save_map(editor_current_lvnum(), editor_current_save_dir(), EdSaveFmt_Auto,
            editor_current_level_name(), editor_current_level_players(), editor_current_level_is_multiplayer(),
            editor_current_level_description()))
        editor_clear_dirty();
}

void editor_dialogs_request_exit(void)
{
    if (editor_is_dirty())
    {
        s_pending_action = PA_Exit;
        s_show_unsaved_confirm = true;
    }
    else
    {
        editor_close();
    }
}

void editor_dialogs_open_level_settings(void)
{
    snprintf(s_level_settings_name, sizeof(s_level_settings_name), "%s", editor_current_level_name());
    s_level_settings_name[sizeof(s_level_settings_name) - 1] = '\0';
    s_level_settings_players = editor_current_level_players();
    s_level_settings_multiplayer = editor_current_level_is_multiplayer() != 0;
    snprintf(s_level_settings_description, sizeof(s_level_settings_description), "%s", editor_current_level_description());
    s_level_settings_description[sizeof(s_level_settings_description) - 1] = '\0';
    snprintf(s_level_settings_author, sizeof(s_level_settings_author), "%s", editor_current_level_author());
    s_level_settings_author[sizeof(s_level_settings_author) - 1] = '\0';
    s_level_settings_texture_id = (int)kfx_config_state.texture_id;
    s_resize_w = (int)kfx_sim_state.map_tiles_x;
    s_resize_h = (int)kfx_sim_state.map_tiles_y;
    // docs/refactor/editor/05-script-and-level-settings.md §4.2 -- read
    // back from whatever managed region the current script already has
    // (all-zero/empty pool for a level that's never had this Applied
    // before, same "best-effort read, empty is a valid starting state"
    // convention every other Level Settings field already follows).
    // s_level_settings_players is already set above, in time to size
    // start_money/max_creatures correctly for parse.
    {
        std::string managed_body = script_setup_extract_region(editor_current_level_script_text());
        ManagedSetupValues managed_values = script_setup_parse(managed_body, s_level_settings_players,
            script_setup_level_version(editor_current_level_script_text()));
        s_level_settings_generate_speed = managed_values.generate_speed;
        s_level_settings_start_money = managed_values.start_money;
        s_level_settings_max_creatures = managed_values.max_creatures;
        s_level_settings_creature_pool = managed_values.creature_pool;
        s_level_settings_rules = managed_values.rules;
        s_win_var_names = collect_win_var_names(s_level_settings_rules);
    }
    s_show_level_settings = true;
}

void editor_dialogs_open_playtest_confirm(void)
{
    s_show_playtest_confirm = true;
}

void editor_dialogs_open_verify_map(void)
{
    MapContent content;
    editor_snapshot_current_map(content);
    s_verify_issues = verify_map_content(content);
    // The script's own problems (unknown commands, unclosed IF) belong in the
    // same report; they carry no map position.
    for (const ScriptIssue &si : editor_script_validate_engine(content.script_text))
    {
        MapVerifyIssue issue;
        issue.severity = (si.severity == ScrIssue_Error) ? MVI_Error : MVI_Warn;
        issue.message = "Script line " + std::to_string(si.line + 1) + ": " + si.message;
        s_verify_issues.push_back(issue);
    }
    if (content.has_lua)
    {
        for (const ScriptIssue &si : editor_lua_validate_engine(content.lua_text, editor_current_save_dir()))
        {
            MapVerifyIssue issue;
            issue.severity = (si.severity == ScrIssue_Error) ? MVI_Error : MVI_Warn;
            issue.message = "Lua line " + std::to_string(si.line + 1) + ": " + si.message;
            s_verify_issues.push_back(issue);
        }
    }
    s_show_verify_map = true;
}

void editor_dialogs_frame(void)
{
    draw_new_map_dialog();
    draw_open_map_dialog();
    draw_save_as_dialog();
    draw_overwrite_confirm();
    draw_sidecar_confirm();
    draw_resize_confirm();
    draw_level_settings_dialog();
    draw_playtest_confirm();
    draw_verify_map_dialog();
    draw_verify_map_markers();
    draw_unsaved_confirm();
    draw_dialog_error();
}
