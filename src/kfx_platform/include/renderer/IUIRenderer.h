/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.h
 *     The UI drawing a backend has to provide.
 * @par Design:
 *     The engine's Lb* entry points route here, so this is where a backend
 *     decides how a draw is realised. The defaults are the software path, which
 *     calls the existing raster; a GPU backend overrides what it accelerates.
 */
/******************************************************************************/
#pragma once

#include "renderer/DrawState.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include <cstdint>

struct TbSprite;

/******************************************************************************/

class IUIRenderer {
public:
    virtual ~IUIRenderer() = default;

    virtual TbResult SubmitRawSprite(int64_t x, int64_t y, const struct TbSprite* spr,
                                     KfxDrawState state);
    virtual TbResult SubmitRawSpriteOneColour(int64_t x, int64_t y, const struct TbSprite* spr,
                                              TbPixel colour, KfxDrawState state);

    // Drawn at an explicit size rather than the sprite's own.
    virtual TbResult SubmitRawSpriteScaled(int64_t x, int64_t y, const struct TbSprite* spr,
                                           int64_t w, int64_t h, KfxDrawState state);
    virtual TbResult SubmitRawSpriteScaledOneColour(int64_t x, int64_t y, const struct TbSprite* spr,
                                                    int64_t w, int64_t h, TbPixel colour,
                                                    KfxDrawState state);
    virtual int64_t      SubmitRawSpriteScaledRemap(int64_t x, int64_t y, const struct TbSprite* spr,
                                                int64_t w, int64_t h, const TbPixel* cmap,
                                                KfxDrawState state);

    virtual void SubmitSolidBox(int64_t x, int64_t y, int64_t w, int64_t h,
                                TbPixel colour_idx, KfxDrawState state);

    /** Tile the GUI slab texture over a rect. */
    virtual void SubmitSlabBackground(int64_t x, int64_t y, int64_t w, int64_t h);

    virtual const char* GetName() const { return "UI"; }
};

/******************************************************************************/
