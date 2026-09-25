/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file WindowSystemSDL.cpp
 *     SDL3 desktop window-system implementation of IWindowSystem.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "platform/WindowSystemSDL.h"
#include "bflib_basics.h"
#include "bflib_video.h"
#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include "post_inc.h"

#ifndef _WIN32
extern "C" const unsigned char kfx_window_icon_png[];
extern "C" const uint64_t kfx_window_icon_png_size;
#endif

static void ApplyWindowIcon(SDL_Window *window)
{
#ifndef _WIN32
    SDL_Surface *icon = IMG_Load_IO(SDL_IOFromConstMem(kfx_window_icon_png, kfx_window_icon_png_size), true);
    if (icon == nullptr) {
        WARNLOG("Failed to load window icon: %s", SDL_GetError());
        return;
    }
    if (!SDL_SetWindowIcon(window, icon)) {
        WARNLOG("Failed to window icon: %s", SDL_GetError());
    }
    SDL_DestroySurface(icon);
#else
    // MS Windows executable gets icon from .rc resource
#endif
}

/******************************************************************************/

static WindowSystemSDL s_sdlWindowSystem;

WindowSystemSDL* GetSDLWindowSystem()
{
    return &s_sdlWindowSystem;
}

// Translate a 0-based display index (as used by the span-all-displays loop in
// bflib_video.c) into an SDL3 display ID. Falls back to the primary display.
static SDL_DisplayID display_index_to_id(int64_t index)
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    SDL_DisplayID id = (displays && index >= 0 && index < count)
                       ? displays[index]
                       : SDL_GetPrimaryDisplay();
    if (displays) SDL_free(displays);
    return id;
}

bool WindowSystemSDL::IsAppActive() const { return m_appActive; }
void WindowSystemSDL::OnFocusGained()     { m_appActive = true;  ApplyOsCursorPolicy(); }
void WindowSystemSDL::OnFocusLost()       { m_appActive = false; ApplyOsCursorPolicy(); }

void WindowSystemSDL::ApplyOsCursorPolicy()
{
    if (!lbWindow)
        return;
    const bool focused = (SDL_GetWindowFlags(lbWindow) & SDL_WINDOW_INPUT_FOCUS) != 0;
    if (focused)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}

void WindowSystemSDL::SetCursorGrab(bool grab)
{
    if (!lbWindow)
        return;
    if (m_useRelativeMouse) {
        // deltas straight from the OS.
        SDL_SetWindowRelativeMouseMode(lbWindow, grab);
    }
    else{
        // confine the cursor to the window and re-center it.
        SDL_SetWindowMouseGrab(lbWindow, grab);
        if (grab)
        {
            int w = 0, h = 0;
            SDL_GetWindowSize(lbWindow, &w, &h);
            SDL_WarpMouseInWindow(lbWindow, w / 2.0, h / 2.0);
        }
    }
    ApplyOsCursorPolicy();
}

void WindowSystemSDL::SetUseRelativeMouse(bool relative)
{
    if (relative == m_useRelativeMouse)
        return;

    if (lbWindow)
    {
        if (m_useRelativeMouse)
            SDL_SetWindowRelativeMouseMode(lbWindow, false);
        else
            SDL_SetWindowMouseGrab(lbWindow, false);
    }
    m_useRelativeMouse = relative;
}

void WindowSystemSDL::SetCursorVisible(bool visible)
{
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void WindowSystemSDL::WarpCursor(int64_t x, int64_t y)
{
    if (!lbWindow)
        return;
    SDL_WarpMouseInWindow(lbWindow, (double)x, (double)y);
}

bool WindowSystemSDL::IsCursorInWindow() const
{
    if (!lbWindow)
        return false;
    float gx = 0.0f, gy = 0.0f;
    SDL_GetGlobalMouseState(&gx, &gy);
    int wx = 0, wy = 0, ww = 0, wh = 0;
    SDL_GetWindowPosition(lbWindow, &wx, &wy);
    SDL_GetWindowSize(lbWindow, &ww, &wh);
    return gx >= wx && gx < wx + ww && gy >= wy && gy < wy + wh;
}

// ----- Window management -----

bool WindowSystemSDL::HasWindow() const          { return lbWindow != nullptr; }
SDL_Window* WindowSystemSDL::GetSDLWindow() const { return lbWindow; }

uint64_t WindowSystemSDL::GetWindowFlags() const
{
    if (!lbWindow)
        return 0;
    SDL_WindowFlags sdl_flags = SDL_GetWindowFlags(lbWindow);
    uint64_t kfx_flags = 0;
    if (sdl_flags & SDL_WINDOW_FULLSCREEN)
    {
        // SDL3: a NULL fullscreen mode means desktop (borderless) fullscreen;
        // a non-NULL mode means an exclusive video mode.
        if (SDL_GetWindowFullscreenMode(lbWindow) == nullptr)
            kfx_flags |= KFX_WF_FULLSCREEN_DESKTOP;
        else
            kfx_flags |= KFX_WF_FULLSCREEN_EXCLUSIVE;
    }
    if (sdl_flags & SDL_WINDOW_BORDERLESS) kfx_flags |= KFX_WF_BORDERLESS;
    if (sdl_flags & SDL_WINDOW_HIDDEN)     kfx_flags |= KFX_WF_HIDDEN;
    return kfx_flags;
}

void WindowSystemSDL::GetWindowSize(int64_t* out_w, int64_t* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (lbWindow == nullptr)
        return;
    int w = 0, h = 0;
    SDL_GetWindowSize(lbWindow, &w, &h);
    if (out_w) *out_w = w;
    if (out_h) *out_h = h;
}

int64_t WindowSystemSDL::GetWindowDisplayIndex() const
{
    if (lbWindow)
        return (int64_t)SDL_GetDisplayForWindow(lbWindow);

    return (int64_t)SDL_GetPrimaryDisplay();
}

int64_t WindowSystemSDL::GetNumVideoDisplays() const
{
    int count = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&count);
    if (displays)
        SDL_free(displays);
    return count;
}

int64_t WindowSystemSDL::GetDesktopDisplayMode(int64_t display, int64_t* out_w, int64_t* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    // SDL3 display IDs start at 1; 0 means "not set yet" — fall back to primary.
    SDL_DisplayID disp_id = (display > 0) ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* mode = SDL_GetDesktopDisplayMode(disp_id);
    if (!mode)
        return -1;
    if (out_w) *out_w = mode->w;
    if (out_h) *out_h = mode->h;
    return 0;
}

int64_t WindowSystemSDL::GetDisplayBounds(int64_t display, int64_t* out_x, int64_t* out_y, int64_t* out_w, int64_t* out_h) const
{
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    SDL_DisplayID disp_id = display_index_to_id(display);
    SDL_Rect rect = {0, 0, 0, 0};
    if (!SDL_GetDisplayBounds(disp_id, &rect))
        return -1;
    if (out_x) *out_x = rect.x;
    if (out_y) *out_y = rect.y;
    if (out_w) *out_w = rect.w;
    if (out_h) *out_h = rect.h;
    return 0;
}

int64_t WindowSystemSDL::GetClosestDisplayMode(int64_t display, int64_t desired_w, int64_t desired_h, int64_t* out_w, int64_t* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    SDL_DisplayID disp_id = (display > 0) ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
    SDL_DisplayMode closest = {};
    // include_high_density_modes must be true here, matching
    // GetFullscreenDisplayModeAt()/GetFullscreenDisplayModeCount()'s own
    // SDL_GetFullscreenDisplayModes() call (which doesn't filter by pixel
    // density at all) -- otherwise a resolution the INGAME_RES picker
    // legitimately offered (enumerated including high-density modes) could
    // fail this exact-match check (LbHwCheckIsModeAvailable, bflib_video.c)
    // simply because it only exists as a high-density mode on this display,
    // rejecting a mode the user was never told was unavailable and silently
    // falling back to the 640x480 failsafe.
    if (!SDL_GetClosestFullscreenDisplayMode(disp_id, desired_w, desired_h, 0.0, true, &closest))
        return 0;
    if (out_w) *out_w = closest.w;
    if (out_h) *out_h = closest.h;
    return 1;
}

int64_t WindowSystemSDL::SetWindowDisplayMode(int64_t w, int64_t h)
{
    if (lbWindow == nullptr)
        return -1;
    SDL_DisplayID disp_id = SDL_GetDisplayForWindow(lbWindow);
    SDL_DisplayMode dm = {};
    // include_high_density_modes true, same reasoning as GetClosestDisplayMode()
    // above -- must agree with what LbHwCheckIsModeAvailable() already accepted
    // as available, or a mode that passed that check could still fail to
    // actually apply here.
    if (SDL_GetClosestFullscreenDisplayMode(disp_id, w, h, 0.0, true, &dm))
        return SDL_SetWindowFullscreenMode(lbWindow, &dm) ? 0 : -1;
    // No matching exclusive mode — fall back to desktop (borderless) fullscreen.
    return SDL_SetWindowFullscreenMode(lbWindow, nullptr) ? 0 : -1;
}

void WindowSystemSDL::SetWindowSize(int64_t w, int64_t h)
{
    if (lbWindow != nullptr)
        SDL_SetWindowSize(lbWindow, w, h);
}

int64_t WindowSystemSDL::SetWindowFullscreen(uint64_t flags)
{
    if (!lbWindow)
        return -1;
    if (flags == 0)
        return SDL_SetWindowFullscreen(lbWindow, false) ? 0 : -1; // windowed
    if (flags & KFX_WF_FULLSCREEN_DESKTOP)
    {
        // Desktop (borderless) fullscreen at the native resolution.
        SDL_SetWindowFullscreenMode(lbWindow, nullptr);
        return SDL_SetWindowFullscreen(lbWindow, true) ? 0 : -1;
    }
    // Exclusive fullscreen: the specific mode is applied via SetWindowDisplayMode();
    // here we just enter fullscreen.
    return SDL_SetWindowFullscreen(lbWindow, true) ? 0 : -1;
}

void WindowSystemSDL::SetWindowBordered(int64_t bordered)
{
    if (lbWindow != nullptr)
        SDL_SetWindowBordered(lbWindow, bordered ? true : false);
}

void WindowSystemSDL::SetWindowPosition(int64_t x, int64_t y)
{
    if (lbWindow != nullptr)
        SDL_SetWindowPosition(lbWindow, x, y);
}

bool WindowSystemSDL::CreateWindow(const char* title, int64_t x, int64_t y, int64_t w, int64_t h, uint64_t flags)
{
    // Translate KfxWindowFlags to SDL3 window creation flags.
    SDL_WindowFlags sdl3_flags = 0;
    if (flags & (KFX_WF_FULLSCREEN_EXCLUSIVE | KFX_WF_FULLSCREEN_DESKTOP))
        sdl3_flags |= SDL_WINDOW_FULLSCREEN;
    if (flags & KFX_WF_BORDERLESS) sdl3_flags |= SDL_WINDOW_BORDERLESS;
    if (flags & KFX_WF_HIDDEN)     sdl3_flags |= SDL_WINDOW_HIDDEN;

    lbWindow = SDL_CreateWindow(title, w, h, sdl3_flags);
    if (!lbWindow)
        return false;
    ApplyWindowIcon(lbWindow);

    // A window created with SDL_WINDOW_FULLSCREEN starts as desktop fullscreen,
    // which is exactly what a desktop-fullscreen mode wants; an exclusive mode is
    // applied later via SetWindowDisplayMode(). Windowed modes get their position.
    if (!(sdl3_flags & SDL_WINDOW_FULLSCREEN))
        SDL_SetWindowPosition(lbWindow, x, y);
    return true;
}

bool WindowSystemSDL::RecreateForSoftwareRenderer()
{
    if (lbWindow == nullptr)
        return false;
    SDL_WindowFlags cur_flags = SDL_GetWindowFlags(lbWindow);
    if (!(cur_flags & (SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN)))
        return true;

    int w = 0, h = 0;
    SDL_GetWindowSize(lbWindow, &w, &h);
    int x = SDL_WINDOWPOS_UNDEFINED, y = SDL_WINDOWPOS_UNDEFINED;
    SDL_GetWindowPosition(lbWindow, &x, &y);
    char title[256];
    { const char* cur = SDL_GetWindowTitle(lbWindow); snprintf(title, sizeof(title), "%s", cur ? cur : ""); }
    SDL_WindowFlags new_flags = cur_flags & ~(SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_VULKAN);

    SDL_DestroyWindow(lbWindow);
    lbWindow = SDL_CreateWindow(title, w, h, new_flags);
    if (!lbWindow) {
        ERRORLOG("WindowSystemSDL::RecreateForSoftwareRenderer failed: %s", SDL_GetError());
        return false;
    }
    ApplyWindowIcon(lbWindow);
    SDL_SetWindowPosition(lbWindow, x, y);
    SDL_ShowWindow(lbWindow);
    return true;
}

bool WindowSystemSDL::RecreateForVulkanRenderer()
{
    // No Vulkan window in this build; kept for interface parity with develop.
    if (lbWindow == nullptr)
        return false;
    if (!(SDL_GetWindowFlags(lbWindow) & SDL_WINDOW_VULKAN))
        return true;
    return true;
}

int64_t WindowSystemSDL::GetDisplayRefreshRate() const
{
    if (lbWindow == nullptr)
        return 0;
    SDL_DisplayID disp_id = SDL_GetDisplayForWindow(lbWindow);
    if (disp_id == 0)
        return 0;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(disp_id);
    if (mode && mode->refresh_rate > 0)
        return (int64_t)(mode->refresh_rate + 0.5);
    return 0;
}

// SDL_GetFullscreenDisplayModes() lists one entry per (width, height,
// refresh rate, pixel density) combination -- a resolution picker only
// wants distinct (width, height) pairs, so both methods below walk the
// same list and skip any (w, h) already seen earlier in it. O(n^2) in the
// mode count, which SDL reports as at most a few dozen even on unusual
// setups, so this is not worth a cache.
static bool is_duplicate_resolution(SDL_DisplayMode** modes, int64_t upto, int64_t w, int64_t h)
{
    for (int64_t j = 0; j < upto; j++)
    {
        if ((modes[j]->w == w) && (modes[j]->h == h))
            return true;
    }
    return false;
}

int64_t WindowSystemSDL::GetFullscreenDisplayModeCount(int64_t display) const
{
    SDL_DisplayID disp_id = (display > 0) ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(disp_id, &count);
    if (!modes)
        return 0;
    int64_t distinct = 0;
    for (int64_t i = 0; i < count; i++)
    {
        if (!is_duplicate_resolution(modes, i, modes[i]->w, modes[i]->h))
            distinct++;
    }
    SDL_free(modes);
    return distinct;
}

bool WindowSystemSDL::GetFullscreenDisplayModeAt(int64_t display, int64_t index, int64_t* out_w, int64_t* out_h) const
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    if (index < 0)
        return false;
    SDL_DisplayID disp_id = (display > 0) ? (SDL_DisplayID)display : SDL_GetPrimaryDisplay();
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(disp_id, &count);
    if (!modes)
        return false;
    int64_t distinct = -1;
    bool found = false;
    for (int64_t i = 0; i < count; i++)
    {
        if (is_duplicate_resolution(modes, i, modes[i]->w, modes[i]->h))
            continue;
        distinct++;
        if (distinct == index)
        {
            if (out_w) *out_w = modes[i]->w;
            if (out_h) *out_h = modes[i]->h;
            found = true;
            break;
        }
    }
    SDL_free(modes);
    return found;
}
