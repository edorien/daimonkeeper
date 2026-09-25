/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file instance_info.h
 *     Creature-instance config-data type, shared by kfx_sim's
 *     creature_instances.h (which authors and defines the instance
 *     mechanism) and kfx_config's config_magic.h, whose struct
 *     MagicConfig embeds struct InstanceInfo by value.
 * @par Comment:
 *     Physically split out of kfx_sim's creature_instances.h into
 *     this new low-rank header, same pattern as camera_data.h/
 *     packet_data.h/speech_ref.h/save_catalogue.h: kfx_config needs
 *     the complete type by value, not just a pointer, and it's a
 *     plain POD struct with no kfx_sim-specific type dependencies. See
 *     docs/refactor/stage-13-enforce-and-document.md.
 */
/******************************************************************************/
#ifndef DK_INSTANCE_INFO_H
#define DK_INSTANCE_INFO_H

#include "bflib_basics.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#pragma pack(1)

struct InstanceInfo {
    TbBool instant;
    int64_t time;
    int64_t fp_time;
    int64_t action_time;
    int64_t fp_action_time;
    int64_t reset_time;
    int64_t fp_reset_time;
    unsigned char graphics_idx;
    char postal_priority;
    int64_t instance_property_flags;
    int64_t force_visibility;
    unsigned char primary_target;
    unsigned char func_idx;
    int64_t func_params[2];
    int64_t range_min;
    int64_t range_max;
    int64_t symbol_spridx;
    int64_t tooltip_stridx;
    TbBool no_animation_loop;
    // Refer to creature_instances_validate_func_list (kfx_sim)
    uint8_t validate_source_func;
    int64_t validate_source_func_params[2];
    uint8_t validate_target_func;
    int64_t validate_target_func_params[2];
    // Refer to creature_instances_search_targets_func_list (kfx_sim)
    uint8_t search_func;
    int64_t search_func_params[2];
    TbBool fp_allow_self_cast_while_frozen;
    TbBool fp_allow_self_cast_when_chicken;
};

#pragma pack()
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
