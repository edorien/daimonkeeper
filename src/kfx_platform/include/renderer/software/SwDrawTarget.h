/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file SwDrawTarget.h
 *     The surface and clip window the software raster draws into.
 ******************************************************************************/
#pragma once

#include "bflib_video.h"   /* TbPixel */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Base of the target surface. */
TbPixel* SwTargetWScreen(void);
/** Surface base advanced to the clip window origin. */
TbPixel* SwTargetGraphicsWindowPtr(void);
/** Surface pitch, in bytes per row. */
int64_t SwTargetScanline(void);
/** Full surface height, in rows. */
int64_t SwTargetScreenHeight(void);

/** The clip window the primitives draw within. */
int64_t SwTargetWindowX(void);
int64_t SwTargetWindowY(void);
int64_t SwTargetWindowWidth(void);
int64_t SwTargetWindowHeight(void);

/**
 * The 3D rasterizer's own render target, as last configured by setup_vecs()
 * (bflib_vidraw.c). Distinct from SwTargetWScreen()/SwTargetWindow*() above:
 * setup_vecs() can point this at a sub-region or a different scale than the
 * main framebuffer window (e.g. gui_parchment.c's downscaled overlay), so
 * engine_render.c/engine_textures.c/the software rasterizer read the vec_*
 * state through these accessors instead of externing the globals directly.
 */
TbPixel* SwTargetVecScreen(void);
/** Same base one scanline above SwTargetVecScreen() -- see setup_vecs()'s poly_screen assignment. */
TbPixel* SwTargetPolyScreen(void);
/**
 * Current texture-atlas source pointer, as last set by setup_vecs(). Stays
 * 8-bit palette-indexed (like TbSpriteData), NOT TbPixel -- this is source
 * texture data, not a framebuffer destination. See
 * docs/refactor/renderer/02a-pixel-format-design.md §2/§3.
 */
const unsigned char* SwTargetVecMap(void);
/**
 * SwTargetVecScreen()'s scanline pitch, in TbPixel units (not bytes -- every
 * consumer uses it directly in TbPixel* pointer arithmetic, which
 * auto-scales by sizeof(TbPixel); see setup_vecs()'s `line_len` callers, all
 * of which pass a pixel-width value). Returns `unsigned long` -- matching
 * bflib_vidraw.c's vec_screen_width exactly, not narrowed to uint32_t --
 * because callers multiply this against a signed row index (e.g.
 * bflib_render_gpoly.c's `vec_screen_width * state.y`), and the usual
 * arithmetic conversions make the *width* of this return type decide whether
 * that multiplication happens in 32-bit or 64-bit. Narrowing it silently
 * changes that arithmetic's width, not just this function's own precision.
 */
uint64_t SwTargetVecScreenWidth(void);
/** The rasterizer's clip width/height, as last set by setup_vecs(). `long`, matching vec_window_width/height exactly -- see SwTargetVecScreenWidth()'s comment. */
int64_t SwTargetVecWindowWidth(void);
int64_t SwTargetVecWindowHeight(void);

#ifdef __cplusplus
}
#endif
