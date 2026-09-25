/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file bflib_video.h
 *     Header file for bflib_video.c.
 * @par Purpose:
 *     Video support library for 8-bit graphics.
 * @par Comment:
 *     Just a header file - #defines, typedefs, function prototypes etc.
 * @author   Tomasz Lis
 * @date     11 Feb 2008 - 26 Jun 2010
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#ifndef BFLIB_VIDEO_H
#define BFLIB_VIDEO_H

#include "bflib_basics.h"

#include "globals.h"

#include <stdint.h>
#include <SDL3/SDL.h>

/** Window-mode flags: the currency passed across the window-system seam. */
enum KfxWindowFlags {
    KFX_WF_FULLSCREEN_EXCLUSIVE = 0x1, // fullscreen at a specific video mode
    KFX_WF_FULLSCREEN_DESKTOP   = 0x2, // borderless fullscreen at native resolution
    KFX_WF_BORDERLESS           = 0x4, // borderless window (also FILL ALL)
    KFX_WF_HIDDEN               = 0x8, // created hidden
};

#ifdef __cplusplus
extern "C" {
#endif
/******************************************************************************/

#define PALETTE_COLORS 256
#define PALETTE_SIZE (3*PALETTE_COLORS)

#define LOWRES_SCREEN_SIZE          320

#define MAX_SUPPORTED_SCREEN_WIDTH  3840
#define MAX_SUPPORTED_SCREEN_HEIGHT 2160

/******************************************************************************/
#pragma pack(1)

/**
 * Pixel definition - represents value of one point on the graphics screen.
 * True-colour RGBA, byte order {r,g,b,a} -- matches SDL_PIXELFORMAT_RGBA32
 * exactly (RendererSoftware's present-time texture format) on both little-
 * and big-endian hosts, so the CPU framebuffer can be handed to SDL with no
 * reinterpretation. See docs/refactor/renderer/02a-pixel-format-design.md
 * for the full migration design (was `unsigned char`, a palette index).
 */
typedef struct TbPixel {
    uint8_t r, g, b, a;
} TbPixel;

static inline TbPixel TbPixel_RGB(uint8_t r, uint8_t g, uint8_t b)
{
    TbPixel p = { r, g, b, 255 };
    return p;
}

static inline TbPixel TbPixel_RGBA(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    TbPixel p = { r, g, b, a };
    return p;
}

#define TbPixel_Transparent ((TbPixel){0, 0, 0, 0})

static inline TbBool TbPixel_IsTransparent(TbPixel p)
{
    return p.a == 0;
}

static inline TbBool TbPixel_Equal(TbPixel a, TbPixel b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

/**
 * Pack/unpack a TbPixel to/from a plain 32-bit integer, byte order {r,g,b,a}
 * matching the struct's own layout exactly (so this is a bit-reinterpret,
 * not a format conversion). For call sites that smuggle a colour through an
 * existing `long`-typed field never intended to hold a struct -- e.g.
 * engine_render.c's line-drawing bucket items reuse `struct PolyPoint::S`
 * (otherwise a shade value) to carry a flat line colour through the same
 * queue regular shaded polygons use. Prefer passing TbPixel directly
 * wherever the call site's own type isn't already fixed by something else
 * (e.g. serialized/queued data); this exists only for the sites that can't.
 */
static inline uint64_t TbPixel_Pack(TbPixel p)
{
    return ((uint64_t)p.r << 24) | ((uint64_t)p.g << 16) | ((uint64_t)p.b << 8) | (uint64_t)p.a;
}

static inline TbPixel TbPixel_Unpack(uint64_t v)
{
    return TbPixel_RGBA((uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v);
}

/**
 * A stride reported in bytes, as every SDL ->pitch field does. Distinct from
 * a plain int/long pixel count so "used a byte pitch where a pixel-stride
 * TbPixel* advance was expected" is a compile error rather than a silent 4x
 * under-advance -- exactly the bug found three times independently in Stage
 * 2 (RendererLockFramebuffer(), bflib_video.c's mode setup, and
 * bflib_mspointer.cpp's cursor backup surface -- see
 * docs/refactor/renderer/02c-post-migration-audit-and-refactor-opportunities.md
 * §2.3.1). Convert to a TbPixel* pixel count via TbBytePitch_ToPixels()
 * before using it as pointer arithmetic on a TbPixel buffer; use .bytes
 * directly for byte-counted APIs (memset(), SDL's own pitch parameters).
 */
typedef struct TbBytePitch {
    int64_t bytes;
} TbBytePitch;

static inline int64_t TbBytePitch_ToPixels(TbBytePitch bp)
{
    return bp.bytes / (int64_t)sizeof(TbPixel);
}

/** VGA 6-bit (0-63) palette channel to 8-bit (0-255). Canonical conversion --
 * every 6-bit-scale blend formula in the pixel-format design doc (§2) uses
 * this, not an ad-hoc scale factor, so results match exactly. */
static inline uint8_t chan6_to_8(uint8_t v)
{
    return (uint8_t)((v * 255) / 63);
}

/**
 * Expand one palette-indexed sprite byte to a true-colour pixel, sampling
 * the given palette (768-byte VGA-6 RGB triples, PALETTE_SIZE bytes -- e.g.
 * engine_palette, or the palette a specific draw call is using). Preserves
 * the "index 0 = transparent" convention every sprite blit primitive relied
 * on before this migration. See docs/refactor/renderer/
 * 02a-pixel-format-design.md §3.1.
 */
static inline TbPixel expand_indexed_pixel(uint8_t index, const unsigned char *pal)
{
    if (index == 0)
        return TbPixel_Transparent;
    return TbPixel_RGB(
        chan6_to_8(pal[3 * index + 0]),
        chan6_to_8(pal[3 * index + 1]),
        chan6_to_8(pal[3 * index + 2]));
}

/** Inverse of chan6_to_8: an 8-bit (0-255) colour channel down to VGA 6-bit
 * (0-63) palette scale. Needed when a genuine true-colour pixel must be
 * matched back to a palette index, e.g. via LbPaletteFindColour(). */
static inline uint8_t chan8_to_6(uint8_t v)
{
    return (uint8_t)((v * 63) / 255);
}

/**
 * Plain palette-index to true-colour lookup, with no transparency
 * special-case (unlike expand_indexed_pixel(), which reserves index 0 to
 * mean "sprite texel not drawn"). Use this for flat colour-table lookups
 * where index 0 is a normal opaque colour like any other -- e.g. the
 * minimap panel colour table -- and expand_indexed_pixel() for sprite/font
 * texel expansion.
 */
static inline TbPixel resolve_indexed_pixel(uint8_t index, const unsigned char *pal)
{
    return TbPixel_RGB(
        chan6_to_8(pal[3 * index + 0]),
        chan6_to_8(pal[3 * index + 1]),
        chan6_to_8(pal[3 * index + 2]));
}

/** Standard video modes, registered by LbScreenInitialize().
 * These are standard VESA modes, indexed this way in all Bullfrog games.
 */
enum ScreenMode {
    Lb_SCREEN_MODE_INVALID      = 0x00,
    Lb_SCREEN_MODE_320_200_8    = 0x01,
    Lb_SCREEN_MODE_320_200_16   = 0x02,
    Lb_SCREEN_MODE_320_200_24   = 0x03,
    Lb_SCREEN_MODE_512_384_16   = 0x08,
    Lb_SCREEN_MODE_512_384_24   = 0x09,
    Lb_SCREEN_MODE_640_400_8    = 0x0A,
    Lb_SCREEN_MODE_640_400_16   = 0x0B,
    Lb_SCREEN_MODE_320_240_8    = 0x04,
    Lb_SCREEN_MODE_320_240_16   = 0x05,
    Lb_SCREEN_MODE_320_240_24   = 0x06,
    Lb_SCREEN_MODE_512_384_8    = 0x07,
    Lb_SCREEN_MODE_640_400_24   = 0x0C,
    Lb_SCREEN_MODE_640_480_8    = 0x0D,
    Lb_SCREEN_MODE_640_480_16   = 0x0E,
    Lb_SCREEN_MODE_640_480_24   = 0x0F,
    Lb_SCREEN_MODE_800_600_8    = 0x10,
    Lb_SCREEN_MODE_800_600_16   = 0x11,
    Lb_SCREEN_MODE_800_600_24   = 0x12,
    Lb_SCREEN_MODE_1024_768_8   = 0x13,
    Lb_SCREEN_MODE_1024_768_16  = 0x14,
    Lb_SCREEN_MODE_1024_768_24  = 0x15,
    Lb_SCREEN_MODE_1200_1024_8  = 0x16,
    Lb_SCREEN_MODE_1200_1024_16 = 0x17,
    Lb_SCREEN_MODE_1200_1024_24 = 0x18,
    Lb_SCREEN_MODE_1600_1200_8  = 0x19,
    Lb_SCREEN_MODE_1600_1200_16 = 0x1A,
    Lb_SCREEN_MODE_1600_1200_24 = 0x1B,
};

typedef int64_t TbScreenMode;
typedef int64_t TbScreenCoord;

enum TbPaletteFadeFlag {
    Lb_PALETTE_FADE_OPEN   = 0,
    Lb_PALETTE_FADE_CLOSED = 1,
};

enum TbDrawFlags {
    Lb_SPRITE_FLIP_HORIZ   = 0x0001,
    Lb_SPRITE_FLIP_VERTIC  = 0x0002,
    Lb_SPRITE_TRANSPAR4    = 0x0004,
    Lb_SPRITE_TRANSPAR8    = 0x0008,
    Lb_SPRITE_OUTLINE      = 0x0010,
    Lb_TEXT_HALIGN_LEFT    = 0x0020,
    Lb_TEXT_ONE_COLOR      = 0x0040,
    Lb_TEXT_HALIGN_RIGHT   = 0x0080,
    Lb_TEXT_HALIGN_CENTER  = 0x0100,
    Lb_TEXT_HALIGN_JUSTIFY = 0x0200,
    Lb_TEXT_UNDERLINE      = 0x0400,
    Lb_SPRITE_REMAP        = 0x0800,
    Lb_TEXT_UNDERLNSHADOW  = 0x1000,
    Lb_TEXT_REMAP          = 0x2000,
};

enum TbVideoModeFlags {
    Lb_VF_DEFAULT     = 0x0000, // dummy flag
    Lb_VF_RGBCOLOR    = 0x0001,
    Lb_VF_TRUCOLOR    = 0x0002,
    Lb_VF_PALETTE     = 0x0004,
    Lb_VF_WINDOWED    = 0x0010,
    Lb_VF_BORDERLESS  = 0x0020,
    Lb_VF_DESKTOP     = 0x0040,
    Lb_VF_FILLALL     = 0x0080,
};

struct GraphicsWindow {
    int64_t x;
    int64_t y;
    int64_t width;
    int64_t height;
    TbPixel *ptr;
};
typedef struct GraphicsWindow TbGraphicsWindow;

struct ScreenModeInfo {
    /** Hardware driver screen width. */
    TbScreenCoord Width;
    /** Hardware driver screen height. */
    TbScreenCoord Height;
    /** Hardware driver color depth. */
    int64_t BitsPerPixel;
    /** Is the mode currently available for use. */
    int64_t Available;
    /** Video mode flags. */
    uint64_t VideoFlags;
     /** Window position X. */
    int64_t window_pos_x;
     /** Window position Y. */
    int64_t window_pos_y;
    /** Window-mode flags (KfxWindowFlags). */
    Uint32 windowFlags;
    /** Text description of the mode. */
    char Desc[23];
};
typedef struct ScreenModeInfo TbScreenModeInfo;

struct DisplayStruct {
        /** Pointer to physical screen buffer, if locked. */
        TbPixel *PhysicalScreen;
        /** Pointer to graphics screen buffer, if locked. */
        TbPixel *WScreen;
        /** Pointer to glass map, used for 8-bit video transparency. */
        uchar *GlassMap;
        /** Pointer to fade table, used for 8-bit video fading. */
        uchar *FadeTable;
        /** Pointer to graphics window buffer, if locked. */
        TbPixel *GraphicsWindowPtr;
        /** Sprite used as mouse cursor. */
        const struct TbSprite *MouseSprite;
        /** Resolution in width of the current video mode.
         *  Note that it's not always "physical" size.
         *  It is the part of screen buffer which is being drawn
         *  on physical screen (WScreen X drawing size). */
        int64_t PhysicalScreenWidth;
        /** Resolution in height of the current video mode.
         *  Note that it's not always "physical" size.
         *  It is the part of screen buffer which is being drawn
         *  on physical screen (WScreen Y drawing size). */
        int64_t PhysicalScreenHeight;
        /** Width of the screen buffer (WScreen X pitch).
         *  Note that only part of this width may be drawn on real screen. */
        int64_t GraphicsScreenWidth;
        /** Height of the screen buffer (WScreen Y pitch).
        *  Note that only part of this height may be drawn on real screen. */
        int64_t GraphicsScreenHeight;
        /** Current graphics window beginning X coordinate. */
        int64_t GraphicsWindowX;
        /** Current graphics window beginning Y coordinate. */
        int64_t GraphicsWindowY;
        /** Current graphics window width (size in X axis). */
        int64_t GraphicsWindowWidth;
        /** Current graphics window height (size in Y axis). */
        int64_t GraphicsWindowHeight;
        /** Current mouse clipping window start X coordinate. */
        int64_t MouseWindowX;
        /** Current mouse clipping window start Y coordinate. */
        int64_t MouseWindowY;
        /** Current mouse clipping window width (in pixels). */
        int64_t MouseWindowWidth;
        /** Current mouse clipping window height (in pixels). */
        int64_t MouseWindowHeight;
        /** Mouse position during button "down" event, X coordinate. */
        int64_t MouseX;
        /** Mouse position during button "down" event, Y coordinate. */
        int64_t MouseY;
        /** Mouse position during move, X coordinate. */
        int64_t MMouseX;
        /** Mouse position during move, Y coordinate. */
        int64_t MMouseY;
        /** Mouse position during button release, X coordinate. */
        int64_t RMouseX;
        /** Mouse position during button release, Y coordinate. */
        int64_t RMouseY;
        int64_t MouseMoveRatio; // was ushort OldVideoMode; but wasn't needed
        uint64_t ScreenMode;
        /** VESA set-up flag, used only with VBE video modes. */
        uchar VesaIsSetUp;
        uchar LeftButton;
        uchar RightButton;
        uchar MiddleButton;
        uchar MLeftButton;
        uchar MRightButton;
        uchar MMiddleButton;
        uchar RLeftButton;
        uchar RMiddleButton;
        uchar RRightButton;
        uchar FadeStep;
        /** Currently active colour palette.
         *  LbPaletteGet() should be used to retrieve a copy of the palette. */
        uchar *Palette;
};
typedef struct DisplayStruct TbDisplayStruct;

/** Extensions to DisplayStruct - will be later integrated into it. */
struct DisplayStructEx {
    int64_t WhellPosition;
    uint64_t WhellMoveUp;
    uint64_t WhellMoveDown;
    /** Colour index used for drawing shadow. */
    uchar ShadowColour;
};
typedef struct DisplayStructEx TbDisplayStructEx;

struct SSurface;
typedef struct SSurface TSurface;

/******************************************************************************/


#pragma pack()
/******************************************************************************/
extern volatile TbBool lbScreenInitialised;
extern volatile TbBool lbUseSdk;
extern volatile TbBool lbInteruptMouse;
extern volatile TbDisplayStructEx lbDisplayEx;

#define DEFAULT_UI_SCALE                       128 // is equivilent to size 1 or 100%
#define DEFAULT_ASPECT_RATIO_FACTOR            160 // is equivilent to 16/10 * 100
#define DEFAULT_FIRST_PERSON_HORIZONTAL_FOV     94 // 94 degrees at 16/10 aspect ratio
#define DEFAULT_FIRST_PERSON_VERTICAL_FOV       68 // 68 degrees at 16/10 aspect ratio

enum UIScaleSettings {
    UI_NORMAL_SIZE = DEFAULT_UI_SCALE,
    UI_HALF_SIZE   = DEFAULT_UI_SCALE / 2,
    UI_DOUBLE_SIZE = DEFAULT_UI_SCALE * 2,
};

extern int64_t units_per_pixel_width;
extern int64_t units_per_pixel_height;
extern int64_t units_per_pixel_menu_height;
extern int64_t units_per_pixel_best;
extern int64_t units_per_pixel_menu;
extern int64_t units_per_pixel_landview;
extern int64_t units_per_pixel_landview_frame;
extern int64_t units_per_pixel_ui;
extern uint64_t aspect_ratio_factor_HOR_PLUS;
extern uint64_t aspect_ratio_factor_HOR_PLUS_AND_VERT_PLUS;
// first_person_horizontal_fov is declared in kfx_render's vidmode.h, not
// here: it's read only by kfx_render's own engine_camera.c, never by any
// kfx_platform code, so it doesn't belong on kfx_platform's public
// surface. See docs/refactor/todo/check-layering-symbol-level-blind-spot.md.
extern uint64_t first_person_vertical_fov;
extern uint64_t landview_frame_movement_scale_x;
extern uint64_t landview_frame_movement_scale_y;

// units_per_pixel_width/height/ui/best/menu above are written by
// kfx_render's vidmode.c (update_screen_mode_data(), which needs its own
// render-config/RendererManager context to compute them) -- this file's
// scaling math (scale_value_by_horizontal_resolution and friends) reads
// them back through this callback instead of the bare extern, since that
// bare read is a genuine kfx_render-state dependency, not just a
// misplaced definition. Registered from main.cpp with vidmode.c's own
// get_video_scale_values(), which already owns the real values. See
// docs/refactor/todo/check-layering-symbol-level-blind-spot.md.
struct VideoScaleValues {
    int64_t units_per_pixel_width;
    int64_t units_per_pixel_height;
    int64_t units_per_pixel_ui;
    int64_t units_per_pixel_best;
    int64_t units_per_pixel_menu;
};
struct VideoScaleCallbacks {
    const struct VideoScaleValues *(*get_video_scale_values)(void);
};
void set_video_scale_callbacks(const struct VideoScaleCallbacks *callbacks);
extern const struct VideoScaleCallbacks *video_scale_callbacks;

extern int64_t MyScreenWidth;
extern int64_t MyScreenHeight;
extern int64_t pixel_size;
extern int64_t pixels_per_block;
extern int64_t units_per_pixel;

extern int64_t display_id;

extern TbBool vsync_enabled;

/** Set by -headless (main.cpp) before PlatformManager_InitVideo() runs;
  * see bflib_video.c for what it does. */
extern TbBool VideoDisabled;

extern TbDisplayStruct lbDisplay;
extern SDL_Window *lbWindow;

// Moved from kfx_apploop's game_session_loop.h (stage 13.3, docs/refactor/
// stage-13-enforce-and-document.md) -- redetect_screen_refresh_rate_for_draw()
// only ever touched lbWindow and these three globals, a misclassified
// function with no real game-loop coupling; kfx_game's writers
// (main_game.c/console_cmd.c) reach these via a legal downward reference.
extern int64_t fps_limit_current;
extern int64_t fps_limit_main; // -1 if auto
extern int64_t fps_limit_secondary;
void redetect_screen_refresh_rate_for_draw(void);
// Populates the standard+modern video mode table (idempotent -- a no-op once
// lbScreenModeInfoNum is non-zero) without touching SDL/the platform layer,
// unlike LbScreenInitialize() which also calls PlatformManager_InitVideo().
// Exists so config parsing (load_configuration(), which runs before
// LbScreenInitialize() in setup_game()'s startup order) can call
// LbRegisterVideoModeString() -- e.g. for INGAME_RES -- against an
// already-populated table. The table's first entry is always a reserved
// "INVALID" placeholder at index 0 (Lb_SCREEN_MODE_INVALID); without this
// call having run first, a config-parsed custom resolution would become the
// table's actual first entry and land on that same index 0, indistinguishable
// from failure to every "mode > 0" caller.
void LbRegisterDefaultVideoModesIfNeeded(void);
/******************************************************************************/
TbResult LbScreenInitialize(void);
TbResult LbScreenSetDoubleBuffering(TbBool state);
TbResult LbScreenSetup(TbScreenMode mode, TbScreenCoord width, TbScreenCoord height,
    unsigned char *palette, int64_t buffers_count, TbBool wscreen_vid);
TbResult LbScreenReset(TbBool exiting_application);

TbBool LbScreenIsModeAvailable(TbScreenMode mode, int64_t display);
TbScreenMode LbRecogniseVideoModeString(const char *desc);
TbScreenMode LbRegisterVideoMode(const char *desc, TbScreenCoord width, TbScreenCoord height,
    int64_t bpp, uint64_t flags);
TbScreenMode LbRegisterVideoModeString(const char *desc);
TbScreenModeInfo *LbScreenGetModeInfo(TbScreenMode mode);

TbScreenMode LbScreenActiveMode(void);
TbScreenCoord LbScreenWidth(void);
TbScreenCoord LbScreenHeight(void);
int64_t LbGraphicsScreenBPP(void);
TbScreenCoord LbGraphicsScreenWidth(void);
TbScreenCoord LbGraphicsScreenHeight(void);

TbBool LbScreenIsLocked(void);

TbResult LbScreenWaitVbi(void);
int64_t LbGetCurrentDisplayIndex();

int64_t LbPaletteFade(unsigned char *pal, int64_t n, enum TbPaletteFadeFlag flg);
TbResult LbPaletteStopOpenFade(void);
TbResult LbPaletteStore(const unsigned char *palette);
TbResult LbPaletteGet(unsigned char *palette);
const unsigned char *LbPaletteGetReadonly(void);
/** Returns a palette INDEX (not a resolved TbPixel) -- see the definition's comment. */
unsigned char LbPaletteFindColour(const unsigned char *pal, unsigned char r, unsigned char g, unsigned char b);
TbResult LbPaletteDataFillBlack(unsigned char *palette);
TbResult LbPaletteDataFillWhite(unsigned char *palette);

TbResult LbScreenStoreGraphicsWindow(TbGraphicsWindow *grwnd);
TbResult LbScreenLoadGraphicsWindow(TbGraphicsWindow *grwnd);
TbResult LbScreenSetGraphicsWindow(TbScreenCoord x, TbScreenCoord y,
    TbScreenCoord width, TbScreenCoord height);

TbResult LbSetTitle(const char *title);
TbResult LbSetIcon(int64_t nicon);

int64_t scale_value_for_resolution(int64_t base_value);
int64_t scale_value_for_resolution_with_upp(int64_t base_value, int64_t units_per_px);
int64_t scale_value_by_horizontal_resolution(int64_t base_value);
int64_t scale_value_by_vertical_resolution(int64_t base_value);
int64_t scale_ui_value_lofi(int64_t base_value);
int64_t scale_ui_value(int64_t base_value);
int64_t scale_fixed_DK_value(int64_t base_value);
int64_t scale_value_menu(int64_t base_value);
int64_t scale_value_landview(int64_t base_value);
void calculate_landview_upp(int64_t width, int64_t height, int64_t landview_width, int64_t landview_height);
TbBool is_ar_wider_than_original(int64_t width, int64_t height);
TbBool is_menu_ar_wider_than_original(int64_t width, int64_t height);
int64_t calculate_relative_upp(int64_t base_length, int64_t reference_upp, int64_t reference_length);
int64_t resize_ui(int64_t units_per_px, int64_t ui_scale);
void calculate_aspect_ratio_factor(int64_t width, int64_t height);
int64_t scale_fixed_DK_value_by_ar(int64_t base_value, TbBool scale_up, TbBool vert_plus);
int64_t FOV_based_on_aspect_ratio(void);
/******************************************************************************/
#ifdef __cplusplus
}
#endif
#endif
