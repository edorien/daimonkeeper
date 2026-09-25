/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file engine_textures.c
 *     Texture blocks support.
 * @par Purpose:
 *     Defines texture blocks, their initialization and loading.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     02 Apr 2010 - 02 May 2014
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "engine_textures.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "bflib_vidraw.h"
#include "renderer/software/SwDrawTarget.h"
#include "engine_lenses.h"
#include "vidmode.h"
#include "config.h"
#include "kfx_config_state.h"
#include "config_mods.h"
#include "bflib_render.h"
#include "renderer/RendererManager.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
unsigned char block_mem[TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_STAT_COUNT * 32 * 32];
unsigned char *block_ptrs[TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT];

int64_t block_dimension = 32;
int64_t block_count_per_row = 8;

static int64_t anim_counter;
/******************************************************************************/
#ifdef __cplusplus
}
#endif
/******************************************************************************/
void setup_texture_block_mem(void)
{
    unsigned char** dst = block_ptrs;
    unsigned char* src  = block_mem;
    for (int64_t i = 0; i < (TEXTURE_VARIATIONS_COUNT * TEXTURE_BLOCKS_COUNT); i++)
    {
        block_ptrs[i] = block_mem + block_dimension;
    }
    for (int64_t f = 0; f < TEXTURE_VARIATIONS_COUNT; f++)
    {
        for (int64_t i = 0; i < TEXTURE_BLOCKS_STAT_COUNT_A / block_count_per_row; i++)
        {
            for (uint64_t k = 0; k < block_count_per_row; k++)
            {
                *dst = src;
                src += block_dimension;
                dst++;
            }
            src += (block_dimension-1)*block_dimension*block_count_per_row;
        }
        dst += TEXTURE_BLOCKS_ANIM_COUNT;

        for (int64_t i = 0; i < TEXTURE_BLOCKS_STAT_COUNT_B / block_count_per_row; i++)
        {
            for (uint64_t k = 0; k < block_count_per_row; k++)
            {
                *dst = src;
                src += block_dimension;
                dst++;
            }
            src += (block_dimension-1)*block_dimension*block_count_per_row;
        }

    }
}

int64_t init_animating_texture_maps(void)
{
    SYNCDBG(8,"Starting");
    anim_counter = TEXTURE_BLOCKS_ANIM_FRAMES-1;
    return update_animating_texture_maps();
}

int64_t update_animating_texture_maps(void)
{
  SYNCDBG(18,"Starting");
  unsigned char** dst = block_ptrs;
  int64_t result=true;

  anim_counter = (anim_counter+1) % TEXTURE_BLOCKS_ANIM_FRAMES;
  for (int64_t f = 0; f < TEXTURE_VARIATIONS_COUNT; f++)
  {
      for (int64_t i = 0; i < TEXTURE_BLOCKS_ANIM_COUNT; i++)
      {
          int64_t j = kfx_config_state.texture_animation[TEXTURE_BLOCKS_ANIM_FRAMES*i+anim_counter];
          if (((j>=0) && (j<TEXTURE_BLOCKS_STAT_COUNT_A)) ||
              ((j>=TEX_B_START_POINT) && (j<(TEX_B_START_POINT + TEXTURE_BLOCKS_STAT_COUNT_B))))
          {
            dst[TEXTURE_BLOCKS_STAT_COUNT_A + i] = dst[j];
          }
          else
          {
            result=false;
          }
      }
      dst += TEXTURE_BLOCKS_COUNT;
  }
  return result;
}

static char *prepare_letter_one_file_path_for_mod(uint64_t tmapidx, char letter, LevelNumber lvnum, int64_t fgroup, const struct ModConfigItem *mod_item)
{
    // Note that this is the reverse mods direction

    const struct ModExistState *mod_state = &mod_item->state;
    char* fname = NULL;
    char mod_dir[256] = {0};
    sprintf(mod_dir, "%s/%s", MODS_DIR_NAME, mod_item->name);

    if (mod_state->cmpg_lvls)
    {
        fname = prepare_file_fmtpath_mod(mod_dir, FGrp_CmpgLvls, "map%05" PRIu64 ".tmap%c%03" PRIu64 ".dat", (uint64_t)lvnum, (int)(letter), (uint64_t)(tmapidx));
        if (fname[0] != 0 && LbFileExists(fname))
            return fname;
    }

    if (mod_state->cmpg_config)
    {
        fname = prepare_file_fmtpath_mod(mod_dir, FGrp_CmpgConfig, "tmap%c%03" PRIu64 ".dat", (int)(letter), (uint64_t)(tmapidx));
        if (fname[0] != 0 && LbFileExists(fname))
            return fname;
    }

    if (mod_state->std_data)
    {
        fname = prepare_file_fmtpath_mod(mod_dir, FGrp_StdData, "tmap%c%03" PRIu64 ".dat", (int)(letter), (uint64_t)(tmapidx));
        if (fname[0] != 0 && LbFileExists(fname))
            return fname;
    }

    return NULL;
}

static char *prepare_letter_one_file_path_for_mod_list(uint64_t tmapidx, char letter, LevelNumber lvnum, int64_t fgroup, const struct ModConfigItem *mod_items, int64_t mod_cnt)
{
    // Note that this is the reverse mods direction
    for (int64_t i=mod_cnt-1; i>=0; i--)
    {
        const struct ModConfigItem *mod_item = mod_items + i;
        if (mod_item->state.mod_dir == 0)
            continue;

        char *fname = prepare_letter_one_file_path_for_mod(tmapidx, letter, fgroup, lvnum, mod_item);
        if (fname != NULL)
            return fname;
    }

    return NULL;
}

static char *prepare_letter_one_file_path(uint64_t tmapidx, char letter, LevelNumber lvnum, int64_t fgroup)
{
    // Note that this is the reverse mods direction
    char* fname = NULL;
    if (mods_conf.after_map_cnt > 0)
    {
        fname = prepare_letter_one_file_path_for_mod_list(tmapidx, letter, lvnum, fgroup, mods_conf.after_map_item, mods_conf.after_map_cnt);
        if (fname != NULL)
            return fname;
    }

    fname = prepare_file_fmtpath(fgroup, "map%05" PRIu64 ".tmap%c%03" PRIu64 ".dat",(uint64_t)lvnum, (int)(letter), (uint64_t)(tmapidx));
    if (LbFileExists(fname))
        return fname;

    if (mods_conf.after_campaign_cnt > 0)
    {
        fname = prepare_letter_one_file_path_for_mod_list(tmapidx, letter, lvnum, fgroup, mods_conf.after_campaign_item, mods_conf.after_campaign_cnt);
        if (fname != NULL)
            return fname;
    }

    fname = prepare_file_fmtpath(FGrp_CmpgConfig, "tmap%c%03" PRIu64 ".dat", (int)(letter), (uint64_t)(tmapidx));
    if (LbFileExists(fname))
        return fname;

    if (mods_conf.after_base_cnt > 0)
    {
        fname = prepare_letter_one_file_path_for_mod_list(tmapidx, letter, lvnum, fgroup, mods_conf.after_base_item, mods_conf.after_base_cnt);
        if (fname != NULL)
            return fname;
    }

    fname = prepare_file_fmtpath(FGrp_StdData, "tmap%c%03" PRIu64 ".dat", (int)(letter), (uint64_t)(tmapidx));
    return fname;
}

TbBool texture_pack_available(uint64_t tmapidx, LevelNumber lvnum, int64_t fgroup)
{
    char* fname = prepare_letter_one_file_path(tmapidx, 'a', lvnum, fgroup);
    return (fname != NULL) && LbFileExists(fname);
}

static TbBool load_letter_one_file(uint64_t tmapidx, char letter, void *dst, LevelNumber lvnum, int64_t fgroup)
{
    SYNCDBG(9,"Starting");

    char* fname = prepare_letter_one_file_path(tmapidx, letter, lvnum, fgroup);
    if (!LbFileExists(fname))
    {
        SYNCDBG(10, "Texture file \"%s\" doesn't exist.",fname);
        return false;
    }

    if (LbFileLoadAt(fname, dst) < 1024)
    {
        WARNMSG("Texture file \"%s\" can't be loaded or is too small.",fname);
        return false;
    }
    SYNCDBG(6, "Texture file \"%s\" succesfully loaded.", fname);
    return true;
}

TbBool load_texture_map_file(uint64_t tmapidx, LevelNumber lvnum, int64_t fgroup)
{
    SYNCDBG(7,"Starting");
    memset(block_mem, 130, sizeof(block_mem));
    if (!load_letter_one_file(tmapidx,'a', block_mem,lvnum,fgroup))
    {
        return false;
    }
    unsigned char *dst = block_mem + (TEXTURE_BLOCKS_STAT_COUNT_A * 32 * 32);
    load_letter_one_file(tmapidx,'b', dst, lvnum, fgroup);
    dst += (TEXTURE_BLOCKS_STAT_COUNT_B * 32 * 32);

    for (int64_t i = 0; i < TEXTURE_VARIATIONS_COUNT-1; i++)

    {
        load_letter_one_file(i,'a', dst, lvnum, fgroup);

        dst += (TEXTURE_BLOCKS_STAT_COUNT_A * 32 * 32);
        load_letter_one_file(i,'b', dst, lvnum, fgroup);
        dst += (TEXTURE_BLOCKS_STAT_COUNT_B * 32 * 32);

    }
    return true;
}

void scale_tmap2(int64_t texture_block_index, int64_t flags, int64_t fade_level, int64_t screen_x, int64_t screen_y, int64_t scaled_width, int64_t scaled_height)
{
    if ((scaled_width == 0) || (scaled_height == 0)) {
        return;
    }
    int64_t xstart;
    int64_t ystart;
    int64_t xend;
    int64_t yend;
    char orient;
    switch (flags)
    {
    case 0:
        xstart = 0;
        ystart = 0;
        xend = 2097151 / scaled_width;
        yend = 2097151 / scaled_height;
        orient = 0;
        break;
    case 0x10:
        xstart = 2097151;
        ystart = 0;
        xend = -2097151 / scaled_width;
        yend = 2097151 / scaled_height;
        orient = 0;
        break;
    case 0x20:
        xstart = 0;
        ystart = 2097151;
        xend = 2097151 / scaled_width;
        yend = -2097151 / scaled_height;
        orient = 0;
        break;
    case 0x30:
        xstart = 2097151;
        ystart = 2097151;
        xend = -2097151 / scaled_width;
        yend = -2097151 / scaled_height;
        orient = 0;
        break;
    case 0x40:
        ystart = 0;
        xstart = 0;
        yend = 2097151 / scaled_height;
        xend = 2097151 / scaled_width;
        orient = 1;
        break;
    case 0x50:
        ystart = 0;
        xstart = 2097151;
        yend = 2097151 / scaled_height;
        xend = -2097151 / scaled_width;
        orient = 1;
        break;
    case 0x60:
        ystart = 2097151;
        xstart = 0;
        yend = -2097151 / scaled_height;
        xend = 2097151 / scaled_width;
        orient = 1;
        break;
    case 0x70:
        xstart = 2097151;
        ystart = 2097151;
        yend = -2097151 / scaled_height;
        xend = -2097151 / scaled_width;
        orient = 1;
        break;
    default:
          return;
    }
    int64_t local_screen_x;
    int64_t local_screen_y;
    local_screen_x = screen_x;
    if (local_screen_x < 0)
    {
        scaled_width += local_screen_x;
        if (scaled_width < 0) {
            return;
        }
        xstart -= xend * local_screen_x;
        local_screen_x = 0;
    }
    if (local_screen_x + scaled_width > vec_window_width)
    {
        scaled_width = vec_window_width - local_screen_x;
        if (scaled_width < 0) {
            return;
        }
    }
    local_screen_y = screen_y;
    if (local_screen_y < 0)
    {
        scaled_height += local_screen_y;
        if (scaled_height < 0) {
            return;
        }
        ystart -= local_screen_y * yend;
        local_screen_y = 0;
    }
    if (local_screen_y + scaled_height > vec_window_height)
    {
        scaled_height = vec_window_height - local_screen_y;
        if (scaled_height < 0) {
            return;
        }
    }
    int64_t i;
    int64_t hlimits[480];
    int64_t wlimits[640];
    int64_t *xlim;
    int64_t *ylim;
    TbPixel *dbuf;
    unsigned char *block;
    const unsigned char *const palette = RendererGetActivePalette();
    /* Captured once per call, not per pixel -- the rasterizer's own render
     * target, as last configured by setup_vecs(); see SwDrawTarget.h. */
    TbPixel *const vscreen = SwTargetVecScreen();
    const uint64_t vscreen_width = SwTargetVecScreenWidth();
    if (!orient)
    {
        xlim = wlimits;
        for (i = scaled_width; i > 0; i--)
        {
            *xlim = xstart;
            xlim++;
            xstart += xend;
        }
        ylim = hlimits;
        for (i = scaled_height; i > 0; i--)
        {
            *ylim = ystart;
            ylim++;
            ystart += yend;
        }
        dbuf = &vscreen[local_screen_x + local_screen_y * vscreen_width];
        block = block_ptrs[texture_block_index];
        ylim = hlimits;
        int64_t px;
        int64_t py;
        int64_t srcx;
        int64_t srcy;
        TbPixel *d;
        if ( fade_level >= 0 )
        {
          for (py = scaled_height; py > 0; py--)
          {
              xlim = wlimits;
              d = dbuf;
              srcy = (((*ylim) & 0xFF0000u) >> 16);
              for (px = scaled_width; px > 0; px--)
              {
                srcx = (((*xlim) & 0xFF0000u) >> 16);
                xlim++;
                *d = render_shade(expand_indexed_pixel(block[(srcy << 8) + srcx], palette), fade_level);
                ++d;
              }
              dbuf += vscreen_width;
              ylim++;
          }
        } else
        {
          for (py = scaled_height; py > 0; py--)
          {
            xlim = wlimits;
            d = dbuf;
            srcy = (((*ylim) & 0xFF0000u) >> 16);
            for (px = scaled_width; px > 0; px--)
            {
              srcx = (((*xlim) & 0xFF0000u) >> 16);
              xlim++;
              *d = expand_indexed_pixel(block[(srcy << 8) + srcx], palette);
              ++d;
            }
            dbuf += vscreen_width;
            ylim++;
          }
        }
    } else
    {
        ylim = wlimits;
        for (i = scaled_height; i > 0; i--)
        {
          *ylim = ystart;
          ylim++;
          ystart += yend;
        }
        xlim = hlimits;
        for (i = scaled_width; i > 0; i--)
        {
          *xlim = xstart;
          xlim++;
          xstart += xend;
        }
        dbuf = &vscreen[local_screen_x + local_screen_y * vscreen_width];
        block = block_ptrs[texture_block_index];
        ylim = wlimits;
        int64_t px;
        int64_t py;
        int64_t srcx;
        int64_t srcy;
        TbPixel *d;
        if ( fade_level >= 0 )
        {
          for (py = scaled_height; py > 0; py--)
          {
              xlim = hlimits;
              d = dbuf;
              srcy = (((*ylim) & 0xFF0000u) >> 16);
              for (px = scaled_width; px > 0; px--)
              {
                srcx = (((*xlim) & 0xFF0000u) >> 16);
                xlim++;
                *d = render_shade(expand_indexed_pixel(block[(srcx << 8) + srcy], palette), fade_level);
                ++d;
              }
              dbuf += vscreen_width;
              ylim++;
          }
        } else
        {
          for (py = scaled_height; py > 0; py--)
          {
            xlim = hlimits;
            d = dbuf;
            srcy = (((*ylim) & 0xFF0000u) >> 16);
            for (px = scaled_width; px > 0; px--)
            {
              srcx = (((*xlim) & 0xFF0000u) >> 16);
              xlim++;
              *d = expand_indexed_pixel(block[(srcx << 8) + srcy], palette);
              ++d;
            }
            dbuf += vscreen_width;
            ylim++;
          }
        }
    }
}

void draw_texture(int64_t texture_x, int64_t texture_y, int64_t texture_width, int64_t texture_height, int64_t texture_block_index, int64_t flags, int64_t fade_level)
{
    scale_tmap2(texture_block_index, flags, fade_level, texture_x / pixel_size, texture_y / pixel_size, texture_width / pixel_size, texture_height / pixel_size);
}
/******************************************************************************/
