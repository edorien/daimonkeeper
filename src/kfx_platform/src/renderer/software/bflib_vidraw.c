/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw.c
 *     Graphics canvas drawing library.
 * @par Purpose:
 *    Screen drawing routines; draws half-transparent boxes and other elements.
 * @par Comment:
 *     Medium level library, draws on screen buffer used in bflib_video.
 *     Used for drawing screen components.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 10 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/software/SwDrawTarget.h"
#include "renderer/RendererManager.h"
#include "renderer/WorldFrame.h" // WorldFrameSpriteMode
#include "bflib_vidraw.h"

#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stddef.h>
#include "globals.h"
#include "bflib_video.h"
#include "bflib_sprite.h"
#include "bflib_mouse.h"
#include "bflib_render.h"
#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
struct TbSpriteDrawData {
    char *sp;
    int64_t Wd;
    int64_t Ht;
    TbPixel *r;
    int64_t nextRowDelta;
    int64_t startShift;
    TbBool mirror;
};
/******************************************************************************/
int64_t xsteps_array[2*SPRITE_SCALING_XSTEPS];
int64_t ysteps_array[2*SPRITE_SCALING_YSTEPS];

TbPixel *poly_screen;
TbPixel *vec_screen;
unsigned char *vec_map;
uint64_t vec_screen_width;
int64_t vec_window_width;
int64_t vec_window_height;
unsigned char *dither_map;
unsigned char *dither_end;
TbPixel *lbSpriteReMapPtr;
TbPixel lbSpriteRemapTable[256];
int64_t scale_up;
/* What the current lbSpriteRemapTable holds: a plain shade table (the per-pixel-lit capture path can
 * undo it and shade per pixel instead) or something else (tint, flash, ghost). */
static int lb_remap_is_shade = 0;
/******************************************************************************/

void SetupSpriteRemapGhost(uint8_t ref_index, uint8_t strength)
{
    lb_remap_is_shade = 0;
    const unsigned char *palette = RendererGetActivePalette();
    /* resolve_indexed_pixel(), not expand_indexed_pixel(): ref_index is a tint
     * reference colour, not a sprite texel, so palette index 0 is a normal
     * opaque colour here -- never "transparent". */
    TbPixel ref = resolve_indexed_pixel(ref_index, palette);
    const int64_t inv = 255 - strength;
    for (int64_t i = 0; i < 256; i++) {
        TbPixel texel = expand_indexed_pixel((uint8_t)i, palette);
        /* strength == SPRITE_TINT_LEGACY (85) reproduces render_ghost_blend()'s
         * exact 1/3 weight (255/3); higher values blend more of the tint in. */
        lbSpriteRemapTable[i] = TbPixel_RGBA(
            (uint8_t)((ref.r * strength + texel.r * inv) / 255),
            (uint8_t)((ref.g * strength + texel.g * inv) / 255),
            (uint8_t)((ref.b * strength + texel.b * inv) / 255),
            255);
    }
    lbSpriteReMapPtr = lbSpriteRemapTable;
}

void SetupSpriteRemapShade(int64_t shade)
{
    lb_remap_is_shade = 1;
    const unsigned char *palette = RendererGetActivePalette();
    for (int64_t i = 0; i < 256; i++) {
        lbSpriteRemapTable[i] = render_shade(expand_indexed_pixel((uint8_t)i, palette), shade);
    }
    lbSpriteReMapPtr = lbSpriteRemapTable;
}

void SetupSpriteRemapWhiteFlash(void)
{
    lb_remap_is_shade = 0;
    const unsigned char *palette = RendererGetActivePalette();
    for (int64_t i = 0; i < 256; i++) {
        lbSpriteRemapTable[i] = render_flash_blend(expand_indexed_pixel((uint8_t)i, palette), 48, 48, 48);
    }
    lbSpriteReMapPtr = lbSpriteRemapTable;
}

void SetupSpriteRemapRedFlash(void)
{
    lb_remap_is_shade = 0;
    const unsigned char *palette = RendererGetActivePalette();
    for (int64_t i = 0; i < 256; i++) {
        lbSpriteRemapTable[i] = render_flash_blend(expand_indexed_pixel((uint8_t)i, palette), 20, -10, -10);
    }
    lbSpriteReMapPtr = lbSpriteRemapTable;
}
/******************************************************************************/
/**  Prints horizontal or vertical line on current graphics window.
 *  Does no screen locking - screen must be lock before and unlocked
 *  after a call to this function.
 *
 * @param xpos1
 * @param ypos1
 * @param xpos2
 * @param ypos2
 * @param colour
 */
void LbDrawHVLine(int64_t xpos1, int64_t ypos1, int64_t xpos2, int64_t ypos2, TbPixel colour)
{
  int64_t width_max = SwTargetWindowWidth() - 1;
  int64_t height_max = SwTargetWindowHeight() - 1;
  if ( xpos1 > xpos2 )
  { //Switching & clipping x coordinates
    if (xpos1 < 0) return;
    if (xpos2 > width_max) return;
    int64_t nxpos1=xpos2;
    int64_t nxpos2=xpos1;
    if ( xpos2 < 0 )
      nxpos1 = 0;
    if ( xpos1 > width_max )
      nxpos2 = SwTargetWindowWidth() - 1;
    xpos1 = nxpos1;
    xpos2 = nxpos2;
  } else
  { //Clipping x coordinates
    if (xpos2 < 0) return;
    if (xpos1 > width_max) return;
    if ( xpos1 < 0 )
      xpos1 = 0;
    if ( xpos2 > width_max )
      xpos2 = SwTargetWindowWidth() - 1;
  }
  if ( ypos1 > ypos2 )
  { //Switching & clipping y coordinates
    if (ypos1 < 0) return;
    if (ypos2 > height_max) return;
    int64_t nxpos1=xpos2;
    int64_t nxpos2=xpos1;
    if ( ypos2 < 0 )
      nxpos1 = 0;
    if ( ypos1 > height_max )
      nxpos2 = SwTargetWindowHeight() - 1;
    ypos1 = nxpos1;
    ypos2 = nxpos2;
  } else
  { //Clipping y coordinates
    if (ypos2 < 0) return;
    if (ypos1 > height_max) return;
    if (ypos1 < 0)
      ypos1 = 0;
    if ( ypos2 > height_max )
      ypos2 = SwTargetWindowHeight() - 1;
  }
  if (!(RendererGetDrawFlags() & (Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_TRANSPAR8)) &&
      SwCaptureRect(xpos1, ypos1, xpos2 - xpos1 + 1, ypos2 - ypos1 + 1, colour))
    return;
  //And now to drawing
  TbPixel *screen_ptr = SwTargetGraphicsWindowPtr() + xpos1 +
          SwTargetScanline() * ypos1;
  if ( xpos2 == xpos1 )
  {//Vertical line
    int64_t idx = ypos2 - ypos1 + 1;
    if (RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4)
    {
      do {
        *screen_ptr = render_ghost_blend(colour, *screen_ptr);
        screen_ptr += SwTargetScanline();
        idx--;
      } while ( idx>0 );
    } else
    {
      if (RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8)
      {
        do
        {
          *screen_ptr = render_ghost_blend_2(colour, *screen_ptr);
          screen_ptr += SwTargetScanline();
          idx--;
        }
        while ( idx>0 );
      } else
      {
        do
        {
          *screen_ptr = colour;
          screen_ptr += SwTargetScanline();
          idx--;
        }
        while ( idx>0 );
      }
    }
  } else
  {//Horizontal line
    int64_t idx = xpos2 - xpos1 + 1;
    if (RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4)
    {
      do
      {
        *screen_ptr = render_ghost_blend(colour, *screen_ptr);
        screen_ptr++;
        idx--;
      }
      while ( idx>0 );
    }
    else
    {
      if (RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8)
      {
        do
        {
          *screen_ptr = render_ghost_blend_2(colour, *screen_ptr);
          screen_ptr++;
          idx--;
        }
        while ( idx>0 );
      }
      else
      {
        while ( idx>0 )
        {
          *screen_ptr = colour;
          screen_ptr++;
          idx--;
        }
      }
    }
  }
}

/** Draws a filled box on current graphic window.
 *  Performs clipping if needed to stay inside the window.
 *  Does no screen locking.
 *
 * @param x
 * @param y
 * @param width
 * @param height
 * @param colour
 */
void LbDrawBoxClip(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour)
{
  int64_t ypos = y;
  //Checking and clipping coordinates
  if ( y >= SwTargetWindowHeight() )
      return;
  if ( y < 0 )
  {
      height += y;
      ypos = 0;
  }
  if ( (int64_t)(height + ypos) > SwTargetWindowHeight() )
      height -= height + ypos - SwTargetWindowHeight();
  if ( (int64_t)height <= 0 )
      return;

  const int64_t ypos_clipped = ypos;
  ypos = SwTargetScanline() * (SwTargetWindowY() + ypos);
  int64_t xpos = x;
  if ( x >= SwTargetWindowWidth() )
      return;
  if ( x < 0 )
  {
      width += x;
      xpos = 0;
  }
  if ( (int64_t)(width + xpos) > SwTargetWindowWidth() )
      width -= width + xpos - SwTargetWindowWidth();
  if ( (int64_t)width <= 0 )
      return;
  if (!(RendererGetDrawFlags() & (Lb_SPRITE_TRANSPAR4 | Lb_SPRITE_TRANSPAR8)) &&
      SwCaptureRect(xpos, ypos_clipped, (int64_t)width, (int64_t)height, colour))
      return;
  //And now let's start drawing
  TbPixel *screen_ptr = &SwTargetWScreen()[SwTargetWindowX()] + xpos + ypos;
  uint64_t idxh = height;
  //Space between lines in video buffer
  uint64_t screen_delta = SwTargetScanline() - width;
  if ( RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4 )
  {
      do {
          uint64_t idxw = width;
          do {
                *screen_ptr = render_ghost_blend(colour, *screen_ptr);
                screen_ptr++;
                idxw--;
          } while ( idxw>0 );
          screen_ptr += screen_delta;
          idxh--;
      } while ( idxh>0 );
  } else
  if ( RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8 )
  {
      do {
            uint64_t idxw = width;
            do {
              *screen_ptr = render_ghost_blend_2(colour, *screen_ptr);
              screen_ptr++;
              idxw--;
            } while ( idxw>0 );
            screen_ptr += screen_delta;
            idxh--;
      } while ( idxh>0 );
  } else
  {
      do {
            uint64_t idxw = width;
            do {
              *screen_ptr = colour;
              screen_ptr++;
              idxw--;
            } while ( idxw>0 );
            screen_ptr += screen_delta;
            idxh--;
      } while ( idxh>0 );
  }
}

/** Draws a rectangular box on current graphics window.
 *  Does no screen locking.
 *
 * @param x Box left border coordinate.
 * @param y Box top border coordinate.
 * @param width Box width.
 * @param height Box height.
 * @param colour Colour index used to draw the box.
 * @return If wrong dimensions gives Lb_FAIL. On success gives Lb_SUCCESS.
 */
TbResult LbDrawBox(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour)
{
    return RendererDrawBox(x, y, width, height, colour);
}

TbResult LbDrawBoxImmediate(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour)
{
    if (RendererGetDrawFlags() & Lb_SPRITE_OUTLINE)
    {
        if ((width < 1) || (height < 1))
          return Lb_FAIL;
        LbDrawHVLine(x, y, width + x - 1, y, colour);
        LbDrawHVLine(x, height + y - 1, width + x - 1, height + y - 1, colour);
        if (height > 2)
        {
          LbDrawHVLine(x, y + 1, x, height + y - 2, colour);
          LbDrawHVLine(width + x - 1, y + 1, width + x - 1, height + y - 2, colour);
        }
    } else
    {
        LbDrawBoxClip(x, y, width, height, colour);
    }
    return Lb_SUCCESS;
}

/** Internal function used to prepare sprite drawing.
 *  Fills TbSpriteDrawData struct with values accepted by drawing routines.
 *
 * @param spd The TbSpriteDrawData struct to be filled.
 * @param x Drawing position x coordinate.
 * @param y Drawing position y coordinate.
 * @param spr Sprite to be drawn.
 * @return Gives Lb_SUCCESS if the data was prepared.
 */
static inline TbResult LbSpriteDrawPrepare(struct TbSpriteDrawData *spd, int64_t x, int64_t y, const struct TbSprite *spr)
{
    if (spr == NULL)
    {
        SYNCDBG(19,"NULL sprite");
        return Lb_FAIL;
    }
    if ((spr->SWidth < 1) || (spr->SHeight < 1))
    {
        SYNCDBG(19,"Zero size sprite (%" PRId64 ",%" PRId64 ")",(int64_t)(spr->SWidth),(int64_t)(spr->SHeight));
        return Lb_OK;
    }
    if ((SwTargetWindowWidth() == 0) || (SwTargetWindowHeight() == 0))
    {
        SYNCDBG(19,"Invalid graphics window dimensions");
        return Lb_FAIL;
    }
    x += SwTargetWindowX();
    y += SwTargetWindowY();
    int64_t left;
    int64_t right;
    int64_t top;
    int64_t btm;
    int64_t sprWd = spr->SWidth;
    int64_t sprHt = spr->SHeight;
    //Coordinates range checking - x coords
    int64_t delta;
    delta = SwTargetWindowX() - x;
    if (delta <= 0)
    {
        left = 0;
    } else
    {
        if (sprWd <= delta)
            return Lb_OK;
        left = delta;
    }
    delta = x + sprWd - (SwTargetWindowWidth()+SwTargetWindowX());
    if ( delta <= 0 )
    {
        right = sprWd;
    } else
    {
        if (sprWd <= delta)
            return Lb_OK;
        right = sprWd - delta;
    }
    //Coordinates range checking - y coords
    delta = SwTargetWindowY() - y;
    if (delta <= 0)
    {
      top = 0;
    } else
    {
      if (sprHt <= delta)
        return Lb_OK;
      top = delta;
    }
    delta = y + sprHt - (SwTargetWindowHeight() + SwTargetWindowY());
    if (y + sprHt - (SwTargetWindowHeight() + SwTargetWindowY()) <= 0)
    {
      btm = sprHt;
    } else
    {
      if (sprHt <= delta)
        return Lb_OK;
      btm = sprHt - delta;
    }
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_VERTIC) != 0)
    {
        spd->r = &SwTargetWScreen()[x + (y+btm-1)*SwTargetScanline() + left];
        spd->nextRowDelta = -SwTargetScanline();
        int64_t tmp_btm = btm;
        btm = sprHt - top;
        top = sprHt - tmp_btm;
    } else
    {
        spd->r = &SwTargetWScreen()[x + (y+top)*SwTargetScanline() + left];
        spd->nextRowDelta = SwTargetScanline();
    }
    spd->Ht = btm - top;
    spd->Wd = right - left;
    spd->sp = (char *)spr->Data;
    SYNCDBG(19,"Sprite coords X=%" PRId64 "...%" PRId64 " Y=%" PRId64 "...%" PRId64 " data=%p",(int64_t)(left),(int64_t)(right),(int64_t)(top),(int64_t)(btm),spd->sp);
    int64_t htIndex;
    if ( top )
    {
        htIndex = top;
        while ( 1 )
        {
            char chr = *(spd->sp);
            while (chr > 0)
            {
                spd->sp += chr + 1;
                chr = *(spd->sp);
            }
            spd->sp++;
            if (chr == 0)
            {
              htIndex--;
              if (htIndex <= 0) break;
            }
        }
    }
    SYNCDBG(19,"Drawing sprite of size (%" PRId64 ",%" PRId64 ")",(int64_t)spd->Ht,(int64_t)spd->Wd);
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
    {
        spd->r += spd->Wd - 1;
        spd->mirror = true;
        int64_t tmpwidth = spr->SWidth;
        int64_t tmpright = right;
        right = tmpwidth - left;
        spd->startShift = tmpwidth - tmpright;
    } else
    {
        spd->mirror = false;
        spd->startShift = left;
    }
    return Lb_SUCCESS;
}

/** Internal function used to skip some of sprite data before drawing is started.
 *
 * @param sp Sprite data buffer pointer.
 * @param r Output buffer pointer.
 * @param remaining_width Width to be drawn.
 * @param left Width of the area to skip.
 */
static inline int64_t LbSpriteDrawLineSkipLeft(const char **sp, int64_t *remaining_width, int64_t left)
{
    char schr;
    // Cut the left side of the sprite, if needed
    if (left != 0)
    {
        int64_t lpos = left;
        while (lpos > 0)
        {
            schr = *(*sp);
            // Value > 0 means count of filled characters, < 0 means skipped characters
            // Equal to 0 means EOL
            if (schr == 0)
            {
              (*remaining_width) = 0;
              break;
            }
            if (schr < 0)
            {
                if (-schr <= lpos)
                {
                    lpos += schr;
                    (*sp)++;
                } else
                // If we have more empty spaces than we want to skip
                {
                    // Return remaining part to skip, so that we can do it outside
                    return lpos;
                }
            } else
            //if (schr > 0)
            {
                if (schr <= lpos)
                // If we have less than we want to skip
                {
                    lpos -= schr;
                    (*sp) += (*(*sp)) + 1;
                } else
                // If we have more characters than we want to skip
                {
                    // Return remaining part to skip, so that we can draw it
                    return lpos;
                }
            }
        }
    }
    return 0;
}

/** Internal function used to skip to next line after drawing a requested area.
 *
 * @param sp Sprite data buffer pointer.
 * @param remaining_width Width difference after draw.
 */
static inline void LbSpriteDrawLineSkipToEol(const char **sp, int64_t *remaining_width)
{
    char schr;
    if ((*remaining_width) <= 0)
    {
      do {
        schr = *(*sp);
        while (schr > 0)
        {
          (*sp) += schr+1;
          schr = *(*sp);
        }
        (*sp)++;
      } while (schr);
    } else
    {
        (*sp)++;
    }
}

/** Internal function used to draw part of sprite line.
 *
 * @param buf_out
 * @param buf_inp
 * @param buf_len
 * @param mirror
 */
static inline void LbDrawBufferTranspr(TbPixel **buf_out,const char *buf_inp,
        const int64_t buf_len, const TbBool mirror)
{
  int64_t i;
  const unsigned char *palette = RendererGetActivePalette();
  TbPixel val;
  if ( mirror )
  {
    if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
    {
        for (i=0; i<buf_len; i++ )
        {
            val = expand_indexed_pixel(*(const unsigned char *)buf_inp, palette);
            **buf_out = render_ghost_blend(val, **buf_out);
            buf_inp++;
            (*buf_out)--;
        }
    } else
    {
        for (i=0; i<buf_len; i++ )
        {
            val = expand_indexed_pixel(*(const unsigned char *)buf_inp, palette);
            **buf_out = render_ghost_blend_2(val, **buf_out);
            buf_inp++;
            (*buf_out)--;
        }
    }
  } else
  {
    if ( RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4 )
    {
        for (i=0; i<buf_len; i++ )
        {
            val = expand_indexed_pixel(*(const unsigned char *)buf_inp, palette);
            **buf_out = render_ghost_blend(val, **buf_out);
            buf_inp++;
            (*buf_out)++;
        }
    } else
    {
        for (i=0; i<buf_len; i++ )
        {
            val = expand_indexed_pixel(*(const unsigned char *)buf_inp, palette);
            **buf_out = render_ghost_blend_2(val, **buf_out);
            buf_inp++;
            (*buf_out)++;
        }
    }
  }
}

/** Internal function used to draw part of sprite line.
 *  Draws by copying pixels from input buffer into output buffer, without any kind of blending
 *  or altering the color values. Palette for both buffers must be identical.
 *
 * @param buf_out
 * @param buf_inp
 * @param buf_len
 * @param mirror
 */
static inline void LbDrawBufferSolid(TbPixel **buf_out,const char *buf_inp,
        const int64_t buf_len, const TbBool mirror)
{
    int64_t i;
    const unsigned char *palette = RendererGetActivePalette();
    if ( mirror )
    {
        for (i=0; i < buf_len; i++)
        {
            **buf_out = expand_indexed_pixel(*(const unsigned char *)buf_inp, palette);
            buf_inp++;
            (*buf_out)--;
        }
    } else
    {
        for (i=0; i < buf_len; i++)
        {
            **buf_out = expand_indexed_pixel(*(const unsigned char *)buf_inp, palette);
            buf_inp++;
            (*buf_out)++;
        }
    }
}

/** Internal function used to draw part of sprite line with single colour.
 *
 * @param buf_scr
 * @param colour
 * @param buf_len
 * @param mirror
 */
static inline void LbDrawBufferOneColour(TbPixel **buf_out,const TbPixel colour,
        const int64_t buf_len, const TbBool mirror)
{
    int64_t i;
    if ( mirror )
    {
        if ( RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4 )
        {
            for (i=0; i<buf_len; i++ )
            {
                **buf_out = render_ghost_blend(colour, **buf_out);
                (*buf_out)--;
            }
        } else
        {
            for (i=0; i<buf_len; i++ )
            {
                **buf_out = render_ghost_blend_2(colour, **buf_out);
                (*buf_out)--;
            }
        }
    } else
    {
        if ( RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4 )
        {
            for (i=0; i<buf_len; i++ )
            {
                **buf_out = render_ghost_blend(colour, **buf_out);
                (*buf_out)++;
            }
        } else
        {
            for (i=0; i<buf_len; i++ )
            {
                **buf_out = render_ghost_blend_2(colour, **buf_out);
                (*buf_out)++;
            }
        }
    }
}

/** Internal function used to draw part of sprite line with single colour.
 *
 * @param buf_out
 * @param colour
 * @param buf_len
 */
static inline void LbDrawBufferOneColorSolid(TbPixel **buf_out,const TbPixel colour,
        const int64_t buf_len, const TbBool mirror)
{
    int64_t i;
    if ( mirror )
    {
        for (i=0; i < buf_len; i++)
        {
            **buf_out = colour;
            (*buf_out)--;
        }
    } else
    {
        for (i=0; i < buf_len; i++)
        {
            **buf_out = colour;
            (*buf_out)++;
        }
    }
}

/** Internal routine to draw one line of a transparent sprite.
 *
 * @param sp
 * @param r
 * @param remaining_width
 * @param lpos
 * @param mirror
 */
static inline void LbSpriteDrawLineTranspr(const char **sp, TbPixel **r, int64_t *remaining_width,
    int64_t lpos, const TbBool mirror)
{
    char schr;
    unsigned char drawOut;
    // Draw any unfinished block, which should be only partially visible
    if (lpos > 0)
    {
        schr = *(*sp);
        if (schr < 0)
        {
            drawOut = -schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            if ( mirror )
                (*r) -= drawOut;
            else
                (*r) += drawOut;
            (*sp)++;

        } else
        {
            // Draw the part of current block which exceeds value of 'lpos'
            drawOut = schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            LbDrawBufferTranspr(r,(*sp)+(lpos+1),drawOut,mirror);
            // Update positions and break the skipping loop
            (*sp) += (*(*sp)) + 1;
        }
        (*remaining_width) -= drawOut;
    }
    // Draw the visible part of a sprite
    while ((*remaining_width) > 0)
    {
        schr = *(*sp);
        if (schr == 0)
        { // EOL, breaking line loop
            break;
        }
        if (schr < 0)
        { // Skipping some pixels
            (*remaining_width) += schr;
            if ( mirror )
               (*r) += *(*sp);
            else
               (*r) -= *(*sp);
            (*sp)++;
        } else
        //if ( schr > 0 )
        { // Drawing some pixels
            drawOut = schr;
            if (drawOut >= (*remaining_width))
                drawOut = (*remaining_width);
            LbDrawBufferTranspr(r,(*sp)+1,drawOut,mirror);
            (*remaining_width) -= schr;
            (*sp) += (*(*sp)) + 1;
        }
    } //end while
}

static inline TbResult LbSpriteDrawTranspr(const char *sp,int64_t sprWd,int64_t sprHt,TbPixel *r,
    int64_t nextRowDelta,int64_t left,const TbBool mirror)
{
    TbPixel *nextRow;
    int64_t htIndex;
    nextRow = &(r[nextRowDelta]);
    htIndex = sprHt;
    // For all lines of the sprite
    while (1)
    {
        int64_t x1;
        int64_t lpos;
        x1 = sprWd;
        // Skip the pixels left before drawing area
        lpos = LbSpriteDrawLineSkipLeft(&sp,&x1,left);
        // Do the actual drawing
        LbSpriteDrawLineTranspr(&sp,&r,&x1,lpos,mirror);
        // Go to next line
        htIndex--;
        if (htIndex == 0)
            return Lb_SUCCESS;
        LbSpriteDrawLineSkipToEol(&sp,&x1);
        r = nextRow;
        nextRow += nextRowDelta;
    } //end while
    return Lb_SUCCESS;
}

/** Internal routine to draw one line of a solid sprite.
 *  Supports only mirrored sprites.
 *
 * @param sp
 * @param r
 * @param remaining_width
 * @param lpos
 * @param mirror
 */
static inline void LbSpriteDrawLineSolid(const char **sp, TbPixel **r, int64_t *remaining_width, int64_t lpos, const TbBool mirror)
{
    char schr;
    unsigned char drawOut;
    // Draw any unfinished block, which should be only partially visible
    if (lpos > 0)
    {
        schr = *(*sp);
        if (schr < 0)
        {
            drawOut = -schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            (*r) -= drawOut;
            (*sp)++;
        } else
        {
            // Draw the part of current block which exceeds value of 'lpos'
            drawOut = schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            LbDrawBufferSolid(r,(*sp)+(lpos+1),drawOut,mirror);
            // Update positions and break the skipping loop
            (*sp) += (*(*sp)) + 1;
        }
        (*remaining_width) -= drawOut;
    }
    // Draw the visible part of a sprite
    while ((*remaining_width) > 0)
    {
        schr = *(*sp);
        if (schr == 0)
        { // EOL, breaking line loop
            break;
        }
        if (schr < 0)
        { // Skipping some pixels
            (*remaining_width) += schr;
            (*r) += *(*sp);
            (*sp)++;
        } else
        //if ( schr > 0 )
        { // Drawing some pixels
            drawOut = schr;
            if (drawOut >= (*remaining_width))
                drawOut = (*remaining_width);
            LbDrawBufferSolid(r,(*sp)+1,drawOut,mirror);
            (*remaining_width) -= schr;
            (*sp) += (*(*sp)) + 1;
        }
    } //end while
}

/** Solid sprite drawing routine. Optimized for mirrored ones, without transparency.
 *
 * @param sp
 * @param sprWd
 * @param sprHt
 * @param r
 * @param nextRowDelta
 * @param left
 * @param mirror
 * @return
 */
static inline TbResult LbSpriteDrawSolid(const char *sp,int64_t sprWd,int64_t sprHt,TbPixel *r,
    int64_t nextRowDelta,int64_t left,const TbBool mirror)
{
    TbPixel *nextRow;
    int64_t htIndex;
    nextRow = &(r[nextRowDelta]);
    htIndex = sprHt;
    // For all lines of the sprite
    while (1)
    {
        int64_t x1;
        int64_t lpos;
        x1 = sprWd;
        // Skip the pixels left before drawing area
        lpos = LbSpriteDrawLineSkipLeft(&sp,&x1,left);
        // Do the actual drawing
        LbSpriteDrawLineSolid(&sp,&r,&x1,lpos,mirror);
        // Go to next line
        htIndex--;
        if (htIndex == 0)
            return Lb_SUCCESS;
        LbSpriteDrawLineSkipToEol(&sp,&x1);
        r = nextRow;
        nextRow += nextRowDelta;
    } //end while
    return Lb_SUCCESS;
}

static inline void LbSpriteDrawLineFastCpy(const char **sp, TbPixel **r, int64_t *remaining_width, int64_t lpos)
{
    char schr;
    unsigned char drawOut;
    if (lpos > 0)
    {
        // Draw the part of current block which exceeds value of 'lpos'
        schr = *(*sp);
        if (schr < 0)
        {
            drawOut = -schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            (*r) += drawOut;
            (*sp)++;
        } else
        {
            drawOut = schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            LbDrawBufferSolid(r, (*sp)+(lpos+1), drawOut, false);
            (*sp) += (*(*sp)) + 1;
        }
        (*remaining_width) -= drawOut;
    }
    // Draw the visible part of a sprite
    while ((*remaining_width) > 0)
    {
        schr = *(*sp);
        if (schr == 0)
        { // EOL, breaking line loop
            break;
        }
        if (schr < 0)
        { // Skipping some pixels
            (*remaining_width) += schr;
            (*r) -= *(*sp);
            (*sp)++;
        } else
        //if ( schr > 0 )
        { // Drawing some pixels
            drawOut = schr;
            if (drawOut >= (*remaining_width))
                drawOut = (*remaining_width);
            LbDrawBufferSolid(r, (*sp)+1, drawOut, false);
            (*remaining_width) -= schr;
            (*sp) += (*(*sp)) + 1;
        }
    } //end while
}

/** Fast copy sprite drawing routine. Does not support transparency nor mirroring.
 *
 * @param sp
 * @param sprWd
 * @param sprHt
 * @param r
 * @param nextRowDelta
 * @param left
 * @param mirror
 * @return
 */
static inline TbResult LbSpriteDrawFastCpy(const char *sp,int64_t sprWd,int64_t sprHt,TbPixel *r,
    int64_t nextRowDelta,int64_t left,const TbBool mirror)
{
    TbPixel *nextRow;
    int64_t htIndex;
    nextRow = &(r[nextRowDelta]);
    htIndex = sprHt;
    // For all lines of the sprite
    while (1)
    {
        int64_t x1;
        int64_t lpos;
        x1 = sprWd;
        // Skip the pixels left before drawing area
        lpos = LbSpriteDrawLineSkipLeft(&sp,&x1,left);
        // Do the actual drawing
        LbSpriteDrawLineFastCpy(&sp,&r,&x1,lpos);
        // Go to next line
        htIndex--;
        if (htIndex == 0)
            return Lb_SUCCESS;
        LbSpriteDrawLineSkipToEol(&sp,&x1);
        r = nextRow;
        nextRow += nextRowDelta;
    } //end while
    return Lb_SUCCESS;
}

/* Each entry point below routes to the renderer, which records the draw for this
 * frame or draws it now. The matching ...Immediate function is the draw itself. */
TbResult LbSpriteDraw(int64_t x, int64_t y, const struct TbSprite *spr)
{
    return RendererSpriteDraw(x, y, spr);
}

TbResult LbSpriteDrawOneColour(int64_t x, int64_t y, const struct TbSprite *spr, const TbPixel colour)
{
    return RendererSpriteDrawOneColour(x, y, spr, colour);
}

TbResult LbSpriteDrawScaled(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height)
{
    return RendererSpriteDrawScaled(xpos, ypos, sprite, dest_width, dest_height);
}

TbResult LbSpriteDrawScaledOneColour(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel colour)
{
    return RendererSpriteDrawScaledOneColour(xpos, ypos, sprite, dest_width, dest_height, colour);
}

int64_t LbSpriteDrawScaledRemap(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel *cmap)
{
    return RendererSpriteDrawScaledRemap(xpos, ypos, sprite, dest_width, dest_height, cmap);
}

TbResult LbSpriteDrawImmediate(int64_t x, int64_t y, const struct TbSprite *spr)
{
    struct TbSpriteDrawData spd;
    TbResult ret;
    SYNCDBG(19,"At (%" PRId64 ",%" PRId64 ")",(int64_t)(x),(int64_t)(y));
    ret = LbSpriteDrawPrepare(&spd, x, y, spr);
    if (ret != Lb_SUCCESS)
        return ret;
    if ((RendererGetDrawFlags() & (Lb_SPRITE_TRANSPAR4|Lb_SPRITE_TRANSPAR8)) != 0)
        return LbSpriteDrawTranspr(spd.sp,spd.Wd,spd.Ht,spd.r,spd.nextRowDelta,spd.startShift,spd.mirror);
    else
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
        return LbSpriteDrawSolid(spd.sp,spd.Wd,spd.Ht,spd.r,spd.nextRowDelta,spd.startShift,spd.mirror);
    else
        return LbSpriteDrawFastCpy(spd.sp,spd.Wd,spd.Ht,spd.r,spd.nextRowDelta,spd.startShift,spd.mirror);
}

/** Internal routine to draw one line of a transparent sprite.
 *
 * @param sp
 * @param r
 * @param remaining_width
 * @param lpos
 * @param mirror
 */
static inline void LbSpriteDrawLineTrOneColour(const char **sp, TbPixel **r, int64_t *remaining_width,
    TbPixel colour, int64_t lpos,const TbBool mirror)
{
    char schr;
    unsigned char drawOut;
    // Draw any unfinished block, which should be only partially visible
    if (lpos > 0)
    {
        schr = *(*sp);
        if (schr < 0)
        {
            drawOut = -schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            if ( mirror )
                (*r) -= drawOut;
            else
                (*r) += drawOut;
            (*sp)++;

        } else
        {
            // Draw the part of current block which exceeds value of 'lpos'
            drawOut = schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            LbDrawBufferOneColour(r,colour,drawOut,mirror);
            // Update positions and break the skipping loop
            (*sp) += (*(*sp)) + 1;
        }
        (*remaining_width) -= drawOut;
    }
    // Draw the visible part of a sprite
    while ((*remaining_width) > 0)
    {
        schr = *(*sp);
        if (schr == 0)
        { // EOL, breaking line loop
            break;
        }
        if (schr < 0)
        { // Skipping some pixels
            (*remaining_width) += schr;
            if ( mirror )
               (*r) += *(*sp);
            else
               (*r) -= *(*sp);
            (*sp)++;
        } else
        //if ( schr > 0 )
        { // Drawing some pixels
            drawOut = schr;
            if (drawOut >= (*remaining_width))
                drawOut = (*remaining_width);
            LbDrawBufferOneColour(r,colour,drawOut,mirror);
            (*remaining_width) -= schr;
            (*sp) += (*(*sp)) + 1;
        }
    } //end while
}

static inline TbResult LbSpriteDrawTrOneColour(const char *sp,int64_t sprWd,int64_t sprHt,
        TbPixel *r,TbPixel colour,int64_t nextRowDelta,int64_t left,const TbBool mirror)
{
    TbPixel *nextRow;
    int64_t htIndex;
    nextRow = &(r[nextRowDelta]);
    htIndex = sprHt;
    // For all lines of the sprite
    while (1)
    {
        int64_t x1;
        int64_t lpos;
        x1 = sprWd;
        // Skip the pixels left before drawing area
        lpos = LbSpriteDrawLineSkipLeft(&sp,&x1,left);
        // Do the actual drawing
        LbSpriteDrawLineTrOneColour(&sp,&r,&x1,colour,lpos,mirror);
        // Go to next line
        htIndex--;
        if (htIndex == 0)
            return Lb_SUCCESS;
        LbSpriteDrawLineSkipToEol(&sp,&x1);
        r = nextRow;
        nextRow += nextRowDelta;
    } //end while
    return Lb_SUCCESS;
}

static inline void LbSpriteDrawLineSlOneColour(const char **sp, TbPixel **r, int64_t *remaining_width,
    TbPixel colour, int64_t lpos,const TbBool mirror)
{
    char schr;
    unsigned char drawOut;
    // Draw any unfinished block, which should be only partially visible
    if (lpos > 0)
    {
        schr = *(*sp);
        if (schr < 0)
        {
            drawOut = -schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            (*r) -= drawOut;
            (*sp)++;
        } else
        {
            // Draw the part of current block which exceeds value of 'lpos'
            drawOut = schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            LbDrawBufferOneColorSolid(r,colour,drawOut,mirror);
            // Update positions and break the skipping loop
            (*sp) += (*(*sp)) + 1;
        }
        (*remaining_width) -= drawOut;
    }
    // Draw the visible part of a sprite
    while ((*remaining_width) > 0)
    {
        schr = *(*sp);
        if (schr == 0)
        { // EOL, breaking line loop
            break;
        }
        if (schr < 0)
        { // Skipping some pixels
            (*remaining_width) += schr;
            (*r) += *(*sp);
            (*sp)++;
        } else
        //if ( schr > 0 )
        { // Drawing some pixels
            drawOut = schr;
            if (drawOut >= (*remaining_width))
                drawOut = (*remaining_width);
            LbDrawBufferOneColorSolid(r,colour,drawOut,mirror);
            (*remaining_width) -= schr;
            (*sp) += (*(*sp)) + 1;
        }
    } //end while
}

static inline TbResult LbSpriteDrawSlOneColour(const char *sp,int64_t sprWd,int64_t sprHt,
        TbPixel *r,TbPixel colour,int64_t nextRowDelta,int64_t left,const TbBool mirror)
{
    TbPixel *nextRow;
    int64_t htIndex;
    nextRow = &(r[nextRowDelta]);
    htIndex = sprHt;
    // For all lines of the sprite
    while (1)
    {
        int64_t x1;
        int64_t lpos;
        x1 = sprWd;
        // Skip the pixels left before drawing area
        lpos = LbSpriteDrawLineSkipLeft(&sp,&x1,left);
        // Do the actual drawing
        LbSpriteDrawLineSlOneColour(&sp,&r,&x1,colour,lpos,mirror);
        // Go to next line
        htIndex--;
        if (htIndex == 0)
            return Lb_SUCCESS;
        LbSpriteDrawLineSkipToEol(&sp,&x1);
        r = nextRow;
        nextRow += nextRowDelta;
    } //end while
    return Lb_SUCCESS;
}

static inline void LbSpriteDrawLineFCOneColour(const char **sp, TbPixel **r, int64_t *remaining_width, TbPixel colour, int64_t lpos)
{
    char schr;
    unsigned char drawOut;
    if (lpos > 0)
    {
        // Draw the part of current block which exceeds value of 'lpos'
        schr = *(*sp);
        if (schr < 0)
        {
            drawOut = -schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            (*r) += drawOut;
            (*sp)++;
        } else
        {
            drawOut = schr - lpos;
            if (drawOut > (*remaining_width))
              drawOut = (*remaining_width);
            LbDrawBufferOneColorSolid(r, colour, drawOut, false);
            (*r) += drawOut;
            (*sp) += (*(*sp)) + 1;
        }
        (*remaining_width) -= drawOut;
    }
    // Draw the visible part of a sprite
    while ((*remaining_width) > 0)
    {
        schr = *(*sp);
        if (schr == 0)
        { // EOL, breaking line loop
            break;
        }
        if (schr < 0)
        { // Skipping some pixels
            (*remaining_width) += schr;
            (*r) -= *(*sp);
            (*sp)++;
        } else
        //if ( schr > 0 )
        { // Drawing some pixels
            drawOut = schr;
            if (drawOut >= (*remaining_width))
                drawOut = (*remaining_width);
            /* Was memset() -- no longer valid now a pixel is 4 bytes, not 1. */
            for (int64_t px = 0; px < drawOut; px++)
                (*r)[px] = colour;
            (*remaining_width) -= schr;
            (*r) += schr;
            (*sp) += (*(*sp)) + 1;
        }
    } //end while
}

/** Fast copy one color sprite drawing routine. Does not support transparency nor mirroring.
 *
 * @param sp
 * @param sprWd
 * @param sprHt
 * @param r
 * @param nextRowDelta
 * @param left
 * @param mirror
 * @return
 */
static inline TbResult LbSpriteDrawFCOneColour(const char *sp,int64_t sprWd,int64_t sprHt,TbPixel *r,
    TbPixel colour,int64_t nextRowDelta,int64_t left,const TbBool mirror)
{
    TbPixel *nextRow;
    int64_t htIndex;
    nextRow = &(r[nextRowDelta]);
    htIndex = sprHt;
    // For all lines of the sprite
    while (1)
    {
        int64_t x1;
        int64_t lpos;
        x1 = sprWd;
        // Skip the pixels left before drawing area
        lpos = LbSpriteDrawLineSkipLeft(&sp,&x1,left);
        // Do the actual drawing
        LbSpriteDrawLineFCOneColour(&sp,&r,&x1,colour,lpos);
        // Go to next line
        htIndex--;
        if (htIndex == 0)
            return Lb_SUCCESS;
        LbSpriteDrawLineSkipToEol(&sp,&x1);
        r = nextRow;
        nextRow += nextRowDelta;
    } //end while
    return Lb_SUCCESS;
}

TbResult LbSpriteDrawOneColourImmediate(int64_t x, int64_t y, const struct TbSprite *spr, const TbPixel colour)
{
    struct TbSpriteDrawData spd;
    TbResult ret;
    SYNCDBG(19,"At (%" PRId64 ",%" PRId64 ")",(int64_t)(x),(int64_t)(y));
    ret = LbSpriteDrawPrepare(&spd, x, y, spr);
    if (ret != Lb_SUCCESS)
        return ret;
    if ((RendererGetDrawFlags() & (Lb_SPRITE_TRANSPAR4|Lb_SPRITE_TRANSPAR8)) != 0) {
        return LbSpriteDrawTrOneColour(spd.sp,spd.Wd,spd.Ht,spd.r,colour,spd.nextRowDelta,spd.startShift,spd.mirror);
    } else
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0) {
        return LbSpriteDrawSlOneColour(spd.sp,spd.Wd,spd.Ht,spd.r,colour,spd.nextRowDelta,spd.startShift,spd.mirror);
    } else {
        return LbSpriteDrawFCOneColour(spd.sp,spd.Wd,spd.Ht,spd.r,colour,spd.nextRowDelta,spd.startShift,spd.mirror);
    }
}

/* Used to byte-align dst to a 4-byte boundary and then bulk-copy 4 bytes
 * (= 4 pixels) at a time via uint32_t -- a real win while TbPixel was one
 * byte. TbPixel is a 4-byte struct now, exactly the width of that uint32_t
 * "batch", so the alignment dance no longer means anything and the bulk
 * copy was silently copying 1 pixel while advancing both pointers by 4
 * pixels' worth of storage -- 3 out of every 4 pixels in a run were never
 * written, which is what "dotted" sprites are: whatever was already in the
 * framebuffer at those positions, left untouched. Plain per-element copy;
 * the compiler vectorizes this itself, no need for a hand-rolled trick. */
void LbPixelBlockCopyForward(TbPixel * dst, const TbPixel * src, int64_t len)
{
    for (int64_t i = 0; i < len; i++)
        dst[i] = src[i];
}

/**
 * Sets X scaling array for drawing scaled sprites.
 * The X scaling array contains position and length of each pixel of the sprite on destination buffer.
 * @param xsteps_arr The destination X scaling array.
 * @param x Position of the sprite in output buffer, X coord.
 * @param swidth Source sprite original width.
 * @param dwidth Width which the sprite should have on destination buffer.
 * @param gwidth Graphics buffer visible window line width.
 */
void LbSpriteSetScalingWidthClippedArray(int64_t * xsteps_arr, int64_t x, int64_t swidth, int64_t dwidth, int64_t gwidth)
{
    int64_t *pwidth;
    int64_t pxpos;
    pwidth = xsteps_arr;
    int64_t factor = (dwidth<<16)/swidth;
    int64_t tmp = (factor >> 1) + (x << 16);
    pxpos = tmp >> 16;
    pxpos = min(pxpos, max(0, x));
    int64_t w = swidth;
    do {
        tmp += factor;
        int64_t pxstart;
        int64_t pxend;
        pxstart = pxpos;
        pxend = tmp>>16;
        // Remember unclipped difference
        int64_t wdiff = pxend - pxstart;
       // Clip both endpoints independently to [0, gwidth]
        if (pxstart < 0) pxstart = 0;
        else if (pxstart > gwidth) pxstart = gwidth;
        if (pxend < 0) pxend = 0;
        else if (pxend > gwidth) pxend = gwidth;
        if (pxend < pxstart) pxend = pxstart;
        // Set clipped difference to be drawn
        pwidth[0] = pxstart;
        pwidth[1] = pxend - pxstart;
        // But store position for the unclipped difference
        pxpos += wdiff;
        w--;
        pwidth += 2;
    } while (w > 0);
}

void LbSpriteSetScalingWidthSimpleArray(int64_t * xsteps_arr, int64_t x, int64_t swidth, int64_t dwidth)
{
    int64_t *pwidth;
    int64_t cwidth;
    pwidth = xsteps_arr;
    int64_t factor = (dwidth<<16)/swidth;
    int64_t tmp = (factor >> 1) + (x << 16);
    cwidth = tmp >> 16;
    int64_t w = swidth;
    do {
      int64_t i;
      for (i=0; i < 16; i+=2)
      {
          pwidth[i] = cwidth;
          tmp += factor;
          pwidth[i+1] = (tmp>>16) - cwidth;
          cwidth = (tmp>>16);
          w--;
          if (w <= 0)
              break;
      }
      pwidth += 16;
    } while (w > 0);
}

void LbSpriteClearScalingWidthArray(int64_t * xsteps_arr, int64_t swidth)
{
    int64_t i;
    int64_t *pwidth;
    pwidth = xsteps_arr;
    for (i=0; i < swidth; i++)
    {
        pwidth[0] = 0;
        pwidth[1] = 0;
        pwidth += 2;
    }
}

/**
 * Sets Y scaling array for drawing scaled sprites.
 * The Y scaling array contains position and length of each line of pixels of the sprite on destination buffer.
 * @param ysteps_arr The destination X scaling array.
 * @param y Position of the sprite in output buffer, Y coord.
 * @param sheight Source sprite original height.
 * @param dheight Height which the sprite should have on destination buffer.
 * @param gheight Graphics buffer visible window lines count.
 */
void LbSpriteSetScalingHeightClippedArray(int64_t * ysteps_arr, int64_t y, int64_t sheight, int64_t dheight, int64_t gheight)
{
    int64_t *pheight;
    int64_t lnpos;
    pheight = ysteps_arr;
    int64_t factor = (dheight<<16)/sheight;
    int64_t tmp = (factor >> 1) + (y << 16);
    lnpos = tmp >> 16;
    lnpos = min(lnpos, max(0, y));
    if (lnpos < 0)
        lnpos = 0;
    if (lnpos >= gheight)
        lnpos = gheight;
    int64_t h = sheight;
    do {
        tmp += factor;
        int64_t lnstart;
        int64_t lnend;
        lnstart = lnpos;
        lnend = tmp>>16;
        // Remember unclipped difference
        int64_t hdiff = lnend - lnstart;
        // Clip both endpoints independently to [0, gheight]
        if (lnstart < 0) lnstart = 0;
        else if (lnstart > gheight) lnstart = gheight;
        if (lnend < 0) lnend = 0;
        else if (lnend > gheight) lnend = gheight;
        if (lnend < lnstart) lnend = lnstart;
        // Set clipped difference to be drawn
        pheight[0] = lnstart;
        pheight[1] = lnend - lnstart;
        // But store position for the unclipped difference
        lnpos += hdiff;
        h--;
        pheight += 2;
    } while (h > 0);
}

void LbSpriteSetScalingHeightSimpleArray(int64_t * ysteps_arr, int64_t y, int64_t sheight, int64_t dheight)
{
    int64_t *pheight;
    int64_t cheight;
    pheight = ysteps_arr;
    int64_t factor = (dheight<<16)/sheight;
    int64_t tmp = (factor >> 1) + (y << 16);
    cheight = tmp >> 16;
    int64_t h = sheight;
    do {
      int64_t i=0;
      for (i=0; i < 16; i+=2)
      {
        pheight[i] = cheight;
        tmp += factor;
        pheight[i+1] = (tmp>>16) - cheight;
        cheight = (tmp>>16);
        h--;
        if (h <= 0)
          break;
      }
      pheight += 16;
    } while (h > 0);
}

void LbSpriteClearScalingHeightArray(int64_t * ysteps_arr, int64_t sheight)
{
    int64_t i;
    int64_t *pheight;
    pheight = ysteps_arr;
    for (i=0; i < sheight; i++)
    {
        pheight[0] = 0;
        pheight[1] = 0;
        pheight += 2;
    }
}

/**
 * Sets scaling data for drawing scaled sprites.
 * @param x Position of the sprite in output buffer, X coord.
 * @param y Position of the sprite in output buffer, Y coord.
 * @param swidth Source sprite original width.
 * @param sheight Source sprite original height.
 * @param dwidth Width which the sprite should have on destination buffer.
 * @param dheight Height which the sprite should have on destination buffer.
 */
void LbSpriteSetScalingData(int64_t x, int64_t y, int64_t swidth, int64_t sheight, int64_t dwidth, int64_t dheight)
{
    int64_t gwidth = SwTargetWindowWidth();
    int64_t gheight = SwTargetWindowHeight();
    scale_up = true;
    if ((dwidth <= swidth) && (dheight <= sheight))
        scale_up = false;
    // Checking whether to select simple scaling creation, or more comprehensive one - with clipping
    if ((swidth <= 0) || (dwidth <= 0)) {
        LbSpriteClearScalingWidthArray(xsteps_array, SPRITE_SCALING_XSTEPS);
    } else
    // Normally it would be enough to check if ((dwidth+y) >= gwidth), but due to rounding we need to add swidth
    if ((x < 0) || ((dwidth+swidth+x) >= gwidth))
    {
        LbSpriteSetScalingWidthClippedArray(xsteps_array, x, min(swidth, SPRITE_SCALING_XSTEPS), dwidth, gwidth);
    } else {
        LbSpriteSetScalingWidthSimpleArray(xsteps_array, x, min(swidth, SPRITE_SCALING_XSTEPS), dwidth);
    }
    if ((sheight <= 0) || (dheight <= 0)) {
        LbSpriteClearScalingHeightArray(ysteps_array, SPRITE_SCALING_YSTEPS);
    } else
    // Normally it would be enough to check if ((dheight+y) >= gheight), but our simple rounding may enlarge the image
    if ((y < 0) || ((dheight+sheight+y) >= gheight))
    {
        LbSpriteSetScalingHeightClippedArray(ysteps_array, y, min(sheight, SPRITE_SCALING_YSTEPS), dheight, gheight);
    } else {
        LbSpriteSetScalingHeightSimpleArray(ysteps_array, y, min(sheight, SPRITE_SCALING_YSTEPS), dheight);
    }
}

TbResult LbSpriteDrawScaledImmediate(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height)
{
    SYNCDBG(19,"At (%" PRId64 ",%" PRId64 ") size (%" PRId64 ",%" PRId64 ")",(int64_t)(xpos),(int64_t)(ypos),(int64_t)(dest_width),(int64_t)(dest_height));
    if ((dest_width <= 0) || (dest_height <= 0))
      return 1;
    if ((RendererGetDrawFlags() & Lb_SPRITE_REMAP) != 0)
        SetupSpriteRemapShade(lbDisplay.FadeStep & 0x3F);
    LbSpriteSetScalingData(xpos, ypos, sprite->SWidth, sprite->SHeight, dest_width, dest_height);
    const struct TbSourceBuffer buffer = {
        sprite->Data,
        sprite->SWidth,
        sprite->SHeight,
        sprite->SWidth,
    };
    return LbSpriteDrawUsingScalingData(0, 0, &buffer);
}

TbResult LbSpriteDrawScaledOneColourImmediate(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel colour)
{
    SYNCDBG(19,"At (%" PRId64 ",%" PRId64 ") size (%" PRId64 ",%" PRId64 ")",(int64_t)(xpos),(int64_t)(ypos),(int64_t)(dest_width),(int64_t)(dest_height));
    if ((dest_width <= 0) || (dest_height <= 0))
      return 1;
    if ((RendererGetDrawFlags() & Lb_SPRITE_REMAP) != 0)
        SetupSpriteRemapShade(lbDisplay.FadeStep & 0x3F);
    LbSpriteSetScalingData(xpos, ypos, sprite->SWidth, sprite->SHeight, dest_width, dest_height);
    return LbSpriteDrawOneColourUsingScalingData(0, 0, sprite, colour);
}

int64_t LbSpriteDrawScaledRemapImmediate(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel *cmap)
{
    SYNCDBG(19,"At (%" PRId64 ",%" PRId64 ") size (%" PRId64 ",%" PRId64 ")",(int64_t)(xpos),(int64_t)(ypos),(int64_t)(dest_width),(int64_t)(dest_height));
    if ((dest_width <= 0) || (dest_height <= 0))
      return 1;
    if ((RendererGetDrawFlags() & Lb_SPRITE_REMAP) != 0)
        SetupSpriteRemapShade(lbDisplay.FadeStep & 0x3F);
    LbSpriteSetScalingData(xpos, ypos, sprite->SWidth, sprite->SHeight, dest_width, dest_height);
    const struct TbSourceBuffer buffer = {
        sprite->Data,
        sprite->SWidth,
        sprite->SHeight,
        sprite->SWidth,
    };
    return LbSpriteDrawRemapUsingScalingData(0, 0, &buffer, cmap);
}


void setup_vecs(TbPixel *screenbuf, unsigned char *nvec_map,
        uint64_t line_len, uint64_t width, uint64_t height)
{
  if ( line_len > 0 )
    vec_screen_width = line_len;
  if (screenbuf != NULL)
  {
    vec_screen = screenbuf;
    poly_screen = screenbuf - vec_screen_width;
  }
  if (nvec_map != NULL)
  {
    vec_map = nvec_map;
    dither_map = nvec_map;
    dither_end = nvec_map + 16;
  }
  if (height > 0)
    vec_window_height = (int64_t)height;
  if (width > 0)
    vec_window_width = (int64_t)width;
}

/**
 * Draws a scaled up big sprite on given buffer, with original colours, from left to right.
 * Requires step arrays for scaling.
 *
 * @param outbuf The output buffer.
 * @param scanline Length of the output buffer scanline.
 * @param xstep Scaling steps array, x dimension.
 * @param ystep Scaling steps array, y dimension.
 * @param sprite The source sprite.
 * @return Gives 0 on success.
 */
TbResult LbHugeSpriteDrawUsingScalingUpData(TbPixel *outbuf, int64_t scanline, int64_t outheight,
    int64_t *xstep, int64_t *ystep, const struct TbHugeSprite *sprite)
{
    SYNCDBG(17,"Drawing");
    int64_t ystep_delta;
    const unsigned char *sprdata;
    int64_t *ycurstep;
    const unsigned char *palette = RendererGetActivePalette();

    ystep_delta = 2;
    if (scanline < 0) {
        ystep_delta = -2;
    }
    ycurstep = ystep;

    for (uint64_t h = 0; h < sprite->SHeight; h++)
    {
        if (ycurstep[1] != 0)
        {
            int64_t ycur;
            int64_t solid_len;
            TbPixel * out_line;
            int64_t xdup;
            int64_t ydup;
            int64_t *xcurstep;
            ydup = ycurstep[1];
            if (ycurstep[0]+ydup > outheight)
                ydup = outheight-ycurstep[0];
            xcurstep = xstep;
            sprdata = &sprite->Data[sprite->Lines[h]];
            TbPixel *out_end;
            out_end = outbuf;
            while (out_end - outbuf < scanline)
            {
                int64_t pxlen;
                pxlen = *(uint32_t *)sprdata; // sprite stream: 32-bit run length
                sprdata += 4;
                TbPixel *out_start;
                out_start = out_end;
                for(;pxlen > 0; pxlen--)
                {
                    xdup = xcurstep[1];
                    if (xcurstep[0]+xdup > llabs(scanline))
                        xdup = llabs(scanline)-xcurstep[0];
                    if (xdup > 0)
                    {
                        TbPixel pxval = expand_indexed_pixel(*sprdata, palette);
                        for (;xdup > 0; xdup--)
                        {
                            *out_end = pxval;
                            out_end++;
                        }
                    }
                    sprdata++;
                    xcurstep += 2;
                }
                ycur = ydup - 1;
                if (ycur > 0)
                {
                    solid_len = out_end - out_start;
                    out_line = out_start + scanline;
                    for (;ycur > 0; ycur--)
                    {
                        if (solid_len > 0) {
                            LbPixelBlockCopyForward(out_line, out_start, solid_len);
                        }
                        out_line += scanline;
                    }
                }
                // Transparent bytes count
                pxlen = *(uint32_t *)sprdata; // sprite stream: 32-bit run length
                sprdata += 4;
                out_end -= xcurstep[0];
                xcurstep += 2 * pxlen;
                // In case we've exceeded sprite width, don't try to access xcurstep[] any more
                if ((uint64_t) ((xcurstep - xstep) / 2) >= sprite->SWidth)
                    break;
                out_end += xcurstep[0];
            }
            outbuf += scanline;
            ycur = ydup - 1;
            for (;ycur > 0; ycur--)
            {
                outbuf += scanline;
            }
        }
        ycurstep += ystep_delta;
    }
    return Lb_SUCCESS;
}

/** Draws a huge sprite, used ie. as frame in land view.
 *  What differs huge sprite from standard one is the index of y line starts, which
 *  speeds up finding a specific line to be drawn.
 * @param spr Sprite data struct.
 * @param sp_len Length of the sprite data.
 * @param r Destination buffer.
 * @param r_row_delta Row interline in the destination buffer.
 * @param r_height Height of the destination buffer.
 * @param xshift Shift of the drawing, X coord.
 * @param yshift Shift of the drawing, Y coord.
 * @return
 */
TbResult LbHugeSpriteDraw(const struct TbHugeSprite * spr, int64_t sp_len,
    TbPixel *r, int64_t r_row_delta, int64_t r_height, int64_t xshift, int64_t yshift, int64_t units_per_px)
{
    LbSpriteSetScalingData(-xshift*units_per_px/16, -yshift*units_per_px/16, spr->SWidth, spr->SHeight, spr->SWidth*units_per_px/16, spr->SHeight*units_per_px/16);
    return LbHugeSpriteDrawUsingScalingUpData(r, r_row_delta, r_height, xsteps_array, ysteps_array, spr);
}

/**
 * Draws a tiled sprite, which consists of multiple sprites.
 * @param start_x
 * @param start_y
 * @param units_per_px
 * @param bigspr
 * @param sprite
 * @note originally named DrawBigSprite()
 */
void LbTiledSpriteDraw(int64_t start_x, int64_t start_y, int64_t units_per_px, struct TiledSprite *bigspr, PanelSpriteLookupFn panel_sprite_fn)
{
    int64_t x;
    int64_t y;
    int64_t delta_x;
    int64_t delta_y;
    int64_t spnum_x;
    int64_t spnum_y;
    delta_y = 0;
    y = start_y;
    for (spnum_y = 0; spnum_y < bigspr->y_num; spnum_y++)
    {
        int64_t spr_idx = bigspr->spr_idx[spnum_y][0];
        x = start_x;
        for (spnum_x = 0; spnum_x < bigspr->x_num; spnum_x++)
        {
            const struct TbSprite * sprite = panel_sprite_fn(spr_idx);
            delta_x = sprite->SWidth * units_per_px / 16;
            delta_y = sprite->SHeight * units_per_px / 16;
            if (spr_idx)
            {
                LbSpriteDrawScaled(x, y, sprite, delta_x, delta_y);
            } else
            {
                int64_t prev_spr_idx = (spr_idx - 10);
                int64_t spnum_p;
                for (spnum_p = 1; spnum_p <= spnum_y; spnum_p++)
                {
                    if (prev_spr_idx) {
                        delta_x = panel_sprite_fn(bigspr->spr_idx[(spnum_y - spnum_p)][spnum_x])->SWidth * units_per_px / 16;
                        break;
                    }
                    prev_spr_idx -= 10;
                }
            }
            spr_idx++;
            x += delta_x;
        }
        y += delta_y;
    }
}

int64_t LbTiledSpriteHeight(struct TiledSprite *bigspr, PanelSpriteLookupFn panel_sprite_fn)
{
    int64_t height = 0;
    for (int64_t spnum_y = 0; spnum_y < bigspr->y_num; spnum_y++)
    {
        height += panel_sprite_fn(bigspr->spr_idx[spnum_y][0])->SHeight;
    }
    return height;
}

void LbDrawPixel(int64_t x, int64_t y, TbPixel colour)
{
    if (SwCaptureRect(x, y, 1, 1, colour))
        return;
    SwTargetGraphicsWindowPtr()[x + SwTargetScanline() * y] = colour;
}

void LbDrawPixelClip(int64_t x, int64_t y, TbPixel colour)
{
    if ( (x < 0) || (x >= SwTargetWindowWidth()) )
        return;
    if ( (y < 0) || (y >= SwTargetWindowHeight()) )
        return;
    TbPixel *buf;
    buf = SwTargetGraphicsWindowPtr() + SwTargetScanline() * y + x;
    if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
    {
        *buf = render_ghost_blend(colour, *buf);
    } else
    if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
    {
        *buf = render_ghost_blend_2(colour, *buf);
    } else
    {
        *buf = colour;
    }
}

void LbDrawCircleFilled(int64_t x, int64_t y, int64_t radius, TbPixel colour)
{
    int64_t r;
    int64_t i;
    int64_t n;
    int64_t dx;
    int64_t dy;
    if (radius < 1)
    {
        LbDrawPixelClip(x, y, colour);
        return;
    }
    if (radius == 1)
    {
        LbDrawPixelClip(x - 1, y, colour);
        LbDrawPixelClip(x, y - 1, colour);
        LbDrawPixelClip(x + 1, y, colour);
        LbDrawPixelClip(x, y + 1, colour);
        LbDrawPixelClip(x, y, colour);
        return;
    }
    n = 3 - 2 * radius;
    LbDrawHVLine(x - radius, y, radius + x, y, colour);
    if (n >= 0)
    {
        LbDrawHVLine(x, y - radius, x, y - radius, colour);
        LbDrawHVLine(x, radius + y, x, radius + y, colour);
        r = radius - 1;
        n += 10 - (4 * (radius - 1) + 4);
    } else
    {
        r = radius;
        n += 10 - 4;
    }
    dx = 1;
    dy = 1;
    while (dx < r)
    {
        LbDrawHVLine(x - r, y - dx, x + r, y - dx, colour);
        LbDrawHVLine(x - r, dx + y, x + r, dx + y, colour);
        if (n >= 0)
        {
            LbDrawHVLine(x - dy, y - r, x + dy, y - r, colour);
            LbDrawHVLine(x - dy, r + y, x + dy, r + y, colour);
            i = dx - r;
            r--;
            n += 4 * i + 10;
        } else
        {
            n += 4 * dx + 6;
        }
        dx++;
        dy = dx;
    }
    if (r == dx)
    {
        LbDrawHVLine(x - r, y - dx, x + r, y - dx, colour);
        LbDrawHVLine(x - r, dx + y, x + r, dx + y, colour);
    }
}

static inline void LbDrawPixelClipOpaq1(int64_t x, int64_t y, TbPixel colour)
{
    if ( (x < 0) || (x >= SwTargetWindowWidth()) )
        return;
    if ( (y < 0) || (y >= SwTargetWindowHeight()) )
        return;
    TbPixel *buf;
    buf = SwTargetGraphicsWindowPtr() + SwTargetScanline() * y + x;
    *buf = render_ghost_blend(colour, *buf);
}

static inline void LbDrawPixelClipOpaq2(int64_t x, int64_t y, TbPixel colour)
{
    if ( (x < 0) || (x >= SwTargetWindowWidth()) )
        return;
    if ( (y < 0) || (y >= SwTargetWindowHeight()) )
        return;
    TbPixel *buf;
    buf = SwTargetGraphicsWindowPtr() + SwTargetScanline() * y + x;
    *buf = render_ghost_blend_2(colour, *buf);
}

static inline void LbDrawPixelClipSolid(int64_t x, int64_t y, TbPixel colour)
{
    if ( (x < 0) || (x >= SwTargetWindowWidth()) )
        return;
    if ( (y < 0) || (y >= SwTargetWindowHeight()) )
        return;
    TbPixel *buf;
    buf = SwTargetGraphicsWindowPtr() + SwTargetScanline() * y + x;
    *buf = colour;
}

void LbDrawCircleOutline(int64_t x, int64_t y, int64_t radius, TbPixel colour)
{
    int64_t na;
    int64_t nb;
    int64_t n;
    if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR4) != 0)
    {
        nb = radius;
        n = 3 - 2 * radius;
        if (radius < 1)
        {
            LbDrawPixelClipOpaq1(x, y, colour);
            return;
        }
        for (na=0; na < nb; na++)
        {
            LbDrawPixelClipOpaq1(x - na, y - nb, colour);
            LbDrawPixelClipOpaq1(x + na, y - nb, colour);
            LbDrawPixelClipOpaq1(x - na, y + nb, colour);
            LbDrawPixelClipOpaq1(x + na, y + nb, colour);
            LbDrawPixelClipOpaq1(x - nb, y - na, colour);
            LbDrawPixelClipOpaq1(x + nb, y - na, colour);
            LbDrawPixelClipOpaq1(x - nb, y + na, colour);
            LbDrawPixelClipOpaq1(x + nb, y + na, colour);
            if (n >= 0)
            {
                n += 10 + 4 * (na - nb);
                nb--;
            } else
            {
                n += 6 + 4 * na;
            }
        }
        if (nb == na)
        {
            LbDrawPixelClipOpaq1(x - na, y - nb, colour);
            LbDrawPixelClipOpaq1(x + na, y - nb, colour);
            LbDrawPixelClipOpaq1(x - na, y + nb, colour);
            LbDrawPixelClipOpaq1(x + na, y + nb, colour);
            LbDrawPixelClipOpaq1(x - nb, y - na, colour);
            LbDrawPixelClipOpaq1(x + nb, y - na, colour);
            LbDrawPixelClipOpaq1(x - nb, y + na, colour);
            LbDrawPixelClipOpaq1(x + nb, y + na, colour);
        }
    } else
    if ((RendererGetDrawFlags() & Lb_SPRITE_TRANSPAR8) != 0)
    {
        nb = radius;
        n = 3 - 2 * radius;
        if (radius < 1)
        {
            LbDrawPixelClipOpaq2(x, y, colour);
            return;
        }
        for (na=0; na < nb; na++)
        {
            LbDrawPixelClipOpaq2(x - na, y - nb, colour);
            LbDrawPixelClipOpaq2(x + na, y - nb, colour);
            LbDrawPixelClipOpaq2(x - na, y + nb, colour);
            LbDrawPixelClipOpaq2(x + na, y + nb, colour);
            LbDrawPixelClipOpaq2(x - nb, y - na, colour);
            LbDrawPixelClipOpaq2(x + nb, y - na, colour);
            LbDrawPixelClipOpaq2(x - nb, y + na, colour);
            LbDrawPixelClipOpaq2(x + nb, y + na, colour);
            if (n >= 0)
            {
                n += 10 + 4 * (na - nb);
                nb--;
            } else
            {
                n += 6 + 4 * na;
            }
        }
        if (nb == na)
        {
            LbDrawPixelClipOpaq2(x - na, y - nb, colour);
            LbDrawPixelClipOpaq2(x + na, y - nb, colour);
            LbDrawPixelClipOpaq2(x - na, y + nb, colour);
            LbDrawPixelClipOpaq2(x + na, y + nb, colour);
            LbDrawPixelClipOpaq2(x - nb, y - na, colour);
            LbDrawPixelClipOpaq2(x + nb, y - na, colour);
            LbDrawPixelClipOpaq2(x - nb, y + na, colour);
            LbDrawPixelClipOpaq2(x + nb, y + na, colour);
        }
    } else
    {
        nb = radius;
        n = 3 - 2 * radius;
        if (radius < 1)
        {
            LbDrawPixelClipSolid(x, y, colour);
            return;
        }
        for (na=0; na < nb; na++)
        {
            LbDrawPixelClipSolid(x - na, y - nb, colour);
            LbDrawPixelClipSolid(x + na, y - nb, colour);
            LbDrawPixelClipSolid(x - na, y + nb, colour);
            LbDrawPixelClipSolid(x + na, y + nb, colour);
            LbDrawPixelClipSolid(x - nb, y - na, colour);
            LbDrawPixelClipSolid(x + nb, y - na, colour);
            LbDrawPixelClipSolid(x - nb, y + na, colour);
            LbDrawPixelClipSolid(x + nb, y + na, colour);
            if (n >= 0)
            {
                n += 10 + 4 * (na - nb);
                nb--;
            } else
            {
                n += 6 + 4 * na;
            }
        }
        if (nb == na)
        {
            LbDrawPixelClipSolid(x - na, y - nb, colour);
            LbDrawPixelClipSolid(x + na, y - nb, colour);
            LbDrawPixelClipSolid(x - na, y + nb, colour);
            LbDrawPixelClipSolid(x + na, y + nb, colour);
            LbDrawPixelClipSolid(x - nb, y - na, colour);
            LbDrawPixelClipSolid(x + nb, y - na, colour);
            LbDrawPixelClipSolid(x - nb, y + na, colour);
            LbDrawPixelClipSolid(x + nb, y + na, colour);
        }
    }


}

void LbDrawCircle(int64_t x, int64_t y, int64_t radius, TbPixel colour)
{
    if ((RendererGetDrawFlags() & Lb_SPRITE_OUTLINE) != 0)
        LbDrawCircleOutline(x, y, radius, colour);
    else
        LbDrawCircleFilled(x, y, radius, colour);
}

/******************************************************************************/
/* gpu-v2 Phase C.2: while a GPU world frame is being recorded (see
 * RendererWorldFrameBegin()), the scaled-sprite dispatchers call this instead
 * of rasterizing. It turns the scaling step tables the CPU path would have
 * used (xsteps_array/ysteps_array, already flipped/clipped for this draw)
 * into per-destination-column/row source lookups, so the GPU reproduces the
 * same source->destination pixel mapping, and records the sprite through
 * RendererWorldFrameAddSprite(). Returns true when the draw was recorded (the
 * caller then skips its CPU rasterization), false to let the CPU draw. */
#define CAPTURE_MAX_DST 4096
TbBool SwCaptureSprite(int64_t posx, int64_t posy, const unsigned char *rle, int64_t width, int64_t height,
                       uint32_t mode, const TbPixel *cmap, TbPixel colour)
{
    static uint16_t xmap[CAPTURE_MAX_DST];
    static uint16_t ymap[CAPTURE_MAX_DST];
    static uint32_t cmap32[256];

    if (!RendererWorldFrameCapturing() || rle == NULL || width <= 0 || height <= 0)
        return false;
    // Sprite coordinates are relative to the graphics window; the recorded
    // frame is relative to the rasterizer's vec window. They coincide while
    // the engine draws the world view -- if not, leave this draw to the CPU.
    if (SwTargetGraphicsWindowPtr() != RendererWorldFrameWindow())
        return false;

    const int64_t flags = RendererGetDrawFlags();
    const TbBool flip_h = (flags & Lb_SPRITE_FLIP_HORIZ) != 0;
    const TbBool flip_v = (flags & Lb_SPRITE_FLIP_VERTIC) != 0;

    int64_t min_x = INT64_MAX, max_x = INT64_MIN, min_y = INT64_MAX, max_y = INT64_MIN;
    for (int64_t j = 0; j < width; j++)
    {
        const int64_t idx = flip_h ? (posx + width - 1 - j) : (posx + j);
        if (idx < 0 || idx >= SPRITE_SCALING_XSTEPS) continue;
        const int64_t pos = xsteps_array[2 * idx], dup = xsteps_array[2 * idx + 1];
        if (dup <= 0) continue;
        if (pos < min_x) min_x = pos;
        if (pos + dup > max_x) max_x = pos + dup;
    }
    for (int64_t j = 0; j < height; j++)
    {
        const int64_t idx = flip_v ? (posy + height - 1 - j) : (posy + j);
        if (idx < 0 || idx >= SPRITE_SCALING_YSTEPS) continue;
        const int64_t pos = ysteps_array[2 * idx], dup = ysteps_array[2 * idx + 1];
        if (dup <= 0) continue;
        if (pos < min_y) min_y = pos;
        if (pos + dup > max_y) max_y = pos + dup;
    }
    if (max_x <= min_x || max_y <= min_y)
        return true; // fully clipped away: nothing to draw, nothing left for the CPU either
    const int64_t dst_w = max_x - min_x, dst_h = max_y - min_y;
    if (dst_w > CAPTURE_MAX_DST || dst_h > CAPTURE_MAX_DST || width > 0xFFFE || height > 0xFFFE)
        return false;

    for (int64_t i = 0; i < dst_w; i++) xmap[i] = 0xFFFF;
    for (int64_t i = 0; i < dst_h; i++) ymap[i] = 0xFFFF;
    for (int64_t j = 0; j < width; j++)
    {
        const int64_t idx = flip_h ? (posx + width - 1 - j) : (posx + j);
        if (idx < 0 || idx >= SPRITE_SCALING_XSTEPS) continue;
        const int64_t pos = xsteps_array[2 * idx], dup = xsteps_array[2 * idx + 1];
        for (int64_t k = 0; k < dup; k++)
            if (pos + k >= min_x && pos + k < max_x) xmap[pos + k - min_x] = (uint16_t)j;
    }
    for (int64_t j = 0; j < height; j++)
    {
        const int64_t idx = flip_v ? (posy + height - 1 - j) : (posy + j);
        if (idx < 0 || idx >= SPRITE_SCALING_YSTEPS) continue;
        const int64_t pos = ysteps_array[2 * idx], dup = ysteps_array[2 * idx + 1];
        for (int64_t k = 0; k < dup; k++)
            if (pos + k >= min_y && pos + k < max_y) ymap[pos + k - min_y] = (uint16_t)j;
    }

    const uint32_t *cmap_ptr = NULL;
    if (cmap != NULL)
    {
        for (int i = 0; i < 256; i++)
            cmap32[i] = (uint32_t)cmap[i].r | ((uint32_t)cmap[i].g << 8) | ((uint32_t)cmap[i].b << 16) | ((uint32_t)cmap[i].a << 24);
        cmap_ptr = cmap32;
    }
    uint32_t rgba = (uint32_t)colour.r | ((uint32_t)colour.g << 8) | ((uint32_t)colour.b << 16) | ((uint32_t)colour.a << 24);
    // Per-pixel lighting (see RendererSpriteLightSet): an opaque thing sprite is recorded as WFS_LIT with
    // its base shade so the GPU can light it per pixel -- with its palette colours if it was plainly shaded,
    // or its tint/flash colour table if it was tinted.
    const int64_t lit_shade = RendererSpriteLightGet();
    if (mode == WFS_SOLID && lit_shade >= 0 && RendererWorldFrameHasLighting() &&
        (cmap == NULL || cmap == lbSpriteReMapPtr))
    {
        mode = WFS_LIT;
        if (cmap != NULL && lb_remap_is_shade)
            cmap_ptr = NULL; // a plain shade table: use the palette colours and shade per pixel instead
        // else: a tint / flash table -- kept as the sprite's colour table, lit on top of it
        rgba = (uint32_t)lit_shade; // base shade x256 (8.8)
    }
    RendererWorldFrameAddSprite(rle, (int32_t)width, (int32_t)height, (int32_t)min_x, (int32_t)min_y, (int32_t)dst_w, (int32_t)dst_h,
                                xmap, ymap, cmap_ptr, mode, rgba);
    return true;
}

/* gpu-v2 Phase C.5: a solid rectangle (window-relative, clipped to the window)
 * drawn while a world frame is being recorded -- selection outlines, health
 * bars, room flags -- recorded as a one-colour 1x1 sprite stretched to the rect,
 * so it lands in painter's order between the terrain/sprites around it instead
 * of on top of everything. Returns true when recorded (or fully clipped). */
static const unsigned char capture_solid_1x1[3] = { 1, 1, 0 };
static uint16_t capture_zero_map[CAPTURE_MAX_DST];
TbBool SwCaptureRect(int64_t x, int64_t y, int64_t w, int64_t h, TbPixel colour)
{
    if (!RendererWorldFrameCapturing() || colour.a != 255)
        return false;
    if (SwTargetGraphicsWindowPtr() != RendererWorldFrameWindow())
        return false;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SwTargetWindowWidth()) w = SwTargetWindowWidth() - x;
    if (y + h > SwTargetWindowHeight()) h = SwTargetWindowHeight() - y;
    if (w <= 0 || h <= 0)
        return true;
    if (w > CAPTURE_MAX_DST || h > CAPTURE_MAX_DST)
        return false;
    const uint32_t rgba = (uint32_t)colour.r | ((uint32_t)colour.g << 8) | ((uint32_t)colour.b << 16) | ((uint32_t)colour.a << 24);
    RendererWorldFrameAddSprite(capture_solid_1x1, 1, 1, (int32_t)x, (int32_t)y, (int32_t)w, (int32_t)h,
                                capture_zero_map, capture_zero_map, NULL, WFS_ONECOLOUR, rgba);
    return true;
}

void setup_steps(int64_t posx, int64_t posy, const struct TbSourceBuffer * src_buf, int64_t **xstep, int64_t **ystep, int64_t *scanline)
{
    int64_t sposx;
    int64_t sposy;
    sposx = posx;
    sposy = posy;
    (*scanline) = SwTargetScanline();
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0) {
        sposx = src_buf->width + posx - 1;
    }
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_VERTIC) != 0) {
        sposy = src_buf->height + posy - 1;
        (*scanline) = -SwTargetScanline();
    }
    (*xstep) = &xsteps_array[2 * sposx];
    (*ystep) = &ysteps_array[2 * sposy];
}

void setup_outbuf(const int64_t *xstep, const int64_t *ystep, TbPixel **outbuf, int64_t *outheight)
{
    int64_t gspos_x;
    int64_t gspos_y;
    gspos_y = ystep[0];
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_VERTIC) != 0)
        gspos_y += ystep[1] - 1;
    gspos_x = xstep[0];
    if ((RendererGetDrawFlags() & Lb_SPRITE_FLIP_HORIZ) != 0)
        gspos_x += xstep[1] - 1;
    (*outbuf) = &SwTargetGraphicsWindowPtr()[gspos_x + SwTargetScanline() * gspos_y];
    (*outheight) = SwTargetScreenHeight();
}

/******************************************************************************/
#ifdef __cplusplus
}
#endif
