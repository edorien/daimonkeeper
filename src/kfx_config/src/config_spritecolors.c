/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_textures.c
 *     texture animation configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for trap and door elements.
 * @par Comment:
 *     None.
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_spritecolors.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "config_strings.h"
#include "kfx_config_state.h"
// Real usage: PLAYER_NEUTRAL.
#include "value_util.h"

#include <toml.h>
#include "config.h"
#include "ports/render_port.h"
#include "ports/sim_port.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool load_spritecolors_config_file(const char *fname, int64_t flags);

const struct ConfigFileData keeper_spritecolors_file_data = {
    .filename = "spritecolors.toml",
    .load_func = load_spritecolors_config_file,
    .pre_load_func = NULL,
    .post_load_func = NULL,
};
/******************************************************************************/
#define MAX_COLORED_SPRITES 255
// COLOURS_COUNT (kfx_sim's player_data.h, a higher-ranked library)
// literal-duplicated -- only this file uses it, not worth pulling in
// player_data.h just for one constant.
#define PLAYER_COLORS_COUNT (9 + 2)
static int64_t gui_panel_sprites_eq[MAX_COLORED_SPRITES * PLAYER_COLORS_COUNT];
static int64_t pointer_sprites_eq[MAX_COLORED_SPRITES * PLAYER_COLORS_COUNT];
static int64_t button_sprite_eq[MAX_COLORED_SPRITES * PLAYER_COLORS_COUNT];
static int64_t animationIds_eq[MAX_COLORED_SPRITES * PLAYER_COLORS_COUNT];
static int64_t objects_eq[MAX_COLORED_SPRITES * PLAYER_COLORS_COUNT];
/******************************************************************************/
static int64_t get_player_colored_idx(int64_t base_icon_idx,unsigned char color_idx,int64_t *arr);
/******************************************************************************/

static void load_array(VALUE* file_root, const char *arr_name,int64_t *arr, int64_t flags,int64_t (*string_to_id_f)(const char *))
{
    if ((flags & CnfLd_AcceptPartial) == 0)
    {
        memset(arr,0,sizeof(int64_t) * MAX_COLORED_SPRITES * PLAYER_COLORS_COUNT );
    }
    VALUE *toml_arr = value_dict_get(file_root, arr_name);
    if (value_array_size(toml_arr) > MAX_COLORED_SPRITES)
    {
        WARNLOG("too many colored frames, max %" PRId64 " got %" PRId64,(int64_t)(MAX_COLORED_SPRITES), (int64_t) value_array_size(toml_arr));
    }
    for (size_t sprite_no = 0; sprite_no < value_array_size(toml_arr); sprite_no++)
    {
        VALUE *col_arr = value_array_get(toml_arr, sprite_no);
        if (value_array_size(col_arr) > PLAYER_COLORS_COUNT)
        {
            WARNLOG("too many colors for %s, max %" PRId64 " got %" PRId64,arr_name,(int64_t)(PLAYER_COLORS_COUNT), (int64_t) value_array_size(col_arr));
            continue;
        }
        for (size_t plr_idx = 0; plr_idx < value_array_size(col_arr); plr_idx++)
        {
            VALUE * entry = value_array_get(col_arr, plr_idx);
            if (value_type(entry) == VALUE_INT32)
            {
                arr[sprite_no * PLAYER_COLORS_COUNT + plr_idx] = value_int32(entry);
            }
            else
            {
                int64_t icon_id = string_to_id_f(value_string(entry));
                if (icon_id == -2)
                {
                    WARNLOG("unknown sprite %s",value_string(entry));
                }
                arr[sprite_no * PLAYER_COLORS_COUNT + plr_idx] = icon_id;
            }
        }
    }
}

static TbBool load_spritecolors_config_file(const char *fname, int64_t flags)
{
    VALUE file_root;
    if (!load_toml_file(fname,&file_root,flags))
        return false;

    load_array(&file_root,"gui_panel_sprites",gui_panel_sprites_eq,flags,render_port->get_icon_id);
    load_array(&file_root,"pointer_sprites",pointer_sprites_eq,flags,render_port->get_icon_id);
    load_array(&file_root,"button_sprite",button_sprite_eq,flags,render_port->get_icon_id);
    load_array(&file_root,"animationIds",animationIds_eq,flags,render_port->get_anim_id_);
    load_array(&file_root,"objects",objects_eq,flags,render_port->get_anim_id_);

    for (size_t plr_idx = 0; plr_idx < PLAYER_COLORS_COUNT; plr_idx++)
    {
        simport_set_call_to_arms_graphics(plr_idx,
            get_player_colored_idx(867,plr_idx + 1,animationIds_eq),
            get_player_colored_idx(868,plr_idx + 1,animationIds_eq),
            get_player_colored_idx(869,plr_idx + 1,animationIds_eq));
    }


    value_fini(&file_root);

    return true;
}

static int64_t get_player_colored_idx(int64_t base_icon_idx,unsigned char color_idx,int64_t *arr)
{
    if (color_idx >= PLAYER_COLORS_COUNT)
    {
        return base_icon_idx;
    }
    for (size_t i = 0; i < MAX_COLORED_SPRITES; i++)
    {
        if (arr[i * PLAYER_COLORS_COUNT] == base_icon_idx)
        {
            return arr[i * PLAYER_COLORS_COUNT + color_idx];
        }
        else if (arr[i * PLAYER_COLORS_COUNT] == 0)
        {
            return base_icon_idx;
        }
    }
    return base_icon_idx;
}

int64_t get_player_colored_icon_idx(int64_t base_icon_idx,PlayerNumber plyr_idx)
{
    return get_player_colored_idx(base_icon_idx,simport_get_player_color_idx(plyr_idx) + 1,gui_panel_sprites_eq);
}
int64_t get_player_colored_pointer_icon_idx(int64_t base_icon_idx,PlayerNumber plyr_idx)
{
    return get_player_colored_idx(base_icon_idx,simport_get_player_color_idx(plyr_idx) + 1,pointer_sprites_eq);
}

int64_t get_player_colored_button_sprite_idx(const int64_t base_icon_idx,const PlayerNumber plyr_idx)
{
    unsigned char color_idx;
    if (plyr_idx == PLAYER_NEUTRAL)
    {
        color_idx = (get_gameturn() % (4 * kfx_config_state.neutral_flash_rate)) / kfx_config_state.neutral_flash_rate;
    }
    else
    {
        color_idx = simport_get_player_color_idx(plyr_idx);
    }

    return get_player_colored_idx(base_icon_idx,color_idx + 1,button_sprite_eq);
}

ThingModel get_player_colored_object_model(ThingModel base_model_idx,PlayerNumber plyr_idx)
{
    return get_player_colored_idx(base_model_idx,simport_get_player_color_idx(plyr_idx) + 1,objects_eq);
}

ThingModel get_coloured_object_base_model(ThingModel model_idx)
{
    for (size_t i = 0; i < MAX_COLORED_SPRITES; i++)
    {
        ThingModel base = objects_eq[i*PLAYER_COLORS_COUNT];
        if (base == 0)
            return 0;

        for (size_t j = 0; j < PLAYER_COLORS_COUNT; j++)
        {
            if (model_idx == objects_eq[i*PLAYER_COLORS_COUNT+j])
                return base;
        }
    }
    return 0;

}
/******************************************************************************/
#ifdef __cplusplus
}
#endif
