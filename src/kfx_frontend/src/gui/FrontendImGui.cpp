#include "pre_inc.h"
#include "gui/FrontendImGui.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>
#include "kjm_input.h"       // GetMouseX/GetMouseY
#include "frontend.h"        // frontend_menu_state
#include "frontgui_screens.h" // frontend_imgui_screen_active
#include "frontgui_ingame_parchment.h" // ingame_parchment_active
#include "frontgui_style.h"  // FeStyleGetCursorImage
#include "post_inc.h"

namespace {
    bool s_active = false;
    bool s_demo_visible = false;
    SDL_Window*   s_window   = nullptr;
    SDL_Renderer* s_renderer = nullptr;
    SDL_Texture* s_cursor_texture = nullptr;
    int64_t s_cursor_w = 0, s_cursor_h = 0;
    int64_t s_cursor_hotspot_x = 0, s_cursor_hotspot_y = 0;
    bool s_cursor_native_size = false;
    bool s_cursor_have = false;          // FeStyleGetCursorImage produced an image this frame
    uint64_t s_cursor_serial = 0xFFFFFFFFu;
    int64_t s_cursor_tex_w = 0, s_cursor_tex_h = 0;

    void shutdown_backends()
    {
        if (!s_active)
            return;
        if (s_cursor_texture != nullptr)
        {
            SDL_DestroyTexture(s_cursor_texture);
            s_cursor_texture = nullptr;
        }
        s_cursor_tex_w = s_cursor_tex_h = 0;
        s_cursor_serial = 0xFFFFFFFFu;
        s_cursor_have = false;
        ImGui_ImplSDLRenderer3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        s_active = false;
        s_window = nullptr;
        s_renderer = nullptr;
    }

    // Polled every frame: the in-game provider returns the game's *current*
    // pointer sprite (which changes), the frontend provider a fixed one
    // (serial 0). Re-uploads the SDL texture only when `serial` or the
    // dimensions change; recreates it when the size changes. A provider
    // that isn't ready yet (frontend sprite sheet not loaded, in-game
    // pointer hidden) returns false -- s_cursor_have goes false and no
    // cursor is drawn that frame, and it keeps retrying.
    void refresh_cursor_texture()
    {
        s_cursor_have = false;

        ImGuiCursorImage img = {};
        if (!FeStyleGetCursorImage(&img) || img.rgba == nullptr || img.width <= 0 || img.height <= 0)
            return;

        if (s_cursor_texture == nullptr || img.width != s_cursor_tex_w || img.height != s_cursor_tex_h)
        {
            if (s_cursor_texture != nullptr)
                SDL_DestroyTexture(s_cursor_texture);
            s_cursor_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STREAMING, img.width, img.height);
            if (s_cursor_texture == nullptr)
            {
                s_cursor_tex_w = s_cursor_tex_h = 0;
                return;
            }
            SDL_SetTextureBlendMode(s_cursor_texture, SDL_BLENDMODE_BLEND);
            SDL_SetTextureScaleMode(s_cursor_texture, SDL_SCALEMODE_NEAREST);
            s_cursor_tex_w = img.width;
            s_cursor_tex_h = img.height;
            s_cursor_serial = img.serial - 1u;   // force the upload below
        }
        if (img.serial != s_cursor_serial)
        {
            SDL_UpdateTexture(s_cursor_texture, nullptr, img.rgba, img.width * 4);
            s_cursor_serial = img.serial;
        }

        s_cursor_w = img.width;
        s_cursor_h = img.height;
        s_cursor_hotspot_x = img.hotspot_x;
        s_cursor_hotspot_y = img.hotspot_y;
        s_cursor_native_size = (img.native_size != 0);
        s_cursor_have = true;
    }
}

extern "C" {

TbBool FrontendImGuiEnsure(SDL_Window *window, SDL_Renderer *renderer)
{
    if (window == nullptr || renderer == nullptr)
        return 0;

    if (s_active && s_window == window && s_renderer == renderer)
        return 1;

    // Either not yet created, or the window/renderer changed under us
    // (RendererSoftware::ensure_present_target recreates both when the SDL
    // window changes) -- tear down and recreate against the new handles.
    shutdown_backends();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.MouseDrawCursor = false; // the game draws its own cursor sprite around the swap
    io.IniFilename = nullptr;   // no imgui.ini next to the game binary
    // Found live ("cursor alignment seems to be (0,0) over the land view
    // preview, while being (26,18) everywhere else" / "large cursor" during
    // the quit transition): ImGui_ImplSDL3_UpdateMouseCursor()
    // (imgui_impl_sdl3.cpp) calls SDL_ShowCursor()/SDL_SetCursor() every
    // frame whenever io.MouseDrawCursor is false and ImGui::GetMouseCursor()
    // isn't ImGuiMouseCursor_None -- i.e. always, since nothing in this
    // codebase ever calls ImGui::SetMouseCursor(). Nothing in this codebase
    // calls SDL_HideCursor()/SDL_ShowCursor()/SDL_SetCursor() directly
    // either -- OS cursor visibility is left entirely to SDL's own relative
    // mouse mode (Ft_RelativeMouseMode, main.cpp) hiding it automatically.
    // ImGui's backend calling SDL_ShowCursor() every frame fights that,
    // intermittently winning the race and showing the real OS cursor (its
    // own native shape and hotspot, unrelated to this game's sprite or
    // FeStyleGetCursorImage's hotspot at all) on top of/instead of the
    // custom-drawn one -- most visible wherever the timing tips in its
    // favour (an embedded widget like the land preview panel) or when the
    // custom cursor stops drawing entirely (leaving only the OS one, during
    // the brief FeSt_QUIT_GAME/FeSt_INITIAL handoff, neither ImGui-owned).
    // This flag makes the backend never touch OS cursor visibility/shape at
    // all, leaving SDL's relative-mode hiding as the sole authority.
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer))
    {
        ImGui::DestroyContext();
        return 0;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer))
    {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return 0;
    }

    s_window = window;
    s_renderer = renderer;
    s_active = true;
    return 1;
}

void FrontendImGuiRendererDestroying(void)
{
    shutdown_backends();
}

TbBool FrontendImGuiIsActive(void)
{
    return s_active ? 1 : 0;
}

void FrontendImGuiProcessEvent(const SDL_Event *event)
{
    if (!s_active || event == nullptr)
        return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

TbBool FrontendImGuiScreenOwned(void)
{
    return frontend_imgui_screen_active(frontend_menu_state) || ingame_parchment_active();
}

void FrontendImGuiBeginFrame(void)
{
    if (!s_active)
        return;
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    // Override whatever position raw (possibly warp-confused) motion
    // events produced with the game's own tracked position, before
    // NewFrame() drains the queued input events -- the game's own mouse
    // handling (bflib_inputctrl.cpp) grab-warps the OS cursor back toward
    // the window centre whenever it nears an edge ("warp-based relative
    // motion"), tracking its real logical position via accumulated deltas
    // instead of the OS cursor's absolute position, so a raw motion event's
    // absolute x/y doesn't reflect it.
    ImGui::GetIO().AddMousePosEvent((double)GetMouseX(), (double)GetMouseY());

    ImGui::NewFrame();

    // ImGui's own built-in software cursor (io.MouseDrawCursor) is a
    // generic arrow -- visibly mismatched against the game's actual cursor
    // sprite everywhere else, found live. Draw that same sprite ourselves
    // instead, via the foreground draw list, always on top regardless of
    // which window is current: io.MouseDrawCursor stays permanently false.
    //
    // docs/refactor/renderer/gpu-v2/01-phase-b-2d-compositing.md's cursor
    // unification: this used to draw only while io.WantCaptureMouse or
    // FrontendImGuiScreenOwned() was true, falling back to a second,
    // entirely separate cursor mechanism the rest of the time --
    // bflib_mspointer.cpp's LbI_PointerHandler blitting the sprite
    // straight into the locked framebuffer, gated on the exact inverse
    // condition. Two coexisting draws, kept from visibly doubling up only
    // by those gates staying each other's precise complement -- found
    // live to already be racy (R7, docs/refactor/renderer/gpu-v2/
    // 08-risks.md): OnBeginSwap() (the old legacy draw) ran at the very
    // start of PresentFrame(), before this frame's own begin_frame() had
    // recomputed WantCaptureMouse, so a WantCaptureMouse transition
    // between two frames could pass through a frame where both drew, or
    // neither did. Now there is exactly one cursor mechanism, drawn
    // unconditionally (gated only on a valid image existing at all, via
    // s_cursor_have below) -- bflib_mspointer.cpp no longer draws
    // anything, only tracks position/sprite/hotspot state (still needed:
    // FeStyleGetCursorImage()'s call to LbMouseGetSprite() and
    // GetPointerHotspot() below both read it).
    ImGuiIO &io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    refresh_cursor_texture();
    if (s_cursor_have && s_cursor_texture != nullptr && s_cursor_h > 0)
    {
        if (s_cursor_native_size)
        {
            // Already scaled to the game's own cursor size by the provider
            // -- draw 1:1 so it matches the cursor over the 3D view exactly.
            const ImVec2 pos(io.MousePos.x - (double)s_cursor_hotspot_x,
                             io.MousePos.y - (double)s_cursor_hotspot_y);
            ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(intptr_t)s_cursor_texture,
                pos, ImVec2(pos.x + (double)s_cursor_w, pos.y + (double)s_cursor_h));
        }
        else
        {
        // The texture holds the sprite at its native pixel size (built
        // once, cached). This used to be rescaled through
        // scale_ui_value_lofi() -- the same legacy DK-asset scale
        // LbI_PointerHandler::OnBeginSwap (bflib_mspointer.cpp) used for
        // the cursor outside ImGui content, back when that was a second,
        // separate draw path (retired by the cursor-unification pass that
        // added the comment above this block) -- specifically to avoid a
        // visible pop crossing the ImGui/legacy boundary. But that scale is tuned
        // for bitmap UI stretched proportionally from a 640x400 reference
        // (units_per_pixel_ui, vidmode.c's update_screen_mode_data(): grows
        // roughly with io.DisplaySize.y/25), while ImGui content is laid
        // out in real native pixels sized off io.DisplaySize.y/32
        // (FeStylePushFont, frontgui_style.cpp) -- a visibly gentler curve.
        // The two happen to roughly agree around 640x480 (where this was
        // last tuned) but diverge sharply at higher resolutions -- found
        // live at 1080p+ as a cursor large enough to obscure the very
        // button it's meant to click. 1.0x -- roughly matching body-text
        // height -- is the current middle ground.
        double ref_px = io.DisplaySize.y / 32.0;
        if (ref_px < 11.0) ref_px = 11.0;
        if (ref_px > 96.0) ref_px = 96.0;
        double target_h = ref_px * 1.0;
        double cursor_scale = target_h / (double)s_cursor_h;
        double scaled_w = (double)s_cursor_w * cursor_scale;
        double scaled_h = (double)s_cursor_h * cursor_scale;
        double scaled_hot_x = (double)s_cursor_hotspot_x * cursor_scale;
        double scaled_hot_y = (double)s_cursor_hotspot_y * cursor_scale;
        ImVec2 pos(io.MousePos.x - scaled_hot_x, io.MousePos.y - scaled_hot_y);
        ImGui::GetForegroundDrawList()->AddImage((ImTextureID)(intptr_t)s_cursor_texture,
            pos, ImVec2(pos.x + scaled_w, pos.y + scaled_h));
        }
    }

    if (s_demo_visible)
        ImGui::ShowDemoWindow(&s_demo_visible);
}

void FrontendImGuiRender(void)
{
    if (!s_active)
        return;
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s_renderer);
}

void FrontendImGuiSetDemoVisible(TbBool visible)
{
    s_demo_visible = (visible != 0);
}

} // extern "C"
