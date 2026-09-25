/******************************************************************************/
// Free implementation of Bullfrog's Dungeon Keeper strategy game.
/******************************************************************************/
/** @file OverlayEffect.cpp
 *     Overlay lens effect implementation.
 * @par Purpose:
 *     Self-contained overlay compositing effect with alpha blending.
 * @par Comment:
 *     Loads and draws overlay sprites onto the viewport.
 * @author   Peter Lockett, KeeperFX Team
 * @date     09 Feb 2026
 * @par  Copying and copyrights:
 *     This program is free software; you can redistribute it and/or modify
 *     it under the terms of the GNU General Public License as published by
 *     the Free Software Foundation; either version 2 of the License, or
 *     (at your option) any later version.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "OverlayEffect.h"

#include <string.h>
#include <memory>
#include <new>
#include "bflib_basics.h"
#include "bflib_fileio.h"
#include "bflib_dernc.h"
#include "globals.h"
#include "config_lenses.h"
#include "custom_sprites.h"
#include "renderer/RendererManager.h"

#include "post_inc.h"

/******************************************************************************/
// INTERNAL OVERLAY RENDERER CLASS
/*****************************************************************************/

/**
 * COverlayRenderer - Internal overlay compositing system.
 * Manages overlay image loading and alpha blending.
 */
class COverlayRenderer {
public:
    COverlayRenderer(OverlayEffect* parent);
    ~COverlayRenderer();
    
    TbBool LoadOverlay(int64_t lens_idx);
    void Render(TbPixel *dstbuf, int64_t dstpitch, TbPixel *srcbuf, int64_t srcpitch,
                int64_t width, int64_t height);
    
private:
    OverlayEffect* m_parent;         // Parent effect for asset loading
    // m_owned_data is non-null only when we allocated the overlay ourselves
    // (the file-fallback path below) and RAII-frees it; m_overlay_data is
    // the pointer Render() actually reads through, set to either
    // m_owned_data.get() or a borrowed (never freed) registry pointer. Two
    // fields instead of one pointer + an "am I responsible for freeing it"
    // bool makes a future call site that forgets to check the bool before
    // freeing structurally impossible, not just unlikely.
    std::unique_ptr<unsigned char[]> m_owned_data;
    const unsigned char* m_overlay_data;
    int64_t m_width;                     // Overlay width
    int64_t m_height;                    // Overlay height
    int64_t m_alpha;                   // Alpha blending level (0-256)
    TbBool m_loaded;                 // Whether overlay is loaded
};

COverlayRenderer::COverlayRenderer(OverlayEffect* parent)
    : m_parent(parent)
    , m_overlay_data(NULL)
    , m_width(0)
    , m_height(0)
    , m_alpha(128)
    , m_loaded(false)
{
}

COverlayRenderer::~COverlayRenderer() = default;

TbBool COverlayRenderer::LoadOverlay(int64_t lens_idx)
{
    // Get lens configuration
    struct LensConfig* cfg = get_lens_config(lens_idx);
    if (cfg == NULL)
    {
        WARNLOG("Failed to get lens config for index %" PRId64, (int64_t)(lens_idx));
        return false;
    }
    
    // Check if this lens has an overlay effect
    if ((cfg->flags & LCF_HasOverlay) == 0)
    {
        SYNCDBG(8, "Lens %" PRId64 " does not have overlay effect", (int64_t)(lens_idx));
        return false;
    }
    
    // Try to get overlay data from asset registry (ZIP files with lenses.json)
    const struct LensOverlayData* overlay = get_lens_overlay_data(cfg->overlay_file);
    
    if (overlay != NULL && overlay->data != NULL &&
        overlay->width > 0 && overlay->height > 0)
    {
        // Use registry data (we don't own it, no need to free)
        m_owned_data.reset();
        m_overlay_data = overlay->data;
        m_width = overlay->width;
        m_height = overlay->height;

        SYNCDBG(7, "Loaded overlay '%s' (%" PRId64 "x%" PRId64 ") from asset registry",
                cfg->overlay_file, (int64_t)(m_width), (int64_t)(m_height));
    }
    else
    {
        // Registry lookup failed - try loading raw files from mods/data directories
        // This allows simple file-based mods without requiring ZIP/JSON
        // For overlays without registry, assume 256x256 (standard size)
        const int64_t default_size = 256;
        m_owned_data.reset(new (std::nothrow) unsigned char[default_size * default_size]);

        if (!m_owned_data)
        {
            ERRORLOG("Failed to allocate memory for overlay (lens %" PRId64 ")", (int64_t)(lens_idx));
            return false;
        }

        const char* loaded_from = NULL;
        if (!m_parent->LoadAssetWithFallback(cfg->overlay_file, m_owned_data.get(),
                                             default_size * default_size, &loaded_from))
        {
            WARNLOG("Failed to load overlay '%s' from registry or files for lens %" PRId64,
                    cfg->overlay_file, (int64_t)(lens_idx));
            m_owned_data.reset();
            return false;
        }

        m_overlay_data = m_owned_data.get();
        m_width = default_size;
        m_height = default_size;
        
        if (loaded_from != NULL) {
            SYNCDBG(7, "Loaded overlay '%s' (%" PRId64 "x%" PRId64 ") from mod '%s'", 
                    cfg->overlay_file, (int64_t)(m_width), (int64_t)(m_height), loaded_from);
        } else {
            SYNCDBG(7, "Loaded overlay '%s' (%" PRId64 "x%" PRId64 ") from base game files", 
                    cfg->overlay_file, (int64_t)(m_width), (int64_t)(m_height));
        }
    }
    
    // Store alpha value from config
    m_alpha = cfg->overlay_alpha;
    m_loaded = true;
    
    return true;
}

void COverlayRenderer::Render(TbPixel *dstbuf, int64_t dstpitch, TbPixel *srcbuf, int64_t srcpitch,
                              int64_t width, int64_t height)
{
    if (!m_loaded || m_overlay_data == NULL)
    {
        return;
    }

    const unsigned char* overlay_src = m_overlay_data;
    const int64_t alpha = m_alpha;
    const unsigned char *pal = RendererGetActivePalette();

    // Overlay dimensions
    const int64_t overlay_w = m_width;
    const int64_t overlay_h = m_height;

    // Stretch-to-fit: use fixed-point scaling (16.16 format) for precision
    // Scale factors map viewport coordinates to overlay texture coordinates
    const uint64_t scale_x = (overlay_w << 16) / width;
    const uint64_t scale_y = (overlay_h << 16) / height;

    // Clamp alpha to valid range (0-256, where 256 = opaque)
    int64_t alpha_clamped = (alpha < 0) ? 0 : ((alpha > 256) ? 256 : alpha);
    int64_t inv_alpha = 256 - alpha_clamped;

    // Composite overlay onto destination buffer with stretch-to-fit
    for (int64_t y = 0; y < height; y++)
    {
        // Calculate overlay Y coordinate using fixed-point
        int64_t overlay_y = (y * scale_y) >> 16;
        if (overlay_y >= overlay_h) overlay_y = overlay_h - 1;

        const unsigned char* overlay_row = overlay_src + (overlay_y * overlay_w);
        TbPixel* dst_row = dstbuf + (y * dstpitch);
        const TbPixel* src_row = srcbuf + (y * srcpitch);

        for (int64_t x = 0; x < width; x++)
        {
            // Get source pixel (the 3D view) -- already a real colour
            TbPixel src_pixel = src_row[x];

            // Calculate overlay X coordinate using fixed-point (nearest-neighbor sampling)
            int64_t overlay_x = (x * scale_x) >> 16;
            if (overlay_x >= overlay_w) overlay_x = overlay_w - 1;

            // Get overlay pixel (still a palette-index byte in the loaded asset)
            unsigned char overlay_idx = overlay_row[overlay_x];

            // Index 255 is this overlay format's transparent sentinel (distinct
            // from the sprite-blit index-0 convention) - skip blending.
            if (overlay_idx == 255)
            {
                dst_row[x] = src_pixel;
                continue;
            }

            TbPixel overlay_pixel = resolve_indexed_pixel(overlay_idx, pal);

            // Alpha blend: result = (overlay * alpha + src * (1 - alpha)) / 256,
            // done per RGB channel now instead of averaging raw index bytes.
            dst_row[x] = TbPixel_RGBA(
                (uint8_t)((overlay_pixel.r * alpha_clamped + src_pixel.r * inv_alpha) >> 8),
                (uint8_t)((overlay_pixel.g * alpha_clamped + src_pixel.g * inv_alpha) >> 8),
                (uint8_t)((overlay_pixel.b * alpha_clamped + src_pixel.b * inv_alpha) >> 8),
                255);
        }
    }
}

/******************************************************************************/
// OVERLAYEFFECT PUBLIC INTERFACE
/******************************************************************************/

OverlayEffect::OverlayEffect()
    : LensEffect(LensEffectType::Overlay, "Overlay")
    , m_current_lens(-1)
{
}

OverlayEffect::~OverlayEffect()
{
    Cleanup();
}

TbBool OverlayEffect::Setup(int64_t lens_idx)
{
    SYNCDBG(8, "Setting up overlay effect for lens %" PRId64, (int64_t)(lens_idx));
    
    struct LensConfig* cfg = get_lens_config(lens_idx);
    if (cfg == NULL)
    {
        WARNLOG("Failed to get lens config for index %" PRId64, (int64_t)(lens_idx));
        return true;  // Continue without overlay
    }
    
    // Check if this lens has an overlay effect configured
    if ((cfg->flags & LCF_HasOverlay) == 0)
    {
        SYNCDBG(8, "Lens %" PRId64 " does not have overlay effect configured", (int64_t)(lens_idx));
        return true;  // Not an error - effect just not configured
    }
    
    // Create and setup renderer
    COverlayRenderer* renderer = new COverlayRenderer(this);
    
    if (!renderer->LoadOverlay(lens_idx))
    {
        WARNLOG("Failed to load overlay for lens %" PRId64 " - effect will be skipped", (int64_t)(lens_idx));
        delete renderer;
        return true;  // Continue without overlay effect (graceful degradation)
    }
    
    m_user_data = renderer;
    m_current_lens = lens_idx;
    
    SYNCDBG(7, "Overlay effect ready");
    return true;
}

void OverlayEffect::Cleanup()
{
    if (m_current_lens >= 0)
    {
        if (m_user_data != NULL)
        {
            delete static_cast<COverlayRenderer*>(m_user_data);
            m_user_data = NULL;
        }
        m_current_lens = -1;
        SYNCDBG(9, "Overlay effect cleaned up");
    }
}

TbBool OverlayEffect::Draw(LensRenderContext* ctx)
{
    if (m_current_lens < 0 || m_user_data == NULL)
    {
        return false;
    }
    
    SYNCDBG(16, "Drawing overlay effect");
    
    COverlayRenderer* renderer = static_cast<COverlayRenderer*>(m_user_data);
    
    // Overlay reads from source (3D view) and composites with overlay sprite to destination
    TbPixel* viewport_src = ctx->srcbuf + ctx->viewport_x;
    renderer->Render(ctx->dstbuf, ctx->dstpitch, viewport_src, ctx->srcpitch,
                    ctx->width, ctx->height);
    
    ctx->buffer_copied = true;  // We wrote the full frame to dstbuf
    return true;
}

/******************************************************************************/
