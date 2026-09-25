/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_overlay.cpp
 *     See editor_overlay.h. docs/refactor/editor/04-views-camera-overlays.md
 *     phase 4 slice 4 -- slab grid, cursor coordinate readout, and
 *     ownership tint, all drawn on top of the already-rendered 3D scene via
 *     project_world_position_to_screen() (kfx_render, slice 3's own
 *     projection primitive).
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "editor_overlay.h"

#include "player_data.h" // get_my_player, get_player_active_camera
#include "kjm_input.h" // GetMouseX/GetMouseY
#include "engine_redraw.h" // screen_to_map
#include "engine_render.h" // project_world_position_to_screen
#include "kfx_sim_state.h" // map_tiles_x/y
#include "slab_data.h" // get_slabmap_block/slabmap_owner
#include "thing_data.h" // thing_get/thing_is_invalid
#include "thing_list.h" // THINGS_COUNT
#include "thing_creature.h" // thing_is_creature
#include "thing_objects.h" // thing_is_object/object_is_hero_gate
#include "thing_traps.h" // thing_is_deployed_trap
#include "thing_doors.h" // thing_is_deployed_door
#include "light_data.h" // lish.lights[]/LgtF_Allocated/LgtF_Dynamic
#include "actionpt.h" // action_point_get/ACTN_POINTS_COUNT

#include <imgui.h>
#include <cstdio>
#include <cmath>
#include "post_inc.h"

/******************************************************************************/
namespace {
    bool s_slab_grid_enabled = false;
    bool s_coordinates_enabled = false;
    bool s_ownership_tint_enabled = false;
    bool s_thing_markers_enabled = false;
    bool s_light_markers_enabled = false;
    bool s_ap_herogate_markers_enabled = false;

    // Same "approximate classic DK player colours" table
    // frontmenu_landpreview.c's own land_preview_minimap_colours[] already
    // uses for the minimap thumbnail -- duplicated here rather than
    // exported, since that table is file-local to a minimap-specific file
    // and this is a different, independent consumer (same "two tables, no
    // collision" precedent docs/refactor/editor/10-definable-keybindings.md
    // already established for duplicating a default key across two
    // independent tables).
    ImU32 owner_tint_color(PlayerNumber owner)
    {
        switch (owner)
        {
            case PLAYER0:     return IM_COL32(196, 40, 40, 90);  // red
            case PLAYER1:     return IM_COL32(40, 90, 196, 90);  // blue
            case PLAYER2:     return IM_COL32(60, 170, 70, 90);  // green
            case PLAYER3:     return IM_COL32(206, 190, 40, 90); // yellow
            case PLAYER_GOOD: return IM_COL32(220, 220, 220, 90);// white
            case PLAYER4:     return IM_COL32(150, 60, 190, 90); // purple
            case PLAYER5:     return IM_COL32(30, 30, 30, 90);   // black
            case PLAYER6:     return IM_COL32(220, 130, 30, 90); // orange
            default:          return 0; // PLAYER_NEUTRAL / unowned -- no tint
        }
    }

    // One line per slab boundary, spanning the whole map edge to edge.
    // Skips a boundary line entirely when either endpoint is off-screen or
    // behind the camera (project_world_position_to_screen() returning
    // false) rather than clipping it to the visible portion -- a
    // simplification worth revisiting if live-testing on a large map shows
    // grid lines dropping out near the screen edges more than expected.
    void draw_slab_grid(void)
    {
        int64_t map_w = (int64_t)kfx_sim_state.map_tiles_x * STL_PER_SLB * COORD_PER_STL;
        int64_t map_h = (int64_t)kfx_sim_state.map_tiles_y * STL_PER_SLB * COORD_PER_STL;
        ImDrawList *draw_list = ImGui::GetForegroundDrawList();

        for (int64_t slb_x = 0; slb_x <= kfx_sim_state.map_tiles_x; slb_x++)
        {
            int64_t wx = slb_x * STL_PER_SLB * COORD_PER_STL;
            int64_t sx1, sy1, sx2, sy2;
            if (!project_world_position_to_screen(wx, 0, 0, &sx1, &sy1))
                continue;
            if (!project_world_position_to_screen(wx, map_h, 0, &sx2, &sy2))
                continue;
            bool heavy = (slb_x % 5) == 0;
            ImU32 color = heavy ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 40);
            draw_list->AddLine(ImVec2((double)sx1, (double)sy1), ImVec2((double)sx2, (double)sy2), color, heavy ? 1.5 : 1.0);
        }
        for (int64_t slb_y = 0; slb_y <= kfx_sim_state.map_tiles_y; slb_y++)
        {
            int64_t wy = slb_y * STL_PER_SLB * COORD_PER_STL;
            int64_t sx1, sy1, sx2, sy2;
            if (!project_world_position_to_screen(0, wy, 0, &sx1, &sy1))
                continue;
            if (!project_world_position_to_screen(map_w, wy, 0, &sx2, &sy2))
                continue;
            bool heavy = (slb_y % 5) == 0;
            ImU32 color = heavy ? IM_COL32(255, 255, 255, 90) : IM_COL32(255, 255, 255, 40);
            draw_list->AddLine(ImVec2((double)sx1, (double)sy1), ImVec2((double)sx2, (double)sy2), color, heavy ? 1.5 : 1.0);
        }
    }

    // Cursor's current slab + subtile, drawn as plain text in the bottom-
    // left corner -- same fixed-corner idiom frontgui_ingame_debug.cpp's
    // own overlays already use, reimplemented directly here rather than
    // reusing that file's begin_corner_overlay() helper (kfx_frontend-
    // internal, not part of any public header) to avoid a new cross-file
    // dependency for one small text draw.
    void draw_coordinate_readout(void)
    {
        struct PlayerInfo *player = get_my_player();
        struct Camera *camera = get_player_active_camera(player);
        struct Coord3d pos;
        if (!screen_to_map(camera, GetMouseX(), GetMouseY(), &pos))
            return;

        char text[64];
        snprintf(text, sizeof(text), "Slab (%" PRId64 ", %" PRId64 ")  Subtile (%" PRId64 ", %" PRId64 ")",
            (int64_t)(pos.x.stl.num / STL_PER_SLB), (int64_t)(pos.y.stl.num / STL_PER_SLB),
            (int64_t)pos.x.stl.num, (int64_t)pos.y.stl.num);

        ImGuiIO &io = ImGui::GetIO();
        ImVec2 text_size = ImGui::CalcTextSize(text);
        ImVec2 draw_pos(10.0, io.DisplaySize.y - text_size.y - 10.0);
        ImDrawList *draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddRectFilled(ImVec2(draw_pos.x - 4, draw_pos.y - 2),
            ImVec2(draw_pos.x + text_size.x + 4, draw_pos.y + text_size.y + 2), IM_COL32(0, 0, 0, 140));
        draw_list->AddText(draw_pos, IM_COL32(255, 255, 255, 255), text);
    }

    // Translucent per-slab colour wash by owner, same "read world truth
    // directly" precedent handle_object_placement_click()/the eyedropper
    // tool already established for get_slabmap_block()/slabmap_owner()
    // (kfx_sim -- a lower layer). PLAYER_NEUTRAL/unowned slabs are left
    // untinted (owner_tint_color() returns alpha 0).
    void draw_ownership_tint(void)
    {
        ImDrawList *draw_list = ImGui::GetForegroundDrawList();
        for (int64_t slb_y = 0; slb_y < kfx_sim_state.map_tiles_y; slb_y++)
        {
            for (int64_t slb_x = 0; slb_x < kfx_sim_state.map_tiles_x; slb_x++)
            {
                struct SlabMap *slb = get_slabmap_block(slb_x, slb_y);
                PlayerNumber owner = (PlayerNumber)slabmap_owner(slb);
                ImU32 color = owner_tint_color(owner);
                if (color == 0)
                    continue;

                int64_t wx1 = slb_x * STL_PER_SLB * COORD_PER_STL;
                int64_t wy1 = slb_y * STL_PER_SLB * COORD_PER_STL;
                int64_t wx2 = wx1 + STL_PER_SLB * COORD_PER_STL;
                int64_t wy2 = wy1 + STL_PER_SLB * COORD_PER_STL;
                int64_t sx1, sy1, sx2, sy2, sx3, sy3, sx4, sy4;
                if (!project_world_position_to_screen(wx1, wy1, 0, &sx1, &sy1))
                    continue;
                if (!project_world_position_to_screen(wx2, wy1, 0, &sx2, &sy2))
                    continue;
                if (!project_world_position_to_screen(wx2, wy2, 0, &sx3, &sy3))
                    continue;
                if (!project_world_position_to_screen(wx1, wy2, 0, &sx4, &sy4))
                    continue;
                draw_list->AddQuadFilled(
                    ImVec2((double)sx1, (double)sy1), ImVec2((double)sx2, (double)sy2),
                    ImVec2((double)sx3, (double)sy3), ImVec2((double)sx4, (double)sy4), color);
            }
        }
    }
    // Small dot + text label at a world position, used by every marker
    // overlay below -- same shape slice 3's Verify Map markers already
    // established (filled circle + dark outline + a short text label
    // offset to one side, rather than a sprite/icon atlas this editor has
    // no lookup table for).
    void draw_marker(int64_t wx, int64_t wy, int64_t wz, ImU32 color, const char *label)
    {
        int64_t sx, sy;
        if (!project_world_position_to_screen(wx, wy, wz, &sx, &sy))
            return;
        ImDrawList *draw_list = ImGui::GetForegroundDrawList();
        draw_list->AddCircleFilled(ImVec2((double)sx, (double)sy), 5.0, color);
        draw_list->AddCircle(ImVec2((double)sx, (double)sy), 5.0, IM_COL32(0, 0, 0, 255), 0, 1.0);
        if (label != nullptr)
            draw_list->AddText(ImVec2((double)sx + 7.0, (double)sy - 7.0), IM_COL32(255, 255, 255, 255), label);
    }

    // Approximates a world-space radius as an on-screen circle by
    // projecting the center and one point offset along the world X axis,
    // then using the screen-space distance between them as the on-screen
    // radius. Under the isometric perspective a true 3D radius actually
    // projects to an ellipse, not a circle -- this is a deliberate
    // simplification (a rough "about this big" indicator), not a claim of
    // exact ground-footprint accuracy.
    void draw_radius_ring(int64_t wx, int64_t wy, int64_t wz, int64_t radius, ImU32 color)
    {
        if (radius <= 0)
            return;
        int64_t cx, cy, ex, ey;
        if (!project_world_position_to_screen(wx, wy, wz, &cx, &cy))
            return;
        if (!project_world_position_to_screen(wx + radius, wy, wz, &ex, &ey))
            return;
        double screen_radius = sqrt((double)((ex - cx) * (ex - cx) + (ey - cy) * (ey - cy)));
        if (screen_radius < 1.0)
            return;
        ImGui::GetForegroundDrawList()->AddCircle(ImVec2((double)cx, (double)cy), screen_radius, color, 0, 1.5);
    }

    // Creatures/objects/traps/doors, excluding hero gates (drawn by
    // draw_ap_herogate_markers() instead, alongside action points --
    // matches the design doc's own single "Action point / hero gate
    // markers" bullet). THINGS_COUNT (12288) slots scanned every frame the
    // toggle is on -- same "cheap enough not to matter, not measured here"
    // reasoning as slice 4's ownership tint scanning every slab.
    void draw_thing_markers(void)
    {
        for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        {
            struct Thing *thing = thing_get(i);
            if (thing_is_invalid(thing))
                continue;
            char label[16];
            ImU32 color;
            if (thing_is_creature(thing))
            {
                color = IM_COL32(80, 220, 80, 255);
                snprintf(label, sizeof(label), "C%" PRId64, (int64_t)thing->model);
            }
            else if (thing_is_object(thing))
            {
                if (object_is_hero_gate(thing))
                    continue; // drawn by draw_ap_herogate_markers()
                color = IM_COL32(80, 200, 220, 255);
                snprintf(label, sizeof(label), "O%" PRId64, (int64_t)thing->model);
            }
            else if (thing_is_deployed_trap(thing))
            {
                color = IM_COL32(230, 150, 40, 255);
                snprintf(label, sizeof(label), "T%" PRId64, (int64_t)thing->model);
            }
            else if (thing_is_deployed_door(thing))
            {
                color = IM_COL32(160, 110, 70, 255);
                snprintf(label, sizeof(label), "D%" PRId64, (int64_t)thing->model);
            }
            else if (thing->class_id == TCls_EffectGen)
            {
                // Effect generators are invisible things -- without a marker
                // there is nothing on screen to click, select or delete.
                color = IM_COL32(120, 200, 255, 255);
                snprintf(label, sizeof(label), "FX%" PRId64, (int64_t)thing->model);
                draw_radius_ring(thing->mappos.x.val, thing->mappos.y.val, thing->mappos.z.val,
                    thing->effect_generator.range, IM_COL32(120, 200, 255, 110));
            }
            else
            {
                continue;
            }
            draw_marker(thing->mappos.x.val, thing->mappos.y.val, thing->mappos.z.val, color, label);
        }
    }

    // Static lights only (LgtF_Allocated set, LgtF_Dynamic clear) -- a
    // dynamic light (spell effects, creature-carried) isn't level-designer
    // placed content and would churn every frame preview motion runs.
    void draw_light_markers(void)
    {
        for (int64_t i = 1; i < LIGHTS_COUNT; i++)
        {
            struct Light *light = &lish.lights[i];
            if ((light->flags & LgtF_Allocated) == 0)
                continue;
            if ((light->flags & LgtF_Dynamic) != 0)
                continue;
            draw_marker(light->mappos.x.val, light->mappos.y.val, light->mappos.z.val,
                IM_COL32(255, 230, 90, 255), "L");
            draw_radius_ring(light->mappos.x.val, light->mappos.y.val, light->mappos.z.val,
                light->radius, IM_COL32(255, 230, 90, 130));
        }
    }

    // Action points and hero gates share one toggle (see editor_overlay.h's
    // own comment on why).
    void draw_ap_herogate_markers(void)
    {
        for (ActionPointId apt_idx = 1; apt_idx < ACTN_POINTS_COUNT; apt_idx++)
        {
            struct ActionPoint *apt = action_point_get(apt_idx);
            if (!action_point_exists(apt))
                continue;
            char label[16];
            snprintf(label, sizeof(label), "AP%" PRId64, (int64_t)apt->num);
            draw_marker(apt->mappos.x.val, apt->mappos.y.val, 0, IM_COL32(230, 230, 230, 255), label);
            draw_radius_ring(apt->mappos.x.val, apt->mappos.y.val, 0, apt->range, IM_COL32(230, 230, 230, 110));
        }

        for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        {
            struct Thing *thing = thing_get(i);
            if (thing_is_invalid(thing))
                continue;
            if (!thing_is_object(thing) || !object_is_hero_gate(thing))
                continue;
            char label[16];
            snprintf(label, sizeof(label), "HG%" PRId64, (int64_t)thing->hero_gate.number);
            draw_marker(thing->mappos.x.val, thing->mappos.y.val, thing->mappos.z.val,
                IM_COL32(220, 90, 220, 255), label);
        }
    }
} // namespace

void editor_overlay_frame(void)
{
    // Found live: markers/grid/tint showed up in visually implausible
    // positions while View > Map View was active. project_world_position_
    // to_screen() replicates do_map_who_for_thing()'s exact recipe, which
    // only holds for the normal isometric dungeon view -- map_x_pos/
    // map_y_pos/map_z_pos/camera_matrix are set up for that camera/lens,
    // not for the parchment screen's own unrelated 2D draw path (whose
    // values they'd otherwise be reading stale/meaningless leftovers of).
    // Every overlay in this file assumes the isometric view; skip all of
    // them outside it rather than let each one silently misbehave.
    if (get_my_player()->view_type != PVT_DungeonTop)
        return;

    if (s_ownership_tint_enabled)
        draw_ownership_tint();
    if (s_slab_grid_enabled)
        draw_slab_grid();
    if (s_light_markers_enabled)
        draw_light_markers();
    if (s_thing_markers_enabled)
        draw_thing_markers();
    if (s_ap_herogate_markers_enabled)
        draw_ap_herogate_markers();
    if (s_coordinates_enabled)
        draw_coordinate_readout();
}

void editor_overlay_draw_marker(int64_t wx, int64_t wy, int64_t wz, uint64_t color, const char *label)
{
    draw_marker(wx, wy, wz, (ImU32)color, label);
}

void editor_overlay_draw_radius_ring(int64_t wx, int64_t wy, int64_t wz, int64_t radius, uint64_t color)
{
    draw_radius_ring(wx, wy, wz, radius, (ImU32)color);
}

TbBool editor_overlay_slab_grid_enabled(void) { return s_slab_grid_enabled; }
void editor_overlay_set_slab_grid_enabled(TbBool enabled) { s_slab_grid_enabled = enabled; }
TbBool editor_overlay_coordinates_enabled(void) { return s_coordinates_enabled; }
void editor_overlay_set_coordinates_enabled(TbBool enabled) { s_coordinates_enabled = enabled; }
TbBool editor_overlay_ownership_tint_enabled(void) { return s_ownership_tint_enabled; }
void editor_overlay_set_ownership_tint_enabled(TbBool enabled) { s_ownership_tint_enabled = enabled; }
TbBool editor_overlay_thing_markers_enabled(void) { return s_thing_markers_enabled; }
void editor_overlay_set_thing_markers_enabled(TbBool enabled) { s_thing_markers_enabled = enabled; }
TbBool editor_overlay_light_markers_enabled(void) { return s_light_markers_enabled; }
void editor_overlay_set_light_markers_enabled(TbBool enabled) { s_light_markers_enabled = enabled; }
TbBool editor_overlay_ap_herogate_markers_enabled(void) { return s_ap_herogate_markers_enabled; }
void editor_overlay_set_ap_herogate_markers_enabled(TbBool enabled) { s_ap_herogate_markers_enabled = enabled; }
/******************************************************************************/
