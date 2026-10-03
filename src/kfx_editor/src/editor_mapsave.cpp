/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file editor_mapsave.cpp
 *     editor_save_map() -- see its own declaration (kfx_editor.h) and
 *     docs/refactor/editor/phase3/00-slice1-native-save.md.
 * @par Purpose:
 *     Thin orchestration only: this file's whole job is to snapshot live
 *     engine state into a MapContent (src/kfx_sim/include/map_content.h)
 *     and hand it to a MapContentWriter. All the real format knowledge
 *     lives in kfx_sim (map_content_writer.h/.cpp), not here -- this file
 *     was written when gathering that snapshot needed to reach into both
 *     kfx_sim_state (slabs/things/action points) *and* kfx_render's lish
 *     (lights), which kfx_sim couldn't. Since refactor pass 2 (S11) the
 *     lights are in kfx_sim_state.light_registry too, so the gathering
 *     could now move down into kfx_sim.
 * @par Comment:
 *     None.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "kfx_editor.h"
#include "editor_map_snapshot.h"
#include "editor_resize.h"
#include "frontend.h" // relaunch onto the scratch level

#include "map_content.h"
#include "map_content_writer.h"
#include "map_content_compat.h"

#include "kfx_sim_state.h"
#include "kfx_config_state.h"
#include "thing_data.h"
#include "thing_list.h"
#include "thing_objects.h"
#include "creature_control.h"
#include "actionpt.h"
#include "slab_data.h"
#include "map_data.h"
#include "map_columns.h"
#include "light_registry.h"
#include "editor_points.h"
#include "lvl_filesdk1.h"
#include "editor_kfx_compat.h"

#include <cstdlib>
#include <cstring>
#include "post_inc.h"

namespace {

// object_is_gold_pile()/object_is_hero_gate()/thing_is_custom_special_box()
// (thing_objects.h) all key off the object's model against config-driven
// object-class flags, exactly like thing_create_thing_adv()'s own read
// side does -- reused here rather than re-deriving "is this object special"
// from scratch.
void snapshot_thing(const struct Thing *thing, MapThingRecord &t)
{
    t.thing_class = thing->class_id;
    t.model = thing->model;
    t.owner = thing->owner;
    t.pos_x = thing->mappos.x.val;
    t.pos_y = thing->mappos.y.val;
    t.pos_z = thing->mappos.z.val;
    // ParentTile (slab-attachment index) isn't tracked back out of a live
    // Thing in this slice -- create_thing()'s own parent_idx handling
    // varies per class and several classes (e.g. doors) overwrite it with
    // the thing's own index for unrelated bookkeeping, so there's no
    // single reliable readback path yet. Left at MapThingRecord's own
    // "-1 = none" default; a real gap for a *loaded* object/trap/
    // effectgen's attachment surviving a save/reload round-trip, worth
    // revisiting in a later slice, not blocking this one (nothing in this
    // slice's own ftest coverage depends on it).

    switch (thing->class_id)
    {
        case TCls_Creature:
        {
            t.orientation = thing->move_angle_xy;
            const struct CreatureControl *cctrl = creature_control_get_from_thing(thing);
            t.creature_level = cctrl->exp_level;
            t.creature_gold = thing->creature.gold_carried;
            t.creature_health_percent = (cctrl->max_health > 0)
                ? (int64_t)((int64_t)thing->health * 100 / cctrl->max_health) : 0;
            if (cctrl->creature_name[0] != '\0')
                t.creature_name = cctrl->creature_name;
            break;
        }
        case TCls_Object:
            t.orientation = thing->move_angle_xy;
            if (object_is_hero_gate(thing))
                t.herogate_number = thing->hero_gate.number;
            else if (thing_is_custom_special_box(thing))
                t.custom_box_kind = thing->custom_box.box_kind;
            else if (object_is_gold_pile(thing))
                t.gold_value = thing->valuable.gold_stored;
            break;
        case TCls_Trap:
            t.orientation = thing->move_angle_xy;
            break;
        case TCls_EffectGen:
            // Found while building the Points tool: neither field was
            // captured, so every saved effect generator came back with range
            // 0 and no parent slab.
            t.effect_range = thing->effect_generator.range;
            t.parent_tile = thing->parent_idx;
            break;
        case TCls_Door:
            t.door_orientation = thing->door.orientation;
            t.door_locked = thing->door.is_locked != 0;
            break;
        default:
            // Every other class (shots, effects, ambient sounds, ...)
            // isn't part of this schema (F3) -- captured with only the
            // base fields above, same leniency the loader itself has for
            // an unset field.
            break;
    }
}

// Found live: an unfiltered sweep of every existing thing also picks up
// transient, runtime-only classes (TCls_Shot, TCls_EffectElem,
// TCls_AmbientSnd, ...) that thing_create_thing_adv() (thing_factory.c)
// has no case for at all -- its switch only handles Object/Creature/
// EffectGen/Trap/Door(+the two unused legacy slots). Writing one of those
// out made the reload's own TOML parse legitimately reject it ("Invalid
// class N, thing discarded") for *every* such thing, failing the whole
// load. Only the classes the schema actually supports belong in a saved
// map at all -- everything else is simulation-generated, not level
// design, and shouldn't be persisted regardless.
TbBool thing_class_is_saveable(ThingClass class_id)
{
    switch (class_id)
    {
        case TCls_Object:
        case TCls_Creature:
        case TCls_EffectGen:
        case TCls_Trap:
        case TCls_Door:
            return true;
        default:
            return false;
    }
}

void snapshot_things(MapContent &content)
{
    for (int64_t i = 0; i < THINGS_COUNT; i++)
    {
        struct Thing *thing = thing_get(i);
        if (!thing_exists(thing))
            continue;
        if (!thing_class_is_saveable(thing->class_id))
            continue;
        MapThingRecord t;
        snapshot_thing(thing, t);
        content.things.push_back(t);
    }
}

void snapshot_lights(MapContent &content)
{
    // A light a thing (a torch, a creature's glow, a spell effect) or a
    // player's cursor owns is recreated by that owner on load; persisting it
    // as a level light too would duplicate it on every save/reload cycle.
    unsigned char owned[LIGHTS_COUNT];
    editor_points_mark_thing_owned_lights(owned);
    for (int64_t i = 1; i < LIGHTS_COUNT; i++)
    {
        const struct Light *lgt = &kfx_sim_state.light_registry.lights[i];
        if ((lgt->flags & LgtF_Allocated) == 0)
            continue;
        if (owned[i])
            continue;
        MapLightRecord l;
        l.is_dynamic = (lgt->flags & LgtF_Dynamic) != 0;
        l.pos_x = lgt->mappos.x.val;
        l.pos_y = lgt->mappos.y.val;
        l.pos_z = lgt->mappos.z.val;
        l.range = lgt->radius;
        l.intensity = lgt->intensity;
        l.parent_tile = (uint64_t)lgt->attached_slb;
        content.lights.push_back(l);
    }
}

void snapshot_action_points(MapContent &content)
{
    for (int64_t i = 0; i < ACTN_POINTS_COUNT; i++)
    {
        const struct ActionPoint *apt = &kfx_sim_state.action_points[i];
        if (!apt->exists)
            continue;
        MapActionPointRecord a;
        a.point_number = apt->num;
        a.pos_x = apt->mappos.x.val;
        a.pos_y = apt->mappos.y.val;
        a.range = apt->range;
        content.action_points.push_back(a);
    }
}

void snapshot_level_info(MapContent &content, const char *level_name, int64_t level_players, TbBool level_is_multiplayer,
    const char *level_description)
{
    if (level_name != nullptr)
        content.level_info.name_text = level_name;
    content.level_info.players = level_players;
    content.level_info.is_multiplayer = level_is_multiplayer != 0;
    // docs/refactor/editor/05-script-and-level-settings.md -- same "NULL/
    // empty leaves it unset" convention level_name already established.
    if (level_description != nullptr)
        content.level_info.description_text = level_description;
    content.level_info.author_text = editor_current_level_author();
}

// The classic file set also carries the column table, the per-subtile column
// index and the wibble map (see MapContent::derived_*): serialised here in the
// exact layout load_column_file() / load_map_data_file() / load_map_wibble_file()
// read (lvl_filesdk1.c). The column table is written up to the highest column
// any subtile (or the engine's "unrevealed" column) refers to.
void snapshot_derived_files(MapContent &content)
{
    const int64_t stl_x = kfx_sim_state.map_subtiles_x + 1;
    const int64_t stl_y = kfx_sim_state.map_subtiles_y + 1;
    content.derived_dat.assign((size_t)(2 * stl_x * stl_y), 0);
    content.derived_wib.assign((size_t)(stl_x * stl_y), 0);
    int64_t highest = labs((int64_t)kfx_sim_state.unrevealed_column_idx);
    size_t d = 0;
    size_t w = 0;
    for (int64_t y = 0; y < stl_y; y++)
    {
        for (int64_t x = 0; x < stl_x; x++)
        {
            const struct Map *mapblk = get_map_block_at(x, y);
            // In memory col_idx is the column's index into columns_data[]; the
            // file stores it negated as a 16-bit word (load_map_data_file()).
            const int64_t col = (int64_t)mapblk->col_idx;
            if (col > highest)
                highest = col;
            const int64_t packed = (int64_t)(-col);
            content.derived_dat[d++] = (unsigned char)(packed & 0xFF);
            content.derived_dat[d++] = (unsigned char)(packed >> 8);
            content.derived_wib[w++] = (unsigned char)get_mapblk_wibble_value(mapblk);
        }
    }
    if (highest < 0)
        highest = 0;
    if (highest >= COLUMNS_COUNT)
        highest = COLUMNS_COUNT - 1;
    // Shipped files always carry 2048 columns; the header count is that too.
    const uint64_t count = (highest < 2047) ? 2048U : (uint64_t)highest + 1;
    content.derived_clm.assign(8 + count * sizeof(struct LegacyColumn), 0);
    content.derived_clm[0] = (unsigned char)(count & 0xFF);
    content.derived_clm[1] = (unsigned char)((count >> 8) & 0xFF);
    content.derived_clm[2] = (unsigned char)((count >> 16) & 0xFF);
    content.derived_clm[3] = (unsigned char)((count >> 24) & 0xFF);
    for (uint64_t k = 0; k < count; k++)
    {
        struct LegacyColumn lcol;
        column_to_legacy(&lcol, &kfx_sim_state.columns_data[k]);
        std::memcpy(&content.derived_clm[8 + k * sizeof(lcol)], &lcol, sizeof(lcol));
    }
}

void snapshot_map(MapContent &content, LevelNumber lvnum, const char *level_name, int64_t level_players, TbBool level_is_multiplayer,
    const char *level_description)
{
    content.map_tiles_x = kfx_sim_state.map_tiles_x;
    content.map_tiles_y = kfx_sim_state.map_tiles_y;
    content.slab_kind.assign((size_t)(content.map_tiles_x * content.map_tiles_y), SlbT_ROCK);
    content.slab_owner.assign((size_t)(content.map_tiles_x * content.map_tiles_y), 0);
    content.slab_texture.assign((size_t)(content.map_tiles_x * content.map_tiles_y), 0);
    for (int64_t y = 0; y < content.map_tiles_y; y++)
    {
        for (int64_t x = 0; x < content.map_tiles_x; x++)
        {
            const struct SlabMap *slb = get_slabmap_block(x, y);
            content.slab_kind[content.slab_index(x, y)] = slb->kind;
            content.slab_owner[content.slab_index(x, y)] = (PlayerNumber)slabmap_owner(slb);
            // docs/refactor/editor/05-script-and-level-settings.md's
            // "per-slab texture paint" item -- get_slab_number()'s own
            // index expression (y*map_tiles_x+x) is identical to slab_
            // index() above, so this is a direct copy, same shape as
            // slab_kind/slab_owner right above it.
            content.slab_texture[content.slab_index(x, y)] = kfx_config_state.slab_ext_data[get_slab_number(x, y)];
        }
    }
    content.texture_id = kfx_config_state.texture_id;
    snapshot_derived_files(content);

    snapshot_things(content);
    snapshot_lights(content);
    snapshot_action_points(content);
    snapshot_level_info(content, level_name, level_players, level_is_multiplayer, level_description);
    // docs/refactor/editor/05-script-and-level-settings.md §0 -- the script
    // text read once in editor_open(), carried unchanged through the
    // session; MapContentWriter::write_script() writes this verbatim
    // instead of overwriting a real script with the empty stub.
    content.script_text = editor_current_level_script_text();
    content.lua_text = editor_current_level_lua_text();
    content.has_lua = editor_current_level_has_lua();
    (void)lvnum;
}

} // namespace

TbBool editor_save_map(LevelNumber lvnum, const char *dir, enum EditorSaveFormat format,
    const char *level_name, int64_t level_players, TbBool level_is_multiplayer, const char *level_description)
{
    // A running motion preview has moved things off where the mapmaker put them.
    if (editor_preview_motion())
        editor_set_preview_motion(false);
    if (editor_preview_restore_pending())
        return false; // still in 1st Person: leave it first, so the map is saved as placed
    MapContent content;
    snapshot_map(content, lvnum, level_name, level_players, level_is_multiplayer, level_description);

    // Force KeeperFX promises a map that loads in the KeeperFX release this game
    // is compatible with (editor_kfx_compat.h): refuse, writing nothing, if the
    // map uses anything only this game has.
    editor_set_last_save_compat_problems({});
    if (format == EdSaveFmt_ForceKeeperFX)
    {
        const std::vector<std::string> problems = editor_kfx_compat_problems(content);
        if (!problems.empty())
        {
            editor_set_last_save_compat_problems(problems);
            WARNLOG("Not saved: %" PRId64 " thing(s) in this map don't load in %s", (int64_t)problems.size(), editor_kfx_compat_reference_label());
            return false;
        }
    }

    // docs/refactor/editor/phase3/01-slice2-classic-save.md -- Auto picks
    // classic only when nothing would be silently lost by doing so;
    // Force* bypasses that check entirely (see this function's own
    // declaration, kfx_editor.h).
    bool use_classic = (format == EdSaveFmt_ForceClassic)
        || ((format == EdSaveFmt_Auto) && map_is_legacy_compatible(content));

    bool write_ok;
    if (use_classic)
    {
        ClassicMapContentWriter writer;
        write_ok = writer.write(content, dir, lvnum);
    }
    else
    {
        KfxNativeMapContentWriter writer;
        write_ok = writer.write(content, dir, lvnum);
    }
    if (!write_ok)
        return false;

    // docs/refactor/editor/07-investigation-findings.md F13 -- .lof
    // auto-discovery only runs at campaign load, not on demand, so a
    // freshly saved map needs an explicit re-scan to show up in Free Play
    // without a full campaign reload. Direct call, not via
    // SimPort: every existing downward reach from
    // kfx_editor into kfx_sim in this codebase is a plain #include+call
    // (kfx_sim ranks below kfx_editor), not a callback-struct indirection
    // -- that pattern exists only for the reverse direction (a lower
    // library needing to call up), which doesn't apply here.
    find_and_load_lof_files();
    find_and_load_lif_files();

    return true;
}

TbBool editor_save_level_info(LevelNumber lvnum, const char *dir,
    const char *level_name, int64_t level_players, TbBool level_is_multiplayer, const char *level_description)
{
    MapContent content;
    content.map_tiles_x = kfx_sim_state.map_tiles_x; // the .lof carries MAPSIZE
    content.map_tiles_y = kfx_sim_state.map_tiles_y;
    snapshot_level_info(content, level_name, level_players, level_is_multiplayer, level_description);

    // Any concrete writer works -- write_level_info_only() is
    // format-independent (see map_content_writer.h's own comment).
    KfxNativeMapContentWriter writer;
    if (!writer.write_level_info_only(content, dir, lvnum))
        return false;

    // Same re-scan reasoning as editor_save_map()'s own call -- KIND
    // (re-)registers the level into campaign.single_levels/multi_levels.
    find_and_load_lof_files();
    find_and_load_lif_files();

    return true;
}

void editor_snapshot_current_map(MapContent &content)
{
    snapshot_map(content, editor_current_lvnum(), editor_current_level_name(),
        editor_current_level_players(), editor_current_level_is_multiplayer(),
        editor_current_level_description());
}

extern "C" TbBool editor_resize_preview(int64_t new_w, int64_t new_h, TbBool centered, int64_t *dropped_things, int64_t *dropped_lights, int64_t *dropped_points)
{
    MapContent content;
    editor_snapshot_current_map(content);
    EditorResizeReport rep;
    if (!editor_resize_content(content, new_w, new_h, centered != 0, kfx_config_state.neutral_player_num, &rep))
        return false;
    if (dropped_things != NULL) *dropped_things = rep.things_dropped;
    if (dropped_lights != NULL) *dropped_lights = rep.lights_dropped;
    if (dropped_points != NULL) *dropped_points = rep.action_points_dropped;
    return true;
}

extern "C" TbBool editor_resize_map(int64_t new_w, int64_t new_h, TbBool centered)
{
    if (editor_preview_motion())
        editor_set_preview_motion(false);
    if (editor_preview_restore_pending())
        return false;
    MapContent content;
    editor_snapshot_current_map(content);
    if (!editor_resize_content(content, new_w, new_h, centered != 0, kfx_config_state.neutral_player_num, NULL))
        return false;
    char dir[512];
    editor_level_save_dir(EDITOR_PLAYTEST_LEVEL_NUMBER, dir, sizeof(dir));
    KfxNativeMapContentWriter writer;
    if (!writer.write(content, dir, EDITOR_PLAYTEST_LEVEL_NUMBER))
        return false;
    find_and_load_lof_files();
    // The resized copy opens as this same level, unsaved (as after a playtest).
    editor_playtest_begin();
    frontend_request_editor_relaunch(EDITOR_PLAYTEST_LEVEL_NUMBER, false, 0, 0, 0);
    editor_close();
    return true;
}
