/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_trapdoor.h
 *     Header file for config_trapdoor.c.
 * @par Purpose:
 *     Traps and doors configuration loading functions.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 21 Dec 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef DK_CFGTRAPDOOR_H
#define DK_CFGTRAPDOOR_H

#include "bflib_basics.h"
#include "globals.h"
#include "config_objects.h"
#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define TRAPDOOR_TYPES_MAX 2000

/******************************************************************************/
struct DoorConfigStats {
    char code_name[COMMAND_WORD_LEN];
    TextStringId name_stridx;
    TextStringId tooltip_stridx;
    int64_t bigsym_sprite_idx;
    int64_t medsym_sprite_idx;
    int64_t pointer_sprite_idx;
    int64_t panel_tab_idx;
    unsigned char manufct_level;
    uint64_t manufct_required;
    HitPoints health;
    int64_t slbkind[2];
    int64_t open_speed;
    int64_t model_flags;
    GoldAmount selling_value;
    TbBool unsellable;
    int64_t place_sound_idx;
    FuncIdx updatefn_idx;
};

/* Contains properties of a door model, to be stored in DoorConfigStats. */
enum DoorModelFlags {
    DoMF_ResistNonMagic = 0x0001,
    DoMF_Secret         = 0x0002,
    DoMF_Thick          = 0x0004,
    DoMF_Midas          = 0x0008,
};

struct TrapConfigStats {
    char code_name[COMMAND_WORD_LEN];
    TextStringId name_stridx;
    TextStringId tooltip_stridx;
    int64_t bigsym_sprite_idx;
    int64_t medsym_sprite_idx;
    int64_t pointer_sprite_idx;
    int64_t panel_tab_idx;
    unsigned char manufct_level;
    uint64_t manufct_required;
    int64_t shots;
    GameTurnDelta shots_delay;
    int64_t initial_delay; // Trap is placed on reload phase, value in game turns.
    unsigned char trigger_type;
    unsigned char activation_type;
    FuncIdx activation_lua_func_idx;
    int64_t created_itm_model; // Shot model, effect model, slab kind.
    unsigned char activation_level;
    unsigned char hit_type;
    TbBool hidden;
    unsigned char slappable;
    TbBool detect_invisible;
    TbBool notify;
    TbBool place_on_bridge;
    TbBool place_on_subtile;
    TbBool instant_placement;
    TbBool remove_once_depleted;
    HitPoints health;
    char destructible;
    char unstable;
    EffectOrEffElModel destroyed_effect;
    int64_t size_xy;
    int64_t size_z;
    uint64_t sprite_anim_idx;
    uint64_t attack_sprite_anim_idx;
    uint64_t recharge_sprite_anim_idx;
    uint64_t sprite_size_max;
    uint64_t anim_speed;
    uint64_t attack_anim_speed;
    uint64_t recharge_anim_speed;
    unsigned char unanimated;
    unsigned char unshaded;
    unsigned char random_start_frame;
    unsigned char flag_number;
    int64_t light_radius; // Creates light if not null.
    unsigned char light_intensity;
    unsigned char light_flag;
    unsigned char light_colour_r, light_colour_g, light_colour_b; /* per-pixel lighting colour, 0,0,0 = white */
    unsigned char transparency_flag; // Transparency in lower 2 bits.
    int64_t shot_shift_x;
    int64_t shot_shift_y;
    int64_t shot_shift_z;
    struct ComponentVector shotvector;
    struct FlameProperties flame;
    GoldAmount selling_value;
    TbBool unsellable;
    int64_t place_sound_idx;
    int64_t trigger_sound_idx;
    FuncIdx updatefn_idx;
};

/* Manufacture types data. Originally was named TrapData, but stores both traps and doors, now no longer matches original. */
struct ManufactureData {
    ThingClass tngclass; // Thing class created when manufactured design is placed.
    ThingModel tngmodel; // Thing model created when manufactured design is placed.
    int64_t work_state; // Work state used to place the manufactured item on map.
    TextStringId tooltip_stridx;
    int64_t bigsym_sprite_idx;
    int64_t medsym_sprite_idx;
    int64_t panel_tab_idx;
};

struct TrapDoorConfig {
    int64_t trap_types_count;
    struct TrapConfigStats trap_cfgstats[TRAPDOOR_TYPES_MAX];
    int64_t door_types_count;
    struct DoorConfigStats door_cfgstats[TRAPDOOR_TYPES_MAX];
    ThingModel trap_to_object[TRAPDOOR_TYPES_MAX];
    ThingModel door_to_object[TRAPDOOR_TYPES_MAX];
    int64_t manufacture_types_count;
    /* Stores manufacturable items. Was originally named trap_data. */
    struct ManufactureData manufacture_data[2*TRAPDOOR_TYPES_MAX];
};
/******************************************************************************/
extern const struct ConfigFileData keeper_trapdoor_file_data;
extern struct NamedCommand trap_desc[TRAPDOOR_TYPES_MAX];
extern struct NamedCommand door_desc[TRAPDOOR_TYPES_MAX];
extern const struct NamedFieldSet trapdoor_door_named_fields_set;
extern const struct NamedFieldSet trapdoor_trap_named_fields_set;
/******************************************************************************/
struct TrapConfigStats* get_trap_model_stats(int64_t tngmodel);
struct DoorConfigStats *get_door_model_stats(int64_t tngmodel);
struct ManufactureData *get_manufacture_data(int64_t manufctr_idx);
int64_t get_manufacture_data_index_for_thing(ThingClass tngclass, ThingModel tngmodel);

ThingModel door_crate_object_model(ThingModel tngmodel);
ThingModel trap_crate_object_model(ThingModel tngmodel);
const char *door_code_name(int64_t tngmodel);
const char *trap_code_name(int64_t tngmodel);
int64_t door_model_id(const char * code_name);
int64_t trap_model_id(const char * code_name);

TbBool is_trap_placeable(PlayerNumber plyr_idx, int64_t trap_idx);
TbBool is_trap_buildable(PlayerNumber plyr_idx, int64_t trap_idx);
TbBool is_trap_built(PlayerNumber plyr_idx, int64_t tngmodel);
TbBool is_door_placeable(PlayerNumber plyr_idx, int64_t door_idx);
TbBool is_door_buildable(PlayerNumber plyr_idx, int64_t door_idx);
TbBool is_door_built(PlayerNumber plyr_idx, int64_t door_idx);
TbBool create_manufacture_array_from_trapdoor_data(void);
TbBool make_available_all_doors(PlayerNumber plyr_idx);
TbBool make_available_all_traps(PlayerNumber plyr_idx);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
