/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_points.cpp
 *     The "Points" tool -- place / select / edit / delete for static
 *     lights, action points and effect generators.
 * @par Purpose:
 *     docs/refactor/editor/05-script-and-level-settings.md §2/§3 and
 *     phase5/06-slices6-8-points-tool.md. All three are "a position plus an
 *     effect radius", so they share one tool, one selection model, one
 *     drag-to-size gesture and one panel.
 * @par Comment:
 *     Mutations are direct calls into kfx_render/kfx_sim
 *     (light_create_light(), actnpoint_create_actnpoint(),
 *     create_effect_generator()), not packets -- the same D2
 *     single-player-local exception the rect-terrain undo, Stamp and Paint
 *     Texture already use. A packet round trip buys nothing here: lights are
 *     render state that was never synced, and every verb would need its own
 *     PckA_* plus a Redo twin for the undo journal (see editor_journal.cpp's
 *     own history with ambient-position verbs).
 *
 *     Editing a light or effect generator recreates it (delete + create with
 *     the new parameters) rather than poking fields: light_delete_light()
 *     already marks the old footprint for a static-light refresh and
 *     light_create_light() marks the new one, which is the only path known to
 *     keep the static light map correct. Property edits are live but are not
 *     journaled (same as the object position edit); placement and deletion
 *     are.
 */
#include "pre_inc.h"
#include "editor_points.h"
#include "editor_overlay.h"
#include "editor_journal.h"
#include "kfx_editor.h"
#include "frontgui_widgets.h"
#include "player_data.h"
#include "kjm_input.h"
#include "engine_redraw.h"
#include "engine_render.h"
#include "local_camera.h"
#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "config_effects.h"
#include "thing_data.h"
#include "thing_list.h"
#include "thing_effects.h"
#include "light_data.h"
#include "actionpt.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "post_inc.h"

/******************************************************************************/
namespace {

    const long kStl = 256;                 // raw map units per subtile
    const float kPickPixels = 16.0f;       // marker pick radius on screen
    const long kClickVsDragDistance = 192; // < 0.75 subtile of mouse travel = a plain click
    const char *const kKindNames[EPK_Count] = {"Light", "Action Point", "Effect Generator"};

    int s_kind = EPK_Light;

    // Defaults applied to newly placed points (also what a plain click, as
    // opposed to a drag-to-size, uses).
    float s_def_light_radius = 5.0f;   // subtiles
    float s_def_light_height = 1.5f;   // subtiles
    float s_def_intensity = 32.0f;
    float s_def_ap_range = 3.0f;       // subtiles
    float s_def_fx_range = 3.0f;       // subtiles
    int s_def_fx_model = 1;

    // Selection. -1 = none.
    int s_sel_kind = -1;
    long s_sel_id = 0;

    // Drag-to-size gesture.
    bool s_dragging = false;
    long s_drag_x = 0, s_drag_y = 0;

    unsigned char s_owned_lights[LIGHTS_COUNT];

    const char *s_status = "";

    long clamp_long(long v, long lo, long hi)
    {
        return (v < lo) ? lo : ((v > hi) ? hi : v);
    }

    // Subtile-centre snap: points read best (and match shipped maps, whose
    // effect generators sit at [n, 128]) when they aren't at an arbitrary
    // sub-subtile offset from wherever the cursor happened to be.
    long snap_to_centre(long raw)
    {
        return (raw / kStl) * kStl + kStl / 2;
    }

    long stl_to_raw(float stl)
    {
        return (long)std::lround(stl * (float)kStl);
    }

    void refresh_owned_lights()
    {
        editor_points_mark_thing_owned_lights(s_owned_lights);
    }

    bool light_is_level_content(long idx)
    {
        if ((idx <= 0) || (idx >= LIGHTS_COUNT))
            return false;
        const struct Light *lgt = &lish.lights[idx];
        if ((lgt->flags & LgtF_Allocated) == 0)
            return false;
        if ((lgt->flags & LgtF_Dynamic) != 0)
            return false;
        return s_owned_lights[idx] == 0;
    }

    int next_free_action_point_number()
    {
        for (int num = 1; num <= ACTN_POINTS_COUNT * 4; num++)
        {
            bool used = false;
            for (ActionPointId i = 1; i < ACTN_POINTS_COUNT; i++)
            {
                const struct ActionPoint *apt = action_point_get(i);
                if (action_point_exists(apt) && (apt->num == num))
                {
                    used = true;
                    break;
                }
            }
            if (!used)
                return num;
        }
        return 0;
    }

    struct ActionPoint *find_action_point(long number)
    {
        for (ActionPointId i = 1; i < ACTN_POINTS_COUNT; i++)
        {
            struct ActionPoint *apt = action_point_get(i);
            if (action_point_exists(apt) && (apt->num == number))
                return apt;
        }
        return NULL;
    }

    // Reads the live point back into a snapshot; false if it no longer exists.
    bool read_point(int kind, long id, EditorPointSnapshot *out)
    {
        std::memset(out, 0, sizeof(*out));
        out->kind = kind;
        out->id = id;
        switch (kind)
        {
            case EPK_Light:
            {
                if (!light_is_level_content(id))
                    return false;
                const struct Light *lgt = &lish.lights[id];
                out->x = lgt->mappos.x.val;
                out->y = lgt->mappos.y.val;
                out->z = lgt->mappos.z.val;
                out->radius = lgt->radius;
                out->intensity = lgt->intensity;
                out->parent = lgt->attached_slb;
                return true;
            }
            case EPK_ActionPoint:
            {
                const struct ActionPoint *apt = find_action_point(id);
                if (apt == NULL)
                    return false;
                out->x = apt->mappos.x.val;
                out->y = apt->mappos.y.val;
                out->radius = apt->range;
                return true;
            }
            case EPK_EffectGen:
            {
                struct Thing *thing = thing_get(id);
                if (!thing_exists(thing) || (thing->class_id != TCls_EffectGen))
                    return false;
                out->x = thing->mappos.x.val;
                out->y = thing->mappos.y.val;
                out->z = thing->mappos.z.val;
                out->radius = thing->effect_generator.range;
                out->model = thing->model;
                out->owner = thing->owner;
                out->parent = thing->parent_idx;
                return true;
            }
            default:
                return false;
        }
    }

    // A snapshot still describes what is at its id (slots get reused).
    bool snapshot_matches_live(const EditorPointSnapshot *snap)
    {
        EditorPointSnapshot live;
        if (!read_point(snap->kind, snap->id, &live))
            return false;
        if ((live.x != snap->x) || (live.y != snap->y))
            return false;
        if ((snap->kind == EPK_EffectGen) && (live.model != snap->model))
            return false;
        return true;
    }

    void select_point(int kind, long id)
    {
        s_sel_kind = kind;
        s_sel_id = id;
    }

    void clear_selection()
    {
        s_sel_kind = -1;
        s_sel_id = 0;
    }

    struct PointRef {
        int kind;
        long id;
        long x, y, z;
    };

    void collect_points(std::vector<PointRef> &out)
    {
        for (long i = 1; i < LIGHTS_COUNT; i++)
        {
            if (!light_is_level_content(i))
                continue;
            const struct Light *lgt = &lish.lights[i];
            out.push_back({EPK_Light, i, lgt->mappos.x.val, lgt->mappos.y.val, lgt->mappos.z.val});
        }
        for (ActionPointId i = 1; i < ACTN_POINTS_COUNT; i++)
        {
            const struct ActionPoint *apt = action_point_get(i);
            if (!action_point_exists(apt))
                continue;
            out.push_back({EPK_ActionPoint, apt->num, apt->mappos.x.val, apt->mappos.y.val, 0});
        }
        for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        {
            struct Thing *thing = thing_get(i);
            if (!thing_exists(thing) || (thing->class_id != TCls_EffectGen))
                continue;
            out.push_back({EPK_EffectGen, thing->index, thing->mappos.x.val, thing->mappos.y.val, thing->mappos.z.val});
        }
    }

    // Nearest marker to the mouse within kPickPixels, in screen space (the
    // markers are drawn by projecting to screen, so picking has to use the
    // same projection or it would disagree with what the user sees).
    bool pick_point(const std::vector<PointRef> &points, PointRef *hit)
    {
        float best = kPickPixels * kPickPixels;
        bool found = false;
        float mx = (float)GetMouseX();
        float my = (float)GetMouseY();
        for (size_t i = 0; i < points.size(); i++)
        {
            long sx, sy;
            if (!project_world_position_to_screen(points[i].x, points[i].y, points[i].z, &sx, &sy))
                continue;
            float dx = (float)sx - mx;
            float dy = (float)sy - my;
            float d2 = dx * dx + dy * dy;
            if (d2 <= best)
            {
                best = d2;
                *hit = points[i];
                found = true;
            }
        }
        return found;
    }

    long default_radius_raw(int kind)
    {
        switch (kind)
        {
            case EPK_Light:       return stl_to_raw(s_def_light_radius);
            case EPK_ActionPoint: return stl_to_raw(s_def_ap_range);
            default:              return stl_to_raw(s_def_fx_range);
        }
    }

    // Kinds and names of the effect generators the loaded config knows.
    // Walks effectgen_desc (NULL-terminated, filled by the config loader) --
    // effects_conf.effectgen_cfgstats_count is never written by the loader,
    // so counting on it left the picker empty and only the default (model 1,
    // lava) placeable.
    void collect_effectgen_models(std::vector<int> &models, std::vector<const char *> &names)
    {
        for (int i = 0; (i < EFFECTSGEN_TYPES_MAX) && (effectgen_desc[i].name != NULL); i++)
        {
            int model = effectgen_desc[i].num;
            const char *name = effectgen_desc[i].name;
            if ((model <= 0) || (name[0] == '\0'))
                continue;
            models.push_back(model);
            names.push_back(name);
        }
    }

    bool create_and_journal(EditorPointSnapshot *snap)
    {
        if (!editor_points_create(snap))
        {
            s_status = (snap->kind == EPK_Light) ? "No free light slot."
                : (snap->kind == EPK_ActionPoint) ? "No free action point slot."
                : "Couldn't create the effect generator (out of thing slots?).";
            return false;
        }
        s_status = "";
        editor_journal_record_point(true, snap);
        select_point(snap->kind, snap->id);
        return true;
    }

    void place_point(long x, long y, long radius)
    {
        EditorPointSnapshot snap;
        std::memset(&snap, 0, sizeof(snap));
        snap.kind = s_kind;
        snap.x = snap_to_centre(x);
        snap.y = snap_to_centre(y);
        snap.radius = radius;
        switch (s_kind)
        {
            case EPK_Light:
                snap.z = stl_to_raw(s_def_light_height);
                snap.intensity = (int)s_def_intensity;
                break;
            case EPK_ActionPoint:
                snap.id = next_free_action_point_number();
                if (snap.id == 0)
                {
                    s_status = "No free action point number.";
                    return;
                }
                break;
            default:
                snap.model = s_def_fx_model;
                snap.owner = PLAYER_NEUTRAL;
                snap.parent = -1;
                break;
        }
        create_and_journal(&snap);
    }

    void delete_selected()
    {
        EditorPointSnapshot snap;
        if ((s_sel_kind < 0) || !read_point(s_sel_kind, s_sel_id, &snap))
        {
            clear_selection();
            return;
        }
        if (editor_points_delete(&snap))
            editor_journal_record_point(false, &snap);
        clear_selection();
    }

    // Applies an edited snapshot over the selected point. Lights/effect
    // generators are recreated (see file comment); action points are edited
    // in place since nothing else refers to their slot.
    void apply_edit(const EditorPointSnapshot &before, EditorPointSnapshot after)
    {
        editor_mark_dirty();
        if (before.kind == EPK_ActionPoint)
        {
            struct ActionPoint *apt = find_action_point(before.id);
            if (apt == NULL)
                return;
            if (after.id != before.id)
            {
                struct ActionPoint *other = find_action_point(after.id);
                if ((after.id <= 0) || (after.id > 65535) || (other != NULL))
                {
                    s_status = "That action point number is already in use (or invalid).";
                    return;
                }
                apt->num = (ActionPointNumber)after.id;
                select_point(EPK_ActionPoint, after.id);
            }
            apt->mappos.x.val = (MapCoord)clamp_long(after.x, 0, 65535);
            apt->mappos.y.val = (MapCoord)clamp_long(after.y, 0, 65535);
            apt->range = (unsigned short)clamp_long(after.radius, 0, 65535);
            s_status = "";
            return;
        }
        // Recreate. Try the new parameters first; if that fails (out of
        // slots) put the original back so the edit never loses a point.
        if (!editor_points_delete(&before))
            return;
        after.id = 0;
        if (!editor_points_create(&after))
        {
            EditorPointSnapshot restore = before;
            restore.id = 0;
            if (editor_points_create(&restore))
                select_point(restore.kind, restore.id);
            else
                clear_selection();
            s_status = "Edit failed; no free slot for the recreated point.";
            return;
        }
        s_status = "";
        select_point(after.kind, after.id);
    }

    void draw_inspector()
    {
        EditorPointSnapshot snap;
        if ((s_sel_kind < 0) || !read_point(s_sel_kind, s_sel_id, &snap))
        {
            FeBodyText("Nothing selected. Click a marker to edit it.");
            return;
        }
        EditorPointSnapshot edited = snap;
        bool changed = false;

        char title[96];
        if (snap.kind == EPK_ActionPoint)
            snprintf(title, sizeof(title), "Action Point %ld", snap.id);
        else if (snap.kind == EPK_EffectGen)
            snprintf(title, sizeof(title), "Effect Generator: %s", effectgenerator_code_name((ThingModel)snap.model));
        else
            snprintf(title, sizeof(title), "Light #%ld", snap.id);
        FeSubheading(title);

        float fx = (float)snap.x / (float)kStl;
        float fy = (float)snap.y / (float)kStl;
        ImGui::SetNextItemWidth(200);
        ImGui::InputFloat("X (subtiles)##PtX", &fx, 0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            edited.x = stl_to_raw(fx);
            changed = true;
        }
        ImGui::SetNextItemWidth(200);
        ImGui::InputFloat("Y (subtiles)##PtY", &fy, 0.0f, 0.0f, "%.2f");
        if (ImGui::IsItemDeactivatedAfterEdit())
        {
            edited.y = stl_to_raw(fy);
            changed = true;
        }

        float radius = (float)snap.radius / (float)kStl;
        const char *radius_label = (snap.kind == EPK_Light) ? "Radius (subtiles)"
            : (snap.kind == EPK_ActionPoint) ? "Range (subtiles)" : "Effect range (subtiles)";
        if (FeSlider(radius_label, &radius, 0.0f, (snap.kind == EPK_Light) ? 40.0f : 20.0f, "%.1f"))
        {
            edited.radius = stl_to_raw(radius);
            changed = true;
        }

        if (snap.kind == EPK_Light)
        {
            float intensity = (float)snap.intensity;
            if (FeSlider("Intensity", &intensity, 1.0f, 255.0f, "%.0f"))
            {
                edited.intensity = (int)intensity;
                changed = true;
            }
            float height = (float)snap.z / (float)kStl;
            if (FeSlider("Height (subtiles)", &height, 0.0f, 8.0f, "%.2f"))
            {
                edited.z = stl_to_raw(height);
                changed = true;
            }
        }
        else if (snap.kind == EPK_ActionPoint)
        {
            int number = (int)snap.id;
            ImGui::SetNextItemWidth(200);
            ImGui::InputInt("Number##PtNum", &number);
            if (ImGui::IsItemDeactivatedAfterEdit())
            {
                edited.id = number;
                changed = true;
            }
            FeCaption("Scripts refer to this number; renumbering does not update them.");
        }
        else
        {
            std::vector<int> models;
            std::vector<const char *> names;
            collect_effectgen_models(models, names);
            int cur = 0;
            for (size_t i = 0; i < models.size(); i++)
                if (models[i] == snap.model)
                    cur = (int)i;
            if (!names.empty() && FeCombo("Kind##PtFxKind", &cur, names.data(), (int)names.size()))
            {
                edited.model = models[cur];
                changed = true;
            }
        }

        if (changed)
            apply_edit(snap, edited);
        if (FeButton("Delete", ImVec2(200, 0)))
            delete_selected();
    }

    void draw_counts()
    {
        int lights = 0, aps = 0, fx = 0;
        for (long i = 1; i < LIGHTS_COUNT; i++)
            if (light_is_level_content(i))
                lights++;
        for (ActionPointId i = 1; i < ACTN_POINTS_COUNT; i++)
            if (action_point_exists(action_point_get(i)))
                aps++;
        for (ThingIndex i = 1; i < THINGS_COUNT; i++)
        {
            struct Thing *thing = thing_get(i);
            if (thing_exists(thing) && (thing->class_id == TCls_EffectGen))
                fx++;
        }
        char line[128];
        snprintf(line, sizeof(line), "Lights: %d   Action points: %d/%d   Effect gens: %d",
            lights, aps, ACTN_POINTS_COUNT - 1, fx);
        FeCaption(line);
        // Same threshold lvl_filesdk1.c warns at when loading a level whose
        // static lights fill half the light slots.
        if (lights >= LIGHTS_COUNT / 2)
            FeBodyText("Warning: over half of the light slots are in use.");
    }

    void draw_selected_highlight()
    {
        EditorPointSnapshot snap;
        if ((s_sel_kind < 0) || !read_point(s_sel_kind, s_sel_id, &snap))
            return;
        editor_overlay_draw_marker(snap.x, snap.y, snap.z, IM_COL32(255, 60, 60, 255), "*");
        editor_overlay_draw_radius_ring(snap.x, snap.y, snap.z, snap.radius, IM_COL32(255, 60, 60, 200));
    }

    // While the tool is active every point must be visible to be pickable,
    // whether or not the matching View-menu toggle is on -- but a kind whose
    // toggle IS on is already drawn by editor_overlay.cpp, so skip it here.
    void draw_tool_markers(const std::vector<PointRef> &points)
    {
        for (size_t i = 0; i < points.size(); i++)
        {
            const PointRef &p = points[i];
            char label[24];
            unsigned int color, ring;
            switch (p.kind)
            {
                case EPK_Light:
                    if (editor_overlay_light_markers_enabled())
                        continue;
                    snprintf(label, sizeof(label), "L");
                    color = IM_COL32(255, 230, 90, 255);
                    ring = IM_COL32(255, 230, 90, 130);
                    break;
                case EPK_ActionPoint:
                    if (editor_overlay_ap_herogate_markers_enabled())
                        continue;
                    snprintf(label, sizeof(label), "AP%ld", p.id);
                    color = IM_COL32(230, 230, 230, 255);
                    ring = IM_COL32(230, 230, 230, 110);
                    break;
                default:
                    if (editor_overlay_thing_markers_enabled())
                        continue;
                    snprintf(label, sizeof(label), "FX");
                    color = IM_COL32(120, 200, 255, 255);
                    ring = IM_COL32(120, 200, 255, 110);
                    break;
            }
            EditorPointSnapshot snap;
            editor_overlay_draw_marker(p.x, p.y, p.z, color, label);
            if (read_point(p.kind, p.id, &snap))
                editor_overlay_draw_radius_ring(p.x, p.y, p.z, snap.radius, ring);
        }
    }

} // namespace

/******************************************************************************/
extern "C" int editor_points_effectgen_kind_count(void)
{
    std::vector<int> models;
    std::vector<const char *> names;
    collect_effectgen_models(models, names);
    return (int)models.size();
}

extern "C" void editor_points_reset(void)
{
    clear_selection();
    s_dragging = false;
    s_status = "";
}

extern "C" void editor_points_mark_thing_owned_lights(unsigned char *owned)
{
    std::memset(owned, 0, LIGHTS_COUNT);
    for (ThingIndex i = 1; i < THINGS_COUNT; i++)
    {
        const struct Thing *thing = thing_get(i);
        if (!thing_exists(thing))
            continue;
        if ((thing->light_id > 0) && (thing->light_id < LIGHTS_COUNT))
            owned[thing->light_id] = 1;
    }
    for (int u = 0; u < MAX_NET_USERS; u++)
    {
        int idx = kfx_sim_state.user_states[u].cursor_light_idx;
        if ((idx > 0) && (idx < LIGHTS_COUNT))
            owned[idx] = 1;
    }
}

extern "C" TbBool editor_points_create(struct EditorPointSnapshot *snap)
{
    editor_mark_dirty();
    switch (snap->kind)
    {
        case EPK_Light:
        {
            struct InitLight ilght;
            std::memset(&ilght, 0, sizeof(ilght));
            ilght.mappos.x.val = (MapCoord)snap->x;
            ilght.mappos.y.val = (MapCoord)snap->y;
            ilght.mappos.z.val = (MapCoord)snap->z;
            ilght.radius = (short)clamp_long(snap->radius, 0, 32767);
            ilght.intensity = (unsigned char)clamp_long(snap->intensity, 1, 255);
            ilght.is_dynamic = 0;
            ilght.attached_slb = (SlabCodedCoords)snap->parent;
            long idx = light_create_light(&ilght);
            if (idx == 0)
                return false;
            snap->id = idx;
            return true;
        }
        case EPK_ActionPoint:
        {
            long number = snap->id;
            if (number <= 0)
                number = next_free_action_point_number();
            if ((number <= 0) || (number > 65535) || (find_action_point(number) != NULL))
                return false;
            struct InitActionPoint iapt;
            std::memset(&iapt, 0, sizeof(iapt));
            iapt.mappos.x.val = (MapCoord)clamp_long(snap->x, 0, 65535);
            iapt.mappos.y.val = (MapCoord)clamp_long(snap->y, 0, 65535);
            iapt.range = (unsigned short)clamp_long(snap->radius, 0, 65535);
            iapt.num = (ActionPointNumber)number;
            struct ActionPoint *apt = actnpoint_create_actnpoint(&iapt);
            if (action_point_is_invalid(apt))
                return false;
            snap->id = number;
            return true;
        }
        case EPK_EffectGen:
        {
            struct Coord3d pos;
            std::memset(&pos, 0, sizeof(pos));
            pos.x.val = (MapCoord)snap->x;
            pos.y.val = (MapCoord)snap->y;
            pos.z.val = (MapCoord)snap->z;
            struct Thing *thing = create_effect_generator(&pos, (ThingModel)snap->model,
                (unsigned short)clamp_long(snap->radius, 0, 32767), (unsigned short)snap->owner, snap->parent);
            if (thing_is_invalid(thing))
                return false;
            snap->id = thing->index;
            return true;
        }
        default:
            return false;
    }
}

extern "C" TbBool editor_points_delete(const struct EditorPointSnapshot *snap)
{
    refresh_owned_lights();
    if (!snapshot_matches_live(snap))
        return false;
    editor_mark_dirty();
    switch (snap->kind)
    {
        case EPK_Light:
            light_delete_light(snap->id);
            return true;
        case EPK_ActionPoint:
        {
            struct ActionPoint *apt = find_action_point(snap->id);
            if (apt == NULL)
                return false;
            delete_action_point_structure(apt);
            return true;
        }
        case EPK_EffectGen:
            delete_thing_structure(thing_get(snap->id), 0);
            return true;
        default:
            return false;
    }
}

extern "C" const char *editor_points_describe(const struct EditorPointSnapshot *snap, TbBool placed)
{
    static char buf[64];
    const char *kind = ((snap->kind >= 0) && (snap->kind < EPK_Count)) ? kKindNames[snap->kind] : "Point";
    snprintf(buf, sizeof(buf), "%s %s", placed ? "Place" : "Delete", kind);
    return buf;
}

extern "C" void editor_points_draw_panel(void)
{
    refresh_owned_lights();
    FeSubheading("Points");
    for (int k = 0; k < EPK_Count; k++)
    {
        if (FeNavButton(kKindNames[k], s_kind == k))
            s_kind = k;
    }

    FeSeparator();
    FeCaption("New points");
    switch (s_kind)
    {
        case EPK_Light:
            FeSlider("Radius (subtiles)##DefLr", &s_def_light_radius, 1.0f, 40.0f, "%.1f");
            FeSlider("Intensity##DefLi", &s_def_intensity, 1.0f, 255.0f, "%.0f");
            FeSlider("Height (subtiles)##DefLh", &s_def_light_height, 0.0f, 8.0f, "%.2f");
            break;
        case EPK_ActionPoint:
            FeSlider("Range (subtiles)##DefAr", &s_def_ap_range, 0.0f, 20.0f, "%.1f");
            break;
        default:
        {
            std::vector<int> models;
            std::vector<const char *> names;
            collect_effectgen_models(models, names);
            int cur = 0;
            for (size_t i = 0; i < models.size(); i++)
                if (models[i] == s_def_fx_model)
                    cur = (int)i;
            if (!names.empty() && FeCombo("Kind##DefFx", &cur, names.data(), (int)names.size()))
                s_def_fx_model = models[cur];
            FeSlider("Effect range (subtiles)##DefFr", &s_def_fx_range, 0.0f, 20.0f, "%.1f");
            break;
        }
    }
    FeCaption("Click ground to place; drag outward to size. Click a marker to select. Del deletes.");
    draw_counts();
    if (s_status[0] != '\0')
        FeBodyText(s_status);

    FeSeparator();
    draw_inspector();
}

extern "C" void editor_points_frame(void)
{
    struct PlayerInfo *player = get_my_player();
    // The overlay projection only holds for the isometric dungeon view (see
    // editor_overlay_frame()); picking would be wrong anywhere else.
    if (player->view_type != PVT_DungeonTop)
        return;
    refresh_owned_lights();

    EditorPointSnapshot probe;
    if ((s_sel_kind >= 0) && !read_point(s_sel_kind, s_sel_id, &probe))
        clear_selection(); // deleted elsewhere (undo, eraser, ...)

    std::vector<PointRef> points;
    collect_points(points);
    draw_tool_markers(points);
    draw_selected_highlight();

    ImGuiIO &io = ImGui::GetIO();
    struct Camera *camera = get_local_active_camera(player);
    struct Coord3d mouse_pos;
    bool have_pos = screen_to_map(camera, GetMouseX(), GetMouseY(), &mouse_pos) != 0;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse)
    {
        PointRef hit;
        if (pick_point(points, &hit))
        {
            select_point(hit.kind, hit.id);
        }
        else if (have_pos)
        {
            s_dragging = true;
            s_drag_x = snap_to_centre(mouse_pos.x.val);
            s_drag_y = snap_to_centre(mouse_pos.y.val);
        }
    }

    if (s_dragging)
    {
        long dist = 0;
        if (have_pos)
        {
            double dx = (double)mouse_pos.x.val - (double)s_drag_x;
            double dy = (double)mouse_pos.y.val - (double)s_drag_y;
            dist = (long)std::sqrt(dx * dx + dy * dy);
        }
        if (dist >= kClickVsDragDistance)
            editor_overlay_draw_radius_ring(s_drag_x, s_drag_y, (s_kind == EPK_Light) ? stl_to_raw(s_def_light_height) : 0,
                dist, IM_COL32(120, 255, 120, 220));
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
        {
            s_dragging = false;
            long radius = (dist >= kClickVsDragDistance) ? dist : default_radius_raw(s_kind);
            place_point(s_drag_x, s_drag_y, radius);
        }
    }

    if (!io.WantCaptureKeyboard && (s_sel_kind >= 0) && ImGui::IsKeyPressed(ImGuiKey_Delete, false))
        delete_selected();
}
