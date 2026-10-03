#ifndef PLATFORM_MANAGER_H
#define PLATFORM_MANAGER_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// OS information — facade over IPlatform::Get*.
const char * PlatformManager_GetOSVersion(void);
const void * PlatformManager_GetImageBase(void);
const char * PlatformManager_GetWineVersion(void);
const char * PlatformManager_GetWineHost(void);

int64_t          PlatformManager_InitVideo(void);
int64_t          PlatformManager_HasWindow(void);
int64_t          PlatformManager_ForcesAllModesAvailable(void);

uint64_t PlatformManager_GetWindowFlags(void);
// Returns the window's SDL display ID (opaque), not a 0-based index.
int64_t          PlatformManager_GetWindowDisplayIndex(void);
int64_t          PlatformManager_GetNumVideoDisplays(void);
int64_t          PlatformManager_GetDesktopDisplayMode(int64_t display, int64_t* out_w, int64_t* out_h);
int64_t          PlatformManager_GetDisplayBounds(int64_t display, int64_t* out_x, int64_t* out_y, int64_t* out_w, int64_t* out_h);
int64_t          PlatformManager_GetClosestDisplayMode(int64_t display, int64_t desired_w, int64_t desired_h, int64_t* out_w, int64_t* out_h);
int64_t          PlatformManager_SetWindowDisplayMode(int64_t w, int64_t h);
void         PlatformManager_SetWindowSize(int64_t w, int64_t h);
int64_t          PlatformManager_SetWindowFullscreen(uint64_t flags);
void         PlatformManager_SetWindowBordered(int64_t bordered);
void         PlatformManager_SetWindowPosition(int64_t x, int64_t y);
int64_t          PlatformManager_CreateWindow(const char* title, int64_t x, int64_t y, int64_t w, int64_t h, uint64_t flags);
void         PlatformManager_WarpCursor(int64_t x, int64_t y);
int64_t          PlatformManager_IsCursorInWindow(void);
int64_t          PlatformManager_GetDisplayRefreshRate(void);
int64_t          PlatformManager_GetFullscreenDisplayModeCount(int64_t display);
int64_t          PlatformManager_GetFullscreenDisplayModeAt(int64_t display, int64_t index, int64_t* out_w, int64_t* out_h);

// Lists the immediate sub-directory names of `path` into `out` (a flat
// buffer of `max` slots, `stride` bytes each; each name NUL-terminated and
// truncated to fit). Returns the number of names written. LbFileFindFirst
// deliberately drops directories on every platform, so this is separate.
int64_t          PlatformManager_ListSubdirectories(const char* path, char* out, int64_t stride, int64_t max);

#ifdef __cplusplus
}
#endif

#endif // PLATFORM_MANAGER_H
