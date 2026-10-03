/******************************************************************************/
// Bullfrog Engine Emulation Library - for use to remake classic games like
// Syndicate Wars, Magic Carpet or Dungeon Keeper.
/******************************************************************************/
/** @file PlatformManager.cpp
 *     C-callable windowing facade delegating to the desktop window system.
 */
/******************************************************************************/
#include "pre_inc.h"
#include "platform/PlatformManager.h"
#include "platform/WindowSystemSDL.h"
#include "platform/IPlatform.h"
#include "platform/PlatformWindows.h"
#include "platform/PlatformLinux.h"
#include "platform/FileFind.h"
#include "bflib_fileio.h"
#include "bflib_video.h"
#include "cdrom.h"
#include "steam_api.hpp"
#include <SDL3/SDL.h>
#include "post_inc.h"

/******************************************************************************/

IWindowSystem* IPlatform::GetWindowSystem() { return GetSDLWindowSystem(); }

IPlatform* GetPlatform()
{
#if defined(_WIN32)
    static PlatformWindows s_platform;
#else
    static PlatformLinux s_platform;
#endif
    return &s_platform;
}

/******************************************************************************/

extern "C" const char * PlatformManager_GetOSVersion(void)   { return GetPlatform()->GetOSVersion(); }
extern "C" const void * PlatformManager_GetImageBase(void)   { return GetPlatform()->GetImageBase(); }
extern "C" const char * PlatformManager_GetWineVersion(void) { return GetPlatform()->GetWineVersion(); }
extern "C" const char * PlatformManager_GetWineHost(void)    { return GetPlatform()->GetWineHost(); }

/******************************************************************************/

// The directory walk is per-OS; iterating and freeing the result is not.
extern "C" struct TbFileFind * LbFileFindFirst(const char * filespec, struct TbFileEntry * fentry)
{
    return GetPlatform()->FileFindFirst(filespec, fentry);
}

extern "C" int64_t LbFileFindNext(struct TbFileFind * ffind, struct TbFileEntry * fentry)
{
    if (!ffind) {
        return -1;
    }
    ffind->index++;
    if (ffind->index >= ffind->names.size()) {
        return -1;
    }
    fentry->Filename = ffind->names[ffind->index].second.c_str();
    return 1;
}

extern "C" void LbFileFindEnd(struct TbFileFind * ffind)
{
    delete ffind;
}

namespace {
struct SubdirCollect { char* out; int64_t stride; int64_t max; int64_t count; };

SDL_EnumerationResult SDLCALL subdir_enum_cb(void* ud, const char* dirname, const char* fname)
{
    SubdirCollect* c = static_cast<SubdirCollect*>(ud);
    if (c->count >= c->max)
        return SDL_ENUM_SUCCESS;
    char full[1024];
    SDL_snprintf(full, sizeof(full), "%s/%s", dirname, fname);
    SDL_PathInfo info;
    if (SDL_GetPathInfo(full, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
    {
        SDL_strlcpy(c->out + (size_t)c->count * c->stride, fname, c->stride);
        c->count++;
    }
    return SDL_ENUM_CONTINUE;
}
} // namespace

extern "C" int64_t PlatformManager_ListSubdirectories(const char* path, char* out, int64_t stride, int64_t max)
{
    if (out == nullptr || stride <= 0 || max <= 0)
        return 0;
    SubdirCollect c { out, stride, max, 0 };
    SDL_EnumerateDirectory(path, subdir_enum_cb, &c);
    return c.count;
}

/******************************************************************************/

extern "C" void   SetRedbookVolume(SoundVolume value) { GetPlatform()->SetRedbookVolume(value); }
extern "C" TbBool PlayRedbookTrack(int64_t track)         { return GetPlatform()->PlayRedbookTrack(track); }
extern "C" void   PauseRedbookTrack(void)             { GetPlatform()->PauseRedbookTrack(); }
extern "C" void   ResumeRedbookTrack(void)            { GetPlatform()->ResumeRedbookTrack(); }
extern "C" void   StopRedbookTrack(void)              { GetPlatform()->StopRedbookTrack(); }

extern "C" int64_t  steam_api_init(void)     { return GetPlatform()->InitSteam(); }
extern "C" void steam_api_shutdown(void) { GetPlatform()->ShutdownSteam(); }

/******************************************************************************/

extern "C" int64_t PlatformManager_InitVideo(void)
{
    return GetPlatform()->VideoInit() ? 1 : 0;
}

extern "C" int64_t PlatformManager_HasWindow(void)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return (ws && ws->HasWindow()) ? 1 : 0;
}

// -headless (main.cpp): SDL's dummy driver reports zero real display
// modes, so LbHwCheckIsModeAvailable() (bflib_video.c) would reject every
// resolution -- including the 320x200 failsafe -- and fail startup
// entirely. VideoDisabled short-circuits through this same
// already-existing "trust the requested mode" escape hatch.
extern "C" int64_t PlatformManager_ForcesAllModesAvailable(void) { return (VideoDisabled || GetPlatform()->ForcesAllModesAvailable()) ? 1 : 0; }

extern "C" uint64_t PlatformManager_GetWindowFlags(void)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetWindowFlags() : 0;
}

extern "C" int64_t PlatformManager_GetWindowDisplayIndex(void)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetWindowDisplayIndex() : -1;
}

extern "C" int64_t PlatformManager_GetNumVideoDisplays(void)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetNumVideoDisplays() : 0;
}

extern "C" int64_t PlatformManager_GetDesktopDisplayMode(int64_t display, int64_t* out_w, int64_t* out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetDesktopDisplayMode(display, out_w, out_h) : -1;
}

extern "C" int64_t PlatformManager_GetDisplayBounds(int64_t display, int64_t* out_x, int64_t* out_y, int64_t* out_w, int64_t* out_h)
{
    if (out_x) *out_x = 0;
    if (out_y) *out_y = 0;
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetDisplayBounds(display, out_x, out_y, out_w, out_h) : -1;
}

extern "C" int64_t PlatformManager_GetClosestDisplayMode(int64_t display, int64_t desired_w, int64_t desired_h, int64_t* out_w, int64_t* out_h)
{
    if (out_w) *out_w = 0;
    if (out_h) *out_h = 0;
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetClosestDisplayMode(display, desired_w, desired_h, out_w, out_h) : 0;
}

extern "C" int64_t PlatformManager_SetWindowDisplayMode(int64_t w, int64_t h)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->SetWindowDisplayMode(w, h) : -1;
}

extern "C" void PlatformManager_SetWindowSize(int64_t w, int64_t h)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    if (ws) ws->SetWindowSize(w, h);
}

extern "C" int64_t PlatformManager_SetWindowFullscreen(uint64_t flags)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->SetWindowFullscreen(flags) : -1;
}

extern "C" void PlatformManager_SetWindowBordered(int64_t bordered)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    if (ws) ws->SetWindowBordered(bordered);
}

extern "C" void PlatformManager_SetWindowPosition(int64_t x, int64_t y)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    if (ws) ws->SetWindowPosition(x, y);
}

extern "C" int64_t PlatformManager_CreateWindow(const char* title, int64_t x, int64_t y, int64_t w, int64_t h, uint64_t flags)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return (ws && ws->CreateWindow(title, x, y, w, h, flags)) ? 1 : 0;
}

extern "C" void PlatformManager_WarpCursor(int64_t x, int64_t y)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    if (ws) ws->WarpCursor(x, y);
}

extern "C" int64_t PlatformManager_IsCursorInWindow(void)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return (ws && ws->IsCursorInWindow()) ? 1 : 0;
}

extern "C" int64_t PlatformManager_GetDisplayRefreshRate(void)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetDisplayRefreshRate() : 0;
}

extern "C" int64_t PlatformManager_GetFullscreenDisplayModeCount(int64_t display)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    return ws ? ws->GetFullscreenDisplayModeCount(display) : 0;
}

extern "C" int64_t PlatformManager_GetFullscreenDisplayModeAt(int64_t display, int64_t index, int64_t* out_w, int64_t* out_h)
{
    IWindowSystem* ws = GetSDLWindowSystem();
    if (!ws)
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return 0;
    }
    return ws->GetFullscreenDisplayModeAt(display, index, out_w, out_h) ? 1 : 0;
}
