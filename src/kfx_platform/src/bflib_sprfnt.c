/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sprfnt.c
 *     Bitmap sprite fonts support library.
 * @par Purpose:
 *     Functions for reading bitmap sprite fonts and using them to display text.
 * @par Comment:
 *     None.
 * @author   Tomasz Lis
 * @date     29 Dec 2008 - 11 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/RendererManager.h"
#include "bflib_sprfnt.h"

#include <stdarg.h>
#include "bflib_basics.h"
#include "globals.h"

#include "bflib_sprite.h"
#include "bflib_fileio.h"
#include "bflib_vidraw.h"
#include "bflib_text.h"
#include "bflib_string.h"

#include "post_inc.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

// See bf_sprfnt_set_language_lwrstr()/bf_sprfnt_set_fxdata_dir() and
// docs/refactor/stage-02-decouple-bflib.md.
static char bf_sprfnt_language_lwrstr[4] = "eng";
static char bf_sprfnt_fxdata_dir[4096] = "";

void bf_sprfnt_set_language_lwrstr(const char *language_lwrstr)
{
    snprintf(bf_sprfnt_language_lwrstr, sizeof(bf_sprfnt_language_lwrstr), "%s", language_lwrstr);
}

void bf_sprfnt_set_fxdata_dir(const char *fxdata_dir)
{
    snprintf(bf_sprfnt_fxdata_dir, sizeof(bf_sprfnt_fxdata_dir), "%s", fxdata_dir);
}

static enum TbFontRole default_font_role_resolver(const struct TbSpriteSheet *font)
{
    return FontRole_Unknown;
}

static TbFontRoleResolverFn bf_sprfnt_font_role_resolver = default_font_role_resolver;

void bf_sprfnt_set_font_role_resolver(TbFontRoleResolverFn resolver_fn)
{
    bf_sprfnt_font_role_resolver = resolver_fn ? resolver_fn : default_font_role_resolver;
}

struct AsianDraw {
  uint64_t draw_char;
  uint64_t bits_width;
  uint64_t bits_height;
  uint64_t character_spacing;
  uint64_t vertical_offset;
  uint64_t y_spacing;
  unsigned char *sprite_data;
};

/**
 * Defines a font drawing window.
 * Values are signed to ease comparison with negative values.
 */
struct AsianFontWindow {
  int64_t width;
  int64_t height;
  int64_t scanline;
  TbPixel *buf_ptr;
};

struct AsianFont {
  const char *fname;
  unsigned char *data;
  int64_t *widths;
  uint64_t *offsets;
  uint64_t height;
  uint64_t narrow_spacing;
  uint64_t wide_spacing;
  uint64_t baseline_offset;
  uint64_t line_spacing;
};

/******************************************************************************/

#define DOUBLE_UNDERLINE_BOUND 16

int64_t dbc_colour0 = 0;
int64_t dbc_colour1 = 0;
TbBool dbc_initialized = false;
TbBool dbc_enabled = true;
const struct TbSpriteSheet *lbFontPtr;

#define UNIFONT_INDEX_COUNT 65536
#define UNIFONT_INDEX_SIZE 6

static TbGraphicsWindow lbTextJustifyWindow;
static TbGraphicsWindow lbTextClipWindow;
static unsigned char lbSpacesPerTab;
struct AsianFont *active_dbcfont = NULL;



struct AsianFont dbcfonts[] = {
  {"font12", NULL,NULL,NULL , 12, 0, 1, 1, 1},
  {"font16", NULL,NULL,NULL , 16, 0, 1, 4, 2},
  //{"font24", NULL,NULL,NULL , 24, 0, 1, 4, 2},
};

/******************************************************************************/


static int64_t dbc_char_height(uint64_t chr)
{
    if (active_dbcfont == NULL)
        return 0;
    return active_dbcfont->height + active_dbcfont->line_spacing + active_dbcfont->baseline_offset;
}

static int64_t dbc_char_width(uint64_t chr)
{
    if (chr > 0xFFFF || active_dbcfont == NULL || active_dbcfont->widths == NULL)
        return 0;
    return active_dbcfont->widths[(uint64_t)chr] + active_dbcfont->wide_spacing;
}

/** Returns if the given char starts a wide charcode.
 * @param chr
 */
static TbBool is_duospace_char(uint64_t chr)
{
    if (chr < 0xFF)
        return false;
    return dbc_char_width(chr) >= 16;
}

/**
 * Draws an underline below the character.
 * @param pos_x
 * @param pos_y
 * @param width
 * @param height
 * @param draw_colr
 * @param shadow_colr
 */
/* draw_colr/shadow_colr are already-resolved TbPixel colours -- callers
 * resolve their own raw palette index (or remap) before calling, since
 * draw_dbc_char() needs the remapped colour, not a fresh
 * expand_indexed_pixel() of the raw index. */
static void LbDrawCharUnderline(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height, int64_t units_per_px, TbPixel draw_colr, TbPixel shadow_colr)
{
    if (units_per_px < 1)
        units_per_px = 1;
    // The bound checks follow the original code, so use the unscaled height for them
    int64_t base_height = height * 16 / units_per_px;
    // Stroke thickness scaled the same way the font sprites are
    int64_t thickness = ((base_height > DOUBLE_UNDERLINE_BOUND) ? 2 : 1) * units_per_px / 16;
    if (thickness < 1)
        thickness = 1;
    int64_t h = height;
    int64_t w = width;
    // Draw shadow
    if ((RendererGetDrawFlags() & Lb_TEXT_UNDERLNSHADOW) != 0) {
        int64_t shadow_off = ((base_height > 2*DOUBLE_UNDERLINE_BOUND) ? 2 : 1) * units_per_px / 16;
        if (shadow_off < 1)
            shadow_off = 1;
        int64_t shadow_x = pos_x + shadow_off;
        for (int64_t i = 0; i < thickness; i++) {
            LbDrawHVLine(shadow_x, pos_y+h, shadow_x+w, pos_y+h, shadow_colr);
            h--;
        }
    }
    // Draw underline
    for (int64_t i = 0; i < thickness; i++) {
        LbDrawHVLine(pos_x, pos_y+h, pos_x+w, pos_y+h, draw_colr);
        h--;
    }
}

static int64_t dbc_get_sprite_for_char(struct AsianDraw *adraw, uint64_t chr)
{
    SYNCDBG(19,"Starting");
    if (adraw == NULL)
        return 4;
    if (active_dbcfont == NULL)
        return 4;
    if (active_dbcfont->widths == NULL)
        return 4;
    if (chr > 0xFFFF)
        return 6;
    const uint64_t codepoint = (uint64_t)chr;
    int64_t width = active_dbcfont->widths[codepoint];
    if (width == 0)
        return 6;
    uint64_t offset = active_dbcfont->offsets[codepoint];
    adraw->draw_char = chr;
    adraw->bits_width = width;
    adraw->bits_height = active_dbcfont->height;

    if (is_duospace_char(chr))
        adraw->character_spacing = active_dbcfont->wide_spacing;
    else
        adraw->character_spacing = active_dbcfont->narrow_spacing;

    adraw->vertical_offset = active_dbcfont->baseline_offset;
    adraw->y_spacing = active_dbcfont->line_spacing;
    adraw->sprite_data = active_dbcfont->data + offset;
    return 0;
}

/* colr1/colr2 used to be `short`, with the "don't draw this bit value"
 * sentinel spelled as any value outside 0-255 (checked via `colour & 0xFF00`)
 * -- typically -1. Now that they're resolved TbPixel colours, the same
 * "don't draw" case is spelled as TbPixel_Transparent, checked via
 * TbPixel_IsTransparent(). */
static int64_t dbc_draw_font_sprite(TbPixel *dst_buf, int64_t dst_scanline, unsigned char *src_buf,
      int64_t src_bitwidth, int64_t start_x, int64_t start_y, int64_t width, int64_t height,
      TbPixel colr1, TbPixel colr2)
{
    SYNCDBG(19,"Starting at %" PRId64 ",%" PRId64 " size %" PRId64 ",%" PRId64,(int64_t)start_x, (int64_t)start_y, (int64_t)width, (int64_t)height);
    // Computing width in bytes from the number of bits
    int64_t src_scanline = src_bitwidth >> 3;
    if ((src_bitwidth & 7) != 0)
        src_scanline++;
    if (start_y != 0)
        src_buf += src_scanline * (int64_t)start_y;
    int64_t src_val = 0;
    for (int64_t y = height; y > 0; y--)
    {
        unsigned char* src = src_buf;
        TbPixel* dst = dst_buf;
        int64_t skip_count = start_x;
        for (int64_t x = 0; x < start_x + width; x++)
        {
          if ((x & 7) == 0)
            src_val = *src++;
          src_val <<= 1;
          TbPixel colour;
          if ((src_val & 0x100) != 0)
              colour = colr1;
          else
            colour = colr2;
          if (skip_count > 0)
          {
            skip_count--;
            continue;
          }
          if (!TbPixel_IsTransparent(colour))
            *dst = colour;
          dst++;
        }
        src_buf += src_scanline;
        dst_buf += dst_scanline;
    }
    return 0;
}

/* colr1/colr2/colr3 used to be `short`, "don't draw" spelled as a
 * negative sentinel; see dbc_draw_font_sprite()'s comment for the
 * TbPixel_IsTransparent() replacement. */
static int64_t dbc_draw_font_sprite_text(const struct AsianFontWindow *awind, const struct AsianDraw *adraw,
      int64_t pos_x, int64_t pos_y, TbPixel colr1, TbPixel colr2, TbPixel colr3)
{
    int64_t scr_x;
    int64_t scr_y;
    TbPixel *dst_buf;
    int64_t width;
    int64_t height;
    int64_t x;
    int64_t y;
    SYNCDBG(19,"Starting");
    if ((adraw == NULL) || (awind == NULL))
      return 4;
    if ((adraw->sprite_data == NULL) || (awind->buf_ptr == NULL))
      return 4;
    if (!TbPixel_IsTransparent(colr3))
    {
      x = 0;
      y = 0;
      scr_y = adraw->vertical_offset + pos_y + 1;
      scr_x = pos_x + 1;
      width = adraw->bits_width;
      height = adraw->bits_height;
      if (scr_x < 0)
      {
        width += scr_x;
        if (width <= 0)
          goto skip_sprite_draw;
        x = -scr_x;
        scr_x = 0;
        if (width > awind->width)
          width = awind->width;
      } else
      if ((int64_t) (scr_x + adraw->bits_width) > awind->width)
      {
        if (scr_x >= awind->width)
          goto skip_sprite_draw;
        width = awind->width - scr_x;
      }
      if (scr_y < 0)
      {
        height += scr_y;
        if (height > 0)
        {
          y = -scr_y;
          scr_y = 0;
          if (height > awind->height)
            height = awind->height;
          if ((width != 0) && (height != 0))
          {
            if ((scr_x < 0) || (scr_x >= awind->width) || (scr_y < 0) || (scr_y >= awind->height))
              goto skip_sprite_draw;
            dst_buf = &awind->buf_ptr[awind->scanline * scr_y + scr_x];
            dbc_draw_font_sprite(dst_buf, awind->scanline, adraw->sprite_data, adraw->bits_width, x, y, width, height, colr3, TbPixel_Transparent);
          }
        }
      } else
      if (height+scr_y > awind->height)
      {
        if (scr_y < awind->height)
        {
          height = awind->height - scr_y;
          if ((width != 0) && (height != 0))
          {
            if ((scr_x < 0) || (scr_x >= awind->width) || (scr_y < 0) || (scr_y >= awind->height))
              goto skip_sprite_draw;
            dst_buf = &awind->buf_ptr[awind->scanline * scr_y + scr_x];
            dbc_draw_font_sprite(dst_buf, awind->scanline, adraw->sprite_data, adraw->bits_width, x, y, width, height, colr3, TbPixel_Transparent);
          }
        }
      }
    }
skip_sprite_draw:
    if (!TbPixel_IsTransparent(colr1) || !TbPixel_IsTransparent(colr2))
    {
      y = 0;
      x = 0;
      width = adraw->bits_width;
      height = adraw->bits_height;
      scr_y = pos_y + adraw->vertical_offset;
      scr_x = pos_x;
      if (pos_x >= 0)
      {
        if (width + pos_x > awind->width)
        {
          if (pos_x >= awind->width)
            return 0;
          width = awind->width - pos_x;
        }
      } else
      {
        width += pos_x;
        if (width <= 0)
          return 0;
        scr_x = 0;
        x = -pos_x;
        if (width > awind->width)
          width = awind->width;
      }
      if (scr_y >= 0)
      {
        if (height + scr_y > awind->height)
        {
          if (scr_y >= awind->height)
            return 0;
          height = awind->height - scr_y;
        }
      } else
      {
        height += scr_y;
        if (height <= 0)
          return 0;
        y = -scr_y;
        scr_y = 0;
        if (height > awind->height)
          height = awind->height;
      }
      if ((width != 0) && (height != 0))
      {
        if ((scr_x < 0) || (scr_x >= awind->width) || (scr_y < 0) || (scr_y >= awind->height))
          return 4;
        dst_buf = &awind->buf_ptr[awind->scanline * scr_y + scr_x];
        dbc_draw_font_sprite(dst_buf, awind->scanline, adraw->sprite_data, adraw->bits_width, x, y, width, height, colr1, colr2);
      }
    }
    return 0;
}

static int64_t get_bit_to_array(unsigned char* arrD, int64_t iX, int64_t iY, int64_t iMax)
{
    int64_t iRet = 0;
    int64_t iBytePos = 0;
    int64_t iModBitPos = 0;
    int64_t iPos = (iY * iMax + iX);

    iBytePos = iPos / 8;
    iModBitPos = iPos % 8;

    iRet = (*(arrD + iBytePos) & (0x80 >> iModBitPos)) == (0x80 >> iModBitPos) ? 1 : 0;

    return iRet;
}

static void set_bit_to_array(unsigned char* arrD, int64_t iX, int64_t iY, int64_t iMax, int64_t iValue)
{
    int64_t iBytePos = 0;
    int64_t iModBitPos = 0;
    int64_t iPos = (iY * iMax + iX);

    iBytePos = iPos / 8;
    iModBitPos = iPos % 8;

    if (iValue == 1)
        *(arrD + iBytePos) |= 0x80 >> iModBitPos;
    else
        *(arrD + iBytePos) &= ~(0x80 >> iModBitPos);
}

static int8_t draw_dbc_char(uint64_t chr, struct AsianFontWindow *awind, int64_t *pos_x, int64_t pos_y, int64_t units_per_px)
{
    SYNCDBG(19,"Got needs_draw");
    struct AsianDraw adraw;
    TbPixel colour;
    TbPixel shadow_colour;
    uint8_t colour_idx;
    uint8_t shadow_idx = (uint8_t)dbc_colour1;
    if (dbc_get_sprite_for_char(&adraw, chr) == 0)
    {
        if ((RendererGetDrawFlags() & Lb_TEXT_ONE_COLOR) == 0)
          colour_idx = (uint8_t)dbc_colour0;
        else
          colour_idx = RendererGetDrawColour();
        if ((RendererGetDrawFlags() & Lb_TEXT_REMAP) != 0) {
            colour = lbSpriteReMapPtr[colour_idx];
            shadow_colour = lbSpriteReMapPtr[shadow_idx];
        } else {
            const unsigned char *palette = RendererGetActivePalette();
            colour = expand_indexed_pixel(colour_idx, palette);
            shadow_colour = expand_indexed_pixel(shadow_idx, palette);
        }

        #define MAX_DBC_SPRITE_SIZE 8192
        unsigned char dest_pixel[MAX_DBC_SPRITE_SIZE] = { 0 };
        if (units_per_px != 16)
        {            
            // Needs to be a multiple of 8
            int64_t iDstSizeH = (units_per_px / 8) * 8;
            int64_t iDstSizeW = (units_per_px * adraw.bits_width / 16 / 8) * 8;
            
            double scale_factorX = (double)adraw.bits_width / (double)iDstSizeW;
            double scale_factorY = (double)adraw.bits_height / (double)iDstSizeH;

            if ((iDstSizeW * iDstSizeH) > MAX_DBC_SPRITE_SIZE)
            {
                ERRORLOG("DBC sprite size %" PRId64 ",%" PRId64 " exceeds max %" PRId64,(int64_t)(iDstSizeW),(int64_t)(iDstSizeH),(int64_t)(MAX_DBC_SPRITE_SIZE));
                return -1;
            }
            for (int64_t sY = 0; sY < iDstSizeH; sY++)
            {
                for (int64_t sX = 0; sX < iDstSizeW; sX++)
                {
                    set_bit_to_array(dest_pixel, sX, sY, iDstSizeW, get_bit_to_array(adraw.sprite_data, (int64_t)(sX * scale_factorX), (int64_t)(sY * scale_factorY), adraw.bits_width));
                }
            }

            adraw.bits_width = iDstSizeW;
            adraw.bits_height = iDstSizeH;
            adraw.sprite_data = dest_pixel;
            adraw.character_spacing = adraw.character_spacing * units_per_px / 16;
            adraw.vertical_offset = adraw.vertical_offset * units_per_px / 16;
            adraw.y_spacing = adraw.y_spacing * units_per_px / 16;
        }

        dbc_draw_font_sprite_text(awind, &adraw, *pos_x, pos_y, colour, TbPixel_Transparent, shadow_colour);

        int64_t w;
        if (adraw.bits_height == 16)
        {
           w = (adraw.character_spacing + adraw.bits_width) * units_per_px / 16;
        }
        else
        {
            w = (adraw.character_spacing + adraw.bits_width);
        }
        if ((RendererGetDrawFlags() & Lb_TEXT_UNDERLINE) != 0)
        {
            int64_t h = adraw.bits_height * units_per_px / 16;
            LbDrawCharUnderline(*pos_x,pos_y,w,h,units_per_px,colour,expand_indexed_pixel(lbDisplayEx.ShadowColour, RendererGetActivePalette()));
        }
        *pos_x += w;
        if (*pos_x >= awind->width)
        {
          return -1;
        }
    }
    return 0;
}

static int8_t draw_simpletext_char(uint64_t chr, int64_t *pos_x, int64_t pos_y, int64_t units_per_px)
{
    const struct TbSprite *spr = LbFontCharSprite(lbFontPtr, chr);
    if (spr != NULL)
    {
        if ((RendererGetDrawFlags() & Lb_TEXT_ONE_COLOR) != 0) {
            LbSpriteDrawResizedOneColourImmediate(*pos_x, pos_y, units_per_px, spr, expand_indexed_pixel(RendererGetDrawColour(), RendererGetActivePalette()));
        }
        else if ((RendererGetDrawFlags() & Lb_TEXT_REMAP) != 0) {
            LbSpriteDrawResizedRemap(*pos_x, pos_y, units_per_px, spr, lbSpriteReMapPtr);
        }
        else {
            LbSpriteDrawResizedImmediate(*pos_x, pos_y, units_per_px, spr);
        }
        int64_t w = spr->SWidth * units_per_px / 16;
        if ((RendererGetDrawFlags() & Lb_TEXT_UNDERLINE) != 0)
        {
            int64_t h = LbTextLineHeight() * units_per_px / 16;
            LbDrawCharUnderline(*pos_x, pos_y, w, h, units_per_px, expand_indexed_pixel(RendererGetDrawColour(), RendererGetActivePalette()), expand_indexed_pixel(lbDisplayEx.ShadowColour, RendererGetActivePalette()));
        }
        *pos_x += w;
        return 1;
    }
    return 0;
}

static int8_t draw_char(uint64_t chr, struct AsianFontWindow *awind, int64_t *pos_x, int64_t pos_y, int64_t units_per_px)
{
    if ((dbc_initialized) && (dbc_enabled))
    {
        return draw_dbc_char(chr, awind, pos_x, pos_y, units_per_px);
    } else
    {
        if (draw_simpletext_char(chr, pos_x, pos_y, units_per_px) == 0)
            return draw_dbc_char(chr, awind, pos_x, pos_y, units_per_px);
    }
    return 0;
}


/**
 * Puts scaled simple text sprites on screen.
 * @param sbuf
 * @param ebuf
 * @param x
 * @param y
 * @param len
 */
static void put_down_sprites(const char *sbuf, const char *ebuf, int64_t x, int64_t y, int64_t space_len, int64_t units_per_px)
{
  const char *c;
  int64_t w;
  int64_t h;
    struct AsianFontWindow awind;
    awind.buf_ptr = lbDisplay.GraphicsWindowPtr;
    awind.width = lbDisplay.GraphicsWindowWidth;
    awind.height = lbDisplay.GraphicsWindowHeight;
    awind.scanline = lbDisplay.GraphicsScreenWidth;
  for (c=sbuf; c < ebuf; )
  {
    size_t seq_len;
    uint64_t chr = read_utf_8_codepoint((const char *)c, &seq_len);
    c += seq_len;

    if (chr > colour_modifiers_begin && chr < colour_modifiers_end)
    {
        RendererSetDrawColour((unsigned char)(chr - colour_modifiers_begin));
    } else
    if (chr == 0xA0 || chr == ' ') //NO-BREAK SPACE or SPACE
    {
        w = space_len;
        if ((RendererGetDrawFlags() & Lb_TEXT_UNDERLINE) != 0)
        {
            h = LbTextLineHeight() * units_per_px / 16;
            LbDrawCharUnderline(x,y,w,h,units_per_px,expand_indexed_pixel(RendererGetDrawColour(), RendererGetActivePalette()),expand_indexed_pixel(lbDisplayEx.ShadowColour, RendererGetActivePalette()));
        }
        x += w;
    } else
    if (chr > 32)
    {
        if (draw_char(chr, &awind, &x, y, units_per_px) < 0)
        {
            return;
        }
    } else
    if (chr == '\t')
    {
        w = space_len*(int64_t)lbSpacesPerTab;
        if ((RendererGetDrawFlags() & Lb_TEXT_UNDERLINE) != 0)
        {
            h = LbTextLineHeight() * units_per_px / 16;
            LbDrawCharUnderline(x,y,w,h,units_per_px,expand_indexed_pixel(RendererGetDrawColour(), RendererGetActivePalette()),expand_indexed_pixel(lbDisplayEx.ShadowColour, RendererGetActivePalette()));
        }
        x += w;
    } else
    {
      switch (chr)
      {
        case DKChr_Modifier_Transparent4:
          RendererToggleDrawFlags(Lb_SPRITE_TRANSPAR4);
          break;
        case DKChr_Modifier_Transparent8:
          RendererToggleDrawFlags(Lb_SPRITE_TRANSPAR8);
          break;
        case DKChr_Modifier_Outline:
          RendererToggleDrawFlags(Lb_SPRITE_OUTLINE);
          break;
        case DKChr_Modifier_FlipHoriz:
          RendererToggleDrawFlags(Lb_SPRITE_FLIP_HORIZ);
          break;
        case DKChr_Modifier_FlipVertic:
          RendererToggleDrawFlags(Lb_SPRITE_FLIP_VERTIC);
          break;
        case DKChr_Modifier_Underline:
          RendererToggleDrawFlags(Lb_TEXT_UNDERLINE);
          break;
        case DKChr_Modifier_OneColor:
          RendererToggleDrawFlags(Lb_TEXT_ONE_COLOR);
          break;
        case DKChr_NewLine:
            break;
        default:
          break;
      }
    }
  }
}

/**
 * Given text and its scale, returns unscaled height which the text would occupy
 * if drawn with current fornt on current text window.
 *
 * @param units_per_px
 * @param text
 */
int64_t text_string_height(int64_t units_per_px, const char *text)
{
    int64_t nlines = 0;
    if (lbFontPtr == NULL)
      return 0;
    int64_t lnwidth_clip = lbTextJustifyWindow.x - lbTextClipWindow.x;
    int64_t lnwidth = lnwidth_clip;
    for (const char* pchr = text; *pchr != '\0'; )
    {

        size_t seq_len;
        uint64_t chr = read_utf_8_codepoint((const char *)pchr, &seq_len);
        pchr += seq_len;

      int64_t w;
      if (chr > 32)
      {
          w = LbTextCharWidthM(chr, units_per_px);
          if (lnwidth + w - lnwidth_clip > lbTextJustifyWindow.width)
          {
            lnwidth = lnwidth_clip + w;
            nlines++;
          } else
          {
            lnwidth += w;
          }
      } else

      if (chr == ' ')
      {
        if (lnwidth > 0)
        {
          w = LbTextCharWidth(' ') * units_per_px / 16;
          if (lnwidth + w + LbTextWordWidthM(pchr, units_per_px) - lnwidth_clip > lbTextJustifyWindow.width)
          {
            lnwidth = lnwidth_clip;
            nlines++;
          } else
          {
            lnwidth += w;
          }
        }
      } else
      switch (chr)
      {
      case DKChr_Return:
          lnwidth = lnwidth_clip;
          nlines++;
          if (pchr[0] == DKChr_NewLine) pchr++;
          break;
      case DKChr_NewLine:
          lnwidth = lnwidth_clip;
          nlines++;
          break;
      case DKChr_Tab:
          w = LbTextCharWidth(' ') * units_per_px / 16;
          lnwidth += lbSpacesPerTab * w;
          if (lnwidth + LbTextWordWidthM(pchr, units_per_px) - lnwidth_clip > lbTextJustifyWindow.width)
          {
            lnwidth = lnwidth_clip;
            nlines++;
          }
          break;
      }
    }
    nlines++;
    return nlines * (LbTextLineHeight() * units_per_px / 16);
}

/**
 * Draws a string in the current text window in given scale.
 * @param posx Position of the text, X coord.
 * @param posy Position of the text, Y coord.
 * @param units_per_px Scale in pixels; 16 is 100%.
 * @param text The text to be drawn.
 * @return
 */
TbBool LbTextDrawResizedImmediate(int64_t posx, int64_t posy, int64_t units_per_px, const char *text)
{
    // Counter for amount of blank characters in a line
    const char *ebuf;
    int64_t x;
    int64_t y;
    int64_t len;
    if ((lbFontPtr == NULL) || (text == NULL))
        return true;
    TbGraphicsWindow grwnd;
    LbScreenStoreGraphicsWindow(&grwnd);
    LbScreenLoadGraphicsWindow(&lbTextClipWindow);
    int64_t count = 0;
    int64_t justifyx = lbTextJustifyWindow.x - lbTextClipWindow.x;
    int64_t justifyy = lbTextJustifyWindow.y - lbTextClipWindow.y;
    posx += justifyx;
    int64_t startx = posx;
    int64_t starty = posy + justifyy;

    int64_t h = LbTextLineHeight() * units_per_px / 16;
    const char *draw_buffer = text;

    const char* sbuf = draw_buffer;
    
    for (ebuf=draw_buffer; *ebuf != '\0'; )
    {
        const char* text_backup_pointer = ebuf;

        size_t seq_len;
        uint64_t chr = read_utf_8_codepoint((const char *)ebuf, &seq_len);
        ebuf += seq_len;

        int64_t w;
        if ((chr > 32))
        {
            // Align when ansi and unicode are mixed on one screen
            w = LbTextCharWidthM(chr, units_per_px);

            if (is_duospace_char(chr))
            {
                count = 0;
            }
            if ((posx+w-justifyx <= lbTextJustifyWindow.width) || (count > 0) || !LbAlignMethodSet(RendererGetDrawFlags()))
            {
                posx += w;
                continue;
            }
            // If the char exceeds screen, and there were no spaces in that line, and alignment is set - divide the line here
            w = LbTextCharWidthM(' ', units_per_px);
            posx += w;
            x = LbGetJustifiedCharPosX(startx, posx, w, 1, RendererGetDrawFlags());
            y = LbGetJustifiedCharPosY(starty, h, h, RendererGetDrawFlags());
            len = LbGetJustifiedCharWidth(posx, w, count, units_per_px, RendererGetDrawFlags());
            put_down_sprites(sbuf, text_backup_pointer, x, y, len, units_per_px);
            // We already know that alignment is set - don't re-check
            {
                posx = startx;
                sbuf = text_backup_pointer; // sbuf points at start of char for next loop. text_backup_pointer points at unprocessed char.
                ebuf = text_backup_pointer;
                starty += h;
            }
            count = 0;
        } else
        if (chr == ' ')
        {
            w = LbTextCharWidthM(' ', units_per_px);
            len = LbTextWordWidthM(ebuf, units_per_px);
            if (posx+w+len-justifyx <= lbTextJustifyWindow.width)
            {
                count++;
                posx += w;
                continue;
            }
            posx += w;
            x = LbGetJustifiedCharPosX(startx, posx, w, 1, RendererGetDrawFlags());
            y = LbGetJustifiedCharPosY(starty, h, h, RendererGetDrawFlags());
            len = LbGetJustifiedCharWidth(posx, w, count, units_per_px, RendererGetDrawFlags());
            put_down_sprites(sbuf, ebuf, x, y, len, units_per_px);
            // End the line only if align method is set
            if (LbAlignMethodSet(RendererGetDrawFlags()))
            {
              posx = startx;
              sbuf = ebuf; // sbuf should start at the next character, not skip it
              starty += h;
            }
            count = 0;
        } else
        if (chr == '\n')
        {
            w = 0;
            x = LbGetJustifiedCharPosX(startx, posx, w, 1, RendererGetDrawFlags());
            y = LbGetJustifiedCharPosY(starty, h, h, RendererGetDrawFlags());
            len = LbTextCharWidthM(' ', units_per_px);
            y = starty;
            put_down_sprites(sbuf, ebuf, x, y, len, units_per_px);
            // We've got EOL sign - end the line
            sbuf = ebuf;
            posx = startx;
            starty += h;
            count = 0;
        } else
        if (chr == '\t')
        {
            w = LbTextCharWidthM(' ', units_per_px);
            posx += lbSpacesPerTab*w;
            len = LbTextWordWidthM(ebuf, units_per_px);
            if (posx+len-justifyx <= lbTextJustifyWindow.width)
            {
              count += lbSpacesPerTab;
              continue;
            }
            x = LbGetJustifiedCharPosX(startx, posx, w, lbSpacesPerTab, RendererGetDrawFlags());
            y = LbGetJustifiedCharPosY(starty, h, h, RendererGetDrawFlags());
            len = LbGetJustifiedCharWidth(posx, w, count, units_per_px, RendererGetDrawFlags());
            put_down_sprites(sbuf, ebuf, x, y, len, units_per_px);
            if (LbAlignMethodSet(RendererGetDrawFlags()))
            {
              posx = startx;
              sbuf = ebuf;
              starty += h;
            }
            count = 0;
            continue;
        } else
        if ((chr == DKChr_AlignLeft) || (chr == DKChr_AlignRight) || (chr == DKChr_AlignCenter) || (chr == DKChr_AlignJustify))
        {
            if (posx-justifyx > lbTextJustifyWindow.width)
            {
              x = startx;
              y = starty;
              len = LbTextCharWidthM(' ', units_per_px);
              put_down_sprites(sbuf, ebuf, x, y, len, units_per_px);
              posx = startx;
              sbuf = ebuf;
              count = 0;
              starty += h;
            }
            switch (*ebuf)
            {
            case DKChr_AlignLeft:
              RendererToggleDrawFlags(Lb_TEXT_HALIGN_LEFT);
              break;
            case DKChr_AlignRight:
                JUSTLOG("Right align");
              RendererToggleDrawFlags(Lb_TEXT_HALIGN_RIGHT);
              break;
            case DKChr_AlignCenter:
              RendererToggleDrawFlags(Lb_TEXT_HALIGN_CENTER);
              break;
            case DKChr_AlignJustify:
              RendererToggleDrawFlags(Lb_TEXT_HALIGN_JUSTIFY);
              break;
            }
        }
    }
    x = LbGetJustifiedCharPosX(startx, posx, 0, 1, RendererGetDrawFlags());
    y = LbGetJustifiedCharPosY(starty, h, h, RendererGetDrawFlags());
    len = LbTextCharWidthM(' ', units_per_px);
    put_down_sprites(sbuf, ebuf, x, y, len, units_per_px);
    LbScreenLoadGraphicsWindow(&grwnd);
    return true;
}

/** Route a text draw through the renderer, which either records it for this
 *  frame or draws it now. LbTextDrawResizedImmediate is the draw itself.
 */
TbBool LbTextDrawResized(int64_t posx, int64_t posy, int64_t units_per_px, const char *text)
{
    return RendererTextDrawResized(posx, posy, units_per_px, text);
}

/**
 * Draws a string in the current text window.
 * @param posx Position of the text, X coord.
 * @param posy Position of the text, Y coord.
 * @param text The text to be drawn.
 * @return
 */
TbBool LbTextDraw(int64_t posx, int64_t posy, const char *text)
{
    // Using resized version - it will end up with version optimized for no resize anyway
    return LbTextDrawResized(posx, posy, 16, text);
}

/**
 * Draws a formatted string in the current text window.
 * @param posx Position of the text, X coord.
 * @param posy Position of the text, Y coord.
 * @param fmt The text format to be drawn.
 * @return
 */
TbBool LbTextDrawResizedFmt(int64_t posx, int64_t posy, int64_t units_per_px, const char *fmt, ...)
{
    char * text = (char *)malloc(8192);
    if (text == NULL) return false;
    va_list val;
    va_start(val, fmt);
    vsnprintf(text, TEXT_DRAW_MAX_LEN, fmt, val);
    va_end(val);
    TbBool result = LbTextDrawResized(posx, posy, units_per_px, text);
    free(text);
    return result;
}

/** Returns standard height of a line of text, in currently active font.
 *  Supports both sprite fonts and dbc fonts.
 */
int64_t LbTextLineHeight(void)
{
    if ((dbc_initialized) && (dbc_enabled))
    {
      return dbc_char_height(0xFFFF);
    } else
    {
      return LbSprFontCharHeight(lbFontPtr,' ');
    }
}

int64_t LbTextHeight(const char *text)
{
    if ((dbc_initialized) && (dbc_enabled))
    {
      return dbc_char_height(0xFFFF);
    } else
    {
      return LbSprFontCharHeight(lbFontPtr,' ');
    }
}

static int64_t dbc_char_widthM(uint64_t chr, int64_t units_per_px)
{
    if (chr == 0 || active_dbcfont == NULL || active_dbcfont->widths == NULL)
    {
        return 0;
    }

    struct AsianDraw adraw = { 0 };
    if (dbc_get_sprite_for_char(&adraw, chr) == 0)
    {
       return (adraw.bits_width + adraw.character_spacing) * units_per_px / 16;
    }

    return 0;
}

int64_t LbTextCharWidthM(const uint64_t chr, int64_t units_per_px)
{
    if ((dbc_initialized) && (dbc_enabled))
    {
        return dbc_char_widthM(chr, units_per_px);
    }
    else
    {
        return LbSprFontCharWidth(lbFontPtr, chr) * units_per_px / 16;
    }
}

int64_t LbTextCharWidth(const uint64_t chr)
{
    if ((dbc_initialized) && (dbc_enabled))
    {
      return dbc_char_width(chr);
    } else
    {
      return LbSprFontCharWidth(lbFontPtr,(unsigned char)chr);
    }
}

void LbTextUseByteCoding(TbBool is_enabled)
{
    dbc_enabled = is_enabled;
}

int64_t LbTextSetWindow(int64_t posx, int64_t posy, int64_t width, int64_t height)
{
    lbTextJustifyWindow.x = posx;
    lbTextJustifyWindow.y = posy;
    lbTextJustifyWindow.width = width;
    lbTextJustifyWindow.ptr = &RendererGetFramebuffer()[posx + posy * lbDisplay.GraphicsScreenWidth];
    LbTextSetClipWindow(posx, posy, width, height);
    return 1;
}

TbBool LbTextSetFont(const struct TbSpriteSheet *font)
{
    lbFontPtr = font;
    dbc_colour0 = LbTextGetFontFaceColor();
    dbc_colour1 = LbTextGetFontBackColor();


    if (bf_sprfnt_font_role_resolver(lbFontPtr) == FontRole_Frontend0)
    {
        active_dbcfont = &dbcfonts[1];
        // set to 2 when I added a 24 pixel font
        //active_dbcfont = &dbcfonts[2];
    }
    else if (lbDisplay.PhysicalScreenWidth < 512)
        active_dbcfont = &dbcfonts[0];
    else
        active_dbcfont = &dbcfonts[1];

    return true;
}

unsigned char LbTextGetFontFaceColor(void)
{
    switch (bf_sprfnt_font_role_resolver(lbFontPtr)) {
      case FontRole_Frontend0: return 238;
      case FontRole_Frontend1: return 243;
      case FontRole_Frontend2: return 248;
      case FontRole_Frontend3: return 119;
      case FontRole_Win: return 73;
      case FontRole_Sprites: return 1;
      case FontRole_Story: return 237;
      default: return 70;
    }
}

unsigned char LbTextGetFontBackColor(void)
{
    switch (bf_sprfnt_font_role_resolver(lbFontPtr)) {
      case FontRole_Sprites: return 0;
      case FontRole_Story: return 232;
      default: return 1;
    }
}

int64_t LbTextStringPartWidthM(const char *text, int64_t part, int64_t units_per_px)
{
    if (lbFontPtr == NULL)
        return 0;
    int64_t max_len = 0;
    int64_t len = 0;
    for (const char* ebuf = text; *ebuf != '\0'; )
    {
        size_t seq_len;
        uint64_t chr = read_utf_8_codepoint((const char *)ebuf, &seq_len);
        ebuf += seq_len;


        if (part <= 0) break;
        part--;

        if (chr > 31)
        {
            len += LbTextCharWidthM(chr, units_per_px);
        }
        else
            if (chr == '\r')
            {
                if (len > max_len)
                {
                    max_len = len;
                }
                len = 0;
            }
            else
                if (chr == '\t')
                {
                    len += lbSpacesPerTab * LbTextCharWidthM(' ', units_per_px);
                }
                else
                    if ((chr == 6) || (chr == 7) || (chr == 8) || (chr == 9) || (chr == 14))
                    {
                        ebuf++;
                        if (*ebuf == '\0')
                            break;
                    }
    }
    if (len > max_len)
        max_len = len;
    return max_len;
}

/**
 * Returns length of part of a text if drawn on screen.
 * @param text The text to be probed.
 * @param part Amount of characters to be probed.
 * @return Width of the text image, in pixels.
 */
int64_t LbTextStringPartWidth(const char *text, int64_t part)
{
    return LbTextStringPartWidthM(text, part, 16);
}

/**
 * Returns length of given text if drawn on screen.
 * @param text The text to be probed.
 * @return Width of the text image, in pixels.
 */
int64_t LbTextStringWidth(const char *text)
{
    return LbTextStringPartWidth(text, INT_MAX);
}

int64_t LbTextStringWidthM(const char *text, int64_t units_per_px)
{
    if ((dbc_initialized) && (dbc_enabled))
    {
        return LbTextStringPartWidthM(text, INT_MAX, units_per_px);
    }
    else
    {
        return LbTextStringWidth(text) * units_per_px / 16;
    }
}

/* @function
 *   Get the scaled length of word for multiple encodings, that is, compatible with dbc or non-dbc.
 *   Like LbTextCharWidthM, but change from one char to one word.
 *   One word defined as continuous and uninterrupted letters.
 *
 * @param units_per_px Scale in pixels.
 */
int64_t LbTextWordWidthM(const char *str, int64_t units_per_px)
{
    if (str == NULL || str[0] == 0)
        return 0;


    int64_t len = 0;
    const char *sbuf = str;
    while (true)
    {
        size_t seq_len;
        uint64_t chr = read_utf_8_codepoint((const char *)sbuf, &seq_len);
        sbuf += seq_len;

        if ((chr == ' ') || (chr == '\t') || (chr == '\0') || (chr == '\r') || (chr == '\n'))
            break;

        if ((dbc_initialized) && (dbc_enabled))
        {
            if (is_duospace_char(chr))
            {
                if (len != 0)
                    break; // letters before, need to stop.
                return dbc_char_widthM(chr, units_per_px);
            }

            len += dbc_char_widthM(chr, units_per_px);
        }
        else
        {
           len += LbSprFontCharWidth(lbFontPtr, chr) * units_per_px / 16; 
        }
    }
    return len;
}

int64_t LbTextStringHeight(const char *str)
{
    int64_t lines = 1;
    if ((lbFontPtr == NULL) || (str == NULL))
        return 0;
    for (int64_t i = 0; i < MAX_TEXT_LENGTH; i++)
    {
        if (str[i]=='\0') break;
        if (str[i]=='\r') lines++;
    }
    int64_t h = LbTextLineHeight();
    return h*lines;
}

int64_t LbTextNumberDraw(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t number, int64_t fdflags)
{
    if (lbFontPtr == NULL)
      return 0;
    char text[16] = "";
    snprintf(text, sizeof(text), "%" PRId64, (int64_t)(number));
    int64_t h = LbTextLineHeight() * units_per_px / 16;
    int64_t w = LbTextStringWidthM(text, units_per_px);
    switch (fdflags & 0x03)
    {
    case Fnt_LeftJustify:
        LbTextSetWindow(pos_x, pos_y, w, h);
        break;
    case Fnt_RightJustify:
        LbTextSetWindow(pos_x-w, pos_y, w, h);
        break;
    case Fnt_CenterPos:
        LbTextSetWindow(pos_x-(w>>1), pos_y, w, h);
        break;
    }
    LbTextDrawResized(0, 0, units_per_px, text);
    return w;
}

int64_t LbTextStringDraw(int64_t pos_x, int64_t pos_y, int64_t units_per_px, const char *text, int64_t fdflags)
{
    if (lbFontPtr == NULL)
      return 0;
    if (text == NULL)
      return 0;
    int64_t h = LbTextLineHeight() * units_per_px / 16;
    int64_t w = LbTextStringWidthM(text, units_per_px);
    switch (fdflags & 0x03)
    {
    case Fnt_LeftJustify:
        LbTextSetWindow(pos_x, pos_y, w, h);
        break;
    case Fnt_RightJustify:
        LbTextSetWindow(pos_x-w, pos_y, w, h);
        break;
    case Fnt_CenterPos:
        LbTextSetWindow(pos_x-(w>>1), pos_y, w, h);
        break;
    }
    LbTextDrawResized(0, 0, units_per_px, text);
    return w;
}

TbBool LbAlignMethodSet(int64_t fdflags)
{
  const int64_t align_flags =
        Lb_TEXT_HALIGN_LEFT | Lb_TEXT_HALIGN_RIGHT
      | Lb_TEXT_HALIGN_CENTER | Lb_TEXT_HALIGN_JUSTIFY;
  if ((fdflags & align_flags) != 0)
    return true;
  return false;
}

TbResult LbTextSetJustifyWindow(int64_t pos_x, int64_t pos_y, int64_t width)
{
    lbTextJustifyWindow.x = pos_x;
    lbTextJustifyWindow.y = pos_y;
    lbTextJustifyWindow.width = width;
    /* Note: DON'T USE lbTextJustifyWindow_window_ptr in KeeperFX!
    if (lbDisplay.WScreen != NULL)
    {
        lbTextJustifyWindow_window_ptr = lbDisplay.WScreen + pos_x + lbDisplay.GraphicsScreenWidth * pos_y;
    } else
    {
        lbTextJustifyWindow_window_ptr = NULL;
    }*/
    return Lb_SUCCESS;
}

TbResult LbTextSetClipWindow(int64_t pos_x, int64_t pos_y, int64_t width, int64_t height)
{
    int64_t i;
    int64_t start_x = pos_x;
    int64_t start_y = pos_y;
    int64_t end_x = pos_x + width;
    int64_t end_y = pos_y + height;
    if (pos_x > end_x)
    {
      i = pos_x ^ end_x;
      start_x = i ^ pos_x;
      end_x = i ^ pos_x ^ i;
    }
    if ( pos_y > end_y )
    {
      i = pos_y ^ end_y;
      start_y = i ^ pos_y;
      end_y = i ^ pos_y ^ i;
    }
    if ( start_x < 0 )
        start_x = 0;
    if ( end_x < 0 )
      end_x = 0;
    if ( start_y < 0 )
        start_y = 0;
    if ( end_y < 0 )
      end_y = 0;
    if (start_x > lbDisplay.GraphicsScreenWidth)
        start_x = lbDisplay.GraphicsScreenWidth;
    if (end_x > lbDisplay.GraphicsScreenWidth)
      end_x = lbDisplay.GraphicsScreenWidth;
    if (start_y > lbDisplay.GraphicsScreenHeight)
        start_y = lbDisplay.GraphicsScreenHeight;
    if (end_y > lbDisplay.GraphicsScreenHeight)
      end_y = lbDisplay.GraphicsScreenHeight;
    lbTextClipWindow.x = start_x;
    lbTextClipWindow.y = start_y;
    lbTextClipWindow.width = end_x - start_x;
    lbTextClipWindow.height = end_y - start_y;
    /* Note: DON'T USE lbTextClipWindow_window_ptr in KeeperFX!
    lbTextClipWindow_window_ptr = lbDisplay.WScreen + pos_x + lbDisplay.GraphicsScreenWidth * pos_y;
    */
    return Lb_SUCCESS;
}

/**
 * Returns X coordinate for a text character on screen.
 * Takes into account the current text window and justification settings.
 */
int64_t LbGetJustifiedCharPosX(int64_t startx, int64_t all_chars_width, int64_t spr_width, int64_t mul_width, int64_t fdflags)
{
    int64_t justifyx;
    if ((fdflags & Lb_TEXT_HALIGN_LEFT) != 0)
    {
        return startx;
    } else
    if ((fdflags & Lb_TEXT_HALIGN_RIGHT) != 0)
    {
        justifyx = lbTextJustifyWindow.x - lbTextClipWindow.x;
        return startx + (lbTextJustifyWindow.width + justifyx + mul_width*spr_width - all_chars_width);
    } else
    if ((fdflags & Lb_TEXT_HALIGN_CENTER) != 0)
    {
        justifyx = lbTextJustifyWindow.x - lbTextClipWindow.x;
        return startx + (lbTextJustifyWindow.width + justifyx + mul_width*spr_width - all_chars_width) / 2;
    } else
    if ((fdflags & Lb_TEXT_HALIGN_JUSTIFY) != 0)
    {
        return startx;
    }
    return startx;
}

/**
 * Returns Y coordinate for a text character on screen.
 * Takes into account the current text window and justification settings.
 */
int64_t LbGetJustifiedCharPosY(int64_t starty, int64_t all_lines_height, int64_t spr_height, int64_t fdflags)
{
    // No vertical justification supported - so the decision is simple
    return starty;
}

/**
 * Returns width for an empty space between words in text on screen.
 * Takes into account the current text window and justification settings.
 */
int64_t LbGetJustifiedCharWidth(int64_t all_chars_width, int64_t spr_width, int64_t words_count, int64_t units_per_px, int64_t fdflags)
{
    if ((fdflags & Lb_TEXT_HALIGN_JUSTIFY) != 0)
    {
        int64_t space_width = LbTextCharWidth(' ') * units_per_px / 16;
        int64_t justifyx = lbTextJustifyWindow.x - lbTextClipWindow.x;
        if (words_count > 0)
            return spr_width + (lbTextJustifyWindow.width + justifyx + space_width - all_chars_width) / words_count;
        return spr_width;
    }
    return spr_width;
}


/**
 * Computes width of a single character in given font.
 * For characters that don't have a sprite (like tab), returns 0.
 * @note Works only for characters stored in the sprite list.
 *       Multibyte characters are usually stored somewhere else.
 */
int64_t LbSprFontCharWidth(const struct TbSpriteSheet * font, const uint64_t chr)
{
    const struct TbSprite* spr = LbFontCharSprite(font, chr);
    if (spr == NULL)
        return dbc_char_width(chr);
    return spr->SWidth;
}

/**
 * Computes height of a single character in given font.
 * For characters that don't have a sprite (like tab), returns 0.
 * @note Works only for characters stored in the sprite list.
 *       Multibyte characters are usually stored somewhere else.
 */
int64_t LbSprFontCharHeight(const struct TbSpriteSheet * font, const uint64_t chr)
{
    const struct TbSprite* spr = LbFontCharSprite(font, chr);
    if (spr == NULL)
        return dbc_char_height(chr);
    return spr->SHeight;
}

/**
 * Returns sprite of a single character in given font.
 * For characters that don't have a sprite, returns NULL.
 */
const struct TbSprite * LbFontCharSprite(const struct TbSpriteSheet * font, const uint64_t codepoint)
{
    if (font == NULL)
        return NULL;

    uint64_t sprite_index = 0;

    if (codepoint < 0x80){
        sprite_index = codepoint - 31;
    } else if (codepoint >= white_numbers_start && codepoint <= white_numbers_end){
        // white numbers mapped to private use area in unicode
        sprite_index = codepoint - white_numbers_start + 153;
    } else {
      static const struct
      {
          uint64_t unicode;
          int64_t sprite_idx;
      } codepage_map[] = {
        {0x0410, 34 }, {0x0411, 146}, {0x0412, 35 }, {0x0413, 147},
        {0x0414, 148}, {0x0415, 38 }, {0x0416, 149}, {0x0417, 163},
        {0x0418, 164}, {0x0419, 165}, {0x041A, 44 }, {0x041B, 96},
        {0x041C, 46 }, {0x041D, 41 }, {0x041E, 48 }, {0x041F, 115},
        {0x0420, 49 }, {0x0421, 36 }, {0x0422, 53 }, {0x0423, 166},
        {0x0424, 169}, {0x0425, 57 }, {0x0426, 170}, {0x0427, 171},
        {0x0428, 172}, {0x0429, 173}, {0x042A, 67 }, {0x042B, 201},
        {0x042C, 67 }, {0x042D, 124}, {0x042E, 125}, {0x042F, 126},
        {0x0430, 66 }, {0x0431, 174}, {0x0432, 175}, {0x0433, 176},
        {0x0434, 177}, {0x0435, 70 }, {0x0436, 178}, {0x0437, 182},
        {0x0438, 186}, {0x0439, 187}, {0x043A, 127}, {0x043B, 128},
        {0x043C, 135}, {0x043D, 136}, {0x043E, 80 }, {0x043F, 138},
        {0x0440, 81 }, {0x0441, 68 }, {0x0442, 188}, {0x0443, 90 },
        {0x0444, 189}, {0x0445, 89 }, {0x0446, 190}, {0x0447, 192},
        {0x0448, 199}, {0x0449, 200}, {0x044A, 139}, {0x044B, 140},
        {0x044C, 141}, {0x044D, 143}, {0x044E, 144}, {0x044F, 145},
        {0x0407, 185}, {0x0457, 108}, {0x0456, 74 }, {0x0406, 42 },
        {0x0404, 223}, {0x0454, 224}, {0x0490, 225}, {0x0491, 226},
        {0x0451, 106}, {0x0401, 180},
        {0x2019, 8  }, {0x2014, 14 }, 
        {0x00C7, 97 }, {0x00FC, 98 }, {0x00E9, 99 }, {0x00E2, 100},
        {0x00E4, 101}, {0x00E0, 102}, {0x00E5, 103}, {0x00E7, 104},
        {0x00EA, 105}, {0x00EB, 106}, {0x00E8, 107}, {0x00EF, 108},
        {0x00EE, 109}, {0x00EC, 110}, {0x00C4, 111}, {0x00C5, 112},
        {0x00C9, 113}, {0x00E6, 114}, {0x00F4, 116}, {0x00F6, 117},
        {0x00F2, 118}, {0x00FB, 119}, {0x00F9, 120}, {0x00FF, 121},
        {0x00D6, 122}, {0x00DC, 123}, {0x00E1, 129}, {0x00ED, 130},
        {0x00F3, 131}, {0x00FA, 132}, {0x00F1, 133}, {0x00D1, 134},
        {0x00BF, 137}, {0x00A1, 142}, {0x00C1, 150}, {0x00C2, 151},
        {0x00C0, 152}, {0x00E3, 167}, {0x00C3, 168}, {0x00CA, 179},
        {0x00CB, 180}, {0x00C8, 181}, {0x00CD, 183}, {0x00CE, 184},
        {0x00CF, 185}, {0x00CC, 191}, {0x00D3, 193}, {0x00DF, 194},
        {0x00D4, 195}, {0x00D2, 196}, {0x00F5, 197}, {0x00D5, 198},
        {0x00DA, 202}, {0x00DB, 203}, {0x00D9, 204}, {0x00FD, 205},
        {0x00DD, 206}, {0x0105, 207}, {0x0104, 208}, {0x0107, 209},
        {0x0106, 210}, {0x0119, 211}, {0x0118, 212}, {0x0142, 213},
        {0x0141, 214}, {0x0144, 215}, {0x0143, 216}, {0x015B, 217},
        {0x015A, 218}, {0x017A, 219}, {0x0179, 220}, {0x017C, 221},
        {0x017B, 222},
        {0x011B, 70 }, {0x010D, 68 }, {0x010F, 69}, {0x0159, 83},
        {0x0161, 84 }, {0x017E, 91 }, {0x0165, 85}, {0x0148, 79},
        {0x016F, 86 }, {0x010C, 36 }, {0x010E, 37}, {0x0158, 51},
        {0x0160, 52 }, {0x017D, 59 }, {0x0164, 53}, {0x0147, 47},
        {0x016E, 54 }, {0x011A, 38 }, {0x1804, 27}, {0xFE55, 27},
        {0xFE54, 28 }, {0x00C6, 114},

      };
      for (size_t i = 0; i < sizeof(codepage_map) / sizeof(codepage_map[0]); ++i)
      {
          if (codepage_map[i].unicode == codepoint)
          {
              sprite_index = codepage_map[i].sprite_idx;
              break;
          }
      }
    }
    if (sprite_index == 0)
    {
        return NULL;
    }
    return get_sprite(font, sprite_index);
}

static int64_t load_unifont_file(struct AsianFont * dbcfont)
{
    char fpath[4096];
    snprintf(fpath, sizeof(fpath), "%s/%s_%s.fxfont", bf_sprfnt_fxdata_dir, dbcfont->fname, bf_sprfnt_language_lwrstr);

    if ( !LbFileExists(fpath) )
    {
        snprintf(fpath, sizeof(fpath), "%s/%s.fxfont", bf_sprfnt_fxdata_dir, dbcfont->fname);
    }

    int64_t filelen = LbFileLength(fpath);
    if (filelen < UNIFONT_INDEX_COUNT * UNIFONT_INDEX_SIZE)
    {
        ERRORLOG("Unifont file \"%s\" is too small or doesn't exist", fpath);
        return 1;
    }
    TbFileHandle fhandle = LbFileOpen(fpath, Lb_FILE_MODE_READ_ONLY);
    if (!fhandle)
    {
        ERRORLOG("Can't open unifont file \"%s\"", fpath);
        return 1;
    }
    int64_t index_bytes = UNIFONT_INDEX_COUNT * UNIFONT_INDEX_SIZE;
    unsigned char *index_buf = (unsigned char *)malloc(index_bytes);
    if (index_buf == NULL)
    {
        ERRORLOG("Can't allocate memory for unifont index");
        LbFileClose(fhandle);
        return 2;
    }
    if (LbFileRead(fhandle, index_buf, index_bytes) != index_bytes)
    {
        ERRORLOG("Error reading unifont index from \"%s\"", fpath);
        free(index_buf);
        LbFileClose(fhandle);
        return 3;
    }
    int64_t data_len = filelen - index_bytes;
    unsigned char *data_buf = (unsigned char *)malloc(data_len > 0 ? data_len : 1);
    if (data_buf == NULL)
    {
        ERRORLOG("Can't allocate memory for unifont data");
        free(index_buf);
        LbFileClose(fhandle);
        return 2;
    }
    if (data_len > 0 && LbFileRead(fhandle, data_buf, data_len) != data_len)
    {
        ERRORLOG("Error reading unifont data from \"%s\"", fpath);
        free(index_buf);
        free(data_buf);
        LbFileClose(fhandle);
        return 3;
    }
    LbFileClose(fhandle);

    int64_t *widths = (int64_t *)malloc(UNIFONT_INDEX_COUNT * sizeof(*widths));
    uint64_t *offsets = (uint64_t *)malloc(UNIFONT_INDEX_COUNT * sizeof(*offsets));
    if (widths == NULL || offsets == NULL)
    {
        ERRORLOG("Can't allocate memory for unifont index arrays");
        free(index_buf);
        free(data_buf);
        free(widths);
        free(offsets);
        return 2;
    }
    for (uint64_t i = 0; i < UNIFONT_INDEX_COUNT; ++i)
    {
        uint64_t pos = i * UNIFONT_INDEX_SIZE;
        widths[i] = (int64_t)lword(&index_buf[pos]);
        offsets[i] = (uint64_t)llong(&index_buf[pos + 2]);
        if (widths[i] != 0)
        {
            uint64_t row_bytes = (widths[i] + 7) >> 3;
            uint64_t max_offset = offsets[i] + row_bytes * dbcfont->height;
            if ((int64_t)max_offset > data_len)
            {
                ERRORLOG("Invalid unifont offset for codepoint %" PRIu64, (uint64_t)(i));
                free(index_buf);
                free(data_buf);
                free(widths);
                free(offsets);
                return 3;
            }
        }
    }
    free(index_buf);
    dbcfont->data = data_buf;
    dbcfont->widths = widths;
    dbcfont->offsets = offsets;
    return 0;
}

int64_t load_unifont_files()
{
    SYNCDBG(7,"Starting");
    for (int64_t i = 0; i < sizeof(dbcfonts) / sizeof(dbcfonts[0]); ++i)
    {
        load_unifont_file(&dbcfonts[i]);
    }
    return 0;
}



/******************************************************************************/
#ifdef __cplusplus
}
#endif
