/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config.h
 *     Header file for config.c.
 * @par Purpose:
 *     Configuration and campaign files support.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     30 Jan 2009 - 11 Feb 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/

#ifndef DK_CONFIG_H
#define DK_CONFIG_H

#include "bflib_basics.h"
#include "compiler_compat.h"
#include "globals.h"
#include "init_thing.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
typedef struct VALUE VALUE;
struct CreditsItem;
struct GameCampaign;
/******************************************************************************/
#define SINGLEPLAYER_FINISHED        -1
#define SINGLEPLAYER_NOTSTARTED       0
#define LEVELNUMBER_ERROR            -2

#define MIN_CONFIG_FILE_SIZE          4

#define LANDVIEW_MAP_WIDTH         1280
#define LANDVIEW_MAP_HEIGHT         960

// enum TbFileGroups moved to kfx_platform's globals.h (stage 13.3,
// docs/refactor/stage-13-enforce-and-document.md) -- a pure ID
// vocabulary enum with zero functional coupling, already used by every
// library including kfx_platform itself, same shape as EventKinds/
// GameKeys/ShotFireLogics moved there earlier this session.

enum TbExtraLevels {
    ExLv_None      =  0,
    ExLv_FullMoon  =  1,
    ExLv_NewMoon   =  2,
};

enum TbLevelKinds {
    LvKind_None      =  0x00,
    LvKind_IsSingle  =  0x01,
    LvKind_IsMulti   =  0x02,
    LvKind_IsBonus   =  0x04,
    LvKind_IsExtra   =  0x08,
    LvKind_IsFree    =  0x10,
};

enum Ensigns {
    EnsNone         = 0,
    EnsTutorial     = 2,
    EnsFullFlag     = 10,
    EnsBonus        = 18,
    EnsFullMoon     = 26,
    EnsNewMoon      = 37,
    EnsDisTutorial  = 35,
    EnsDisFull      = 36,
    EnsDisMoonF     = 34,
    EnsDisMoonN     = 45,
    EnsDisMulti2    = 46,
    EnsDisMulti3    = 47,
    EnsDisMulti4    = 48,
    EnsCoop         = 49,
};

// Custom (zip-loaded) ensign sprites start right past the built-in enum
// Ensigns values above; kfx_render/custom_sprites.h owns the sprite-sheet
// lookup, but this offset is also needed by kfx_config (config_campaigns.c)
// and kfx_game (lvl_script_commands.c's SET_LEVEL_ENSIGN), both ranked
// below kfx_render, so it lives here instead.
#define CUSTOM_ENSIGN_BASE 50

enum TbLevelState {
    LvSt_Hidden    =  0,
    LvSt_HalfShow  =  1,
    LvSt_Visible   =  2,
};

enum TbLevelLocation {
    LvLc_VarLevels =  0,
    LvLc_Campaign  =  1,
    LvLc_Custom    =  2,
};



enum TbConfigLoadFlags {
    CnfLd_Standard      =  0x00, /**< Standard load, no special behavior. */
    CnfLd_ListOnly      =  0x01, /**< Load only list of items and their names, don't parse actual options (when applicable). */
    CnfLd_AcceptPartial =  0x02, /**< Accept partial files (with only some options set), and don't clear previous configuration. */
    CnfLd_IgnoreErrors  =  0x04, /**< Do not log error message on failures (still, return with error). */
    CnfLd_PreListed     =  0x08, /**< Already parsed the names. */
};

#pragma pack(1)


/******************************************************************************/

enum confCommandResults
{
    ccr_comment = 0,
    ccr_ok = 1,
    ccr_endOfFile = -1,
    ccr_unrecognised = -2,
    ccr_endOfBlock = -3,
    ccr_error = -4,
};

enum confChangeFlags
{
    ccf_None           = 0x00,
    ccf_DuringLevel    = 0x01,
    ccf_SplitExecution = 0x02,
};

enum dataTypes
{
    dt_default,
    dt_uchar,
    dt_schar,
    dt_char,
    dt_short,
    dt_ushort,
    dt_int,
    dt_uint,
    dt_long,
    dt_ulong,
    dt_longlong,
    dt_ulonglong,
    dt_float,
    dt_double,
    dt_longdouble,
    dt_void,
    dt_charptr,
};

#define var_type(expr)\
    (_Generic((expr),\
              unsigned char: dt_uchar, \
              signed char: dt_schar, \
              short: dt_short, unsigned short: dt_ushort, \
              int: dt_int, unsigned int: dt_uint, \
              long: dt_long, unsigned long: dt_ulong, \
              long long: dt_longlong, unsigned long long: dt_ulonglong, \
              float: dt_float, \
              double: dt_double, \
              long double: dt_longdouble, \
              void*: dt_void, \
              char*: dt_charptr, \
              default: _Generic((expr), \
                    char: dt_char, \
                    default: dt_default)))

// field_t: portable, works on all C99+ compilers including MSVC.
// Takes the struct type name explicitly — produces a compile-time constant offset.
// Use for simple (non-array-subscript) member paths.
#include <stddef.h>
#define field_t(type_name, member_path) \
    (void*)(ptrdiff_t)offsetof(type_name, member_path), \
    var_type(((type_name*)0)->member_path)

// field_a: like field_t but for array-element member paths array[idx].
// offsetof(T, arr) + idx*sizeof(element) is compile-time constant on all compilers,
// whereas offsetof(T, arr[n]) is a GCC extension rejected by MSVC.
#define field_a(type_name, array_member, idx) \
    (void*)(ptrdiff_t)(offsetof(type_name, array_member) + (idx) * sizeof(((type_name*)0)->array_member[0])), \
    var_type(((type_name*)0)->array_member[idx])

// field: GCC/Clang-only convenience alias that infers the type from an expression
// using the typeof extension. Do not use in new code — prefer field_t()/field_a().
#ifndef _MSC_VER
#define field(elem0_expr, member_path) \
    field_t(typeof(elem0_expr), member_path)
#endif

/******************************************************************************/
struct CommandWord {
    char text[COMMAND_WORD_LEN];
};

// struct NamedCommand and get_rid() moved to kfx_platform's
// bflib_basics.h/.c (included above) -- see docs/refactor/todo/
// check-layering-symbol-level-blind-spot.md.

struct LongNamedCommand {
    const char* name;
    long long num;
};

struct NamedFieldSet;

struct NamedField {
    const char *name;
    char argnum; //for fields that assign multiple values, -1 passes full string to assign function
    void* field;
    uchar type;
    int64_t default_value;
    int64_t min;
    int64_t max;
    const struct NamedCommand *namedCommand;
    int64_t (*parse_func)(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags); // converts the text to the a number
    void (*assign_func)(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
};

struct NamedFieldSet {
    int64_t* (*get_count)(void);
    const char* block_basename;
    const struct NamedField* named_fields;
    struct NamedCommand* names;
    const int64_t max_count;
    const size_t struct_size;
    void* (*get_struct_base)(void);
};

#define NAMFIELDWRNLOG(format, ...) LbWarnLog("%s(line %" PRIu64 "): " format "\n", src_str , text_line_number, ##__VA_ARGS__)
#define NAMFIELDERRLOG(format, ...) LbErrorLog("%s(line %" PRIu64 "): " format "\n", src_str , text_line_number, ##__VA_ARGS__)

extern TbBool AssignCpuKeepers;

extern uint64_t vid_scale_flags;

extern const struct NamedCommand logicval_type[];

struct ConfigFileData{
    const char *filename;
    TbBool (*load_func)(const char *fname, int64_t flags);
    void (*pre_load_func)();
    TbBool (*post_load_func)();
};

// Moved from kfx_render's light_data.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- embedded by value in
// config_effects.h's EffectConfigStats and config_objects.h's
// ObjectConfigStats; kfx_config is the lowest-ranked of its real
// consumers (kfx_render/kfx_sim also read it).
struct InitLight { // sizeof=0x14
    int64_t radius;
    unsigned char intensity;
    unsigned char flags;
    struct Coord3d mappos;
    unsigned char is_dynamic;
    SlabCodedCoords attached_slb;
};

struct Thing;
struct SlabMap;
struct SlabSet;
struct SlabObj;
struct Computer2;
struct Room;

// Injected so config_campaigns.c/config_terrain.c/config_trapdoor.c/
// config_rules.c don't need frontmenu_ingame_tabs.h/frontmenu_ingame_map.h/
// thing_doors.h/thing_traps.h/room_library.h directly just to push a
// just-reloaded value out to the live GUI/sim state that displays it. See
// docs/refactor/stage-04-kfx-config.md issue A. (config_crtrmodel.c's own
// live-propagation calls are deeper -- generic per-creature iterators
// taking further callback arguments -- and are left as a documented
// residual rather than force-fit into this same shape.)
struct ConfigReloadCallbacks {
    void (*update_room_tab_to_config)(void);
    void (*update_trap_tab_to_config)(void);
    void (*update_powers_tab_to_config)(void);
    void (*update_creatr_model_activities_list)(TbBool forced);
    void (*update_all_door_stats)(void);
    // thing_traps.h -- config_trapdoor.c's update_all_trap_draws_of_model()
    // sweeps the live per-class thing list to refresh every placed trap's
    // draw info after a config reload, same "config needs something from
    // above" shape as the rest of this struct. See docs/refactor/
    // stage-13-enforce-and-document.md.
    void (*update_all_trap_draws_of_model)(int64_t trap_model);
    TbBool (*add_research_to_all_players)(int64_t rtyp, int64_t rkind, int64_t amount);
    TbBool (*clear_research_for_all_players)(void);
    void (*panel_map_update)(int64_t x, int64_t y, int64_t w, int64_t h);
    void (*update_panel_color_player_color)(PlayerNumber plyr_idx, unsigned char color_idx);
    void (*setup_panel_colors)(void);
    // Invalidates the minimap panel's cached background-colour capture
    // (frontmenu_ingame_map.c's PrevPixelSize gate), so the next in-game
    // draw recaptures it against the level actually being loaded instead
    // of reusing a previous level's capture, taken under whatever palette
    // happened to be active at this early a point in loading. See
    // docs/refactor/renderer/02b-legacy-bugs-found.md.
    void (*reset_panel_map_background_cache)(void);

    // light_data.h -- lvl_filesdk1.c's load_map_data_file() resets
    // every subtile's lightness while loading a level, same as this
    // function already does. struct LightsShadows is kfx_render-owned
    // (the lish global embeds it), so kfx_config can't call it directly.
    // See docs/refactor/stage-13-enforce-and-document.md.
    void (*clear_subtiles_lightness)(void);

    // lua_cfg_funcs.h -- config-value parsing (value_function()) resolves a
    // config-file-supplied Lua function name to its registered FuncIdx.
    // kfx_script-layer lookup needed mid-parse, same "config needs something
    // from above" shape as the rest of this struct. See
    // docs/refactor/stage-09-kfx-game.md.
    FuncIdx (*get_lua_function_idx)(const char *func_name, const struct NamedCommand *named_command);

    // kfx_sim_state.h -- current level's map dimensions, read by
    // config_rules.c while parsing rules.cfg. kfx_sim is these fields'
    // true owner (49+ consumers across every layer read them during
    // simulation); kfx_config only ever reads the value another loader
    // already set, same "config needs something from above" shape as
    // the rest of this struct. See docs/refactor/
    // stage-13-enforce-and-document.md.
    int64_t (*get_map_subtiles_x)(void);
    int64_t (*get_map_subtiles_y)(void);

    // thing_objects.h -- config_objects.c's crate_thing_to_workshop_item_class()
    // queries live thing state while reclassifying already-placed objects,
    // same "config needs something from above" shape as the rest of this
    // struct. See docs/refactor/stage-13-enforce-and-document.md.
    TbBool (*thing_is_workshop_crate)(const struct Thing *thing);
    int64_t (*get_wealth_size_of_gold_hoard_model)(ThingModel objmodel);

    // thing_objects.h -- config_spritecolors.c writes the per-player Call
    // to Arms animation indices at config-load time; kfx_sim's
    // call_to_arms_graphics[] array (thing_objects.c) is the true owner
    // and only real reader. Same "config writes into a value owned by a
    // higher layer" shape as sim_feedback.h's other setter-shaped
    // entries. See docs/refactor/stage-13-enforce-and-document.md.
    void (*set_call_to_arms_graphics)(PlayerNumber plyr_idx, int64_t birth_anim_idx, int64_t alive_anim_idx, int64_t leave_anim_idx);

    // vidmode.h -- config_keeperfx.c sets this from keeperfx.cfg's
    // INGAME_RES/POINTER_SENSITIVITY commands; kfx_render owns the
    // underlying state (TbScreenMode passed as unsigned short since
    // kfx_config sits below kfx_render).
    void (*set_screen_vidmode)(int64_t nmode);
    // Getter half, for config_settingschema.c's INGAME_RES row -- reads the
    // *pending* mode (screen_vidmode itself, kfx_render/vidmode.c), not
    // LbScreenActiveMode()'s currently-applied one: INGAME_RES is
    // SApply_NeedsRestart, so right after picking a new resolution the
    // active mode hasn't changed yet, and the combo needs to show what was
    // just chosen rather than immediately reverting to the old value.
    int64_t (*get_screen_vidmode)(void);
    void (*set_base_mouse_sensitivity)(int64_t val);
    // Getter half, for config_settingschema.c's POINTER_SENSITIVITY row
    // (Phase G §6.3) -- same "load-time-only until the settings screen
    // needed to read the current value back" gap as get_screenshot_format/
    // get_hand_scale above.
    int64_t (*get_base_mouse_sensitivity)(void);

    // thing_creature.h -- config_creature.c's get_job_for_subtile() queries
    // live thing state while resolving a creature's job preference, same
    // "config needs something from above" shape as the rest of this
    // struct. See docs/refactor/stage-13-enforce-and-document.md.
    TbBool (*thing_is_creature_digger)(const struct Thing *thing);
    TbBool (*creature_is_for_dungeon_diggers_list)(const struct Thing *creatng);

    // thing_data.h/creature_control.h -- config_creature.c's
    // creature_stats_get_from_thing()/get_creature_model_flags()/
    // creature_own_name() need a handful of struct Thing/struct
    // CreatureControl fields; narrow accessors instead of exposing
    // either type by value (both are kfx_sim-owned and much larger).
    // get_creature_name_buffer() returns a pointer into the live
    // cctrl->creature_name buffer so creature_own_name()'s name
    // generator can write the result directly, same shape as this
    // struct's other pointer-returning entries (e.g. get_slabset_array).
    ThingModel (*get_thing_model)(const struct Thing *thing);
    ThingClass (*get_thing_class_id)(const struct Thing *thing);
    PlayerNumber (*get_thing_owner)(const struct Thing *thing);
    uint64_t (*get_thing_creation_turn)(const struct Thing *thing);
    int64_t (*get_thing_index)(const struct Thing *thing);
    unsigned char (*get_creature_blood_type)(const struct Thing *creatng);
    char *(*get_creature_name_buffer)(const struct Thing *creatng);

    // slab_data.h -- config_creature.c's get_job_for_subtile() also
    // needs live slab-map state, same shape as the entries above.
    struct SlabMap *(*get_slabmap_for_subtile)(MapSubtlCoord stl_x, MapSubtlCoord stl_y);
    int64_t (*slabmap_owner)(const struct SlabMap *slb);

    // thing_factory.h -- lvl_filesdk1.c's legacy .tng/.tngfx level-file
    // loaders create things directly, same "config needs something
    // from above" shape as the rest of this struct.
    TbBool (*thing_create_thing)(struct InitThing *itng);
    TbBool (*thing_create_thing_adv)(VALUE *init_data);

    // scrcapt.h -- config_keeperfx.c sets this from keeperfx.cfg's
    // SCREENSHOT command; kfx_render owns the underlying global.
    void (*set_screenshot_format)(unsigned char val);
    // Getter half, for config_settingschema.c's SCREENSHOT row (Phase G
    // §6.3) -- every other ConfigReloadCallbacks entry before this one was
    // load-time-only (file -> engine), so none needed a matching getter
    // until the settings screen needed to show the *current* value.
    unsigned char (*get_screenshot_format)(void);

    // engine_redraw.h -- config_keeperfx.c sets this from keeperfx.cfg's
    // new VID_SMOOTH command (docs/refactor/renderer/04-imgui-gui-
    // foundation.md §6.2 finding 2 -- previously only settable via the
    // "-vidsmooth" launch flag); kfx_render owns smooth_on.
    void (*set_vid_smooth)(TbBool val);

    // power_hand.h -- config_keeperfx.c sets this from keeperfx.cfg's
    // HAND_SIZE command; kfx_sim owns the underlying global.
    void (*set_hand_scale)(double val);
    // Getter half, for config_settingschema.c's HAND_SIZE row (Phase G
    // §6.3) -- same "load-time-only until the settings screen needed to
    // read the current value back" gap as get_screenshot_format above.
    double (*get_hand_scale)(void);

    // room_data.h -- config_creature.c's get_job_for_subtile() needs the
    // RoomKind of the room a creature is standing on (RoK_NONE if none);
    // kfx_sim owns struct Room, so the room->kind read happens on its side.
    RoomKind (*get_room_kind_thing_is_on)(const struct Thing *creatng);

    // player_data.h -- config_spritecolors.c needs a player's dungeon
    // colour index to pick the right sprite/icon/object variant; the
    // lookup reads kfx_sim's struct Dungeon, so it can't be a local
    // formula like PLAYER_COLORS_COUNT is.
    unsigned char (*get_player_color_idx)(PlayerNumber plyr_idx);

    // kfx_sim_state.h -- config_slabsets.c writes parsed slabset/slabobj
    // data directly into kfx_sim's live slabset[]/slabobjs[] arrays;
    // those arrays stay kfx_sim-owned (map_blocks.c reads them
    // pervasively at runtime), reached here via pointer accessors.
    struct SlabSet *(*get_slabset_array)(void);
    int64_t *(*get_slabset_num_ptr)(void);
    struct SlabObj *(*get_slabobjs_array)(void);
    int64_t *(*get_slabobjs_idx_array)(void);
    int64_t *(*get_slabobjs_num_ptr)(void);

    // kfx_sim_state.h -- config_terrain.c writes a single parsed
    // BLOCKHEALTH value directly into kfx_sim's live block_health[]
    // array; the array stays kfx_sim-owned (read pervasively at
    // runtime), reached here via a narrow single-index setter.
    void (*set_block_health)(int64_t idx, int64_t val);

    // player_data.h -- config_creature.c's special-digger-breed
    // get/set needs a single struct PlayerInfo field
    // (player->special_digger); kfx_sim owns struct PlayerInfo.
    ThingModel (*get_player_special_digger)(PlayerNumber plyr_idx);
    void (*set_player_special_digger)(PlayerNumber plyr_idx, ThingModel model);

    // player_computer.h/room_data.h/slab_data.h -- config_terrain.c's
    // computer-player room-building-process reactivation needs a
    // handful of kfx_sim reads/calls it previously reached via bare
    // same-file extern declarations. slabmap_kind is slabmap_owner's
    // sibling above. See docs/refactor/todo/
    // check-layering-symbol-level-blind-spot.md.
    struct Computer2 *(*get_computer_player_f)(int64_t plyr_idx, const char *func_name);
    TbBool (*reactivate_build_process)(struct Computer2 *comp, RoomKind rkind);
    int64_t (*reinitialise_rooms_of_kind)(RoomKind rkind);
    int64_t (*recalculate_effeciency_for_rooms_of_kind)(RoomKind rkind);
    TbBool (*slabmap_block_invalid)(const struct SlabMap *slb);
    SlabKind (*slabmap_kind)(const struct SlabMap *slb);

    // lvl_filesdk1.h -- config_campaigns.c triggers a re-scan of the
    // level/campaign directories for .lif/.lof files after a campaign
    // reload; the scan itself is kfx_sim state (level_strings[] et al).
    TbBool (*find_and_load_lif_files)(void);
    TbBool (*find_and_load_lof_files)(void);

    // player_compprocs.h/player_compchecks.h/player_compevents.h --
    // config_compp.c's computer-player .cfg field tables reference these
    // NamedCommand tables by pointer; the tables are the true (kfx_sim)
    // registry of computer-player process/check/event function slots.
    const struct NamedCommand *(*get_computer_process_func_type)(void);
    const struct NamedCommand *(*get_computer_check_func_type)(void);
    const struct NamedCommand *(*get_computer_event_func_type)(void);
    const struct NamedCommand *(*get_computer_event_test_func_type)(void);

    // player_data.h/thing_navigate.h/map_utils.h/thing_stats.h --
    // config_creature.c's availability/job-assignment logic needs a
    // handful of kfx_sim reads it previously reached via bare same-file
    // extern declarations. thing_class_and_model_name is also used by
    // config_rules.c's sacrifice logging.
    unsigned char (*get_my_player_number)(void);
    TbBool (*player_is_roaming)(PlayerNumber plyr_num);
    TbBool (*slab_is_area_inner_fill)(MapSlabCoord slb_x, MapSlabCoord slb_y);
    const char *(*thing_class_and_model_name)(ThingClass class_id, ThingModel model);

    // creature_instances.h -- config_creature.c's .cfg field tables
    // reference these NamedCommand tables by pointer; the tables are the
    // true (kfx_sim) registry of creature-instance function slots.
    const struct NamedCommand *(*get_creature_instances_func_type)(void);
    const struct NamedCommand *(*get_creature_instances_validate_func_type)(void);
    const struct NamedCommand *(*get_creature_instances_search_targets_func_type)(void);

    // creature_jobs.h -- same shape as the creature-instances tables
    // above, for the job-assignment/coords-check/coords-assign function
    // registries.
    const struct NamedCommand *(*get_creature_job_player_assign_func_type)(void);
    const struct NamedCommand *(*get_creature_job_player_check_func_type)(void);
    const struct NamedCommand *(*get_creature_job_coords_check_func_type)(void);
    const struct NamedCommand *(*get_creature_job_coords_assign_func_type)(void);

    // thing_creature.h/thing_list.h -- config_crtrmodel.c's per-model
    // config-reload sweep (creature availability change, etc.) needs to
    // both call these directly and pass several of them by pointer into
    // do_to_players_all_creatures_of_model()/
    // do_to_all_things_of_class_and_model() (kfx_sim's live-thing
    // iteration helpers); all Thing-operating, so all kfx_sim state.
    TbBool (*remove_creature_lair)(struct Thing *thing);
    TbBool (*update_creature_health_to_max)(struct Thing *creatng);
    TbBool (*update_relative_creature_health)(struct Thing *creatng);
    int64_t (*do_to_players_all_creatures_of_model)(PlayerNumber plyr_idx, int64_t crmodel, TbBool (*do_cb)(struct Thing *));
    int64_t (*do_to_all_things_of_class_and_model)(int64_t tngclass, int64_t tngmodel, TbBool (*do_cb)(struct Thing *));
    void (*recalculate_all_creature_digger_lists)(void);
    TbBool (*update_speed_of_player_creatures_of_model)(PlayerNumber plyr_idx, int64_t crmodel);
    TbBool (*creature_increase_available_instances)(struct Thing *thing);
    TbBool (*process_job_stress_and_going_postal)(struct Thing *creatng);

    // creature_states.h -- config_crtrstates.c's .cfg field tables
    // reference these NamedCommand tables by pointer; the tables are the
    // true (kfx_sim) registry of creature-state process/cleanup/
    // move-from-slab/move-check function slots.
    const struct NamedCommand *(*get_process_func_commands)(void);
    const struct NamedCommand *(*get_cleanup_func_commands)(void);
    const struct NamedCommand *(*get_move_from_slab_func_commands)(void);
    const struct NamedCommand *(*get_move_check_func_commands)(void);

    // dungeon_data.h -- config_magic.c's power-cast eligibility check
    // needs to know whether a player still has a dungeon heart; kfx_sim
    // owns struct Dungeon.
    TbBool (*player_has_heart)(PlayerNumber plyr_idx);

    // thing_data.h -- config_objects.c's crate-reclassification query
    // needs the same Thing-pointer bounds check as kfx_platform's
    // SoundStateCallbacks::thing_is_invalid (same real function, reached
    // through this struct instead since config_objects.c is kfx_config,
    // not kfx_platform).
    int64_t (*thing_is_invalid)(const struct Thing *thing);

    // thing_list.h -- config_rules.c's excess-creature rule enforcement
    // walks the live thing list; kfx_sim owns that state.
    int64_t (*setup_excess_creatures_to_leave_or_die)(int64_t max_remain);

    // lvl_filesdk1.h -- config_strings.c looks up a parsed level-name
    // string by index; kfx_sim owns the level_strings[] buffer (filled
    // while loading level files).
    char **(*get_level_strings)(void);

    // room_data.h/thing_traps.h -- config_trapdoor.c's per-player
    // door/trap buildability + amount update needs kfx_sim's struct
    // PlayerInfo/Dungeon state.
    TbBool (*set_door_buildable_and_add_to_amount)(PlayerNumber plyr_idx, ThingModel door_kind, int64_t buildable, int64_t amount);
    TbBool (*set_trap_buildable_and_add_to_amount)(PlayerNumber plyr_idx, ThingModel trap_kind, int64_t buildable, int64_t amount);

    // gui_soundmsgs.h -- config_sounds.c writes the [system] section's
    // speech_queue_limit setting directly into kfx_frontend's live
    // g_speech_queue_limit; kfx_frontend owns and reads it (the speech
    // message queue is a UI concern), same "config writes into a value
    // owned by a higher layer" shape as set_call_to_arms_graphics above.
    void (*set_speech_queue_limit)(int64_t limit);

    // lvl_script_lib.h -- config.c's script-hook config value parsing
    // (icon/anim-by-name lookups, dynamic string params) interns strings
    // into kfx_game's live script string pool.
    int64_t (*script_strdup)(const char *src);
    const char *(*script_strval)(int64_t offset);

    // game_campaign_progress.h -- config_settingschema.c's Reset Progress
    // action row (SOptT_Action, docs/refactor/gui/
    // 05-campaign-progress-and-landview.md §3.5/Phase E) needs
    // reset_all_campaign_progress(); kfx_game owns save/progress.cfg.
    void (*reset_campaign_progress)(void);
};
void set_config_reload_callbacks(const struct ConfigReloadCallbacks *callbacks);
extern const struct ConfigReloadCallbacks *config_reload_callbacks;

/******************************************************************************/
extern char keeper_runtime_directory[152];

#pragma pack()
/******************************************************************************/
extern uint64_t text_line_number;
/******************************************************************************/
char *prepare_file_path_buf_mod(char *dst, int64_t dst_size, const char *mod_dir, int64_t fgroup, const char *fname);
char *prepare_file_path_mod(const char *mod_dir, int64_t fgroup, const char *fname);
char *prepare_file_fmtpath_mod(const char *mod_dir, int64_t fgroup, const char *fmt_str, ...) KFX_PRINTF_FORMAT(3, 4);
char *prepare_file_path_buf(char *dst, int64_t dst_size, int64_t fgroup, const char *fname);
char *prepare_file_path(int64_t fgroup, const char *fname);
char *prepare_file_fmtpath(int64_t fgroup, const char *fmt_str, ...) KFX_PRINTF_FORMAT(2, 3);
/* New API - self-documenting game vs. mod distinction */
char *get_game_file_path(int64_t fgroup, const char *fname);
char *get_mod_file_path(const char *mod_dir, int64_t fgroup, const char *fname);
char *get_game_file_path_fmt(int64_t fgroup, const char *fmt_str, ...) KFX_PRINTF_FORMAT(2, 3);
char *get_mod_file_path_fmt(const char *mod_dir, int64_t fgroup, const char *fmt_str, ...) KFX_PRINTF_FORMAT(3, 4);
unsigned char *load_data_file_to_buffer(int64_t *ldsize, int64_t fgroup, const char *fmt_str, ...) KFX_PRINTF_FORMAT(3, 4);
/******************************************************************************/
TbBool load_config(const struct ConfigFileData* file_data, int64_t flags);
/******************************************************************************/
int64_t is_bonus_level(LevelNumber lvnum);
int64_t is_extra_level(LevelNumber lvnum);
int64_t is_singleplayer_level(LevelNumber lvnum);
int64_t is_singleplayer_like_level(LevelNumber lvnum);
int64_t is_multiplayer_level(LevelNumber lvnum);
int64_t is_campaign_level(LevelNumber lvnum);
int64_t is_freeplay_level(LevelNumber lvnum);
TbBool is_level_in_current_campaign(LevelNumber lvnum);
int64_t array_index_for_singleplayer_level(LevelNumber sp_lvnum);
int64_t storage_index_for_bonus_level(LevelNumber bn_lvnum);
LevelNumber first_singleplayer_level(void);
LevelNumber last_singleplayer_level(void);
LevelNumber next_singleplayer_level(LevelNumber sp_lvnum, TbBool ignore);
LevelNumber prev_singleplayer_level(LevelNumber sp_lvnum);
LevelNumber bonus_level_for_singleplayer_level(LevelNumber sp_lvnum);
LevelNumber first_multiplayer_level(void);
LevelNumber next_multiplayer_level(LevelNumber mp_lvnum);
LevelNumber first_extra_level(void);
LevelNumber next_extra_level(LevelNumber ex_lvnum);
LevelNumber get_extra_level(int64_t elv_kind);
// Level info support for active campaign
struct LevelInformation *get_level_info(LevelNumber lvnum);
struct LevelInformation *get_or_create_level_info(LevelNumber lvnum, uint64_t lvoptions);
struct LevelInformation *get_first_level_info(void);
struct LevelInformation *get_last_level_info(void);
struct LevelInformation *get_next_level_info(struct LevelInformation *previnfo);
struct LevelInformation *get_prev_level_info(struct LevelInformation *nextinfo);
int64_t set_level_info_text_name(LevelNumber lvnum, char *name, uint64_t lvoptions);
int64_t set_level_info_string_index(LevelNumber lvnum, char *stridx, uint64_t lvoptions);
int64_t get_level_fgroup(LevelNumber lvnum);
const char *get_language_lwrstr(int64_t lang_id);
/******************************************************************************/
TbBool reset_credits(struct CreditsItem *credits);
TbBool setup_campaign_credits_data(struct GameCampaign *campgn);
/******************************************************************************/
TbBool parameter_is_number(const char* parstr);

int64_t find_conf_block(const char *buf,int64_t *pos,int64_t buflen,const char *blockname);
TbBool iterate_conf_blocks(const char * buf, int64_t * pos, int64_t buflen, const char ** name, int64_t * namelen);
int64_t recognize_conf_command(const char *buf,int64_t *pos,int64_t buflen,const struct NamedCommand *commands);
int64_t get_conf_line(const char *buf, int64_t *pos, int64_t buflen, char *dst, int64_t dstlen);
TbBool skip_conf_to_next_line(const char *buf,int64_t *pos,int64_t buflen);
int64_t get_conf_parameter_single(const char *buf,int64_t *pos,int64_t buflen,char *dst,int64_t dstlen);
int64_t get_conf_parameter_whole(const char *buf,int64_t *pos,int64_t buflen,char *dst,int64_t dstlen);

TbBool parse_named_field_block(const char *buf, int64_t len, const char *config_textname, int64_t flags,const char* blockname,
    const struct NamedField named_field[], const struct NamedFieldSet* named_fields_set, int64_t idx);
TbBool parse_named_field_blocks(char *buf, int64_t len, const char *config_textname, int64_t flags,
        const struct NamedFieldSet* named_fields_set);
int64_t recognize_conf_parameter(const char *buf,int64_t *pos,int64_t buflen,const struct NamedCommand *commands);
void assign_named_field_value(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
const char *get_conf_parameter_text(const struct NamedCommand commands[],int64_t num);
int64_t get_named_field_id(const struct NamedField *desc, const char *itmname);
int64_t get_id(const struct NamedCommand *desc, const char *itmname);
long long get_long_id(const struct LongNamedCommand* desc, const char* itmname);
/******************************************************************************/
int64_t value_name           (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_default        (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_flagsfield     (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_longflagsfield (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_icon           (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_effOrEffEl     (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_animid         (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_transpflg      (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_stltocoord     (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_function       (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_stringId       (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);

void assign_icon   (const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
void assign_default(const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
void assign_null   (const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
void assign_animid (const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);

int64_t parse_named_field_value(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t get_named_field_value(const struct NamedField* named_field, const struct NamedFieldSet* named_fields_set, int64_t idx);

#ifdef __cplusplus
}
#endif
#endif
