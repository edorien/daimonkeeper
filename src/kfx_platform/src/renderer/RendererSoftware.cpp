#include "pre_inc.h"
#include "renderer/RendererSoftware.h"
#include "renderer/RendererManager.h" // RendererScreenOwned, RendererRunImGuiFrameCallback
#include "bflib_video.h"       // PALETTE_COLORS, lbWindow, SDL, vsync_enabled
#include "bflib_vidsurface.h"  // lbDrawSurface
#include "bflib_mouse.h"       // LbMouseOnBeginSwap/EndSwap (software cursor around present)
#include "gui/ImGuiContext.h"
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
        m_renderer = SDL_CreateRenderer(lbWindow, NULL);
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
    ImGuiContextShutdown();
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

bool RendererSoftware::ScheduleScreenshot(const char* path, int64_t fmt)
{
    if (lbDrawSurface == NULL)
        return false;
    bool ok;
    switch (fmt)
    {
        case 1:  ok = IMG_SavePNG(lbDrawSurface, path); break;
        case 2:  ok = SDL_SaveBMP(lbDrawSurface, path); break;
        default: return false;
    }
    if (!ok)
        ERRORLOG("Screenshot save failed (%s): %s", path, SDL_GetError());
    return ok;
}

void RendererSoftware::PresentFrame()
{
    if (lbDrawSurface == NULL || !ensure_present_target())
        return;
    LbMouseOnBeginSwap();
    // The draw surface is already RGBA32 -- the same format m_texture was
    // created with -- so presentation is a direct upload now, no more
    // INDEX8->RGBA blit through a locked texture surface.
    if (!SDL_UpdateTexture(m_texture, NULL, lbDrawSurface->pixels, lbDrawSurface->pitch))
    {
        ERRORLOG("Present texture update failed: %s", SDL_GetError());
        LbMouseOnEndSwap();
        return;
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
    if (RendererScreenOwned())
    {
        SDL_RenderClear(m_renderer);
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
    // frontend_set_state() -- called from RendererRunImGuiFrameCallback()'s
    // own deferred-pending-state application, itself already inside this
    // function's ImGuiContextNewFrame()/Render() pair -- used to trigger
    // fade_out()/fade_in() (ProperFadePalette -> LbPaletteFade ->
    // LbPaletteFadeStep), whose own multi-step animation loop called
    // RendererPresentFrame() again per step to actually show the palette
    // dimming/brightening. Without this guard, that nested call re-entered
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
    if (!s_presenting_imgui_frame && ImGuiContextEnsure(lbWindow, m_renderer))
    {
        s_presenting_imgui_frame = true;
        ImGuiContextNewFrame();
        RendererRunImGuiFrameCallback(); // kfx_frontend's §5 wrappers / Phase B style-sheet test screen
        ImGuiContextRender();
        s_presenting_imgui_frame = false;
    }

    SDL_RenderPresent(m_renderer);
    LbMouseOnEndSwap();
}
