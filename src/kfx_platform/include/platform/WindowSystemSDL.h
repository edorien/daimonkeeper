#ifndef WINDOW_SYSTEM_SDL_H
#define WINDOW_SYSTEM_SDL_H

#include <stdint.h>
#include "platform/IWindowSystem.h"

struct SDL_Window;  // forward declaration; full type in WindowSystemSDL.cpp

/** SDL3 desktop window-system implementation.
 *
 */
class WindowSystemSDL : public IWindowSystem {
public:
    bool IsAppActive() const override;
    void OnFocusGained() override;
    void OnFocusLost() override;

    bool HasOSCursor() const override { return true; }
    void SetCursorGrab(bool grab) override;
    void SetUseRelativeMouse(bool relative) override;
    void SetCursorVisible(bool visible) override;
    void WarpCursor(int64_t x, int64_t y) override;
    bool IsCursorInWindow() const override;

    bool HasWindow() const override;
    SDL_Window* GetSDLWindow() const;
    uint64_t GetWindowFlags() const override;
    void GetWindowSize(int64_t* out_w, int64_t* out_h) const override;
    int64_t GetWindowDisplayIndex() const override;
    int64_t GetNumVideoDisplays() const override;
    int64_t GetDesktopDisplayMode(int64_t display, int64_t* out_w, int64_t* out_h) const override;
    int64_t GetDisplayBounds(int64_t display, int64_t* out_x, int64_t* out_y, int64_t* out_w, int64_t* out_h) const override;
    int64_t GetClosestDisplayMode(int64_t display, int64_t desired_w, int64_t desired_h, int64_t* out_w, int64_t* out_h) const override;
    int64_t SetWindowDisplayMode(int64_t w, int64_t h) override;
    void SetWindowSize(int64_t w, int64_t h) override;
    int64_t SetWindowFullscreen(uint64_t flags) override;
    void SetWindowBordered(int64_t bordered) override;
    void SetWindowPosition(int64_t x, int64_t y) override;
    bool CreateWindow(const char* title, int64_t x, int64_t y, int64_t w, int64_t h, uint64_t flags) override;
    bool RecreateForSoftwareRenderer() override;
    bool RecreateForVulkanRenderer() override;

    // ----- Display info -----
    int64_t GetDisplayRefreshRate() const override;
    int64_t GetFullscreenDisplayModeCount(int64_t display) const override;
    bool GetFullscreenDisplayModeAt(int64_t display, int64_t index, int64_t* out_w, int64_t* out_h) const override;

    // PollInput is a no-op: SDL delivers mouse input via events.

private:
    void ApplyOsCursorPolicy();

    bool m_appActive = true;
    bool m_useRelativeMouse = true;
};

/** Shared singleton desktop window system. */
WindowSystemSDL* GetSDLWindowSystem();

#endif // WINDOW_SYSTEM_SDL_H
