/******************************************************************************/
// Dungeon Keeper - Renderer Abstraction Layer
/******************************************************************************/
/** @file IUIRenderer.cpp
 *     Software UI drawing.
 * @par Design:
 *     Each body applies the submitted draw state to the ambient state, calls the
 *     existing raster primitive, then restores it — the same sequence the engine
 *     performed around these primitives before they were routed, so a submission
 *     draws exactly what a direct call did.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "renderer/IUIRenderer.h"
#include "renderer/RendererManager.h"
#include "bflib_vidraw.h"   // the raster primitives
#include "bflib_sprite.h"   // TbSprite
#include "bflib_video.h"    // Lb_SPRITE_* draw flags
#include "ports/display_host_port.h"
#include "post_inc.h"

/******************************************************************************/

namespace {

/** Applies a submitted draw state for the duration of one draw. */
class ScopedDrawState {
public:
    explicit ScopedDrawState(TbDrawFlagsMask flags)
        : m_flags(RendererGetDrawFlags())
    {
        RendererSetDrawFlags((int64_t)flags);
    }
    ~ScopedDrawState() { RendererSetDrawFlags(m_flags); }
private:
    int64_t m_flags;
};

} // namespace

/******************************************************************************/

TbResult IUIRenderer::SubmitRawSprite(int64_t x, int64_t y, const struct TbSprite* spr,
                                      KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawImmediate(x, y, spr);
}

TbResult IUIRenderer::SubmitRawSpriteOneColour(int64_t x, int64_t y, const struct TbSprite* spr,
                                               TbPixel colour, KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawOneColourImmediate(x, y, spr, colour);
}

TbResult IUIRenderer::SubmitRawSpriteScaled(int64_t x, int64_t y, const struct TbSprite* spr,
                                            int64_t w, int64_t h, KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawScaledImmediate(x, y, spr, w, h);
}

TbResult IUIRenderer::SubmitRawSpriteScaledOneColour(int64_t x, int64_t y, const struct TbSprite* spr,
                                                     int64_t w, int64_t h, TbPixel colour,
                                                     KfxDrawState state)
{
    if (!spr) return Lb_FAIL;
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawScaledOneColourImmediate(x, y, spr, w, h, colour);
}

int64_t IUIRenderer::SubmitRawSpriteScaledRemap(int64_t x, int64_t y, const struct TbSprite* spr,
                                            int64_t w, int64_t h, const TbPixel* cmap,
                                            KfxDrawState state)
{
    if (!spr || !cmap) return Lb_FAIL;
    ScopedDrawState guard(state.flags);
    return LbSpriteDrawScaledRemapImmediate(x, y, spr, w, h, cmap);
}

void IUIRenderer::SubmitSolidBox(int64_t x, int64_t y, int64_t w, int64_t h,
                                 TbPixel colour_idx, KfxDrawState state)
{
    if (w <= 0 || h <= 0) return;
    // LbDrawBox reads the outline flag itself, so the whole state just goes ambient.
    ScopedDrawState guard(state.flags);
    LbDrawBoxImmediate(x, y, (uint64_t)w, (uint64_t)h, colour_idx);
}

void IUIRenderer::SubmitSlabBackground(int64_t x, int64_t y, int64_t w, int64_t h)
{
    display_draw_slab_background_immediate(x, y, w, h);
}

/******************************************************************************/
