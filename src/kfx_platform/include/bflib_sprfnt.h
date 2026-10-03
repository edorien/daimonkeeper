/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_sprfnt.h
 *     Header file for bflib_sprfnt.c.
 * @par Purpose:
 *     Bitmap sprite fonts support library.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     29 Dec 2008 - 11 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_SPRFNT_H
#define BFLIB_SPRFNT_H

#include "bflib_basics.h"
#include "compiler_compat.h"
#include "globals.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEXT_DRAW_MAX_LEN 4096

enum TbFontDrawFlags {
  Fnt_LeftJustify   = 0x00,
  Fnt_RightJustify  = 0x01,
  Fnt_CenterPos     = 0x02,
  Fnt_CenterLeftPos = 0x03,
  };

/******************************************************************************/
#pragma pack(1)

struct TbSprite;
struct TbSetupSprite;

enum DkcodepageLetter {
   DKChr_Null,
   DKChr_Modifier_Transparent4,
   DKChr_Modifier_Transparent8,
   DKChr_Modifier_Outline,
   DKChr_Modifier_FlipHoriz,
   DKChr_Modifier_FlipVertic,
   DKChr_AlignLeft,
   DKChr_AlignRight,
   DKChr_AlignCenter,
   DKChr_AlignJustify = 9, //tab and AlignJustify overlap so Justify can't be reached
   DKChr_Tab = 9,
   DKChr_NewLine,
   DKChr_Modifier_Underline,
   DKChr_Modifier_OneColor,
   DKChr_Return,
   DKChr_Modifier_Colour,
};

// unicode private use area mappings
static const uint64_t white_numbers_start = 0xF000;
static const uint64_t white_numbers_end   = 0xF009;
static const uint64_t colour_modifiers_begin = 0xF100;
static const uint64_t colour_modifiers_end   = 0xF1FF;


extern TbBool dbc_enabled;
extern TbBool dbc_initialized;
extern const struct TbSpriteSheet *lbFontPtr;

/******************************************************************************/


#pragma pack()
/******************************************************************************/
TbBool LbTextDraw(int64_t posx, int64_t posy, const char *text);
#define LbTextDrawFmt(posx, posy, fmt, ...) LbTextDrawResizedFmt(posx, posy, 16, fmt, ##__VA_ARGS__)
TbBool LbTextDrawResized(int64_t posx, int64_t posy, int64_t units_per_px, const char *text);
/** The text draw itself. LbTextDrawResized routes through the renderer first;
 *  the renderer calls this when it is time to actually put pixels down. */
TbBool LbTextDrawResizedImmediate(int64_t posx, int64_t posy, int64_t units_per_px, const char *text);
TbBool LbTextDrawResizedFmt(int64_t posx, int64_t posy, int64_t units_per_px, const char *fmt, ...) KFX_PRINTF_FORMAT(4, 5);
int64_t LbTextHeight(const char *text);
int64_t LbTextLineHeight(void);
int64_t LbTextSetWindow(int64_t posx, int64_t posy, int64_t width, int64_t height);
TbResult LbTextSetJustifyWindow(int64_t pos_x, int64_t pos_y, int64_t width);
TbResult LbTextSetClipWindow(int64_t x1, int64_t y1, int64_t x2, int64_t y2);
TbBool LbTextSetFont(const struct TbSpriteSheet *font);
unsigned char LbTextGetFontFaceColor(void);
unsigned char LbTextGetFontBackColor(void);

// Injected resolver replacing lbFontPtr == <frontend/front_credits global>
// special-casing, so this file doesn't need frontend.h/front_credits.h
// directly. Queried live since the frontend's font pointers get reloaded
// on video mode changes. See docs/refactor/stage-02-decouple-bflib.md.
enum TbFontRole {
    FontRole_Unknown = 0,
    FontRole_Frontend0,
    FontRole_Frontend1,
    FontRole_Frontend2,
    FontRole_Frontend3,
    FontRole_Win,
    FontRole_Sprites,
    FontRole_Story,
};
typedef enum TbFontRole (*TbFontRoleResolverFn)(const struct TbSpriteSheet *font);
void bf_sprfnt_set_font_role_resolver(TbFontRoleResolverFn resolver_fn);
int64_t LbTextStringWidth(const char *str);
int64_t LbTextStringPartWidth(const char *text, int64_t part);
int64_t LbTextStringHeight(const char *str);
int64_t LbTextCharWidth(const uint64_t chr);
int64_t LbTextCharWidthM(const uint64_t chr, int64_t units_per_px);
int64_t LbTextStringWidthM(const char *str, int64_t units_per_px);
int64_t LbTextWordWidthM(const char *str, int64_t units_per_px);

int64_t LbTextNumberDraw(int64_t pos_x, int64_t pos_y, int64_t units_per_px, int64_t number, int64_t fdflags);
int64_t LbTextStringDraw(int64_t pos_x, int64_t pos_y, int64_t units_per_px, const char *text, int64_t fdflags);

// Sub-routines, used for drawing text strings. For use in custom drawing methods.
TbBool LbAlignMethodSet(int64_t fdflags);
int64_t LbGetJustifiedCharPosX(int64_t startx, int64_t all_chars_width, int64_t spr_width, int64_t mul_width, int64_t fdflags);
int64_t LbGetJustifiedCharPosY(int64_t starty, int64_t all_lines_height, int64_t spr_height, int64_t fdflags);
int64_t LbGetJustifiedCharWidth(int64_t all_chars_width, int64_t spr_width, int64_t words_count, int64_t units_per_px, int64_t fdflags);

// Function which require font sprites as parameter
int64_t LbSprFontCharWidth(const struct TbSpriteSheet * font, const uint64_t chr);
int64_t LbSprFontCharHeight(const struct TbSpriteSheet * font,const uint64_t chr);
const struct TbSprite * LbFontCharSprite(const struct TbSpriteSheet * font, const uint64_t chr);

void LbTextUseByteCoding(TbBool is_enabled);
int64_t text_string_height(int64_t units_per_px, const char *text);
int64_t load_unifont_files();

// Registers the resolved (lowercase, 3-char) language code used to locate
// per-language unifont files, so this file doesn't need config_keeperfx.h's
// install_info/get_language_lwrstr() directly. See
// docs/refactor/stage-02-decouple-bflib.md.
void bf_sprfnt_set_language_lwrstr(const char *language_lwrstr);

// Registers the resolved FxData directory (config.h's FGrp_FxData group,
// e.g. via prepare_file_path(FGrp_FxData, "")), so this file doesn't need
// config.h's install-path/mod resolution machinery directly. See
// docs/refactor/stage-02-decouple-bflib.md.
void bf_sprfnt_set_fxdata_dir(const char *fxdata_dir);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
