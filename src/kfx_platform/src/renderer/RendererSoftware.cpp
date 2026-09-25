#include "pre_inc.h"
#include "renderer/RendererSoftware.h"
#include "renderer/RendererManager.h" // RendererScreenOwned, renderer_imgui_callbacks
#include "bflib_video.h"       // PALETTE_COLORS, lbWindow, SDL, vsync_enabled
#include "bflib_vidsurface.h"  // lbDrawSurface
#include "bflib_render.h"      // draw_gpoly, vec_mode, VecModes
#include "bflib_vidraw.h"      // vec_map
#include <SDL3_image/SDL_image.h> // IMG_SavePNG (screenshots)
#include "post_inc.h"

bool RendererSoftware::Init()
{
    return true;
}

void RendererSoftware::Shutdown()
{
    destroy_present_target();
}

void RendererSoftware::SetDisplayPalette(const unsigned char* rgb8)
{
    // Vestigial: the draw surface is RGBA32 now, not an indexed surface with
    // its own SDL palette to push colours into. The game's authoritative
    // palette (LbPaletteGetReadonly()/RendererGetActivePalette()) is tracked
    // independently of the surface's pixel format and is what source-asset
    // bytes actually get resolved against; this callback has nothing left
    // to do for the software backend.
    (void)rgb8;
}

void RendererSoftware::ClearScreen(unsigned char colour)
{
    // A full clear starts a new frame: any GPU world underlay belonged to the
    // previous one. (Presents without a clear -- palette-fade steps, resync
    // and loading-screen progress presents -- keep showing it, exactly as
    // they keep re-presenting the unchanged CPU framebuffer.)
    m_underlay_pending = false;
    if (lbDrawSurface == NULL)
        return;
    // colour is a palette index (every caller passes a literal like 0 or
    // 144) -- resolve it through the game's own palette, then map it to
    // the draw surface's own (RGBA32) pixel format for the fill.
    TbPixel px = resolve_indexed_pixel(colour, LbPaletteGetReadonly());
    Uint32 mapped = SDL_MapSurfaceRGBA(lbDrawSurface, px.r, px.g, px.b, px.a);
    if (!SDL_FillSurfaceRect(lbDrawSurface, NULL, mapped))
        ERRORLOG("Error while clearing screen: %s", SDL_GetError());
}

bool RendererSoftware::ensure_present_target()
{
    if (m_renderer != nullptr && SDL_GetRenderWindow(m_renderer) != lbWindow)
        destroy_present_target();
    if (m_renderer == nullptr)
    {
        if (m_gpu_device != nullptr)
        {
            // gpu-v2 Phase C.1: share RendererGpu3D's SDL_GPUDevice instead
            // of letting SDL create/own its own -- see UseGpuDevice()'s
            // header comment.
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, lbWindow);
            SDL_SetStringProperty(props, SDL_PROP_RENDERER_CREATE_NAME_STRING, SDL_GPU_RENDERER);
            SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_GPU_DEVICE_POINTER, m_gpu_device);
            m_renderer = SDL_CreateRendererWithProperties(props);
            SDL_DestroyProperties(props);
        }
        else
        {
            m_renderer = SDL_CreateRenderer(lbWindow, NULL);
        }
        if (m_renderer == nullptr)
        {
            ERRORLOG("SDL_CreateRenderer failed: %s", SDL_GetError());
            return false;
        }
        // Name the backend SDL picked for us, so a bug report tells which graphics path the
        // game was presenting through, and which driver libraries that pulls into the process.
        const char* backend = SDL_GetRendererName(m_renderer);
        SYNCLOG("Presenting through SDL renderer: %s", (backend != nullptr) ? backend : "unknown");
    }

    const int64_t want_vsync = vsync_enabled ? 1 : 0;
    if (m_vsync != want_vsync)
    {
        SDL_SetRenderVSync(m_renderer, want_vsync);
        m_vsync = want_vsync;
    }

    if (m_texture == nullptr || m_tex_w != lbDrawSurface->w || m_tex_h != lbDrawSurface->h)
    {
        if (m_texture != nullptr) { SDL_DestroyTexture(m_texture); m_texture = nullptr; }
        m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
                                      SDL_TEXTUREACCESS_STREAMING, lbDrawSurface->w, lbDrawSurface->h);
        if (m_texture == nullptr)
        {
            ERRORLOG("SDL_CreateTexture failed: %s", SDL_GetError());
            return false;
        }
        SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_NEAREST); // crisp pixels
        m_tex_w = lbDrawSurface->w;
        m_tex_h = lbDrawSurface->h;
    }
    return true;
}

void RendererSoftware::destroy_present_target()
{
    // Must happen before m_renderer is destroyed below -- the ImGui
    // SDLRenderer3 backend holds references into it.
    renderer_imgui_callbacks->renderer_destroying();
    if (m_underlay_tex != nullptr) { SDL_DestroyTexture(m_underlay_tex); m_underlay_tex = nullptr; m_underlay_tex_src = nullptr; }
    if (m_texture != nullptr) { SDL_DestroyTexture(m_texture); m_texture = nullptr; }
    if (m_renderer != nullptr) { SDL_DestroyRenderer(m_renderer); m_renderer = nullptr; }
    m_tex_w = 0;
    m_tex_h = 0;
    m_vsync = -1;
}

unsigned char* RendererSoftware::LockFramebuffer(TbBytePitch* out_pitch)
{
    if (lbDrawSurface == NULL || !SDL_LockSurface(lbDrawSurface))
        return nullptr;
    if (out_pitch != nullptr)
        *out_pitch = TbBytePitch{ lbDrawSurface->pitch };
    return static_cast<unsigned char*>(lbDrawSurface->pixels);
}

void RendererSoftware::UnlockFramebuffer()
{
    if (lbDrawSurface != NULL)
        SDL_UnlockSurface(lbDrawSurface);
}

// docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2: this
// used to save lbDrawSurface synchronously and directly -- the CPU
// backdrop only, missing the ImGui overlay entirely. Now just validates
// and queues the request; perform_pending_screenshot() (called from
// PresentFrame(), post-composite) does the actual capture. The `bool`
// returned here means "queued", not "saved" -- true in every case except
// an invalid path/format, since the real capture almost always completes
// within the same frame, a few SDL calls later.
bool RendererSoftware::ScheduleScreenshot(const char* path, int64_t fmt)
{
    if (path == nullptr || (fmt != 1 && fmt != 2))
        return false;
    snprintf(m_screenshot_path, sizeof(m_screenshot_path), "%s", path);
    m_screenshot_fmt = fmt;
    m_screenshot_pending = true;
    return true;
}

// Called from PresentFrame(), after the ImGui overlay has been rendered
// into m_renderer's backbuffer but before SDL_RenderPresent() -- some
// backends leave the backbuffer undefined immediately after present, so
// this is the only safe window to read it back.
void RendererSoftware::perform_pending_screenshot()
{
    if (!m_screenshot_pending)
        return;
    SDL_Surface *captured = SDL_RenderReadPixels(m_renderer, nullptr);
    if (captured == nullptr)
    {
        ERRORLOG("Screenshot capture failed: %s", SDL_GetError());
        m_screenshot_pending = false;
        return;
    }
    // SDL_RenderReadPixels returns the renderer's own native pixel
    // format, not necessarily RGBA32 -- normalise to the exact format
    // lbDrawSurface always had, so IMG_SavePNG/SDL_SaveBMP see the same
    // shape of surface they always did, regardless of which backend/
    // format the active SDL_Renderer happens to use.
    SDL_Surface *converted = SDL_ConvertSurface(captured, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(captured);
    if (converted == nullptr)
    {
        ERRORLOG("Screenshot pixel-format conversion failed: %s", SDL_GetError());
        m_screenshot_pending = false;
        return;
    }
    bool ok;
    switch (m_screenshot_fmt)
    {
        case 1:  ok = IMG_SavePNG(converted, m_screenshot_path); break;
        case 2:  ok = SDL_SaveBMP(converted, m_screenshot_path); break;
        default: ok = false; break;
    }
    if (!ok)
        ERRORLOG("Screenshot save failed (%s): %s", m_screenshot_path, SDL_GetError());
    SDL_DestroySurface(converted);
    m_screenshot_pending = false;
}

void* RendererSoftware::CreateDynamicTexture(int64_t width, int64_t height)
{
    if (m_renderer == nullptr || width <= 0 || height <= 0)
        return nullptr;
    SDL_Texture *tex = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (tex == nullptr)
        return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    // Scaled/zoomed map content (land preview, minimap, level thumbnails)
    // -- LINEAR matches docs/refactor/renderer/04-imgui-gui-foundation.md
    // §5.1's decision for upscaled art in this migration.
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    return (void*)tex;
}

void RendererSoftware::UpdateDynamicTexture(void *texture, const void *rgba_data, int64_t width, int64_t height)
{
    if (texture == nullptr || rgba_data == nullptr)
        return;
    SDL_UpdateTexture((SDL_Texture*)texture, nullptr, rgba_data, width * 4);
}

void RendererSoftware::DestroyDynamicTexture(void *texture)
{
    if (texture != nullptr)
        SDL_DestroyTexture((SDL_Texture*)texture);
}

void RendererSoftware::PresentFrame()
{
    if (lbDrawSurface == NULL || !ensure_present_target())
        return;
    // docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B1: skip
    // the CPU->GPU upload entirely on a frame where its result is never
    // sampled. RendererScreenOwned() screens (every migrated frontend
    // screen, the in-game parchment map -- see the RendererScreenOwned()
    // branch below) never draw m_texture at all, so re-uploading the
    // whole framebuffer for them every frame was pure wasted CPU->GPU
    // bandwidth (a full lbDrawSurface, e.g. ~8MB at 1080p, for content
    // that's then discarded unread). Read RendererScreenOwned() once,
    // ahead of the upload, and gate both the upload and the draw on the
    // same value -- so a frame either uploads-and-draws m_texture, or
    // does neither; there's no way for the drawn texture to be one frame
    // stale.
    const TbBool screen_owned = RendererScreenOwned();
    if (!screen_owned)
    {
        // The draw surface is already RGBA32 -- the same format m_texture was
        // created with -- so presentation is a direct upload now, no more
        // INDEX8->RGBA blit through a locked texture surface.
        if (!SDL_UpdateTexture(m_texture, NULL, lbDrawSurface->pixels, lbDrawSurface->pitch))
        {
            ERRORLOG("Present texture update failed: %s", SDL_GetError());
            return;
        }
    }
    // docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase C: skip
    // the legacy framebuffer blit entirely for screens ImGui fully owns
    // (its own backdrop image, drawn from draw_menu_backdrop() below,
    // replaces it) -- otherwise it painted over whatever the legacy cursor
    // draw (bflib_mspointer.cpp) had just put into lbDrawSurface for the
    // area outside the small centred menu panel, since that backdrop image
    // is drawn *after* this blit, every frame. Still cleared to black first
    // so there's no stale content visible for even one frame before ImGui's
    // own background draw list runs.
    // gpu-v2 Phase C.1: with a GPU world-view underlay pending (see
    // SetWorldUnderlay()), draw it first and blend the CPU framebuffer over
    // it -- the CPU window there was cleared transparent, so only what the
    // CPU still draws on top (sprites, overlays) shows through.
    SDL_Texture* underlay = nullptr;
    if (m_underlay_pending && m_underlay_gpu != nullptr && !screen_owned && m_gpu_device != nullptr)
    {
        if (m_underlay_tex == nullptr || m_underlay_tex_src != m_underlay_gpu || m_underlay_tex_w != m_underlay_w || m_underlay_tex_h != m_underlay_h)
        {
            if (m_underlay_tex != nullptr) { SDL_DestroyTexture(m_underlay_tex); m_underlay_tex = nullptr; }
            SDL_PropertiesID tp = SDL_CreateProperties();
            SDL_SetPointerProperty(tp, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, m_underlay_gpu);
            SDL_SetNumberProperty(tp, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
            SDL_SetNumberProperty(tp, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_TARGET);
            SDL_SetNumberProperty(tp, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, m_underlay_w);
            SDL_SetNumberProperty(tp, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, m_underlay_h);
            m_underlay_tex = SDL_CreateTextureWithProperties(m_renderer, tp);
            SDL_DestroyProperties(tp);
            m_underlay_tex_src = m_underlay_gpu;
            m_underlay_tex_w = m_underlay_w; m_underlay_tex_h = m_underlay_h;
            if (m_underlay_tex == nullptr)
                ERRORLOG("Wrapping the GPU world texture failed: %s", SDL_GetError());
            else
                SDL_SetTextureScaleMode(m_underlay_tex, SDL_SCALEMODE_NEAREST);
        }
        underlay = m_underlay_tex;
    }

    if (screen_owned)
    {
        SDL_RenderClear(m_renderer);
    }
    else if (underlay != nullptr)
    {
        SDL_RenderClear(m_renderer);
        SDL_SetTextureBlendMode(underlay, SDL_BLENDMODE_NONE);
        SDL_RenderTexture(m_renderer, underlay, NULL, NULL);
        SDL_BlendMode saved_blend = SDL_BLENDMODE_NONE;
        SDL_GetTextureBlendMode(m_texture, &saved_blend);
        SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_BLEND);
        SDL_RenderTexture(m_renderer, m_texture, NULL, NULL);
        SDL_SetTextureBlendMode(m_texture, saved_blend);
    }
    else
    {
        SDL_RenderClear(m_renderer);
        SDL_RenderTexture(m_renderer, m_texture, NULL, NULL);
    }

    // docs/refactor/renderer/04-imgui-gui-foundation.md §3.3/§3.4: the
    // software framebuffer above is the backdrop layer; ImGui composites as
    // a true overlay on top of it, between the backdrop blit and present.
    //
    // Reentrancy guard: found live (real SIGABRT, real backtrace) that
    // frontend_set_state() -- called from renderer_imgui_callbacks->submit()'s
    // own deferred-pending-state application, itself already inside this
    // function's begin_frame()/render() pair -- used to trigger
    // fade_out()/fade_in() (ProperFadePalette -> LbPaletteFade ->
    // LbPaletteFadeStep), whose own multi-step animation loop called
    // the present entry point (RendererPresentStepFrame() today; the raw
    // RendererPresentFrame() this comment originally described was folded
    // into it, docs/refactor/renderer/gpu-v2/06-call-site-consolidation.md)
    // again per step to actually show the palette dimming/brightening.
    // Without this guard, that nested call re-entered
    // the ImGui block below and tried to start a second ImGui frame before
    // the outer one had reached Render(), tripping ImGui's own
    // ErrorCheckNewFrameSanityChecks() ("Forgot to call Render() or
    // EndFrame()..."). fade_out()/fade_in() themselves are gone now
    // (docs/refactor/renderer/05-imgui-owned-menu-backdrop.md Phase 0) --
    // this was their only known trigger, so the guard is provably dead as
    // of that change, but left in place as a harmless defensive no-op
    // rather than removed sight unseen; revisit once live testing confirms
    // nothing else re-enters this function the same way.
    static bool s_presenting_imgui_frame = false;
    if (!s_presenting_imgui_frame && renderer_imgui_callbacks->ensure(lbWindow, m_renderer))
    {
        s_presenting_imgui_frame = true;
        renderer_imgui_callbacks->begin_frame();
        renderer_imgui_callbacks->submit(); // kfx_frontend's §5 wrappers / Phase B style-sheet test screen
        renderer_imgui_callbacks->render();
        // docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md B2:
        // the composited backbuffer (backdrop + ImGui overlay) is complete
        // right here -- after render(), before SDL_RenderPresent() below.
        // Deliberately inside this reentrancy-guarded block (not after
        // it), so a nested/re-entrant present -- which skips ImGui
        // submission entirely -- never captures a frame with no overlay
        // in it; the request just stays pending and is retried on the
        // next (outer) present instead.
        perform_pending_screenshot();
        s_presenting_imgui_frame = false;
    }

    SDL_RenderPresent(m_renderer);
}

// docs/refactor/renderer/gpu-v2/04-architecture-and-ir-boundary.md Phase
// C.0: the CPU reference implementation of the WorldFrame seam --
// translates each already-flattened QK_PolygonStandard item straight back
// into the same vec_mode/vec_map/draw_gpoly() sequence
// engine_render.c's display_drawlist() already runs for that bucket kind
// (see kfx_render/src/engine_render.c's QK_PolygonStandard case), so this
// round-trip is provably a no-op on rasterized output -- it does not (yet)
// replace that live call site; see WorldFrame.h's file comment for why the
// flatten/collect side stays dormant until a second (GPU) implementer of
// SubmitWorldFrame() exists to justify paying the redirection cost in the
// hot present path.
void RendererSoftware::SubmitWorldFrame(const WorldFrame& frame)
{
    for (int64_t i = 0; i < frame.op_count; ++i)
    {
        if (frame.ops[i].kind != WF_OP_POLY)
            continue; // sprites were never captured for the CPU path -- see WorldFrame.h
        WorldFramePolyItem item = frame.ops[i].u.poly; // copy: draw_gpoly() takes non-const PolyPoint*
        vec_mode = VM_QuadTextured;
        vec_map = item.texture;
        draw_gpoly(&item.v0, &item.v1, &item.v2);
    }
}
