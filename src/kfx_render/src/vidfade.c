/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file vidfade.c
 *     Video fading routines.
 * @par Purpose:
 *     Helper functions for fading of video screen.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     16 Jul 2010 - 05 Nov 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "vidfade.h"

#include "globals.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include "bflib_datetm.h"
#include "bflib_video.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "config_settings.h"

#include "vidmode.h"
#include "player_data.h"
#include "packet_data.h"
#include "player_instances.h"
#include "config_keeperfx.h"
#include "kfx_sim_state.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
static TbBool lbAdvancedFade = true;
static int lbFadeDelay = 25;

unsigned char frontend_palette[768];
unsigned char palette_buf[PALETTE_SIZE];
/******************************************************************************/
// fade_in()/fade_out()/ProperFadePalette() removed per
// docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase 0 -- the
// between-screens fade they drove existed to mask loading time on
// decades-old hardware and is no longer needed. ProperForcedFadePalette()
// below is a separate function (still used by front_simple.c for cutscene
// playback) and is untouched.
void compute_fade_tables(struct TbColorTables *coltbl,unsigned char *spal,unsigned char *dpal)
{
    unsigned long i;
    unsigned long k;
    unsigned long r;
    unsigned long g;
    unsigned long b;
    SYNCMSG("Recomputing fade tables");
    // Intense fade to/from black - slower fade near black
    unsigned char* dst = coltbl->fade_tables;
    for (i=0; i < 32; i++)
    {
      for (k=0; k < 256; k++)
      {
        r = spal[3*k+0];
        g = spal[3*k+1];
        b = spal[3*k+2];
        *dst = LbPaletteFindColour(dpal, i * r >> 5, i * g >> 5, i * b >> 5);
        dst++;
      }
    }
    // Intense fade to/from black - faster fade part
    for (i=32; i < 192; i+=3)
    {
      for (k=0; k < 256; k++)
      {
        r = spal[3*k+0];
        g = spal[3*k+1];
        b = spal[3*k+2];
        *dst = LbPaletteFindColour(dpal, i * r >> 5, i * g >> 5, i * b >> 5);
        dst++;
      }
    }
    // Other fadings - between all the colors
    dst = coltbl->ghost;
    for (i=0; i < 256; i++)
    {
      // Reference colors
      unsigned long rr = spal[3 * i + 0];
      unsigned long rg = spal[3 * i + 1];
      unsigned long rb = spal[3 * i + 2];
      // Creating fades
      for (k=0; k < 256; k++)
      {
        r = dpal[3*k+0];
        g = dpal[3*k+1];
        b = dpal[3*k+2];
        *dst = LbPaletteFindColour(dpal, (rr+2*r) / 3, (rg+2*g) / 3, (rb+2*b) / 3);
        dst++;
      }
    }
}

void compute_alpha_table(unsigned char *alphtbl, unsigned char *spal, unsigned char *dpal, char dred, char dgreen, char dblue)
{
    int blendR = 0;
    int blendG = 0;
    int blendB = 0;
    // Every color alpha-blended with given values for 8 steps of intensity
    for (int nrow = 0; nrow < 8; nrow++)
    {
        for (int n = 0; n < 256; n++)
        {
            unsigned char* baseCol = &spal[3 * n];
            int valR = blendR + baseCol[0];
            if (valR >= 63)
              valR = 63;
            else if (valR < 0)
              valR = 0;
            int valG = blendG + baseCol[1];
            if (valG >= 63)
              valG = 63;
            else if (valG < 0)
              valG = 0;
            int valB = blendB + baseCol[2];
            if (valB >= 63)
              valB = 63;
            else if (valB < 0)
              valB = 0;

            unsigned char c = LbPaletteFindColour(dpal, valR, valG, valB);
            alphtbl[nrow*256 + n] = c;
        }
        blendR += dred;
        blendG += dgreen;
        blendB += dblue;
    }
}

void compute_alpha_tables(struct TbAlphaTables *alphtbls,unsigned char *spal,unsigned char *dpal)
{
    SYNCMSG("Recomputing alpha tables");
    {
        for (int n = 0; n < 256; n++)
        {
            alphtbls->black[n] = 144;
        }
    }
    // Every color alpha-blended with shade of white
    compute_alpha_table(alphtbls->white,  spal, dpal, 4, 4, 4);
    // Every color alpha-blended with yellow
    compute_alpha_table(alphtbls->yellow, spal, dpal, 6, 4, 0);
    // Every color alpha-blended with red
    compute_alpha_table(alphtbls->red,    spal, dpal, 6, 1, 1);
    // Every color alpha-blended with blue
    compute_alpha_table(alphtbls->blue,   spal, dpal, 2, 2, 6);
    // Every color alpha-blended with green
    compute_alpha_table(alphtbls->green,  spal, dpal, 2, 6, 2);
    // Every color alpha-blended with purple
    compute_alpha_table(alphtbls->purple, spal, dpal, 3, 0, 3);
    // Every color alpha-blended with black
    compute_alpha_table(alphtbls->black,  spal, dpal,-2,-2,-2);
    // Every color alpha-blended with orange
    compute_alpha_table(alphtbls->orange, spal, dpal, 6, 3, 1);
}

void compute_rgb2idx_table(TbRGBColorTable ctab,unsigned char *spal)
{
    SYNCMSG("Recomputing rgb-to-index tables");
    int scaler = (1 << 6) / COLOUR_TABLE_DIMENSION;
    for (int valR = 0; valR < COLOUR_TABLE_DIMENSION; valR++)
    {
        for (int valG = 0; valG < COLOUR_TABLE_DIMENSION; valG++)
        {
            for (int valB = 0; valB < COLOUR_TABLE_DIMENSION; valB++)
            {
                unsigned char c = LbPaletteFindColour(spal, scaler * valR + (scaler-1),
                    scaler * valG + (scaler-1), scaler * valB + (scaler-1));
                ctab[valR][valG][valB] = c;
            }
        }
    }
}

void ProperForcedFadePalette(unsigned char *pal, long fade_steps, enum TbPaletteFadeFlag flg)
{
    if (flg == Lb_PALETTE_FADE_OPEN)
    {
        LbPaletteFade(pal, fade_steps, flg);
        return;
    }
    if (lbAdvancedFade)
    {
        TbClockMSec latest_loop_time = LbTimerClock();
        while (LbPaletteFade(pal, fade_steps, Lb_PALETTE_FADE_OPEN) < fade_steps)
        {
          latest_loop_time += lbFadeDelay;

          if (flag_is_set(start_params.startup_flags, (SFlg_Legal|SFlg_FX))) {
              LbSleepUntil(latest_loop_time);
          }
        }
    } else
    if (pal != NULL)
    {
        RendererPaletteSet(pal);
    } else
    {
        memset(palette_buf, 0, sizeof(palette_buf));
        RendererPaletteSet(palette_buf);
    }
}

long PaletteFadePlayer(struct PlayerInfo *player)
{
    if (!is_my_player(player))
        return 0;
    long i;
    unsigned char palette[PALETTE_SIZE];
    // Find the fade step
    if ((local_state.palette_fade_step_pain != 0) && (local_state.palette_fade_step_possession != 0))
    {
        i = 12 * (local_state.palette_fade_step_pain - 1) + 10 * (local_state.palette_fade_step_possession - 1);
  } else
  if (local_state.palette_fade_step_possession != 0)
  {
    i = 2 * (5 * (local_state.palette_fade_step_possession-1));
  } else
  if (local_state.palette_fade_step_pain != 0)
  {
    i = 4 * (3 * (local_state.palette_fade_step_pain-1));
  } else
  { // both are == 0 - no fade
    return 0;
  }
  if (i >= 120)
    i = 120;
  long step = 120 - i;
  // Create the new palette
  for (i=0; i < PALETTE_COLORS; i++)
  {
      unsigned char* src = &local_state.main_palette[3 * i];
      unsigned char* dst = &palette[3 * i];
      unsigned long pix = ((step * (((long)src[0]) - 63)) / 120) + 63;
      if (pix > 63)
          pix = 63;
      dst[0] = pix;
      pix = (step * ((long)src[1])) / 120;
      if (pix > 63)
          pix = 63;
      dst[1] = pix;
      pix = (step * ((long)src[2])) / 120;
      if (pix > 63)
          pix = 63;
      dst[2] = pix;
  }
  // Update the fade step
  if (local_state.palette_fade_step_pain > 0)
    local_state.palette_fade_step_pain--;
  if ((local_state.palette_fade_step_possession == 0) || (player->instance_num == PI_UnusedSlot18) || (player->instance_num == PI_UnusedSlot17))
  {
  } else
  if ((player->instance_num == PI_DirctCtrl) || (player->instance_num == PI_PsngrCtrl))
  {
    if (local_state.palette_fade_step_possession <= 12)
      local_state.palette_fade_step_possession++;
  } else
  {
    if (local_state.palette_fade_step_possession > 0)
      local_state.palette_fade_step_possession--;
  }
  // Set the palette to screen
  LbScreenWaitVbi();
  RendererPaletteSet(palette);
  return step;
}

void PaletteApplyPainToPlayer(struct PlayerInfo *player, long intense)
{
    if (!is_my_player(player))
        return;
    long i = local_state.palette_fade_step_pain + intense;
    if (i < 1)
        i = 1;
    else
    if (i > 10)
        i = 10;
    local_state.palette_fade_step_pain = i;
}

void PaletteSetUserPalette(NetUserId user, unsigned char *pal)
{
    struct UserState* ustate = get_user_state(user);
    if (user_state_invalid(ustate))
        return;
    if (pal == blue_palette) // if the requested palette is the Freeze palette
    {
      if ((ustate->additional_flags & UsrAF_FreezePaletteIsActive) != 0)
        return; // Freeze palette is already on
      ustate->additional_flags |= UsrAF_FreezePaletteIsActive; // flag Freeze palette is active
    } else
    {
      ustate->additional_flags &= ~UsrAF_FreezePaletteIsActive; // flag Freeze palette is not active
    }
    if (user != get_local_user())
        return;
    if ( (local_state.lens_palette == 0) || ((pal != local_state.main_palette) && (pal == local_state.lens_palette)) )
    {
        local_state.main_palette = pal;
        local_state.palette_fade_step_pain = 0;
        local_state.palette_fade_step_possession = 0;
        LbScreenWaitVbi();
        RendererPaletteSet(pal);
    }
}

TbBool set_gamma(char corrlvl, TbBool do_set)
{
    char *fname;
    TbBool result = true;
    if (corrlvl < 0)
      corrlvl = 0;
    else
    if (corrlvl > 4)
      corrlvl = 4;
    settings.gamma_correction = corrlvl;
    fname=prepare_file_fmtpath(FGrp_StdData,"pal%05d.dat",settings.gamma_correction);
    if (!LbFileExists(fname))
    {
      WARNMSG("Palette file \"%s\" doesn't exist.", fname);
      result = false;
    }
    if (result)
    {
      result = (LbFileLoadAt(fname, engine_palette) != -1);
    }
    if ((result) && (do_set))
    {
      PaletteSetUserPalette(get_local_user(), engine_palette);
    }
    if (!result)
      ERRORLOG("Can't load palette file.");
    return result;
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
