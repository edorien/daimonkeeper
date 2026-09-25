/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file config_slabsets.c
 *     Slabs, rooms, traps and doors configuration loading functions.
 * @par Purpose:
 *     Support of configuration files for trap and door elements.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     25 May 2009 - 26 Jul 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "kfx_memory.h"
#include "pre_inc.h"
#include "config_slabsets.h"
#include "globals.h"

#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "value_util.h"

#include <toml.h>
#include "config_strings.h"
#include "kfx_config_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool load_slabset_config_file(const char *fname, int64_t flags);
static TbBool load_columns_config_file(const char *fname, int64_t flags);

const struct ConfigFileData keeper_slabset_file_data = {
    .filename = "slabset.toml",
    .load_func = load_slabset_config_file,
    .pre_load_func = NULL,
    .post_load_func = NULL,
};

const struct ConfigFileData keeper_columns_file_data = {
    .filename = "columnset.toml",
    .load_func = load_columns_config_file,
    .pre_load_func = NULL,
    .post_load_func = NULL,
};

/******************************************************************************/
typedef struct VALUE VALUE;
const struct NamedCommand slab_styles_commands[] = {
    {"S",         0},
    {"W",         1},
    {"N",         2},
    {"E",         3},
    {"SW",        4},
    {"NW",        5},
    {"NE",        6},
    {"SE",        7},
    {"ALL",       8},
    {"S_LAVA",    9},
    {"W_LAVA",   10},
    {"N_LAVA",   11},
    {"E_LAVA",   12},
    {"SW_LAVA",  13},
    {"NW_LAVA",  14},
    {"NE_LAVA",  15},
    {"SE_LAVA",  16},
    {"ALL_LAVA", 17},
    {"S_WATER",  18},
    {"W_WATER",  19},
    {"N_WATER",  20},
    {"E_WATER",  21},
    {"SW_WATER", 22},
    {"NW_WATER", 23},
    {"NE_WATER", 24},
    {"SE_WATER", 25},
    {"ALL_WATER",26},
    {"CENTER",   27}
};

static TbBool load_slabset_config_file(const char *fname, int64_t flags)
{
    VALUE file_root;

    if ((flags & CnfLd_AcceptPartial) == 0)
    {
        clear_slabsets();
    }
    
    if (!load_toml_file(fname,&file_root,flags))
        return false;

    char key[64] = "";
    VALUE *slb_section;
    struct SlabSet *slabset_arr = config_reload_callbacks->get_slabset_array();
    struct SlabObj *slabobjs_arr = config_reload_callbacks->get_slabobjs_array();
    int64_t *slabobjs_idx_arr = config_reload_callbacks->get_slabobjs_idx_array();
    int64_t *slabobjs_num_ptr = config_reload_callbacks->get_slabobjs_num_ptr();
    // Create sections
    for (int64_t slab_kind = 0; slab_kind < kfx_config_state.conf.slab_conf.slab_types_count; slab_kind++)
    {
        snprintf(key, sizeof(key), "slab%" PRId64, (int64_t)(slab_kind));
        slb_section = value_dict_get(&file_root, key);
        if (value_type(slb_section) == VALUE_DICT)
        {
            for (int64_t slabstyle_no = 0; slabstyle_no < SLABSETS_PER_SLAB; slabstyle_no++)
            {
                VALUE * section = value_dict_get(slb_section, slab_styles_commands[slabstyle_no].name);

                int64_t slabset_no = slab_kind * SLABSETS_PER_SLAB + slabstyle_no;

                VALUE *col_arr = value_dict_get(section, "Columns");
                if (value_type(col_arr) == VALUE_ARRAY)
                {
                    for (size_t col_no = 0; col_no < 9; col_no++)
                    {
                        ColumnIndex col_idx = value_int32(value_array_get(col_arr, col_no));
                        slabset_arr[slabset_no].col_idx[col_no] = -col_idx;
                    }
                }

                snprintf(key, sizeof(key), "%s_objects", slab_styles_commands[slabstyle_no].name);
                VALUE * objects_arr = value_dict_get(slb_section, key);
                if(value_type(section) == VALUE_DICT && value_array_size(objects_arr) == 0)
                {
                    slabobjs_idx_arr[slabset_no] = -1;
                }
                else
                {
                    for (size_t i = 0; i < value_array_size(objects_arr); i++)
                    {
                        if (*slabobjs_num_ptr >= SLABOBJS_COUNT)
                        {
                            ERRORLOG("Exceeding max of %" PRId64 " slabobjects",(int64_t)(SLABOBJS_COUNT));
                            break;
                        }
                        struct SlabObj* slabobj = &slabobjs_arr[*slabobjs_num_ptr];
                        VALUE * object = value_array_get(objects_arr, i);
                        CONDITIONAL_ASSIGN_CLASS(object,"ThingType",slabobj->class_id);
                        CONDITIONAL_ASSIGN_BOOL(object, "IsLight", slabobj->isLight);
                        CONDITIONAL_ASSIGN_MODEL(object,"Subtype",slabobj->model,slabobj->class_id);
                        CONDITIONAL_ASSIGN_ARR3_INT(object,"RelativePosition",slabobj->offset_x,slabobj->offset_y,slabobj->offset_z)
                        CONDITIONAL_ASSIGN_INT(object, "EffectRange", slabobj->range);
                        CONDITIONAL_ASSIGN_INT(object, "Subtile",     slabobj->stl_id);

                        slabobj->slabset_id = slabset_no;
                        if (i == 0)
                        {
                            slabobjs_idx_arr[slabset_no] = *slabobjs_num_ptr;
                        }
                        (*slabobjs_num_ptr)++;
                    }
                }
            }
        }
    }
    value_fini(&file_root);
    return true;
}

static TbBool load_columns_config_file(const char *fname, int64_t flags)
{
    VALUE file_root;
    
    if (!load_toml_file(fname,&file_root,flags))
        return false;

    char key[64];
    VALUE *section;
    // Create sections
    for (int64_t col_no = 0; col_no < COLUMNS_COUNT; col_no++)
    {
        {
            snprintf(key, sizeof(key), "column%" PRId64, (int64_t)(col_no));
            section = value_dict_get(&file_root, key);
        }
        if (value_type(section) == VALUE_DICT)
        {
            unsigned char bitfields = 0;
            TbBool permanent = true;
            bitfields |= permanent;

            if (col_no + 1 > kfx_config_state.conf.column_conf.columns_count)
                kfx_config_state.conf.column_conf.columns_count = col_no + 1;
            
            VALUE *lintel_val = value_dict_get(section, "Lintel");
            if (value_type(lintel_val) == VALUE_INT32)
            {
                char Lintel = value_int32(lintel_val);
                if (Lintel > 7 || Lintel < 0)
                {
                    ERRORLOG("invalid Lintel (%" PRId64 ") for column %" PRId64,(int64_t)(Lintel),(int64_t)(col_no));
                    continue;
                }
                Lintel <<= 1;
                bitfields |= Lintel;
            }
            
            VALUE *height_val = value_dict_get(section, "Height");
            if (value_type(height_val) == VALUE_INT32)
            {
                char floorHeight = value_int32(height_val);
                if (floorHeight > COLUMN_STACK_HEIGHT || floorHeight < 0)
                {
                    ERRORLOG("invalid floorHeight (%" PRId64 ") for column %" PRId64,(int64_t)(floorHeight),(int64_t)(col_no));
                    continue;
                }
                floorHeight <<= 4;
                bitfields |= floorHeight;
            }

            kfx_config_state.conf.column_conf.cols[col_no].bitfields = bitfields;
            CONDITIONAL_ASSIGN_INT(section, "SolidMask",    kfx_config_state.conf.column_conf.cols[col_no].solidmask    );
            CONDITIONAL_ASSIGN_INT(section, "FloorTexture", kfx_config_state.conf.column_conf.cols[col_no].floor_texture);
            CONDITIONAL_ASSIGN_INT(section, "Orientation",  kfx_config_state.conf.column_conf.cols[col_no].orient       );

            VALUE *Cubes_arr = value_dict_get(section, "Cubes");
            if(value_type(Cubes_arr) == VALUE_ARRAY)
            {
                for (size_t cube_no = 0; cube_no < COLUMN_STACK_HEIGHT; cube_no++)
                {
                    kfx_config_state.conf.column_conf.cols[col_no].cubes[cube_no] = value_int32(value_array_get(Cubes_arr, cube_no));
                }
            }
        }
    }
    value_fini(&file_root);    
    return true;
}

void clear_slabsets(void)
{
    struct SlabSet *slabset_arr = config_reload_callbacks->get_slabset_array();
    struct SlabObj *slabobjs_arr = config_reload_callbacks->get_slabobjs_array();
    int64_t *slabobjs_idx_arr = config_reload_callbacks->get_slabobjs_idx_array();
    struct SlabSet *sset;
    struct SlabObj *sobj;
    int64_t i;
    for (i=0; i < SLABSET_COUNT; i++)
    {
        sset = &slabset_arr[i];
        memset(sset, 0, sizeof(struct SlabSet));
        slabobjs_idx_arr[i] = -1;
    }
    *config_reload_callbacks->get_slabset_num_ptr() = SLABSET_COUNT;
    *config_reload_callbacks->get_slabobjs_num_ptr() = 0;
    for (i=0; i < SLABOBJS_COUNT; i++)
    {
        sobj = &slabobjs_arr[i];
        memset(sobj, 0, sizeof(struct SlabObj));
    }
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
