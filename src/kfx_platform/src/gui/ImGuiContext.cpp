#include "pre_inc.h"
#include "gui/ImGuiContext.h"
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>
#include <SDL3/SDL.h>
#include "post_inc.h"

namespace {
    bool s_active = false;
    bool s_demo_visible = false;
    SDL_Window*   s_window   = nullptr;
    SDL_Renderer* s_renderer = nullptr;
    ImGuiMousePositionFn s_mouse_position_fn = nullptr;
    ImGuiCursorImageFn s_cursor_image_fn = nullptr;
    SDL_Texture* s_cursor_texture = nullptr;
    int64_t s_cursor_w = 0, s_cursor_h = 0;
    int64_t s_cursor_hotspot_x = 0, s_cursor_hotspot_y = 0;
    bool s_cursor_native_size = false;
    bool s_cursor_have = false;          // callback produced an image this frame
    uint64_t s_cursor_serial = 0xFFFFFFFFu;
    int64_t s_cursor_tex_w = 0, s_cursor_tex_h = 0;
    ImGuiScreenOwnedFn s_screen_owned_fn = nullptr;

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
        if (s_cursor_image_fn == nullptr)
            return;

        ImGuiCursorImage img = {};
        if (!s_cursor_image_fn(&img) || img.rgba == nullptr || img.width <= 0 || img.height <= 0)
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

TbBool ImGuiContextEnsure(SDL_Window *window, SDL_Renderer *renderer)
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

void ImGuiContextShutdown(void)
{
    shutdown_backends();
}

TbBool ImGuiContextIsActive(void)
{
    return s_active ? 1 : 0;
}

void ImGuiContextProcessEvent(const SDL_Event *event)
{
    if (!s_active || event == nullptr)
        return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

void ImGuiContextSetMousePositionCallback(ImGuiMousePositionFn fn)
{
    s_mouse_position_fn = fn;
}

void ImGuiContextSetCursorImageCallback(ImGuiCursorImageFn fn)
{
    s_cursor_image_fn = fn;
}

void ImGuiContextSetScreenOwnedCallback(ImGuiScreenOwnedFn fn)
{
    s_screen_owned_fn = fn;
}

TbBool ImGuiContextScreenOwned(void)
{
    return (s_screen_owned_fn != nullptr) && s_screen_owned_fn();
}

void ImGuiContextNewFrame(void)
{
    if (!s_active)
        return;
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    // Override whatever position raw (possibly warp-confused) motion
    // events produced with the game's own tracked position, before
    // NewFrame() drains the queued input events -- see this function's
    // declaration comment (ImGuiContext.h) for why.
    if (s_mouse_position_fn != nullptr)
    {
        int64_t x = 0, y = 0;
        s_mouse_position_fn(&x, &y);
        ImGui::GetIO().AddMousePosEvent((double)x, (double)y);
    }

    ImGui::NewFrame();

    // ImGui's own built-in software cursor (io.MouseDrawCursor) is a
    // generic arrow -- visibly mismatched against the game's actual cursor
    // sprite everywhere else, found live. Draw that same sprite ourselves
    // instead: io.MouseDrawCursor stays permanently false, and whenever
    // the pointer is over ImGui content (§3.3: the game's own cursor is
    // baked into the software backdrop *before* ImGui composites on top of
    // it, so it needs a stand-in exactly then) the cursor image callback's
    // texture is drawn via the foreground draw list, always on top
    // regardless of which window is current. WantCaptureMouse reflects
    // last frame's window layout (queried right after NewFrame, before
    // this frame's content is submitted) -- the standard way to read it,
    // and window layouts are stable frame to frame so the one-frame lag
    // isn't visible in practice.
    ImGuiIO &io = ImGui::GetIO();
    io.MouseDrawCursor = false;
    refresh_cursor_texture();
    // docs/refactor/renderer/05-imgui-owned-menu-backdrop.md: found live,
    // "cursor only appears when hovering over menus, disappears over
    // background" -- WantCaptureMouse alone is only true over the actual
    // centred menu panel, not the surrounding backdrop area, which is
    // itself drawn by ImGui now (draw_menu_backdrop(), frontgui_screens.cpp)
    // but isn't a real window/doesn't set WantCaptureMouse. The legacy
    // cursor draw that used to fall back to over that backdrop area
    // (bflib_mspointer.cpp) gets painted over by that same backdrop image,
    // drawn after it in the frame. ImGuiContextScreenOwned() is true for
    // exactly those screens (no live legacy content underneath at all), so
    // draw the ImGui cursor unconditionally there -- there's no legacy
    // fallback left to defer to on any part of the screen.
    bool want_cursor = io.WantCaptureMouse || ImGuiContextScreenOwned();
    if (want_cursor && s_cursor_have && s_cursor_texture != nullptr && s_cursor_h > 0)
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
        // LbI_PointerHandler::OnBeginSwap (bflib_mspointer.cpp) uses for the
        // cursor outside ImGui content -- specifically to avoid a visible
        // pop crossing the ImGui/legacy boundary. But that scale is tuned
        // for bitmap UI stretched proportionally from a 640x400 reference
        // (units_per_pixel_ui, vidmode.c's update_screen_mode_data(): grows
        // roughly with io.DisplaySize.y/25), while ImGui content is laid
        // out in real native pixels sized off io.DisplaySize.y/32
        // (FeStylePushFont, frontgui_style.cpp) -- a visibly gentler curve.
        // The two happen to roughly agree around 640x480 (where this was
        // last tuned) but diverge sharply at higher resolutions -- found
        // live at 1080p+ as a cursor large enough to obscure the very
        // button it's meant to click. Kfx_platform can't reach
        // FeStylePushFont directly (kfx_config/kfx_frontend sit above it),
        // so this re-derives the same shape locally against io.DisplaySize,
        // trading the old "matches the legacy cursor exactly" guarantee for
        // "stays clickable-sized against ImGui content", which matters more
        // here since ImGui is where every clickable control now lives.
        // Found live, twice, in opposite directions: 1.2x made the cursor
        // "significantly too large" (tried right after switching the
        // sprite source to MousePG_Arrow, frontgui_style.cpp), then 0.7x
        // (tried as a smaller, more conservative correction) came back
        // "tiny" once the black-cursor palette bug was also reported --
        // the sprite source has since reverted to GFS_cursor_horny (same
        // file's own comment), which has different native dimensions
        // again, so neither prior data point necessarily still applies.
        // 1.0x -- roughly matching body-text height -- is a fresh middle
        // ground, not yet confirmed live either way.
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

void ImGuiContextRender(void)
{
    if (!s_active)
        return;
    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), s_renderer);
}

void ImGuiContextSetDemoVisible(TbBool visible)
{
    s_demo_visible = (visible != 0);
}

TbBool ImGuiContextWantCaptureMouse(void)
{
    if (!s_active)
        return 0;
    return ImGui::GetIO().WantCaptureMouse ? 1 : 0;
}

TbBool ImGuiContextWantCaptureKeyboard(void)
{
    if (!s_active)
        return 0;
    return ImGui::GetIO().WantCaptureKeyboard ? 1 : 0;
}

void* ImGuiContextCreateTexture(int64_t width, int64_t height)
{
    if (!s_active || width <= 0 || height <= 0)
        return nullptr;
    SDL_Texture *tex = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STREAMING, width, height);
    if (tex == nullptr)
        return nullptr;
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    // Unlike the cursor's NEAREST (a small overlay meant to stay crisp at
    // 1:1), this texture is scaled/zoomed map content -- LINEAR matches
    // the plan doc's own §5.1 decision ("Ship the low-resolution chrome
    // as-is. Linear filtering...") for upscaled art in this migration.
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
    return (void*)tex;
}

void ImGuiContextUpdateTexture(void *texture, const void *rgba_data, int64_t width, int64_t height)
{
    if (texture == nullptr || rgba_data == nullptr)
        return;
    SDL_UpdateTexture((SDL_Texture*)texture, nullptr, rgba_data, width * 4);
}

void ImGuiContextDestroyTexture(void *texture)
{
    if (texture != nullptr)
        SDL_DestroyTexture((SDL_Texture*)texture);
}

} // extern "C"
