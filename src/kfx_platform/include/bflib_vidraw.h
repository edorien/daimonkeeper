/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_vidraw.h
 *     Header file for bflib_vidraw.c.
 * @par Purpose:
 *     Graphics canvas drawing library.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     12 Feb 2008 - 10 Jan 2009
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_VIDRAW_H
#define BFLIB_VIDRAW_H

#include "bflib_video.h"

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/
#define MAX_SUPPORTED_SPRITE_DIM 256

#define NUM_DRAWITEMS 238
#define SPRITE_SCALING_XSTEPS max(MAX_SUPPORTED_SPRITE_DIM,MAX_SUPPORTED_SCREEN_WIDTH)
#define SPRITE_SCALING_YSTEPS max(MAX_SUPPORTED_SPRITE_DIM,MAX_SUPPORTED_SCREEN_HEIGHT)

/******************************************************************************/
#pragma pack(1)

struct TiledSprite;
struct TbSprite;
struct TbHugeSprite;

typedef void FlicFunc(void);

struct StartScreenPoint {
        int64_t X;
        int64_t Y;
};

//Note: this name is incorrect! (not from game)
struct LongPoint {
        int64_t X;
        int64_t Y;
};

struct EnginePoint {
        int64_t X;
        int64_t Y;
        int64_t TMapX;
        int64_t TMapY;
        int64_t Shade;
        int64_t coordinate_x_3d;
        int64_t coordinate_y_3d;
        int64_t coordinate_z_3d;
        int64_t DistSqr;
        int64_t padw;
        unsigned char Flags;
        unsigned char padb;
};

struct TbDItmHotspot {
        int64_t X;
        int64_t Y;
};

struct TbDItmFlic {
        FlicFunc *Function;
        TbPixel Colour;
};

struct TbDItmText {
        int64_t WindowX;
        int64_t WindowY;
        int64_t Width;
        int64_t Height;
        int64_t X;
        int64_t Y;
        const char *Text;
        struct TbSprite *Font;
        int64_t Line;
        TbPixel Colour;
};

struct TbDItmSprite {
        int64_t X;
        int64_t Y;
        struct TbSprite *Sprite;
        TbPixel Colour;
};

struct TbDItmTrig {
        int64_t vertex_2_x;
        int64_t vertex_2_y;
        int64_t vertex_3_x;
        int64_t vertex_3_y;
        TbPixel Colour;
};

struct TbDItmTriangle {
        int64_t vertex_1_x;
        int64_t vertex_1_y;
        int64_t vertex_2_x;
        int64_t vertex_2_y;
        int64_t vertex_3_x;
        int64_t vertex_3_y;
        TbPixel Colour;
};

struct TbDItmBox {
        int64_t X;
        int64_t Y;
        int64_t Width;
        int64_t Height;
        TbPixel Colour;
};

struct TbDItmLine {
        int64_t vertex_1_x;
        int64_t vertex_1_y;
        int64_t vertex_2_x;
        int64_t vertex_2_y;
        TbPixel Colour;
};

union TbDItmU {
        struct TbDItmTrig Trig;
        struct TbDItmTriangle Triangle;
        struct TbDItmBox Box;
        struct TbDItmLine Line;
        struct TbDItmSprite Sprite;
        struct TbDItmText Text;
        struct TbDItmFlic Flic;
        struct TbDItmHotspot Hotspot;
};

//Original size (incl. any padding) = 26 bytes
struct PurpleDrawItem {
        union TbDItmU U;
        // pos=23d
        unsigned char Type;
        // pos=24d
        int64_t Flags;
};

struct TbSourceBuffer {
        const void * data;
        uint64_t width;
        uint64_t height;
        uint64_t pitch;
};

/******************************************************************************/
extern TbPixel *poly_screen;
extern TbPixel *vec_screen;
/* vec_map is the texture atlas SOURCE (block_mem-backed) -- stays 8-bit
 * palette-indexed, like TbSpriteData, not a TbPixel framebuffer pointer.
 * See docs/refactor/renderer/02a-pixel-format-design.md §2/§3. */
extern unsigned char *vec_map;
/* The block vec_map points at is 32 << vec_map_shift texels wide (0..3; 0: the 32x32 blocks; -1..-5: the mip
 * levels 16x16 down to 1x1), in the same 256-texel-wide atlas rows; UVs stay in 32-res texels. vec_map_parity: sample as the 32 block would, on a
 * nearest-neighbour upscale (the texture-resolution equivalence test). Read by draw_gpoly() only. */
extern int64_t vec_map_shift;
extern TbBool vec_map_parity;
/* A true-colour HD block (texture resolution, 02-design.md section 3.3): when set, draw_gpoly() samples this
 * page of TbPixels -- 256 texels a row, like vec_map -- instead of expanding vec_map's indices, and maps each
 * texel from the HD reference palette's colours to the active palette's (RendererHdColourTransform()). Alpha 0
 * texels are texture index 0's black. NULL: vec_map, as before. */
extern const TbPixel *vec_map_rgba;
/* With vec_map_rgba: its 32x32 level is the original indexed block (vec_map's), not a reduction of it -- the GPU
 * builds its mip levels from the finest level and needs to know (texture-resolution Phase 4). */
extern TbBool vec_map_original_at_32;
extern uint64_t vec_screen_width;
extern int64_t vec_window_width;
extern int64_t vec_window_height;
extern unsigned char *dither_map;
extern unsigned char *dither_end;
/* lbSpriteReMapPtr: a 256-entry TbPixel lookup, indexed by a sprite's own
 * source byte, used by the "remp" family of blit functions
 * (bflib_vidraw_spr_remp.c) for tint/shade sprite effects. Used to be a
 * pointer straight into a row of the old palette-index render_ghost/
 * render_fade_tables 2D tables (retired -- see
 * docs/refactor/renderer/02a-pixel-format-design.md's scope-correction
 * note); now points at lbSpriteRemapTable, filled on demand by
 * SetupSpriteRemapGhost()/SetupSpriteRemapShade() below. */
extern TbPixel *lbSpriteReMapPtr;
extern TbPixel lbSpriteRemapTable[256];
/* Tint weight for SetupSpriteRemapGhost()'s `strength` arg (0 = source
 * unchanged, 255 = pure tint colour). SPRITE_TINT_LEGACY is exactly the
 * historical render_ghost_blend() 1/3 weight (255/3); SPRITE_TINT_STRONG is
 * the heavier blend the freeze effect needs to stay visible now that the
 * 8-bit renderer's per-pixel palette-snap no longer amplifies weak tints. */
#define SPRITE_TINT_LEGACY  85
#define SPRITE_TINT_STRONG  128
/** What the last SetupSpriteRemap*() built lbSpriteRemapTable from, as a colour operation: HD sprites, which have
 *  colours and not palette indices, apply it as SpriteRemapCurves() (docs/refactor/sprites/02-design.md section 5).
 *  Valid while lbSpriteReMapPtr points at lbSpriteRemapTable (a caller with a table of its own leaves it stale). */
enum SpriteRemapKind {
    SprRemap_Shade = 1, ///< render_shade(c, shade)
    SprRemap_Ghost,     ///< (ref * strength + c * (255 - strength)) / 255, ref resolved through the palette at setup
    SprRemap_Flash,     ///< render_flash_blend(c, shift[0], shift[1], shift[2])
};
struct SpriteRemapOp {
    uint8_t kind;      ///< enum SpriteRemapKind (0: none set yet)
    uint8_t strength;  ///< Ghost
    int16_t shift[3];  ///< Flash, in the original's 6-bit steps
    int64_t shade;     ///< Shade
    TbPixel ref;       ///< Ghost
};
extern struct SpriteRemapOp lbSpriteRemapOp;
/** An op as three per-channel curves: out[c][v] is what channel c (0 red, 1 green, 2 blue) of value v becomes. Every
 *  op is per channel, so for a colour c, (out[0][c.r], out[1][c.g], out[2][c.b]) is exactly what the op's table gives
 *  a palette entry of colour c. An op of kind 0 gives the identity. */
void SpriteRemapCurves(const struct SpriteRemapOp *op, uint8_t out[3][256]);
/** An HD sprite frame (one level of it) for LbSpriteDrawHdUsingScalingData(): texels (RGBA in the HD reference
 *  palette's colours, alpha below 128 transparent) of `scale` x the original frame, `stride` texels a row; and the
 *  original frame it stands for: its run-length data (parity mode walks it) and size, orig_height cut short by the
 *  caller as the original's is (the water cutoff). */
struct TbHdSpriteSource {
    const TbPixel *texels;
    int64_t stride;
    int64_t scale;
    const unsigned char *rle;
    int64_t orig_width;
    int64_t orig_height;
    int64_t full_height;  ///< the original's whole height: the texels are scale x that many rows
    uint64_t id;          ///< names these texels for the GPU's cache: never reused for other texels
    TbBool parity;
    /** Art past the original's crop, whole original texels (left, top, right, bottom): the texels hold that much
     *  around the original's (stride included). Precision mode draws it through the scaling arrays extended by their
     *  own rule; parity mode ignores it. */
    int64_t spill[4];
    /** Additive art for an alpha-ramp draw (DrawAlphaSpriteUsingScalingData(), Phase 9): texels' RGB is what each
     *  draws adds to the destination (alpha below 128: nothing, or in parity mode black, as the original's index 0
     *  or above 64 writes), sub_texels' RGB (same layout; NULL: none) what it subtracts. No colour transform. */
    TbBool additive;
    const TbPixel *sub_texels;
};
/** Draws an HD sprite where LbSpriteDrawUsingScalingData(posx, posy, original) would draw its original, through the
 *  scaling data LbSpriteSetScalingData() set for that original, with the draw flags' flips, blends and remap
 *  (bflib_vidraw_spr_hd.c). Returns false, drawing nothing, when it can't and the caller should draw the original:
 *  a GPU world frame is being recorded, or the remap is a caller's own table (not lbSpriteRemapTable). The alpha
 *  ramps (DrawAlphaSpriteUsingScalingData()) draw additive art (src->additive), whatever the flags. */
TbBool LbSpriteDrawHdUsingScalingData(int64_t posx, int64_t posy, const struct TbHdSpriteSource *src);
/** Records an HD sprite into the GPU world frame (bflib_vidraw.c), as SwCaptureSprite() records an original. */
TbBool SwCaptureSpriteHd(int64_t posx, int64_t posy, const struct TbHdSpriteSource *src, uint32_t mode, TbBool remap);
/** Fills lbSpriteRemapTable[i] with ref_index tinted over source byte i at the given strength, and points lbSpriteReMapPtr at it. */
void SetupSpriteRemapGhost(uint8_t ref_index, uint8_t strength);
/** Fills lbSpriteRemapTable[i] = render_shade(i, shade) for every possible source byte i, and points lbSpriteReMapPtr at it. */
void SetupSpriteRemapShade(int64_t shade);
/** Fills lbSpriteRemapTable with the possession/full-flash remap (replaces the old white_pal[256]) and points lbSpriteReMapPtr at it. */
void SetupSpriteRemapWhiteFlash(void);
/** Fills lbSpriteRemapTable with the damage-flash remap (replaces the old red_pal[256]) and points lbSpriteReMapPtr at it. */
void SetupSpriteRemapRedFlash(void);
/** What LbSpriteSetScalingData() was last given (HD sprites extend its arrays over art past the original's crop). */
struct TbSpriteScaling {
    int64_t x, y, swidth, sheight, dwidth, dheight;
};
extern struct TbSpriteScaling lbSpriteScaling;
extern int64_t scale_up;
extern int64_t xsteps_array[2*SPRITE_SCALING_XSTEPS];
extern int64_t ysteps_array[2*SPRITE_SCALING_YSTEPS];

#pragma pack()

/******************************************************************************/
TbResult LbDrawBox(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour);
TbResult LbDrawBoxImmediate(int64_t x, int64_t y, uint64_t width, uint64_t height, TbPixel colour);
void LbDrawHVLine(int64_t xpos1, int64_t ypos1, int64_t xpos2, int64_t ypos2, TbPixel colour);

void LbDrawPixel(int64_t x, int64_t y, TbPixel colour);
void LbDrawCircle(int64_t x, int64_t y, int64_t radius, TbPixel colour);

void setup_vecs(TbPixel *screenbuf, unsigned char *nvec_map,
        uint64_t line_len, uint64_t width, uint64_t height);
/** gpu-v2 Phase C.2: records a scaled sprite draw into the GPU world frame instead of rasterizing it (see bflib_vidraw.c). `mode` is enum WorldFrameSpriteMode (renderer/WorldFrame.h). */
TbBool SwCaptureSprite(int64_t posx, int64_t posy, const unsigned char *rle, int64_t width, int64_t height,
                       uint32_t mode, const TbPixel *cmap, TbPixel colour);
/** gpu-v2 Phase C.5: records a solid window-relative rectangle into the GPU world frame (selection lines, bars) instead of drawing it. */
TbBool SwCaptureRect(int64_t x, int64_t y, int64_t w, int64_t h, TbPixel colour);
void setup_steps(int64_t posx, int64_t posy, const struct TbSourceBuffer * src_buf, int64_t **xstep, int64_t **ystep, int64_t *scanline);
void setup_outbuf(const int64_t *xstep, const int64_t *ystep, TbPixel **outbuf, int64_t *outheight);
TbResult LbSpriteDrawUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *);
TbResult LbSpriteDrawRemapUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *, const TbPixel *cmap);
TbResult LbSpriteDrawOneColourUsingScalingData(int64_t posx, int64_t posy, const struct TbSprite *sprite, TbPixel colour);
void LbSpriteSetScalingData(int64_t x, int64_t y, int64_t swidth, int64_t sheight, int64_t dwidth, int64_t dheight);
TbResult DrawAlphaSpriteUsingScalingData(int64_t posx, int64_t posy, const struct TbSourceBuffer *);
void LbSpriteSetScalingWidthSimpleArray(int64_t * xsteps_arr, int64_t x, int64_t swidth, int64_t dwidth);
void LbSpriteSetScalingWidthClippedArray(int64_t * xsteps_arr, int64_t x, int64_t swidth, int64_t dwidth, int64_t gwidth);
void LbSpriteSetScalingHeightSimpleArray(int64_t * ysteps_arr, int64_t y, int64_t sheight, int64_t dheight);
void LbSpriteSetScalingHeightClippedArray(int64_t * ysteps_arr, int64_t y, int64_t sheight, int64_t dheight, int64_t gheight);
// Had real external linkage but no header declaration at all -- added,
// the usual "add the missing declaration" fix.
void LbSpriteClearScalingWidthArray(int64_t * xsteps_arr, int64_t swidth);
void LbSpriteClearScalingHeightArray(int64_t * ysteps_arr, int64_t sheight);

TbResult LbSpriteDraw(int64_t x, int64_t y, const struct TbSprite *spr);

TbResult LbSpriteDrawScaled(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height);
TbResult LbSpriteDrawScaledOneColour(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel colour);
int64_t LbSpriteDrawScaledRemap(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel *cmap);
/* The draws themselves. The entry points above route through the renderer first;
 * these are what it calls when it is time to put pixels down. */
TbResult LbSpriteDrawImmediate(int64_t x, int64_t y, const struct TbSprite *spr);
TbResult LbSpriteDrawOneColourImmediate(int64_t x, int64_t y, const struct TbSprite *spr, const TbPixel colour);
TbResult LbSpriteDrawScaledImmediate(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height);
TbResult LbSpriteDrawScaledOneColourImmediate(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel colour);
int64_t LbSpriteDrawScaledRemapImmediate(int64_t xpos, int64_t ypos, const struct TbSprite *sprite, int64_t dest_width, int64_t dest_height, const TbPixel *cmap);
#define LbSpriteDrawResizedImmediate(xpos, ypos, un_per_px, sprite) LbSpriteDrawScaledImmediate(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16)
#define LbSpriteDrawResizedOneColourImmediate(xpos, ypos, un_per_px, sprite, colour) LbSpriteDrawScaledOneColourImmediate(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, colour)
#define LbSpriteDrawResizedRemapImmediate(xpos, ypos, un_per_px, sprite, cmap) LbSpriteDrawScaledRemapImmediate(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, cmap)
#define LbSpriteDrawResized(xpos, ypos, un_per_px, sprite) LbSpriteDrawScaled(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16)
#define LbSpriteDrawResizedOneColour(xpos, ypos, un_per_px, sprite, colour) LbSpriteDrawScaledOneColour(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, colour)
#define LbSpriteDrawResizedRemap(xpos, ypos, un_per_px, sprite, cmap) LbSpriteDrawScaledRemap(xpos, ypos, sprite, ((sprite)->SWidth * un_per_px + 8) / 16, ((sprite)->SHeight * un_per_px + 8) / 16, cmap)

TbResult LbHugeSpriteDraw(const struct TbHugeSprite * spr, int64_t sp_len,
    TbPixel *r, int64_t r_row_delta, int64_t r_height, int64_t xshift, int64_t yshift, int64_t units_per_px);
// Injected lookup for the panel sprite behind a TiledSprite index (impl. in
// custom_sprites.c), so this file doesn't need custom_sprites.h directly.
// See docs/refactor/stage-02-decouple-bflib.md.
typedef const struct TbSprite *(*PanelSpriteLookupFn)(int64_t sprite_idx);

void LbTiledSpriteDraw(int64_t x, int64_t y, int64_t units_per_px, struct TiledSprite *bigspr, PanelSpriteLookupFn panel_sprite_fn);
int64_t LbTiledSpriteHeight(struct TiledSprite *bigspr, PanelSpriteLookupFn panel_sprite_fn);

// mspointer needs this for some reason
TbResult LbSpriteDrawUsingScalingUpDataSolidLR(TbPixel *outbuf, int64_t scanline, int64_t outheight, int64_t *xstep, int64_t *ystep, const struct TbSourceBuffer * src_buf);

// Shared "solid run" copy helper used by the scaled-sprite blitters in
// bflib_vidraw_spr_norm.c/_onec.c/_remp.c, defined in bflib_vidraw.c. One
// declaration here instead of three identical local externs.
void LbPixelBlockCopyForward(TbPixel * dst, const TbPixel * src, int64_t len);

/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
