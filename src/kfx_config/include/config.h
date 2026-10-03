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
    CnfLd_ListKnownKeys =  0x10, /**< With CnfLd_ListOnly, for NamedField tables: still recognise the other keys (and skip them) so that only unknown keys are reported, as the hand-written parsers do. */
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
#include "port_check.h"
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
    char argnum; //for fields that assign multiple values, -1 passes full string to assign function (-2: without its length limit)
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
    /* gpu-v2 lighting pass: light colour, 0..255 per channel; all zero (the default) means white. Only the
     * Vulkan renderer's per-pixel lighting uses it -- classic lighting is scalar. */
    unsigned char colour_r, colour_g, colour_b;
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
struct GameCampaign;
/** A file in a campaign's or map pack's levels folder, for any campaign -- not
 *  only the loaded one (FGrp_CmpgLvls). dst is set to "" when it has none. */
char *prepare_campaign_levels_path(char *dst, int64_t dst_size, const struct GameCampaign *campgn, const char *fname);
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
/* The level numbers per-level config loading needs are kfx_sim_state's
   (selected_level_number, loaded_level_number); kfx_config can't see
   kfx_sim_state, so it reads them through read-only pointers main.cpp's
   wire_ports() installs -- plain loads, never stale, even after a save load
   replaces the sim state. They read 0 until wired. Refactor pass 2, S10
   (they were SimFeedbackCallbacks getters). */
void set_config_level_sources(const LevelNumber *selected_level, const LevelNumber *loaded_level);
/* The selected level, or the loaded one when none is selected: the same
   rule as kfx_sim's get_level_number(). */
LevelNumber config_level_number(void);
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
/** Parses the lines of the block pos is in (just after its header), up to the next block; leaves pos there. */
void parse_named_field_block_lines(const char *buf, int64_t *pos, int64_t len, const char *config_textname, int64_t flags,
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

/**
 * argnum for a row that, like -1, gets the whole rest of the line, but with no
 * length limit (-1 rows get at most 1023 characters), and with the line's end
 * character when there is one, so get_conf_parameter_single() on it behaves as
 * on the file (an empty value still writes an empty word). For list keys.
 */
#define NAMFIELD_WHOLE_LINE_UNLIMITED -2
/** A row's min and max when the row has no bounds of its own (the editor schema then uses the field type's range). */
#define NAMFIELD_NO_BOUNDS INT64_MIN, INT64_MAX
/** Returned by a parse function to leave the field as it is (assign_cast stores nothing). */
#define NAMFIELD_KEEP INT64_MIN
// The hand-written parsers' rules, kept by refactor pass 3 (S04); see config.c.
int64_t value_atoi           (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_atoi_nonneg    (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_atoi_in_bounds (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_id_nonneg      (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_id_positive    (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_ignored        (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_string_id_positive(const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_word           (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
int64_t value_ids_or         (const struct NamedField* named_field, const char* value_text, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);
void assign_cast   (const struct NamedField* named_field, int64_t value, const struct NamedFieldSet* named_fields_set, int64_t idx, const char* src_str, unsigned char flags);

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
