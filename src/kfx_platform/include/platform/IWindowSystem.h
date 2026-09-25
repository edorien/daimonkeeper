#ifndef IWINDOWSYSTEM_H
#define IWINDOWSYSTEM_H

#include <stdint.h>
/** Abstract interface for platform windowing, focus, and OS cursor management.
 *
 */
class IWindowSystem {
public:
    virtual ~IWindowSystem() = default;

    // ----- Focus / activity -----

    /** Returns true if the application window currently has OS focus.
     *  On consoles that own the display exclusively this always returns true. */
    virtual bool IsAppActive() const { return true; }

    /** Called by the SDL event loop when the window gains focus. */
    virtual void OnFocusGained() {}

    /** Called by the SDL event loop when the window loses focus. */
    virtual void OnFocusLost() {}

    /** Returns true if the platform has a real OS-managed cursor that can be
     *  grabbed, hidden, and warped (e.g. SDL desktop).  Returns false on console
     *  platforms where the "cursor" is a virtual game-layer concept.
     *
     *  Any function that applies OS cursor policy should early-return when this
     *  returns false. */
    virtual bool HasOSCursor() const { return false; }

    /** Grab or release the OS cursor. */
    virtual void SetCursorGrab(bool /*grab*/) {}

    /** Select how a subsequent SetCursorGrab(true) locks the cursor */
    virtual void SetUseRelativeMouse(bool /*relative*/) {}

    /** Show or hide the OS cursor. */
    virtual void SetCursorVisible(bool /*visible*/) {}

    /** Warp the cursor to (x, y) in game-surface coordinates. */
    virtual void WarpCursor(int64_t /*x*/, int64_t /*y*/) {}

    /** True if the OS cursor is within the window bounds. 
     * On platforms that own the display exclusively the cursor can never leave
     * so the default is true. */
    virtual bool IsCursorInWindow() const { return true; }

    // ----- Window management -----

    virtual bool HasWindow() const { return false; }
    virtual uint64_t GetWindowFlags() const { return 0; }
    virtual void GetWindowSize(int64_t* out_w, int64_t* out_h) const
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
    }
    virtual int64_t GetWindowDisplayIndex() const { return -1; }
    virtual int64_t GetNumVideoDisplays() const { return 0; }
    virtual int64_t GetDesktopDisplayMode(int64_t /*display*/, int64_t* out_w, int64_t* out_h) const
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return -1;
    }
    virtual int64_t GetDisplayBounds(int64_t /*display*/, int64_t* out_x, int64_t* out_y, int64_t* out_w, int64_t* out_h) const
    {
        if (out_x) *out_x = 0;
        if (out_y) *out_y = 0;
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return -1;
    }
    virtual int64_t GetClosestDisplayMode(int64_t /*display*/, int64_t /*desired_w*/, int64_t /*desired_h*/, int64_t* out_w, int64_t* out_h) const
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return 0;
    }
    virtual int64_t SetWindowDisplayMode(int64_t /*w*/, int64_t /*h*/) { return -1; }
    virtual void SetWindowSize(int64_t /*w*/, int64_t /*h*/) {}
    virtual int64_t SetWindowFullscreen(uint64_t /*flags*/) { return -1; }
    virtual void SetWindowBordered(int64_t /*bordered*/) {}
    virtual void SetWindowPosition(int64_t /*x*/, int64_t /*y*/) {}
    virtual bool CreateWindow(const char* /*title*/, int64_t /*x*/, int64_t /*y*/, int64_t /*w*/, int64_t /*h*/, uint64_t /*flags*/) { return false; }

    /** Recreate the window without SDL_WINDOW_OPENGL so that SDL_GetWindowSurface()
     *  can be used for software rendering.  No-op (returns true) on platforms where
     *  the window is not OpenGL-flagged or where this is not applicable. */
    virtual bool RecreateForSoftwareRenderer() { return true; }

    /** Recreate the window without SDL_WINDOW_VULKAN so that the Vulkan surface is
     *  released before switching away from the Vulkan backend.  No-op (returns
     *  true) on platforms where the window is not Vulkan-flagged. */
    virtual bool RecreateForVulkanRenderer() { return true; }

    // ----- Display info -----

    /** Returns the refresh rate (Hz) of the display the game window is on.
     *  Returns 0 when unavailable or not applicable (consoles with fixed rate). */
    virtual int64_t GetDisplayRefreshRate() const { return 0; }

    /** Number of distinct fullscreen resolutions (width x height,
     *  deduplicated across refresh rates -- callers only want a resolution
     *  picker, not one entry per refresh rate) the given display supports.
     *  display <= 0 means the primary display. 0 when unavailable.
     *  docs/refactor/renderer/04-imgui-gui-foundation.md §6.2/Phase G:
     *  backs the INGAME_RES settings-screen picker. */
    virtual int64_t GetFullscreenDisplayModeCount(int64_t /*display*/) const { return 0; }
    /** Fills out_w/out_h with the index'th distinct resolution (0-based, in
     *  whatever order the platform reports them -- SDL: largest first) for
     *  the given display. Returns false (leaving out_w/out_h at 0) if index
     *  is out of range. */
    virtual bool GetFullscreenDisplayModeAt(int64_t /*display*/, int64_t /*index*/, int64_t* out_w, int64_t* out_h) const
    {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return false;
    }

    // ----- Per-frame poll -----

    /** Called once per event-poll cycle.  SDL: no-op (input arrives via events). */
    virtual void PollInput() {}
};

#endif // IWINDOWSYSTEM_H
