/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file map_content.h
 *     A plain-data snapshot of a map's content, decoupled from the live
 *     kfx_sim_state/kfx_render global arrays.
 * @par Purpose:
 *     docs/refactor/editor/phase3/00-slice1-native-save.md -- the middle
 *     layer between "live engine state" and "a file on disk". Field names
 *     and types mirror the KFX-native TOML schema exactly (read directly
 *     from thing_create_thing_adv()/light_create_light_adv()/
 *     actnpoint_create_actnpoint_adv(), not just the design doc's summary
 *     of them), so mapping to/from that format is mechanical in both
 *     directions. This is more than a test fixture: MapContent plus a
 *     writer/reader pair (map_content_writer.h/map_content_reader.h) is
 *     the structured, engine-decoupled representation of a map that a
 *     future non-editor consumer (the project's longer-term goal of a
 *     format an LLM could read or produce) would also want -- kept
 *     deliberately free of any kfx_sim_state/kfx_render pointers or
 *     indices so it can be built, inspected, and compared without a live
 *     game session.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 */
/******************************************************************************/
#ifndef DK_MAP_CONTENT_H
#define DK_MAP_CONTENT_H

#include "bflib_basics.h"
#include "globals.h"

#include <string>
#include <vector>

// A position field in the KFX-native TOML schema is a [whole_subtile,
// sub_subtile] pair (value_read_stl_coord(), src/kfx_config/include/
// value_util.h), not a bare integer -- MapContent keeps the same raw,
// already-combined MapCoord (COORD_PER_STL=256 per subtile) every other
// engine position field uses (struct Coord3d's own .val fields), so a
// writer/reader only needs to split/join it at the TOML boundary, not
// carry two representations through the rest of the pipeline.

// docs/refactor/editor/07-investigation-findings.md F3, cross-checked
// directly against thing_create_thing_adv() (src/kfx_sim/src/thing_factory.c).
// One record covers every thing class this schema supports; fields that
// don't apply to a given thing_class are left at their zero-value default
// (mirrors the loader's own "absent VALUE -> field untouched" leniency --
// e.g. CreatureLevel/DoorOrientation/DoorLocked all quietly default when
// missing, not an error).
struct MapThingRecord
{
    ThingClass thing_class = 0;    // ThingType
    ThingModel model = 0;          // Subtype / SubtypeStringID
    PlayerNumber owner = 0;        // Ownership
    MapCoord pos_x = 0, pos_y = 0, pos_z = 0; // SubtileX/Y/Z

    long parent_tile = -1;         // ParentTile (object/trap/effectgen); -1 = none
    long orientation = 0;          // Orientation (object/creature/trap)

    // Creature-only.
    int creature_level = 0;        // CreatureLevel, 1-10 in the file, stored 0-based
    long creature_gold = 0;        // CreatureGold
    int creature_health_percent = 0; // CreatureInitialHealth (% of max health)
    std::string creature_name;     // CreatureName

    // Object-only, mutually exclusive by the object's own model.
    long gold_value = 0;           // GoldValue (gold pile objects)
    long custom_box_kind = 0;      // CustomBox (custom special-box objects)
    long herogate_number = 0;      // HerogateNumber (hero gate objects)

    // Effect-generator-only.
    MapCoord effect_range = 0;     // EffectRange

    // Door-only.
    long door_orientation = 0;     // DoorOrientation
    TbBool door_locked = false;    // DoorLocked
};

// F3, cross-checked against light_create_light_adv() (src/kfx_render/src/
// light_data.c). Flags beyond Dynamic are unimplemented in the loader
// itself (a literal "TODO: not implemented yet" there) -- MapContent
// carries only what the format actually round-trips today.
struct MapLightRecord
{
    TbBool is_dynamic = false;     // Dynamic
    MapCoord pos_x = 0, pos_y = 0, pos_z = 0; // SubtileX/Y/Z
    MapCoord range = 0;            // LightRange
    unsigned long intensity = 0;   // LightIntensity
    unsigned long parent_tile = 0; // ParentTile
};

// F3, cross-checked against actnpoint_create_actnpoint_adv() (src/kfx_sim/
// src/actionpt.c). Action points only -- no Z, no owner (player-
// independent); hero gates are MapThingRecord objects, not this.
struct MapActionPointRecord
{
    long point_number = 0;         // PointNumber
    MapCoord pos_x = 0, pos_y = 0; // SubtileX/Y
    MapCoord range = 0;            // PointRange
};

// .lof metadata (docs/refactor/editor/07-investigation-findings.md F4) --
// what editor_save_map() needs to write so the map is auto-discovered
// (find_and_load_lof_files()) with no levels.txt step.
struct MapLevelInfo
{
    std::string name_text;
    int players = 1;
    bool is_multiplayer = false;   // KIND = SINGLE vs MULTI
    // docs/refactor/editor/05-script-and-level-settings.md -- DESCRIPTION
    // was already a recognized .lof keyword and an existing
    // LevelInformation::description field (config_campaigns.h), just never
    // wired up on either the read or write side before this. AUTHOR has no
    // equivalent LevelInformation field yet -- deferred, not added here.
    std::string description_text;
};

// The full snapshot: everything editor_save_map()'s KFX-native path
// writes. Slab kind/ownership are flat, row-major (sy outer, sx inner)
// grids sized map_tiles_x * map_tiles_y -- same order packets_cheats.c's
// own editor_snapshot_slab_rect()/journal replay already use.
struct MapContent
{
    long map_tiles_x = 0;
    long map_tiles_y = 0;
    std::vector<SlabKind> slab_kind;      // size map_tiles_x * map_tiles_y
    std::vector<PlayerNumber> slab_owner; // size map_tiles_x * map_tiles_y
    long texture_id = 0;                  // .inf
    // Per-slab base-texture-set override (.slx, "ExtSlab" -- kfx_config_
    // state.slab_ext_data's own on-disk form, load_ext_slabs()/lvl_filesdk1.c).
    // 0 means "no override, use texture_id's own set" for that slab -- same
    // convention load_ext_slabs() itself uses (missing/short file -> all
    // zero). Sized map_tiles_x * map_tiles_y like slab_kind/slab_owner above.
    std::vector<unsigned char> slab_texture;

    std::vector<MapThingRecord> things;
    std::vector<MapLightRecord> lights;
    std::vector<MapActionPointRecord> action_points;
    MapLevelInfo level_info;

    // docs/refactor/editor/05-script-and-level-settings.md §0 -- the map's
    // own map%05lu.txt script, kept as an opaque verbatim blob (this format
    // has no structured per-command schema the way things/lights/APs do,
    // and phase 5's editor doesn't need one yet -- it edits raw text). Empty
    // means "no script text known" -- a genuinely new map, or a level whose
    // .txt wasn't read for some reason -- and is what makes
    // MapContentWriter::write_script() fall back to its own empty-script
    // stub rather than truncating a real script to nothing.
    std::string script_text;

    long slab_index(long x, long y) const { return y * map_tiles_x + x; }
};

#endif
